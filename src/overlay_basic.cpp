#include "overlay_basic.h"
#include "text_transfer.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <algorithm>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "app_config.h"
#include "crosspoint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// -----------------------------------------------------------------------------
// Matrix
//
//                C0          C1          C2          C3
//   Row 0        GO          PAUSE       HALT        DIVIDE
//   Row 1        7           8           9           MULTIPLY
//   Row 2        4           5           6           MINUS
//   Row 3        1           2           3           PLUS
//   Row 4        SPACE       0           ERASE       EQUALS
//   Row 5        GREEN       RED         BLUE        WORDS
// -----------------------------------------------------------------------------

namespace
{
    constexpr MatrixPosition kGreenShift = {5, 0};
    constexpr MatrixPosition kRedShift   = {5, 1};
    constexpr MatrixPosition kBlueShift  = {5, 2};
    constexpr MatrixPosition kWordsShift = {5, 3};

    constexpr MatrixPosition kGo       = {0, 0};
    constexpr MatrixPosition kPause    = {0, 1};
    constexpr MatrixPosition kHalt     = {0, 2};
    constexpr MatrixPosition kDivide   = {0, 3};

    constexpr MatrixPosition kSeven    = {1, 0};
    constexpr MatrixPosition kEight    = {1, 1};
    constexpr MatrixPosition kNine     = {1, 2};
    constexpr MatrixPosition kMultiply = {1, 3};

    constexpr MatrixPosition kFour     = {2, 0};
    constexpr MatrixPosition kFive     = {2, 1};
    constexpr MatrixPosition kSix      = {2, 2};
    constexpr MatrixPosition kMinus    = {2, 3};

    constexpr MatrixPosition kOne      = {3, 0};
    constexpr MatrixPosition kTwo      = {3, 1};
    constexpr MatrixPosition kThree    = {3, 2};
    constexpr MatrixPosition kPlus     = {3, 3};

    constexpr MatrixPosition kSpace    = {4, 0};
    constexpr MatrixPosition kZero     = {4, 1};
    constexpr MatrixPosition kErase    = {4, 2};
    constexpr MatrixPosition kEquals   = {4, 3};

    enum class BasicPlane : uint8_t
    {
        Default,
        Green,
        Red,
        Blue,
        Words
    };

    struct BasicAction
    {
        BasicPlane plane;
        MatrixPosition target;
    };


    // USB HID keyboard usage IDs.
    enum : uint8_t
    {
        HID_KEY_A             = 0x04,
        HID_KEY_Z             = 0x1D,

        HID_KEY_1             = 0x1E,
        HID_KEY_2             = 0x1F,
        HID_KEY_3             = 0x20,
        HID_KEY_4             = 0x21,
        HID_KEY_5             = 0x22,
        HID_KEY_6             = 0x23,
        HID_KEY_7             = 0x24,
        HID_KEY_8             = 0x25,
        HID_KEY_9             = 0x26,
        HID_KEY_0             = 0x27,

        HID_KEY_ENTER         = 0x28,
        HID_KEY_ESCAPE        = 0x29,
        HID_KEY_BACKSPACE     = 0x2A,
        HID_KEY_SPACE         = 0x2C,

        HID_KEY_MINUS         = 0x2D,
        HID_KEY_EQUAL         = 0x2E,
        HID_KEY_LEFT_BRACKET  = 0x2F,
        HID_KEY_RIGHT_BRACKET = 0x30,
        HID_KEY_BACKSLASH     = 0x31,
        HID_KEY_SEMICOLON     = 0x33,
        HID_KEY_APOSTROPHE    = 0x34,
        HID_KEY_COMMA         = 0x36,
        HID_KEY_PERIOD        = 0x37,
        HID_KEY_SLASH         = 0x38,

        HID_KEY_F1            = 0x3A,
        HID_KEY_F2            = 0x3B,
        HID_KEY_F3            = 0x3C,
        HID_KEY_F4            = 0x3D,
        HID_KEY_F5            = 0x3E,
        HID_KEY_F6            = 0x3F,
        HID_KEY_F7            = 0x40,
        HID_KEY_F8            = 0x41,
        HID_KEY_F9            = 0x42,
        HID_KEY_F10           = 0x43,
        HID_KEY_F11           = 0x44,
        HID_KEY_F12           = 0x45,

        HID_KEY_PAUSE         = 0x48,
        HID_KEY_DELETE        = 0x4C,

        HID_KEY_ARROW_RIGHT   = 0x4F,
        HID_KEY_ARROW_LEFT    = 0x50,
        HID_KEY_ARROW_DOWN    = 0x51,
        HID_KEY_ARROW_UP      = 0x52,

        HID_KEY_KP_DIVIDE     = 0x54,
        HID_KEY_KP_MULTIPLY   = 0x55,
        HID_KEY_KP_MINUS      = 0x56,
        HID_KEY_KP_PLUS       = 0x57,
        HID_KEY_KP_ENTER      = 0x58,

        HID_KEY_KP_1          = 0x59,
        HID_KEY_KP_2          = 0x5A,
        HID_KEY_KP_3          = 0x5B,
        HID_KEY_KP_4          = 0x5C,
        HID_KEY_KP_5          = 0x5D,
        HID_KEY_KP_6          = 0x5E,
        HID_KEY_KP_7          = 0x5F,
        HID_KEY_KP_8          = 0x60,
        HID_KEY_KP_9          = 0x61,
        HID_KEY_KP_0          = 0x62,
        HID_KEY_KP_DECIMAL    = 0x63,

        HID_KEY_KP_EQUAL      = 0x67
    };

    constexpr uint8_t kCtrlMask  = 0x11;
    constexpr uint8_t kShiftMask = 0x22;

    constexpr BasicAction kLetterActions[26] = {
        {BasicPlane::Green, {1, 0}}, // A
        {BasicPlane::Red,   {1, 0}}, // B
        {BasicPlane::Blue,  {1, 0}}, // C

        {BasicPlane::Green, {1, 1}}, // D
        {BasicPlane::Red,   {1, 1}}, // E
        {BasicPlane::Blue,  {1, 1}}, // F

        {BasicPlane::Green, {1, 2}}, // G
        {BasicPlane::Red,   {1, 2}}, // H
        {BasicPlane::Blue,  {1, 2}}, // I

        {BasicPlane::Green, {1, 3}}, // J
        {BasicPlane::Red,   {1, 3}}, // K
        {BasicPlane::Blue,  {1, 3}}, // L

        {BasicPlane::Green, {2, 0}}, // M
        {BasicPlane::Red,   {2, 0}}, // N
        {BasicPlane::Blue,  {2, 0}}, // O

        {BasicPlane::Green, {2, 1}}, // P
        {BasicPlane::Red,   {2, 1}}, // Q
        {BasicPlane::Blue,  {2, 1}}, // R

        {BasicPlane::Green, {2, 2}}, // S
        {BasicPlane::Red,   {2, 2}}, // T
        {BasicPlane::Blue,  {2, 2}}, // U

        {BasicPlane::Green, {2, 3}}, // V
        {BasicPlane::Red,   {2, 3}}, // W
        {BasicPlane::Blue,  {2, 3}}, // X

        {BasicPlane::Green, {3, 0}}, // Y
        {BasicPlane::Red,   {3, 0}}  // Z
    };

    // F1-F12 = WORDS functions in overlay order.
    constexpr BasicAction kGoldFKeyActions[12] = {
        {BasicPlane::Words, {1, 0}}, // FOR
        {BasicPlane::Words, {1, 1}}, // TO
        {BasicPlane::Words, {1, 2}}, // STEP
        {BasicPlane::Words, {1, 3}}, // NEXT

        {BasicPlane::Words, {2, 0}}, // GOSUB
        {BasicPlane::Words, {2, 1}}, // RETURN
        {BasicPlane::Words, {2, 2}}, // RND
        {BasicPlane::Words, {2, 3}}, // IF

        {BasicPlane::Words, {3, 0}}, // CLEAR
        {BasicPlane::Words, {3, 1}}, // LINE
        {BasicPlane::Words, {3, 2}}, // BOX
        {BasicPlane::Words, {3, 3}}  // GOTO
    };

    constexpr const char *kGoldFKeyNames[12] = {
        "FOR", "TO", "STEP", "NEXT",
        "GOSUB", "RETURN", "RND", "IF",
        "CLEAR", "LINE", "BOX", "GOTO"
    };

    QueueHandle_t gQueue = nullptr;
    SemaphoreHandle_t gStateMutex = nullptr;
    TaskHandle_t gActionTask = nullptr;
    TextKey *gTextPlan = nullptr;
    TransferStatus gTransfer;
    bool gActionActive = false;
    class StateGuard {
    public:
        StateGuard() { xSemaphoreTakeRecursive(gStateMutex, portMAX_DELAY); }
        ~StateGuard() { xSemaphoreGiveRecursive(gStateMutex); }
    };
    bool translate(uint8_t usage, uint8_t modifiers, BasicAction &action);
    bool exclusive() {
        return gTextPlan || gTransfer.state == TransferState::Maintenance;
    }
    void waitMs(uint32_t ms) {
        const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(ms) * 1000;
        int64_t remaining;
        while ((remaining = deadline - esp_timer_get_time()) > 0) {
            const uint32_t tickUs = 1000000 / configTICK_RATE_HZ;
            vTaskDelay(static_cast<TickType_t>((remaining + tickUs - 1) / tickUs));
        }
    }


    bool gHaltHeld[
        static_cast<std::size_t>(InputSource::Count)
    ] = {};

    MatrixPosition shiftPositionForPlane(BasicPlane plane)
    {
        switch (plane)
        {
            case BasicPlane::Green:
                return kGreenShift;

            case BasicPlane::Red:
                return kRedShift;

            case BasicPlane::Blue:
                return kBlueShift;

            case BasicPlane::Words:
                return kWordsShift;

            case BasicPlane::Default:
            default:
                return {0, 0};
        }
    }

    const char *planeName(BasicPlane plane)
    {
        switch (plane)
        {
            case BasicPlane::Green:
                return "GREEN";

            case BasicPlane::Red:
                return "RED";

            case BasicPlane::Blue:
                return "BLUE";

            case BasicPlane::Words:
                return "WORDS";

            case BasicPlane::Default:
            default:
                return "DEFAULT";
        }
    }

    uint8_t crosspointAddressFor(
        const MatrixPosition &position)
    {
        return static_cast<uint8_t>(
            (position.row << 3) | position.column
        );
    }

    int appendPositionDescription(
        char *buffer,
        std::size_t length,
        const char *label,
        const MatrixPosition &position)
    {
        if (length == 0)
        {
            return 0;
        }

        return std::snprintf(
            buffer,
            length,
            "%s r=%u c=%u addr=0x%02X D=1/0",
            label,
            static_cast<unsigned>(position.row),
            static_cast<unsigned>(position.column),
            static_cast<unsigned>(crosspointAddressFor(position))
        );
    }

    const char *translatedNameFor(
        uint8_t usage,
        uint8_t modifiers)
    {
        const bool ctrl =
            (modifiers & kCtrlMask) != 0;

        const bool shift =
            (modifiers & kShiftMask) != 0;

        static const char *const letters[] = {
            "A", "B", "C", "D", "E", "F", "G", "H",
            "I", "J", "K", "L", "M", "N", "O", "P",
            "Q", "R", "S", "T", "U", "V", "W", "X",
            "Y", "Z"
        };

        if (ctrl)
        {
            switch (usage)
            {
                case HID_KEY_F1:
                    return "GO+10";
                case HID_KEY_F2:
                    return "RUN";
                case HID_KEY_F3:
                    return "LIST";
                case HID_KEY_F4:
                    return "INPUT";
                case HID_KEY_F5:
                    return "PRINT";
                case HID_KEY_SLASH:
                    return "DIVIDE";
                case HID_KEY_8:
                    return shift ? "MULTIPLY" : nullptr;
                default:
                    return nullptr;
            }
        }

        if (usage >= HID_KEY_A &&
            usage <= HID_KEY_Z)
        {
            return letters[usage - HID_KEY_A];
        }

        if (usage >= HID_KEY_F1 &&
            usage <= HID_KEY_F12)
        {
            return kGoldFKeyNames[usage - HID_KEY_F1];
        }

        switch (usage)
        {
            case HID_KEY_ENTER:
                return shift ? "GO+10" : "GO";
            case HID_KEY_SPACE:
                return "SPACE";
            case HID_KEY_BACKSPACE:
            case HID_KEY_DELETE:
                return "ERASE";
            case HID_KEY_PAUSE:
                return "PAUSE";
            case HID_KEY_1:
                return shift ? "!" : "1";
            case HID_KEY_2:
                return shift ? "@" : "2";
            case HID_KEY_3:
                return shift ? "#" : "3";
            case HID_KEY_4:
                return shift ? "$" : "4";
            case HID_KEY_5:
                return shift ? "%" : "5";
            case HID_KEY_6:
                return shift ? "^" : "6";
            case HID_KEY_7:
                return shift ? "&" : "7";
            case HID_KEY_8:
                return shift ? "*" : "8";
            case HID_KEY_9:
                return shift ? "(" : "9";
            case HID_KEY_0:
                return shift ? ")" : "0";
            case HID_KEY_MINUS:
                return shift ? "_" : "MINUS";
            case HID_KEY_EQUAL:
                return shift ? "PLUS" : "EQUALS";
            case HID_KEY_LEFT_BRACKET:
                return shift ? "{" : "[";
            case HID_KEY_RIGHT_BRACKET:
                return shift ? "}" : "]";
            case HID_KEY_BACKSLASH:
                return shift ? "|" : "\\";
            case HID_KEY_SEMICOLON:
                return shift ? ":" : ";";
            case HID_KEY_APOSTROPHE:
                return shift ? "\"" : "'";
            case HID_KEY_COMMA:
                return shift ? "<" : ",";
            case HID_KEY_PERIOD:
                return shift ? ">" : ".";
            case HID_KEY_SLASH:
                return shift ? "?" : "/";
            case HID_KEY_ARROW_LEFT:
                return "LEFT";
            case HID_KEY_ARROW_RIGHT:
                return "RIGHT";
            case HID_KEY_ARROW_UP:
                return "UP";
            case HID_KEY_ARROW_DOWN:
                return "DOWN";
            case HID_KEY_KP_0:
                return "0";
            case HID_KEY_KP_1:
                return "1";
            case HID_KEY_KP_2:
                return "2";
            case HID_KEY_KP_3:
                return "3";
            case HID_KEY_KP_4:
                return "4";
            case HID_KEY_KP_5:
                return "5";
            case HID_KEY_KP_6:
                return "6";
            case HID_KEY_KP_7:
                return "7";
            case HID_KEY_KP_8:
                return "8";
            case HID_KEY_KP_9:
                return "9";
            case HID_KEY_KP_DECIMAL:
                return ".";
            case HID_KEY_KP_DIVIDE:
                return "DIVIDE";
            case HID_KEY_KP_MULTIPLY:
                return "MULTIPLY";
            case HID_KEY_KP_MINUS:
                return "MINUS";
            case HID_KEY_KP_PLUS:
                return "PLUS";
            case HID_KEY_KP_ENTER:
                return "GO";
            case HID_KEY_KP_EQUAL:
                return "EQUALS";
            default:
                return nullptr;
        }
    }

    void describeAction(
        const BasicAction &action,
        char *buffer,
        std::size_t length,
        const char *translatedName = nullptr)
    {
        if (buffer == nullptr || length == 0)
        {
            return;
        }

        buffer[0] = '\0';

        if (action.plane == BasicPlane::Default)
        {
            (void)std::snprintf(
                buffer,
                length,
                translatedName != nullptr
                    ? "basic=%s plane=%s xpt=["
                    : "basic=%s xpt=[",
                translatedName != nullptr
                    ? translatedName
                    : planeName(action.plane),
                planeName(action.plane)
            );

            const std::size_t used =
                std::strlen(buffer);

            if (used < length)
            {
                (void)appendPositionDescription(
                    buffer + used,
                    length - used,
                    "target",
                    action.target
                );
            }

            const std::size_t after =
                std::strlen(buffer);

            if (after < length)
            {
                (void)std::snprintf(
                    buffer + after,
                    length - after,
                    "]"
                );
            }

            return;
        }

        const MatrixPosition shift =
            shiftPositionForPlane(action.plane);

        (void)std::snprintf(
            buffer,
            length,
            translatedName != nullptr
                ? "basic=%s plane=%s xpt=["
                : "basic=%s xpt=[",
            translatedName != nullptr
                ? translatedName
                : planeName(action.plane),
            planeName(action.plane)
        );

        std::size_t used = std::strlen(buffer);

        if (used < length)
        {
            (void)appendPositionDescription(
                buffer + used,
                length - used,
                "shift",
                shift
            );
        }

        used = std::strlen(buffer);

        if (used < length)
        {
            (void)std::snprintf(
                buffer + used,
                length - used,
                ", "
            );
        }

        used = std::strlen(buffer);

        if (used < length)
        {
            (void)appendPositionDescription(
                buffer + used,
                length - used,
                "target",
                action.target
            );
        }

        used = std::strlen(buffer);

        if (used < length)
        {
            (void)std::snprintf(
                buffer + used,
                length - used,
                "]"
            );
        }
    }

    void tapMatrixPosition(
        const MatrixPosition &position)
    {
        if (!crosspointPress(position))
        {
            return;
        }

        waitMs(AppConfig::kKeypressDownMs);

        crosspointRelease(position);

        waitMs(AppConfig::kKeypressGapMs);
    }

    void actionTask(void *)
    {
        for (;;) {
            BasicAction action{};
            bool haveAction = false, textAction = false, lineEnd = false;
            TextKey *finished = nullptr;
            {
                StateGuard guard;
                if (gTextPlan && gTransfer.state == TransferState::Cancelling) {
                    finished = gTextPlan;
                    gTextPlan = nullptr;
                    gTransfer.state = TransferState::Cancelled;
                } else if (gTextPlan && gTransfer.state == TransferState::Running) {
                    const TextKey key = gTextPlan[gTransfer.sent];
                    haveAction = translate(key.usage, key.modifiers, action);
                    textAction = true;
                    lineEnd = key.usage == HID_KEY_ENTER;
                } else if (!exclusive()) {
                    haveAction = xQueueReceive(gQueue, &action, 0) == pdTRUE;
                }
                gActionActive = haveAction;
            }
            if (finished) heap_caps_free(finished);
            if (!haveAction) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
                continue;
            }
            if (action.plane != BasicPlane::Default) {
                tapMatrixPosition(shiftPositionForPlane(action.plane));
            }
            tapMatrixPosition(action.target);
            if (textAction && lineEnd && ASTROCADE_TEXT_LINE_GAP_MS) {
                waitMs(ASTROCADE_TEXT_LINE_GAP_MS);
            }
            {
                StateGuard guard;
                gActionActive = false;
                if (textAction) {
                    gTransfer.sent++;
                    if (lineEnd) gTransfer.linesSent++;
                    if (gTransfer.sent == gTransfer.total) {
                        finished = gTextPlan;
                        gTextPlan = nullptr;
                        gTransfer.state = TransferState::Complete;
                    }
                }
            }
            if (textAction && finished) heap_caps_free(finished);
        }
    }

    bool queueAction(const BasicAction &action)
    {
        StateGuard guard;
        if (gQueue == nullptr || exclusive())
        {
            return false;
        }

        const bool queued = xQueueSend(gQueue, &action, 0) == pdTRUE;
        if (queued) xTaskNotifyGive(gActionTask);
        return queued;
    }

    bool translate(
        uint8_t usage,
        uint8_t modifiers,
        BasicAction &action)
    {
        const bool ctrl =
            (modifiers & kCtrlMask) != 0;

        const bool shift =
            (modifiers & kShiftMask) != 0;

        if (ctrl)
        {
            switch (usage)
            {
                case HID_KEY_F1:
                    action = {BasicPlane::Words, {0, 0}}; // GO+10
                    return true;

                case HID_KEY_F2:
                    action = {BasicPlane::Words, {0, 2}}; // RUN
                    return true;

                case HID_KEY_F3:
                    action = {BasicPlane::Words, {0, 3}}; // LIST
                    return true;

                case HID_KEY_F4:
                    action = {BasicPlane::Words, {4, 1}}; // INPUT
                    return true;

                case HID_KEY_F5:
                    action = {BasicPlane::Words, {4, 3}}; // PRINT
                    return true;

                case HID_KEY_SLASH:
                    action = {BasicPlane::Default, kDivide};
                    return true;

                case HID_KEY_8:
                    if (shift)
                    {
                        action = {
                            BasicPlane::Default,
                            kMultiply
                        };
                        return true;
                    }
                    break;

                default:
                    break;
            }

            return false;
        }

        if (usage >= HID_KEY_A &&
            usage <= HID_KEY_Z)
        {
            action =
                kLetterActions[usage - HID_KEY_A];

            return true;
        }

        if (usage >= HID_KEY_F1 &&
            usage <= HID_KEY_F12)
        {
            action =
                kGoldFKeyActions[usage - HID_KEY_F1];

            return true;
        }

        switch (usage)
        {
            case HID_KEY_ENTER:
                action = shift
                    ? BasicAction{BasicPlane::Words, {0, 0}}
                    : BasicAction{BasicPlane::Default, kGo};
                return true;

            case HID_KEY_SPACE:
                action = {BasicPlane::Default, kSpace};
                return true;

            case HID_KEY_BACKSPACE:
            case HID_KEY_DELETE:
                action = {BasicPlane::Default, kErase};
                return true;

            case HID_KEY_PAUSE:
                action = {BasicPlane::Default, kPause};
                return true;

            case HID_KEY_1:
                action = shift
                    ? BasicAction{BasicPlane::Blue, {3, 0}}
                    : BasicAction{BasicPlane::Default, kOne};
                return true;

            case HID_KEY_2:
                action = shift
                    ? BasicAction{BasicPlane::Red, {3, 3}}
                    : BasicAction{BasicPlane::Default, kTwo};
                return true;

            case HID_KEY_3:
                action = shift
                    ? BasicAction{BasicPlane::Green, {4, 3}}
                    : BasicAction{BasicPlane::Default, kThree};
                return true;

            case HID_KEY_4:
                action = shift
                    ? BasicAction{BasicPlane::Green, {4, 0}}
                    : BasicAction{BasicPlane::Default, kFour};
                return true;

            case HID_KEY_5:
                action = shift
                    ? BasicAction{BasicPlane::Red, {4, 3}}
                    : BasicAction{BasicPlane::Default, kFive};
                return true;

            case HID_KEY_6:
                if (shift)
                {
                    return false; // ^ is not on this overlay.
                }

                action = {BasicPlane::Default, kSix};
                return true;

            case HID_KEY_7:
                action = shift
                    ? BasicAction{BasicPlane::Green, {3, 3}}
                    : BasicAction{BasicPlane::Default, kSeven};
                return true;

            case HID_KEY_8:
                action = shift
                    ? BasicAction{BasicPlane::Blue, {3, 3}}
                    : BasicAction{BasicPlane::Default, kEight};
                return true;

            case HID_KEY_9:
                action = shift
                    ? BasicAction{BasicPlane::Green, {4, 2}}
                    : BasicAction{BasicPlane::Default, kNine};
                return true;

            case HID_KEY_0:
                action = shift
                    ? BasicAction{BasicPlane::Blue, {4, 2}}
                    : BasicAction{BasicPlane::Default, kZero};
                return true;

            case HID_KEY_MINUS:
                if (shift)
                {
                    return false; // _ is not on this overlay.
                }

                action = {BasicPlane::Default, kMinus};
                return true;

            case HID_KEY_EQUAL:
                action = shift
                    ? BasicAction{BasicPlane::Default, kPlus}
                    : BasicAction{BasicPlane::Default, kEquals};
                return true;

            case HID_KEY_LEFT_BRACKET:
                if (shift)
                {
                    return false;
                }

                action = {BasicPlane::Red, {0, 3}};
                return true;

            case HID_KEY_RIGHT_BRACKET:
                if (shift)
                {
                    return false;
                }

                action = {BasicPlane::Blue, {0, 3}};
                return true;

            case HID_KEY_BACKSLASH:
                if (shift)
                {
                    return false;
                }

                action = {BasicPlane::Blue, {0, 1}};
                return true;

            case HID_KEY_SEMICOLON:
                action = shift
                    ? BasicAction{BasicPlane::Blue, {4, 3}}
                    : BasicAction{BasicPlane::Red, {4, 2}};
                return true;

            case HID_KEY_APOSTROPHE:
                action = shift
                    ? BasicAction{BasicPlane::Red, {4, 1}}
                    : BasicAction{BasicPlane::Red, {3, 1}};
                return true;

            case HID_KEY_COMMA:
                action = shift
                    ? BasicAction{BasicPlane::Green, {4, 1}}
                    : BasicAction{BasicPlane::Red, {4, 0}};
                return true;

            case HID_KEY_PERIOD:
                action = shift
                    ? BasicAction{BasicPlane::Blue, {4, 1}}
                    : BasicAction{BasicPlane::Red, {3, 2}};
                return true;

            case HID_KEY_SLASH:
                action = shift
                    ? BasicAction{BasicPlane::Blue, {4, 0}}
                    : BasicAction{BasicPlane::Red, {0, 1}};
                return true;

            case HID_KEY_ARROW_LEFT:
                action = {BasicPlane::Green, {3, 1}};
                return true;

            case HID_KEY_ARROW_RIGHT:
                action = {BasicPlane::Blue, {3, 1}};
                return true;

            case HID_KEY_ARROW_UP:
                action = {BasicPlane::Green, {3, 2}};
                return true;

            case HID_KEY_ARROW_DOWN:
                action = {BasicPlane::Blue, {3, 2}};
                return true;

            case HID_KEY_KP_0:
                action = {BasicPlane::Default, kZero};
                return true;

            case HID_KEY_KP_1:
                action = {BasicPlane::Default, kOne};
                return true;

            case HID_KEY_KP_2:
                action = {BasicPlane::Default, kTwo};
                return true;

            case HID_KEY_KP_3:
                action = {BasicPlane::Default, kThree};
                return true;

            case HID_KEY_KP_4:
                action = {BasicPlane::Default, kFour};
                return true;

            case HID_KEY_KP_5:
                action = {BasicPlane::Default, kFive};
                return true;

            case HID_KEY_KP_6:
                action = {BasicPlane::Default, kSix};
                return true;

            case HID_KEY_KP_7:
                action = {BasicPlane::Default, kSeven};
                return true;

            case HID_KEY_KP_8:
                action = {BasicPlane::Default, kEight};
                return true;

            case HID_KEY_KP_9:
                action = {BasicPlane::Default, kNine};
                return true;

            case HID_KEY_KP_DECIMAL:
                action = {BasicPlane::Red, {3, 2}};
                return true;

            case HID_KEY_KP_DIVIDE:
                action = {BasicPlane::Default, kDivide};
                return true;

            case HID_KEY_KP_MULTIPLY:
                action = {BasicPlane::Default, kMultiply};
                return true;

            case HID_KEY_KP_MINUS:
                action = {BasicPlane::Default, kMinus};
                return true;

            case HID_KEY_KP_PLUS:
                action = {BasicPlane::Default, kPlus};
                return true;

            case HID_KEY_KP_ENTER:
                action = {BasicPlane::Default, kGo};
                return true;

            case HID_KEY_KP_EQUAL:
                action = {BasicPlane::Default, kEquals};
                return true;

            default:
                return false;
        }
    }
}

bool overlayBasicInit()
{
    gStateMutex = xSemaphoreCreateRecursiveMutex();
    if (!gStateMutex) return false;
    gQueue =
        xQueueCreate(
            64,
            sizeof(BasicAction)
        );

    if (gQueue == nullptr)
    {
        return false;
    }

    return xTaskCreatePinnedToCore(
        actionTask, "basic_keys", 4096, nullptr, 8, &gActionTask, 1
    ) == pdPASS;
}

bool overlayBasicDescribeKeyDown(
    uint8_t usage,
    uint8_t modifiers,
    char *buffer,
    std::size_t length)
{
    if (buffer == nullptr || length == 0)
    {
        return false;
    }

    buffer[0] = '\0';

    if (usage == HID_KEY_ESCAPE)
    {
        (void)std::snprintf(
            buffer,
            length,
            "basic=HALT xpt=[target r=%u c=%u addr=0x%02X D=1 hold]",
            static_cast<unsigned>(kHalt.row),
            static_cast<unsigned>(kHalt.column),
            static_cast<unsigned>(crosspointAddressFor(kHalt))
        );

        return true;
    }

#if ASTROCADE_BACKSPACE_LEFT_ERASE_ENABLED
    if (usage == HID_KEY_BACKSPACE)
    {
        char left[96] = {};
        char erase[80] = {};

        describeAction(
            {BasicPlane::Green, {3, 1}},
            left,
            sizeof(left)
        );

        describeAction(
            {BasicPlane::Default, kErase},
            erase,
            sizeof(erase)
        );

        (void)std::snprintf(
            buffer,
            length,
            "basic=BACKSPACE_MACRO %s then %s",
            left,
            erase
        );

        return true;
    }
#endif

    BasicAction action = {};

    if (!translate(usage, modifiers, action))
    {
        (void)std::snprintf(
            buffer,
            length,
            "basic=unmapped"
        );

        return false;
    }

    describeAction(
        action,
        buffer,
        length,
        translatedNameFor(usage, modifiers)
    );
    return true;
}

void overlayBasicKeyDown(
    InputSource source,
    uint8_t usage,
    uint8_t modifiers)
{
    const std::size_t sourceIndex =
        static_cast<std::size_t>(source);

    StateGuard guard;
    if (exclusive()) {
        if (usage == HID_KEY_ESCAPE && gTextPlan) {
            gTransfer.state = TransferState::Cancelling;
            xTaskNotifyGive(gActionTask);
        }
        return;
    }

    // Escape = HALT and is intentionally held until key-up.
    if (usage == HID_KEY_ESCAPE)
    {
        if (sourceIndex <
                static_cast<std::size_t>(InputSource::Count) &&
            !gHaltHeld[sourceIndex])
        {
            if (crosspointPress(kHalt))
            {
                gHaltHeld[sourceIndex] = true;
            }
        }

        return;
    }

    BasicAction action = {};

#if ASTROCADE_BACKSPACE_LEFT_ERASE_ENABLED
    if (usage == HID_KEY_BACKSPACE)
    {
        (void)queueAction({BasicPlane::Green, {3, 1}});
        (void)queueAction({BasicPlane::Default, kErase});
        return;
    }
#endif

    if (translate(usage, modifiers, action))
    {
        (void)queueAction(action);
    }
}

void overlayBasicKeyUp(
    InputSource source,
    uint8_t usage,
    uint8_t)
{
    const std::size_t sourceIndex =
        static_cast<std::size_t>(source);

    StateGuard guard;
    if (usage == HID_KEY_ESCAPE &&
        sourceIndex <
            static_cast<std::size_t>(InputSource::Count) &&
        gHaltHeld[sourceIndex])
    {
        crosspointRelease(kHalt);
        gHaltHeld[sourceIndex] = false;
    }
}

void overlayBasicSourceDisconnected(InputSource source)
{
    const std::size_t sourceIndex =
        static_cast<std::size_t>(source);

    StateGuard guard;
    if (sourceIndex <
            static_cast<std::size_t>(InputSource::Count) &&
        gHaltHeld[sourceIndex])
    {
        crosspointRelease(kHalt);
        gHaltHeld[sourceIndex] = false;
    }

    // A disconnect should not allow already-buffered characters from the lost
    // keyboard to continue typing into the Astrocade.
    if (gQueue != nullptr)
    {
        xQueueReset(gQueue);
    }
}

void overlayBasicEmergencyRelease()
{
    if (gQueue != nullptr)
    {
        xQueueReset(gQueue);
    }

    std::memset(gHaltHeld, 0, sizeof(gHaltHeld));
    crosspointReleaseAll();
}
bool overlayBasicMapText(uint32_t character, TextKey &key)
{
    if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    if (character >= 'A' && character <= 'Z') key = {static_cast<uint8_t>(4 + character - 'A'), 0};
    else if (character >= '1' && character <= '9') key = {static_cast<uint8_t>(0x1E + character - '1'), 0};
    else {
        switch (character) {
        case '0': key = {0x27, 0}; break;
        case '\n': key = {0x28, 0}; break;
        case ' ': key = {0x2C, 0}; break;
        case '-': key = {0x2D, 0}; break;
        case '+': key = {0x2E, 0x02}; break;
        case '=': key = {0x2E, 0}; break;
        case '[': key = {0x2F, 0}; break;
        case ']': key = {0x30, 0}; break;
        case '\\': key = {0x31, 0}; break;
        case ';': key = {0x33, 0}; break;
        case ':': key = {0x33, 0x02}; break;
        case '\'': key = {0x34, 0}; break;
        case '"': key = {0x34, 0x02}; break;
        case ',': key = {0x36, 0}; break;
        case '<': key = {0x36, 0x02}; break;
        case '.': key = {0x37, 0}; break;
        case '>': key = {0x37, 0x02}; break;
        case '/': key = {0x38, 0}; break;
        case '?': key = {0x38, 0x02}; break;
        default: {
            const char *symbols = "!@#$%^&*()";
            const char *found = character < 128 ? std::strchr(symbols, static_cast<char>(character)) : nullptr;
            if (!found || !character) return false;
            key = {static_cast<uint8_t>(0x1E + (found - symbols)), 0x02};
        }
        }
    }
    BasicAction action{};
    return translate(key.usage, key.modifiers, action);
}

const char *transferStateName(TransferState state)
{
    switch (state) {
    case TransferState::Running: return "running";
    case TransferState::Paused: return "paused";
    case TransferState::Cancelling: return "cancelling";
    case TransferState::Cancelled: return "cancelled";
    case TransferState::Complete: return "complete";
    case TransferState::Maintenance: return "maintenance";
    default: return "idle";
    }
}

TransferStatus textTransferStatus()
{
    StateGuard guard;
    return gTransfer;
}

namespace {
    bool workerIdle() {
        if (exclusive() || gActionActive || uxQueueMessagesWaiting(gQueue)) return false;
        for (bool held : gHaltHeld) if (held) return false;
        return true;
    }
}

bool textTransferStart(TextKey *plan, std::size_t length, uint32_t lines)
{
    if (!plan || !length) return false;
    StateGuard guard;
    if (!workerIdle()) return false;
    gTransfer = {TransferState::Running, gTransfer.id + 1, 0, length, 0, lines};
    gTextPlan = plan;
    xTaskNotifyGive(gActionTask);
    return true;
}

bool textTransferControl(const char *command)
{
    StateGuard guard;
    if (!gTextPlan) return false;
    if (!std::strcmp(command, "cancel")) gTransfer.state = TransferState::Cancelling;
    else if (!std::strcmp(command, "pause") && gTransfer.state == TransferState::Running)
        gTransfer.state = TransferState::Paused;
    else if (!std::strcmp(command, "resume") && gTransfer.state == TransferState::Paused)
        gTransfer.state = TransferState::Running;
    else return false;
    xTaskNotifyGive(gActionTask);
    return true;
}

bool textTransferBeginMaintenance()
{
    StateGuard guard;
    if (!workerIdle()) return false;
    gTransfer.state = TransferState::Maintenance;
    return true;
}
void textTransferEndMaintenance()
{
    StateGuard guard;
    if (gTransfer.state == TransferState::Maintenance) gTransfer.state = TransferState::Idle;
}
