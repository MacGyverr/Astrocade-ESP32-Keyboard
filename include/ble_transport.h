#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLE_TRANSPORT_SCAN_NEW,
    BLE_TRANSPORT_SCAN_SAVED,
    BLE_TRANSPORT_CONNECTING,
    BLE_TRANSPORT_READY,
    BLE_TRANSPORT_DISCONNECTED,
    BLE_TRANSPORT_NOT_FOUND,
    BLE_TRANSPORT_CLEARED,
} ble_transport_event_t;

typedef struct {
    uint32_t enrollment_seconds;
    uint32_t reconnect_seconds;
    uint32_t reconnect_delay_ms;
    uint32_t passkey;
    bool reconnect;
    bool mitm;
    bool verbose;
    void (*state)(ble_transport_event_t event);
    void (*report)(const uint8_t boot_report[8]);
} ble_transport_config_t;

bool bleTransportInit(const ble_transport_config_t *config);
void bleTransportSetLeds(uint8_t leds);
void bleTransportClearBonds(void);
void bleTransportLog(const char *format, ...) __attribute__((format(printf, 1, 2)));
void bleTransportNotice(const char *format, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif
