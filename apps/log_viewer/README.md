# Arielo MiniPC OS - Log Viewer 02A FIXED USB MOUSE TOUCH

Segunda app herramienta externa para Arielo MiniPC OS.

## Objetivo 02A

Visor de logs sencillo, seguro y de solo lectura.

- Abre por defecto: `/usb/log.txt`
- Boton `SD`: abre `/sdcard/log.txt`
- Boton `USB`: vuelve a `/usb/log.txt`
- `RLD`: recarga el archivo actual
- Scroll con teclado, touch o raton
- Busqueda rapida `ERR`: salta a la siguiente linea con `ERROR`, `FAIL`, `ELF` o `PANIC`
- Sale con `Q`, `ESC`, boton `ESC` o boton derecho del raton

## Controles

Teclado:

```text
UP/DOWN      scroll linea
PgUp/PgDn    scroll pagina
Home/End     principio/final
Left/Right   desplazamiento horizontal
R            reload
U            /usb/log.txt
S            /sdcard/log.txt
E            siguiente ERROR/FAIL/ELF/PANIC
Q/ESC        salir
```

Touch/raton:

```text
RLD  recargar
USB  /usb/log.txt
SD   /sdcard/log.txt
UP   subir una linea
DN   bajar una linea
PG+  pagina abajo
PG-  pagina arriba
ERR  siguiente marca de error
ESC  salir
```

## Estructura

```text
log_viewer/
  log_viewer.c
  buildelf.bat
  buildelf.sh
  README.md
  local_include/
```

## Simbolos del SO base necesarios

Ya deberian estar abiertos desde calculator 01H:

```c
ESP_ELFSYM_EXPORT(usb_hid_mouse_get_state),
ESP_ELFSYM_EXPORT(usb_hid_mouse_set_position),
ESP_ELFSYM_EXPORT(minipc_touch_ok),
ESP_ELFSYM_EXPORT(minipc_touch_read),
```

Ademas esta app usa E/S de fichero estandar:

```text
fopen
fread
fclose
```

Si el ELF loader fallara con `Can't find symbol fopen`, `fread` o `fclose`, entonces el siguiente parche seria exportar esas funciones de libc/VFS en el `esp_all_symbol.c` activo.

## Compilar

Desde consola ESP-IDF dentro de la carpeta `log_viewer`:

```bat
buildelf.bat
```

Salida esperada:

```text
log_viewer.xtensa.elf
```

Copiar `log_viewer.xtensa.elf` al USB, SD o `/root/bin` y lanzar desde APPS Launcher.
