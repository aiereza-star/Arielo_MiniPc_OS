# Arielo MiniPC OS - Mini Paint 03C_LOAD_PICKER

App externa pura ELF para Arielo MiniPC OS.

## Novedad 03C

- El boton `LOAD` ya no carga solo el nombre actual a ciegas.
- Abre una mini ventana con los `.BMP` existentes en la ubicacion activa:
  - `USB` -> lista `/usb/*.bmp`
  - `SD`  -> lista `/sdcard/*.bmp`
- Se puede escoger el BMP con touch, raton o teclado.
- Al cargar, actualiza el nombre base al archivo elegido.

## Estructura

```text
mini_paint/
  mini_paint.c
  buildelf.bat
  buildelf.sh
  README.md
  local_include/
```

## Botones superiores

- `ESC`: salir.
- `PEN`: pincel.
- `ERS`: goma.
- `CLR`: limpiar lienzo.
- `-` / `+`: tamano de pincel.
- `COL`: cambia color.
- `USB/SD`: alterna destino.
- `NAM`: cambia nombre base.
- `SAVE`: guarda BMP como `/usb/<nombre>.bmp` o `/sdcard/<nombre>.bmp`.
- `LOAD`: abre selector de BMP en la ubicacion activa.

## Ventana LOAD

- Flecha arriba/abajo: mover seleccion.
- Enter: cargar seleccionado.
- ESC/Q: cancelar.
- Click/touch sobre un archivo: cargarlo.
- Botones `UP`, `DN`, `OK`, `CANCEL` disponibles en pantalla.

## Teclado principal

- Flechas: mover cursor.
- Shift + flechas: movimiento rapido.
- Space/Enter: dibujar.
- `P`: pincel.
- `E`: goma.
- `C`: color.
- `X`: limpiar.
- `1` / `2`: pincel menor/mayor.
- `U`: destino USB.
- `D`: destino SD.
- `N`: editar nombre.
- `B` o `S`: guardar BMP.
- `L`: abrir selector LOAD.
- `R`: guardar RAW.
- `Q` / `ESC`: salir.

## Formato BMP compatible

El cargador esta pensado para BMPs creados por esta app:

```text
384 x 184
8 bpp indexado
sin compresion
```

## Simbolos necesarios

Ademas de las puertas ya abiertas para graficos, teclado, mouse y touch, esta version usa listado de directorios:

```text
opendir
readdir
closedir
```

Si el loader ELF muestra `Can't find symbol opendir`, `readdir` o `closedir`, hay que exportar esos simbolos en el `esp_all_symbol.c` activo igual que se hizo con mouse/touch.
