# Arielo MiniPC OS - TactileBrowser 10AM

## Prueba recomendada

```powershell
cd D:\ESP32_IDF_LAB\WAVESHARE_7_MINIPC_BREEZYBOX_LAB_10AM_TBROWSER_CLASSIC_HISTORY_FORWARD
idf.py fullclean
idf.py build flash monitor
```

Luego:

```text
desktop
WEB
```

Pruebas:

```text
URL -> neverssl.com/ -> ENTER
Abrir un enlace
<    vuelve atras
>    avanza otra vez
HIST abre about:history
HOME vuelve a about:home
RLD recarga
EXIT sale
```

Si `BACK`, `FORWARD`, `HIST`, `URL`, `HOME`, `RLD` y `EXIT` funcionan sin reinicio, salvar como:

```text
10AM_OK_TBROWSER_CLASSIC_HISTORY_FORWARD
```
