# WAVESHARE_7_MINIPC_BREEZYBOX_LAB_10AI_TBROWSER_GUI_TOUCH_BUTTONS

Primer escalón táctil del TactileBrowser gráfico interno.

Parte de la base validada `10AH_FIX2`, donde el navegador gráfico ya tenía:

- Lexbor integrado en firmware con memoria PSRAM-first.
- Home interno `about:home`.
- Ayuda interna `about:help`.
- Favoritos internos `about:bookmarks`.
- Modo gráfico estable sin reinicio RGB al navegar varias veces.

## Nuevo en 10AI

La barra inferior del navegador GUI ahora funciona como botones táctiles:

| Botón | Acción |
|---|---|
| BACK | volver atrás |
| HOME | abrir `about:home` |
| RELD | recargar |
| FAV | guardar favorito |
| LINK | mostrar/ocultar lista de enlaces |
| HELP | mostrar ayuda |
| EXIT | salir |

También se puede tocar el área de contenido:

- Toque arriba: página arriba.
- Toque abajo: página abajo.
- Toque sobre una línea `[1]`, `[2]`, etc.: abre el enlace.

El teclado sigue funcionando igual que antes.
