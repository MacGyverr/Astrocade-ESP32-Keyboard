#include "keyboard_status.h"
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace {
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    keyboard_connections_t connections{};
    void update(keyboard_connection_t &destination, bool connected, const char *name)
    {
        keyboard_connection_t value{};
        value.connected = connected;
        if (connected && name) std::snprintf(value.name, sizeof(value.name), "%s", name);
        // Only event-time snapshots; never lock around GATT, USB or HTTP operations.
        portENTER_CRITICAL(&lock);
        destination = value;
        portEXIT_CRITICAL(&lock);
    }
}
void keyboardStatusSetUsb(bool connected, const char *name) { update(connections.usb, connected, name); }
void keyboardStatusSetBle(bool connected, const char *name) { update(connections.ble, connected, name); }
void keyboardStatusGet(keyboard_connections_t *out)
{
    portENTER_CRITICAL(&lock);
    *out = connections;
    portEXIT_CRITICAL(&lock);
}
