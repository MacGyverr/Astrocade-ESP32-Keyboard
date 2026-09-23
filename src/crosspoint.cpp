#include "crosspoint.h"

#include <cstring>
#include <atomic>

#include "app_config.h"
#include "app_log.h"
#include "status_led.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// Profiles supply S3 or classic ESP32 GPIOs; both use the same crosspoint bit order.

static constexpr gpio_num_t kAddressPins[6] = {
    static_cast<gpio_num_t>(AppConfig::kCrosspointPins[0]), // A0
    static_cast<gpio_num_t>(AppConfig::kCrosspointPins[1]), // A1
    static_cast<gpio_num_t>(AppConfig::kCrosspointPins[2]), // A2
    static_cast<gpio_num_t>(AppConfig::kCrosspointPins[3]), // A3
    static_cast<gpio_num_t>(AppConfig::kCrosspointPins[4]), // A4
    static_cast<gpio_num_t>(AppConfig::kCrosspointPins[5])  // A5
};

static constexpr gpio_num_t kDataPin = static_cast<gpio_num_t>(AppConfig::kCrosspointPins[6]);
static constexpr gpio_num_t kStrobePin = static_cast<gpio_num_t>(AppConfig::kCrosspointPins[7]);

static SemaphoreHandle_t gMutex = nullptr;
static bool gDebugWritesEnabled = false;
#if ASTROCADE_CROSSPOINT_DEBUG_ENABLED && ASTROCADE_SERIAL_DEBUG_ENABLED
struct DebugWrite { uint8_t row, column, address, data; };
static QueueHandle_t gDebugQueue = nullptr;
static std::atomic<unsigned> gDroppedLogs{0};
static void debugTask(void *)
{
    DebugWrite event{};
    for (;;) {
        if (xQueueReceive(gDebugQueue, &event, portMAX_DELAY) != pdTRUE) continue;
        APP_LOG("crosspoint: row=%u col=%u addr=0x%02X data=%u strobe=LOW",
                unsigned(event.row), unsigned(event.column),
                unsigned(event.address), unsigned(event.data));
        const auto dropped = gDroppedLogs.exchange(0);
        if (dropped) APP_LOG("crosspoint: skipped %u debug records; output timing takes priority", dropped);
    }
}
#endif

// Reference counting prevents one logical input source from releasing a
// matrix position that another source is still holding.
static uint8_t gHoldCount[6][4] = {};

static void writeCrosspointLocked(
    uint8_t row,
    uint8_t column,
    bool closed)
{
    // Y = Astrocade row
    // X = Astrocade column
    //
    // A5..A3 = Y
    // A2..A0 = X
    const uint8_t address =
        static_cast<uint8_t>((row << 3) | column);

    for (uint8_t bit = 0; bit < 6; ++bit)
    {
        gpio_set_level(
            kAddressPins[bit],
            (address >> bit) & 0x01
        );
    }

    gpio_set_level(kDataPin, closed ? 1 : 0);

    esp_rom_delay_us(AppConfig::kCrosspointSetupDelayUs);

    // STROBE is active-low.
    gpio_set_level(kStrobePin, 0);
    esp_rom_delay_us(AppConfig::kCrosspointStrobeLowUs);
    gpio_set_level(kStrobePin, 1);
    esp_rom_delay_us(AppConfig::kCrosspointRecoveryDelayUs);

#if ASTROCADE_CROSSPOINT_DEBUG_ENABLED && ASTROCADE_SERIAL_DEBUG_ENABLED
    if (gDebugWritesEnabled && gDebugQueue)
    {
        // Never wait for UART output in the timed keypress path.
        const DebugWrite event{row, column, address, static_cast<uint8_t>(closed)};
        if (xQueueSend(gDebugQueue, &event, 0) != pdTRUE) ++gDroppedLogs;
    }
#endif

#if ASTROCADE_CROSSPOINT_STROBE_LED_ENABLED
    statusLedSignalCrosspointStrobe();
#endif
}

bool crosspointPress(const MatrixPosition &position)
{
    if (position.row >= 6 ||
        position.column >= 4 ||
        gMutex == nullptr)
    {
        return false;
    }

    xSemaphoreTake(gMutex, portMAX_DELAY);

    uint8_t &count =
        gHoldCount[position.row][position.column];

    if (count == 0)
    {
        writeCrosspointLocked(
            position.row,
            position.column,
            true
        );
    }

    if (count != 0xFF)
    {
        ++count;
    }

    xSemaphoreGive(gMutex);

    return true;
}

void crosspointRelease(const MatrixPosition &position)
{
    if (position.row >= 6 ||
        position.column >= 4 ||
        gMutex == nullptr)
    {
        return;
    }

    xSemaphoreTake(gMutex, portMAX_DELAY);

    uint8_t &count =
        gHoldCount[position.row][position.column];

    if (count != 0)
    {
        --count;

        if (count == 0)
        {
            writeCrosspointLocked(
                position.row,
                position.column,
                false
            );
        }
    }

    xSemaphoreGive(gMutex);
}

void crosspointReleaseAll()
{
    if (gMutex == nullptr)
    {
        return;
    }

    xSemaphoreTake(gMutex, portMAX_DELAY);

    std::memset(gHoldCount, 0, sizeof(gHoldCount));

    // Clear every one of the 64 internal latches, not only the 24 positions
    // used by the Astrocade.
    for (uint8_t row = 0; row < 8; ++row)
    {
        for (uint8_t column = 0; column < 8; ++column)
        {
            writeCrosspointLocked(row, column, false);
        }
    }

    xSemaphoreGive(gMutex);
}

bool crosspointInit()
{
    APP_NOTICE("crosspoint: A0..A5=%d,%d,%d,%d,%d,%d DATA=%d STROBE=%d",
               AppConfig::kCrosspointPins[0], AppConfig::kCrosspointPins[1],
               AppConfig::kCrosspointPins[2], AppConfig::kCrosspointPins[3],
               AppConfig::kCrosspointPins[4], AppConfig::kCrosspointPins[5],
               AppConfig::kCrosspointPins[6], AppConfig::kCrosspointPins[7]);
    gMutex = xSemaphoreCreateMutex();

    if (gMutex == nullptr)
    {
        return false;
    }

    uint64_t pinMask = 0;

    for (gpio_num_t pin : kAddressPins)
    {
        pinMask |=
            (1ULL << static_cast<uint32_t>(pin));
    }

    pinMask |=
        (1ULL << static_cast<uint32_t>(kDataPin));

    pinMask |=
        (1ULL << static_cast<uint32_t>(kStrobePin));

    gpio_config_t config = {};
    config.pin_bit_mask = pinMask;
    config.mode = GPIO_MODE_OUTPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;

    if (gpio_config(&config) != ESP_OK)
    {
        return false;
    }

    // Safe startup:
    //   address = 0
    //   DATA    = 0
    //   STROBE  = HIGH
    //
    // With the external STROBE pull-up and DATA/address pull-downs, boot-time
    // GPIO float cannot intentionally close a keypad switch.
    for (gpio_num_t pin : kAddressPins)
    {
        gpio_set_level(pin, 0);
    }

    gpio_set_level(kDataPin, 0);
    gpio_set_level(kStrobePin, 1);

    vTaskDelay(pdMS_TO_TICKS(2));

    gDebugWritesEnabled = false;
    crosspointReleaseAll();
#if ASTROCADE_CROSSPOINT_DEBUG_ENABLED && ASTROCADE_SERIAL_DEBUG_ENABLED
    gDebugQueue = xQueueCreate(128, sizeof(DebugWrite));
    if (gDebugQueue && xTaskCreatePinnedToCore(debugTask, "xpt_log", 3072,
            nullptr, 1, nullptr, 0) != pdPASS) {
        vQueueDelete(gDebugQueue);
        gDebugQueue = nullptr;
    }
    if (!gDebugQueue) APP_LOG("crosspoint: debug logger unavailable");
#endif
    gDebugWritesEnabled = true;

    return true;
}
