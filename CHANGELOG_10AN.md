# 10AN_TBROWSER_BUSY_HOURGLASS_CURSOR

Base: 10AM_TBROWSER_CLASSIC_HISTORY_FORWARD.

Cambios:
- Añadido modo de puntero ocupado en el driver RGB (`rgb_display_set_mouse_busy`).
- El overlay del ratón cambia de flecha a reloj de arena amarillo durante cargas bloqueantes.
- Activado al cargar URL, HOME, RLD, BACK, FORWARD, enlaces, historial y favoritos.
- Sin tocar el motor HTML/HTTP ni el historial validado de 10AM.

Objetivo: que el usuario vea actividad durante los segundos de descarga/render y no parezca que el navegador se ha quedado colgado.


## 10AN_FIX2_BUSY_CURSOR_HEADER_AND_LOCAL_PROTO
- Blindaje del prototipo `rgb_display_set_mouse_busy(int busy)` en `include/rgb_display.h`, `rgb_display.h` y declaración local en `cmd_desktop_07A_GUI_SHELL.c` para evitar `implicit declaration` aunque CMake incluya otra ruta de cabecera.
