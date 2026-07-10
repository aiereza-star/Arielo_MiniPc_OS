#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 03A_FIX2_SPLASH_FOREGROUND_BOOT
 *
 * Splash fijo de arranque para Arielo MiniPC OS.
 * Redibuja la pantalla completa tras cada etapa para tapar cualquier
 * printf/log que pueda salir durante la carga.
 *
 * Usar SOLO después de my_console_init().
 */

void minipc_splash_fg_begin(void);
void minipc_splash_fg_set(const char *name, const char *status);
void minipc_splash_fg_ok(const char *name);
void minipc_splash_fg_wait(const char *name);
void minipc_splash_fg_fail(const char *name);
void minipc_splash_fg_message(const char *message);
void minipc_splash_fg_ready(void);
void minipc_splash_fg_finish_to_shell(void);

#ifdef __cplusplus
}
#endif
