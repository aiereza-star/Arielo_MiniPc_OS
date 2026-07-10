// minipc_touch_gt911.c
// Driver GT911 para Waveshare ESP32-S3-Touch-LCD-7 (Rev 1.2).
// Portado del sketch validado WAVESHARE_7_SD_TEST_01G.
//
// Claves de la placa (ya validadas en pruebas anteriores):
//   - I2C: GPIO8=SDA, GPIO9=SCL, I2C_NUM_0 (el mismo que usa la SD).
//   - GT911 en direccion 0x14 (reset simple, sin tocar INT).
//   - TP_RST = CH422G EXIO1.
//   - status reg 0x814E, punto1 0x8150.
//
// IMPORTANTE: el CH422G es compartido (SD_CS=EXIO4, USB_SEL=EXIO5). Para no
// pisar esos bits al manejar EXIO1, leemos el estado actual del CH422G por su
// registro de lectura (0x26), modificamos SOLO el bit EXIO1 y reescribimos.

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_log.h"

#include "minipc_touch_gt911.h"

static const char *TAG = "minipc_touch";

#define TOUCH_I2C_PORT    I2C_NUM_0
#define TOUCH_TIMEOUT_MS  50

// CH422G
#define CH422G_ADDR_WR_SET 0x24
#define CH422G_ADDR_WR_IO  0x38
#define CH422G_ADDR_RD_IO  0x26
#define CH422G_WRITE_ENABLE 0x01
#define EXIO_TP_RST_BIT    (1u << 1)   // EXIO1

// GT911
// La direccion depende del nivel de INT durante el reset y en esta placa
// varia entre arranques (a veces 0x5D, a veces 0x14). Por eso NO la fijamos:
// la autodetectamos en init probando ambas y guardamos la que responda.
#define GT911_ADDR_PRIMARY   0x5D
#define GT911_ADDR_ALT       0x14
#define GT911_STATUS_REG  0x814E
#define GT911_POINT1_REG  0x8150

static uint8_t s_gt911_addr = GT911_ADDR_PRIMARY;  // se ajusta en init

// Pantalla fisica
#define TOUCH_W 800
#define TOUCH_H 480

static int s_touch_ok = 0;

// ---- CH422G: escribir un EXIO preservando los demas bits ----
static esp_err_t ch422g_write_raw(uint8_t io_state)
{
    // Habilitar escritura
    uint8_t we = CH422G_WRITE_ENABLE;
    esp_err_t e = i2c_master_write_to_device(TOUCH_I2C_PORT, CH422G_ADDR_WR_SET,
                                             &we, 1, pdMS_TO_TICKS(TOUCH_TIMEOUT_MS));
    if (e != ESP_OK) return e;

    return i2c_master_write_to_device(TOUCH_I2C_PORT, CH422G_ADDR_WR_IO,
                                      &io_state, 1, pdMS_TO_TICKS(TOUCH_TIMEOUT_MS));
}

static esp_err_t ch422g_set_tp_rst(int high)
{
    // Leer estado actual del CH422G para no pisar SD_CS / USB_SEL.
    uint8_t cur = 0xFF;
    esp_err_t e = i2c_master_read_from_device(TOUCH_I2C_PORT, CH422G_ADDR_RD_IO,
                                              &cur, 1, pdMS_TO_TICKS(TOUCH_TIMEOUT_MS));
    if (e != ESP_OK) {
        // Si la lectura falla, partimos de un estado seguro conocido:
        // todo alto excepto SD_CS (EXIO4) y USB_SEL (EXIO5) que deben quedar bajos.
        cur = 0xFF & ~((1u << 4) | (1u << 5));
    }

    if (high) cur |= EXIO_TP_RST_BIT;       // EXIO1 alto
    else      cur &= (uint8_t)~EXIO_TP_RST_BIT;  // EXIO1 bajo

    return ch422g_write_raw(cur);
}

// ---- GT911 I2C ----
static esp_err_t gt911_read_reg(uint16_t reg, uint8_t *buf, size_t len)
{
    uint8_t r[2] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF) };
    return i2c_master_write_read_device(TOUCH_I2C_PORT, s_gt911_addr,
                                        r, 2, buf, len,
                                        pdMS_TO_TICKS(TOUCH_TIMEOUT_MS));
}

static esp_err_t gt911_write_reg8(uint16_t reg, uint8_t val)
{
    uint8_t b[3] = { (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), val };
    return i2c_master_write_to_device(TOUCH_I2C_PORT, s_gt911_addr,
                                      b, 3, pdMS_TO_TICKS(TOUCH_TIMEOUT_MS));
}

// Comprueba si el GT911 responde en 'addr' leyendo su status reg.
static bool gt911_responds_at(uint8_t addr)
{
    uint8_t r[2] = { (uint8_t)(GT911_STATUS_REG >> 8),
                     (uint8_t)(GT911_STATUS_REG & 0xFF) };
    uint8_t st = 0;
    return i2c_master_write_read_device(TOUCH_I2C_PORT, addr,
                                        r, 2, &st, 1,
                                        pdMS_TO_TICKS(TOUCH_TIMEOUT_MS)) == ESP_OK;
}

esp_err_t minipc_touch_init(void)
{
    printf("\r\n[08A_TOUCH] Inicializando GT911 (0x14)...\r\n");
    ESP_LOGI(TAG, "Init GT911");

    // Reset suave por TP_RST (EXIO1): LOW 20ms -> HIGH 200ms.
    ch422g_set_tp_rst(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    ch422g_set_tp_rst(1);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Autodeteccion de direccion: esta placa cae a veces en 0x5D, a veces 0x14
    // (depende del INT durante el reset). Probamos ambas y nos quedamos con la
    // que responda.
    if (gt911_responds_at(GT911_ADDR_PRIMARY)) {
        s_gt911_addr = GT911_ADDR_PRIMARY;
    } else if (gt911_responds_at(GT911_ADDR_ALT)) {
        s_gt911_addr = GT911_ADDR_ALT;
    } else {
        printf("[08A_TOUCH] GT911 NO responde ni en 0x5D ni en 0x14\r\n");
        ESP_LOGW(TAG, "GT911 sin respuesta en ninguna direccion");
        s_touch_ok = 0;
        return ESP_ERR_NOT_FOUND;
    }
    printf("[08A_TOUCH] GT911 detectado en 0x%02X\r\n", s_gt911_addr);

    uint8_t st = 0;
    esp_err_t e = gt911_read_reg(GT911_STATUS_REG, &st, 1);
    if (e != ESP_OK) {
        printf("[08A_TOUCH] GT911 fallo al leer status: %s\r\n", esp_err_to_name(e));
        ESP_LOGW(TAG, "GT911 status read fallo: %s", esp_err_to_name(e));
        s_touch_ok = 0;
        return e;
    }

    gt911_write_reg8(GT911_STATUS_REG, 0x00);  // limpiar flag
    s_touch_ok = 1;
    printf("[08A_TOUCH] GT911 OK en 0x%02X (status=0x%02X). Touch activo.\r\n",
           s_gt911_addr, st);
    ESP_LOGI(TAG, "GT911 OK addr=0x%02X status=0x%02X", s_gt911_addr, st);
    return ESP_OK;
}

bool minipc_touch_read(int16_t *x, int16_t *y)
{
    if (!s_touch_ok) return false;

    uint8_t st = 0;
    if (gt911_read_reg(GT911_STATUS_REG, &st, 1) != ESP_OK) return false;

    if (!(st & 0x80)) return false;       // bit buffer-ready

    uint8_t count = st & 0x0F;
    if (count == 0) {
        gt911_write_reg8(GT911_STATUS_REG, 0x00);
        return false;
    }

    uint8_t p[8] = {0};
    if (gt911_read_reg(GT911_POINT1_REG, p, sizeof(p)) != ESP_OK) {
        gt911_write_reg8(GT911_STATUS_REG, 0x00);
        return false;
    }
    gt911_write_reg8(GT911_STATUS_REG, 0x00);  // limpiar SIEMPRE tras leer

    uint16_t tx = ((uint16_t)p[1] << 8) | p[0];
    uint16_t ty = ((uint16_t)p[3] << 8) | p[2];

    if (tx >= TOUCH_W) tx = TOUCH_W - 1;
    if (ty >= TOUCH_H) ty = TOUCH_H - 1;

    if (x) *x = (int16_t)tx;
    if (y) *y = (int16_t)ty;
    return true;
}

int minipc_touch_ok(void)
{
    return s_touch_ok;
}
