#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 03C_BOOTGFX_LOGO_PRO
 *
 * Splash grafico por celdas con logo Arielo MiniPC OS mejorado.
 * Mantiene la arquitectura validada de 03B:
 *   - buffer propio lcd_cell_t
 *   - sin stdout/VTerm durante splash
 *   - sin scroll
 *   - my_console_init() recupera la pantalla al final
 */

void minipc_bootgfx_begin(void);
void minipc_bootgfx_status(const char *name, const char *status);
void minipc_bootgfx_ok(const char *name);
void minipc_bootgfx_wait(const char *name);
void minipc_bootgfx_fail(const char *name);
void minipc_bootgfx_message(const char *msg);
void minipc_bootgfx_ready(void);

#ifdef __cplusplus
}
#endif
