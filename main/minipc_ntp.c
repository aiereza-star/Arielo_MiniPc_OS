// minipc_ntp.c
// NTP_FAST_SYNC_01B
//
// Basado en NTP_LATE_WIFI_FIX_01A.
//
// Problema observado:
//   - SNTP se inicia pronto cuando WiFi recibe IP.
//   - Pero a veces la fecha/hora tarda casi 2 minutos en aparecer.
//   - Causa probable: el primer intento SNTP/DNS no sincroniza y lwIP tarda
//     demasiado en reintentar por su cuenta.
//
// 01B:
//   - Mantiene el worker vivo que espera WiFi/IP.
//   - Inicia SNTP en cuanto hay WiFi.
//   - Si no sincroniza en pocos segundos, para/reinicia SNTP y reintenta.
//   - Reintenta cada NTP_FAST_RETRY_MS hasta que la hora sea válida.
//   - No bloquea arranque, USB, navegador ni apps.
//   - Mantiene TZ Europe/Madrid.
//
// Objetivo practico:
//   - Pasar de "puede tardar casi 2 minutos" a normalmente pocos segundos,
//     dependiendo de DNS/red/NTP.

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_sntp.h"

#include "minipc_ntp.h"
#include "minipc_wifi_02c.h"

static const char *TAG = "minipc_ntp";

#define NTP_FAST_RETRY_MS        8000u
#define NTP_WORKER_FAST_MS       1000u
#define NTP_WORKER_IDLE_MS       60000u

static volatile bool s_synced = false;
static volatile bool s_task_started = false;
static volatile bool s_sntp_started = false;
static volatile int  s_start_attempts = 0;
static volatile uint32_t s_sntp_start_ms = 0;

// Meses abreviados en espanol (3 letras)
static const char *MESES_ES[12] = {
    "ene", "feb", "mar", "abr", "may", "jun",
    "jul", "ago", "sep", "oct", "nov", "dic"
};

static uint32_t ntp_millis(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static uint32_t ntp_elapsed_ms(uint32_t start_ms)
{
    return (uint32_t)(ntp_millis() - start_ms);
}

static void ntp_apply_timezone(void)
{
    // Zona horaria Espana peninsular: CET en invierno (UTC+1), CEST en verano
    // (UTC+2). Regla POSIX: ultimo domingo de marzo y de octubre.
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

static bool ntp_time_looks_valid(void)
{
    time_t now = 0;
    struct tm tm_info;

    time(&now);
    localtime_r(&now, &tm_info);

    return (tm_info.tm_year + 1900) >= 2024;
}

// Callback cuando SNTP sincroniza la hora.
static void on_time_sync(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "Hora sincronizada por NTP");
    printf("[NTP] Hora sincronizada\n");
}

static void ntp_stop_sntp_safe(void)
{
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    s_sntp_started = false;
    s_sntp_start_ms = 0;
}

static void ntp_start_sntp_fresh(const char *reason)
{
    ntp_apply_timezone();

    if (s_synced || ntp_time_looks_valid()) {
        s_synced = true;
        return;
    }

    // Reinicio limpio para no depender del temporizador interno de lwIP/SNTP.
    if (esp_sntp_enabled()) {
        esp_sntp_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    s_start_attempts++;

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

    // Tres servidores. pool primero suele ir bien; Google/Cloudflare ayudan
    // si el pool o DNS tarda en responder.
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");

    sntp_set_time_sync_notification_cb(on_time_sync);
    esp_sntp_init();

    s_sntp_started = true;
    s_sntp_start_ms = ntp_millis();

    ESP_LOGI(TAG, "SNTP iniciado/reiniciado intento=%d motivo=%s TZ=Europe/Madrid",
             s_start_attempts, reason ? reason : "-");

    printf("[NTP_FAST] intento=%d motivo=%s WiFi IP=%s\n",
           s_start_attempts,
           reason ? reason : "-",
           minipc_wifi_get_ip());
}

static void ntp_worker_task(void *arg)
{
    (void)arg;

    ntp_apply_timezone();

    printf("[NTP_FAST] Servicio NTP vivo; esperando WiFi/IP...\n");

    for (;;) {
        if (!s_synced && ntp_time_looks_valid()) {
            s_synced = true;
            ESP_LOGI(TAG, "Hora valida detectada");
            printf("[NTP_FAST] Hora valida detectada\n");
        }

        if (!s_synced) {
            if (minipc_wifi_is_connected()) {
                if (!s_sntp_started || !esp_sntp_enabled()) {
                    ntp_start_sntp_fresh("wifi_ready");
                } else if (s_sntp_start_ms != 0 &&
                           ntp_elapsed_ms(s_sntp_start_ms) >= NTP_FAST_RETRY_MS) {
                    printf("[NTP_FAST] sin sync en %u ms, reintento SNTP\n",
                           (unsigned)ntp_elapsed_ms(s_sntp_start_ms));
                    ntp_start_sntp_fresh("retry_timeout");
                }
            } else {
                if (s_sntp_started || esp_sntp_enabled()) {
                    ESP_LOGW(TAG, "WiFi desconectada, paro SNTP");
                    printf("[NTP_FAST] WiFi desconectada, SNTP stop\n");
                    ntp_stop_sntp_safe();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(s_synced ? NTP_WORKER_IDLE_MS : NTP_WORKER_FAST_MS));
    }
}

void minipc_ntp_start(void)
{
    ntp_apply_timezone();

    if (s_task_started) {
        ESP_LOGI(TAG, "NTP worker ya estaba activo");
        return;
    }

    s_task_started = true;

    BaseType_t ok = xTaskCreatePinnedToCore(
        ntp_worker_task,
        "minipc_ntp",
        4096,
        NULL,
        4,
        NULL,
        0
    );

    if (ok != pdPASS) {
        s_task_started = false;
        ESP_LOGE(TAG, "No se pudo crear task NTP");
        printf("[NTP_FAST] ERROR creando task NTP\n");
        return;
    }

    ESP_LOGI(TAG, "NTP_FAST worker arrancado");
}

bool minipc_ntp_synced(void)
{
    if (!s_synced && ntp_time_looks_valid()) {
        s_synced = true;
    }
    return s_synced;
}

void minipc_ntp_format(char *out, int out_size)
{
    if (!out || out_size < 18) return;

    if (!minipc_ntp_synced()) {
        snprintf(out, out_size, "--/---/---- --:--");
        return;
    }

    time_t now = 0;
    struct tm tm_info;
    time(&now);
    localtime_r(&now, &tm_info);

    int mes = tm_info.tm_mon;   // 0..11
    if (mes < 0 || mes > 11) mes = 0;

    // "dd/mmm/aaaa hh:mm"
    snprintf(out, out_size, "%02d/%s/%04d %02d:%02d",
             tm_info.tm_mday,
             MESES_ES[mes],
             tm_info.tm_year + 1900,
             tm_info.tm_hour,
             tm_info.tm_min);
}
