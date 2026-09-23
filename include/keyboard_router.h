#pragma once

#include <cstddef>
#include <cstdint>

#include "input_source.h"

using KeyboardRouterLockToggleCallback = void (*)(InputSource source);

void keyboardRouterInit();

void keyboardRouterSetNumLockToggleCallback(
    KeyboardRouterLockToggleCallback callback);

void keyboardRouterSetCapsLockToggleCallback(
    KeyboardRouterLockToggleCallback callback);

void keyboardRouterSetLockState(
    InputSource source,
    bool capsLock,
    bool numLock);

void keyboardRouterHandleBootReport(
    InputSource source,
    const uint8_t *data,
    std::size_t length);

void keyboardRouterSourceDisconnected(InputSource source);
