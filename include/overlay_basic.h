#pragma once

#include <cstdint>
#include <cstddef>
#include "text_compiler.h"

#include "input_source.h"

bool overlayBasicInit();
bool overlayBasicMapText(uint32_t character, TextKey &key);

void overlayBasicKeyDown(
    InputSource source,
    uint8_t usage,
    uint8_t modifiers);

bool overlayBasicDescribeKeyDown(
    uint8_t usage,
    uint8_t modifiers,
    char *buffer,
    std::size_t length);

void overlayBasicKeyUp(
    InputSource source,
    uint8_t usage,
    uint8_t modifiers);

void overlayBasicSourceDisconnected(InputSource source);

// Drops queued synthetic BASIC keystrokes and releases all electronic matrix
// switches. Used only for fatal/error recovery.
void overlayBasicEmergencyRelease();
