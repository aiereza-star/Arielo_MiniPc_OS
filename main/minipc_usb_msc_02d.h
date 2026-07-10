#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Inicializa el cliente USB MSC (pendrive). Debe llamarse DESPUES de
// usb_hid_keyboard_init(), porque el USB Host ya lo instala el HID.
// USB_MSC_HOTPLUG_LAB_01E_FIX1:
//   - monta pendrive conectado en caliente en /usb
//   - desmonta al retirar
//   - permite reconectar sin reiniciar
//   - reset retardado solo del driver MSC tras desconectar
//   - parche Unit Attention/Read Capacity retry en componente MSC
//   - /usb VFS persistente para evitar ESP_ERR_NO_MEM al reconectar
esp_err_t minipc_usb_msc_init(void);

// 1 si hay un pendrive montado ahora mismo.
int minipc_usb_msc_mounted(void);

// Texto corto de estado para debug/FILES/topbar.
const char *minipc_usb_msc_status_text(void);

// Contador que cambia al montar/desmontar o tras fallos importantes.
uint32_t minipc_usb_msc_generation(void);

#ifdef __cplusplus
}
#endif
