# 10AL_FIX3_HEADER_URL_ONE_ENTER

- Mantiene la barra URL integrada en la cabecera.
- Drena CR/LF sobrantes tras pulsar ENTER en la barra URL para evitar que el visor interprete el ENTER de búsqueda como PageDown.
- Añade pantalla gráfica de “Descargando página...” mientras se realiza HTTP GET.
- Objetivo: escribir URL + ENTER debe cargar y mostrar la página sin necesitar otro ENTER para refrescar/avanzar.
