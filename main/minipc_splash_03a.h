#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 03A_BOOT_SPLASH_TEXT_SAFE
 *
 * Splash textual seguro para Arielo MiniPC OS.
 * Usa stdout/VTerm ya inicializado por my_console_init().
 * No toca framebuffer, USB, WiFi ni BreezyBox internamente.
 */

void minipc_splash_clear(void);
void minipc_splash_title(void);
void minipc_splash_status(const char *name, const char *status);
void minipc_splash_status_ok(const char *name);
void minipc_splash_status_wait(const char *name);
void minipc_splash_done(void);

#ifdef __cplusplus
}
#endif
