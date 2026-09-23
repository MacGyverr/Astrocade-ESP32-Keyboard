#include "web_portal.h"
#include "app_config.h"
#include "app_log.h"
#include "overlay_catalog.h"
#include "text_transfer.h"
#include "captive_dns.h"
#include "keyboard_status.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_http_server.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"
#include "dhcpserver/dhcpserver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#if ASTROCADE_WEB_ENABLED
extern const uint8_t htmlStart[] asm("_binary_index_html_start");
extern const uint8_t htmlEnd[] asm("_binary_index_html_end");
extern const uint8_t jsStart[] asm("_binary_app_js_start");
extern const uint8_t jsEnd[] asm("_binary_app_js_end");
extern const uint8_t cssStart[] asm("_binary_style_css_start");
extern const uint8_t cssEnd[] asm("_binary_style_css_end");

namespace {
    struct Credentials { char ssid[33]; char password[65]; };
    Credentials credentials{};
    QueueHandle_t networkChanges;
    SemaphoreHandle_t networkMutex;
    std::atomic<bool> connected{false}, apEnabled{false}, restarting{false};
    char ip[16] = "0.0.0.0";
    char ssid[33] = "";
    char apName[33];
    uint32_t physicalFlashBytes = 0;
    constexpr char adminKey[] = "123456";
    constexpr char apPassword[] = "12345678";
    char csrf[33];
    esp_netif_t *station;
    httpd_handle_t server;
    nvs_handle_t storage;
    int64_t connectedSince = 0;

    void randomHex(char *out, std::size_t bytes) {
        uint8_t raw[16]; esp_fill_random(raw, bytes);
        for (std::size_t i = 0; i < bytes; i++) std::snprintf(out + i * 2, 3, "%02x", raw[i]);
    }
    esp_err_t respond(httpd_req_t *req, cJSON *object, const char *status = "200 OK") {
        if (!object) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        char *encoded = cJSON_PrintUnformatted(object);
        cJSON_Delete(object);
        if (!encoded) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        httpd_resp_set_status(req, status);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        const esp_err_t result = httpd_resp_send(req, encoded, HTTPD_RESP_USE_STRLEN);
        cJSON_free(encoded);
        return result;
    }
    esp_err_t error(httpd_req_t *req, const char *status, const char *message) {
        auto json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "error", message);
        httpd_resp_set_hdr(req, "Connection", "close");
        return respond(req, json, status);
    }
    bool headerMatches(httpd_req_t *req, const char *name, const char *expected) {
        char value[80] = {};
        if (httpd_req_get_hdr_value_str(req, name, value, sizeof(value)) != ESP_OK ||
            std::strlen(value) != std::strlen(expected)) return false;
        unsigned difference = 0;
        for (std::size_t i = 0; expected[i]; i++) difference |= value[i] ^ expected[i];
        return difference == 0;
    }
    bool allowedHost(httpd_req_t *req) {
        char host[80] = {}, local[16];
        if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) return false;
        if (auto port = std::strchr(host, ':')) *port = 0;
        xSemaphoreTake(networkMutex, portMAX_DELAY);
        std::memcpy(local, ip, sizeof(local));
        xSemaphoreGive(networkMutex);
        return !std::strcmp(host, "astrocadekeyboard.local") ||
            !std::strcmp(host, "astrocadekeyboard") || !std::strcmp(host, "192.168.4.1") ||
            (std::strcmp(local, "0.0.0.0") && !std::strcmp(host, local));
    }
    bool authorize(httpd_req_t *req, bool admin = false) {
        if (!allowedHost(req) || !headerMatches(req, "X-Astrocade-Token", csrf)) {
            error(req, "403 Forbidden", "Refresh the page before trying again."); return false;
        }
        if (admin && !headerMatches(req, "X-Admin-Key", adminKey)) {
            error(req, "401 Unauthorized", "Device key required. It is printed in the serial monitor."); return false;
        }
        return true;
    }
    bool receive(httpd_req_t *req, uint8_t *out, std::size_t length) {
        std::size_t got = 0; unsigned timeouts = 0;
        while (got < length) {
            int n = httpd_req_recv(req, reinterpret_cast<char *>(out + got), length - got);
            if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
            if (n <= 0) return false;
            got += n;
        }
        return true;
    }
    cJSON *jobJson() {
        const auto job = textTransferStatus();
        auto out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "state", transferStateName(job.state));
        cJSON_AddNumberToObject(out, "id", job.id);
        cJSON_AddNumberToObject(out, "sent", job.sent);
        cJSON_AddNumberToObject(out, "total", job.total);
        cJSON_AddNumberToObject(out, "linesSent", job.linesSent);
        cJSON_AddNumberToObject(out, "lines", job.lines);
        return out;
    }
    esp_err_t statusGet(httpd_req_t *req) {
        if (!allowedHost(req)) return error(req, "403 Forbidden", "Use the device address.");
        auto out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "token", csrf);
        cJSON_AddStringToObject(out, "hostname", "astrocadekeyboard.local");
        cJSON_AddStringToObject(out, "version", esp_app_get_description()->version);
        cJSON_AddStringToObject(out, "buildType", AppConfig::kBuildType);
        cJSON_AddStringToObject(out, "boardModel", AppConfig::kBoardModel);
        cJSON_AddNumberToObject(out, "flashBytes", physicalFlashBytes);
        cJSON_AddNumberToObject(out, "psramBytes", heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        cJSON_AddBoolToObject(out, "rgbEnabled", ASTROCADE_STATUS_LED_ENABLED != 0);
        cJSON_AddStringToObject(out, "chip", CONFIG_IDF_TARGET);
        const auto otaPartition = esp_ota_get_next_update_partition(nullptr);
        cJSON_AddNumberToObject(out, "otaMaxBytes", otaPartition ? otaPartition->size : 0);
        keyboard_connections_t keyboards{};
        keyboardStatusGet(&keyboards);
        auto addKeyboard = [&](const char *key, const keyboard_connection_t &connection, bool enabled) {
            auto item = cJSON_AddObjectToObject(out, key);
            cJSON_AddBoolToObject(item, "enabled", enabled);
            cJSON_AddBoolToObject(item, "connected", connection.connected);
            cJSON_AddStringToObject(item, "name", connection.name);
        };
        addKeyboard("usb", keyboards.usb, ASTROCADE_USB_ENABLED != 0);
        addKeyboard("ble", keyboards.ble, true);
        cJSON_AddBoolToObject(out, "connected", connected.load());
        cJSON_AddBoolToObject(out, "setup", apEnabled.load());
        cJSON_AddStringToObject(out, "ap", apName);
        xSemaphoreTake(networkMutex, portMAX_DELAY);
        cJSON_AddStringToObject(out, "ip", ip);
        cJSON_AddStringToObject(out, "ssid", ssid);
        xSemaphoreGive(networkMutex);
        cJSON_AddStringToObject(out, "overlay", activeOverlay().id);
        auto overlays = cJSON_AddArrayToObject(out, "overlays");
        std::size_t count;
        auto catalog = overlayCatalog(count);
        for (std::size_t i = 0; i < count; i++) {
            auto item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "id", catalog[i].id);
            cJSON_AddStringToObject(item, "name", catalog[i].name);
            cJSON_AddItemToArray(overlays, item);
        }
        cJSON_AddNumberToObject(out, "maxBytes", ASTROCADE_TEXT_MAX_BYTES);
        cJSON_AddNumberToObject(out, "downMs", AppConfig::kKeypressDownMs);
        cJSON_AddNumberToObject(out, "gapMs", AppConfig::kKeypressGapMs);
        cJSON_AddNumberToObject(out, "lineGapMs", ASTROCADE_TEXT_LINE_GAP_MS);
        cJSON_AddItemToObject(out, "job", jobJson());
        return respond(req, out);
    }
    esp_err_t textPost(httpd_req_t *req) {
        if (!authorize(req)) return ESP_OK;
        if (restarting.load()) return error(req, "409 Conflict", "Device is restarting.");
        if (!headerMatches(req, "X-Overlay", activeOverlay().id))
            return error(req, "409 Conflict", "Overlay changed. Refresh and validate again.");
        const bool start = !std::strcmp(req->uri, "/api/send");
        if (req->content_len == 0 || req->content_len > ASTROCADE_TEXT_MAX_BYTES)
            return error(req, "413 Content Too Large", "Text must be between 1 byte and the displayed limit.");
        const auto state = textTransferStatus().state;
        if (state == TransferState::Running || state == TransferState::Paused ||
            state == TransferState::Cancelling || state == TransferState::Maintenance)
            return error(req, "409 Conflict", "A transfer or maintenance operation is active.");
        constexpr uint32_t textCaps = MALLOC_CAP_8BIT |
            (ASTROCADE_TEXT_USE_PSRAM ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
        auto text = static_cast<uint8_t *>(heap_caps_malloc(req->content_len, textCaps));
        auto plan = start ? static_cast<TextKey *>(heap_caps_malloc(
            (req->content_len + 1) * sizeof(TextKey), textCaps)) : nullptr;
        if (!text || (start && !plan)) {
            heap_caps_free(text); heap_caps_free(plan);
            return error(req, "503 Service Unavailable", "Not enough memory; try a smaller program.");
        }
        if (!receive(req, text, req->content_len)) {
            heap_caps_free(text); heap_caps_free(plan);
            return error(req, "408 Request Timeout", "Upload incomplete; nothing was sent.");
        }
        const auto result = compileText(text, req->content_len, plan, req->content_len + 1, activeOverlay().map);
        heap_caps_free(text);
        auto out = cJSON_CreateObject();
        cJSON_AddNumberToObject(out, "characters", result.count);
        cJSON_AddNumberToObject(out, "lines", result.lines);
        cJSON_AddNumberToObject(out, "errors", result.errors);
        auto issues = cJSON_AddArrayToObject(out, "issues");
        for (unsigned i = 0; i < std::min<uint32_t>(result.errors, 16); i++) {
            auto issue = cJSON_CreateObject();
            cJSON_AddNumberToObject(issue, "line", result.issues[i].line);
            cJSON_AddNumberToObject(issue, "column", result.issues[i].column);
            cJSON_AddNumberToObject(issue, "codepoint", result.issues[i].codepoint);
            cJSON_AddItemToArray(issues, issue);
        }
        if (result.errors || !result.count) {
            heap_caps_free(plan);
            cJSON_AddStringToObject(out, "error", result.errors ?
                "Unsupported characters. Nothing was sent." : "No text to send.");
            return respond(req, out, "422 Unprocessable Content");
        }
        if (start && !textTransferStart(plan, result.count, result.lines)) {
            heap_caps_free(plan); cJSON_Delete(out);
            return error(req, "409 Conflict", "Keyboard is busy. Release keys and try again.");
        }
        cJSON_AddBoolToObject(out, "valid", true);
        cJSON_AddItemToObject(out, "job", jobJson());
        return respond(req, out, start ? "202 Accepted" : "200 OK");
    }
    esp_err_t controlPost(httpd_req_t *req) {
        if (!authorize(req)) return ESP_OK;
        if (req->content_len) return error(req, "400 Bad Request", "No request body expected.");
        const char *command = std::strrchr(req->uri, '/') + 1;
        if (!textTransferControl(command)) return error(req, "409 Conflict", "Transfer state changed.");
        return respond(req, jobJson());
    }
    esp_err_t networksGet(httpd_req_t *req) {
        if (!authorize(req, true)) return ESP_OK;
        if (!textTransferBeginMaintenance()) return error(req, "409 Conflict", "Finish the transfer before scanning.");
        const esp_err_t scan = esp_wifi_scan_start(nullptr, true);
        uint16_t count = 24;
        wifi_ap_record_t records[24]{};
        const esp_err_t read = scan == ESP_OK ? esp_wifi_scan_get_ap_records(&count, records) : scan;
        textTransferEndMaintenance();
        if (read != ESP_OK) return error(req, "503 Service Unavailable", "Wi-Fi is busy; retry scanning.");
        auto out = cJSON_CreateObject(); auto list = cJSON_AddArrayToObject(out, "networks");
        for (uint16_t i = 0; i < count; i++) {
            auto item = cJSON_CreateObject();
            char name[33]{}; std::memcpy(name, records[i].ssid, 32);
            cJSON_AddStringToObject(item, "ssid", name);
            cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
            cJSON_AddBoolToObject(item, "secure", records[i].authmode != WIFI_AUTH_OPEN);
            cJSON_AddItemToArray(list, item);
        }
        return respond(req, out);
    }
    esp_err_t wifiPost(httpd_req_t *req) {
        if (!authorize(req, true)) return ESP_OK;
        if (!req->content_len || req->content_len > 512) return error(req, "400 Bad Request", "Invalid network settings.");
        char body[513]{};
        if (!receive(req, reinterpret_cast<uint8_t *>(body), req->content_len))
            return error(req, "408 Request Timeout", "Incomplete network settings.");
        auto document = cJSON_ParseWithLength(body, req->content_len);
        if (!document) return error(req, "400 Bad Request", "Invalid JSON.");
        auto name = cJSON_GetObjectItemCaseSensitive(document, "ssid");
        auto password = cJSON_GetObjectItemCaseSensitive(document, "password");
        Credentials next{};
        bool valid = cJSON_IsString(name) && cJSON_IsString(password) &&
            std::strlen(name->valuestring) >= 1 && std::strlen(name->valuestring) <= 32 &&
            (std::strlen(password->valuestring) == 0 ||
             (std::strlen(password->valuestring) >= 8 && std::strlen(password->valuestring) <= 63));
        if (valid) {
            std::strcpy(next.ssid, name->valuestring);
            std::strcpy(next.password, password->valuestring);
        }
        cJSON_Delete(document);
        if (!valid) return error(req, "400 Bad Request", "SSID: 1-32 bytes. Password: empty or 8-63 bytes.");
        if (!textTransferBeginMaintenance()) return error(req, "409 Conflict", "Finish the transfer first.");
        esp_err_t saved = nvs_set_blob(storage, "wifi", &next, sizeof(next));
        if (saved == ESP_OK) saved = nvs_commit(storage);
        textTransferEndMaintenance();
        if (saved != ESP_OK) return error(req, "500 Internal Server Error", "Could not save Wi-Fi settings.");
        xQueueOverwrite(networkChanges, &next);
        auto out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "saved", true);
        return respond(req, out, "202 Accepted");
    }
    void rebootTask(void *) {
        vTaskDelay(pdMS_TO_TICKS(1200));
        esp_restart();
    }
    esp_err_t otaPost(httpd_req_t *req) {
        if (!authorize(req, true)) return ESP_OK;
        const auto partition = esp_ota_get_next_update_partition(nullptr);
        constexpr std::size_t prefixLength = sizeof(esp_image_header_t) +
            sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
        if (!partition || req->content_len < prefixLength || req->content_len > partition->size)
            return error(req, "413 Content Too Large", "Firmware does not fit this device's application slot.");
        if (restarting.load() || !textTransferBeginMaintenance())
            return error(req, "409 Conflict", "Finish the transfer and release keyboard keys first.");
        uint8_t buffer[4096];
        if (!receive(req, buffer, prefixLength)) {
            textTransferEndMaintenance();
            return error(req, "408 Request Timeout", "Incomplete firmware.");
        }
        esp_image_header_t header{};
        esp_app_desc_t app{};
        std::memcpy(&header, buffer, sizeof(header));
        std::memcpy(&app, buffer + sizeof(header) + sizeof(esp_image_segment_header_t), sizeof(app));
        const auto running = esp_app_get_description();
        if (header.magic != ESP_IMAGE_HEADER_MAGIC || header.chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID ||
            app.magic_word != ESP_APP_DESC_MAGIC_WORD ||
            std::memcmp(app.project_name, running->project_name, sizeof(app.project_name))) {
            textTransferEndMaintenance();
            return error(req, "422 Unprocessable Content", "Not an Astrocade application image for this chip.");
        }
        esp_ota_handle_t handle = 0;
        esp_err_t result = esp_ota_begin(partition, req->content_len, &handle);
        bool began = result == ESP_OK;
        std::size_t received = prefixLength;
        if (result == ESP_OK) result = esp_ota_write(handle, buffer, prefixLength);
        while (result == ESP_OK && received < req->content_len) {
            auto amount = std::min(sizeof(buffer), req->content_len - received);
            if (!receive(req, buffer, amount)) { result = ESP_ERR_TIMEOUT; break; }
            result = esp_ota_write(handle, buffer, amount);
            received += amount;
        }
        if (result == ESP_OK) {
            result = esp_ota_end(handle);
            began = false; // end releases the OTA handle even when validation fails.
        }
        if (result == ESP_OK) result = esp_ota_set_boot_partition(partition);
        if (result != ESP_OK) {
            if (began) esp_ota_abort(handle);
            textTransferEndMaintenance();
            APP_LOG("ota: rejected %s", esp_err_to_name(result));
            return error(req, "422 Unprocessable Content", "Firmware upload failed validation; current firmware remains selected.");
        }
        restarting.store(true);
        auto out = cJSON_CreateObject(); cJSON_AddBoolToObject(out, "restarting", true);
        auto response = respond(req, out);
        xTaskCreate(rebootTask, "ota_restart", 2048, nullptr, 2, nullptr);
        return response;
    }
    esp_err_t portalRedirect(httpd_req_t *req) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/#network");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_sendstr(req, "Wi-Fi setup");
    }
    esp_err_t captiveGet(httpd_req_t *req) {
        httpd_resp_set_type(req, "application/captive+json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_sendstr(req, apEnabled.load()
            ? "{\"captive\":true,\"user-portal-url\":\"http://192.168.4.1/#network\"}"
            : "{\"captive\":false}");
    }
    esp_err_t assetGet(httpd_req_t *req) {
        if (apEnabled.load() && !allowedHost(req)) return portalRedirect(req);
        const uint8_t *begin = htmlStart, *end = htmlEnd;
        const char *mime = "text/html; charset=utf-8";
        if (!std::strcmp(req->uri, "/app.js")) { begin = jsStart; end = jsEnd; mime = "text/javascript"; }
        if (!std::strcmp(req->uri, "/style.css")) { begin = cssStart; end = cssEnd; mime = "text/css"; }
        httpd_resp_set_type(req, mime);
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req, "Content-Security-Policy",
            "default-src 'self'; img-src 'self' data:; style-src 'self'; script-src 'self'; frame-ancestors 'none'; base-uri 'none'");
        return httpd_resp_send(req, reinterpret_cast<const char *>(begin), end - begin);
    }
    esp_err_t notFound(httpd_req_t *req, httpd_err_code_t) {
        if (apEnabled.load()) {
            return portalRedirect(req);
        }
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
    }
    bool startServer() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.core_id = 0; config.task_priority = 3; config.stack_size = 12288;
        config.max_uri_handlers = 16; config.max_open_sockets = 5;
        config.lru_purge_enable = true; config.recv_wait_timeout = 5; config.send_wait_timeout = 5;
        if (httpd_start(&server, &config) != ESP_OK) return false;
        auto add = [](const char *uri, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
            httpd_uri_t route{}; route.uri = uri; route.method = method; route.handler = handler;
            return httpd_register_uri_handler(server, &route) == ESP_OK;
        };
        bool ok = add("/", HTTP_GET, assetGet) && add("/app.js", HTTP_GET, assetGet) &&
            add("/style.css", HTTP_GET, assetGet) && add("/api/status", HTTP_GET, statusGet) &&
            add("/api/validate", HTTP_POST, textPost) && add("/api/send", HTTP_POST, textPost) &&
            add("/api/pause", HTTP_POST, controlPost) && add("/api/resume", HTTP_POST, controlPost) &&
            add("/api/cancel", HTTP_POST, controlPost) && add("/api/networks", HTTP_GET, networksGet) &&
            add("/api/wifi", HTTP_POST, wifiPost) && add("/api/ota", HTTP_POST, otaPost) &&
            add("/api/captive", HTTP_GET, captiveGet);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, notFound);
        return ok;
    }
    void wifiEvent(void *, esp_event_base_t base, int32_t id, void *data) {
        if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
            connected.store(false);
            xSemaphoreTake(networkMutex, portMAX_DELAY);
            std::strcpy(ip, "0.0.0.0");
            xSemaphoreGive(networkMutex);
        } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            const auto event = static_cast<ip_event_got_ip_t *>(data);
            xSemaphoreTake(networkMutex, portMAX_DELAY);
            std::snprintf(ip, sizeof(ip), IPSTR, IP2STR(&event->ip_info.ip));
            xSemaphoreGive(networkMutex);
            connected.store(true);
            wifi_ap_record_t joined{};
            esp_wifi_sta_get_ap_info(&joined);
            APP_NOTICE("wifi: joined SSID=\"%.32s\"; http://" IPSTR "/; http://astrocadekeyboard.local/",
                       reinterpret_cast<const char *>(joined.ssid), IP2STR(&event->ip_info.ip));
        }
    }
    esp_err_t configureAp() {
        wifi_config_t config{};
        std::strcpy(reinterpret_cast<char *>(config.ap.ssid), apName);
        std::strcpy(reinterpret_cast<char *>(config.ap.password), apPassword);
        config.ap.ssid_len = std::strlen(apName);
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        config.ap.max_connection = 3;
        config.ap.channel = 1;
        return esp_wifi_set_config(WIFI_IF_AP, &config);
    }
    void setupAp(bool on) {
        if (on == apEnabled.load()) return;
        esp_err_t result;
        if (on) {
            // Configure before beacons start, including when recovering from STA-only mode.
            result = esp_wifi_stop();
            if (result != ESP_OK) { APP_LOG("wifi: stop failed %s", esp_err_to_name(result)); return; }
            connected.store(false);
            result = esp_wifi_set_mode(WIFI_MODE_APSTA);
            if (result == ESP_OK) result = configureAp();
            if (result != ESP_OK) {
                APP_LOG("wifi: AP configuration failed %s", esp_err_to_name(result));
                esp_wifi_set_mode(WIFI_MODE_STA);
                esp_wifi_start();
                if (credentials.ssid[0]) esp_wifi_connect();
                return;
            }
            result = esp_wifi_start();
            if (result == ESP_OK && credentials.ssid[0]) esp_wifi_connect();
        } else {
            result = esp_wifi_set_mode(WIFI_MODE_STA);
        }
        if (result != ESP_OK) { APP_LOG("wifi: mode change failed %s", esp_err_to_name(result)); return; }
        apEnabled.store(on);
        captiveDnsSetEnabled(on);
        if (on) APP_NOTICE("wifi: setup SSID=%s password=%s admin=%s URL=http://192.168.4.1/", apName, apPassword, adminKey);
    }
    void applyCredentials(const Credentials &next) {
        credentials = next;
        esp_wifi_disconnect();
        wifi_config_t config{};
        std::memcpy(config.sta.ssid, next.ssid, std::strlen(next.ssid));
        std::memcpy(config.sta.password, next.password, std::strlen(next.password));
        config.sta.pmf_cfg.capable = true;
        esp_wifi_set_config(WIFI_IF_STA, &config);
        xSemaphoreTake(networkMutex, portMAX_DELAY);
        std::strcpy(ssid, next.ssid);
        xSemaphoreGive(networkMutex);
        if (next.ssid[0]) esp_wifi_connect();
    }
    void networkTask(void *) {
        // Let the startup BLE enrollment finish before starting the Wi-Fi radio.
        vTaskDelay(pdMS_TO_TICKS((AppConfig::kBleEnrollmentWindowSeconds + 3) * 1000));
        auto fail = [](esp_err_t err) {
            if (err == ESP_OK) return false;
            APP_NOTICE("web: startup failed %s; keyboard remains available", esp_err_to_name(err));
            return true;
        };
        if (fail(nvs_open("portal", NVS_READWRITE, &storage))) { vTaskDelete(nullptr); return; }
        std::size_t length = sizeof(credentials);
        if (nvs_get_blob(storage, "wifi", &credentials, &length) != ESP_OK || length != sizeof(credentials))
            credentials = {};
        credentials.ssid[32] = 0; credentials.password[64] = 0;
        randomHex(csrf, 16);
        uint8_t mac[6]; esp_read_mac(mac, ESP_MAC_WIFI_STA);
        std::snprintf(apName, sizeof(apName), "AstrocadeKeyboard-%02X%02X", mac[4], mac[5]);
        if (fail(esp_netif_init())) { vTaskDelete(nullptr); return; }
        auto eventResult = esp_event_loop_create_default();
        if (eventResult != ESP_ERR_INVALID_STATE && fail(eventResult)) { vTaskDelete(nullptr); return; }
        station = esp_netif_create_default_wifi_sta();
        auto ap = esp_netif_create_default_wifi_ap();
        if (!station || !ap) { vTaskDelete(nullptr); return; }
        // Advertise our DNS and portal endpoint to clients without needing a terminal.
        esp_netif_dhcps_stop(ap);
        esp_netif_ip_info_t apIp{};
        esp_netif_get_ip_info(ap, &apIp);
        esp_netif_dns_info_t dns{};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4 = apIp.ip;
        auto dnsResult = esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
        uint8_t offerDns = OFFER_DNS;
        auto offerResult = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET,
            ESP_NETIF_DOMAIN_NAME_SERVER, &offerDns, sizeof(offerDns));
        static char portalUri[] = "http://192.168.4.1/api/captive";
        auto portalResult = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET,
            ESP_NETIF_CAPTIVEPORTAL_URI, portalUri, sizeof(portalUri) - 1);
        if (dnsResult != ESP_OK || offerResult != ESP_OK || portalResult != ESP_OK)
            APP_NOTICE("wifi: captive discovery configuration incomplete; use http://192.168.4.1/");
        // Re-arm DHCP after configuring it; AP_START then begins serving leases.
        if (fail(esp_netif_dhcps_start(ap))) { vTaskDelete(nullptr); return; }
        esp_netif_set_hostname(station, "astrocadekeyboard");
        wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
        if (fail(esp_wifi_init(&init)) || fail(esp_wifi_set_storage(WIFI_STORAGE_RAM))) {
            vTaskDelete(nullptr); return;
        }
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifiEvent, nullptr);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifiEvent, nullptr);
        if (fail(esp_wifi_set_mode(WIFI_MODE_APSTA)) || fail(configureAp())) {
            vTaskDelete(nullptr); return;
        }
        const bool needsSetup = !credentials.ssid[0];
        if (!needsSetup && fail(esp_wifi_set_mode(WIFI_MODE_STA))) { vTaskDelete(nullptr); return; }
        if (fail(esp_wifi_start())) { vTaskDelete(nullptr); return; }
        apEnabled.store(needsSetup);
        captiveDnsSetEnabled(needsSetup);
        if (needsSetup) APP_NOTICE("wifi: setup SSID=%s password=%s admin=%s URL=http://192.168.4.1/", apName, apPassword, adminKey);
        applyCredentials(credentials);
        if (fail(mdns_init())) { vTaskDelete(nullptr); return; }
        mdns_hostname_set("astrocadekeyboard");
        mdns_instance_name_set("Astrocade Keyboard");
        mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
        if (!startServer()) { APP_NOTICE("web: server failed"); vTaskDelete(nullptr); return; }
        APP_NOTICE("web: admin key=%s; local address http://astrocadekeyboard.local/", adminKey);
        esp_ota_img_states_t state;
        if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
            state == ESP_OTA_IMG_PENDING_VERIFY) esp_ota_mark_app_valid_cancel_rollback();
        int64_t offlineSince = esp_timer_get_time(), nextConnect = offlineSince + 5000000;
        for (;;) {
            Credentials next{};
            if (xQueueReceive(networkChanges, &next, pdMS_TO_TICKS(500)) == pdTRUE) {
                setupAp(true); applyCredentials(next); connected.store(false);
                offlineSince = esp_timer_get_time(); nextConnect = offlineSince + 5000000;
                connectedSince = 0;
            }
            const auto now = esp_timer_get_time();
            if (connected.load()) {
                offlineSince = now;
                if (!connectedSince) connectedSince = now;
                if (now - connectedSince > 30000000) setupAp(false);
            } else {
                connectedSince = 0;
                if (now - offlineSince > 20000000) setupAp(true);
                if (credentials.ssid[0] && now >= nextConnect) {
                    esp_wifi_connect(); nextConnect = now + 5000000;
                }
            }
        }
    }
}
#endif

bool webPortalInit()
{
#if ASTROCADE_WEB_ENABLED
    // Detect once, before HTTP starts; polling must not perform flash transactions.
    esp_flash_get_physical_size(nullptr, &physicalFlashBytes);
    networkMutex = xSemaphoreCreateMutex();
    networkChanges = xQueueCreate(1, sizeof(Credentials));
    if (!networkMutex || !networkChanges) return false;
    return xTaskCreatePinnedToCore(networkTask, "web_network", 6144, nullptr, 3, nullptr, 0) == pdPASS;
#else
    return true;
#endif
}
