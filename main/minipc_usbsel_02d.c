/*
 * minipc_usbsel_02d.c - FIX3 NO_I2C_INSTALL
 *
 * Waveshare ESP32-S3 Touch LCD 7 Rev 1.2
 *
 * Este fix NO instala de nuevo el driver I2C.
 * En esta rama el I2C ya suele estar activo por CH422G/SD.
 *
 * Motivo del fix:
 *   Si llamamos otra vez a i2c_driver_install(), IDF puede imprimir:
 *     E I2C: I2C DRIVER INSTALL ERROR
 *
 * Por eso aquí solo escribimos al CH422G usando el driver existente.
 */

#include "minipc_usbsel_02d.h"

#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "driver/i2c.h"

static const char *TAG = "MINIPC_USBSEL";

#define MINIPC_I2C_PORT       I2C_NUM_0

#define CH422G_WR_SET         0x24
#define CH422G_WR_OC          0x23
#define CH422G_WR_IO          0x38

#define CH422G_CONFIG_WRITE_ENABLE 0x01

// Mapa validado:
// EXIO5 = USB_SEL.
// LOW = ruta USB Host activa.
#define CH422G_USB_SEL_MASK   (1u << 5)

// EXIO4 = SD_CS (activo en bajo). El USBSEL comparte el registro WR_IO del
// CH422G con el driver de la SD. Si el USBSEL deja este bit a 1, la SD queda
// DESELECCIONADA y, aunque este montada, no responde a lecturas/escrituras.
// Por eso, en el estado final dejamos SD_CS BAJO (bit a 0) para no pisar la SD.
#define CH422G_SD_CS_MASK     (1u << 4)

static esp_err_t ch422g_write_addr(uint8_t addr7, uint8_t data)
{
    return i2c_master_write_to_device(
        MINIPC_I2C_PORT,
        addr7,
        &data,
        1,
        pdMS_TO_TICKS(100)
    );
}

static esp_err_t ch422g_write_io(uint8_t io_state)
{
    esp_err_t ret;

    ret = ch422g_write_addr(CH422G_WR_SET, CH422G_CONFIG_WRITE_ENABLE);
    if (ret != ESP_OK) {
        printf("[02D_USBSEL] ERROR WR_SET 0x24: %s\r\n", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(2));

    // Mantener salidas OC en estado compatible con lo usado en SD.
    ret = ch422g_write_addr(CH422G_WR_OC, 0x0F);
    if (ret != ESP_OK) {
        printf("[02D_USBSEL] ERROR WR_OC 0x23: %s\r\n", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(2));

    ret = ch422g_write_addr(CH422G_WR_IO, io_state);
    if (ret != ESP_OK) {
        printf("[02D_USBSEL] ERROR WR_IO 0x38: %s\r\n", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

esp_err_t minipc_usbsel_force_host_mode(void)
{
    // EXIO5/USB_SEL bajo (ruta USB Host) y EXIO4/SD_CS bajo (SD seleccionable).
    // Antes se forzaba 0xFF y SD_CS quedaba alto -> la SD montaba pero no se
    // podia leer/escribir. Ahora preservamos SD_CS bajo.
    uint8_t io_state = 0xFF;
    io_state &= (uint8_t)~CH422G_USB_SEL_MASK;  // EXIO5 = 0
    io_state &= (uint8_t)~CH422G_SD_CS_MASK;    // EXIO4 = 0 (SD seleccionada)

    esp_err_t ret = ch422g_write_io(io_state);
    if (ret != ESP_OK) {
        printf("[02D_USBSEL] ERROR al forzar USB Host: %s\r\n", esp_err_to_name(ret));
        ESP_LOGE(TAG, "USBSEL force host fallo: %s", esp_err_to_name(ret));
        return ret;
    }

    printf("[02D_USBSEL] USB_SEL/EXIO5 LOW + SD_CS LOW, WR_IO=0x%02X\r\n", io_state);
    ESP_LOGI(TAG, "USB_SEL/EXIO5 LOW + SD_CS LOW, WR_IO=0x%02X", io_state);

    vTaskDelay(pdMS_TO_TICKS(800));
    return ESP_OK;
}

esp_err_t minipc_usbsel_cycle_host_mode(void)
{
    // HIGH temporal: limpieza de ruta/mux.
    uint8_t high_state = 0xFF;

    esp_err_t ret = ch422g_write_io(high_state);
    if (ret != ESP_OK) {
        printf("[02D_USBSEL] ERROR ciclo HIGH: %s\r\n", esp_err_to_name(ret));
        return ret;
    }

    printf("[02D_USBSEL] ciclo USB_SEL HIGH, WR_IO=0x%02X\r\n", high_state);
    vTaskDelay(pdMS_TO_TICKS(250));

    // LOW definitivo: USB Host + SD_CS bajo (SD seleccionable).
    uint8_t low_state = 0xFF;
    low_state &= (uint8_t)~CH422G_USB_SEL_MASK;  // EXIO5 = 0
    low_state &= (uint8_t)~CH422G_SD_CS_MASK;    // EXIO4 = 0 (SD seleccionada)

    ret = ch422g_write_io(low_state);
    if (ret != ESP_OK) {
        printf("[02D_USBSEL] ERROR ciclo LOW: %s\r\n", esp_err_to_name(ret));
        return ret;
    }

    printf("[02D_USBSEL] ciclo USB_SEL LOW + SD_CS LOW, WR_IO=0x%02X\r\n", low_state);
    ESP_LOGI(TAG, "USB_SEL cycle HIGH->LOW OK + SD_CS LOW, WR_IO=0x%02X", low_state);

    vTaskDelay(pdMS_TO_TICKS(800));
    return ESP_OK;
}
