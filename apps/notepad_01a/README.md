# Arielo MiniPC OS - Notepad 01K FONT CAPSLOCK FIX

Versión basada en:

```text
NOTEPAD_01J_CURSOR_UNTITLED_SAVE
```

## Corrige letras minúsculas feas

La 01I/01J generaba minúsculas a partir de mayúsculas desplazadas. En pantalla quedaban poco legibles.

En 01K:

```text
se sustituyen por glifos minúsculos 5x7 reales
se mantiene la fuente clara de la calculadora
las mayúsculas siguen igual de legibles
```

## Añade CapsLock fijo

Se define:

```c
#define BT_KEY_CAPSLOCK 0x39
```

Funcionamiento:

```text
CapsLock     activa/desactiva mayúsculas fijas
Shift        invierte temporalmente
CAP          aparece en pantalla cuando CapsLock está activo
```

## Mantiene

```text
cursor corregido
NEW sin nombre
SAVE pregunta nombre
buffer 16 KB
selector OPEN
ROOT / SD / USB
guardar / guardar como
chasis estable de Calculator 01H
```

## Compilar

```bat
buildelf.bat
```

Copia `notepad.xtensa.elf` a `/root/bin`, SD o USB y lánzalo desde APPS Launcher.
