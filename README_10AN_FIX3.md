# Arielo MiniPC OS - 10AN_FIX3_HEADER_BUSY_HOURGLASS

Base: 10AN_FIX2 compilada por el usuario.

Cambio principal:
- Se abandona depender visualmente del cambio de forma del puntero.
- Se añade un indicador fijo `WAIT` + reloj de arena dibujado en la cabecera superior derecha.
- El indicador se pinta antes de entrar en cargas bloqueantes y desaparece al mostrarse la página.

Se activa en:
- URL / Enter
- enlaces
- reload
- back / forward
- about:history, about:bookmarks, about:files
- carga HTTP y carga de archivo

Archivo principal modificado:
- `main/cmd_tbbrowser_gui_10ah.c`
