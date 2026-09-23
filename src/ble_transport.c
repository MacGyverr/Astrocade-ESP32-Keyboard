#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_err.h"
#include "esp_log.h"
#include "ble_transport.h"
#include "ble_report.h"
#include "keyboard_status.h"
#include "esp_hid_common.h"
#include <stdatomic.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/hid/ble_svc_hid.h"

static ble_transport_config_t s_config;
#define BLE_INFO(tag, ...) bleTransportLog(__VA_ARGS__)
#define BLE_DETAIL(tag, ...) do { if (s_config.verbose) bleTransportLog(__VA_ARGS__); } while (0)

#define BLE_APPEARANCE_HID_GENERIC 0x03C0
#define BLE_APPEARANCE_HID_KEYBOARD 0x03C1
#define MAX_HID_CHARS 40
#define MAX_NOTIFY_BYTES 64

void ble_store_config_init(void);

typedef struct {
    bool valid;
    ble_addr_t addr;
    int rssi;
    uint16_t appearance;
    bool hid_service;
    bool keyboardish;
    bool directed_to_us;
    bool bonded;
    char name[BLE_HS_ADV_MAX_SZ + 1];
} candidate_t;

typedef struct {
    uint16_t def_handle;
    uint16_t val_handle;
    uint16_t end_handle;
    uint16_t ccc_handle;
    uint16_t report_ref_handle;
    uint16_t uuid16;
    uint8_t props;
    uint8_t report_id;
    uint8_t report_type;
    bool has_report_ref;
    bool subscribed;
    bool keyboard;
    uint16_t report_length;
} hid_chr_t;

typedef struct {
    bool connected;
    bool opening;
    bool discovering;
    bool ready;
    uint16_t conn_handle;
    uint16_t hid_start;
    uint16_t hid_end;
    hid_chr_t chrs[MAX_HID_CHARS];
    size_t chr_count;
    size_t desc_index;
    size_t ref_index;
    size_t sub_index;
    uint16_t input_handle;
    uint16_t led_handle;
    uint8_t led_props;
} hid_state_t;

static candidate_t s_best;
static hid_state_t s_hid;
static struct ble_npl_callout s_scan_retry;
static struct ble_npl_callout s_open_timeout;
static struct ble_npl_event s_led_event;
static struct ble_npl_event s_clear_event;
static atomic_uchar s_leds;
static bool s_enrollment = true;
static bool s_clearing;
static bool s_initialized;
static uint8_t s_report_map[1024];
static size_t s_report_map_length;

static void schedule_scan(void);
static void finish_clear(void);
static void connect_to_best(void);
static void read_report_map(uint16_t conn_handle);

static uint8_t s_own_addr_type = BLE_OWN_ADDR_PUBLIC;
static ble_addr_t s_own_id_addr = {
    .type = BLE_ADDR_PUBLIC,
};

static int gap_event(struct ble_gap_event *event, void *arg);
static int hid_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *service, void *arg);
static int hid_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      const struct ble_gatt_chr *chr, void *arg);
static int hid_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                      void *arg);
static int report_ref_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg);
static int subscribe_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg);
static void start_hid_discovery(uint16_t conn_handle);
static void discover_next_descriptor(uint16_t conn_handle);
static void read_next_report_ref(uint16_t conn_handle);
static void subscribe_next(uint16_t conn_handle);

static const char *addr_type_str(uint8_t type)
{
    switch (type) {
    case BLE_ADDR_PUBLIC:
        return "public";
    case BLE_ADDR_RANDOM:
        return "random";
    case BLE_ADDR_PUBLIC_ID:
        return "public-id";
    case BLE_ADDR_RANDOM_ID:
        return "random-id";
    default:
        return "unknown";
    }
}

static const char *adv_event_type_str(uint8_t type)
{
    switch (type) {
    case BLE_HCI_ADV_RPT_EVTYPE_ADV_IND:
        return "adv";
    case BLE_HCI_ADV_RPT_EVTYPE_DIR_IND:
        return "directed";
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_IND:
        return "scan-ind";
    case BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND:
        return "nonconn";
    case BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP:
        return "scan-rsp";
    default:
        return "unknown";
    }
}

static void addr_val_to_str(const uint8_t addr[6], char out[18])
{
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static void addr_to_str(const ble_addr_t *addr, char out[18])
{
    addr_val_to_str(addr->val, out);
}

static bool addr_matches_own(const ble_addr_t *addr)
{
    return addr->type == s_own_id_addr.type &&
           memcmp(addr->val, s_own_id_addr.val, sizeof(s_own_id_addr.val)) == 0;
}

static bool peer_is_bonded(const ble_addr_t *addr)
{
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count, CONFIG_BT_NIMBLE_MAX_BONDS) != 0) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        /* Resolved identity types 2/3 refer to public/random types 0/1. */
        if ((peers[i].type & 1) == (addr->type & 1) &&
            memcmp(peers[i].val, addr->val, sizeof(addr->val)) == 0) {
            return true;
        }
    }
    return false;
}

static void log_bonds(void)
{
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int count = 0;
    int rc = ble_store_util_bonded_peers(peers, &count, CONFIG_BT_NIMBLE_MAX_BONDS);
    BLE_INFO(TAG, "saved bonds=%d read_rc=%d", count, rc);
    for (int i = 0; rc == 0 && i < count; i++) {
        char addr[18];
        addr_to_str(&peers[i], addr);
        BLE_INFO(TAG, "saved peer %s/%s", addr, addr_type_str(peers[i].type));
    }
}

static bool name_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) {
        return false;
    }

    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < needle_len && p[i] &&
               (char)tolower((unsigned char)p[i]) == (char)tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == needle_len) {
            return true;
        }
    }
    return false;
}

static bool adv_has_hid_service(const struct ble_hs_adv_fields *fields)
{
    for (int i = 0; i < fields->num_uuids16; i++) {
        if (ble_uuid_u16(&fields->uuids16[i].u) == BLE_SVC_HID_UUID16) {
            return true;
        }
    }
    return false;
}

static bool appearance_is_keyboardish(uint16_t appearance)
{
    return appearance == BLE_APPEARANCE_HID_GENERIC ||
           appearance == BLE_APPEARANCE_HID_KEYBOARD;
}

static bool name_is_keyboardish(const char *name)
{
    return name_contains_ci(name, "keyboard") ||
           name_contains_ci(name, "k380") ||
           name_contains_ci(name, "pebble") ||
           name_contains_ci(name, "keys");
}

static int candidate_score(const candidate_t *candidate)
{
    int score = candidate->rssi;
    if (candidate->bonded) {
        score += 3000;
    }
    if (candidate->directed_to_us) {
        score += 1200;
    }
    if (candidate->hid_service) {
        score += 1000;
    }
    if (candidate->appearance == BLE_APPEARANCE_HID_KEYBOARD) {
        score += 400;
    } else if (candidate->appearance == BLE_APPEARANCE_HID_GENERIC) {
        score += 200;
    }
    if (candidate->keyboardish) {
        score += 300;
    }
    return score;
}

static void maybe_note_candidate(const candidate_t *candidate)
{
    if (!s_enrollment && !candidate->bonded) {
        return;
    }
    if (!candidate->hid_service && !candidate->keyboardish &&
        !candidate->directed_to_us && !candidate->bonded) {
        return;
    }

    if (!s_best.valid || candidate_score(candidate) > candidate_score(&s_best)) {
        s_best = *candidate;
        s_best.valid = true;
        char addr[18];
        addr_to_str(&candidate->addr, addr);
        BLE_DETAIL(TAG,
                 "candidate %s type=%s rssi=%d directed_to_us=%d hid=%d appearance=0x%04X name=\"%s\" score=%d",
                 addr, addr_type_str(candidate->addr.type), candidate->rssi,
                 candidate->directed_to_us, candidate->hid_service, candidate->appearance,
                 candidate->name, candidate_score(candidate));
    }
}

static void handle_advertisement(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields = {0};
    bool parsed = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0;

    candidate_t candidate = {
        .addr = disc->addr,
        .rssi = disc->rssi,
        .bonded = peer_is_bonded(&disc->addr),
    };

    if (parsed) {
        candidate.appearance = fields.appearance_is_present ? fields.appearance : 0;
        candidate.hid_service = adv_has_hid_service(&fields);

        if (fields.name && fields.name_len) {
            size_t len = fields.name_len;
            if (len >= sizeof(candidate.name)) {
                len = sizeof(candidate.name) - 1;
            }
            memcpy(candidate.name, fields.name, len);
            candidate.name[len] = '\0';
        }
    }

    candidate.directed_to_us =
        disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND &&
        addr_matches_own(&disc->direct_addr);
    candidate.keyboardish = appearance_is_keyboardish(candidate.appearance) ||
                            name_is_keyboardish(candidate.name);

if (s_config.verbose) {
    if (candidate.name[0] || candidate.hid_service || candidate.keyboardish ||
        disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND || candidate.directed_to_us) {
        char addr[18];
        char direct_addr[18];
        addr_to_str(&candidate.addr, addr);
        addr_to_str(&disc->direct_addr, direct_addr);
        BLE_DETAIL(TAG,
                 "adv ev=%s peer=%s type=%s rssi=%d len=%u direct=%s/%s directed_to_us=%d hid=%d appearance=0x%04X name=\"%s\"",
                 adv_event_type_str(disc->event_type), addr, addr_type_str(candidate.addr.type),
                 candidate.rssi, disc->length_data, direct_addr,
                 addr_type_str(disc->direct_addr.type), candidate.directed_to_us,
                 candidate.hid_service, candidate.appearance, candidate.name);
    }
}

    maybe_note_candidate(&candidate);
    if (candidate.bonded && !s_hid.connected && !s_hid.opening &&
        (disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND ||
         disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND)) {
        /* Legacy directed reports need not contain the target address or HID data. */
        if (ble_gap_disc_cancel() == 0) {
            s_best = candidate;
            s_best.valid = true;
            BLE_INFO(TAG, "reconnect: saved peer seen; connecting immediately");
            s_enrollment = false;
            connect_to_best();
        }
    }
}

static uint16_t om_copy(struct os_mbuf *om, uint8_t *out, uint16_t out_len)
{
    uint16_t len = OS_MBUF_PKTLEN(om);
    if (len > out_len) {
        len = out_len;
    }
    if (len) {
        os_mbuf_copydata(om, 0, len, out);
    }
    return len;
}

static const char *hid_chr_name(uint16_t uuid16)
{
    switch (uuid16) {
    case BLE_SVC_HID_CHR_UUID16_REPORT_MAP:
        return "report-map";
    case BLE_SVC_HID_CHR_UUID16_HID_INFO:
        return "hid-info";
    case BLE_SVC_HID_CHR_UUID16_HID_CTRL_PT:
        return "hid-control";
    case BLE_SVC_HID_CHR_UUID16_RPT:
        return "report";
    case BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE:
        return "protocol-mode";
    case BLE_SVC_HID_CHR_UUID16_BOOT_KBD_INP:
        return "boot-keyboard-input";
    case BLE_SVC_HID_CHR_UUID16_BOOT_KBD_OUT:
        return "boot-keyboard-output";
    case BLE_SVC_HID_CHR_UUID16_BOOT_MOUSE_INP:
        return "boot-mouse-input";
    default:
        return "unknown";
    }
}

static const hid_chr_t *find_chr_by_value_handle(uint16_t value_handle)
{
    for (size_t i = 0; i < s_hid.chr_count; i++) {
        if (s_hid.chrs[i].val_handle == value_handle) {
            return &s_hid.chrs[i];
        }
    }
    return NULL;
}

static bool is_input_report_char(const hid_chr_t *chr)
{
    return chr->val_handle == s_hid.input_handle &&
           (chr->props & BLE_GATT_CHR_PROP_NOTIFY);
}

static void handle_notify(const struct ble_gap_event *event)
{
    if (!s_hid.ready || event->notify_rx.conn_handle != s_hid.conn_handle ||
        event->notify_rx.attr_handle != s_hid.input_handle) {
        return;
    }
    const hid_chr_t *chr = find_chr_by_value_handle(event->notify_rx.attr_handle);
    uint8_t data[8], boot[8];
    const uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (!chr || len > sizeof(data) || len != chr->report_length) {
        BLE_DETAIL(TAG, "unsupported keyboard report length=%u", len);
        const uint8_t released[8] = {0};
        s_config.report(released);
        return;
    }
    if (om_copy(event->notify_rx.om, data, sizeof(data)) == len &&
        bleReportToBoot(data, len, boot)) {
        s_config.report(boot);
    }
}

static void log_conn_security(const char *prefix, uint16_t conn_handle)
{
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        BLE_INFO(TAG, "%s conn=%u desc lookup failed rc=%d", prefix, conn_handle, rc);
        return;
    }

    char peer[18];
    char ota[18];
    addr_to_str(&desc.peer_id_addr, peer);
    addr_to_str(&desc.peer_ota_addr, ota);
    BLE_DETAIL(TAG,
             "%s conn=%u peer_id=%s/%s peer_ota=%s/%s encrypted=%d authenticated=%d bonded=%d key_size=%d",
             prefix, conn_handle, peer, addr_type_str(desc.peer_id_addr.type),
             ota, addr_type_str(desc.peer_ota_addr.type), desc.sec_state.encrypted,
             desc.sec_state.authenticated, desc.sec_state.bonded, desc.sec_state.key_size);
}

static void reset_connection_state(void)
{
    memset(&s_hid, 0, sizeof(s_hid));
}

static void handle_connect(uint16_t conn_handle)
{
    s_hid.connected = true;
    s_hid.opening = false;
    s_hid.conn_handle = conn_handle;
    s_hid.ready = false;
    s_hid.discovering = false;
    log_conn_security("connected", conn_handle);
    ble_npl_callout_reset(&s_open_timeout, ble_npl_time_ms_to_ticks32(60000));

    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn_handle, &desc) == 0 && desc.sec_state.encrypted) {
        start_hid_discovery(conn_handle);
        return;
    }

    int rc = ble_gap_security_initiate(conn_handle);
    if (rc == BLE_HS_EALREADY) {
        BLE_DETAIL(TAG, "security already in progress");
    } else if (rc == 0) {
        BLE_DETAIL(TAG, "security initiated; waiting for encrypted link before HID discovery");
    } else {
        BLE_INFO(TAG, "security initiate failed rc=%d; disconnecting", rc);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        handle_advertisement(&event->disc);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        BLE_DETAIL(TAG, "scan complete reason=%d", event->disc_complete.reason);
        if (s_clearing) {
            return 0;
        }
        {
            const bool startup = s_enrollment;
            s_enrollment = false;
            if (s_best.valid) {
                connect_to_best();
            } else {
                if (startup) {
                    BLE_INFO(TAG, "no keyboard found during startup window");
                    s_config.state(BLE_TRANSPORT_NOT_FOUND);
                }
                schedule_scan();
            }
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            BLE_INFO(TAG, "connect ok conn=%u", event->connect.conn_handle);
            handle_connect(event->connect.conn_handle);
        } else {
            BLE_INFO(TAG, "connect failed status=%d", event->connect.status);
            reset_connection_state();
            s_config.state(BLE_TRANSPORT_NOT_FOUND);
            schedule_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        keyboardStatusSetBle(false, NULL);
        BLE_INFO(TAG, "disconnect reason=%d", event->disconnect.reason);
        ble_npl_callout_stop(&s_open_timeout);
        reset_connection_state();
        s_config.state(BLE_TRANSPORT_DISCONNECTED);
        if (s_clearing) {
            finish_clear();
        } else {
            schedule_scan();
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        BLE_INFO(TAG, "encryption change status=%d", event->enc_change.status);
        log_conn_security("security", event->enc_change.conn_handle);
        if (event->enc_change.status == 0) {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->enc_change.conn_handle, &desc) == 0 &&
                desc.sec_state.encrypted) {
                start_hid_discovery(event->enc_change.conn_handle);
            }
        } else {
            ble_gap_terminate(event->enc_change.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pkey = {
            .action = event->passkey.params.action,
        };
        int rc = 0;

        switch (event->passkey.params.action) {
        case BLE_SM_IOACT_DISP:
            pkey.passkey = s_config.passkey;
            bleTransportNotice("PAIRING: type %06" PRIu32 " on the keyboard, then press Enter", s_config.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            BLE_DETAIL(TAG, "passkey display injected rc=%d", rc);
            break;

        case BLE_SM_IOACT_INPUT:
            pkey.passkey = s_config.passkey;
            bleTransportNotice("PAIRING: injecting passkey %06" PRIu32, s_config.passkey);
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            BLE_DETAIL(TAG, "passkey input injected rc=%d", rc);
            break;

        case BLE_SM_IOACT_NUMCMP:
            BLE_INFO(TAG, "PAIRING: numeric comparison %06" PRIu32 " auto-accepted",
                     event->passkey.params.numcmp);
            pkey.numcmp_accept = 1;
            rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
            BLE_DETAIL(TAG, "numeric comparison rc=%d", rc);
            break;

        default:
            BLE_INFO(TAG, "unhandled passkey action=%u", event->passkey.params.action);
            break;
        }
        return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_RX:
        handle_notify(event);
        return 0;

    case BLE_GAP_EVENT_MTU:
        BLE_DETAIL(TAG, "mtu conn=%u value=%u", event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        BLE_DETAIL(TAG, "connection update conn=%u status=%d",
                 event->conn_update.conn_handle, event->conn_update.status);
        return 0;

    default:
        return 0;
    }
}

static void start_hid_discovery(uint16_t conn_handle)
{
    if (s_hid.discovering || s_hid.ready) {
        return;
    }

    s_hid.discovering = true;
    s_hid.hid_start = 0;
    s_hid.hid_end = 0;
    s_hid.chr_count = 0;
    s_hid.desc_index = 0;
    s_hid.ref_index = 0;
    s_hid.sub_index = 0;
    memset(s_hid.chrs, 0, sizeof(s_hid.chrs));

    BLE_DETAIL(TAG, "HID discovery starting after encryption");
    int rc = ble_gattc_disc_svc_by_uuid(conn_handle, BLE_UUID16_DECLARE(BLE_SVC_HID_UUID16),
                                        hid_svc_cb, NULL);
    if (rc != 0) {
        BLE_INFO(TAG, "HID service discovery start failed rc=%d", rc);
        s_hid.discovering = false;
    }
}

static int hid_svc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *service, void *arg)
{
    (void)arg;

    if (error->status == 0 && service) {
        s_hid.hid_start = service->start_handle;
        s_hid.hid_end = service->end_handle;
        BLE_DETAIL(TAG, "HID service handles start=0x%04X end=0x%04X",
                 s_hid.hid_start, s_hid.hid_end);
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        if (!s_hid.hid_start) {
            BLE_INFO(TAG, "HID service not found");
            s_hid.discovering = false;
            return 0;
        }

        int rc = ble_gattc_disc_all_chrs(conn_handle, s_hid.hid_start, s_hid.hid_end,
                                         hid_chr_cb, NULL);
        if (rc != 0) {
            BLE_INFO(TAG, "HID characteristic discovery start failed rc=%d", rc);
            s_hid.discovering = false;
        }
        return 0;
    }

    BLE_INFO(TAG, "HID service discovery failed status=%d att=%d",
             error->status, error->att_handle);
    s_hid.discovering = false;
    return 0;
}

static int hid_chr_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;

    if (error->status == 0 && chr) {
        if (s_hid.chr_count >= MAX_HID_CHARS) {
            BLE_INFO(TAG, "too many HID chars; skipping handle=0x%04X", chr->val_handle);
            return 0;
        }

        hid_chr_t *out = &s_hid.chrs[s_hid.chr_count++];
        out->def_handle = chr->def_handle;
        out->val_handle = chr->val_handle;
        out->uuid16 = ble_uuid_u16(&chr->uuid.u);
        out->props = chr->properties;
        BLE_DETAIL(TAG, "char[%u] %s uuid=0x%04X def=0x%04X val=0x%04X props=0x%02X",
                 (unsigned)(s_hid.chr_count - 1), hid_chr_name(out->uuid16), out->uuid16,
                 out->def_handle, out->val_handle, out->props);
        return 0;
    }

    if (error->status == BLE_HS_EDONE) {
        for (size_t i = 0; i < s_hid.chr_count; i++) {
            if (i + 1 < s_hid.chr_count && s_hid.chrs[i + 1].def_handle > 0) {
                s_hid.chrs[i].end_handle = s_hid.chrs[i + 1].def_handle - 1;
            } else {
                s_hid.chrs[i].end_handle = s_hid.hid_end;
            }
        }
        BLE_DETAIL(TAG, "characteristic discovery complete count=%u", (unsigned)s_hid.chr_count);
        s_hid.desc_index = 0;
        discover_next_descriptor(conn_handle);
        return 0;
    }

    BLE_INFO(TAG, "characteristic discovery failed status=%d att=%d",
             error->status, error->att_handle);
    s_hid.discovering = false;
    return 0;
}

static int hid_dsc_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                      uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc,
                      void *arg)
{
    size_t idx = (size_t)(uintptr_t)arg;
    (void)chr_val_handle;

    if (idx >= s_hid.chr_count) {
        return 0;
    }

    hid_chr_t *chr = &s_hid.chrs[idx];
    if (error->status == 0 && dsc) {
        uint16_t uuid16 = ble_uuid_u16(&dsc->uuid.u);
        if (uuid16 == BLE_GATT_DSC_CLT_CFG_UUID16) {
            chr->ccc_handle = dsc->handle;
        } else if (uuid16 == BLE_SVC_HID_DSC_UUID16_RPT_REF) {
            chr->report_ref_handle = dsc->handle;
        }
        BLE_DETAIL(TAG, "desc char[%u] %s dsc_uuid=0x%04X handle=0x%04X",
                 (unsigned)idx, hid_chr_name(chr->uuid16), uuid16, dsc->handle);
        return 0;
    }

    if (error->status != BLE_HS_EDONE) {
        BLE_INFO(TAG, "descriptor discovery char[%u] status=%d att=%d",
                 (unsigned)idx, error->status, error->att_handle);
    }

    s_hid.desc_index = idx + 1;
    discover_next_descriptor(conn_handle);
    return 0;
}

static void discover_next_descriptor(uint16_t conn_handle)
{
    while (s_hid.desc_index < s_hid.chr_count) {
        size_t idx = s_hid.desc_index;
        hid_chr_t *chr = &s_hid.chrs[idx];
        if (chr->val_handle >= chr->end_handle) {
            s_hid.desc_index++;
            continue;
        }

        int rc = ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, chr->end_handle,
                                         hid_dsc_cb, (void *)(uintptr_t)idx);
        if (rc == 0) {
            return;
        }

        BLE_INFO(TAG, "descriptor discovery could not start char[%u] rc=%d",
                 (unsigned)idx, rc);
        s_hid.desc_index++;
    }

    s_hid.ref_index = 0;
    read_next_report_ref(conn_handle);
}

static int report_ref_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    size_t idx = (size_t)(uintptr_t)arg;
    if (idx < s_hid.chr_count) {
        hid_chr_t *chr = &s_hid.chrs[idx];
        if (error->status == 0 && attr && attr->om) {
            uint8_t data[2] = {0};
            uint16_t len = om_copy(attr->om, data, sizeof(data));
            if (len >= 2) {
                chr->report_id = data[0];
                chr->report_type = data[1];
                chr->has_report_ref = true;
                BLE_DETAIL(TAG, "report ref char[%u] id=%u type=%u",
                         (unsigned)idx, chr->report_id, chr->report_type);
            } else {
                BLE_INFO(TAG, "report ref char[%u] short len=%u", (unsigned)idx, len);
            }
        } else {
            BLE_INFO(TAG, "report ref read char[%u] status=%d att=%d",
                     (unsigned)idx, error->status, error->att_handle);
        }
        s_hid.ref_index = idx + 1;
    }

    read_next_report_ref(conn_handle);
    return 0;
}

static void read_next_report_ref(uint16_t conn_handle)
{
    while (s_hid.ref_index < s_hid.chr_count) {
        size_t idx = s_hid.ref_index;
        hid_chr_t *chr = &s_hid.chrs[idx];
        if (chr->report_ref_handle == 0) {
            s_hid.ref_index++;
            continue;
        }

        int rc = ble_gattc_read(conn_handle, chr->report_ref_handle, report_ref_cb,
                                (void *)(uintptr_t)idx);
        if (rc == 0) {
            return;
        }

        BLE_INFO(TAG, "report ref read could not start char[%u] rc=%d",
                 (unsigned)idx, rc);
        s_hid.ref_index++;
    }

    read_report_map(conn_handle);
}

static int subscribe_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    size_t idx = (size_t)(uintptr_t)arg;
    if (idx < s_hid.chr_count) {
        hid_chr_t *chr = &s_hid.chrs[idx];
        if (error->status == 0) {
            chr->subscribed = true;
            BLE_DETAIL(TAG, "subscribed char[%u] %s value=0x%04X ccc=0x%04X",
                     (unsigned)idx, hid_chr_name(chr->uuid16), chr->val_handle,
                     chr->ccc_handle);
        } else {
            BLE_INFO(TAG, "subscribe char[%u] status=%d att=%d",
                     (unsigned)idx, error->status, error->att_handle);
        }
        s_hid.sub_index = idx + 1;
    }

    subscribe_next(conn_handle);
    return 0;
}

static int device_name_read(uint16_t conn_handle, const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (error->status == 0 && attr && attr->om && s_hid.ready &&
        conn_handle == s_hid.conn_handle) {
        char name[64] = {0};
        uint16_t len = OS_MBUF_PKTLEN(attr->om);
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        if (os_mbuf_copydata(attr->om, 0, len, name) == 0 && name[0]) {
            keyboardStatusSetBle(true, name);
            bleTransportNotice("keyboard name=\"%s\"", name);
        }
    }
    return 0;
}

static void subscribe_next(uint16_t conn_handle)
{
    while (s_hid.sub_index < s_hid.chr_count) {
        size_t idx = s_hid.sub_index;
        hid_chr_t *chr = &s_hid.chrs[idx];
        if (!is_input_report_char(chr) || chr->ccc_handle == 0) {
            s_hid.sub_index++;
            continue;
        }

        uint8_t ccc_notify[2] = {0x01, 0x00};
        BLE_DETAIL(TAG, "subscribing char[%u] %s id=%u type=%u value=0x%04X ccc=0x%04X",
                 (unsigned)idx, hid_chr_name(chr->uuid16), chr->report_id,
                 chr->report_type, chr->val_handle, chr->ccc_handle);
        int rc = ble_gattc_write_flat(conn_handle, chr->ccc_handle, ccc_notify,
                                      sizeof(ccc_notify), subscribe_cb,
                                      (void *)(uintptr_t)idx);
        if (rc == 0) {
            return;
        }

        BLE_INFO(TAG, "subscribe write could not start char[%u] rc=%d", (unsigned)idx, rc);
        s_hid.sub_index++;
    }

    const hid_chr_t *input = find_chr_by_value_handle(s_hid.input_handle);
    if (!input || !input->subscribed) {
        BLE_INFO(TAG, "no usable keyboard subscription; disconnecting");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    s_hid.ready = true;
    keyboardStatusSetBle(true, s_best.name);
    s_hid.discovering = false;
    ble_npl_callout_stop(&s_open_timeout);
    bleTransportNotice("keyboard connected and ready: %s",
                       s_best.name[0] ? s_best.name : "(name not advertised)");
    if (!s_best.name[0]) {
        // Bonded keyboards often reconnect using advertisements without a name.
        const ble_uuid16_t name_uuid = BLE_UUID16_INIT(0x2A00);
        ble_gattc_read_by_uuid(conn_handle, 1, 0xFFFF, &name_uuid.u, device_name_read, NULL);
    }
    s_config.state(BLE_TRANSPORT_READY);
}

static int protocol_written(uint16_t conn_handle, const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    (void)arg;
    if (error->status != 0) {
        BLE_INFO(TAG, "protocol mode write failed status=%d", error->status);
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }
    s_hid.sub_index = 0;
    subscribe_next(conn_handle);
    return 0;
}

static void select_keyboard(uint16_t conn_handle, esp_hid_report_map_t *map)
{
    /* Use the stock descriptor parser only, never esp_hidh or its private structs. */
    for (size_t i = 0; map && i < s_hid.chr_count; i++) {
        hid_chr_t *chr = &s_hid.chrs[i];
        for (size_t j = 0; j < map->reports_len; j++) {
            const esp_hid_report_item_t *report = &map->reports[j];
            if (chr->has_report_ref && chr->report_id == report->report_id &&
                chr->report_type == report->report_type &&
                report->protocol_mode == ESP_HID_PROTOCOL_MODE_REPORT &&
                report->usage == ESP_HID_USAGE_KEYBOARD) {
                chr->keyboard = true;
                chr->report_length = report->value_len;
            }
        }
        if (!s_hid.input_handle && chr->keyboard &&
            chr->report_type == BLE_SVC_HID_RPT_TYPE_INPUT &&
            (chr->report_length == 7 || chr->report_length == 8) &&
            chr->ccc_handle && (chr->props & BLE_GATT_CHR_PROP_NOTIFY)) {
            s_hid.input_handle = chr->val_handle;
        }
    }
    if (map) {
        esp_hid_free_report_map(map);
    }

    uint8_t mode = 1;
    if (!s_hid.input_handle) {
        for (size_t i = 0; i < s_hid.chr_count; i++) {
            hid_chr_t *chr = &s_hid.chrs[i];
            if (chr->uuid16 == BLE_SVC_HID_CHR_UUID16_BOOT_KBD_INP &&
                chr->ccc_handle && (chr->props & BLE_GATT_CHR_PROP_NOTIFY)) {
                chr->report_length = 8;
                s_hid.input_handle = chr->val_handle;
                mode = 0;
                break;
            }
        }
    }
    const hid_chr_t *input = find_chr_by_value_handle(s_hid.input_handle);
    if (!input) {
        BLE_INFO(TAG, "no supported keyboard report (7/8-byte or boot); disconnecting");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    for (size_t i = 0; i < s_hid.chr_count; i++) {
        const hid_chr_t *chr = &s_hid.chrs[i];
        bool output = mode == 0 ? chr->uuid16 == BLE_SVC_HID_CHR_UUID16_BOOT_KBD_OUT :
            (chr->keyboard && chr->report_type == BLE_SVC_HID_RPT_TYPE_OUTPUT &&
             chr->report_id == input->report_id && chr->report_length == 1);
        if (output && (chr->props & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP))) {
            s_hid.led_handle = chr->val_handle;
            s_hid.led_props = chr->props;
            break;
        }
    }

    BLE_INFO(TAG, "keyboard input handle=0x%04X length=%u mode=%s",
             input->val_handle, input->report_length, mode ? "report" : "boot");
    for (size_t i = 0; i < s_hid.chr_count; i++) {
        const hid_chr_t *chr = &s_hid.chrs[i];
        if (chr->uuid16 != BLE_SVC_HID_CHR_UUID16_PROTOCOL_MODE) {
            continue;
        }
        int rc;
        if (chr->props & BLE_GATT_CHR_PROP_WRITE_NO_RSP) {
            rc = ble_gattc_write_no_rsp_flat(conn_handle, chr->val_handle, &mode, 1);
        } else if (chr->props & BLE_GATT_CHR_PROP_WRITE) {
            rc = ble_gattc_write_flat(conn_handle, chr->val_handle, &mode, 1,
                                     protocol_written, NULL);
            if (rc == 0) {
                return;
            }
        } else {
            rc = BLE_HS_ENOTSUP;
        }
        if (rc != 0) {
            BLE_INFO(TAG, "protocol mode start failed rc=%d", rc);
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return;
        }
        break;
    }
    s_hid.sub_index = 0;
    subscribe_next(conn_handle);
}

static int report_map_cb(uint16_t conn_handle, const struct ble_gatt_error *error,
                         struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (error->status == 0 && attr && attr->om) {
        size_t len = OS_MBUF_PKTLEN(attr->om);
        if (attr->offset != s_report_map_length ||
            len > sizeof(s_report_map) - s_report_map_length) {
            BLE_INFO(TAG, "report map too large or noncontiguous; trying boot protocol");
            select_keyboard(conn_handle, NULL);
            return BLE_HS_EMSGSIZE;
        }
        os_mbuf_copydata(attr->om, 0, len, s_report_map + s_report_map_length);
        s_report_map_length += len;
        return 0;
    }
    esp_hid_report_map_t *map = NULL;
    if (error->status == BLE_HS_EDONE && s_report_map_length) {
        map = esp_hid_parse_report_map(s_report_map, s_report_map_length);
    } else {
        BLE_INFO(TAG, "report map read status=%d; trying boot protocol", error->status);
    }
    select_keyboard(conn_handle, map);
    return 0;
}

static void read_report_map(uint16_t conn_handle)
{
    s_report_map_length = 0;
    for (size_t i = 0; i < s_hid.chr_count; i++) {
        if (s_hid.chrs[i].uuid16 == BLE_SVC_HID_CHR_UUID16_REPORT_MAP) {
            int rc = ble_gattc_read_long(conn_handle, s_hid.chrs[i].val_handle, 0,
                                        report_map_cb, NULL);
            if (rc == 0) {
                return;
            }
            BLE_INFO(TAG, "report map read start failed rc=%d", rc);
            break;
        }
    }
    select_keyboard(conn_handle, NULL);
}

static int leds_written(uint16_t conn_handle, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    if (error->status != 0) {
        BLE_DETAIL(TAG, "keyboard LED write status=%d", error->status);
    }
    return 0;
}

static void update_leds(struct ble_npl_event *event)
{
    (void)event;
    if (!s_hid.ready || !s_hid.led_handle || s_clearing) {
        return;
    }
    uint8_t leds = atomic_load(&s_leds);
    int rc;
    if (s_hid.led_props & BLE_GATT_CHR_PROP_WRITE_NO_RSP) {
        rc = ble_gattc_write_no_rsp_flat(s_hid.conn_handle, s_hid.led_handle, &leds, 1);
    } else {
        rc = ble_gattc_write_flat(s_hid.conn_handle, s_hid.led_handle, &leds, 1,
                                 leds_written, NULL);
    }
    if (rc != 0) {
        BLE_DETAIL(TAG, "keyboard LED write start failed rc=%d", rc);
    }
}

static void finish_clear(void)
{
    int rc = ble_store_clear();
    if (rc == 0) {
        s_config.state(BLE_TRANSPORT_CLEARED);
    } else {
        BLE_INFO(TAG, "bond clear failed rc=%d; reset and retry", rc);
    }
}

static void clear_bonds(struct ble_npl_event *event)
{
    (void)event;
    s_clearing = true;
    ble_npl_callout_stop(&s_scan_retry);
    ble_npl_callout_stop(&s_open_timeout);
    if (ble_gap_disc_active()) {
        ble_gap_disc_cancel();
    }
    if (s_hid.connected) {
        if (ble_gap_terminate(s_hid.conn_handle, BLE_ERR_REM_USER_CONN_TERM) == 0) {
            return;
        }
    } else if (s_hid.opening) {
        ble_gap_conn_cancel();
    }
    finish_clear();
}

void bleTransportSetLeds(uint8_t leds)
{
    atomic_store(&s_leds, leds);
    if (s_initialized) {
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_led_event);
    }
}

void bleTransportClearBonds(void)
{
    if (s_initialized) {
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_clear_event);
    }
}

static esp_err_t start_scan(uint32_t seconds)
{
    struct ble_gap_disc_params params = {
        .itvl = 0x50,
        .window = 0x30,
        .filter_policy = 0,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 1,
    };

    s_best = (candidate_t){0};
    BLE_DETAIL(TAG, "scan start seconds=%" PRIu32 " own_addr_type=%u", seconds, s_own_addr_type);
    int rc = ble_gap_disc(s_own_addr_type, (int32_t)seconds * 1000, &params, gap_event, NULL);
    if (rc != 0) {
        BLE_INFO(TAG, "ble_gap_disc failed rc=%d", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void connect_to_best(void)
{
    char addr[18];
    addr_to_str(&s_best.addr, addr);
    BLE_INFO(TAG, "connecting %s type=%s name=\"%s\"",
             addr, addr_type_str(s_best.addr.type), s_best.name);

    struct ble_gap_conn_params params = {
        .scan_itvl = 0x50,
        .scan_window = 0x30,
        .itvl_min = 12,
        .itvl_max = 40,
        .latency = 4,
        .supervision_timeout = 400,
        .min_ce_len = 0,
        .max_ce_len = 0,
    };

    s_hid.opening = true;
    s_config.state(BLE_TRANSPORT_CONNECTING);
    int rc = ble_gap_connect(s_own_addr_type, &s_best.addr, 30000, &params, gap_event, NULL);
    if (rc != 0) {
        BLE_INFO(TAG, "ble_gap_connect failed rc=%d", rc);
        reset_connection_state();
        s_config.state(BLE_TRANSPORT_NOT_FOUND);
        schedule_scan();
    }
}

static int bond_count(void)
{
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int count = 0;
    int rc = ble_store_util_bonded_peers(peers, &count, CONFIG_BT_NIMBLE_MAX_BONDS);
    if (rc != 0) {
        BLE_INFO(TAG, "bond lookup failed rc=%d", rc);
        return 0;
    }
    return count;
}

static void schedule_scan(void)
{
    if (!s_clearing && s_config.reconnect && bond_count() > 0) {
        ble_npl_callout_reset(&s_scan_retry,
            ble_npl_time_ms_to_ticks32(s_config.reconnect_delay_ms));
    }
}

static void scan_retry(struct ble_npl_event *event)
{
    (void)event;
    if (s_clearing || s_hid.connected || s_hid.opening || ble_gap_disc_active()) {
        return;
    }
    uint32_t seconds = s_enrollment ? s_config.enrollment_seconds : s_config.reconnect_seconds;
    if (start_scan(seconds) != ESP_OK) {
        schedule_scan();
    }
}

static void open_timeout(struct ble_npl_event *event)
{
    (void)event;
    if (s_hid.connected && !s_hid.ready) {
        BLE_INFO(TAG, "security/HID setup timed out; disconnecting");
        ble_gap_terminate(s_hid.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void host_task(void *param)
{
    (void)param;
    BLE_DETAIL(TAG, "nimble host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_reset(int reason)
{
    BLE_INFO(TAG, "nimble host reset reason=%d", reason);
    ble_npl_callout_stop(&s_scan_retry);
    ble_npl_callout_stop(&s_open_timeout);
    reset_connection_state();
    s_config.state(BLE_TRANSPORT_DISCONNECTED);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        BLE_INFO(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        return;
    }

    uint8_t id_type =
        (s_own_addr_type == BLE_OWN_ADDR_RANDOM ||
         s_own_addr_type == BLE_OWN_ADDR_RPA_RANDOM_DEFAULT) ?
        BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
    int is_nrpa = 0;
    rc = ble_hs_id_copy_addr(id_type, s_own_id_addr.val, &is_nrpa);
    if (rc == 0) {
        s_own_id_addr.type = id_type;
        char own[18];
        addr_to_str(&s_own_id_addr, own);
        BLE_DETAIL(TAG, "sync: own identity %s/%s nrpa=%d own_addr_type=%u",
                 own, addr_type_str(s_own_id_addr.type), is_nrpa, s_own_addr_type);
    } else {
        BLE_INFO(TAG, "sync: own identity lookup failed rc=%d", rc);
    }

    log_bonds();
    if (s_enrollment) {
        s_config.state(bond_count() > 0 ? BLE_TRANSPORT_SCAN_SAVED : BLE_TRANSPORT_SCAN_NEW);
    }
    ble_npl_callout_reset(&s_scan_retry, 1);
}

bool bleTransportInit(const ble_transport_config_t *config)
{
    if (!config || !config->state || !config->report ||
        !config->enrollment_seconds || !config->reconnect_seconds ||
        !config->reconnect_delay_ms || config->passkey > 999999) {
        return false;
    }
    s_config = *config;
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK) {
        BLE_INFO(TAG, "controller memory release failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((err = esp_bt_controller_init(&bt_cfg)) != ESP_OK ||
        (err = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK ||
        (err = esp_nimble_init()) != ESP_OK) {
        BLE_INFO(TAG, "NimBLE init failed: %s", esp_err_to_name(err));
        return false;
    }

    ble_svc_gap_device_name_set("Astrocade Keyboard");
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = s_config.mitm;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_npl_callout_init(&s_scan_retry, nimble_port_get_dflt_eventq(), scan_retry, NULL);
    ble_npl_callout_init(&s_open_timeout, nimble_port_get_dflt_eventq(), open_timeout, NULL);
    ble_npl_event_init(&s_led_event, update_leds, NULL);
    ble_npl_event_init(&s_clear_event, clear_bonds, NULL);
    s_initialized = true;
    BLE_INFO(TAG, "NimBLE ready; enrollment %lu seconds", (unsigned long)s_config.enrollment_seconds);
    nimble_port_freertos_init(host_task);
    return true;
}
