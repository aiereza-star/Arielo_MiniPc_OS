# WAVESHARE_7_MINIPC_BREEZYBOX_LAB_10AO_TBROWSER_HTTPS_TLS_LAB

Escalón 10AO del navegador integrado Arielo MiniPC OS.

Base validada anterior: `10AN_FIX3_OK_HEADER_BUSY_HOURGLASS`.

Objetivo: añadir HTTPS real al TactileBrowser Lexbor GUI, tomando como referencia el camino que ya funcionaba en el navegador antiguo.

## Compilación en carpeta activa del usuario

Copiar el contenido de este ZIP dentro de:

```text
D:\ESP32_IDF_LAB\WAVESHARE_7_MINIPC_BREEZYBOX_LAB_02C_WIFI_APPMAIN
```

Luego:

```powershell
cd D:\ESP32_IDF_LAB\WAVESHARE_7_MINIPC_BREEZYBOX_LAB_02C_WIFI_APPMAIN
idf.py fullclean
idf.py build flash monitor
```

## Prueba rápida

Desde escritorio:

```text
WEB
URL
https://example.com/
ENTER
```

También probar:

```text
https://html.duckduckgo.com/html/
http://neverssl.com/
```

Si compila y navega HTTPS sin reinicios, guardar como:

```text
10AO_OK_TBROWSER_HTTPS_TLS_LAB
```
