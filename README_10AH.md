# Arielo MiniPC OS - 10AH TactileBrowser GUI Internal Home Pages

Esta rama parte de `10AG_OK_HOME_WEB_BUTTON_LEXBOR_GUI`.

La novedad es que el navegador Lexbor GUI empieza a tener paginas internas propias:

- `about:home` / `home`: pagina inicial interna.
- `about:help`: ayuda integrada.
- `about:bookmarks`: favoritos generados desde `/root/tbbrowser_favs.txt`.

El boton WEB del escritorio ejecuta:

```text
tbbgui home
```

y ya no depende de que exista `/root/tbbrowser_home.html`, aunque ese archivo se conserva como respaldo historico.

## Comandos utiles

```text
tbbgui home
tbbgui about:help
tbbgui about:bookmarks
tbbgui file /sdcard/test_10af_gui.html
```

## Controles GUI

```text
n / ENTER / espacio  pagina abajo
p                   pagina arriba
j / k               linea abajo / arriba
g / G               inicio / final
1-9                 abrir enlace
b                   atras
r                   recargar
m                   guardar favorito
v                   abrir favoritos internos
h / ?               ayuda rapida overlay
H                   pagina interna de inicio
q                   salir
```
