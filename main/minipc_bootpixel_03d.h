#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 03D_BOOTPIXEL_REAL_SPLASH
 *
 * Splash grafico real:
 *   - modo grafico SM_VGA13H
 *   - framebuffer 8bpp indexado
 *   - paleta RGB565 propia
 *   - dibujo por pixeles, rectangulos, lineas y fuente 5x7
 *
 * Mantiene el flujo seguro:
 *   rgb_display_init();
 *   minipc_bootpixel_begin();
 *   cargar componentes...
 *   minipc_bootpixel_ready();
 *   minipc_bootpixel_finish_to_text();
 *   my_console_init();
 */

void minipc_bootpixel_begin(void);
void minipc_bootpixel_status(const char *name, const char *status);
void minipc_bootpixel_ok(const char *name);
void minipc_bootpixel_wait(const char *name);
void minipc_bootpixel_fail(const char *name);
void minipc_bootpixel_message(const char *msg);
void minipc_bootpixel_ready(void);
void minipc_bootpixel_finish_to_text(void);

#ifdef __cplusplus
}
#endif
