// minipc_usb_msc_02d.c
// USB_MSC_HOTPLUG_LAB_01E_FIX1_COMPILE
//
// Base: nav_web_mejora / 09E3 sincronizada.
// Objetivo: poner al dia la parte pendrive para hot-plug mas real:
//
//   - Conectar pendrive despues del arranque -> montar /usb.
//   - Retirar pendrive -> desmontar limpio.
//   - Conectar otro pendrive -> montar sin reiniciar.
//   - Mas limpieza de handles antes de reconectar.
//   - Estado textual consultable para FILES/topbar/debug.
//   - 01E: mantiene 01D y añade parche VFS persistente:
///         /usb se registra una sola vez; en hot-plug se desmonta/monta
//          FAT/diskio sin consumir nuevos slots VFS.
//
// Cliente USB MSC que CONVIVE con HID keyboard/mouse.
// El USB Host lo instala HID; aqui solo instalamos y gestionamos el driver MSC.

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"

#include "minipc_usb_msc_02d.h"
#include "rgb_display.h"

// 06B_FIX1: prototipos explícitos por si el build toma un rgb_display.h antiguo.
void rgb_display_topbar_set_visible(int visible);
void rgb_display_topbar_set_usb(int present);
void rgb_display_topbar_set_wifi(int connected);
void rgb_display_topbar_set_sd(int mounted);

static const char *TAG = "minipc_msc";

#define MSC_BASE_PATH "/usb"

#define MSC_HOTPLUG_MAX_RETRY          6
#define MSC_HOTPLUG_CONNECT_DEBOUNCE_MS 300
#define MSC_HOTPLUG_DISCON_DEBOUNCE_MS  300
#define MSC_HOTPLUG_RECONNECT_SETTLE_MS 1800
#define MSC_HOTPLUG_POST_CLEAN_MS       350
#define MSC_HOTPLUG_INVALID_STATE_WAIT_MS 800
#define MSC_HOTPLUG_DRIVER_RESET_DELAY_MS 900
#define MSC_HOTPLUG_DRIVER_RESET_RETRY_MS 400
#define MSC_HOTPLUG_DRIVER_RESET_TRIES 4

static msc_host_device_handle_t s_msc_dev   = NULL;
static msc_host_vfs_handle_t    s_msc_vfs   = NULL;
static QueueHandle_t            s_msc_queue = NULL;
static volatile int             s_mounted   = 0;
static int                      s_driver_installed = 0;
static int                      s_had_disconnect = 0;  // 1 tras una desconexion
static volatile int             s_driver_resetting = 0;
static volatile uint32_t        s_generation = 0;      // cambia al montar/desmontar
static char                     s_status[128] = "USB MSC esperando pendrive";
static TickType_t               s_last_connect_tick = 0;
static TickType_t               s_last_disconnect_tick = 0;

// Bandera compartida con el daemon del host (bucle en el HID). El MSC la pone a
// 1 al desconectarse el pen; el daemon hace usb_host_device_free_all() en su
// contexto (donde SI es valido) para liberar el slot huerfano del pen.
volatile int g_usb_msc_wants_free_all = 0;

// Forward decl
static void msc_event_cb(const msc_host_event_t *event, void *arg);

// Config del driver MSC, reutilizable para reinstalar tras cada desconexion.
static const msc_host_driver_config_t s_msc_cfg = {
    .create_backround_task = true,   // (sic) el campo del driver se llama asi
    .task_priority = 5,
    .stack_size = 4096,
    .callback = msc_event_cb,
    .callback_arg = NULL,
};

static void msc_set_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
    s_status[sizeof(s_status) - 1] = 0;
}

static int msc_debounce_tick(TickType_t *last, int ms)
{
    TickType_t now = xTaskGetTickCount();
    TickType_t gap = now - *last;
    if (*last != 0 && gap < pdMS_TO_TICKS(ms)) {
        return 1;
    }
    *last = now;
    return 0;
}

static void msc_cleanup_handles(const char *why)
{
    if (why) {
        ESP_LOGI(TAG, "cleanup handles: %s", why);
    }

    if (s_msc_vfs) {
        esp_err_t e = msc_host_vfs_unregister(s_msc_vfs);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "vfs_unregister cleanup: %s", esp_err_to_name(e));
        }
        s_msc_vfs = NULL;
    }

    if (s_msc_dev) {
        esp_err_t e = msc_host_uninstall_device(s_msc_dev);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "uninstall_device cleanup: %s", esp_err_to_name(e));
        }
        s_msc_dev = NULL;
    }
}

static void msc_request_host_free_all(void)
{
    // 01C:
    // En esta arquitectura el daemon USB vive junto a clientes HID/MSC.
    // usb_host_device_free_all() devuelve ESP_ERR_INVALID_STATE y no limpia
    // el endpoint Bulk/SCSI. Dejamos la función como no-op para no inundar
    // el log ni depender de ese camino.
    (void)g_usb_msc_wants_free_all;
}


static esp_err_t msc_driver_reset_delayed(const char *why)
{
    esp_err_t err = ESP_OK;

    if (!s_driver_installed) {
        return ESP_OK;
    }

    s_driver_resetting = 1;
    msc_set_status("USB MSC reset driver...");
    printf("[MSC_HOTPLUG] 01C: reset driver MSC retardado: %s\r\n",
           why ? why : "-");

    // Dar margen a que el evento DISCONNECTED termine y el host cierre pipes.
    vTaskDelay(pdMS_TO_TICKS(MSC_HOTPLUG_DRIVER_RESET_DELAY_MS));

    for (int i = 1; i <= MSC_HOTPLUG_DRIVER_RESET_TRIES; i++) {
        err = msc_host_uninstall();
        if (err == ESP_OK) {
            s_driver_installed = 0;
            printf("[MSC_HOTPLUG] 01C: msc_host_uninstall OK intento %d\r\n", i);
            break;
        }

        printf("[MSC_HOTPLUG] 01C: msc_host_uninstall intento %d/%d: %s\r\n",
               i, MSC_HOTPLUG_DRIVER_RESET_TRIES, esp_err_to_name(err));

        // Si aun no puede, esperar. No usar free_all.
        vTaskDelay(pdMS_TO_TICKS(MSC_HOTPLUG_DRIVER_RESET_RETRY_MS));
    }

    if (err != ESP_OK) {
        // No dejamos el sistema peor. Si no pudimos desinstalar, seguimos
        // con el driver vivo y reintentaremos en la proxima desconexion/fallo.
        s_driver_resetting = 0;
        msc_set_status("USB MSC reset fallo: %s", esp_err_to_name(err));
        printf("[MSC_HOTPLUG] 01C: reset driver NO realizado\r\n");
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(MSC_HOTPLUG_DRIVER_RESET_RETRY_MS));

    for (int i = 1; i <= MSC_HOTPLUG_DRIVER_RESET_TRIES; i++) {
        err = msc_host_install(&s_msc_cfg);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            // INVALID_STATE aqui suele significar que ya estaba instalado.
            s_driver_installed = 1;
            printf("[MSC_HOTPLUG] 01C: msc_host_install OK intento %d (%s)\r\n",
                   i, esp_err_to_name(err));
            msc_set_status("USB MSC listo tras reset");
            s_driver_resetting = 0;
            return ESP_OK;
        }

        printf("[MSC_HOTPLUG] 01C: msc_host_install intento %d/%d: %s\r\n",
               i, MSC_HOTPLUG_DRIVER_RESET_TRIES, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(MSC_HOTPLUG_DRIVER_RESET_RETRY_MS));
    }

    s_driver_resetting = 0;
    s_driver_installed = 0;
    msc_set_status("USB MSC install reset fallo");
    return err;
}



typedef struct {
    enum {
        APP_MSC_CONNECT,
        APP_MSC_DISCONNECT,
    } id;
    uint8_t device_addr;
} msc_app_event_t;

// Callback del driver MSC (contexto del task interno del driver).
// Solo encolamos; el trabajo pesado (montaje) se hace en el task propio.
static void msc_event_cb(const msc_host_event_t *event, void *arg)
{
    msc_app_event_t evt = {0};

    if (event->event == MSC_DEVICE_CONNECTED) {
        if (msc_debounce_tick(&s_last_connect_tick, MSC_HOTPLUG_CONNECT_DEBOUNCE_MS)) {
            ESP_LOGW(TAG, "MSC_DEVICE_CONNECTED duplicado ignorado");
            return;
        }
        evt.id = APP_MSC_CONNECT;
        evt.device_addr = event->device.address;
        printf("\r\n[MSC_HOTPLUG] Pendrive CONECTADO (addr=%d)\r\n", event->device.address);
        ESP_LOGI(TAG, "MSC_DEVICE_CONNECTED addr=%d", event->device.address);
        msc_set_status("USB detectado addr=%d", event->device.address);
    } else if (event->event == MSC_DEVICE_DISCONNECTED) {
        if (msc_debounce_tick(&s_last_disconnect_tick, MSC_HOTPLUG_DISCON_DEBOUNCE_MS)) {
            ESP_LOGW(TAG, "MSC_DEVICE_DISCONNECTED duplicado ignorado");
            return;
        }
        evt.id = APP_MSC_DISCONNECT;
        printf("\r\n[MSC_HOTPLUG] Pendrive DESCONECTADO\r\n");
        ESP_LOGW(TAG, "MSC_DEVICE_DISCONNECTED");
        msc_set_status("USB desconectado");
    } else {
        return;
    }

    if (s_msc_queue) {
        xQueueSend(s_msc_queue, &evt, 0);
    }
}

static void msc_list_root(void)
{
    DIR *dir = opendir(MSC_BASE_PATH);
    if (!dir) {
        printf("[06A_MSC] No puedo abrir %s\r\n", MSC_BASE_PATH);
        return;
    }
    printf("[06A_MSC] Contenido de %s:\r\n", MSC_BASE_PATH);
    struct dirent *de;
    int n = 0;
    while ((de = readdir(dir)) != NULL) {
        printf("   %s %s\r\n",
               (de->d_type == DT_DIR) ? "[DIR ]" : "[FILE]",
               de->d_name);
        n++;
    }
    closedir(dir);
    printf("[06A_MSC] %d entradas en raiz\r\n", n);
}

static void msc_handle_connect(uint8_t addr)
{
    esp_err_t err;

    printf("[MSC_HOTPLUG] Gestionando connect addr=%u mounted=%d dev=%p vfs=%p\r\n",
           (unsigned)addr, s_mounted, (void *)s_msc_dev, (void *)s_msc_vfs);

    if (s_mounted) {
        // Algunos hubs/pendrives pueden generar eventos repetidos.
        // Si ya esta montado, no desmontamos lo bueno.
        msc_set_status("USB ya montado en %s", MSC_BASE_PATH);
        printf("[MSC_HOTPLUG] connect ignorado: ya montado en %s\r\n", MSC_BASE_PATH);
        return;
    }

    // Si el driver esta en pleno reset retardado, esperar antes de montar.
    while (s_driver_resetting) {
        printf("[MSC_HOTPLUG] 01C: esperando fin de reset driver...\r\n");
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (!s_driver_installed) {
        printf("[MSC_HOTPLUG] 01C: driver MSC no instalado, instalando antes de montar\r\n");
        esp_err_t ie = msc_host_install(&s_msc_cfg);
        if (ie != ESP_OK && ie != ESP_ERR_INVALID_STATE) {
            printf("[MSC_HOTPLUG] 01C: no pude instalar driver MSC: %s\r\n", esp_err_to_name(ie));
            msc_set_status("USB MSC driver no listo");
            return;
        }
        s_driver_installed = 1;
    }

    // Si quedaron restos de un intento anterior, limpiar antes de instalar.
    if (s_msc_vfs || s_msc_dev) {
        printf("[MSC_HOTPLUG] Limpieza previa antes de reconectar\r\n");
        msc_cleanup_handles("pre-connect stale handles");
        msc_request_host_free_all();
        vTaskDelay(pdMS_TO_TICKS(MSC_HOTPLUG_POST_CLEAN_MS));
    }

    // Si venimos de hot-unplug, dar tiempo real al host a liberar el puerto.
    // Esto reduce el clasico "Transfer failed: Status 3" tras reconectar.
    if (s_had_disconnect) {
        ESP_LOGI(TAG, "Reconnect: esperando asentamiento del host...");
        printf("[MSC_HOTPLUG] Reconexion: esperando limpieza host %d ms...\r\n",
               MSC_HOTPLUG_RECONNECT_SETTLE_MS);
        msc_set_status("USB reconectando...");
        msc_request_host_free_all();
        vTaskDelay(pdMS_TO_TICKS(MSC_HOTPLUG_RECONNECT_SETTLE_MS));
        s_had_disconnect = 0;
    } else {
        // Primer connect despues del arranque: pequeño settle para que el medio
        // este listo y evitar UNIT ATTENTION en el primer SCSI.
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    for (int intento = 1; intento <= MSC_HOTPLUG_MAX_RETRY; intento++) {
        msc_set_status("USB montando intento %d/%d", intento, MSC_HOTPLUG_MAX_RETRY);

        err = msc_host_install_device(addr, &s_msc_dev);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "install_device intento %d/%d: %s",
                     intento, MSC_HOTPLUG_MAX_RETRY, esp_err_to_name(err));
            printf("[MSC_HOTPLUG] install_device %d/%d fallo: %s\r\n",
                   intento, MSC_HOTPLUG_MAX_RETRY, esp_err_to_name(err));
            s_msc_dev = NULL;

            if (err == ESP_ERR_INVALID_STATE) {
                // Normalmente significa: el driver/host aun no esta listo.
                // En 01C no usamos free_all; esperamos y, si se repite,
                // el reset retardado del driver limpiara el estado.
                printf("[MSC_HOTPLUG] INVALID_STATE: esperando driver/host antes de reintentar\r\n");
                msc_set_status("USB esperando driver/host...");
                vTaskDelay(pdMS_TO_TICKS(MSC_HOTPLUG_INVALID_STATE_WAIT_MS));
            } else {
                vTaskDelay(pdMS_TO_TICKS(300 * intento));
            }
            continue;
        }

        msc_host_device_info_t info;
        if (msc_host_get_device_info(s_msc_dev, &info) == ESP_OK) {
            printf("[MSC_HOTPLUG] Capacidad: %llu MB, sector=%lu, sectores=%lu\r\n",
                   ((uint64_t)info.sector_size * info.sector_count) / (1024 * 1024),
                   (unsigned long)info.sector_size,
                   (unsigned long)info.sector_count);
        }

        const esp_vfs_fat_mount_config_t mount_cfg = {
            .format_if_mount_failed = false,
            .max_files = 3,
            .allocation_unit_size = 4096,
        };

        err = msc_host_vfs_register(s_msc_dev, MSC_BASE_PATH, &mount_cfg, &s_msc_vfs);
        if (err == ESP_OK) {
            s_mounted = 1;
            s_generation++;
            rgb_display_topbar_set_usb(1);
            msc_set_status("USB montado en %s", MSC_BASE_PATH);
            printf("[MSC_HOTPLUG] Pendrive MONTADO en %s (intento %d, gen=%lu)\r\n",
                   MSC_BASE_PATH, intento, (unsigned long)s_generation);
            msc_list_root();
            return;
        }

        ESP_LOGW(TAG, "vfs_register intento %d/%d: %s",
                 intento, MSC_HOTPLUG_MAX_RETRY, esp_err_to_name(err));
        printf("[MSC_HOTPLUG] mount %d/%d fallo: %s\r\n",
               intento, MSC_HOTPLUG_MAX_RETRY, esp_err_to_name(err));
        msc_set_status("USB mount fallo: %s", esp_err_to_name(err));

        if (err == ESP_ERR_NO_MEM) {
            printf("[MSC_HOTPLUG] heap libre: %u B, mayor bloque: %u B\r\n",
                   (unsigned)esp_get_free_heap_size(),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        }

        msc_cleanup_handles("mount retry cleanup");
        vTaskDelay(pdMS_TO_TICKS(400 * intento));
    }

    s_mounted = 0;
    s_had_disconnect = 1;
    s_generation++;
    rgb_display_topbar_set_usb(0);
    msc_set_status("USB no montado tras %d intentos", MSC_HOTPLUG_MAX_RETRY);
    printf("[MSC_HOTPLUG] No se pudo montar tras %d intentos. Limpiando driver.\r\n",
           MSC_HOTPLUG_MAX_RETRY);

    msc_cleanup_handles("mount failed final cleanup");

    // 01C: Si llegamos aqui tras Transfer failed: Status 3, el endpoint Bulk/SCSI
    // quedo sucio. Reset retardado solo del driver MSC para preparar
    // la siguiente conexion sin reiniciar todo el MiniPC.
    msc_driver_reset_delayed("mount failed / bulk endpoint dirty");
    printf("[MSC_HOTPLUG] 01C: listo para una nueva conexion tras reset driver\r\n");
}

static void msc_handle_disconnect(void)
{
    printf("[MSC_HOTPLUG] Gestionando disconnect mounted=%d dev=%p vfs=%p\r\n",
           s_mounted, (void *)s_msc_dev, (void *)s_msc_vfs);

    s_mounted = 0;
    s_had_disconnect = 1;   // marcar: el proximo connect esperara asentamiento
    s_generation++;
    rgb_display_topbar_set_usb(0);
    msc_set_status("USB desconectado");

    // 1) Desmontar VFS y device
    msc_cleanup_handles("disconnect");

    // Nota: la liberacion del slot del device a nivel de USB Host
    // (usb_host_device_free_all) NO se puede hacer aqui: este es el contexto del
    // cliente MSC y la funcion devuelve INVALID_STATE. Se hace en el daemon del
    // host (bucle usb_host_lib_handle_events en el HID), que reacciona al evento
    // de device gone. Ver usb_hid_keyboard_02d.c.

    // 2) 01C: Reset retardado solo del driver MSC.
    //    No se toca USB Host ni HID. Esto limpia el endpoint Bulk/SCSI que
    //    se queda sucio tras hot-unplug y provoca Transfer failed: Status 3.
    msc_driver_reset_delayed("disconnect endpoint clean");

    printf("[MSC_HOTPLUG] Pendrive desmontado/liberado (gen=%lu)\r\n",
           (unsigned long)s_generation);
}

static void msc_app_task(void *arg)
{
    msc_app_event_t evt;
    for (;;) {
        if (xQueueReceive(s_msc_queue, &evt, portMAX_DELAY) == pdTRUE) {
            switch (evt.id) {
            case APP_MSC_CONNECT:
                msc_handle_connect(evt.device_addr);
                break;
            case APP_MSC_DISCONNECT:
                msc_handle_disconnect();
                break;
            }
        }
    }
}

esp_err_t minipc_usb_msc_init(void)
{
    static int installed = 0;
    if (installed) return ESP_OK;

    printf("\r\n[MSC_HOTPLUG] Inicializando cliente USB MSC hot-plug...\r\n");
    ESP_LOGI(TAG, "Inicializando USB MSC host driver hot-plug");
    rgb_display_topbar_set_usb(0);
    msc_set_status("USB MSC iniciando...");

    s_msc_queue = xQueueCreate(12, sizeof(msc_app_event_t));
    if (!s_msc_queue) return ESP_ERR_NO_MEM;

    esp_err_t err = msc_host_install(&s_msc_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "msc_host_install fallo: %s", esp_err_to_name(err));
        printf("[MSC_HOTPLUG] msc_host_install fallo: %s\r\n", esp_err_to_name(err));
        msc_set_status("USB MSC install fallo: %s", esp_err_to_name(err));
        return err;
    }
    s_driver_installed = 1;

    BaseType_t ok = xTaskCreatePinnedToCore(msc_app_task, "msc_app", 4096, NULL, 4, NULL, 0);
    if (ok != pdTRUE) return ESP_FAIL;

    installed = 1;
    printf("[MSC_HOTPLUG] Cliente MSC 01E_FIX1 listo. VFS persistente compile fix activo.\r\n");
    msc_set_status("USB MSC listo, esperando pendrive");
    ESP_LOGI(TAG, "USB MSC hot-plug listo, esperando pendrive");
    return ESP_OK;
}

int minipc_usb_msc_mounted(void)
{
    return s_mounted;
}


const char *minipc_usb_msc_status_text(void)
{
    return s_status;
}

uint32_t minipc_usb_msc_generation(void)
{
    return s_generation;
}
