# 10AO - TactileBrowser HTTPS TLS LAB

Base: `10AN_FIX3_OK_HEADER_BUSY_HOURGLASS`.

## Cambios

- Activa la apertura de URLs `https://` en el navegador Lexbor GUI.
- Mantiene `http://` y la navegación clásica validada:
  - barra superior clásica
  - historial `<` y `>`
  - HIST
  - FAV / +FAV
  - FILE
  - LINK
  - indicador `WAIT` en cabecera durante cargas
- Usa el camino ya probado por el navegador antiguo:
  - `esp_http_client`
  - `HTTP_TRANSPORT_OVER_SSL` para HTTPS
  - timeout ampliado por TLS
  - redirecciones automáticas hasta 5 saltos
- Mantiene buffer HTTP en PSRAM-first y límite de 64 KiB.

## Notas

- HTTPS aquí es el primer escalón LAB dentro del navegador Lexbor nuevo.
- El proyecto tiene en `sdkconfig` soporte TLS/MBEDTLS y modo de verificación flexible de laboratorio.
- Muchas webs modernas cargan HTML, pero dependen de JavaScript/CSS/imágenes; este navegador muestra el HTML textual procesado por Lexbor.

## Pruebas sugeridas

- `https://example.com/`
- `https://html.duckduckgo.com/html/`
- `http://neverssl.com/`

