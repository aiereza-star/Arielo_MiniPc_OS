Arielo MiniPC OS - 10AD_TBROWSER_HEADER_BACK_HISTORY

Base: 10AC_TBROWSER_LOCAL_LINKS_NAV validada.

Objetivo 10AD:
- Mantener TactileBrowser interno con Lexbor integrado en firmware normal.
- Mantener navegacion local por enlaces numerados.
- Mejorar cabecera estilo navegador: titulo, ruta, estado, lineas, scroll, links y contador BACK.
- Anadir historial basico BACK con tecla b.
- Mejorar rutas relativas con soporte basico para ./ y ../.
- Conservar tbbrowser10ac como fallback de la rama 10AC.

Comandos:
- tbtest
- tbrender
- tbview
- tbbrowser       -> nueva 10AD
- tbbrowser10ac   -> fallback 10AC

Pruebas sugeridas:
1) tbtest
2) tbtest parse
3) tbbrowser file /sdcard/test_links.html
4) Abrir enlace por numero.
5) Pulsar b para volver atras.
6) Pulsar r para recargar.
7) Pulsar q para salir.

Controles tbbrowser:
- n o ENTER : bajar pagina
- p         : subir pagina
- j/k       : bajar/subir linea
- l         : listar enlaces
- numero    : abrir enlace local
- o RUTA    : abrir ruta manual
- r         : recargar
- b         : volver atras
- g / G     : inicio / final
- h o ?     : ayuda
- q         : salir
