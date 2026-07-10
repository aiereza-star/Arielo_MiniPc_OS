# 10AH_FIX1 - Prototype order fix

- Corrige error de compilación por declaración implícita de `tbb10ah_chomp()` en `cmd_tbbrowser_gui_10ah.c`.
- Se adelanta el prototipo antes de `tbb10ah_make_bookmarks_html()`.
- No cambia funcionalidad del navegador: `about:home`, `about:help`, `about:bookmarks`, botón WEB -> `tbbgui home`.
