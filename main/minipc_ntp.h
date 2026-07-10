#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Arranca el servicio NTP vivo. No bloquea.
// Puede llamarse antes de que WiFi este conectado: espera WiFi/IP en segundo plano.
// Configura la zona horaria Europe/Madrid (CET/CEST con cambio automatico).
void minipc_ntp_start(void);

// true si ya se ha sincronizado la hora al menos una vez.
bool minipc_ntp_synced(void);

// Escribe en 'out' la fecha/hora formateada "dd/mmm/aaaa hh:mm" (24h).
// Si aun no hay hora sincronizada, escribe "--/---/---- --:--".
// 'out' debe tener al menos 24 bytes.
void minipc_ntp_format(char *out, int out_size);

#ifdef __cplusplus
}
#endif
