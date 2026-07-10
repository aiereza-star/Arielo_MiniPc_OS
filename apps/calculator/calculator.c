/*
 * calculator.c - Calculadora externa para Arielo MiniPC OS
 *
 * 01A_BASE_TOOLAPP:
 *   - App externa pura ELF Xtensa ESP32-S3.
 *   - Modo grafico estable SM_400X240.
 *   - UI de herramienta, no juego: visor grande + ayuda de teclas.
 *   - Dibuja solo cuando cambia el estado para evitar parpadeos.
 *   - Entrada por bt_keyboard_is_pressed(), puenteada en el SO a USB/BLE HID.
 *   - Q/ESC salen limpiamente y devuelve SM_TEXT.
 *
 * 01B_NO_RECT_SYMBOL:
 *   - Corrige carga ELF: no usa rgb_gfx_rect(), porque no esta
 *     exportada por el loader actual. Los bordes se dibujan con
 *     cuatro rgb_gfx_rectfill().
 *
 * 01C_MOUSE_TOUCH:
 *   - Anade entrada por click de raton USB HID.
 *   - Anade entrada touch GT911 usando el mismo patron del desktop:
 *     minipc_touch_ok() + minipc_touch_read(), coordenada 800x480 / 2.
 *   - Click izquierdo o dedo sobre botones = pulsa tecla grafica.
 *   - Boton derecho o boton ESC grafico = salir.
 *
 * 01H_COMPACT_MOUSE_TOUCH:
 *   - Reorganiza el keypad para que no quede cortado abajo en 400x240.
 *   - Quita la descripcion inferior que pisaba la ultima fila.
 *   - Mantiene raton USB + touch GT911 + teclado.
 *
 * Controles:
 *   0-9 / keypad 0-9 : introducir numeros
 *   . / keypad .     : decimal
 *   + - * /          : operaciones, preferible teclado numerico
 *   A/P              : sumar
 *   S/M              : restar
 *   X                : multiplicar
 *   D                : dividir
 *   ENTER / =        : calcular
 *   BACKSPACE        : borrar ultimo digito
 *   C                : limpiar
 *   N                : cambiar signo
 *   Q/ESC            : salir
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "rgb_gfx.h"
#include "rgb_display.h"
#include "bt_keyboard.h"
#include "usb_hid_keyboard_02d.h"
#include "minipc_touch_gt911.h"

#define SW 400
#define SH 240

#define COL_BG        0
#define COL_PANEL     1
#define COL_PANEL2    2
#define COL_BORDER    3
#define COL_TEXT      4
#define COL_TEXT_DIM  5
#define COL_WHITE     6
#define COL_ACCENT    7
#define COL_WARN      8
#define COL_GOOD      9
#define COL_KEY       10
#define COL_KEY2      11
#define COL_RED       12

#define MAX_IN 24

/* Teclas USB HID no incluidas en bt_keyboard.h original. */
#define BT_KEY_DOT          0x37
#define BT_KEY_SLASH        0x38
#define BT_KEY_MINUS        0x2D
#define BT_KEY_EQUAL        0x2E
#define BT_KEY_KP_DIV       0x54
#define BT_KEY_KP_MUL       0x55
#define BT_KEY_KP_MINUS     0x56
#define BT_KEY_KP_PLUS      0x57
#define BT_KEY_KP_ENTER     0x58
#define BT_KEY_KP_1         0x59
#define BT_KEY_KP_2         0x5A
#define BT_KEY_KP_3         0x5B
#define BT_KEY_KP_4         0x5C
#define BT_KEY_KP_5         0x5D
#define BT_KEY_KP_6         0x5E
#define BT_KEY_KP_7         0x5F
#define BT_KEY_KP_8         0x60
#define BT_KEY_KP_9         0x61
#define BT_KEY_KP_0         0x62
#define BT_KEY_KP_DOT       0x63

static uint8_t prev_keys[32];

static char input[MAX_IN];
static char status_line[64];
static double acc;
static char pending_op;
static int entering;
static int just_result;
static int error_state;
static int redraw_count;
static uint8_t prev_pointer_buttons;
static int last_pointer_x = -1;
static int last_pointer_y = -1;

static void setup_palette(void)
{
    static uint16_t pal[256];
    memset(pal, 0, sizeof(pal));
    pal[COL_BG]       = 0x0000;
    pal[COL_PANEL]    = 0x1082;
    pal[COL_PANEL2]   = 0x2104;
    pal[COL_BORDER]   = 0x5ACB;
    pal[COL_TEXT]     = 0xC618;
    pal[COL_TEXT_DIM] = 0x7BEF;
    pal[COL_WHITE]    = 0xFFFF;
    pal[COL_ACCENT]   = 0x07FF;
    pal[COL_WARN]     = 0xFFE0;
    pal[COL_GOOD]     = 0x07E0;
    pal[COL_KEY]      = 0x3186;
    pal[COL_KEY2]     = 0x4208;
    pal[COL_RED]      = 0xF800;
    rgb_display_set_vga_palette(pal);
}

static int key_down(uint8_t key)
{
    return bt_keyboard_is_pressed(key) ? 1 : 0;
}

static int key_edge(uint8_t key)
{
    uint8_t mask = (uint8_t)(1U << (key & 7));
    uint8_t *byte = &prev_keys[key >> 3];
    int now = key_down(key);
    int was = ((*byte) & mask) != 0;
    if (now) *byte |= mask;
    else     *byte &= (uint8_t)~mask;
    return now && !was;
}

static int shift_down(void)
{
    return (bt_keyboard_get_modifiers() & BT_MOD_SHIFT) ? 1 : 0;
}

static void sync_start_keys(void)
{
    memset(prev_keys, 0, sizeof(prev_keys));
    for (int k = 0; k < 128; k++) {
        if (key_down((uint8_t)k)) {
            prev_keys[k >> 3] |= (uint8_t)(1U << (k & 7));
        }
    }
}

/* Fuente 5x7 basica, column-major. */
static uint8_t font_col(char c, int col)
{
    if (col < 0 || col >= 5) return 0;
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    switch (c) {
        case '0': { static const uint8_t g[5]={0x3E,0x51,0x49,0x45,0x3E}; return g[col]; }
        case '1': { static const uint8_t g[5]={0x00,0x42,0x7F,0x40,0x00}; return g[col]; }
        case '2': { static const uint8_t g[5]={0x42,0x61,0x51,0x49,0x46}; return g[col]; }
        case '3': { static const uint8_t g[5]={0x21,0x41,0x45,0x4B,0x31}; return g[col]; }
        case '4': { static const uint8_t g[5]={0x18,0x14,0x12,0x7F,0x10}; return g[col]; }
        case '5': { static const uint8_t g[5]={0x27,0x45,0x45,0x45,0x39}; return g[col]; }
        case '6': { static const uint8_t g[5]={0x3C,0x4A,0x49,0x49,0x30}; return g[col]; }
        case '7': { static const uint8_t g[5]={0x01,0x71,0x09,0x05,0x03}; return g[col]; }
        case '8': { static const uint8_t g[5]={0x36,0x49,0x49,0x49,0x36}; return g[col]; }
        case '9': { static const uint8_t g[5]={0x06,0x49,0x49,0x29,0x1E}; return g[col]; }
        case 'A': { static const uint8_t g[5]={0x7E,0x11,0x11,0x11,0x7E}; return g[col]; }
        case 'B': { static const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x36}; return g[col]; }
        case 'C': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x22}; return g[col]; }
        case 'D': { static const uint8_t g[5]={0x7F,0x41,0x41,0x22,0x1C}; return g[col]; }
        case 'E': { static const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x41}; return g[col]; }
        case 'F': { static const uint8_t g[5]={0x7F,0x09,0x09,0x09,0x01}; return g[col]; }
        case 'G': { static const uint8_t g[5]={0x3E,0x41,0x49,0x49,0x7A}; return g[col]; }
        case 'H': { static const uint8_t g[5]={0x7F,0x08,0x08,0x08,0x7F}; return g[col]; }
        case 'I': { static const uint8_t g[5]={0x00,0x41,0x7F,0x41,0x00}; return g[col]; }
        case 'J': { static const uint8_t g[5]={0x20,0x40,0x41,0x3F,0x01}; return g[col]; }
        case 'K': { static const uint8_t g[5]={0x7F,0x08,0x14,0x22,0x41}; return g[col]; }
        case 'L': { static const uint8_t g[5]={0x7F,0x40,0x40,0x40,0x40}; return g[col]; }
        case 'M': { static const uint8_t g[5]={0x7F,0x02,0x0C,0x02,0x7F}; return g[col]; }
        case 'N': { static const uint8_t g[5]={0x7F,0x04,0x08,0x10,0x7F}; return g[col]; }
        case 'O': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x3E}; return g[col]; }
        case 'P': { static const uint8_t g[5]={0x7F,0x09,0x09,0x09,0x06}; return g[col]; }
        case 'Q': { static const uint8_t g[5]={0x3E,0x41,0x51,0x21,0x5E}; return g[col]; }
        case 'R': { static const uint8_t g[5]={0x7F,0x09,0x19,0x29,0x46}; return g[col]; }
        case 'S': { static const uint8_t g[5]={0x46,0x49,0x49,0x49,0x31}; return g[col]; }
        case 'T': { static const uint8_t g[5]={0x01,0x01,0x7F,0x01,0x01}; return g[col]; }
        case 'U': { static const uint8_t g[5]={0x3F,0x40,0x40,0x40,0x3F}; return g[col]; }
        case 'V': { static const uint8_t g[5]={0x1F,0x20,0x40,0x20,0x1F}; return g[col]; }
        case 'W': { static const uint8_t g[5]={0x3F,0x40,0x38,0x40,0x3F}; return g[col]; }
        case 'X': { static const uint8_t g[5]={0x63,0x14,0x08,0x14,0x63}; return g[col]; }
        case 'Y': { static const uint8_t g[5]={0x07,0x08,0x70,0x08,0x07}; return g[col]; }
        case 'Z': { static const uint8_t g[5]={0x61,0x51,0x49,0x45,0x43}; return g[col]; }
        case ':': { static const uint8_t g[5]={0x00,0x36,0x36,0x00,0x00}; return g[col]; }
        case '.': { static const uint8_t g[5]={0x00,0x60,0x60,0x00,0x00}; return g[col]; }
        case ',': { static const uint8_t g[5]={0x00,0x40,0x30,0x00,0x00}; return g[col]; }
        case '-': { static const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; return g[col]; }
        case '+': { static const uint8_t g[5]={0x08,0x08,0x3E,0x08,0x08}; return g[col]; }
        case '*': { static const uint8_t g[5]={0x14,0x08,0x3E,0x08,0x14}; return g[col]; }
        case '/': { static const uint8_t g[5]={0x20,0x10,0x08,0x04,0x02}; return g[col]; }
        case '=': { static const uint8_t g[5]={0x14,0x14,0x14,0x14,0x14}; return g[col]; }
        case '<': { static const uint8_t g[5]={0x08,0x14,0x22,0x41,0x00}; return g[col]; }
        case '>': { static const uint8_t g[5]={0x41,0x22,0x14,0x08,0x00}; return g[col]; }
        case '_': { static const uint8_t g[5]={0x40,0x40,0x40,0x40,0x40}; return g[col]; }
        case '[': { static const uint8_t g[5]={0x00,0x7F,0x41,0x41,0x00}; return g[col]; }
        case ']': { static const uint8_t g[5]={0x00,0x41,0x41,0x7F,0x00}; return g[col]; }
        default: return 0;
    }
}

static int text_width(const char *s, int scale)
{
    int w = 0;
    while (*s) {
        if (*s == ' ') w += 4 * scale;
        else w += 6 * scale;
        s++;
    }
    return w;
}

static void draw_text(int x, int y, const char *s, uint8_t color, int scale)
{
    int px0 = x;
    while (*s) {
        char c = *s++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c == '\n') { y += 9 * scale; px0 = x; continue; }
        if (c == ' ') { px0 += 4 * scale; continue; }
        for (int col = 0; col < 5; col++) {
            uint8_t bits = font_col(c, col);
            for (int row = 0; row < 7; row++) {
                if (bits & (1U << row)) {
                    rgb_gfx_rectfill(px0 + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        px0 += 6 * scale;
    }
}

static void draw_text_center(int x, int y, int w, const char *s, uint8_t color, int scale)
{
    int tw = text_width(s, scale);
    int tx = x + (w - tw) / 2;
    if (tx < x + 2) tx = x + 2;
    draw_text(tx, y, s, color, scale);
}

static void utoa_simple(unsigned long long v, char *out)
{
    char tmp[24];
    int n = 0;
    if (v == 0) {
        out[0] = '0'; out[1] = 0;
        return;
    }
    while (v > 0 && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10ULL));
        v /= 10ULL;
    }
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    out[n] = 0;
}

static void format_double(double v, char *out, int outsz)
{
    if (outsz <= 0) return;
    out[0] = 0;
    if (v != v) { strncpy(out, "NAN", outsz - 1); out[outsz - 1] = 0; return; }

    int neg = 0;
    if (v < 0.0) { neg = 1; v = -v; }

    if (v > 999999999999.0) {
        strncpy(out, "OVERFLOW", outsz - 1);
        out[outsz - 1] = 0;
        return;
    }

    unsigned long long ip = (unsigned long long)v;
    double fd = (v - (double)ip) * 1000000.0 + 0.5;
    unsigned long frac = (unsigned long)fd;
    if (frac >= 1000000UL) {
        ip++;
        frac -= 1000000UL;
    }

    char ibuf[32];
    utoa_simple(ip, ibuf);

    int p = 0;
    if (neg && p < outsz - 1) out[p++] = '-';
    for (int i = 0; ibuf[i] && p < outsz - 1; i++) out[p++] = ibuf[i];

    if (frac != 0 && p < outsz - 1) {
        char fbuf[8];
        for (int i = 5; i >= 0; i--) {
            fbuf[i] = (char)('0' + (frac % 10UL));
            frac /= 10UL;
        }
        fbuf[6] = 0;
        int last = 5;
        while (last >= 0 && fbuf[last] == '0') last--;
        if (last >= 0 && p < outsz - 1) out[p++] = '.';
        for (int i = 0; i <= last && p < outsz - 1; i++) out[p++] = fbuf[i];
    }
    out[p] = 0;
}

static double parse_input(const char *s)
{
    int neg = 0;
    double v = 0.0;
    double div = 10.0;
    int frac = 0;

    if (*s == '-') { neg = 1; s++; }
    while (*s) {
        if (*s >= '0' && *s <= '9') {
            int d = *s - '0';
            if (!frac) v = v * 10.0 + (double)d;
            else { v += (double)d / div; div *= 10.0; }
        } else if (*s == '.') {
            frac = 1;
        }
        s++;
    }
    return neg ? -v : v;
}

static void set_status(const char *s)
{
    strncpy(status_line, s, sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = 0;
}

static void calc_clear(void)
{
    strcpy(input, "0");
    acc = 0.0;
    pending_op = 0;
    entering = 0;
    just_result = 0;
    error_state = 0;
    set_status("READY");
}

static void calc_set_error(const char *msg)
{
    strncpy(input, msg, MAX_IN - 1);
    input[MAX_IN - 1] = 0;
    pending_op = 0;
    entering = 0;
    just_result = 0;
    error_state = 1;
    set_status("ERROR - C PARA LIMPIAR");
}

static int input_has_dot(void)
{
    for (int i = 0; input[i]; i++) if (input[i] == '.') return 1;
    return 0;
}

static void input_digit(char d)
{
    if (error_state) calc_clear();
    if (just_result && !pending_op) {
        strcpy(input, "0");
        just_result = 0;
    }
    if (!entering) {
        strcpy(input, "0");
        entering = 1;
    }

    int len = (int)strlen(input);
    if (len >= MAX_IN - 1) return;

    if (strcmp(input, "0") == 0) {
        input[0] = d;
        input[1] = 0;
    } else if (strcmp(input, "-0") == 0) {
        input[1] = d;
        input[2] = 0;
    } else {
        input[len] = d;
        input[len + 1] = 0;
    }
}

static void input_dot(void)
{
    if (error_state) calc_clear();
    if (just_result && !pending_op) {
        strcpy(input, "0");
        just_result = 0;
    }
    if (!entering) {
        strcpy(input, "0");
        entering = 1;
    }
    if (input_has_dot()) return;
    int len = (int)strlen(input);
    if (len < MAX_IN - 1) {
        input[len] = '.';
        input[len + 1] = 0;
    }
}

static void input_backspace(void)
{
    if (error_state) { calc_clear(); return; }
    if (!entering) return;
    int len = (int)strlen(input);
    if (len <= 1) {
        strcpy(input, "0");
        entering = 0;
        return;
    }
    input[len - 1] = 0;
    if (strcmp(input, "-") == 0) {
        strcpy(input, "0");
        entering = 0;
    }
}

static void input_toggle_sign(void)
{
    if (error_state) calc_clear();
    if (strcmp(input, "0") == 0) {
        strcpy(input, "-0");
        entering = 1;
        return;
    }
    if (input[0] == '-') {
        memmove(input, input + 1, strlen(input));
    } else {
        int len = (int)strlen(input);
        if (len < MAX_IN - 1) {
            memmove(input + 1, input, (size_t)len + 1);
            input[0] = '-';
        }
    }
    entering = 1;
}

static int compute(double a, double b, char op, double *out)
{
    switch (op) {
        case '+': *out = a + b; return 1;
        case '-': *out = a - b; return 1;
        case '*': *out = a * b; return 1;
        case '/':
            if (b > -0.0000000001 && b < 0.0000000001) return 0;
            *out = a / b;
            return 1;
        default:
            *out = b;
            return 1;
    }
}

static void refresh_status_op(void)
{
    char abuf[MAX_IN];
    format_double(acc, abuf, sizeof(abuf));
    status_line[0] = 0;
    strncpy(status_line, "ANS ", sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = 0;
    strncat(status_line, abuf, sizeof(status_line) - strlen(status_line) - 1);
    if (pending_op) {
        char opbuf[4];
        opbuf[0] = ' ';
        opbuf[1] = pending_op;
        opbuf[2] = 0;
        strncat(status_line, opbuf, sizeof(status_line) - strlen(status_line) - 1);
    }
}

static void op_pressed(char op)
{
    if (error_state) return;

    if (pending_op && !entering) {
        pending_op = op;
        refresh_status_op();
        return;
    }

    double cur = parse_input(input);
    if (pending_op) {
        double r;
        if (!compute(acc, cur, pending_op, &r)) {
            calc_set_error("DIV ZERO");
            return;
        }
        acc = r;
    } else {
        acc = cur;
    }

    pending_op = op;
    entering = 0;
    just_result = 0;
    format_double(acc, input, MAX_IN);
    if (strcmp(input, "OVERFLOW") == 0) {
        calc_set_error("OVERFLOW");
        return;
    }
    refresh_status_op();
}

static void enter_pressed(void)
{
    if (error_state) return;

    if (pending_op) {
        double cur = parse_input(input);
        double r;
        if (!compute(acc, cur, pending_op, &r)) {
            calc_set_error("DIV ZERO");
            return;
        }
        acc = r;
        pending_op = 0;
        entering = 0;
        just_result = 1;
        format_double(acc, input, MAX_IN);
        if (strcmp(input, "OVERFLOW") == 0) {
            calc_set_error("OVERFLOW");
            return;
        }
        set_status("RESULT");
    } else {
        acc = parse_input(input);
        entering = 0;
        just_result = 1;
        set_status("READY");
    }
}

static char read_digit_edge(void)
{
    int shifted = shift_down();
    if (!shifted) {
        if (key_edge(BT_KEY_0)) return '0';
        if (key_edge(BT_KEY_1)) return '1';
        if (key_edge(BT_KEY_2)) return '2';
        if (key_edge(BT_KEY_3)) return '3';
        if (key_edge(BT_KEY_4)) return '4';
        if (key_edge(BT_KEY_5)) return '5';
        if (key_edge(BT_KEY_6)) return '6';
        if (key_edge(BT_KEY_7)) return '7';
        if (key_edge(BT_KEY_8)) return '8';
        if (key_edge(BT_KEY_9)) return '9';
    }

    if (key_edge(BT_KEY_KP_0)) return '0';
    if (key_edge(BT_KEY_KP_1)) return '1';
    if (key_edge(BT_KEY_KP_2)) return '2';
    if (key_edge(BT_KEY_KP_3)) return '3';
    if (key_edge(BT_KEY_KP_4)) return '4';
    if (key_edge(BT_KEY_KP_5)) return '5';
    if (key_edge(BT_KEY_KP_6)) return '6';
    if (key_edge(BT_KEY_KP_7)) return '7';
    if (key_edge(BT_KEY_KP_8)) return '8';
    if (key_edge(BT_KEY_KP_9)) return '9';
    return 0;
}

static char read_op_edge(void)
{
    int sh = shift_down();

    if (key_edge(BT_KEY_KP_PLUS))  return '+';
    if (key_edge(BT_KEY_KP_MINUS)) return '-';
    if (key_edge(BT_KEY_KP_MUL))   return '*';
    if (key_edge(BT_KEY_KP_DIV))   return '/';

    if (key_edge(BT_KEY_A) || key_edge(BT_KEY_P)) return '+';
    if (key_edge(BT_KEY_S) || key_edge(BT_KEY_M)) return '-';
    if (key_edge(BT_KEY_X)) return '*';
    if (key_edge(BT_KEY_D)) return '/';

    if (key_edge(BT_KEY_MINUS)) return '-';
    if (key_edge(BT_KEY_SLASH)) return '/';

    if (sh && key_edge(BT_KEY_EQUAL)) return '+';
    if (sh && key_edge(BT_KEY_8)) return '*';

    return 0;
}

static int read_enter_edge(void)
{
    if (key_edge(BT_KEY_ENTER)) return 1;
    if (key_edge(BT_KEY_KP_ENTER)) return 1;
    if (!shift_down() && key_edge(BT_KEY_EQUAL)) return 1;
    return 0;
}

static int read_dot_edge(void)
{
    if (key_edge(BT_KEY_DOT)) return 1;
    if (key_edge(BT_KEY_KP_DOT)) return 1;
    return 0;
}

static void draw_box(int x, int y, int w, int h, uint8_t fill, uint8_t border)
{
    /*
     * IMPORTANTE PARA ARIELO MINIPC OS APPS:
     * rgb_gfx_rect() aparece en algunos headers antiguos, pero en la
     * base actual no esta exportada al ELF loader. Si la usamos, falla
     * la carga con: Can't find symbol rgb_gfx_rect / relocate -88.
     *
     * Por eso el borde se dibuja solo con rgb_gfx_rectfill(), que si
     * esta exportada y ya la usan snake/pong/breakout/tetris.
     */
    rgb_gfx_rectfill(x, y, w, h, fill);

    if (w <= 0 || h <= 0) return;

    rgb_gfx_rectfill(x,         y,         w, 1, border);      /* arriba */
    rgb_gfx_rectfill(x,         y + h - 1, w, 1, border);      /* abajo */
    rgb_gfx_rectfill(x,         y,         1, h, border);      /* izquierda */
    rgb_gfx_rectfill(x + w - 1, y,         1, h, border);      /* derecha */
}


static int inside_rect(int px, int py, int x, int y, int w, int h)
{
    return (px >= x && px < x + w && py >= y && py < y + h);
}

static void handle_touch_as_pointer(int *mx, int *my, uint8_t *buttons)
{
    int16_t tx = 0;
    int16_t ty = 0;

    if (!mx || !my || !buttons) return;

    /*
     * Mismo criterio que el escritorio Arielo MiniPC OS:
     * GT911 entrega 800x480 fisicos y las apps SM_400X240 trabajan a 400x240,
     * asi que dividimos entre 2 y convertimos dedo en click izquierdo.
     */
    if (minipc_touch_ok() && minipc_touch_read(&tx, &ty)) {
        *mx = (int)tx / 2;
        *my = (int)ty / 2;

        if (*mx < 0) *mx = 0;
        if (*mx >= SW) *mx = SW - 1;
        if (*my < 0) *my = 0;
        if (*my >= SH) *my = SH - 1;

        *buttons |= 0x01;  /* dedo = click izquierdo */
        usb_hid_mouse_set_position(*mx, *my);
    }
}

static char keypad_action_at(int mx, int my)
{
    static const char action[5][4] = {
        {'7', '8', '9', '/'},
        {'4', '5', '6', '*'},
        {'1', '2', '3', '-'},
        {'0', '.', '=', '+'},
        {'C', 'B', 'N', 'Q'}
    };

    const int x0 = 20;
    const int y0 = 108;
    const int bw = 62;
    const int bh = 20;
    const int gap = 4;

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            int x = x0 + c * (bw + gap);
            int y = y0 + r * (bh + gap);
            if (inside_rect(mx, my, x, y, bw, bh)) {
                return action[r][c];
            }
        }
    }
    return 0;
}

static int apply_calc_action(char a)
{
    if (!a) return 0;

    if (a >= '0' && a <= '9') {
        input_digit(a);
        return 1;
    }

    switch (a) {
        case '.': input_dot();         return 1;
        case '+': op_pressed('+');     return 1;
        case '-': op_pressed('-');     return 1;
        case '*': op_pressed('*');     return 1;
        case '/': op_pressed('/');     return 1;
        case '=': enter_pressed();     return 1;
        case 'C': calc_clear();        return 1;
        case 'B': input_backspace();   return 1;
        case 'N': input_toggle_sign(); return 1;
        default: return 0;
    }
}

static char read_pointer_action_edge(void)
{
    int mx = 0;
    int my = 0;
    uint8_t buttons = 0;

    usb_hid_mouse_get_state(&mx, &my, &buttons);
    handle_touch_as_pointer(&mx, &my, &buttons);

    if (mx != last_pointer_x || my != last_pointer_y) {
        last_pointer_x = mx;
        last_pointer_y = my;
    }

    int left_edge  = ((buttons & 0x01) != 0) && ((prev_pointer_buttons & 0x01) == 0);
    int right_edge = ((buttons & 0x02) != 0) && ((prev_pointer_buttons & 0x02) == 0);
    prev_pointer_buttons = buttons;

    if (right_edge) return 'Q';
    if (left_edge) return keypad_action_at(mx, my);
    return 0;
}

static void draw_button(int x, int y, int w, int h, const char *label, uint8_t fill, uint8_t text)
{
    draw_box(x, y, w, h, fill, COL_BORDER);
    int scale = 2;
    if ((int)strlen(label) > 2) scale = 1;
    int ty = y + (h - 7 * scale) / 2;
    draw_text_center(x, ty, w, label, text, scale);
}

static void draw_keypad(void)
{
    static const char *lbl[5][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"0", ".", "=", "+"},
        {"C", "BS", "N", "ESC"}
    };

    int x0 = 20;
    int y0 = 108;
    int bw = 62;
    int bh = 20;
    int gap = 4;

    for (int r = 0; r < 5; r++) {
        for (int c = 0; c < 4; c++) {
            uint8_t fill = COL_KEY;
            uint8_t txt = COL_WHITE;
            if (c == 3 || strcmp(lbl[r][c], "=") == 0) fill = COL_KEY2;
            if (strcmp(lbl[r][c], "C") == 0 || strcmp(lbl[r][c], "ESC") == 0) txt = COL_WARN;
            draw_button(x0 + c * (bw + gap), y0 + r * (bh + gap), bw, bh, lbl[r][c], fill, txt);
        }
    }

    draw_text(294, 112, "RATON/TOUCH", COL_ACCENT, 1);
    draw_text(294, 126, "IZQ PULSA", COL_TEXT_DIM, 1);
    draw_text(294, 138, "DER SALE", COL_TEXT_DIM, 1);
    draw_text(294, 154, "A/P  +", COL_TEXT_DIM, 1);
    draw_text(294, 166, "S/M  -", COL_TEXT_DIM, 1);
    draw_text(294, 178, "X    *", COL_TEXT_DIM, 1);
    draw_text(294, 190, "D    /", COL_TEXT_DIM, 1);
    draw_text(294, 206, "Q/ESC SALE", COL_TEXT_DIM, 1);
}

static void draw_ui(void)
{
    rgb_gfx_clear(COL_BG);

    draw_box(8, 6, 384, 24, COL_PANEL2, COL_BORDER);
    draw_text(18, 14, "ARIELO MINIPC OS", COL_ACCENT, 1);
    draw_text(286, 14, "CALC 01H", COL_WHITE, 1);

    draw_box(20, 38, 360, 48, COL_PANEL, COL_BORDER);

    int scale = 3;
    int tw = text_width(input, scale);
    if (tw > 330) { scale = 2; tw = text_width(input, scale); }
    if (tw > 330) { scale = 1; tw = text_width(input, scale); }
    int tx = 365 - tw;
    if (tx < 30) tx = 30;
    int ty = 50;
    if (scale == 2) ty = 55;
    if (scale == 1) ty = 60;
    draw_text(tx, ty, input, error_state ? COL_RED : COL_WHITE, scale);

    draw_box(20, 90, 360, 16, COL_PANEL2, COL_BORDER);
    draw_text(28, 95, status_line, error_state ? COL_WARN : COL_TEXT, 1);

    draw_keypad();

}

int main(void)
{
    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("Calculator: no pudo entrar en SM_400X240\n");
        return 1;
    }

    setup_palette();
    calc_clear();
    sync_start_keys();
    draw_ui();

    int running = 1;
    while (running) {
        int dirty = 0;

        char paction = read_pointer_action_edge();
        if (paction == 'Q') {
            running = 0;
            break;
        }
        if (paction) {
            if (apply_calc_action(paction)) dirty = 1;
        }

        if (!dirty && (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q))) {
            running = 0;
            break;
        }

        char op = dirty ? 0 : read_op_edge();
        if (!dirty) {
            if (op) {
                op_pressed(op);
                dirty = 1;
            } else if (read_enter_edge()) {
                enter_pressed();
                dirty = 1;
            } else if (key_edge(BT_KEY_C)) {
                calc_clear();
                dirty = 1;
            } else if (key_edge(BT_KEY_BACKSPACE)) {
                input_backspace();
                dirty = 1;
            } else if (key_edge(BT_KEY_N)) {
                input_toggle_sign();
                dirty = 1;
            } else if (read_dot_edge()) {
                input_dot();
                dirty = 1;
            } else {
                char d = read_digit_edge();
                if (d) {
                    input_digit(d);
                    dirty = 1;
                }
            }
        }

        if (dirty) {
            redraw_count++;
            draw_ui();
        }

        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    printf("Calculator 01C: salida limpia, redraws=%d\n", redraw_count);
    return 0;
}
