#include "minipc_console_02c.h"

#include <stdio.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"

static const char *TAG = "MINIPC_CONSOLE";

esp_err_t minipc_console_stdio_uart0_init(void)
{
#if CONFIG_ESP_CONSOLE_UART_NUM < 0
    ESP_LOGE(TAG, "CONFIG_ESP_CONSOLE_UART_NUM < 0; consola UART no configurada");
    return ESP_FAIL;
#else
    const int uart_num = CONFIG_ESP_CONSOLE_UART_NUM;

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config fallo: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(uart_num, 2048, 0, 0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install fallo: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_vfs_dev_uart_use_driver(uart_num);

    ESP_LOGI(TAG, "STDIO conectado a UART%d @ %d", uart_num, CONFIG_ESP_CONSOLE_UART_BAUDRATE);
    printf("[02C] STDIO UART%d OK\n", uart_num);

    return ESP_OK;
#endif
}
