#pragma once

#include <cstddef>
#include <cstdint>

enum class OverlayMode : uint8_t
{
    Basic = 0
};

bool settingsInit();

OverlayMode settingsGetOverlayMode();
bool settingsSetOverlayMode(OverlayMode mode);

bool settingsGetPreferredBle(uint8_t address[6], uint8_t *addressType);
bool settingsSetPreferredBle(const uint8_t address[6], uint8_t addressType);
bool settingsGetPreferredBleName(char *name, std::size_t length);
bool settingsSetPreferredBleName(const char *name);
bool settingsClearPreferredBle();
