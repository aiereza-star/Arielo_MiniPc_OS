/*
 * usb_hid_keyboard_04A_FIX2_ENTER_CR_ONLY.c
 *
 * Arielo MiniPC OS - USB HID Keyboard + Mouse base + CAPS LOCK FIX.
 *
 * Mantiene compatibilidad con la rama buena:
 *   - mismas funciones publicas usb_hid_keyboard_* usadas por main.c
 *   - teclado sigue enviando caracteres a my_console_bt_receive()
 *
 * Añade:
 *   - soporte HID mouse boot
 *   - posicion virtual 400x240; se escala x2 a puntero LCD 800x480
 *   - callback opcional de mouse
 *   - mouse silencioso y ENTER como CR para prompt limpio
 *
 * HUB:
 *   El soporte de HUB externo depende de sdkconfig:
 *     CONFIG_USB_HOST_HUBS_SUPPORTED=y
 *
 * No monta pendrive todavia. Primero dejamos teclado+mouse funcionando
 * detras de HUB; luego subimos a MSC.
 */

#include "usb_hid_keyboard_02d.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"

#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "rgb_display.h"

// 05A_FIX1: prototipos explicitos por si el build toma un rgb_display.h antiguo.
void rgb_display_set_mouse_pointer(int x, int y, uint8_t buttons, int visible);
void rgb_display_hide_mouse_pointer(void);

static const char *TAG = "usb_hid04a";

#define ARIELO_MOUSE_W 400
#define ARIELO_MOUSE_H 240

/*
 * FIX1:
 * No sacar telemetria continua del mouse por pantalla/consola.
 * El mouse funciona internamente y entrega callback, pero no inunda
 * BreezyBox/LCD con x/y/dx/dy.
 *
 * Para depurar por COM5, cambiar temporalmente a 1.
 */
#ifndef USBK_MOUSE_LOG
#define USBK_MOUSE_LOG 0
#endif

typedef enum {
    APP_EVENT_HID_HOST = 1,
} app_event_group_t;

typedef struct {
    app_event_group_t event_group;
    struct {
        hid_host_device_handle_t handle;
        hid_host_driver_event_t event;
        void *arg;
    } hid_host_device;
} usb_kbd_event_t;

static QueueHandle_t s_usb_kbd_queue = NULL;
static usb_hid_keyboard_char_cb_t s_char_cb = NULL;
static usb_hid_mouse_cb_t s_mouse_cb = NULL;

static volatile int s_keyboard_connected = 0;
static volatile int s_mouse_connected = 0;
static volatile int s_usb_host_ready = 0;
static volatile int s_hid_installed = 0;

static uint8_t s_prev_keys[6] = {0};

// 09A_FIX1_USUARIO:
// Estado RAW del último report USB HID para apps externas tipo ccleste.
// No definimos bt_keyboard_* aquí para evitar choque con breezy_bt.
// El componente breezy_bt hará fallback a estas funciones usb_hid_keyboard_raw_*.
static volatile uint8_t s_usb_raw_keys_held[6] = {0};
static volatile uint8_t s_usb_raw_modifiers = 0;

// 09A_FIX3:
// Además del estado mantenido, guardamos un latch breve por cada tecla.
// Así un toque corto Z/X no se pierde entre un report USB y el polling del ELF.
static volatile uint32_t s_usb_raw_latch_until[232] = {0};
// 08A_FIX2: estado real de Bloq Mayúsculas.
// En HID boot, Caps Lock llega como scancode 0x39, pero no genera carácter.
// Hay que memorizarlo y aplicarlo a las letras a..z.
static int s_caps_lock_on = 0;

// 05A_FIX_NKEY: acento muerto pendiente tras pulsar la tecla 0x34 (´ ¨).
// 0 = ninguno, 1 = agudo (´), 2 = diéresis (¨).
static int s_pending_accent = 0;

static int s_mouse_x = ARIELO_MOUSE_W / 2;
static int s_mouse_y = ARIELO_MOUSE_H / 2;
static uint8_t s_mouse_buttons = 0;
static int s_mouse_log_div = 0;

// ============================================================================
// MAPA DE TECLADO ESPAÑOL (ES - España). Fase 1: todo el ASCII + AltGr.
// Indexado por scancode USB HID. Los caracteres no-ASCII (ñ, acentos, º, ª,
// ç, €, ¿, ¡) se tratan aparte en la Fase 2 (requieren UTF-8 en el terminal).
//
// Niveles por tecla: HID_MAP (normal), HID_MAP_SHIFT (Shift), HID_MAP_ALTGR.
// Scancodes relevantes del layout ES:
//   0x1E-0x27 = fila 1..0
//   0x2D = '  ?  (\\ en AltGr)      0x2E = ¡ ¿ (no-ASCII, fase 2)
//   0x2F = ` ^ [ (AltGr [)         0x30 = + * ] (AltGr ])
//   0x31 = ç } (AltGr })           0x33 = ñ Ñ (no-ASCII, fase 2)
//   0x34 = ´ ¨ { (AltGr {)         0x35 = º ª \ (no-ASCII salvo \)
//   0x36 = , ;                     0x37 = . :        0x38 = - _
//   0x64 = < > (AltGr |)  [tecla junto a Shift izq]
// ============================================================================
static const char HID_MAP[] = {
    0,0,0,0,'a','b','c','d','e','f','g','h','i','j','k','l','m','n',
    'o','p','q','r','s','t','u','v','w','x','y','z','1','2','3','4',
    '5','6','7','8','9','0','\r',27,'\b','\t',' ','\'',0,'`','+',0,
    0,0,0,0,',','.','-'
};

static const char HID_MAP_SHIFT[] = {
    0,0,0,0,'A','B','C','D','E','F','G','H','I','J','K','L','M','N',
    'O','P','Q','R','S','T','U','V','W','X','Y','Z','!','"',0,'$',
    '%','&','/','(',')','=','\r',27,'\b','\t',' ','?',0,'^','*',0,
    0,0,0,0,';',':','_'
};

// AltGr: solo donde produce ASCII util. 0 = nada.
// Indices exactos: 0x1F(2)=@ 0x20(3)=# 0x21(4)=~ 0x2F(`)=[ 0x30(+)=]
//                  0x31(ç)=} 0x34(´)={ 0x35(º)=backslash
static const char HID_MAP_ALTGR[] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,'@','#','~',0,0,
    0,0,0,0,0,0,0,0,0,0,0,'[',']','}',0,0,'{','\\',
    0,0,0
};

#define USBK_KEY_ESC         0x29
#define USBK_KEY_CAPSLOCK    0x39
#define USBK_KEY_F1          0x3A
#define USBK_KEY_INSERT      0x49
#define USBK_KEY_HOME        0x4A
#define USBK_KEY_PAGEUP      0x4B
#define USBK_KEY_DELETE      0x4C
#define USBK_KEY_END         0x4D
#define USBK_KEY_PAGEDOWN    0x4E
#define USBK_KEY_RIGHT       0x4F
#define USBK_KEY_LEFT        0x50
#define USBK_KEY_DOWN        0x51
#define USBK_KEY_UP          0x52

static inline int clamp_i(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline void deliver_char(char c)
{
    /*
     * 04A_FIX2:
     * Base limpia 19:59 + correccion unica:
     * ENTER de teclado USB debe entrar como CR ('\r'), no LF ('\n').
     * Si entra LF, VTerm baja linea pero no vuelve al margen izquierdo
     * y los prompts "$" aparecen en escalera.
     */
    if (c == '\n') c = '\r';

    usb_hid_keyboard_char_cb_t cb = s_char_cb;
    if (cb) cb(c);
}

static void deliver_seq(const char *seq)
{
    while (seq && *seq) deliver_char(*seq++);
}

// 05A_FIX_NKEY: compone acento muerto + vocal -> secuencia UTF-8 de la
// vocal acentuada. mode: 1=agudo (á é í ó ú), 2=diéresis (ä ë ï ö ü).
// Devuelve NULL si la letra no es una vocal combinable (el llamador debe
// entonces entregar el acento suelto + la letra tal cual).
static const char *usbk_compose_accent(int mode, char c)
{
    if (mode == 1) {
        switch (c) {
            case 'a': return "\xC3\xA1"; case 'A': return "\xC3\x81";
            case 'e': return "\xC3\xA9"; case 'E': return "\xC3\x89";
            case 'i': return "\xC3\xAD"; case 'I': return "\xC3\x8D";
            case 'o': return "\xC3\xB3"; case 'O': return "\xC3\x93";
            case 'u': return "\xC3\xBA"; case 'U': return "\xC3\x9A";
            default: return NULL;
        }
    }
    if (mode == 2) {
        switch (c) {
            case 'a': return "\xC3\xA4"; case 'A': return "\xC3\x84";
            case 'e': return "\xC3\xAB"; case 'E': return "\xC3\x8B";
            case 'i': return "\xC3\xAF"; case 'I': return "\xC3\x8F";
            case 'o': return "\xC3\xB6"; case 'O': return "\xC3\x96";
            case 'u': return "\xC3\xBC"; case 'U': return "\xC3\x9C";
            default: return NULL;
        }
    }
    return NULL;
}

static int handle_special_key(uint8_t key)
{
    switch (key) {
    case USBK_KEY_UP:       deliver_seq("\x1b[A");  return 1;
    case USBK_KEY_DOWN:     deliver_seq("\x1b[B");  return 1;
    case USBK_KEY_RIGHT:    deliver_seq("\x1b[C");  return 1;
    case USBK_KEY_LEFT:     deliver_seq("\x1b[D");  return 1;
    case USBK_KEY_HOME:     deliver_seq("\x1b[H");  return 1;
    case USBK_KEY_END:      deliver_seq("\x1b[F");  return 1;
    case USBK_KEY_INSERT:   deliver_seq("\x1b[2~"); return 1;
    case USBK_KEY_DELETE:   deliver_seq("\x1b[3~"); return 1;
    case USBK_KEY_PAGEUP:   deliver_seq("\x1b[5~"); return 1;
    case USBK_KEY_PAGEDOWN: deliver_seq("\x1b[6~"); return 1;
    case USBK_KEY_F1:       deliver_seq("\x1bOP");  return 1;
    case USBK_KEY_F1 + 1:   deliver_seq("\x1bOQ");  return 1;
    case USBK_KEY_F1 + 2:   deliver_seq("\x1bOR");  return 1;
    case USBK_KEY_F1 + 3:   deliver_seq("\x1bOS");  return 1;
    case USBK_KEY_F1 + 4:   deliver_seq("\x1b[15~"); return 1;
    case USBK_KEY_F1 + 5:   deliver_seq("\x1b[17~"); return 1;
    case USBK_KEY_F1 + 6:   deliver_seq("\x1b[18~"); return 1;
    case USBK_KEY_F1 + 7:   deliver_seq("\x1b[19~"); return 1;
    case USBK_KEY_F1 + 8:   deliver_seq("\x1b[20~"); return 1;
    case USBK_KEY_F1 + 9:   deliver_seq("\x1b[21~"); return 1;
    case USBK_KEY_F1 + 10:  deliver_seq("\x1b[23~"); return 1;
    case USBK_KEY_F1 + 11:  deliver_seq("\x1b[24~"); return 1;
    default: return 0;
    }
}

static bool key_was_pressed(const uint8_t *prev, uint8_t key)
{
    for (int i = 0; i < 6; i++) {
        if (prev[i] == key) return true;
    }
    return false;
}


static void usb_raw_latch_key(uint8_t keycode)
{
    if (keycode >= 232) return;
    uint32_t now = (uint32_t)xTaskGetTickCount();
    s_usb_raw_latch_until[keycode] = now + pdMS_TO_TICKS(250);
}

static void process_keyboard_report_boot(const uint8_t *data, size_t len)
{
    if (!data || len < 8) return;

    uint8_t mod = data[0];
    const uint8_t *keys = &data[2];

    // 09A_FIX1_USUARIO:
    // Guardar el report RAW completo. Esto permite que breezy_bt consulte
    // teclas mantenidas del USB HID mediante fallback, sin duplicar símbolos.
    s_usb_raw_modifiers = mod;
    for (int k = 0; k < 6; k++) {
        s_usb_raw_keys_held[k] = keys[k];

        // Si es una pulsación nueva, mantenerla visible un instante para apps
        // que hacen polling y pueden perder pulsaciones muy cortas.
        if (keys[k] && !key_was_pressed(s_prev_keys, keys[k])) {
            usb_raw_latch_key(keys[k]);
        }
    }
    int shift = (mod & 0x22) != 0;
    int ctrl  = (mod & 0x11) != 0;
    int altgr = (mod & 0x40) != 0;   // Right-Alt = AltGr

    for (int i = 0; i < 6; i++) {
        uint8_t key = keys[i];
        if (key == 0) continue;
        if (key_was_pressed(s_prev_keys, key)) continue;
         if (handle_special_key(key)) continue;

        // 08A_FIX2: Bloq Mayúsculas. Se conmuta una sola vez por pulsación
        // gracias a s_prev_keys, así no se repite mientras la tecla está mantenida.
        if (key == USBK_KEY_CAPSLOCK) {
            s_caps_lock_on = !s_caps_lock_on;
            printf("[05A_USB] BLOQ MAYUS %s\r\n", s_caps_lock_on ? "ON" : "OFF");
            continue;
        }

 	// Tecla especial 0x64 (junto a Shift izq, en teclado ES): < > |
        // Esta fuera del rango de las tablas (que llegan a 0x38), por eso
        // antes no producia nada. La tratamos aparte.
        if (key == 0x64) {
            char c;
            if (altgr)      c = '|';
            else if (shift) c = '>';
            else            c = '<';
            deliver_char(c);
            continue;
        }

        // 05A_FIX_NKEY: Fase 2 activada -- tecla fisica ñ/Ñ real del teclado
        // ES (scancode 0x33). Antes mapeaba a 0 (nada). Se entrega como los
        // dos bytes UTF-8 reales (ñ=0xC3 0xB1, Ñ=0xC3 0x91) via deliver_seq,
        // igual que ya se hace con las secuencias de flechas/Fn. El lado del
        // navegador (tbb10bk_append_input_char_utf8) ya reconoce UTF-8 real
        // byte a byte, asi que no hace falta tocar nada mas alli.
        if (key == 0x33) {
            deliver_seq(shift ? "\xC3\x91" : "\xC3\xB1");
            continue;
        }

        // 05A_FIX_NKEY: Fase 2 activada -- tecla del acento agudo/dieresis
        // (scancode 0x34, ' ´ ¨ ' en el teclado ES). Implementada como
        // "dead key" real: no emite nada al pulsarla, solo arma el acento
        // pendiente para la siguiente vocal (a,e,i,o,u / A,E,I,O,U).
        if (key == 0x34) {
            s_pending_accent = shift ? 2 /* diéresis */ : 1 /* agudo */;
            continue;
        }


        if (handle_special_key(key)) continue;

        if (key < sizeof(HID_MAP)) {
            int is_letter = (key >= 0x04 && key <= 0x1D); // HID a..z
            char c = 0;

            // Ctrl+A..Z debe seguir funcionando aunque Bloq Mayúsculas esté activo.
            if (ctrl && is_letter) {
                c = (char)(key - 0x04 + 1);
            } else if (altgr) {
                c = HID_MAP_ALTGR[key];
            } else {
                // En letras: CapsLock invierte Shift.
                // En números/símbolos: CapsLock no afecta.
                int effective_shift = is_letter ? (shift ^ s_caps_lock_on) : shift;
                c = effective_shift ? HID_MAP_SHIFT[key] : HID_MAP[key];
            }

            // 05A_FIX_NKEY: si hay un acento muerto pendiente (tecla 0x34),
            // se intenta combinar con esta letra. Si la letra no es una
            // vocal combinable, se entrega el acento suelto (') o (") y
            // luego la letra tal cual, en vez de perder la pulsacion.
            if (s_pending_accent != 0 && c != 0) {
                const char *utf8 = usbk_compose_accent(s_pending_accent, c);
                int was_diaeresis = (s_pending_accent == 2);
                s_pending_accent = 0;
                if (utf8 != NULL) {
                    deliver_seq(utf8);
                    continue;
                }
                deliver_char(was_diaeresis ? '"' : '\'');
            }

            if (c) deliver_char(c);
        }
    }

    memcpy(s_prev_keys, keys, 6);
}

// ---------------------------------------------------------------------------
// 09A_FIX1_USUARIO: API RAW USB HID para fallback dentro de breezy_bt.
// NO usar nombres bt_keyboard_* aquí, porque breezy_bt ya los define.
// ---------------------------------------------------------------------------
int usb_hid_keyboard_raw_is_pressed(uint8_t keycode)
{
    int held = 0;
    for (int i = 0; i < 6; i++) {
        if (s_usb_raw_keys_held[i] == keycode) {
            held = 1;
            break;
        }
    }

    if (!held && keycode < 232) {
        uint32_t now = (uint32_t)xTaskGetTickCount();
        uint32_t until = s_usb_raw_latch_until[keycode];
        if ((int32_t)(until - now) > 0) {
            held = 1;
        }
    }

    return held;
}

uint8_t usb_hid_keyboard_raw_get_modifiers(void)
{
    return s_usb_raw_modifiers;
}



static void process_mouse_report_boot(const uint8_t *data, size_t len)
{
    /*
     * Boot mouse report habitual:
     *   byte0 buttons
     *   byte1 dx signed
     *   byte2 dy signed
     *   byte3 wheel signed opcional
     */
    if (!data || len < 3) return;

    uint8_t buttons = data[0];
    int8_t dx = (int8_t)data[1];
    int8_t dy = (int8_t)data[2];
    int8_t wheel = (len >= 4) ? (int8_t)data[3] : 0;

    if (dx == 0 && dy == 0 && wheel == 0 && buttons == s_mouse_buttons) {
        return;
    }

    s_mouse_x = clamp_i(s_mouse_x + dx, 0, ARIELO_MOUSE_W - 1);
    s_mouse_y = clamp_i(s_mouse_y + dy, 0, ARIELO_MOUSE_H - 1);

    uint8_t old_buttons = s_mouse_buttons;
    s_mouse_buttons = buttons;
    (void)s_mouse_log_div;

    // 05A GUI base: mostrar puntero en pantalla fisica 800x480.
    // El raton mantiene coordenadas virtuales 400x240 para ir alineado con SM_400X240;
    // el overlay LCD las escala x2.
    rgb_display_set_mouse_pointer(s_mouse_x * 2, s_mouse_y * 2, buttons, 1);

    usb_hid_mouse_event_t ev = {
        .x = s_mouse_x,
        .y = s_mouse_y,
        .dx = dx,
        .dy = dy,
        .wheel = wheel,
        .buttons = buttons,
        .left = (buttons & 0x01) != 0,
        .right = (buttons & 0x02) != 0,
        .middle = (buttons & 0x04) != 0,
        .pressed = ((buttons & ~old_buttons) != 0),
        .released = ((old_buttons & ~buttons) != 0),
    };

    usb_hid_mouse_cb_t cb = s_mouse_cb;
    if (cb) cb(&ev);

#if USBK_MOUSE_LOG
    /*
     * Log moderado: se imprime en cambios de boton/rueda y de vez en cuando
     * en movimiento para no inundar COM5.
     */
    int button_changed = buttons != old_buttons;
    if (button_changed || wheel != 0 || (++s_mouse_log_div >= 12)) {
        s_mouse_log_div = 0;
        ESP_LOGI(TAG, "MOUSE x=%d y=%d dx=%d dy=%d wheel=%d btn=0x%02X",
                 ev.x, ev.y, ev.dx, ev.dy, ev.wheel, ev.buttons);
    }
#endif
}

static const char *proto_name(hid_protocol_t proto)
{
    switch (proto) {
    case HID_PROTOCOL_KEYBOARD: return "KEYBOARD";
    case HID_PROTOCOL_MOUSE:    return "MOUSE";
    case HID_PROTOCOL_NONE:     return "GENERIC";
    default:                    return "?";
    }
}

static void usb_kbd_interface_callback(hid_host_device_handle_t hid_device_handle,
                                       const hid_host_interface_event_t event,
                                       void *arg)
{
    hid_host_dev_params_t dev_params = {0};
    esp_err_t err = hid_host_device_get_params(hid_device_handle, &dev_params);
    if (err != ESP_OK) return;

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t data[64] = {0};
        size_t data_length = 0;
        err = hid_host_device_get_raw_input_report_data(hid_device_handle, data, sizeof(data), &data_length);
        if (err != ESP_OK) return;

        if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
            process_keyboard_report_boot(data, data_length);
        } else if (dev_params.proto == HID_PROTOCOL_MOUSE) {
            process_mouse_report_boot(data, data_length);
        }
        break;
    }

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "USB HID %s desconectado", proto_name(dev_params.proto));

        if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
            printf("\r\n[04A_USB] teclado desconectado\r\n");
            s_keyboard_connected = 0;
            memset(s_prev_keys, 0, sizeof(s_prev_keys));
        } else if (dev_params.proto == HID_PROTOCOL_MOUSE) {
            printf("\r\n[05A_USB] mouse desconectado\r\n");
            s_mouse_connected = 0;
            s_mouse_buttons = 0;
            rgb_display_hide_mouse_pointer();
        }

        hid_host_device_close(hid_device_handle);
        break;

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "USB HID %s transfer error", proto_name(dev_params.proto));
        break;

    default:
        break;
    }
}

static void usb_kbd_device_event(hid_host_device_handle_t hid_device_handle,
                                 const hid_host_driver_event_t event,
                                 void *arg)
{
    hid_host_dev_params_t dev_params = {0};
    esp_err_t err = hid_host_device_get_params(hid_device_handle, &dev_params);
    if (err != ESP_OK) return;

    switch (event) {
    case HID_HOST_DRIVER_EVENT_CONNECTED: {
        ESP_LOGI(TAG, "USB HID conectado proto=%s subclass=%d",
                 proto_name(dev_params.proto), dev_params.sub_class);

        const hid_host_device_config_t dev_config = {
            .callback = usb_kbd_interface_callback,
            .callback_arg = NULL,
        };

        if (dev_params.proto == HID_PROTOCOL_KEYBOARD ||
            dev_params.proto == HID_PROTOCOL_MOUSE) {

            err = hid_host_device_open(hid_device_handle, &dev_config);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "hid_host_device_open %s fallo: %s",
                         proto_name(dev_params.proto), esp_err_to_name(err));
                return;
            }

            if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT);
                hid_class_request_set_idle(hid_device_handle, 0, 0);
            }

            err = hid_host_device_start(hid_device_handle);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "hid_host_device_start %s fallo: %s",
                         proto_name(dev_params.proto), esp_err_to_name(err));
                hid_host_device_close(hid_device_handle);
                return;
            }

            if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
                s_keyboard_connected = 1;
                memset(s_prev_keys, 0, sizeof(s_prev_keys));
                printf("\r\n[04A_USB] TECLADO USB HID conectado\r\n");
                ESP_LOGI(TAG, "TECLADO USB HID listo");
            } else {
                s_mouse_connected = 1;
                s_mouse_x = ARIELO_MOUSE_W / 2;
                s_mouse_y = ARIELO_MOUSE_H / 2;
                s_mouse_buttons = 0;
                rgb_display_set_mouse_pointer(s_mouse_x * 2, s_mouse_y * 2, 0, 1);
                printf("\r\n[05A_USB] MOUSE USB HID conectado - puntero activo\r\n");
                ESP_LOGI(TAG, "MOUSE USB HID listo x=%d y=%d", s_mouse_x, s_mouse_y);
            }
        } else {
            ESP_LOGI(TAG, "USB HID ignorado proto=%s", proto_name(dev_params.proto));
        }
        break;
    }
    default:
        break;
    }
}

static void usb_kbd_driver_callback(hid_host_device_handle_t hid_device_handle,
                                    const hid_host_driver_event_t event,
                                    void *arg)
{
    if (!s_usb_kbd_queue) return;
    usb_kbd_event_t evt = {
        .event_group = APP_EVENT_HID_HOST,
        .hid_host_device = {
            .handle = hid_device_handle,
            .event = event,
            .arg = arg,
        },
    };
    xQueueSend(s_usb_kbd_queue, &evt, 0);
}

static void usb_kbd_host_lib_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_host_install fallo: %s", esp_err_to_name(err));
        s_usb_host_ready = -1;
        vTaskDelete(NULL);
        return;
    }

    s_usb_host_ready = 1;
    xTaskNotifyGive((TaskHandle_t)arg);
    ESP_LOGI(TAG, "USB Host library OK");

    while (true) {
        uint32_t event_flags = 0;
        // Timeout de 200ms (en vez de portMAX_DELAY) para poder atender la
        // bandera de limpieza aunque no llegue otro evento del host.
        err = usb_host_lib_handle_events(pdMS_TO_TICKS(200), &event_flags);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(err));
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        // CIRUGIA hot-plug: el MSC pone esta bandera al desconectarse el pen.
        // Liberamos el slot huerfano AQUI, en el daemon del host (contexto
        // correcto: desde el cliente MSC daba INVALID_STATE). free_all solo
        // libera devices sin clientes abiertos; el teclado/raton del HID estan
        // abiertos, asi que no se tocan. NOT_FINISHED = quedan clientes (normal).
        extern volatile int g_usb_msc_wants_free_all;
        if (g_usb_msc_wants_free_all) {
            g_usb_msc_wants_free_all = 0;
            esp_err_t fe = usb_host_device_free_all();
            if (fe == ESP_OK) {
                ESP_LOGI(TAG, "free_all (daemon): slot del pen liberado");
            } else if (fe != ESP_ERR_NOT_FINISHED) {
                ESP_LOGW(TAG, "free_all (daemon): %s", esp_err_to_name(fe));
            }
        }
    }
}

static void usb_kbd_app_task(void *arg)
{
    usb_kbd_event_t evt;
    ESP_LOGI(TAG, "USB HID app task lista");
    while (true) {
        if (xQueueReceive(s_usb_kbd_queue, &evt, portMAX_DELAY) == pdTRUE) {
            if (evt.event_group == APP_EVENT_HID_HOST) {
                usb_kbd_device_event(evt.hid_host_device.handle,
                                     evt.hid_host_device.event,
                                     evt.hid_host_device.arg);
            }
        }
    }
}

void usb_hid_keyboard_set_char_callback(usb_hid_keyboard_char_cb_t cb)
{
    s_char_cb = cb;
}

void usb_hid_mouse_set_callback(usb_hid_mouse_cb_t cb)
{
    s_mouse_cb = cb;
}

int usb_hid_keyboard_connected(void)
{
    return s_keyboard_connected;
}

int usb_hid_mouse_connected(void)
{
    return s_mouse_connected;
}

void usb_hid_mouse_get_state(int *x, int *y, uint8_t *buttons)
{
    if (x) *x = s_mouse_x;
    if (y) *y = s_mouse_y;
    if (buttons) *buttons = s_mouse_buttons;
}

void usb_hid_mouse_set_position(int x, int y)
{
    s_mouse_x = clamp_i(x, 0, ARIELO_MOUSE_W - 1);
    s_mouse_y = clamp_i(y, 0, ARIELO_MOUSE_H - 1);
    rgb_display_set_mouse_pointer(s_mouse_x * 2, s_mouse_y * 2, s_mouse_buttons, s_mouse_connected);
}

esp_err_t usb_hid_keyboard_init(void)
{
    if (s_hid_installed) return ESP_OK;

    ESP_LOGI(TAG, "Inicializando USB HID Keyboard+Mouse 05A Pointer...");
    printf("\r\n[05A_USB] Inicializando USB HID Keyboard+Mouse + puntero...\r\n");

    s_usb_kbd_queue = xQueueCreate(16, sizeof(usb_kbd_event_t));
    if (!s_usb_kbd_queue) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreatePinnedToCore(usb_kbd_host_lib_task, "usb_host_lib", 4096,
                                            xTaskGetCurrentTaskHandle(), 4, NULL, 0);
    if (ok != pdTRUE) return ESP_FAIL;

    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
    if (s_usb_host_ready < 0) return ESP_FAIL;

    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = usb_kbd_driver_callback,
        .callback_arg = NULL,
    };

    esp_err_t err = hid_host_install(&hid_cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "hid_host_install fallo: %s", esp_err_to_name(err));
        return err;
    }

    s_hid_installed = 1;

    ok = xTaskCreatePinnedToCore(usb_kbd_app_task, "usb_hid04a_app", 4096, NULL, 5, NULL, 0);
    if (ok != pdTRUE) return ESP_FAIL;

    printf("[04A_USB] Conecte teclado/mouse USB o receptor 2.4GHz\r\n");
    printf("[04A_USB] Para HUB externo: CONFIG_USB_HOST_HUBS_SUPPORTED=y\r\n");
    ESP_LOGI(TAG, "USB HID Keyboard+Mouse + puntero listo, esperando dispositivo");
    return ESP_OK;
}
