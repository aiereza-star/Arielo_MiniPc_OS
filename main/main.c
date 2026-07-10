#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_console.h"
#include "nvs_flash.h"
#include "host/ble_store.h"
#include "minipc_sd_02b.h"
#include "minipc_touch_gt911.h"
#include "breezybox.h"
#include "rgb_display.h"
#include "my_console_io.h"
#include "bt_keyboard.h"
#include "vterm.h"
#include "minipc_wifi_02c.h"
#include "usb_hid_keyboard_02d.h"
#include "minipc_usb_msc_02d.h"
#include "minipc_usbsel_02d.h"
#include "minipc_bootpixel_03d.h"
#include "minipc_ntp.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"


static const char *TAG = "main";

// BreezyBox command to scan for BT
int cmd_btscan(int argc, char **argv) {
    int verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    return bt_keyboard_scan_ex(verbose) == ESP_OK ? 0 : 1;
}

// Command Wrapper
int cmd_btconnect(int argc, char **argv) {
    return bt_keyboard_connect_native() == ESP_OK ? 0 : 1;
}

static int cmd_btclear(int argc, char **argv) {
    bt_keyboard_clear_bonds();
    printf("Bonds cleared. Restart device.\n");
    return 0;
}

// BreezyBox command to check BT status
static int cmd_btstatus(int argc, char **argv)
{
    if (bt_keyboard_connected()) {
        printf("BT keyboard: connected\n");
    } else {
        printf("BT keyboard: not connected\n");
        printf("Use 'btscan' to search for keyboards\n");
    }
    return 0;
}

// WiFi status/control
static int cmd_wifi(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "start") == 0) {
        esp_err_t ret = minipc_wifi_start_async(0);
        printf("WiFi start async: %s\n", esp_err_to_name(ret));
        return ret == ESP_OK ? 0 : 1;
    }

    printf("WiFi: %s\n", minipc_wifi_is_connected() ? "connected" : "not connected");
    printf("IP: %s\n", minipc_wifi_get_ip());
    printf("Usage: wifi [start]\n");
    return 0;
}

// DEBUG
static int cmd_vt(int argc, char **argv)
{
    if (argc < 2) {
        printf("Active: VT%d\n", vterm_get_active());
        return 0;
    }
    int n = atoi(argv[1]);
    if (n >= 0 && n < VTERM_COUNT) {
        vterm_switch(n);
        printf("Switched to VT%d\n", n);
    }
    return 0;
}

// DEBUG
static int cmd_keytest(int argc, char **argv)
{
    printf("Press keys (Ctrl+C to exit):\n");
    while (1) {
        int c = getchar();
        if (c == 3) break;  // Ctrl+C
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (c >= 32 && c < 127) {
            printf("0x%02X '%c'\n", c, c);
        } else {
            printf("0x%02X\n", c);
        }
    }
    return 0;
}

// DEBUG
static int cmd_colortest(int argc, char **argv)
{
    printf("\033[31mRed\033[0m ");
    printf("\033[32mGreen\033[0m ");
    printf("\033[33mYellow\033[0m ");
    printf("\033[34mBlue\033[0m ");
    printf("\033[1;35mBright Magenta\033[0m\n");
    printf("\033[41;37mWhite on Red\033[0m\n");
    return 0;
}

// Set console output routing (lcd, usb, or both)
static int cmd_setcon(int argc, char **argv)
{
    if (argc < 2) {
        console_output_mode_t mode = my_console_get_output_mode();
        const char *mode_str = (mode == CONSOLE_OUT_LCD) ? "lcd" :
                                (mode == CONSOLE_OUT_USB) ? "usb" : "both";
        int usb_connected = my_console_usb_connected();
        printf("Console output: %s\n", mode_str);
        printf("USB status: %s\n", usb_connected ? "connected" : "disconnected (auto-skipped)");
        printf("Usage: setcon <lcd|usb|both|usbreset>\n");
        return 0;
    }

    const char *arg = argv[1];

    // Handle USB reset command
    if (strcmp(arg, "usbreset") == 0) {
        my_console_usb_reconnect();
        printf("USB detection reset - will re-probe on next write\n");
        return 0;
    }

    console_output_mode_t mode;

    if (strcmp(arg, "lcd") == 0) {
        mode = CONSOLE_OUT_LCD;
    } else if (strcmp(arg, "usb") == 0) {
        mode = CONSOLE_OUT_USB;
    } else if (strcmp(arg, "both") == 0) {
        mode = CONSOLE_OUT_BOTH;
    } else {
        printf("Invalid mode: %s\n", arg);
        printf("Usage: setcon <lcd|usb|both|usbreset>\n");
        return 1;
    }

    my_console_set_output_mode(mode);
    printf("Console output: %s\n", arg);
    return 0;
}

// Main loop - keeps task alive while DMA renders display
static void main_loop(void)
{
    // Display renders via DMA bounce-buffer callbacks (zero-copy from vterm)
    // This loop just keeps the main task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Tarea dedicada para el escritorio grafico, con stack amplio (la copia
// recursiva de ficheros necesita mas que la tarea 'main' por defecto).
__attribute__((unused))
static void desktop_task(void *arg)
{
    (void)arg;
    extern int cmd_desktop(int argc, char **argv);
    char *args[] = { "desktop", NULL };

    cmd_desktop(1, args);   // bloqueante: corre el escritorio hasta que salgas

    // Al salir con el boton/ESC, restaurar consola de texto y terminar la tarea.
    // La consola (stdio) sigue viva: desde ahi puedes escribir 'desktop' para
    // volver a entrar cuando quieras.
    printf("\r\n[DESKTOP] Saliste al modo texto. Escribe 'desktop' para volver.\r\n");
    vTaskDelete(NULL);
}



/* --------------------------------------------------------------------------
 * Arielo MiniPC OS - HTTP bridge para apps ELF externas
 * Browser Lite 06A - base limpia
 *
 * La app ELF externa NO importa sockets, getaddrinfo ni lwip_*.
 * Solo llama a:
 *
 *     minipc_http_get_text(const char *url, char *out, int out_max)
 *
 * Esta funcion vive dentro del firmware base, donde si es seguro usar LwIP.
 * -------------------------------------------------------------------------- */

#ifndef MINIPC_HTTP_BRIDGE_MAX_HOST
#define MINIPC_HTTP_BRIDGE_MAX_HOST 96
#endif

#ifndef MINIPC_HTTP_BRIDGE_MAX_PATH
#define MINIPC_HTTP_BRIDGE_MAX_PATH 192
#endif

static int minipc_parse_http_url_06a(const char *url,
                                     char *host, int host_max,
                                     char *path, int path_max)
{
    const char *p;
    const char *slash;
    int host_len;
    int path_len;

    if (!url || !host || !path) return -1;

    if (strncmp(url, "http://", 7) != 0) return -2;

    p = url + 7;
    slash = strchr(p, '/');

    if (slash) {
        host_len = (int)(slash - p);
    } else {
        host_len = (int)strlen(p);
    }

    if (host_len <= 0 || host_len >= host_max) return -3;

    memcpy(host, p, host_len);
    host[host_len] = 0;

    if (slash) {
        path_len = (int)strlen(slash);
        if (path_len <= 0) {
            strncpy(path, "/", path_max - 1);
            path[path_max - 1] = 0;
        } else {
            if (path_len >= path_max) path_len = path_max - 1;
            memcpy(path, slash, path_len);
            path[path_len] = 0;
        }
    } else {
        strncpy(path, "/", path_max - 1);
        path[path_max - 1] = 0;
    }

    return 0;
}

/*
 * API exportada para apps ELF externas.
 *
 * Retorno:
 *   >= 0  bytes utiles escritos en out
 *   <  0  error
 */
int minipc_http_get_text(const char *url, char *out, int out_max)
{
    char host[MINIPC_HTTP_BRIDGE_MAX_HOST];
    char path[MINIPC_HTTP_BRIDGE_MAX_PATH];
    char req[384];
    char rx[512];

    struct addrinfo hints;
    struct addrinfo *res = NULL;

    int s = -1;
    int ret;
    int total = 0;
    int header_done = 0;

    if (!url || !out || out_max <= 1) return -10;

    out[0] = 0;

    ret = minipc_parse_http_url_06a(url, host, sizeof(host), path, sizeof(path));
    if (ret != 0) return ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = lwip_getaddrinfo(host, "80", &hints, &res);
    if (ret != 0 || !res) {
        return -20;
    }

    s = lwip_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0) {
        lwip_freeaddrinfo(res);
        return -21;
    }

    ret = lwip_connect(s, res->ai_addr, res->ai_addrlen);
    lwip_freeaddrinfo(res);
    res = NULL;

    if (ret != 0) {
        lwip_close(s);
        return -22;
    }

    snprintf(req, sizeof(req),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "User-Agent: ArieloMiniPC-HTTPBridge/06A\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);

    ret = lwip_send(s, req, strlen(req), 0);
    if (ret <= 0) {
        lwip_close(s);
        return -23;
    }

    while (total < out_max - 1) {
        int n = lwip_recv(s, rx, sizeof(rx), 0);
        if (n <= 0) break;

        if (!header_done) {
            char *body = NULL;

            for (int i = 0; i <= n - 4; i++) {
                if (rx[i] == '\r' && rx[i + 1] == '\n' &&
                    rx[i + 2] == '\r' && rx[i + 3] == '\n') {
                    body = &rx[i + 4];
                    break;
                }
            }

            if (body) {
                int copy_len = n - (int)(body - rx);
                header_done = 1;

                if (copy_len > out_max - 1 - total) {
                    copy_len = out_max - 1 - total;
                }

                if (copy_len > 0) {
                    memcpy(out + total, body, copy_len);
                    total += copy_len;
                }
            }
        } else {
            int copy_len = n;

            if (copy_len > out_max - 1 - total) {
                copy_len = out_max - 1 - total;
            }

            if (copy_len > 0) {
                memcpy(out + total, rx, copy_len);
                total += copy_len;
            }
        }
    }

    lwip_close(s);

    out[total] = 0;
    return total;
}

void app_main(void)
{

    /*
     * 03D:
     * Splash grafico real en modo VGA13H.
     * No usa stdout/VTerm durante splash, por tanto no puede hacer scroll.
     */
    rgb_display_init();

    minipc_bootpixel_begin();

    minipc_bootpixel_message("Mounting storage...");
    minipc_sd_init();
    minipc_bootpixel_ok("Storage");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    minipc_bootpixel_ok("NVS");

    minipc_bootpixel_message("Preparing USB Host...");
    minipc_usbsel_cycle_host_mode();
    minipc_bootpixel_ok("USB Host");

    minipc_bootpixel_message("Starting touch...");
    minipc_touch_init();
    minipc_bootpixel_ok("Touch GT911");

    minipc_bootpixel_message("Starting keyboard...");
    usb_hid_keyboard_init();
    minipc_bootpixel_ok("Keyboard");

    minipc_bootpixel_message("Starting USB storage...");
    minipc_usb_msc_init();
    minipc_bootpixel_ok("USB MSC");

    minipc_bootpixel_message("Connecting WiFi...");
    minipc_bootpixel_status("WiFi", "CONNECT");
    minipc_wifi_start_async(0);

    // NTP_LATE_WIFI_FIX_01A:
    // Arrancar NTP siempre. El servicio esperara a que WiFi tenga IP.
    // Antes solo se arrancaba si WiFi conectaba dentro de esta ventana corta.
    minipc_ntp_start();

    for (int i = 0; i < 16; i++) {
        if (minipc_wifi_is_connected()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    if (minipc_wifi_is_connected()) {
        minipc_bootpixel_status("WiFi", minipc_wifi_get_ip());
        // NTP ya esta arrancado como servicio vivo.
    } else {
        minipc_bootpixel_status("WiFi", "background");
    }

    minipc_bootpixel_ready();

    // Mantener el splash "Ready" un momento mas. Esto enmascara el tiempo que
    // el stdio necesita para estar operativo antes de inyectar 'desktop', en
    // vez de mostrar una pausa rara despues. (Sugerencia de Arielo.)
    vTaskDelay(pdMS_TO_TICKS(600));

    minipc_bootpixel_finish_to_text();

    /*
     * Entregamos ahora la pantalla al VTerm/console.
     * my_console_init() llamara a rgb_display_set_buffer()
     * con el buffer real del terminal.
     */
    if (my_console_init() != ESP_OK) {
        ESP_LOGE(TAG, "Console init failed!");
        return;
    }

    usb_hid_keyboard_set_char_callback(my_console_bt_receive);

    breezybox_start_stdio(12288, 5);

    // Register custom commands
    extern int cmd_testgfx(int argc, char **argv);
    extern int cmd_desktop(int argc, char **argv);
    extern int cmd_i2cscan(int argc, char **argv);
    extern int cmd_touchprobe(int argc, char **argv);
    extern int cmd_touchtest(int argc, char **argv);
    extern int cmd_tbtest_lexbor_source(int argc, char **argv);
    extern int cmd_tbrender_lexbor_10aa(int argc, char **argv);
    extern int cmd_tbview_lexbor_10ab(int argc, char **argv);
    extern int cmd_tbbrowser_lexbor_10ac(int argc, char **argv);
    extern int cmd_tbbrowser_lexbor_10ad(int argc, char **argv);
    extern int cmd_tbbrowser_lexbor_10ae(int argc, char **argv);
    extern int cmd_tbbrowser_gui_10ah(int argc, char **argv);
    static const esp_console_cmd_t cmds[] = {
        { .command = "btscan", .help = "Scan for BT keyboards", .hint = "[-v]", .func = &cmd_btscan },
        { .command = "btconnect", .help = "Connect to found HID", .func = &cmd_btconnect },
        { .command = "btclear", .help = "Clear saved BT devices", .func = &cmd_btclear },
        { .command = "btstatus", .help = "Show BT keyboard status", .func = &cmd_btstatus },
        { .command = "vt", .help = "Switch VT", .func = &cmd_vt },
        { .command = "keytest", .help = "Keys test", .func = &cmd_keytest },
        { .command = "colortest", .help = "ANSI colors test", .func = &cmd_colortest },
        { .command = "setcon", .help = "Set console output", .hint = "<lcd|usb|both>", .func = &cmd_setcon },
        { .command = "wifi", .help = "Show/start WiFi", .hint = "[start]", .func = &cmd_wifi },
        { .command = "testgfx", .help = "VGA graphics demo", .hint = "[-t seconds] [-v]", .func = &cmd_testgfx },
        { .command = "desktop", .help = "Arielo MiniPC graphical desktop", .hint = "[-t seconds]", .func = &cmd_desktop },
        { .command = "i2cscan", .help = "Scan I2C bus (touch/CH422G bring-up)", .func = &cmd_i2cscan },
        { .command = "touchprobe", .help = "Probe GT911 touch at 0x5D/0x14", .func = &cmd_touchprobe },
        { .command = "touchtest", .help = "Read touch points for ~10s", .func = &cmd_touchtest },
        { .command = "tbtest", .help = "Lexbor builtin source test", .hint = "[create|parse|loop N]", .func = &cmd_tbtest_lexbor_source },
        { .command = "tbrender", .help = "TactileBrowser builtin DOM text render test 10AA", .hint = "[sample|file PATH|loop N]", .func = &cmd_tbrender_lexbor_10aa },
        { .command = "tbview", .help = "TactileBrowser text viewer with scroll 10AB", .hint = "[home|net|url URL|files|bookmarks|file PATH|/ruta.html]", .func = &cmd_tbview_lexbor_10ab },
        { .command = "tbbrowser", .help = "TactileBrowser local navigator with bookmarks 10AE", .hint = "[home|net|url URL|files|bookmarks|file PATH|/ruta.html]", .func = &cmd_tbbrowser_lexbor_10ae },
        { .command = "tbbgui", .help = "TactileBrowser GUI NET HTTP 10AL", .hint = "[home|net|url URL|files|bookmarks|file PATH|/ruta.html]", .func = &cmd_tbbrowser_gui_10ah },
        { .command = "tbbrowsergui", .help = "TactileBrowser GUI NET HTTP 10AL", .hint = "[home|net|url URL|files|bookmarks|file PATH|/ruta.html]", .func = &cmd_tbbrowser_gui_10ah },
        { .command = "tbbrowser10ad", .help = "TactileBrowser header/back history fallback 10AD", .hint = "[home|net|url URL|files|bookmarks|file PATH|/ruta.html]", .func = &cmd_tbbrowser_lexbor_10ad },
        { .command = "tbbrowser10ac", .help = "TactileBrowser local navigator fallback 10AC", .hint = "[home|net|url URL|files|bookmarks|file PATH|/ruta.html]", .func = &cmd_tbbrowser_lexbor_10ac },
    };
    for (int i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        esp_console_cmd_register(&cmds[i]);
    }

    /*
     * 02E: WiFi se arranca en segundo plano.
     * Delay corto para dejar BreezyBox + teclado operativos primero.
     */

    // --- Arranque en modo grafico ---
    // Poner a 0 para arrancar en modo TEXTO (consola) como antes.
    // En modo grafico, al salir del escritorio (boton en pantalla / ESC)
    // se cae a la consola de texto, donde se puede escribir 'desktop' para
    // volver a entrar.
    #define BOOT_TO_DESKTOP 1

#if BOOT_TO_DESKTOP
    // El splash "Ready" ya dio tiempo al stdio; aqui basta un respiro corto.
    vTaskDelay(pdMS_TO_TICKS(200));
    // Arrancar el escritorio INYECTANDO el comando "desktop" en la entrada del
    // stdio, como si el usuario lo hubiera tecleado. Asi cmd_desktop corre
    // DENTRO del bucle del stdio (linenoise queda bloqueado dentro) y NO compite
    // por el teclado con el escritorio.
    {
        const char *autostart = "desktop\n";
        for (const char *p = autostart; *p; p++) {
            vterm_input_feed(*p);
        }
    }
#endif

    // Keep main task alive (display renders via DMA callbacks)
    main_loop();
}