# 10AE_TBROWSER_BOOKMARKS_FAVORITES

Base: 10AD_FIX1_HISTORY_MEMMOVE.

Cambios:
- Nuevo comando principal tbbrowser apunta a 10AE.
- tbbrowser10ad queda como fallback de la 10AD validada.
- tbbrowser10ac se mantiene como fallback de la 10AC.
- Favoritos persistentes en /root/tbbrowser_favs.txt.
- Tecla `m`: guardar pagina actual como favorito.
- Tecla `v`: listar favoritos y abrir por numero.
- Nuevo arranque: `tbbrowser bookmarks`, `tbbrowser favs` o `tbbrowser favoritos`.
- Se mantiene Lexbor integrado PSRAM-first, cabecera, enlaces locales, recarga e historial BACK.

Archivos de prueba:
- test_pages/test_10ae_index.html
- test_pages/test_10ae_page2.html

## 10AE_FIX1_BOOKMARKS_SAFE_COPY
- Corrige error de compilación `-Werror=format-truncation` en `tbb10ae_bookmarks_choose()`.
- Sustituye copias tipo `snprintf(dst, cap, "%s", src)` por `tbb10ae_safe_copy()`.
- Añade `tbb10ae_safe_append()` para construir rutas sin avisos de truncamiento.
- No cambia funcionalidad del navegador; mantiene favoritos, historial BACK, enlaces, recarga y salida.
