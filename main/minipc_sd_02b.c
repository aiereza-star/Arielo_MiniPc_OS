// 06B_TOPBAR: sincroniza estado real microSD con barra superior.
#include "minipc_sd_02b.h"
#include "rgb_display.h"

// 06B_FIX1: prototipos explícitos por si el build toma un rgb_display.h antiguo.
void rgb_display_topbar_set_visible(int visible);
void rgb_display_topbar_set_usb(int present);
void rgb_display_topbar_set_wifi(int connected);
void rgb_display_topbar_set_sd(int mounted);

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"

#include "sdmmc_cmd.h"

// ============================================================================
// MINIPC 02B - SD CARD STORAGE / Waveshare ESP32-S3 Touch LCD 7 Rev 1.2
// /root   = LittleFS interno
// /sdcard = microSD externa por SPI
// SD SPI: MOSI GPIO11, CLK GPIO12, MISO GPIO13, CS CH422G EXIO4
// ============================================================================

static const char *TAG = "MINIPC_SD";

#define MINIPC_I2C_PORT       I2C_NUM_0
#define MINIPC_I2C_SDA        8
#define MINIPC_I2C_SCL        9
#define MINIPC_I2C_FREQ_HZ    100000

#define CH422G_WR_SET         0x24
#define CH422G_WR_OC          0x23
#define CH422G_WR_IO          0x38

// EXIO4 = SD_CS. Activo en bajo.
#define CH422G_SD_CS_MASK     (1u << 4)

#define MINIPC_SD_MOSI        11
#define MINIPC_SD_CLK         12
#define MINIPC_SD_MISO        13
#define MINIPC_SD_HOST        SPI2_HOST
#define MINIPC_SD_MOUNT       "/sdcard"

#ifndef SDSPI_SLOT_NO_CS
#define SDSPI_SLOT_NO_CS      GPIO_NUM_NC
#endif

static uint8_t g_ch422g_io_state = 0xFF;
static sdmmc_card_t *g_sd_card = NULL;
static bool g_sd_mounted = false;

bool minipc_sd_is_mounted(void)
{
    return g_sd_mounted;
}

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

static esp_err_t minipc_ch422g_init_for_sd(void)
{
    i2c_config_t conf = {0};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = MINIPC_I2C_SDA;
    conf.scl_io_num = MINIPC_I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = MINIPC_I2C_FREQ_HZ;

    esp_err_t ret = i2c_param_config(MINIPC_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config fallo: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(MINIPC_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install fallo: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = ch422g_write_addr(CH422G_WR_SET, 0x01);
    if (ret != ESP_OK) return ret;

    ret = ch422g_write_addr(CH422G_WR_OC, 0x0F);
    if (ret != ESP_OK) return ret;

    g_ch422g_io_state = 0xFF;
    ret = ch422g_write_addr(CH422G_WR_IO, g_ch422g_io_state);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "CH422G OK, SD_CS en EXIO4");
    return ESP_OK;
}

static esp_err_t minipc_sd_cs(bool selected)
{
    if (selected) {
        g_ch422g_io_state &= (uint8_t)~CH422G_SD_CS_MASK;  // CS bajo
    } else {
        g_ch422g_io_state |= CH422G_SD_CS_MASK;            // CS alto
    }

    return ch422g_write_addr(CH422G_WR_IO, g_ch422g_io_state);
}

static void minipc_sd_dummy_clocks(void)
{
    spi_device_handle_t tmp = NULL;

    spi_device_interface_config_t devcfg = {0};
    devcfg.clock_speed_hz = 400000;
    devcfg.mode = 0;
    devcfg.spics_io_num = GPIO_NUM_NC;
    devcfg.queue_size = 1;

    if (spi_bus_add_device(MINIPC_SD_HOST, &devcfg, &tmp) != ESP_OK) {
        ESP_LOGW(TAG, "No pude crear dispositivo SPI temporal para dummy clocks");
        return;
    }

    uint8_t tx[16];
    memset(tx, 0xFF, sizeof(tx));

    spi_transaction_t t = {0};
    t.length = sizeof(tx) * 8;
    t.tx_buffer = tx;

    esp_err_t ret = spi_device_transmit(tmp, &t);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "dummy clocks fallo: %s", esp_err_to_name(ret));
    }

    spi_bus_remove_device(tmp);
}

esp_err_t minipc_sd_init(void)
{
    if (g_sd_mounted) {
        ESP_LOGI(TAG, "SD ya estaba montada");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando microSD 02B...");
    rgb_display_topbar_set_sd(0);
    printf("\n[02B] Inicializando SD...\n");

    esp_err_t ret = minipc_ch422g_init_for_sd();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CH422G no inicializa: %s", esp_err_to_name(ret));
        printf("[02B] SD ERROR: CH422G %s\n", esp_err_to_name(ret));
        return ret;
    }

    minipc_sd_cs(false);
    vTaskDelay(pdMS_TO_TICKS(30));

    spi_bus_config_t bus_cfg = {0};
    bus_cfg.mosi_io_num = MINIPC_SD_MOSI;
    bus_cfg.miso_io_num = MINIPC_SD_MISO;
    bus_cfg.sclk_io_num = MINIPC_SD_CLK;
    bus_cfg.quadwp_io_num = GPIO_NUM_NC;
    bus_cfg.quadhd_io_num = GPIO_NUM_NC;
    bus_cfg.max_transfer_sz = 4096;

    ret = spi_bus_initialize(MINIPC_SD_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize fallo: %s", esp_err_to_name(ret));
        printf("[02B] SD ERROR: SPI bus %s\n", esp_err_to_name(ret));
        return ret;
    }

    minipc_sd_dummy_clocks();

    minipc_sd_cs(true);
    vTaskDelay(pdMS_TO_TICKS(50));

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = MINIPC_SD_HOST;
    host.max_freq_khz = 400;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = MINIPC_SD_HOST;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Montando en %s...", MINIPC_SD_MOUNT);

    ret = esp_vfs_fat_sdspi_mount(
        MINIPC_SD_MOUNT,
        &host,
        &slot_config,
        &mount_config,
        &g_sd_card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No monta SD: %s", esp_err_to_name(ret));
        printf("[02B] SD ERROR: no monta (%s)\n", esp_err_to_name(ret));
        minipc_sd_cs(false);
        return ret;
    }

    g_sd_mounted = true;
    rgb_display_topbar_set_sd(1);

    uint64_t size_mb = 0;
    if (g_sd_card) {
        size_mb = ((uint64_t)g_sd_card->csd.capacity) *
                  g_sd_card->csd.sector_size /
                  (1024ULL * 1024ULL);
    }

    ESP_LOGI(TAG, "SD OK montada en %s, size=%" PRIu64 " MB",
             MINIPC_SD_MOUNT, size_mb);

    printf("[02B] SDCARD montada en %s\n", MINIPC_SD_MOUNT);
    printf("[02B] SDCARD size=%" PRIu64 " MB\n", size_mb);

    mkdir("/sdcard/profe", 0775);
    mkdir("/sdcard/logs", 0775);

    FILE *f = fopen("/sdcard/logs/minipc_02b_boot.txt", "a");
    if (f) {
        fprintf(f, "MINIPC 02B SD OK\n");
        fclose(f);
        printf("[02B] Test escritura SD OK: /sdcard/logs/minipc_02b_boot.txt\n");
    } else {
        printf("[02B] Aviso: no pude escribir log de prueba en SD\n");
    }

    return ESP_OK;
}
