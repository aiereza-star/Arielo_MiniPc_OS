# 10AQ_TBROWSER_DDG_RESULTS_POLISH

Base: `10AP_OK_TBROWSER_DDG_HTML_SEARCH`.

Cambios:

- Mantiene HTTP/HTTPS y DuckDuckGo HTML por HTTPS.
- Limpieza ligera de resultados DuckDuckGo HTML:
  - elimina líneas vacías, duplicados consecutivos y controles/ruido habituales.
  - cambia el título interno de resultados DDG a `DuckDuckGo HTML`.
- Mejora de la ventana `LINK`:
  - muestra el texto visible del enlace cuando existe.
  - conserva el número `[1]..[9]` para abrir rápido.
  - si un enlace de DuckDuckGo trae `uddg=`, muestra también la URL real decodificada.
- Apertura directa de resultados DDG:
  - al tocar/abrir un enlace `/l/?uddg=...`, se decodifica y se abre la URL destino real.
- Mantiene barra clásica, historial `<` / `>`, `HIST`, `FAV`, `+FAV`, `FILE`, `LINK` y `WAIT` en cabecera.

Pruebas sugeridas:

1. `WEB`.
2. `URL` -> `ddg: esp32 s3 waveshare` -> ENTER.
3. Abrir `LINK` y revisar que se vean enlaces más útiles.
4. Pulsar/tocar un resultado y comprobar que abre el destino real.
5. Probar `<`, `>`, `HIST`, `RLD`, `HOME`.
