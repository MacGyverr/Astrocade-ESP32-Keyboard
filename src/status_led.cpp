#include "status_led.h"

#include "app_config.h"

#if ASTROCADE_STATUS_LED_ENABLED

#include <cstdint>

#include "app_log.h"

#include "driver/rmt_tx.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace
{
    constexpr uint32_t kRmtResolutionHz = 10000000U;

    constexpr uint32_t kAnimationFrameMs = 30;
    constexpr uint32_t kSlowBluePeriodMs = 1400;
    constexpr uint32_t kFastBluePeriodMs = 260;
    constexpr uint32_t kFailurePulseMs = 120;

    constexpr uint8_t kPulseMin = 1;
    constexpr uint8_t kBlueMax = 24;

    struct Color
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    enum class Event : uint8_t
    {
        StartupSlow,
        StartupFast,
        StartupFailure,
        Off,
        UsbDevice,
        UsbKeyboardReady,
        UsbFailure,
        UsbKeypress,
        BleInput,
        CrosspointStrobe
    };

    enum class BaseMode : uint8_t
    {
        Off,
        SlowBlue,
        FastBlue,
        FailureRed
    };

    constexpr Color kOff    = {0, 0, 0};
    constexpr Color kOrange = {24, 8, 0};
    constexpr Color kGreen  = {0, 24, 0};
    constexpr Color kPurple = {18, 0, 24};
    constexpr Color kRed    = {24, 0, 0};

    QueueHandle_t gQueue = nullptr;
    rmt_channel_handle_t gChannel = nullptr;
    rmt_encoder_handle_t gEncoder = nullptr;
    bool gReady = false;

    BaseMode gBaseMode = BaseMode::Off;
    TickType_t gBaseStart = 0;

    TickType_t gTransientUntil = 0;

    uint8_t gFailureTogglesRemaining = 0;
    bool gFailureOn = false;
    TickType_t gFailureNext = 0;

    rmt_symbol_word_t makeSymbol(
        uint8_t level0,
        uint16_t duration0,
        uint8_t level1,
        uint16_t duration1)
    {
        rmt_symbol_word_t symbol = {};
        symbol.level0 = level0;
        symbol.duration0 = duration0;
        symbol.level1 = level1;
        symbol.duration1 = duration1;
        return symbol;
    }

    size_t ws2812EncoderCallback(
        const void *data,
        size_t dataSize,
        size_t symbolsWritten,
        size_t symbolsFree,
        rmt_symbol_word_t *symbols,
        bool *done,
        void *)
    {
        if (symbolsFree < 8)
        {
            return 0;
        }

        const auto *bytes =
            static_cast<const uint8_t *>(data);

        const size_t dataPosition =
            symbolsWritten / 8;

        if (dataPosition < dataSize)
        {
            size_t symbolPosition = 0;
            const uint8_t value = bytes[dataPosition];

            for (uint8_t mask = 0x80; mask != 0; mask >>= 1)
            {
                symbols[symbolPosition++] =
                    (value & mask) != 0
                        ? makeSymbol(1, 9, 0, 3)
                        : makeSymbol(1, 3, 0, 9);
            }

            return symbolPosition;
        }

        symbols[0] = makeSymbol(0, 250, 0, 250);
        *done = true;
        return 1;
    }

    bool transmitColor(Color color)
    {
        if (!gReady ||
            gChannel == nullptr ||
            gEncoder == nullptr)
        {
            return false;
        }

        uint8_t pixels[3] = {
            color.g,
            color.r,
            color.b
        };

        rmt_transmit_config_t transmitConfig = {};
        transmitConfig.loop_count = 0;
        transmitConfig.flags.eot_level = 0;
        transmitConfig.flags.queue_nonblocking = 1;

        (void)rmt_encoder_reset(gEncoder);

        esp_err_t result =
            rmt_transmit(
                gChannel,
                gEncoder,
                pixels,
                sizeof(pixels),
                &transmitConfig
            );

        if (result != ESP_OK)
        {
            return false;
        }

        result = rmt_tx_wait_all_done(gChannel, 20);

        return result == ESP_OK;
    }

    uint8_t pulseBrightness(
        TickType_t now,
        uint32_t periodMs)
    {
        const uint32_t elapsedMs =
            static_cast<uint32_t>(
                (now - gBaseStart) * portTICK_PERIOD_MS
            );

        const uint32_t phase =
            elapsedMs % periodMs;

        const uint32_t half =
            periodMs / 2U;

        const uint32_t rising =
            phase < half ? phase : periodMs - phase;

        return static_cast<uint8_t>(
            kPulseMin +
            ((rising * (kBlueMax - kPulseMin)) / half)
        );
    }

    void setBaseMode(BaseMode mode)
    {
        gBaseMode = mode;
        gBaseStart = xTaskGetTickCount();
        gTransientUntil = 0;
    }

    void setTransient(
        Color color,
        uint32_t milliseconds)
    {
        gTransientUntil =
            xTaskGetTickCount() +
            pdMS_TO_TICKS(milliseconds);

        (void)transmitColor(color);
    }

    void beginFailurePulse()
    {
        setBaseMode(BaseMode::FailureRed);
        gFailureTogglesRemaining = 8;
        gFailureOn = false;
        gFailureNext = xTaskGetTickCount();
    }

    void applyBaseAnimation()
    {
        const TickType_t now =
            xTaskGetTickCount();

        if (gTransientUntil != 0 &&
            static_cast<int32_t>(now - gTransientUntil) < 0)
        {
            return;
        }

        gTransientUntil = 0;

        if (gBaseMode == BaseMode::FailureRed)
        {
            if (gFailureTogglesRemaining == 0)
            {
                setBaseMode(BaseMode::Off);
                (void)transmitColor(kOff);
                return;
            }

            if (static_cast<int32_t>(now - gFailureNext) >= 0)
            {
                gFailureOn = !gFailureOn;
                --gFailureTogglesRemaining;
                gFailureNext =
                    now + pdMS_TO_TICKS(kFailurePulseMs);

                (void)transmitColor(
                    gFailureOn ? kRed : kOff
                );
            }

            return;
        }

        switch (gBaseMode)
        {
            case BaseMode::SlowBlue:
                (void)transmitColor({
                    0,
                    0,
                    pulseBrightness(
                        now,
                        kSlowBluePeriodMs
                    )
                });
                break;

            case BaseMode::FastBlue:
                (void)transmitColor({
                    0,
                    0,
                    pulseBrightness(
                        now,
                        kFastBluePeriodMs
                    )
                });
                break;

            case BaseMode::Off:
            default:
                (void)transmitColor(kOff);
                break;
        }
    }

    void handleEvent(Event event)
    {
        switch (event)
        {
            case Event::StartupSlow:
                setBaseMode(BaseMode::SlowBlue);
                break;

            case Event::StartupFast:
                setBaseMode(BaseMode::FastBlue);
                break;

            case Event::StartupFailure:
                beginFailurePulse();
                break;

            case Event::Off:
                setBaseMode(BaseMode::Off);
                (void)transmitColor(kOff);
                break;

            case Event::UsbDevice:
                setTransient(kOrange, 140);
                break;

            case Event::UsbKeyboardReady:
                setTransient(kOrange, 320);
                break;

            case Event::UsbFailure:
                beginFailurePulse();
                break;

            case Event::UsbKeypress:
                setTransient(kOrange, 60);
                break;

            case Event::BleInput:
                setTransient(kPurple, 60);
                break;

            case Event::CrosspointStrobe:
                setTransient(kGreen, 25);
                break;
        }
    }

    void ledTask(void *)
    {
        (void)transmitColor(kOff);

        for (;;)
        {
            Event event = Event::Off;

            if (xQueueReceive(
                    gQueue,
                    &event,
                    pdMS_TO_TICKS(kAnimationFrameMs)) == pdTRUE)
            {
                handleEvent(event);
            }

            applyBaseAnimation();
        }
    }

    bool initRmt()
    {
        rmt_tx_channel_config_t channelConfig = {};
        channelConfig.gpio_num =
            static_cast<gpio_num_t>(
                AppConfig::kStatusLedGpio
            );
        channelConfig.clk_src = RMT_CLK_SRC_DEFAULT;
        channelConfig.resolution_hz = kRmtResolutionHz;
        channelConfig.mem_block_symbols = 64;
        channelConfig.trans_queue_depth = 4;
        channelConfig.flags.init_level = 0;

        esp_err_t result =
            rmt_new_tx_channel(
                &channelConfig,
                &gChannel
            );

        if (result != ESP_OK)
        {
            APP_LOG(
                "rgb: RMT channel init failed: %s",
                esp_err_to_name(result)
            );
            return false;
        }

        rmt_simple_encoder_config_t encoderConfig = {};
        encoderConfig.callback = ws2812EncoderCallback;

        result =
            rmt_new_simple_encoder(
                &encoderConfig,
                &gEncoder
            );

        if (result != ESP_OK)
        {
            APP_LOG(
                "rgb: encoder init failed: %s",
                esp_err_to_name(result)
            );
            return false;
        }

        result = rmt_enable(gChannel);

        if (result != ESP_OK)
        {
            APP_LOG(
                "rgb: RMT enable failed: %s",
                esp_err_to_name(result)
            );
            return false;
        }

        gReady = true;
        return true;
    }

    void enqueue(Event event)
    {
        if (gQueue == nullptr)
        {
            return;
        }

        (void)xQueueSend(gQueue, &event, 0);
    }
}

bool statusLedInit()
{
    gQueue = xQueueCreate(16, sizeof(Event));

    if (gQueue == nullptr)
    {
        APP_LOG("rgb: event queue allocation failed");
        return false;
    }

    if (!initRmt())
    {
        return false;
    }

    if (xTaskCreate(
            ledTask,
            "status_led",
            3072,
            nullptr,
            2,
            nullptr) != pdPASS)
    {
        APP_LOG("rgb: task creation failed");
        return false;
    }

    APP_LOG(
        "rgb: enabled on GPIO%lu",
        static_cast<unsigned long>(
            AppConfig::kStatusLedGpio
        )
    );

    return true;
}

void statusLedSetBleStartupSlow()
{
    enqueue(Event::StartupSlow);
}

void statusLedSetBleStartupFast()
{
    enqueue(Event::StartupFast);
}

void statusLedShowBleStartupFailure()
{
    enqueue(Event::StartupFailure);
}

void statusLedOff()
{
    enqueue(Event::Off);
}

void statusLedSignalUsbDevice()
{
    enqueue(Event::UsbDevice);
}

void statusLedSignalUsbKeyboardReady()
{
    enqueue(Event::UsbKeyboardReady);
}

void statusLedSignalUsbFailure()
{
    enqueue(Event::UsbFailure);
}

void statusLedSignalUsbKeypress()
{
    enqueue(Event::UsbKeypress);
}

void statusLedSignalBleInput()
{
    enqueue(Event::BleInput);
}

void statusLedSignalCrosspointStrobe()
{
    enqueue(Event::CrosspointStrobe);
}

#else

bool statusLedInit()
{
    return true;
}

void statusLedSetBleStartupSlow() {}
void statusLedSetBleStartupFast() {}
void statusLedShowBleStartupFailure() {}
void statusLedOff() {}
void statusLedSignalUsbDevice() {}
void statusLedSignalUsbKeyboardReady() {}
void statusLedSignalUsbFailure() {}
void statusLedSignalUsbKeypress() {}
void statusLedSignalBleInput() {}
void statusLedSignalCrosspointStrobe() {}

#endif
