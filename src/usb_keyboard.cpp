#include "app_config.h"

#if ASTROCADE_USB_ENABLED
#include "usb_keyboard.h"

#include <atomic>
#include <cstdint>

#include "app_config.h"
#include "app_log.h"
#include "keyboard_router.h"
#include "keyboard_status.h"
#include "status_led.h"

#include "esp_err.h"
#include "esp_intr_alloc.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "usb/usb_host.h"
#include "usb/hid.h"
#include "usb/hid_host.h"

namespace
{
    hid_host_device_handle_t gKeyboardHandle = nullptr;
    QueueHandle_t gEventQueue = nullptr;
    QueueHandle_t gLedQueue = nullptr;
    std::atomic<bool> gAnyHidDeviceSeen{false};
    std::atomic<bool> gKeyboardReady{false};
    std::atomic<bool> gCapsLockLed{
        ASTROCADE_USB_CAPS_LOCK_LED_DEFAULT_ON != 0
    };
    std::atomic<bool> gNumLockLed{
        ASTROCADE_USB_NUM_LOCK_LED_DEFAULT_ON != 0
    };

    struct HidDriverEvent
    {
        hid_host_device_handle_t handle;
        hid_host_driver_event_t event;
    };

    void interfaceCallback(
        hid_host_device_handle_t handle,
        hid_host_interface_event_t event,
        void *)
    {
        switch (event)
        {
            case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            {
                uint8_t report[64] = {};
                std::size_t reportLength = 0;

                if (hid_host_device_get_raw_input_report_data(
                        handle,
                        report,
                        sizeof(report),
                        &reportLength) != ESP_OK)
                {
                    APP_LOG("usb: failed to read input report");
                    statusLedSignalUsbFailure();
                    keyboardRouterSourceDisconnected(
                        InputSource::Usb
                    );
                    return;
                }

                keyboardRouterHandleBootReport(
                    InputSource::Usb,
                    report,
                    reportLength
                );

                break;
            }

            case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
                APP_LOG("usb: transfer error");
                statusLedSignalUsbFailure();
                keyboardRouterSourceDisconnected(
                    InputSource::Usb
                );
                break;

            case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
                keyboardStatusSetUsb(false, nullptr);
                APP_NOTICE("usb: keyboard disconnected");
                gKeyboardReady.store(false);
                keyboardRouterSourceDisconnected(
                    InputSource::Usb
                );

                if (handle == gKeyboardHandle)
                {
                    gKeyboardHandle = nullptr;
                }

                hid_host_device_close(handle);
                break;

#ifdef HID_HOST_SUSPEND_RESUME_API_SUPPORTED
            case HID_HOST_INTERFACE_EVENT_SUSPENDED:
                APP_LOG("usb: keyboard suspended");
                gKeyboardReady.store(false);
                keyboardRouterSourceDisconnected(
                    InputSource::Usb
                );
                break;

            case HID_HOST_INTERFACE_EVENT_RESUMED:
                APP_LOG("usb: keyboard resumed");
                break;
#endif

            default:
                break;
        }
    }

    void driverCallback(
        hid_host_device_handle_t handle,
        hid_host_driver_event_t event,
        void *)
    {
        if (gEventQueue == nullptr)
        {
            return;
        }

        HidDriverEvent queued = {};
        queued.handle = handle;
        queued.event = event;

        (void)xQueueSend(
            gEventQueue,
            &queued,
            0
        );
    }

    void setKeyboardLockLeds(
        hid_host_device_handle_t handle,
        uint8_t leds)
    {
        esp_err_t result =
            hid_class_request_set_report(
                handle,
                HID_REPORT_TYPE_OUTPUT,
                0,
                &leds,
                sizeof(leds)
            );

        APP_LOG(
            "usb: set keyboard LEDs caps=%u num=%u scroll=%u result=%s",
            (leds & 0x02U) != 0 ? 1U : 0U,
            (leds & 0x01U) != 0 ? 1U : 0U,
            (leds & 0x04U) != 0 ? 1U : 0U,
            esp_err_to_name(result)
        );
    }

    uint8_t currentKeyboardLedByte()
    {
        uint8_t leds = 0;

        if (gNumLockLed.load())
        {
            leds |= 0x01U;
        }

        if (gCapsLockLed.load())
        {
            leds |= 0x02U;
        }

        return leds;
    }

    void queueKeyboardLedUpdate()
    {
        if (gLedQueue == nullptr)
        {
            return;
        }

        const uint8_t leds = currentKeyboardLedByte();
        (void)xQueueOverwrite(gLedQueue, &leds);
    }

    void syncRouterLockState()
    {
        keyboardRouterSetLockState(
            InputSource::Usb,
            gCapsLockLed.load(),
            gNumLockLed.load()
        );
    }

    void handleNumLockToggle(InputSource source)
    {
        if (source != InputSource::Usb)
        {
            return;
        }

#if ASTROCADE_USB_NUM_LOCK_TOGGLE_ENABLED
        const bool nextState = !gNumLockLed.load();
        gNumLockLed.store(nextState);
        syncRouterLockState();
        APP_LOG(
            "usb: num lock toggled to %u",
            nextState ? 1U : 0U
        );
        queueKeyboardLedUpdate();
#else
        APP_LOG("usb: num lock toggle ignored by build flag");
#endif
    }

    void handleCapsLockToggle(InputSource source)
    {
        if (source != InputSource::Usb)
        {
            return;
        }

#if ASTROCADE_USB_CAPS_LOCK_TOGGLE_ENABLED
        const bool nextState = !gCapsLockLed.load();
        gCapsLockLed.store(nextState);
        syncRouterLockState();
        APP_LOG(
            "usb: caps lock toggled to %u",
            nextState ? 1U : 0U
        );
        queueKeyboardLedUpdate();
#else
        APP_LOG("usb: caps lock toggle ignored by build flag");
#endif
    }

    void ledTask(void *)
    {
        for (;;)
        {
            uint8_t leds = 0;

            if (xQueueReceive(
                    gLedQueue,
                    &leds,
                    portMAX_DELAY) != pdTRUE)
            {
                continue;
            }

            const hid_host_device_handle_t handle = gKeyboardHandle;

            if (handle == nullptr ||
                !gKeyboardReady.load())
            {
                continue;
            }

            setKeyboardLockLeds(handle, leds);
        }
    }

    void usbLibraryTask(void *arg)
    {
        TaskHandle_t notifyTask =
            static_cast<TaskHandle_t>(arg);

        usb_host_config_t hostConfig = {};
        hostConfig.skip_phy_setup = false;
        hostConfig.intr_flags = ESP_INTR_FLAG_LOWMED;

        const esp_err_t result =
            usb_host_install(&hostConfig);

        APP_LOG(
            "usb: host library install %s",
            esp_err_to_name(result)
        );

        xTaskNotify(
            notifyTask,
            result == ESP_OK ? 1U : 2U,
            eSetValueWithOverwrite
        );

        if (result != ESP_OK)
        {
            statusLedSignalUsbFailure();
            vTaskDelete(nullptr);
            return;
        }

        for (;;)
        {
            uint32_t eventFlags = 0;

            const esp_err_t result =
                usb_host_lib_handle_events(
                    portMAX_DELAY,
                    &eventFlags);

            if (result != ESP_OK)
            {
                APP_LOG(
                    "usb: host event loop error: %s",
                    esp_err_to_name(result)
                );
                statusLedSignalUsbFailure();
                keyboardRouterSourceDisconnected(
                    InputSource::Usb
                );

                vTaskDelay(pdMS_TO_TICKS(10));
            }

            if (eventFlags != 0)
            {
                APP_LOG(
                    "usb: host event flags=0x%08lX no_clients=%u all_free=%u",
                    static_cast<unsigned long>(eventFlags),
                    (eventFlags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0 ? 1U : 0U,
                    (eventFlags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0 ? 1U : 0U
                );
            }
        }
    }

    void diagnosticsTask(void *)
    {
        vTaskDelay(pdMS_TO_TICKS(12000));
        uint32_t warningCount = 0;

        for (;;)
        {
            if (!gAnyHidDeviceSeen.load())
            {
                APP_LOG(
                    "usb: no HID attach event seen yet (%lu); if keyboard LEDs stay dark, check OTG VBUS/5V host power, USB-C host role/adapter, and GPIO19/GPIO20 D-/D+ wiring; after upload/reset try unplugging and replugging the OTG keyboard because this board does not expose firmware VBUS switching",
                    static_cast<unsigned long>(++warningCount)
                );
            }
            else if (!gKeyboardReady.load())
            {
                APP_LOG(
                    "usb: HID device was seen, but no boot keyboard is ready"
                );
            }

            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    void managerTask(void *)
    {
        for (;;)
        {
            HidDriverEvent queued = {};

            if (xQueueReceive(
                    gEventQueue,
                    &queued,
                    portMAX_DELAY) != pdTRUE)
            {
                continue;
            }

            APP_LOG(
                "usb: HID driver event=%d",
                static_cast<int>(queued.event)
            );

            if (queued.event !=
                HID_HOST_DRIVER_EVENT_CONNECTED)
            {
                continue;
            }

            gAnyHidDeviceSeen.store(true);
            APP_LOG("usb: HID device connected");
            statusLedSignalUsbDevice();

            hid_host_dev_params_t parameters = {};

            if (hid_host_device_get_params(
                    queued.handle,
                    &parameters) != ESP_OK)
            {
                APP_LOG("usb: failed to read HID device parameters");
                statusLedSignalUsbFailure();
                continue;
            }

            if (parameters.sub_class !=
                    HID_SUBCLASS_BOOT_INTERFACE ||
                parameters.proto !=
                    HID_PROTOCOL_KEYBOARD)
            {
                APP_LOG(
                    "usb: ignoring HID subclass=%u proto=%u",
                    static_cast<unsigned>(parameters.sub_class),
                    static_cast<unsigned>(parameters.proto)
                );
                continue;
            }

            if (gKeyboardHandle != nullptr)
            {
                APP_LOG("usb: keyboard already open; ignoring extra keyboard");
                continue;
            }

            keyboardRouterSourceDisconnected(
                InputSource::Usb
            );

            hid_host_device_config_t config = {};
            config.callback = interfaceCallback;
            config.callback_arg = nullptr;

            if (hid_host_device_open(
                    queued.handle,
                    &config) != ESP_OK)
            {
                APP_LOG("usb: failed to open keyboard");
                statusLedSignalUsbFailure();
                continue;
            }

            gKeyboardHandle = queued.handle;
            APP_LOG("usb: keyboard opened");

            if (hid_class_request_set_protocol(
                    queued.handle,
                    HID_REPORT_PROTOCOL_BOOT) != ESP_OK)
            {
                APP_LOG("usb: failed to switch keyboard to boot protocol");
                statusLedSignalUsbFailure();
                hid_host_device_close(queued.handle);
                gKeyboardHandle = nullptr;
                continue;
            }

            (void)hid_class_request_set_idle(
                queued.handle,
                0,
                0
            );

            if (hid_host_device_start(
                    queued.handle) != ESP_OK)
            {
                APP_LOG("usb: failed to start keyboard");
                statusLedSignalUsbFailure();
                hid_host_device_close(queued.handle);
                gKeyboardHandle = nullptr;
                continue;
            }

            hid_host_dev_info_t info{};
            char product[96]{};
            if (hid_host_get_device_info(queued.handle, &info) == ESP_OK) {
                // USB strings are exposed as wchar_t by the stock HID host.
                std::size_t used = 0;
                for (const auto ch : info.iProduct) {
                    if (!ch || used + 4 >= sizeof(product)) break;
                    const uint32_t cp = static_cast<uint32_t>(ch);
                    if (cp < 0x80) product[used++] = cp >= 32 ? char(cp) : ' ';
                    else if (cp < 0x800) {
                        product[used++] = char(0xC0 | (cp >> 6));
                        product[used++] = char(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000 && (cp < 0xD800 || cp > 0xDFFF)) {
                        product[used++] = char(0xE0 | (cp >> 12));
                        product[used++] = char(0x80 | ((cp >> 6) & 0x3F));
                        product[used++] = char(0x80 | (cp & 0x3F));
                    } else product[used++] = '?';
                }
            }
            keyboardStatusSetUsb(true, product);
            APP_NOTICE("usb: keyboard detected and ready: %s", product[0] ? product : "(name unavailable)");
            gKeyboardReady.store(true);
            setKeyboardLockLeds(
                queued.handle,
                currentKeyboardLedByte()
            );
            statusLedSignalUsbKeyboardReady();
        }
    }
}

bool usbKeyboardInit()
{
    gEventQueue =
        xQueueCreate(
            8,
            sizeof(HidDriverEvent)
        );

    if (gEventQueue == nullptr)
    {
        APP_LOG("usb: event queue allocation failed");
        return false;
    }

    gLedQueue =
        xQueueCreate(
            1,
            sizeof(uint8_t)
        );

    if (gLedQueue == nullptr)
    {
        APP_LOG("usb: LED queue allocation failed");
        return false;
    }

    keyboardRouterSetNumLockToggleCallback(handleNumLockToggle);
    keyboardRouterSetCapsLockToggleCallback(handleCapsLockToggle);
    syncRouterLockState();

    APP_LOG("usb: starting native OTG host on GPIO19/GPIO20");

    if (xTaskCreate(
            usbLibraryTask,
            "usb_host_events",
            4096,
            xTaskGetCurrentTaskHandle(),
            4,
            nullptr) != pdPASS)
    {
        APP_LOG("usb: failed to create host event task");
        return false;
    }

    uint32_t notification = 0;

    if (xTaskNotifyWait(
            0,
            0xFFFFFFFFU,
            &notification,
            pdMS_TO_TICKS(5000)) != pdTRUE ||
        notification != 1U)
    {
        APP_LOG("usb: host library did not become ready");
        return false;
    }

    hid_host_driver_config_t hidConfig = {};
    hidConfig.create_background_task = true;
    hidConfig.task_priority = 5;
    hidConfig.stack_size = 4096;
    hidConfig.core_id = tskNO_AFFINITY;
    hidConfig.callback = driverCallback;
    hidConfig.callback_arg = nullptr;

    if (hid_host_install(&hidConfig) != ESP_OK)
    {
        APP_LOG("usb: HID host install failed");
        statusLedSignalUsbFailure();
        return false;
    }

    APP_LOG("usb: HID host installed");

    if (xTaskCreate(
            ledTask,
            "usb_leds",
            3072,
            nullptr,
            3,
            nullptr) != pdPASS)
    {
        APP_LOG("usb: failed to create LED task");
        return false;
    }

    if (xTaskCreate(
            diagnosticsTask,
            "usb_diag",
            3072,
            nullptr,
            2,
            nullptr) != pdPASS)
    {
        APP_LOG("usb: failed to create diagnostic task");
    }

    const bool managerStarted =
        xTaskCreate(
        managerTask,
        "usb_keyboard",
        4096,
        nullptr,
        4,
        nullptr
    ) == pdPASS;

    if (!managerStarted)
    {
        APP_LOG("usb: failed to create keyboard manager task");
    }

    return managerStarted;
}
#endif // ASTROCADE_USB_ENABLED
