#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_hid_keyboard_char_cb_t)(char c);

typedef struct {
    int x;              /* posicion virtual 0..399; LCD = x*2 */
    int y;              /* posicion virtual 0..239; LCD = y*2 */
    int dx;
    int dy;
    int wheel;
    uint8_t buttons;
    bool left;
    bool right;
    bool middle;
    bool pressed;
    bool released;
} usb_hid_mouse_event_t;

typedef void (*usb_hid_mouse_cb_t)(const usb_hid_mouse_event_t *ev);

void usb_hid_keyboard_set_char_callback(usb_hid_keyboard_char_cb_t cb);
esp_err_t usb_hid_keyboard_init(void);
int usb_hid_keyboard_connected(void);

void usb_hid_mouse_set_callback(usb_hid_mouse_cb_t cb);
int usb_hid_mouse_connected(void);
void usb_hid_mouse_get_state(int *x, int *y, uint8_t *buttons);
void usb_hid_mouse_set_position(int x, int y);

#ifdef __cplusplus
}
#endif
