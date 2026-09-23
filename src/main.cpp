#include "app_log.h"
#include "ble_keyboard.h"
#include "crosspoint.h"
#include "keyboard_router.h"
#include "overlay_basic.h"
#include "settings.h"
#include "status_led.h"
#include "usb_keyboard.h"
#include "web_portal.h"

#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
    [[noreturn]]
    void safeHalt()
    {
        overlayBasicEmergencyRelease();

        for (;;)
        {
            crosspointReleaseAll();
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

extern "C" void app_main(void)
{
    APP_NOTICE("boot: Astrocade keyboard adapter starting; console on CH343 COM at 115200");
    APP_NOTICE("boot: version=%s build=%s", ASTROCADE_FIRMWARE_VERSION, AppConfig::kBuildType);
    APP_NOTICE("boot: USB host=%s BLE=enabled Wi-Fi=%s",
               ASTROCADE_USB_ENABLED ? "enabled" : "disabled",
               ASTROCADE_WEB_ENABLED ? "enabled" : "disabled");
    APP_LOG("boot: BLE HID keyboards only; Bluetooth Classic/2.x/3.0 keyboards are not supported by ESP32-S3");

    // NimBLE stores BLE bonds in the normal NVS partition. The same NVS
    // partition also stores our preferred-keyboard address and future overlay
    // selection/configuration values.
    esp_err_t nvsResult = nvs_flash_init();

    if (nvsResult == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvsResult == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        if (nvs_flash_erase() != ESP_OK)
        {
            APP_NOTICE("nvs: erase failed");
            safeHalt();
        }

        nvsResult = nvs_flash_init();
    }

    if (nvsResult != ESP_OK)
    {
        APP_NOTICE("nvs: init failed");
        safeHalt();
    }

    APP_LOG("nvs: ready");

    if (!statusLedInit())
    {
        APP_LOG("rgb: disabled after init failure");
    }

    if (!settingsInit())
    {
        APP_NOTICE("settings: init failed");
        safeHalt();
    }

    APP_LOG("settings: ready");

    if (!crosspointInit())
    {
        APP_NOTICE("crosspoint: init failed");
        safeHalt();
    }

    APP_LOG("crosspoint: ready");

    keyboardRouterInit();
    APP_LOG("router: ready");

    // Overlay selection is already persistent even though BASIC is currently
    // the only implemented overlay. A later Wi-Fi configuration page can
    // change this setting without restructuring the input or hardware layers.
    switch (settingsGetOverlayMode())
    {
        case OverlayMode::Basic:
        default:
            if (!overlayBasicInit())
            {
                APP_NOTICE("overlay: BASIC init failed");
                safeHalt();
            }
            break;
    }

    APP_LOG("overlay: BASIC ready");

#if ASTROCADE_USB_ENABLED
    // USB is optional; BLE and web do not depend on the host stack.
    if (!usbKeyboardInit())
    {
        APP_NOTICE("usb: init failed; BLE and web input remain available");
    }
    else APP_LOG("usb: host ready");
#endif

    // BLE enrolls during startup, then reconnects only bonded keyboards.
    if (!bleKeyboardInit())
    {
        APP_NOTICE("ble: init failed; other enabled inputs remain available");
        // BLE failure should not destroy the USB fallback.
        keyboardRouterSourceDisconnected(
            InputSource::Ble
        );
    }
    else
    {
        APP_LOG("ble: manager ready");
    }

    if (!webPortalInit()) APP_NOTICE("web: init failed; keyboards remain available");

    // All work is callback/task driven from here.
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
