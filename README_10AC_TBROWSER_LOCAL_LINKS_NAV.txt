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
