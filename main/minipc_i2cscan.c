// minipc_i2cscan.c
// Diagnostico I2C para bring-up del touch GT911.
// Reutiliza el bus I2C ya inicializado por la SD (GPIO8=SDA, GPIO9=SCL,
// I2C_NUM_0, 100 kHz). NO reinstala el driver I2C: solo lee.
//
// Comandos:
//   i2cscan     -> lista todas las direcciones que responden (ACK)
//   touchprobe  -> prueba el GT911 en 0x5D y 0x14, lee Product ID

#include <stdio.h>
#include <string.h>
#include "driver/i2c.h"
#include "esp_log.h"

#include "minipc_i2cscan.h"
#include "minipc_touch_gt911.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SCAN_I2C_PORT   I2C_NUM_0
#define SCAN_TIMEOUT_MS 50

// GT911: registro Product ID en 0x8140 (4 bytes ASCII: "911\0")
#define GT911_REG_PRODUCT_ID  0x8140

static bool i2c_addr_acks(uint8_t addr7)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr7 << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(SCAN_I2C_PORT, cmd,
                                         pdMS_TO_TICKS(SCAN_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return (ret == ESP_OK);
}

int cmd_i2cscan(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("\r\n[I2CSCAN] Escaneando bus I2C (SDA=8, SCL=9)...\r\n");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");

    int found = 0;
    for (int hi = 0; hi < 8; hi++) {
        printf("%02x: ", hi << 4);
        for (int lo = 0; lo < 16; lo++) {
            uint8_t addr = (hi << 4) | lo;
            if (addr < 0x08 || addr > 0x77) {
                printf("   ");
                continue;
            }
            if (i2c_addr_acks(addr)) {
                printf("%02x ", addr);
                found++;
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }

    printf("[I2CSCAN] %d dispositivos encontrados.\r\n", found);
    printf("[I2CSCAN] Esperado: 24 (CH422G) y 5d o 14 (GT911 touch).\r\n");
    return 0;
}

// Lee 'len' bytes de un registro de 16 bits del GT911.
static esp_err_t gt911_read_reg(uint8_t addr7, uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t reg_bytes[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };

    return i2c_master_write_read_device(
        SCAN_I2C_PORT,
        addr7,
        reg_bytes, 2,
        buf, len,
        pdMS_TO_TICKS(SCAN_TIMEOUT_MS)
    );
}

static void gt911_try_addr(uint8_t addr7)
{
    printf("[TOUCHPROBE] Probando GT911 en 0x%02X...\r\n", addr7);

    if (!i2c_addr_acks(addr7)) {
        printf("   sin ACK en 0x%02X\r\n", addr7);
        return;
    }
    printf("   ACK OK en 0x%02X\r\n", addr7);

    uint8_t id[4] = {0};
    esp_err_t e = gt911_read_reg(addr7, GT911_REG_PRODUCT_ID, id, 4);
    if (e != ESP_OK) {
        printf("   ACK pero no pude leer Product ID: %s\r\n", esp_err_to_name(e));
        return;
    }

    printf("   Product ID bytes: %02X %02X %02X %02X  (\"%c%c%c%c\")\r\n",
           id[0], id[1], id[2], id[3],
           (id[0] >= 32 && id[0] < 127) ? id[0] : '.',
           (id[1] >= 32 && id[1] < 127) ? id[1] : '.',
           (id[2] >= 32 && id[2] < 127) ? id[2] : '.',
           (id[3] >= 32 && id[3] < 127) ? id[3] : '.');

    if (id[0] == '9' && id[1] == '1' && id[2] == '1') {
        printf("   >>> GT911 CONFIRMADO en 0x%02X <<<\r\n", addr7);
    } else {
        printf("   Responde pero el ID no es '911'. Otro chip?\r\n");
    }
}

int cmd_touchprobe(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("\r\n[TOUCHPROBE] Buscando GT911 en el bus I2C...\r\n");
    printf("[TOUCHPROBE] Nota: si TP_RST no se ha secuenciado, puede que aun\r\n");
    printf("             no responda. Esto solo comprueba si ya esta vivo.\r\n");

    gt911_try_addr(0x5D);
    gt911_try_addr(0x14);

    printf("[TOUCHPROBE] Fin.\r\n");
    return 0;
}

int cmd_touchtest(int argc, char **argv)
{
    (void)argc; (void)argv;

    if (!minipc_touch_ok()) {
        printf("[TOUCHTEST] El touch no se inicializo OK. Revisa 'touchprobe'.\r\n");
        return 1;
    }

    printf("\r\n[TOUCHTEST] Toca la pantalla. ~10s, Ctrl-C para cortar.\r\n");
    int last_print = 0;
    for (int i = 0; i < 1000; i++) {     // 1000 * 10ms = 10s
        int16_t x = 0, y = 0;
        if (minipc_touch_read(&x, &y)) {
            printf("[TOUCHTEST] TOQUE x=%d y=%d (de 800x480)\r\n", x, y);
            last_print = i;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        (void)last_print;
    }
    printf("[TOUCHTEST] Fin.\r\n");
    return 0;
}
