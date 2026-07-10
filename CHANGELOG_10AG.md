# 10AG - HOME WEB BUTTON -> TactileBrowser Lexbor GUI

Base: 10AF_OK_TBROWSER_GUI_VIEWER_FIRST.

Cambios:
- El boton WEB del escritorio ya no abre el navegador antiguo integrado pre-Lexbor.
- El boton WEB lanza `tbbgui file /root/tbbrowser_home.html`.
- Si `/root/tbbrowser_home.html` no existe, el escritorio lo crea automaticamente con una pagina inicial local.
- Al salir de `tbbgui` con `q`, el escritorio restaura SM_400X240 y vuelve al menu principal.
- El navegador antiguo queda compilado como respaldo, pero fuera del flujo principal.

Prueba:
1. `desktop`
2. Pulsar WEB.
3. Debe abrir TactileBrowser GUI 10AF con pagina de inicio local.
4. Probar enlaces, ayuda, favorito, salida `q`.
5. Debe volver al escritorio sin reinicio.
