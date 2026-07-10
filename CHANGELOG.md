# Changelog 10AC

## 10AC_TBROWSER_LOCAL_LINKS_NAV

- Base: 10AB validada.
- Añadido `main/cmd_tbbrowser_lexbor_10ac.c`.
- Añadido comando `tbbrowser`.
- `tbbrowser` carga HTML local desde sample/SD/USB/root.
- Detecta `<a href=...>` durante el recorrido DOM.
- Numera enlaces en el render como `[n]`.
- Permite listar enlaces con `l`.
- Permite abrir enlaces locales con el número.
- Permite `o RUTA` para abrir otro HTML desde el navegador.
- Permite `r` para recargar.
- No toca el flujo estable de `tbview` 10AB.


## 10AN_FIX1
- Added missing `rgb_display_set_mouse_busy(int busy)` prototype to both rgb_display headers to fix implicit declaration build error.


## 10AN_FIX2_BUSY_CURSOR_HEADER_AND_LOCAL_PROTO
- Blindaje del prototipo `rgb_display_set_mouse_busy(int busy)` en `include/rgb_display.h`, `rgb_display.h` y declaración local en `cmd_desktop_07A_GUI_SHELL.c` para evitar `implicit declaration` aunque CMake incluya otra ruta de cabecera.
