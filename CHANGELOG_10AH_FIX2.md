# 10AH_FIX2 - GUI keep-mode + RGB ISR safe

Motivo:
- Tras abrir/cerrar varios enlaces en 10AH aparecio Guru Meditation en `on_bounce_empty()` del RGB LCD.
- El patron encaja con carrera al entrar/salir de `SM_400X240` varias veces: el ISR podia leer el framebuffer mientras otra tarea lo desmontaba.

Cambios:
- `tbbrowser_gui_10ah`: mantiene el modo grafico activo durante toda la sesion del navegador.
- Ya no hace `rgb_display_set_mode(SM_TEXT)` entre enlaces/back/home/bookmarks/reload.
- Solo vuelve a texto al salir definitivamente con `q` o error final.
- `rgb_display.c`: snapshot local del framebuffer dentro del ISR y pequeña espera antes de liberar framebuffer al volver a texto.

Prueba:
- Abrir WEB -> about:home.
- Abrir/cerrar enlaces repetidas veces.
- Usar back, reload, bookmarks, home, q.
- Debe evitar el Guru en `on_bounce_empty`.
