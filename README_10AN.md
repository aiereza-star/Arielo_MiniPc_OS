# Arielo MiniPC OS 10AN - TactileBrowser Busy Hourglass Cursor

Esta versión parte de 10AM_OK. Añade un indicador visual de carga: el puntero del ratón/touch pasa a forma de reloj de arena mientras se ejecutan operaciones bloqueantes del navegador.

Pruebas recomendadas:

```text
desktop
WEB
URL -> neverssl.com/ -> ENTER
Abrir enlaces
< BACK
> FORWARD
HIST
FAV
RLD
HOME
```

Durante cada carga debe verse el reloj de arena. Al finalizar, vuelve la flecha normal.


## 10AN_FIX2_BUSY_CURSOR_HEADER_AND_LOCAL_PROTO
- Blindaje del prototipo `rgb_display_set_mouse_busy(int busy)` en `include/rgb_display.h`, `rgb_display.h` y declaración local en `cmd_desktop_07A_GUI_SHELL.c` para evitar `implicit declaration` aunque CMake incluya otra ruta de cabecera.
