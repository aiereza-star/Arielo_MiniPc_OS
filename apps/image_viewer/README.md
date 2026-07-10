# Arielo MiniPC OS - Image Viewer 04A

App externa ELF para Arielo MiniPC OS.

## Objetivo

Visor de imagenes BMP con teclado, touch GT911 y raton USB HID.

## Version

`04C_FLICKERFIX_DIRTY_REDRAW`

## Funciones

- Modo grafico `SM_400X240`.
- Carga BMP desde `/usb` o `/sdcard`.
- Mini ventana `LOAD` que lista los `.bmp` existentes en la ubicacion activa.
- Soporta BMP **8 bpp indexado** y **24 bpp** sin compresion.
- Modo `FIT` y modo `1:1`.
- `PREV/NEXT` para recorrer imagenes del directorio.
- Salida limpia a `SM_TEXT`.

## Controles en pantalla

- `ESC` salir.
- `USB/SD` cambiar origen.
- `LOAD` abrir selector de BMP.
- `PREV/NEXT` imagen anterior/siguiente.
- `FIT` ajustar a ventana.
- `1:1` modo original.

## Controles de teclado

- `Q` / `ESC` salir.
- `L` abrir selector.
- `U` seleccionar USB.
- `D` seleccionar SD.
- `P` imagen anterior.
- `N` imagen siguiente.
- `F` modo FIT.
- `O` modo 1:1.
- Flechas: desplazamiento en modo `1:1`.
- En selector LOAD:
  - `UP/DOWN` mover seleccion.
  - `ENTER` cargar.
  - `BACKSPACE` cancelar.

## Notas

- Limite de carga actual: `800x480` maximo.
- Esta version esta pensada como base funcional del visor de imagenes.
- Si el loader ELF no encontrase algun simbolo de `stdio/dirent`, habria que exportarlo en el `esp_all_symbol.c` activo, aunque las apps anteriores ya demostraron que `fopen` y `opendir/readdir` funcionan.


## Cambio 04B

- Eliminado uso de `rgb_gfx_hline`.
- Eliminado uso de `rgb_gfx_vline`.
- El puntero del raton se dibuja con `rgb_gfx_rectfill`.
- Evita tener que exportar nuevos simbolos graficos en el loader ELF.


## Cambio 04C

- Eliminado el refresco completo en cada vuelta del bucle.
- La pantalla solo se redibuja cuando cambia algo: LOAD, PREV/NEXT, FIT/1:1, selector o desplazamiento.
- Eliminado el puntero dibujado por la app para no tener que repintar el fondo continuamente.
- Corregido el boton `ESC` grafico para salir de la app.
- Objetivo: quitar barra negra parpadeante y parpadeo al visualizar imagenes.

- No se hace `rgb_gfx_clear()` global en cada repintado; cada zona limpia su fondo para evitar pantallazos negros.
