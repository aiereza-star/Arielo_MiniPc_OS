#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t minipc_wifi_init_appmain(void);
esp_err_t minipc_wifi_start_async(uint32_t delay_ms);

bool minipc_wifi_is_connected(void);
const char *minipc_wifi_get_ip(void);

// Configuracion de WiFi editable (guardada en NVS, no anclada al codigo).
const char *minipc_wifi_get_ssid(void);
int minipc_wifi_save_credentials(const char *ssid, const char *pass);
// Reconexion en caliente con credenciales nuevas. 0=OK (guarda), -1=fallo.
int minipc_wifi_reconnect_with(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
