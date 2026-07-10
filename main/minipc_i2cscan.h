#pragma once
#ifdef __cplusplus
extern "C" {
#endif

// Comando de consola: escanea el bus I2C y lista direcciones que responden.
int cmd_i2cscan(int argc, char **argv);

// Comando de consola: prueba el GT911 en 0x5D y 0x14, leyendo su Product ID.
int cmd_touchprobe(int argc, char **argv);

// Comando de consola: lee puntos del touch en bucle ~10s e imprime coordenadas.
int cmd_touchtest(int argc, char **argv);

#ifdef __cplusplus
}
#endif
