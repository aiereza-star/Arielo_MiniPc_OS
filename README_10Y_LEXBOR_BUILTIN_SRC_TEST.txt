Arielo MiniPC OS - 10Y_LEXBOR_BUILTIN_SRC_TEST
================================================

Esta copia NO toca la base buena. Es una prueba para aislar el problema de
TactileBrowser/Lexbor.

Diferencia frente a la 10X anterior:
- 10X usaba una libreria Lexbor precompilada tomada de la app ELF.
- 10Y usa el repo completo fuente TactileBrowser_FULL y compila Lexbor como
  componente normal ESP-IDF dentro del firmware.

Comandos despues de flashear:

    tbtest
    tbtest parse
    tbtest loop 50

Lectura:

1) Si tbtest/create NO reinicia:
   Lexbor puede inicializarse dentro del firmware normal.

2) Si tbtest parse NO reinicia:
   Lexbor puede crear y parsear HTML dentro del firmware normal.
   Entonces el fallo anterior era muy probablemente por usar Lexbor desde ELF externo.

3) Si tbtest reinicia incluso aqui:
   El problema no es solo el ELF loader: hay que revisar configuracion Lexbor/ESP-IDF/memoria.

Compilacion recomendada:

    cd D:\ESP32_IDF_LAB\WAVESHARE_7_MINIPC_BREEZYBOX_LAB_10Y_LEXBOR_BUILTIN_SRC_TEST
    idf.py fullclean
    idf.py build flash monitor

