#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 03B_BOOTGFX_CELL_SPLASH
 *
 * Splash grafico seguro en buffer propio lcd_cell_t.
 * No usa stdout, no usa VTerm y no puede hacer scroll.
 *
 * Requiere:
 *   rgb_display_init() ya ejecutado.
 *
 * Flujo:
 *   minipc_bootgfx_begin();
 *   minipc_bootgfx_status("Display", "OK");
 *   ...
 *   minipc_bootgfx_ready();
 *   luego my_console_init() recupera la pantalla para VTerm/BreezyBox.
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
