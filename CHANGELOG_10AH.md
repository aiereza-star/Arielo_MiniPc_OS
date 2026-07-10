# 10AH_TBROWSER_GUI_INTERNAL_HOME_PAGES

Base: 10AG_OK_HOME_WEB_BUTTON_LEXBOR_GUI.

Objetivo:
- Dar vida propia al TactileBrowser Lexbor GUI.
- Sustituir la dependencia de `/root/tbbrowser_home.html` por una pagina interna `about:home`.
- Anadir paginas internas tipo navegador local: `about:home`, `about:help`, `about:bookmarks`.
- Mantener el boton WEB del escritorio apuntando al navegador grafico, ahora con `tbbgui home`.

Cambios principales:
- `cmd_tbbrowser_gui_10ah.c`: nueva version del navegador GUI.
- Nuevo soporte para pseudo-rutas internas:
  - `home`
  - `about:home`
  - `about:help`
  - `about:bookmarks`
  - `about:favorites` / `about:favoritos` como alias.
- Tecla `H`: vuelve a la pagina interna de inicio.
- Tecla `v`: abre la pagina interna de favoritos en la GUI, no solo mensaje de consola.
- Boton WEB del escritorio: ejecuta `tbbgui home`.
- El antiguo `/root/tbbrowser_home.html` queda como respaldo, pero ya no es necesario para arrancar el navegador.

Pruebas recomendadas:
- `tbbgui home`
- `tbbgui about:help`
- `tbbgui about:bookmarks`
- Desde el escritorio: pulsar WEB.
- En GUI:
  - abrir ayuda desde enlace [1]
  - abrir favoritos desde enlace [2]
  - H vuelve al inicio
  - v abre favoritos
  - q sale limpio

Notas:
- No se toca CSS, imagenes, red ni formularios.
- Sigue usando Lexbor integrado en firmware normal con memoria PSRAM-first.
