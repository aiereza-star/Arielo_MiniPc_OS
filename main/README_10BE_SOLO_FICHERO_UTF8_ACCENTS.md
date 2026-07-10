# 10BE - UTF8 accents / caracteres especiales

Base: 10BD_OK_HTML10D_WIKI_LAYOUT_READABLE

Solo toca:
- main/cmd_tbbrowser_gui_10ah.c

Cambios:
- Etiqueta visual a 10BE.
- HTML10D conserva bytes UTF-8 en texto visible.
- Convierte entidades HTML numericas `&#...;` y `&#x...;` a UTF-8.
- Convierte entidades latinas frecuentes: á, é, í, ó, ú, ñ, ü, ç, Á, Ñ, etc.
- Añade entidades comunes: &ndash;, &mdash;, &hellip;, &laquo;, &raquo;, &euro;, etc.
- No toca WiFi.
- No toca HTTP/HTTPS.
- No toca Lexbor.
- No toca el SO base.

Pruebas sugeridas:
- `wiki: casas`
- `wiki: españa`
- `wiki: sabiñánigo`
- páginas con acentos y caracteres especiales.
