#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 02D_FIX3_USBSEL_NO_I2C_INSTALL
 *
 * Fuerza USB_SEL / EXIO5 = LOW usando el bus I2C ya inicializado por la placa.
 * No vuelve a llamar a i2c_driver_install(), para evitar:
 *   E I2C: I2C DRIVER INSTALL ERROR
 */
esp_err_t minipc_usbsel_force_host_mode(void);
esp_err_t minipc_usbsel_cycle_host_mode(void);

#ifdef __cplusplus
}
#endif
