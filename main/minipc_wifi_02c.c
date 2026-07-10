// 06B_TOPBAR: sincroniza estado real WiFi con barra superior.
#include "minipc_wifi_02c.h"
#include "rgb_display.h"

// 06B_FIX1: prototipos explícitos por si el build toma un rgb_display.h antiguo.
void rgb_display_topbar_set_visible(int visible);
void rgb_display_topbar_set_usb(int present);
void rgb_display_topbar_set_wifi(int connected);
void rgb_display_topbar_set_sd(int mounted);

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"

// Valores por defecto (solo se usan en el PRIMER arranque, si NVS esta vacio).
// A partir de ahi mandan las credenciales guardadas en NVS, editables desde la
// pantalla WIFI del escritorio. Asi el WiFi no esta anclado al codigo.
#define MINIPC_WIFI_SSID       "MiFibra-4ED3-24G"
#define MINIPC_WIFI_PASS       "CubanitoS2017"
#define MINIPC_WIFI_TIMEOUT_MS 15000

#define WIFI_NVS_NAMESPACE  "minipc_wifi"
#define WIFI_NVS_KEY_SSID   "ssid"
#define WIFI_NVS_KEY_PASS   "pass"

// Credenciales activas (cargadas de NVS o por defecto)
static char g_active_ssid[33] = MINIPC_WIFI_SSID;
static char g_active_pass[65] = MINIPC_WIFI_PASS;

// Carga SSID/pass de NVS. Si no hay, deja los valores por defecto.
static void wifi_load_credentials(void)
{
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return;   // no hay namespace aun: usar defaults
    }
    size_t len;
    len = sizeof(g_active_ssid);
    nvs_get_str(h, WIFI_NVS_KEY_SSID, g_active_ssid, &len);
    len = sizeof(g_active_pass);
    nvs_get_str(h, WIFI_NVS_KEY_PASS, g_active_pass, &len);
    nvs_close(h);
}

// Guarda SSID/pass nuevos en NVS. Devuelve 0 si OK.
int minipc_wifi_save_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !pass) return -1;
    nvs_handle_t h;
    if (nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return -1;
    nvs_set_str(h, WIFI_NVS_KEY_SSID, ssid);
    nvs_set_str(h, WIFI_NVS_KEY_PASS, pass);
    esp_err_t e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) return -1;
    // Actualizar credenciales activas en memoria
    snprintf(g_active_ssid, sizeof(g_active_ssid), "%s", ssid);
    snprintf(g_active_pass, sizeof(g_active_pass), "%s", pass);
    return 0;
}

const char *minipc_wifi_get_ssid(void)
{
    return g_active_ssid;
}

static const char *TAG = "MINIPC_WIFI";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t g_wifi_event_group = NULL;
static esp_netif_t *g_wifi_netif = NULL;
static bool g_wifi_connected = false;
static char g_wifi_ip[16] = "0.0.0.0";
static int g_retry_count = 0;
static const int WIFI_MAX_RETRY = 8;

static bool g_wifi_started_once = false;

bool minipc_wifi_is_connected(void)
{
    return g_wifi_connected;
}

const char *minipc_wifi_get_ip(void)
{
    return g_wifi_ip;
}

static esp_err_t minipc_nvs_init_safe(void)
{
    esp_err_t ret = nvs_flash_init();

    // En esta rama NVS puede haber sido inicializada ya desde app_main.
    // Lo tratamos como OK para no romper la secuencia de arranque.
    if (ret == ESP_ERR_INVALID_STATE) {
        return ESP_OK;
    }

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS necesita erase: %s", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init fallo: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void minipc_wifi_event_handler(void *arg,
                                      esp_event_base_t event_base,
                                      int32_t event_id,
                                      void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA_START -> connect");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        g_wifi_connected = false;
        snprintf(g_wifi_ip, sizeof(g_wifi_ip), "0.0.0.0");
        rgb_display_topbar_set_wifi(0);

        if (g_retry_count < WIFI_MAX_RETRY) {
            g_retry_count++;
            ESP_LOGW(TAG, "WiFi desconectada, reintento %d/%d", g_retry_count, WIFI_MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "WiFi fallo tras %d reintentos", WIFI_MAX_RETRY);
            if (g_wifi_event_group) {
                xEventGroupSetBits(g_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(g_wifi_ip, sizeof(g_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));

        g_retry_count = 0;
        g_wifi_connected = true;
        rgb_display_topbar_set_wifi(1);

        ESP_LOGI(TAG, "WiFi conectada, IP=%s", g_wifi_ip);
        printf("[02E_WIFI] WIFI conectada IP=%s\n", g_wifi_ip);

        if (g_wifi_event_group) {
            xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);
        }
    }
}

esp_err_t minipc_wifi_init_appmain(void)
{
    if (g_wifi_connected) {
        ESP_LOGI(TAG, "WiFi ya conectada IP=%s", g_wifi_ip);
        return ESP_OK;
    }

    if (g_wifi_started_once) {
        ESP_LOGW(TAG, "WiFi ya fue iniciada; no se reinicia");
        return ESP_OK;
    }
    g_wifi_started_once = true;

    printf("\n[02E_WIFI] Inicializando WiFi app_main...\n");
    ESP_LOGI(TAG, "Inicializando WiFi app_main");

    esp_err_t ret = minipc_nvs_init_safe();
    if (ret != ESP_OK) {
        printf("[02E_WIFI] WIFI ERROR: NVS %s\n", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_netif_init fallo: %s", esp_err_to_name(ret));
        printf("[02E_WIFI] WIFI ERROR: netif %s\n", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default fallo: %s", esp_err_to_name(ret));
        printf("[02E_WIFI] WIFI ERROR: event loop %s\n", esp_err_to_name(ret));
        return ret;
    }

    if (!g_wifi_event_group) {
        g_wifi_event_group = xEventGroupCreate();
        if (!g_wifi_event_group) {
            ESP_LOGE(TAG, "No se pudo crear EventGroup WiFi");
            printf("[02E_WIFI] WIFI ERROR: EventGroup\n");
            return ESP_ERR_NO_MEM;
        }
    }

    if (!g_wifi_netif) {
        g_wifi_netif = esp_netif_create_default_wifi_sta();
        if (!g_wifi_netif) {
            ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta fallo");
            printf("[02E_WIFI] WIFI ERROR: create STA netif\n");
            return ESP_FAIL;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init fallo: %s", esp_err_to_name(ret));
        printf("[02E_WIFI] WIFI ERROR: init %s\n", esp_err_to_name(ret));
        return ret;
    }

    static bool handlers_registered = false;
    if (!handlers_registered) {
        esp_event_handler_instance_t instance_any_id;
        esp_event_handler_instance_t instance_got_ip;

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &minipc_wifi_event_handler,
            NULL,
            &instance_any_id
        ));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &minipc_wifi_event_handler,
            NULL,
            &instance_got_ip
        ));

        handlers_registered = true;
    }

    // Cargar credenciales de NVS (o usar defaults si es el primer arranque)
    wifi_load_credentials();

    wifi_config_t wifi_config = {0};
    // Copia acotada (los campos sta.ssid/password son de tamaño fijo). Usamos
    // memcpy con strnlen para evitar el aviso de truncation de snprintf.
    {
        size_t n;
        n = strnlen(g_active_ssid, sizeof(wifi_config.sta.ssid) - 1);
        memcpy(wifi_config.sta.ssid, g_active_ssid, n);
        wifi_config.sta.ssid[n] = '\0';
        n = strnlen(g_active_pass, sizeof(wifi_config.sta.password) - 1);
        memcpy(wifi_config.sta.password, g_active_pass, n);
        wifi_config.sta.password[n] = '\0';
    }

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    g_retry_count = 0;
    g_wifi_connected = false;
    snprintf(g_wifi_ip, sizeof(g_wifi_ip), "0.0.0.0");
    rgb_display_topbar_set_wifi(0);

    ESP_LOGI(TAG, "Conectando a SSID='%s'...", g_active_ssid);
    printf("[02E_WIFI] WIFI conectando a %s...\n", g_active_ssid);

    ret = esp_wifi_start();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_start fallo: %s", esp_err_to_name(ret));
        printf("[02E_WIFI] WIFI ERROR: start %s\n", esp_err_to_name(ret));
        return ret;
    }

    EventBits_t bits = xEventGroupWaitBits(
        g_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(MINIPC_WIFI_TIMEOUT_MS)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi OK IP=%s", g_wifi_ip);
        printf("[02E_WIFI] WIFI OK IP=%s\n", g_wifi_ip);
        return ESP_OK;
    }

    if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "WiFi FAIL");
        printf("[02E_WIFI] WIFI FAIL\n");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "WiFi TIMEOUT, se deja reintentando en segundo plano");
    printf("[02E_WIFI] WIFI TIMEOUT, sigue reintentando...\n");
    return ESP_ERR_TIMEOUT;
}

typedef struct {
    uint32_t delay_ms;
} minipc_wifi_async_arg_t;

static void minipc_wifi_async_task(void *pv)
{
    minipc_wifi_async_arg_t arg = {0};

    if (pv) {
        arg = *(minipc_wifi_async_arg_t *)pv;
        free(pv);
    }

    if (arg.delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(arg.delay_ms));
    }

    printf("\n[02E_WIFI] WiFi async task arrancando...\n");
    minipc_wifi_init_appmain();

    vTaskDelete(NULL);
}

// Reconexion EN CALIENTE con credenciales nuevas (sin reiniciar).
// Aplica ssid/pass, intenta conectar, y espera hasta timeout. Devuelve:
//   0 = conectado OK (y guarda en NVS)
//  -1 = fallo de conexion (credenciales aplicadas pero no conecta)
// El WiFi ya debe estar inicializado (esp_wifi_start ya llamado al arrancar).
int minipc_wifi_reconnect_with(const char *ssid, const char *pass)
{
    if (!ssid || !pass || ssid[0] == '\0') return -1;

    ESP_LOGI(TAG, "Reconexion en caliente a SSID='%s'", ssid);
    printf("[02E_WIFI] Reconectando a '%s'...\n", ssid);

    // Desconectar de la red actual
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    // Aplicar nuevas credenciales (copia acotada para evitar truncation warning)
    wifi_config_t wifi_config = {0};
    {
        size_t n;
        n = strnlen(ssid, sizeof(wifi_config.sta.ssid) - 1);
        memcpy(wifi_config.sta.ssid, ssid, n);
        wifi_config.sta.ssid[n] = '\0';
        n = strnlen(pass, sizeof(wifi_config.sta.password) - 1);
        memcpy(wifi_config.sta.password, pass, n);
        wifi_config.sta.password[n] = '\0';
    }
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK) {
        return -1;
    }

    // Limpiar estado y reconectar
    g_retry_count = 0;
    g_wifi_connected = false;
    snprintf(g_wifi_ip, sizeof(g_wifi_ip), "0.0.0.0");
    rgb_display_topbar_set_wifi(0);
    if (g_wifi_event_group) {
        xEventGroupClearBits(g_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }

    esp_wifi_connect();

    // Esperar resultado (timeout mas corto para no congelar la UI demasiado)
    EventBits_t bits = xEventGroupWaitBits(
        g_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE,
        pdMS_TO_TICKS(12000)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        // Conecto: guardar credenciales en NVS para el proximo arranque
        minipc_wifi_save_credentials(ssid, pass);
        ESP_LOGI(TAG, "Reconexion OK, credenciales guardadas");
        printf("[02E_WIFI] Reconexion OK a '%s', guardada\n", ssid);
        return 0;
    }

    ESP_LOGW(TAG, "Reconexion fallida a '%s'", ssid);
    printf("[02E_WIFI] Reconexion FALLIDA a '%s'\n", ssid);
    return -1;
}

esp_err_t minipc_wifi_start_async(uint32_t delay_ms)
{
    minipc_wifi_async_arg_t *arg = calloc(1, sizeof(minipc_wifi_async_arg_t));
    if (!arg) {
        return ESP_ERR_NO_MEM;
    }

    arg->delay_ms = delay_ms;

    BaseType_t ok = xTaskCreatePinnedToCore(
        minipc_wifi_async_task,
        "minipc_wifi",
        8192,
        arg,
        5,
        NULL,
        0
    );

    if (ok != pdPASS) {
        free(arg);
        return ESP_FAIL;
    }

    printf("[02E_WIFI] WiFi programada async delay=%" PRIu32 " ms\n", delay_ms);
    return ESP_OK;
}
