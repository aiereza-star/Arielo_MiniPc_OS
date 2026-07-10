# 10AM_TBROWSER_CLASSIC_HISTORY_FORWARD

Escalon basado en 10AL_FIX4. Añade boton adelante `>`, pagina interna `about:history` y mantiene barra clasica superior.

# 10AI_TBROWSER_GUI_TOUCH_BUTTONS

Escalón basado en 10AH_FIX2: añade botones táctiles GT911 al TactileBrowser GUI interno, manteniendo teclado y motor HTML validado. Véase `README_10AI.md` y `CHANGELOG_10AI.md`.

# WAVESHARE_7_MINIPC_BREEZYBOX_LAB_10AC_TBROWSER_LOCAL_LINKS_NAV

Base: `10AB_TBROWSER_TEXT_VIEWER_SCROLL` validada.

Objetivo 10AC:

- Mantener Lexbor integrado en firmware normal, con PSRAM-first.
- Mantener `tbtest`, `tbrender` y `tbview` tal como venían de 10AB.
- Añadir `tbbrowser`, un navegador local de texto con navegación básica.
- Detectar enlaces HTML `<a href=...>` y numerarlos como `[1]`, `[2]`, etc.
- Permitir abrir enlaces locales por número.
- Permitir abrir otra ruta desde dentro con `o /sdcard/archivo.html`.
- Permitir recargar con `r`.

## Compilar

```powershell
cd D:\ESP32_IDF_LAB\WAVESHARE_7_MINIPC_BREEZYBOX_LAB_10AC_TBROWSER_LOCAL_LINKS_NAV
idf.py fullclean
idf.py build flash monitor
```

## Pruebas recomendadas

```text
tbtest
tbtest parse
tbrender file /sdcard/test.html
tbview file /sdcard/test.html
tbbrowser file /sdcard/test.html
```

También:

```text
tbbrowser sample
tbbrowser /sdcard/test.html
```

## Controles dentro de tbbrowser

```text
n o ENTER  baja una página
p          sube una página
j/k        baja/sube una línea
l          lista enlaces detectados
numero     abre enlace local [n]
o RUTA     abre archivo local
r          recarga documento actual
g/G        inicio/final
h/?        ayuda
q          salir
```

Notas:

- Enlaces `http://`, `https://`, `mailto:` y `ftp://` se detectan pero aún no se abren.
- Los enlaces relativos se resuelven respecto al directorio del archivo actual.
- Si el documento es `sample`, un enlace relativo se intenta resolver desde `/sdcard/`.
- Sigue sin CSS, imágenes ni red. Este escalón es navegación local HTML estable.

## Arielo MiniPC OS 10AE - TactileBrowser bookmarks

La rama 10AE mantiene el navegador local interno validado en 10AD y añade favoritos persistentes:

- `tbbrowser file /sdcard/test_10ae_index.html`
- `m` guarda la pagina actual en `/root/tbbrowser_favs.txt`
- `v` lista favoritos y permite abrir uno
- `tbbrowser bookmarks` abre directamente el selector de favoritos
- `tbbrowser10ad` queda como fallback de la 10AD validada

## 10AH - TactileBrowser GUI con paginas internas

El boton WEB del escritorio abre ahora `tbbgui home`, que carga la pagina interna `about:home` generada por el propio navegador.
Tambien se agregan `about:help` y `about:bookmarks`.

## 10AH_FIX1

Corrección de compilación: se adelanta el prototipo de `tbb10ah_chomp()` antes de su primer uso en la generación HTML de favoritos internos.


## 10AH_FIX2

Mantiene SM_400X240 activo durante toda la sesion GUI para evitar carreras del ISR RGB al navegar entre paginas.

## 10AK - TactileBrowser GUI visual render polish

Base sobre 10AJ_OK. Pulido visual del visor grafico: encabezados, listas, enlaces y scrollbar lateral.


## 10AL_TBROWSER_NET_HTTP_NAVBAR

Primer escalon de red: HTTP basico + boton URL. Ver `README_10AL.md`.


## 10AN_FIX1
- Added missing `rgb_display_set_mouse_busy(int busy)` prototype to both rgb_display headers to fix implicit declaration build error.


## 10AN_FIX2_BUSY_CURSOR_HEADER_AND_LOCAL_PROTO
- Blindaje del prototipo `rgb_display_set_mouse_busy(int busy)` en `include/rgb_display.h`, `rgb_display.h` y declaración local en `cmd_desktop_07A_GUI_SHELL.c` para evitar `implicit declaration` aunque CMake incluya otra ruta de cabecera.

## Escalón 10AO

Añadido soporte inicial HTTPS/TLS al TactileBrowser Lexbor GUI, manteniendo la barra clásica, historial bidireccional y el indicador WAIT de cabecera de 10AN_FIX3.

## 10AQ_TBROWSER_DDG_RESULTS_POLISH

Pulido del buscador DuckDuckGo HTML: limpieza ligera de resultados, ventana LINK más útil y apertura directa de enlaces DDG con `uddg=`.
