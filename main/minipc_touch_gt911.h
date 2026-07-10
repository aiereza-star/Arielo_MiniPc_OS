#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa el touch GT911 (reset por CH422G EXIO1 + sondeo I2C 0x14).
// Debe llamarse DESPUES de minipc_sd_init() (que deja el I2C y el CH422G listos).
esp_err_t minipc_touch_init(void);

// Lee un punto tactil. Devuelve true si hay toque, y rellena x,y en
// coordenadas de PANTALLA FISICA (0..799, 0..479).
bool minipc_touch_read(int16_t *x, int16_t *y);

// 1 si el GT911 respondio en init.
int minipc_touch_ok(void);

#ifdef __cplusplus
}
#endif
