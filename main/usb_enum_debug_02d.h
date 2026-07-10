#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 02D_FIX1_USB_ENUM_DEBUG
 *
 * Diagnóstico puro de USB Host:
 * - Instala USB Host Library.
 * - Registra un cliente USB.
 * - Imprime NEW_DEV, VID/PID, clase, interfaces y endpoints.
 *
 * No lee teclado y no monta pendrive. Solo responde:
 *   ¿El ESP32-S3 está viendo D+/D- y enumerando dispositivos?
 */
esp_err_t usb_enum_debug_init(void);

#ifdef __cplusplus
}
#endif
