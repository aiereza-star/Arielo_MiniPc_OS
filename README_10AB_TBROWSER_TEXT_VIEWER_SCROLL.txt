Arielo MiniPC OS - 10AB_TBROWSER_TEXT_VIEWER_SCROLL
====================================================

Base: 10AA_TACTILEBROWSER_BUILTIN_RENDER_TEST, ya validada por Arielo.

Objetivo de esta rama:
- Mantener Lexbor integrado dentro del firmware normal, NO como ELF externo.
- Conservar tbtest y tbrender de 10Z/10AA.
- Añadir primer visor TactileBrowser interno con scroll basico por teclado.

Nuevo comando:

    tbview
    tbview sample
    tbview file /sdcard/test.html
    tbview /sdcard/test.html
    tbview file /usb/test.html

Controles dentro del visor:

    n o ENTER  bajar una pagina
    p          subir una pagina
    j          bajar una linea
    k          subir una linea
    g          ir al inicio
    G          ir al final
    h o ?      ayuda
    q          salir

Alcance prudente 10AB:
- HTML basico.
- h1/h2/h3/p/li/br.
- Sin CSS.
- Sin imagenes.
- Sin URL parser.
- Sin ELF externo.

Pruebas recomendadas:

    tbtest
    tbtest parse
    tbrender file /sdcard/test.html
    tbview file /sdcard/test.html

Si tbview muestra el documento y permite n/p/j/k/q sin reiniciar, la 10AB queda como primera base de visor HTML interno con scroll.
