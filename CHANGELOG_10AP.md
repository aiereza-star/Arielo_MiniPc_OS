# 10AP_TBROWSER_DDG_HTML_SEARCH

Base: 10AO_TBROWSER_HTTPS_TLS_LAB validada con HTTPS.

Cambios:
- Añadido buscador integrado tipo navegador antiguo.
- La barra URL acepta texto normal y lo convierte a búsqueda DuckDuckGo HTML sin JavaScript.
- Soporta atajos:
  - `ddg: texto`
  - `buscar texto`
  - `?texto`
  - texto con espacios, por ejemplo `esp32 s3 waveshare`
- Si se escribe `duckduckgo`, `duckduckgo.com` o `https://duckduckgo.com/`, abre `https://html.duckduckgo.com/html/`.
- Para dominios sin esquema se usa `https://` automático.
- Para sitios HTTP puros, escribir explícitamente `http://...`.

Objetivo:
- Recuperar el comportamiento práctico del navegador viejo como buscador,
  evitando la página moderna de DuckDuckGo que pide JavaScript.
