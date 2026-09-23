#pragma once

#include "app_config.h"

#include "esp_rom_sys.h"

#define APP_NOTICE(format, ...)                                             \
    do                                                                      \
    {                                                                       \
        esp_rom_printf("[AstrocadeKB] " format "\r\n", ##__VA_ARGS__);     \
    } while (false)
#if ASTROCADE_SERIAL_DEBUG_ENABLED
#define APP_LOG(...) APP_NOTICE(__VA_ARGS__)
#else
#define APP_LOG(format, ...)                                                \
    do                                                                      \
    {                                                                       \
    } while (false)
#endif
