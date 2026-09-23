#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct { bool connected; char name[96]; } keyboard_connection_t;
typedef struct { keyboard_connection_t usb, ble; } keyboard_connections_t;

void keyboardStatusSetUsb(bool connected, const char *name);
void keyboardStatusSetBle(bool connected, const char *name);
void keyboardStatusGet(keyboard_connections_t *out);

#ifdef __cplusplus
}
#endif
