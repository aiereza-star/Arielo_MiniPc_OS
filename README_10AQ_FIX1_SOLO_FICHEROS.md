10AQ_FIX1_SOLO_FICHEROS

Solo contiene los ficheros afectados para copiar encima de la carpeta activa:
D:\ESP32_IDF_LAB\WAVESHARE_7_MINIPC_BREEZYBOX_LAB_02C_WIFI_APPMAIN

Ficheros incluidos:
- main/cmd_tbbrowser_gui_10ah.c

Correccion:
- Anade el prototipo adelantado de tbb10al_is_http_url() antes de que lo use tbb10aq_extract_uddg_url().
- Evita implicit declaration y conflicting types con -Werror.
