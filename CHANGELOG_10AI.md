# 10AI - TactileBrowser GUI Touch Buttons

Base: `10AH_FIX2_GUI_KEEP_MODE_ISR_SAFE` validada.

Objetivo del escalón:

- Mantener Lexbor integrado en firmware normal y el navegador GUI estable.
- No tocar el motor HTML, favoritos, historial ni rutas.
- Añadir interacción táctil básica del navegador gráfico usando GT911.

Cambios principales:

- `tbbgui` conserva el modo gráfico SM_400X240 durante toda la sesión como en 10AH_FIX2.
- Nueva barra inferior táctil:
  - `BACK` -> historial atrás (`b`)
  - `HOME` -> `about:home` (`H`)
  - `RELD` -> recargar (`r`)
  - `FAV` -> guardar favorito (`m`)
  - `LINK` -> mostrar/listar enlaces (`l`)
  - `HELP` -> ayuda (`h`)
  - `EXIT` -> salir (`q`)
- Toque en zona de contenido:
  - parte superior: página arriba
  - parte inferior: página abajo
  - línea visible `[n]`: abre ese enlace
- Toque sobre overlay de enlaces: abre la fila tocada.
- Se mantiene el teclado como fallback completo.

Prueba recomendada:

1. `desktop`
2. Pulsar botón `WEB`.
3. En `about:home`, tocar enlaces visibles o botón `LINK`.
4. Probar `BACK`, `HOME`, `RELD`, `FAV`, `HELP`, `EXIT` desde la barra inferior.
5. Confirmar que no hay reinicios y que `q`/`EXIT` vuelve correctamente.
