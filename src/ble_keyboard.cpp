#include "ble_keyboard.h"
#include "ble_transport.h"
#include "app_config.h"
#include "app_log.h"
#include "keyboard_router.h"
#include "settings.h"
#include "status_led.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace
{
    struct Message {
        bool input;
        ble_transport_event_t state;
        uint32_t generation;
        uint8_t report[8];
    };

    QueueHandle_t queue;
    std::atomic<bool> overflow{false};
    std::atomic<uint32_t> generation{0};
    bool capsLock = AppConfig::kBleCapsLockLedDefaultOn;
    bool numLock = AppConfig::kBleNumLockLedDefaultOn;

    void syncLocks()
    {
        keyboardRouterSetLockState(InputSource::Ble, capsLock, numLock);
        bleTransportSetLeds((capsLock ? 2 : 0) | (numLock ? 1 : 0));
    }

    void toggleCaps(InputSource source)
    {
        if (source == InputSource::Ble && AppConfig::kBleCapsLockToggleEnabled) {
            capsLock = !capsLock;
            syncLocks();
            APP_LOG("ble: caps lock=%u", capsLock ? 1U : 0U);
        }
    }

    void toggleNum(InputSource source)
    {
        if (source == InputSource::Ble && AppConfig::kBleNumLockToggleEnabled) {
            numLock = !numLock;
            syncLocks();
            APP_LOG("ble: num lock=%u", numLock ? 1U : 0U);
        }
    }

    void enqueue(const Message &message)
    {
        if (xQueueSend(queue, &message, 0) != pdTRUE) {
            generation.fetch_add(1);
            overflow.store(true);
            if (!message.input) {
                // State transitions must survive an input burst filling the queue.
                Message discarded{};
                xQueueReceive(queue, &discarded, 0);
                xQueueSend(queue, &message, 0);
            }
        }
    }

    void stateChanged(ble_transport_event_t event)
    {
        Message message{};
        message.state = event;
        enqueue(message);
    }

    void inputReceived(const uint8_t report[8])
    {
        Message message{};
        message.input = true;
        message.generation = generation.load();
        std::memcpy(message.report, report, sizeof(message.report));
        enqueue(message);
    }

    void worker(void *)
    {
        TickType_t heldSince = 0;
        bool held = false;
        bool clearing = false;
        for (;;) {
            Message message{};
            if (xQueueReceive(queue, &message, pdMS_TO_TICKS(20)) == pdTRUE) {
                if (overflow.exchange(false)) {
                    keyboardRouterSourceDisconnected(InputSource::Ble);
                    APP_LOG("ble: input queue overflow; released keys");
                }
                if (message.input) {
                    // Never replay old presses after an overflow lost their release.
                    if (message.generation == generation.load()) {
                        keyboardRouterHandleBootReport(InputSource::Ble, message.report, 8);
                    }
                } else {
                    switch (message.state) {
                    case BLE_TRANSPORT_SCAN_NEW:
                        statusLedSetBleStartupSlow();
                        break;
                    case BLE_TRANSPORT_SCAN_SAVED:
                    case BLE_TRANSPORT_CONNECTING:
                        statusLedSetBleStartupFast();
                        break;
                    case BLE_TRANSPORT_READY:
                        keyboardRouterSourceDisconnected(InputSource::Ble);
                        syncLocks();
                        statusLedOff();
                        break;
                    case BLE_TRANSPORT_DISCONNECTED:
                        keyboardRouterSourceDisconnected(InputSource::Ble);
                        statusLedOff();
                        break;
                    case BLE_TRANSPORT_NOT_FOUND:
                        statusLedShowBleStartupFailure();
                        break;
                    case BLE_TRANSPORT_CLEARED:
                        settingsClearPreferredBle();
                        APP_LOG("ble: bonds cleared; restarting for enrollment");
                        esp_restart();
                        break;
                    }
                }
            }

            if (AppConfig::kBleResetButtonEnabled && !clearing) {
                const bool pressed = gpio_get_level(
                    static_cast<gpio_num_t>(AppConfig::kBleResetButtonGpio)) == 0;
                if (pressed && !held) {
                    held = true;
                    heldSince = xTaskGetTickCount();
                } else if (!pressed) {
                    held = false;
                } else if (xTaskGetTickCount() - heldSince >=
                           pdMS_TO_TICKS(AppConfig::kBleResetButtonHoldMs)) {
                    clearing = true;
                    bleTransportClearBonds();
                }
            }
        }
    }
}

extern "C" void bleTransportLog(const char *format, ...)
{
#if ASTROCADE_SERIAL_DEBUG_ENABLED
    char message[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    APP_LOG("ble: %s", message);
#else
    (void)format;
#endif
}

extern "C" void bleTransportNotice(const char *format, ...)
{
    char message[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    APP_NOTICE("ble: %s", message);
}

bool bleKeyboardInit()
{
    queue = xQueueCreate(64, sizeof(Message));
    if (!queue) {
        return false;
    }
    if (AppConfig::kBleResetButtonEnabled) {
        gpio_config_t gpio{};
        gpio.pin_bit_mask = 1ULL << AppConfig::kBleResetButtonGpio;
        gpio.mode = GPIO_MODE_INPUT;
        gpio.pull_up_en = GPIO_PULLUP_ENABLE;
        if (gpio_config(&gpio) != ESP_OK) {
            vQueueDelete(queue);
            return false;
        }
    }

    TaskHandle_t task = nullptr;
    if (xTaskCreate(worker, "ble_keys", 4096, nullptr, 4, &task) != pdPASS) {
        vQueueDelete(queue);
        return false;
    }
    ble_transport_config_t config{};
    config.enrollment_seconds = AppConfig::kBleEnrollmentWindowSeconds;
    config.reconnect_seconds = AppConfig::kBleReconnectScanSeconds;
    config.reconnect_delay_ms = AppConfig::kBleReconnectDelayMs;
    config.reconnect = AppConfig::kBleReconnectScanEnabled;
    config.passkey = AppConfig::kBlePairingPasskey;
    config.mitm = AppConfig::kBleRequireMitm;
    config.verbose = ASTROCADE_BLE_VERBOSE_DEBUG_ENABLED != 0;
    config.state = stateChanged;
    config.report = inputReceived;

    keyboardRouterSetLockState(InputSource::Ble, capsLock, numLock);
    if (!bleTransportInit(&config)) {
        vTaskDelete(task);
        vQueueDelete(queue);
        return false;
    }
    keyboardRouterSetNumLockToggleCallback(toggleNum);
    keyboardRouterSetCapsLockToggleCallback(toggleCaps);
    APP_NOTICE("ble: pairing code %06lu; when prompted, type it on the keyboard and press Enter",
               static_cast<unsigned long>(config.passkey));
    return true;
}
