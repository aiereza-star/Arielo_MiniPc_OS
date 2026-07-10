# Arielo MiniPC OS - SHEET 01F CENTER LOADLIST MATHFIX

## Concepto

Mini hoja de cálculo externa para Arielo MiniPC OS, estilo SuperCalc / Lotus 1-2-3 / MS-DOS.

```text
SO base: no tocar
App externa: sí
Chasis: Calculator / Notepad
Modo gráfico: SM_400X240
Salida: SM_TEXT
```

## Qué trae la 01A

```text
- 12 columnas A-L
- 32 filas
- ventana visible de 5 columnas x 10 filas
- celda activa resaltada
- edición de texto/número/fórmula
- fórmulas básicas:
  =A1+B1
  =A1-B1
  =A1*B1
  =A1/B1
  =A1+5
- guardar CSV simple en /root/sheet.csv
- cargar CSV simple desde /root/sheet.csv
- teclado, ratón y touch
```

## Controles

```text
Flechas        mover celda
ENTER / F2     editar celda
Escribir texto empieza edición
BACKSPACE      borrar celda o borrar carácter en edición
TAB            siguiente celda
Ctrl+S         guardar /root/sheet.csv
Ctrl+O         cargar /root/sheet.csv
Ctrl+N         nueva hoja demo
ESC / Q        salir o cancelar edición
```

## Botones

```text
NEW
LOAD
SAVE
EDIT
EXIT
```

## Compilación

En terminal ESP-IDF dentro de esta carpeta:

```bat
buildelf.bat
```

Salida:

```text
sheet.xtensa.elf
```

Copiar a:

```text
/root/bin/sheet.xtensa.elf
```

o SD/USB si APPS Launcher está buscando allí.

## Notas 01A

El CSV es simple: separador coma, sin comillas complejas. Si una celda contiene
coma, se guarda como punto y coma visual para mantener el archivo sencillo.

No usa `int64_t`, `uint64_t`, `float` ni `double`, para evitar problemas de linker
con apps ELF externas.


## Cambios 01B

```text
- teclado ES-España por defecto
- F9 alterna teclado ES/US
- F4 alterna decimales visibles: 0, 1, 2, 3
- indicador ES/US y D0-D3 en barra inferior
- botón EDIT pasa a FORM
- si escribes '=' en navegación, abre asistente de fórmula
- asistente de fórmula:
  1) primera celda
  2) operación + - * /
  3) segunda celda
  4) Enter crea =A1+B1 y desaparece
- en modo FORM los botones inferiores cambian a:
  +  -  *  /  DEC
```

## Teclado ES

Mapa ES práctico para símbolos importantes:

```text
Shift+0 = =
Shift+7 = /
tecla + de teclado ES = +
Shift+tecla + = *
F9 cambia a mapa US si hace falta
```

## Decimales

```text
F4 o botón DEC: D0, D1, D2, D3
```

Los resultados de fórmulas se muestran con esos decimales. Internamente se usa
entero escalado x1000, sin float/double y sin 64 bits.


## FIX1 COMPILEFIX

Corrige dos errores de la 01B:

```text
implicit declaration of function 'parse_cell_ref'
static int save_csvstatic int save_csv(void)
```

Cambios:

```text
- añadido prototipo:
  static int parse_cell_ref(const char **pp, int *row, int *col);

- corregido encabezado pegado de save_csv()
```

La lógica de teclado ES, decimales y asistente de fórmulas se mantiene.


## Cambios 01C SUM CLEAN

```text
- la hoja ya arranca limpia, sin valores demo
- en modo FORM el último botón pasa de DEC a SUM
- F4 sigue cambiando decimales D0-D3
- nueva fórmula:
  =SUM(A1:A10)
  =SUM(A1:E1)
  =SUM(A1:E10)
```

## Uso de SUM

```text
1) sitúate en la celda donde quieres el total
2) pulsa = o botón FORM
3) pulsa botón SUM
4) selecciona o escribe primera celda del rango
5) selecciona o escribe última celda del rango
6) ENTER
```

Ejemplos:

```text
Total columna A: =SUM(A1:A10)
Total fila 1:    =SUM(A1:E1)
Total bloque:    =SUM(A1:E10)
```


## Cambios 01D SAVE DEBOUNCE + NUMBER OPERAND

```text
- SAVE con debounce/latch de touch para evitar re-disparos.
- al guardar muestra "guardando..." y solo guarda el área usada de la hoja.
- el asistente de fórmulas permite celda o número en los operandos.
```

Ejemplos admitidos:

```text
=A8/7
=A8*12
=A8+3.5
=100/B2
```

`SUM` sigue usando rangos de celdas:

```text
=SUM(A1:A10)
```

Notas:

```text
- F4 mantiene decimales D0-D3.
- F9 mantiene cambio ES/US.
- sin float/double y sin int64/uint64.
```


## FIX1 COMPILEFIX

Corrige errores de compilación de la 01D:

```text
implicit declaration of function 'draw_ui'
static int load_csvstatic int load_csv(void)
static void apply_actionstatic void apply_action(char a)
```

Cambios:

```text
- añadido prototipo:
  static void draw_ui(int mx, int my);

- corregidos encabezados pegados:
  load_csv()
  apply_action()
```

Se mantiene:

```text
- SAVE con debounce/cooldown
- guardado solo del área usada
- asistente permite A8/7, A8*12, A8+3.5, 100/B2
- SUM(A1:A10)
- F4 decimales
- F9 ES/US
```


## Cambios 01E FILE PICKER ROOT/SD/USB

```text
- SAVE ya no guarda fijo en /root/sheet.csv.
- LOAD ya no carga fijo desde /root/sheet.csv.
- Al pulsar SAVE o LOAD aparece un selector interno.
- Puedes escoger unidad:
  ROOT -> /root
  SD   -> /sdcard
  USB  -> /usb
- Puedes escribir el nombre del archivo.
- Si no pones extensión, añade .csv automáticamente.
```

## Controles del selector

```text
ROOT / SD / USB  elige unidad
OK               guardar/cargar
CAN              cancelar
ENTER            OK
ESC              cancelar
TAB              cambia unidad
BACKSPACE        borra carácter del nombre
```

Ejemplos válidos:

```text
gastos.csv
trabajo.csv
medidas_tdt.csv
calculo1
```

`calculo1` se guarda como:

```text
calculo1.csv
```


## FIX1 COMPILEFIX

Corrige dos cabeceras pegadas de la 01E:

```text
static int load_csvstatic int load_csv_from_path(...)
static void handle_touch_as_pointerstatic void handle_touch_as_pointer(...)
```

Se mantiene el selector ROOT/SD/USB y nombre de archivo para SAVE/LOAD.


## Cambios 01F CENTER LOADLIST MATHFIX

```text
- tabla principal centrada: GRID_X 36 -> 20
- LOAD lista automáticamente archivos .csv de ROOT/SD/USB
- puedes escoger CSV con touch o flechas UP/DOWN
- TAB cambia unidad y reescanea CSV
- corrección matemática sin int64:
  división fija x1000 sin overflow
  multiplicación fija x1000 sin overflow normal
  suma/resta con saturación
```

Pruebas objetivo:

```text
12635 / 10 -> 1263.50 con D2
12635 / 6  -> 2105.83 con D2
(A7/6)*6   -> vuelve cerca de 12635
-41*6      -> -246
```
