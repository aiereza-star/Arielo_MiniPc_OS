/*
 * usb_enum_debug_02d.c - diagnóstico bruto USB Host para ESP32-S3
 *
 * Uso:
 *   usb_enum_debug_init();
 *
 * Enchufar pendrive/receptor/teclado USB.
 * Si el bus funciona deben salir:
 *   [02D_ENUM] NEW_DEV addr=...
 *   VID:PID ...
 *   Interface ...
 *   Endpoint ...
 */

#include "usb_enum_debug_02d.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"

#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"

static const char *TAG = "usb_enum";

static usb_host_client_handle_t s_client_hdl = NULL;
static TaskHandle_t s_daemon_task_hdl = NULL;
static TaskHandle_t s_client_task_hdl = NULL;
static volatile int s_host_ready = 0;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
} __attribute__((packed)) desc_hdr_t;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static const char *class_name(uint8_t cls)
{
    switch (cls) {
    case 0x00: return "per-interface";
    case 0x01: return "audio";
    case 0x02: return "cdc";
    case 0x03: return "HID";
    case 0x05: return "physical";
    case 0x06: return "image";
    case 0x07: return "printer";
    case 0x08: return "mass-storage";
    case 0x09: return "hub";
    case 0x0A: return "cdc-data";
    case 0x0B: return "smart-card";
    case 0x0D: return "content-security";
    case 0x0E: return "video";
    case 0x0F: return "personal-healthcare";
    case 0x10: return "audio-video";
    case 0xDC: return "diagnostic";
    case 0xE0: return "wireless";
    case 0xEF: return "misc";
    case 0xFE: return "app-specific";
    case 0xFF: return "vendor";
    default:   return "?";
    }
}

static void dump_config_descriptor(const usb_config_desc_t *cfg)
{
    if (!cfg) {
        printf("[02D_ENUM] Sin config descriptor\r\n");
        return;
    }

    const uint8_t *p = (const uint8_t *)cfg;
    int total = cfg->wTotalLength;

    printf("[02D_ENUM] Config: totalLen=%d interfaces=%u configValue=%u attrs=0x%02X maxPower=%umA\r\n",
           total,
           cfg->bNumInterfaces,
           cfg->bConfigurationValue,
           cfg->bmAttributes,
           cfg->bMaxPower * 2);

    int off = cfg->bLength;
    while (off + 2 <= total) {
        const desc_hdr_t *h = (const desc_hdr_t *)(p + off);

        if (h->bLength == 0) {
            printf("[02D_ENUM] Descriptor con bLength=0 en off=%d, paro parseo\r\n", off);
            break;
        }

        if (off + h->bLength > total) {
            printf("[02D_ENUM] Descriptor fuera de rango off=%d len=%u total=%d\r\n",
                   off, h->bLength, total);
            break;
        }

        const uint8_t *d = p + off;

        if (h->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE && h->bLength >= 9) {
            uint8_t ifnum = d[2];
            uint8_t alt = d[3];
            uint8_t eps = d[4];
            uint8_t cls = d[5];
            uint8_t sub = d[6];
            uint8_t proto = d[7];

            printf("[02D_ENUM] Interface %u alt=%u eps=%u class=0x%02X(%s) sub=0x%02X proto=0x%02X",
                   ifnum, alt, eps, cls, class_name(cls), sub, proto);

            if (cls == 0x03) {
                if (sub == 0x01 && proto == 0x01) {
                    printf("  <-- HID KEYBOARD BOOT");
                } else if (sub == 0x01 && proto == 0x02) {
                    printf("  <-- HID MOUSE BOOT");
                } else {
                    printf("  <-- HID");
                }
            } else if (cls == 0x08) {
                printf("  <-- PENDRIVE/MSC");
            }

            printf("\r\n");

        } else if (h->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT && h->bLength >= 7) {
            uint8_t addr = d[2];
            uint8_t attr = d[3];
            uint16_t mps = rd16(&d[4]);
            uint8_t interval = d[6];

            const char *dir = (addr & 0x80) ? "IN" : "OUT";
            const char *type = "?";
            switch (attr & 0x03) {
            case 0: type = "CTRL"; break;
            case 1: type = "ISO"; break;
            case 2: type = "BULK"; break;
            case 3: type = "INT"; break;
            }

            printf("[02D_ENUM]   EP 0x%02X %s %s mps=%u interval=%u\r\n",
                   addr, dir, type, mps, interval);
        }

        off += h->bLength;
    }
}

static void inspect_device(uint8_t dev_addr)
{
    usb_device_handle_t dev_hdl = NULL;

    esp_err_t err = usb_host_device_open(s_client_hdl, dev_addr, &dev_hdl);
    if (err != ESP_OK) {
        printf("[02D_ENUM] device_open addr=%u fallo: %s\r\n", dev_addr, esp_err_to_name(err));
        return;
    }

    const usb_device_desc_t *dev_desc = NULL;
    err = usb_host_get_device_descriptor(dev_hdl, &dev_desc);
    if (err == ESP_OK && dev_desc) {
        printf("\r\n[02D_ENUM] NEW_DEV addr=%u\r\n", dev_addr);
        printf("[02D_ENUM] VID:PID %04X:%04X USB=%x.%02x class=0x%02X(%s) sub=0x%02X proto=0x%02X mps0=%u configs=%u\r\n",
               dev_desc->idVendor,
               dev_desc->idProduct,
               dev_desc->bcdUSB >> 8,
               dev_desc->bcdUSB & 0xFF,
               dev_desc->bDeviceClass,
               class_name(dev_desc->bDeviceClass),
               dev_desc->bDeviceSubClass,
               dev_desc->bDeviceProtocol,
               dev_desc->bMaxPacketSize0,
               dev_desc->bNumConfigurations);
    } else {
        printf("[02D_ENUM] get_device_descriptor fallo: %s\r\n", esp_err_to_name(err));
    }

    const usb_config_desc_t *cfg = NULL;
    err = usb_host_get_active_config_descriptor(dev_hdl, &cfg);
    if (err == ESP_OK && cfg) {
        dump_config_descriptor(cfg);
    } else {
        printf("[02D_ENUM] get_active_config_descriptor fallo: %s\r\n", esp_err_to_name(err));
    }

    usb_host_device_close(s_client_hdl, dev_hdl);
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        printf("\r\n[02D_ENUM] EVENT NEW_DEV addr=%u\r\n", event_msg->new_dev.address);
        inspect_device(event_msg->new_dev.address);
        break;

    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        printf("\r\n[02D_ENUM] EVENT DEV_GONE\r\n");
        break;

    default:
        printf("\r\n[02D_ENUM] EVENT desconocido=%d\r\n", event_msg->event);
        break;
    }
}

static void usb_daemon_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        printf("[02D_ENUM] usb_host_install fallo: %s\r\n", esp_err_to_name(err));
        ESP_LOGE(TAG, "usb_host_install fallo: %s", esp_err_to_name(err));
        s_host_ready = -1;
        vTaskDelete(NULL);
        return;
    }

    printf("[02D_ENUM] USB Host library instalada\r\n");
    ESP_LOGI(TAG, "USB Host library instalada");
    s_host_ready = 1;

    xTaskNotifyGive((TaskHandle_t)arg);

    while (1) {
        uint32_t event_flags = 0;
        err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(err));
        }

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

static void usb_client_task(void *arg)
{
    while (s_host_ready == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_host_ready < 0) {
        printf("[02D_ENUM] Host no listo, cliente abortado\r\n");
        vTaskDelete(NULL);
        return;
    }

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };

    esp_err_t err = usb_host_client_register(&client_config, &s_client_hdl);
    if (err != ESP_OK) {
        printf("[02D_ENUM] usb_host_client_register fallo: %s\r\n", esp_err_to_name(err));
        ESP_LOGE(TAG, "usb_host_client_register fallo: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    printf("[02D_ENUM] Cliente USB registrado. Inserte pendrive/teclado/receptor.\r\n");
    ESP_LOGI(TAG, "Cliente USB registrado");

    while (1) {
        err = usb_host_client_handle_events(s_client_hdl, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "usb_host_client_handle_events: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t usb_enum_debug_init(void)
{
    printf("\r\n[02D_ENUM] Iniciando USB ENUM DEBUG...\r\n");
    ESP_LOGI(TAG, "Iniciando USB ENUM DEBUG");

    if (s_daemon_task_hdl || s_client_task_hdl) {
        return ESP_OK;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(
        usb_daemon_task,
        "usb_enum_daemon",
        4096,
        xTaskGetCurrentTaskHandle(),
        4,
        &s_daemon_task_hdl,
        0
    );
    if (ok != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));

    ok = xTaskCreatePinnedToCore(
        usb_client_task,
        "usb_enum_client",
        4096,
        NULL,
        5,
        &s_client_task_hdl,
        0
    );
    if (ok != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
