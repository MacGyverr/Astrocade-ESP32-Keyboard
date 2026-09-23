#pragma once

#ifndef ASTROCADE_FIRMWARE_VERSION
#define ASTROCADE_FIRMWARE_VERSION "1.0.0"
#endif
#ifndef ASTROCADE_CLASSIC_ESP32
#define ASTROCADE_CLASSIC_ESP32 0
#endif
#ifndef ASTROCADE_BOARD_N8R2
#define ASTROCADE_BOARD_N8R2 0
#endif
#ifndef ASTROCADE_TEXT_USE_PSRAM
#define ASTROCADE_TEXT_USE_PSRAM 1
#endif

#ifndef ASTROCADE_USB_ENABLED
#define ASTROCADE_USB_ENABLED 1
#endif
#if ASTROCADE_CLASSIC_ESP32 && ASTROCADE_USB_ENABLED
#error Classic ESP32 has no native USB host
#endif
#if ASTROCADE_CLASSIC_ESP32 && ASTROCADE_BOARD_N8R2
#error N8R2 board profile requires ESP32-S3
#endif
#if ASTROCADE_USB_ENABLED != 0 && ASTROCADE_USB_ENABLED != 1
#error ASTROCADE_USB_ENABLED must be 0 or 1
#endif

#ifndef ASTROCADE_CROSSPOINT_A0_GPIO
#define ASTROCADE_CROSSPOINT_A0_GPIO 15
#endif
#ifndef ASTROCADE_CROSSPOINT_A1_GPIO
#define ASTROCADE_CROSSPOINT_A1_GPIO 7
#endif
#ifndef ASTROCADE_CROSSPOINT_A2_GPIO
#define ASTROCADE_CROSSPOINT_A2_GPIO 6
#endif
#ifndef ASTROCADE_CROSSPOINT_A3_GPIO
#define ASTROCADE_CROSSPOINT_A3_GPIO 5
#endif
#ifndef ASTROCADE_CROSSPOINT_A4_GPIO
#define ASTROCADE_CROSSPOINT_A4_GPIO 4
#endif
#ifndef ASTROCADE_CROSSPOINT_A5_GPIO
#define ASTROCADE_CROSSPOINT_A5_GPIO 16
#endif
#ifndef ASTROCADE_CROSSPOINT_DATA_GPIO
#define ASTROCADE_CROSSPOINT_DATA_GPIO 17
#endif
#ifndef ASTROCADE_CROSSPOINT_STROBE_GPIO
#define ASTROCADE_CROSSPOINT_STROBE_GPIO 18
#endif

#ifndef ASTROCADE_WEB_ENABLED
#define ASTROCADE_WEB_ENABLED 1
#endif
#ifndef ASTROCADE_TEXT_MAX_BYTES
#define ASTROCADE_TEXT_MAX_BYTES 262144
#endif
#ifndef ASTROCADE_TEXT_LINE_GAP_MS
#define ASTROCADE_TEXT_LINE_GAP_MS 100
#endif

#include <cstddef>
#include <cstdint>

#ifndef ASTROCADE_SERIAL_DEBUG_ENABLED
#define ASTROCADE_SERIAL_DEBUG_ENABLED 1
#endif

#ifndef ASTROCADE_STATUS_LED_ENABLED
#define ASTROCADE_STATUS_LED_ENABLED ASTROCADE_BOARD_N8R2
#endif
#if ASTROCADE_STATUS_LED_ENABLED && !ASTROCADE_BOARD_N8R2
#error RGB GPIO48 is only enabled for the verified N8R2 board profile
#endif

#ifndef ASTROCADE_CROSSPOINT_STROBE_LED_ENABLED
#define ASTROCADE_CROSSPOINT_STROBE_LED_ENABLED 0
#endif

#ifndef ASTROCADE_KEY_DEBUG_ENABLED
#define ASTROCADE_KEY_DEBUG_ENABLED ASTROCADE_SERIAL_DEBUG_ENABLED
#endif

#ifndef ASTROCADE_CROSSPOINT_DEBUG_ENABLED
#define ASTROCADE_CROSSPOINT_DEBUG_ENABLED 0
#endif

#ifndef ASTROCADE_CROSSPOINT_SETUP_DELAY_US
#define ASTROCADE_CROSSPOINT_SETUP_DELAY_US 1
#endif

#ifndef ASTROCADE_CROSSPOINT_STROBE_LOW_US
#define ASTROCADE_CROSSPOINT_STROBE_LOW_US 1
#endif

#ifndef ASTROCADE_CROSSPOINT_RECOVERY_DELAY_US
#define ASTROCADE_CROSSPOINT_RECOVERY_DELAY_US 1
#endif

#ifndef ASTROCADE_KEYPRESS_DOWN_MS
#define ASTROCADE_KEYPRESS_DOWN_MS 15
#endif

#ifndef ASTROCADE_KEYPRESS_GAP_MS
#define ASTROCADE_KEYPRESS_GAP_MS 5
#endif

#ifndef ASTROCADE_BACKSPACE_LEFT_ERASE_ENABLED
#define ASTROCADE_BACKSPACE_LEFT_ERASE_ENABLED 1
#endif

#ifndef ASTROCADE_USB_CAPS_LOCK_LED_DEFAULT_ON
#define ASTROCADE_USB_CAPS_LOCK_LED_DEFAULT_ON 1
#endif

#ifndef ASTROCADE_USB_NUM_LOCK_LED_DEFAULT_ON
#define ASTROCADE_USB_NUM_LOCK_LED_DEFAULT_ON 1
#endif

#ifndef ASTROCADE_USB_NUM_LOCK_TOGGLE_ENABLED
#define ASTROCADE_USB_NUM_LOCK_TOGGLE_ENABLED 1
#endif

#ifndef ASTROCADE_USB_CAPS_LOCK_TOGGLE_ENABLED
#define ASTROCADE_USB_CAPS_LOCK_TOGGLE_ENABLED 1
#endif

#ifndef ASTROCADE_BLE_CAPS_LOCK_LED_DEFAULT_ON
#define ASTROCADE_BLE_CAPS_LOCK_LED_DEFAULT_ON \
    ASTROCADE_USB_CAPS_LOCK_LED_DEFAULT_ON
#endif

#ifndef ASTROCADE_BLE_NUM_LOCK_LED_DEFAULT_ON
#define ASTROCADE_BLE_NUM_LOCK_LED_DEFAULT_ON \
    ASTROCADE_USB_NUM_LOCK_LED_DEFAULT_ON
#endif

#ifndef ASTROCADE_BLE_NUM_LOCK_TOGGLE_ENABLED
#define ASTROCADE_BLE_NUM_LOCK_TOGGLE_ENABLED \
    ASTROCADE_USB_NUM_LOCK_TOGGLE_ENABLED
#endif

#ifndef ASTROCADE_BLE_CAPS_LOCK_TOGGLE_ENABLED
#define ASTROCADE_BLE_CAPS_LOCK_TOGGLE_ENABLED \
    ASTROCADE_USB_CAPS_LOCK_TOGGLE_ENABLED
#endif

#ifndef ASTROCADE_BLE_ENROLLMENT_WINDOW_SECONDS
#define ASTROCADE_BLE_ENROLLMENT_WINDOW_SECONDS 10
#endif

#ifndef ASTROCADE_BLE_REQUIRE_MITM
#define ASTROCADE_BLE_REQUIRE_MITM 1
#endif

#ifndef ASTROCADE_BLE_RESET_BUTTON_ENABLED
#define ASTROCADE_BLE_RESET_BUTTON_ENABLED 1
#endif

#ifndef ASTROCADE_BLE_RESET_BUTTON_GPIO
#define ASTROCADE_BLE_RESET_BUTTON_GPIO 0
#endif

#ifndef ASTROCADE_BLE_RESET_BUTTON_HOLD_MS
#define ASTROCADE_BLE_RESET_BUTTON_HOLD_MS 3000
#endif

#ifndef ASTROCADE_BLE_RECONNECT_SCAN_ENABLED
#define ASTROCADE_BLE_RECONNECT_SCAN_ENABLED 1
#endif

#ifndef ASTROCADE_BLE_RECONNECT_SCAN_SECONDS
#define ASTROCADE_BLE_RECONNECT_SCAN_SECONDS 5
#endif

#ifndef ASTROCADE_BLE_RECONNECT_DELAY_MS
#define ASTROCADE_BLE_RECONNECT_DELAY_MS 100
#endif

#ifndef ASTROCADE_BLE_VERBOSE_DEBUG_ENABLED
#define ASTROCADE_BLE_VERBOSE_DEBUG_ENABLED 0
#endif

namespace AppConfig
{
    constexpr int kCrosspointPins[] = {
        ASTROCADE_CROSSPOINT_A0_GPIO, ASTROCADE_CROSSPOINT_A1_GPIO,
        ASTROCADE_CROSSPOINT_A2_GPIO, ASTROCADE_CROSSPOINT_A3_GPIO,
        ASTROCADE_CROSSPOINT_A4_GPIO, ASTROCADE_CROSSPOINT_A5_GPIO,
        ASTROCADE_CROSSPOINT_DATA_GPIO, ASTROCADE_CROSSPOINT_STROBE_GPIO
    };

    constexpr bool crosspointPinsValid(const int (&pins)[8])
    {
        for (std::size_t i = 0; i < 8; ++i) {
            const int pin = pins[i];
            // Reserve each module family's memory, straps, console and board functions.
#if ASTROCADE_CLASSIC_ESP32
            if (!(pin == 4 || pin == 13 || pin == 14 || pin == 18 || pin == 19 ||
                  (pin >= 21 && pin <= 23) || (pin >= 25 && pin <= 27) ||
                  pin == 32 || pin == 33)) return false;
#else
            if (!((pin >= 1 && pin <= 21 && pin != 3 && pin != 19 && pin != 20) ||
                  (pin >= 38 && pin <= 42) || pin == 47)) return false;
#endif
            if (ASTROCADE_BLE_RESET_BUTTON_ENABLED && pin == ASTROCADE_BLE_RESET_BUTTON_GPIO)
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (pin == pins[j]) return false;
        }
        return true;
    }
    static_assert(crosspointPinsValid(kCrosspointPins),
        "Invalid crosspoint GPIO map: duplicate/reserved pin or BLE reset button conflict");

    constexpr const char *kBuildType = ASTROCADE_CLASSIC_ESP32 ? "ESP32 4MB BLE + Wi-Fi" :
        (ASTROCADE_USB_ENABLED ? "ESP32-S3 USB + BLE + Wi-Fi" : "ESP32-S3 BLE + Wi-Fi");
    constexpr const char *kBoardModel = ASTROCADE_BOARD_N8R2 ? "ESP32-S3-WROOM-1-N8R2" :
        (ASTROCADE_CLASSIC_ESP32 ? "ESP32-WROOM-32 family" : "ESP32-S3");

    constexpr uint32_t kStatusLedGpio = 48;

    constexpr uint32_t kCrosspointSetupDelayUs =
        ASTROCADE_CROSSPOINT_SETUP_DELAY_US;
    constexpr uint32_t kCrosspointStrobeLowUs =
        ASTROCADE_CROSSPOINT_STROBE_LOW_US;
    constexpr uint32_t kCrosspointRecoveryDelayUs =
        ASTROCADE_CROSSPOINT_RECOVERY_DELAY_US;

    constexpr uint32_t kKeypressDownMs =
        ASTROCADE_KEYPRESS_DOWN_MS;
    constexpr uint32_t kKeypressGapMs =
        ASTROCADE_KEYPRESS_GAP_MS;

    // First boot-time BLE enrollment window.
    constexpr uint32_t kBleEnrollmentWindowSeconds =
        ASTROCADE_BLE_ENROLLMENT_WINDOW_SECONDS;

    constexpr bool kBleRequireMitm =
        ASTROCADE_BLE_REQUIRE_MITM != 0;

    constexpr bool kBleCapsLockLedDefaultOn =
        ASTROCADE_BLE_CAPS_LOCK_LED_DEFAULT_ON != 0;

    constexpr bool kBleNumLockLedDefaultOn =
        ASTROCADE_BLE_NUM_LOCK_LED_DEFAULT_ON != 0;

    constexpr bool kBleNumLockToggleEnabled =
        ASTROCADE_BLE_NUM_LOCK_TOGGLE_ENABLED != 0;

    constexpr bool kBleCapsLockToggleEnabled =
        ASTROCADE_BLE_CAPS_LOCK_TOGGLE_ENABLED != 0;

    constexpr bool kBleResetButtonEnabled =
        ASTROCADE_BLE_RESET_BUTTON_ENABLED != 0;

    constexpr uint32_t kBleResetButtonGpio =
        ASTROCADE_BLE_RESET_BUTTON_GPIO;

    constexpr uint32_t kBleResetButtonHoldMs =
        ASTROCADE_BLE_RESET_BUTTON_HOLD_MS;

    constexpr bool kBleReconnectScanEnabled =
        ASTROCADE_BLE_RECONNECT_SCAN_ENABLED != 0;

    // Once enrollment is closed, only bonded keyboards are sought.
    constexpr uint32_t kBleReconnectScanSeconds =
        ASTROCADE_BLE_RECONNECT_SCAN_SECONDS;
    constexpr uint32_t kBleReconnectDelayMs =
        ASTROCADE_BLE_RECONNECT_DELAY_MS;

    // For BLE keyboards that require the host to display a passkey, the ESP
    // has no display. We use a known fixed code instead:
    //
    //     123456
    //
    // Put the keyboard in pairing mode, power the adapter, then type 123456
    // followed by Enter on the BLE keyboard if the keyboard requires it.
    constexpr uint32_t kBlePairingPasskey = 123456;

}
