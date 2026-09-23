#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Called only for descriptor-identified keyboard input, without a report ID. */
#ifdef __cplusplus
static constexpr bool bleReportToBoot(const uint8_t *data, size_t length, uint8_t boot[8])
#else
static inline bool bleReportToBoot(const uint8_t *data, size_t length, uint8_t boot[8])
#endif
{
    if (!data || !boot || (length != 7 && length != 8)) {
        return false;
    }
    if (length == 8) {
        for (size_t i = 0; i < 8; i++) {
            boot[i] = data[i];
        }
    } else {
        /* K380s omits the reserved byte in its seven-byte report. */
        boot[0] = data[0];
        boot[1] = 0;
        for (size_t i = 0; i < 6; i++) {
            boot[i + 2] = data[i + 1];
        }
    }
    return true;
}
