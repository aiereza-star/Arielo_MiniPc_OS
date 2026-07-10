# Arielo MiniPC OS - Calculator 01H COMPACT MOUSE TOUCH

App externa ELF para Arielo MiniPC OS / Waveshare ESP32-S3 7".

## Cambios 01H

- Keypad compactado para que la ultima fila no quede cortada abajo en `SM_400X240`.
- Eliminada la linea de descripcion inferior que pisaba los botones.
- Mantiene entrada por teclado.
- Mantiene entrada por touch GT911.
- Recupera entrada por raton USB HID.
- Click izquierdo o dedo = pulsar boton grafico.
- Click derecho = salir.
- Boton grafico ESC, `Q` o `ESC` = salir.
- Sigue sin usar `rgb_gfx_rect()`, solo `rgb_gfx_rectfill()` para evitar el error de simbolo no exportado.

## Simbolos que debe exportar el SO base

En el `esp_all_symbol.c` activo del loader ELF deben estar exportados:

```c
ESP_ELFSYM_EXPORT(usb_hid_mouse_get_state),
ESP_ELFSYM_EXPORT(usb_hid_mouse_set_position),
ESP_ELFSYM_EXPORT(minipc_touch_ok),
ESP_ELFSYM_EXPORT(minipc_touch_read),
```

Y sus declaraciones externas, usando el mismo estilo que la tabla actual de tu loader:

```c
extern int usb_hid_mouse_get_state;
extern int usb_hid_mouse_set_position;
extern int minipc_touch_ok;
extern int minipc_touch_read;
```

Si tu `esp_all_symbol.c` usa prototipos reales en vez de `extern int`, puedes usar:

```c
extern void usb_hid_mouse_get_state(int *x, int *y, uint8_t *buttons);
extern void usb_hid_mouse_set_position(int x, int y);
extern int  minipc_touch_ok(void);
extern bool minipc_touch_read(int16_t *x, int16_t *y);
```

## Compilar

Desde consola ESP-IDF dentro de la carpeta `calculator`:

```bat
buildelf.bat
```

Salida esperada:

```text
calculator.xtensa.elf
```

Copiar a `/root/bin`, SD o USB y lanzar desde APPS Launcher.
