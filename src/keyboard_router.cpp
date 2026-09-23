#include "keyboard_router.h"

#include <cstddef>
#include <cstring>

#include "app_config.h"
#include "app_log.h"
#include "overlay_basic.h"
#include "status_led.h"

namespace
{
    struct KeyboardState
    {
        uint8_t previousUsages[6];
        uint8_t modifiers;
    };

    struct KeyboardLockState
    {
        bool capsLock;
        bool numLock;
    };

    KeyboardState gState[
        static_cast<std::size_t>(InputSource::Count)
    ] = {};

    KeyboardLockState gLockState[
        static_cast<std::size_t>(InputSource::Count)
    ] = {};

    constexpr std::size_t kMaxLockToggleCallbacks = 4;

    KeyboardRouterLockToggleCallback
        gNumLockToggleCallbacks[kMaxLockToggleCallbacks] = {};
    KeyboardRouterLockToggleCallback
        gCapsLockToggleCallbacks[kMaxLockToggleCallbacks] = {};
    std::size_t gNumLockToggleCallbackCount = 0;
    std::size_t gCapsLockToggleCallbackCount = 0;

    constexpr uint8_t kHidKeyCapsLock = 0x39;
    constexpr uint8_t kHidKeyNumLock = 0x53;
    constexpr uint8_t kHidKeyInsert = 0x49;
    constexpr uint8_t kHidKeyHome = 0x4A;
    constexpr uint8_t kHidKeyPageUp = 0x4B;
    constexpr uint8_t kHidKeyDelete = 0x4C;
    constexpr uint8_t kHidKeyEnd = 0x4D;
    constexpr uint8_t kHidKeyPageDown = 0x4E;
    constexpr uint8_t kHidKeyArrowRight = 0x4F;
    constexpr uint8_t kHidKeyArrowLeft = 0x50;
    constexpr uint8_t kHidKeyArrowDown = 0x51;
    constexpr uint8_t kHidKeyArrowUp = 0x52;
    constexpr uint8_t kHidKeyKp1 = 0x59;
    constexpr uint8_t kHidKeyKp2 = 0x5A;
    constexpr uint8_t kHidKeyKp3 = 0x5B;
    constexpr uint8_t kHidKeyKp4 = 0x5C;
    constexpr uint8_t kHidKeyKp5 = 0x5D;
    constexpr uint8_t kHidKeyKp6 = 0x5E;
    constexpr uint8_t kHidKeyKp7 = 0x5F;
    constexpr uint8_t kHidKeyKp8 = 0x60;
    constexpr uint8_t kHidKeyKp9 = 0x61;
    constexpr uint8_t kHidKeyKp0 = 0x62;
    constexpr uint8_t kHidKeyKpDecimal = 0x63;

    bool usageExists(
        const uint8_t *usages,
        std::size_t count,
        uint8_t usage)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            if (usages[i] == usage)
            {
                return true;
            }
        }

        return false;
    }

    KeyboardState &stateFor(InputSource source)
    {
        return gState[
            static_cast<std::size_t>(source)
        ];
    }

    void registerLockToggleCallback(
        KeyboardRouterLockToggleCallback *callbacks,
        std::size_t *callbackCount,
        KeyboardRouterLockToggleCallback callback)
    {
        if (callback == nullptr ||
            callbacks == nullptr ||
            callbackCount == nullptr)
        {
            return;
        }

        for (std::size_t i = 0; i < *callbackCount; ++i)
        {
            if (callbacks[i] == callback)
            {
                return;
            }
        }

        if (*callbackCount >= kMaxLockToggleCallbacks)
        {
            APP_LOG("router: lock callback table full");
            return;
        }

        callbacks[*callbackCount] = callback;
        ++(*callbackCount);
    }

    void notifyLockToggleCallbacks(
        KeyboardRouterLockToggleCallback *callbacks,
        std::size_t callbackCount,
        InputSource source)
    {
        for (std::size_t i = 0; i < callbackCount; ++i)
        {
            if (callbacks[i] != nullptr)
            {
                callbacks[i](source);
            }
        }
    }

    KeyboardLockState &lockStateFor(InputSource source)
    {
        return gLockState[
            static_cast<std::size_t>(source)
        ];
    }

    const char *sourceName(InputSource source)
    {
        switch (source)
        {
            case InputSource::Usb:
                return "usb";

            case InputSource::Ble:
                return "ble";

            case InputSource::Count:
            default:
                return "?";
        }
    }

    constexpr uint8_t kShiftMask = 0x22;

    const char *usageName(
        InputSource source,
        uint8_t usage,
        uint8_t modifiers)
    {
        const bool shift =
            (modifiers & kShiftMask) != 0;

        const KeyboardLockState &lockState =
            lockStateFor(source);

        static const char *const upperLetters[] = {
            "A", "B", "C", "D", "E", "F", "G", "H",
            "I", "J", "K", "L", "M", "N", "O", "P",
            "Q", "R", "S", "T", "U", "V", "W", "X",
            "Y", "Z"
        };

        static const char *const lowerLetters[] = {
            "a", "b", "c", "d", "e", "f", "g", "h",
            "i", "j", "k", "l", "m", "n", "o", "p",
            "q", "r", "s", "t", "u", "v", "w", "x",
            "y", "z"
        };

        static const char *const digits[] = {
            "1", "2", "3", "4", "5",
            "6", "7", "8", "9", "0"
        };

        static const char *const shiftedDigits[] = {
            "!", "@", "#", "$", "%",
            "^", "&", "*", "(", ")"
        };

        if (usage >= 0x04 && usage <= 0x1D)
        {
            const bool uppercase =
                shift != lockState.capsLock;

            return uppercase
                ? upperLetters[usage - 0x04]
                : lowerLetters[usage - 0x04];
        }

        if (usage >= 0x1E && usage <= 0x27)
        {
            return shift
                ? shiftedDigits[usage - 0x1E]
                : digits[usage - 0x1E];
        }

        switch (usage)
        {
            case 0x28:
                return "ENTER";
            case 0x29:
                return "ESC";
            case 0x2A:
                return "BACKSPACE";
            case 0x2B:
                return "TAB";
            case 0x2C:
                return "SPACE";
            case 0x2D:
                return shift ? "_" : "-";
            case 0x2E:
                return shift ? "+" : "=";
            case 0x2F:
                return shift ? "{" : "[";
            case 0x30:
                return shift ? "}" : "]";
            case 0x31:
                return shift ? "|" : "\\";
            case 0x33:
                return shift ? ":" : ";";
            case 0x34:
                return shift ? "\"" : "'";
            case 0x35:
                return shift ? "~" : "`";
            case 0x36:
                return shift ? "<" : ",";
            case 0x37:
                return shift ? ">" : ".";
            case 0x38:
                return shift ? "?" : "/";
            case 0x39:
                return "CAPSLOCK";
            case 0x3A:
                return "F1";
            case 0x3B:
                return "F2";
            case 0x3C:
                return "F3";
            case 0x3D:
                return "F4";
            case 0x3E:
                return "F5";
            case 0x3F:
                return "F6";
            case 0x40:
                return "F7";
            case 0x41:
                return "F8";
            case 0x42:
                return "F9";
            case 0x43:
                return "F10";
            case 0x44:
                return "F11";
            case 0x45:
                return "F12";
            case 0x46:
                return "PRINTSCREEN";
            case 0x47:
                return "SCROLLLOCK";
            case kHidKeyInsert:
                return "INSERT";
            case kHidKeyHome:
                return "HOME";
            case kHidKeyPageUp:
                return "PAGEUP";
            case kHidKeyDelete:
                return "DELETE";
            case kHidKeyEnd:
                return "END";
            case kHidKeyPageDown:
                return "PAGEDOWN";
            case kHidKeyArrowRight:
                return "RIGHT";
            case kHidKeyArrowLeft:
                return "LEFT";
            case kHidKeyArrowDown:
                return "DOWN";
            case kHidKeyArrowUp:
                return "UP";
            case 0x53:
                return "NUMLOCK";
            case 0x54:
                return "KP_DIVIDE";
            case 0x55:
                return "KP_MULTIPLY";
            case 0x56:
                return "KP_MINUS";
            case 0x57:
                return "KP_PLUS";
            case 0x58:
                return "KP_ENTER";
            case kHidKeyKp1:
                return lockState.numLock ? "KP_1" : "END";
            case kHidKeyKp2:
                return lockState.numLock ? "KP_2" : "DOWN";
            case kHidKeyKp3:
                return lockState.numLock ? "KP_3" : "PAGEDOWN";
            case kHidKeyKp4:
                return lockState.numLock ? "KP_4" : "LEFT";
            case kHidKeyKp5:
                return lockState.numLock ? "KP_5" : "CLEAR";
            case kHidKeyKp6:
                return lockState.numLock ? "KP_6" : "RIGHT";
            case kHidKeyKp7:
                return lockState.numLock ? "KP_7" : "HOME";
            case kHidKeyKp8:
                return lockState.numLock ? "KP_8" : "UP";
            case kHidKeyKp9:
                return lockState.numLock ? "KP_9" : "PAGEUP";
            case kHidKeyKp0:
                return lockState.numLock ? "KP_0" : "INSERT";
            case kHidKeyKpDecimal:
                return lockState.numLock ? "KP_DECIMAL" : "DELETE";
            case 0x67:
                return "KP_EQUAL";
            default:
                return "?";
        }
    }

    uint8_t overlayUsageFor(
        InputSource source,
        uint8_t usage)
    {
        const KeyboardLockState &lockState =
            lockStateFor(source);

        if (lockState.numLock)
        {
            return usage;
        }

        switch (usage)
        {
            case kHidKeyKp1:
                return kHidKeyEnd;
            case kHidKeyKp2:
                return kHidKeyArrowDown;
            case kHidKeyKp3:
                return kHidKeyPageDown;
            case kHidKeyKp4:
                return kHidKeyArrowLeft;
            case kHidKeyKp6:
                return kHidKeyArrowRight;
            case kHidKeyKp7:
                return kHidKeyHome;
            case kHidKeyKp8:
                return kHidKeyArrowUp;
            case kHidKeyKp9:
                return kHidKeyPageUp;
            case kHidKeyKp0:
                return kHidKeyInsert;
            case kHidKeyKpDecimal:
                return kHidKeyDelete;
            default:
                return usage;
        }
    }

    void clearState(InputSource source)
    {
        std::memset(
            &stateFor(source),
            0,
            sizeof(KeyboardState)
        );
    }
}

void keyboardRouterInit()
{
    std::memset(gState, 0, sizeof(gState));
    std::memset(gLockState, 0, sizeof(gLockState));
    std::memset(gNumLockToggleCallbacks, 0, sizeof(gNumLockToggleCallbacks));
    std::memset(gCapsLockToggleCallbacks, 0, sizeof(gCapsLockToggleCallbacks));
    gNumLockToggleCallbackCount = 0;
    gCapsLockToggleCallbackCount = 0;
}

void keyboardRouterSetNumLockToggleCallback(
    KeyboardRouterLockToggleCallback callback)
{
    registerLockToggleCallback(
        gNumLockToggleCallbacks,
        &gNumLockToggleCallbackCount,
        callback
    );
}

void keyboardRouterSetCapsLockToggleCallback(
    KeyboardRouterLockToggleCallback callback)
{
    registerLockToggleCallback(
        gCapsLockToggleCallbacks,
        &gCapsLockToggleCallbackCount,
        callback
    );
}

void keyboardRouterSetLockState(
    InputSource source,
    bool capsLock,
    bool numLock)
{
    const std::size_t index =
        static_cast<std::size_t>(source);

    if (index >= static_cast<std::size_t>(InputSource::Count))
    {
        return;
    }

    gLockState[index].capsLock = capsLock;
    gLockState[index].numLock = numLock;
}

void keyboardRouterSourceDisconnected(InputSource source)
{
    overlayBasicSourceDisconnected(source);
    clearState(source);
}

void keyboardRouterHandleBootReport(
    InputSource source,
    const uint8_t *data,
    std::size_t length)
{
    // Standard Boot Keyboard report:
    // byte 0 = modifiers
    // byte 1 = reserved
    // byte 2..7 = six key usages
    if (data == nullptr || length < 8)
    {
        keyboardRouterSourceDisconnected(source);
        return;
    }

    const uint8_t *newUsages = &data[2];

    // 0x01-0x03 are HID rollover/error indicators.
    for (std::size_t i = 0; i < 6; ++i)
    {
        if (newUsages[i] >= 0x01 &&
            newUsages[i] <= 0x03)
        {
            keyboardRouterSourceDisconnected(source);
            return;
        }
    }

    KeyboardState &state = stateFor(source);

    const uint8_t newModifiers = data[0];

    // Releases first.
    for (std::size_t i = 0; i < 6; ++i)
    {
        const uint8_t usage =
            state.previousUsages[i];

        if (usage == 0)
        {
            continue;
        }

        if (!usageExists(newUsages, 6, usage))
        {
            const uint8_t overlayUsage =
                overlayUsageFor(source, usage);

#if ASTROCADE_KEY_DEBUG_ENABLED
            APP_LOG(
                "key: %s up usage=0x%02X name=%s mods=0x%02X",
                sourceName(source),
                static_cast<unsigned>(usage),
                usageName(source, usage, newModifiers),
                static_cast<unsigned>(newModifiers)
            );
#endif

            if (usage == kHidKeyNumLock ||
                usage == kHidKeyCapsLock)
            {
                continue;
            }

            overlayBasicKeyUp(
                source,
                overlayUsage,
                newModifiers
            );
        }
    }

    state.modifiers = newModifiers;

    bool sawNewPress = false;

    // New presses.
    for (std::size_t i = 0; i < 6; ++i)
    {
        const uint8_t usage = newUsages[i];

        if (usage == 0)
        {
            continue;
        }

        if (!usageExists(
                state.previousUsages,
                6,
                usage))
        {
            const uint8_t overlayUsage =
                overlayUsageFor(source, usage);

#if ASTROCADE_KEY_DEBUG_ENABLED
            char debugPlan[224] = {};
            (void)overlayBasicDescribeKeyDown(
                overlayUsage,
                state.modifiers,
                debugPlan,
                sizeof(debugPlan)
            );

            APP_LOG(
                "key: %s down usage=0x%02X name=%s mods=0x%02X %s",
                sourceName(source),
                static_cast<unsigned>(usage),
                usageName(source, usage, state.modifiers),
                static_cast<unsigned>(state.modifiers),
                debugPlan
            );
#endif

            if (usage == kHidKeyNumLock)
            {
                notifyLockToggleCallbacks(
                    gNumLockToggleCallbacks,
                    gNumLockToggleCallbackCount,
                    source
                );
            }
            else if (usage == kHidKeyCapsLock)
            {
                notifyLockToggleCallbacks(
                    gCapsLockToggleCallbacks,
                    gCapsLockToggleCallbackCount,
                    source
                );
            }
            else
            {
                overlayBasicKeyDown(
                    source,
                    overlayUsage,
                    state.modifiers
                );
            }

            sawNewPress = true;
        }
    }

    if (sawNewPress)
    {
        switch (source)
        {
            case InputSource::Usb:
                statusLedSignalUsbKeypress();
                break;

            case InputSource::Ble:
                statusLedSignalBleInput();
                break;

            case InputSource::Count:
                break;
        }
    }

    std::memcpy(
        state.previousUsages,
        newUsages,
        sizeof(state.previousUsages)
    );
}
