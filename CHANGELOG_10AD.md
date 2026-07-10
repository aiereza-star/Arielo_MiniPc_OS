# 10AD_TBROWSER_HEADER_BACK_HISTORY

- Basado en 10AC validada.
- Nuevo `cmd_tbbrowser_lexbor_10ad.c`.
- `tbbrowser` apunta ahora a 10AD.
- `tbbrowser10ac` conserva la version anterior como fallback.
- Cabecera mejorada con titulo, ruta, lineas, scroll, numero de enlaces y profundidad de historial.
- Historial BACK con tecla `b`.
- Normalizacion basica de rutas locales absolutas/relativas con `.` y `..`.
- Se mantiene Lexbor integrado en firmware normal con memoria PSRAM-first.

## 10AD_FIX1_HISTORY_MEMMOVE

- Corrige error de compilacion `-Werror=restrict` en `tbb10ad_history_push()`.
- Se sustituye el desplazamiento fila a fila con `snprintf(hist[i-1], ..., hist[i])` por un `memmove()` del bloque de historial.
- No cambia funcionalidad: mantiene cabecera, historial BACK, rutas relativas y comandos de 10AD.
