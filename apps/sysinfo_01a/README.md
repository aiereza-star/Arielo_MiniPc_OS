# Arielo MiniPC OS - SYSINFO 01A FIX2 UI ORDER

## Objetivo

Herramienta externa de diagnóstico para Arielo MiniPC OS, usando el mismo
chasis probado de Calculator / Notepad.

## Filosofía

```text
SO base: no tocar
App externa: sí
Modo gráfico: SM_400X240
Entrada: teclado + touch + ratón
Salida: ESC/Q vuelve a SM_TEXT
```

## Qué muestra la 01A

```text
- heap interna libre
- bloque interno mayor
- PSRAM libre
- bloque PSRAM mayor
- uptime
- /root montado
- /root/bin montado
- /sdcard montado
- /usb montado
- contador rápido de entradas por raíz
- BT keyboard conectado
- touch OK
- puntero X/Y + botones
```

## Botones / teclas

```text
R / REFRESH : refrescar datos
S / SAVE    : guardar /root/sysinfo.txt
ESC / Q     : salir
```

## Qué NO hace todavía

```text
- no WiFi IP/RSSI
- no tamaño libre real de filesystem
- uptime no es de reloj real; es aproximado por frames
```

Eso queda para una futura 01B si exponemos símbolos del SO base o integramos
una API interna. La 01A evita pedir símbolos nuevos.

## Compilación

En terminal ESP-IDF dentro de esta carpeta:

```bat
buildelf.bat
```

o en Linux/MSYS:

```bash
./buildelf.sh
```

Salida esperada:

```text
sysinfo.xtensa.elf
```

Copiar a:

```text
/root/bin/sysinfo.xtensa.elf
```

o SD/USB si el APPS Launcher está buscando allí.

## Nota

Esta app usa funciones ya usadas por el chasis de Notepad/Calculator:

```text
rgb_display_set_mode(SM_400X240)
rgb_gfx_rectfill
rgb_display_wait_vsync
bt_keyboard_is_pressed
usb_hid_mouse_get_state
minipc_touch_read
```

y añade consulta de heap:

```text
heap_caps_get_free_size
heap_caps_get_largest_free_block
uptime aproximado por frames
```


## FIX1 NO64 LINKSAFE

Esta variante corrige el error de linker:

```text
BFD assertion fail ... elf32-xtensa.c
```

Se eliminan de la app externa:

```text
- reloj de 64 bits
- operaciones de 64 bits
- calculos de barras de 64 bits
```

Motivo: con `-fPIC -shared` algunas toolchains Xtensa nuevas pueden fallar
internamente al enlazar helpers de 64 bits en ELF externo.

El uptime mostrado es aproximado por frames, suficiente para diagnóstico visual.


## FIX2 UI ORDER

Retoque visual tras primera prueba en pantalla real:

```text
- panel MEMORIA más alto
- uptime movido a la derecha del título
- barras algo más cortas
- PSRAM ya no pisa la línea inferior
- panel MOUNTS más alto
- columnas MOUNTS alineadas: nombre / estado / ruta / items
- USB ya no queda pegado al borde
- panel BTKEY/TOUCH/PTR más compacto
- botones inferiores algo más ordenados
```

La lógica de SYSINFO no cambia.
