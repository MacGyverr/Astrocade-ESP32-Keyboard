#pragma once

#include <cstdint>

enum class InputSource : uint8_t
{
    Usb = 0,
    Ble = 1,
    Count
};
