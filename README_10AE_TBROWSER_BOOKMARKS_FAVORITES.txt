Arielo MiniPC OS - 10AE_TBROWSER_BOOKMARKS_FAVORITES

Base: 10AD_FIX1_HISTORY_MEMMOVE validada.

Objetivo:
- Mantener tbbrowser 10AD con cabecera, enlaces, historial BACK y rutas relativas.
- Anadir favoritos/marcadores locales persistentes.
- Guardar favoritos en /root/tbbrowser_favs.txt.
- Tecla m: guardar pagina actual.
- Tecla v: ver favoritos y abrir uno por numero.
- Comando tbbrowser bookmarks / favs / favoritos: abrir navegador desde favoritos.

Comandos:
- tbtest
- tbtest parse
- tbbrowser file /sdcard/test_10ae_index.html
- tbbrowser bookmarks
- tbbrowser10ad  (fallback de la 10AD validada)
- tbbrowser10ac  (fallback de la 10AC validada)

Controles dentro de tbbrowser:
- n / ENTER: pagina abajo
- p: pagina arriba
- j / k: linea abajo/arriba
- 1..N: abrir enlace local
- l: listar enlaces
- b: atras en historial
- r: recargar
- m: guardar pagina actual como favorito
- v: ver/abrir favoritos
- o RUTA: abrir ruta manual
- h / ?: ayuda
- q: salir

Prueba recomendada:
1) Copiar test_10ae_index.html y test_10ae_page2.html a /sdcard.
2) Ejecutar: tbbrowser file /sdcard/test_10ae_index.html
3) Pulsar m para guardar favorito.
4) Pulsar 1 para abrir page2.
5) Pulsar m para guardar page2.
6) Pulsar v y abrir el favorito [1] o [2].
7) Probar b, r, l, q.

Notas:
- Los favoritos son texto plano: /root/tbbrowser_favs.txt
- Formato: ruta|titulo
- No se guarda sample como favorito; use HTML real en SD/USB/root.
