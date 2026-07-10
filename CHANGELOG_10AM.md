# 10AM_TBROWSER_CLASSIC_HISTORY_FORWARD

Base: 10AL_FIX4_CLASSIC_HEADER_NAVBAR validada.

Objetivo:
- Mantener la barra superior clasica del navegador.
- Añadir boton `>` para avanzar en historial FORWARD.
- Añadir boton `HIST` para abrir una pagina interna `about:history`.
- Mantener HTTP basico, Lexbor GUI, favoritos, enlaces, home, reload y salida limpia.

Controles nuevos:
- `<` / tecla `b`: atras.
- `>` / tecla `F`: adelante.
- `HIST` / tecla `s`: historial interno.

Notas:
- La pila FORWARD se limpia cuando se abre una pagina nueva, igual que en un navegador normal.
- `about:history` lista pagina actual, historial hacia atras y paginas disponibles hacia adelante.
