/*
 * sheet.c - Mini hoja de calculo externa para Arielo MiniPC OS
 *
 * SHEET_01F_CENTER_LOADLIST_MATHFIX
 *
 * Concepto:
 *   - Hoja retro estilo SuperCalc / Lotus 1-2-3 / MS-DOS.
 *   - App externa, sin tocar el SO base.
 *   - Chasis probado de Calculator / Notepad:
 *       rgb_display_set_mode(SM_400X240)
 *       setup_palette()
 *       rgb_gfx_clear()
 *       rgb_gfx_rectfill()
 *       rgb_display_wait_vsync()
 *   - Entrada: teclado BLE/USB via bt_keyboard_is_pressed(), raton USB y touch.
 *   - Salida limpia: SM_TEXT.
 *
 * SHEET 01F:
 *   - 12 columnas A-L
 *   - 32 filas
 *   - ventana visible de 5 columnas x 10 filas
 *   - edicion de celda
 *   - texto/numeros
 *   - formulas basicas:
 *       =A1+B1
 *       =A1-B1
 *       =A1*B1
 *       =A1/B1
 *       tambien permite numeros: =A1+5
 *   - guarda/carga CSV simple en /root/sheet.csv
 *
 * Controles:
 *   Flechas        : mover celda
 *   ENTER / F2     : editar celda
 *   Escribir texto : empieza edicion
 *   BACKSPACE      : borrar celda / borrar caracter en edicion
 *   TAB            : siguiente celda
 *   Ctrl+S         : guardar /root/sheet.csv
 *   Ctrl+O         : cargar /root/sheet.csv
 *   Ctrl+N         : nueva hoja
 *   ESC / Q        : salir o cancelar edicion
 *
 * Botones:
 *   NEW LOAD SAVE EDIT EXIT
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>

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
#define COL_SEL       13
#define COL_GRID      14
#define COL_FORMULA   15

#define SHEET_ROWS    32
#define SHEET_COLS    12
#define CELL_LEN      24

#define VISIBLE_COLS  5
#define VISIBLE_ROWS  10

#define GRID_X        20
#define GRID_Y        60
#define ROW_H         13
#define COL_W         66
#define ROW_HEAD_W    30

#define PATH_DEFAULT  "/root/sheet.csv"
#define CSV_MAX_FILES 8
#define CSV_NAME_LEN  36
#define SHEET_SCALE   1000
#define SHEET_MAXVAL  2000000000

#define MODE_NAV      0
#define MODE_EDIT     1
#define MODE_FORM     2
#define MODE_FILE     3

#define BT_KEY_F2     0x3B
#define BT_KEY_F4     0x3D
#define BT_KEY_F9     0x42

/* Teclas USB HID no incluidas en algunas cabeceras. */
#define BT_KEY_DOT          0x37
#define BT_KEY_SLASH        0x38
#define BT_KEY_MINUS        0x2D
#define BT_KEY_EQUAL        0x2E
#define BT_KEY_LBRACKET     0x2F
#define BT_KEY_RBRACKET     0x30
#define BT_KEY_BACKSLASH    0x31
#define BT_KEY_SEMICOLON    0x33
#define BT_KEY_APOSTROPHE   0x34
#define BT_KEY_GRAVE        0x35
#define BT_KEY_COMMA        0x36
#define BT_KEY_CAPSLOCK     0x39

static uint8_t prev_keys[32];
static uint8_t prev_pointer_buttons;
static int pointer_locked;
static int pointer_release_stable;
static int pointer_cooldown;
static int last_pointer_x = -1;
static int last_pointer_y = -1;

static char cells[SHEET_ROWS][SHEET_COLS][CELL_LEN];

static int cur_row;
static int cur_col;
static int row_scroll;
static int col_scroll;

static int ui_mode = MODE_NAV;
static char edit_buf[CELL_LEN];
static int edit_len;
static int dirty_sheet;
static int caps_lock;
static int key_layout_es = 1;      // 1=ES Espana, 0=US
static int sheet_decimals = 0;     // decimales visibles para resultados numericos

static int file_action;            // 0=SAVE, 1=LOAD
static int file_device;            // 0=/root, 1=/sdcard, 2=/usb
static char sheet_filename[40] = "sheet.csv";
static int sheet_filename_len;
static char file_csv_names[CSV_MAX_FILES][CSV_NAME_LEN];
static int file_csv_count;
static int file_csv_selected;

static int form_step;              // 0=celda1, 1=operacion, 2=celda2
static char form_a[8];
static char form_b[8];
static int form_a_len;
static int form_b_len;
static char form_op = '+';
static int form_sum_mode;

static char status_line[80] = "SHEET 01F listo";
static int redraw_count;

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
    pal[COL_SEL]      = 0x001F;
    pal[COL_GRID]     = 0x39E7;
    pal[COL_FORMULA]  = 0x2104;
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

static int ctrl_down(void)
{
    return (bt_keyboard_get_modifiers() & BT_MOD_CTRL) ? 1 : 0;
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

    switch (c) {
        case 'a': { static const uint8_t g[5]={0x20,0x54,0x54,0x54,0x78}; return g[col]; }
        case 'b': { static const uint8_t g[5]={0x7F,0x48,0x44,0x44,0x38}; return g[col]; }
        case 'c': { static const uint8_t g[5]={0x38,0x44,0x44,0x44,0x20}; return g[col]; }
        case 'd': { static const uint8_t g[5]={0x38,0x44,0x44,0x48,0x7F}; return g[col]; }
        case 'e': { static const uint8_t g[5]={0x38,0x54,0x54,0x54,0x18}; return g[col]; }
        case 'f': { static const uint8_t g[5]={0x08,0x7E,0x09,0x01,0x02}; return g[col]; }
        case 'g': { static const uint8_t g[5]={0x0C,0x52,0x52,0x52,0x3E}; return g[col]; }
        case 'h': { static const uint8_t g[5]={0x7F,0x08,0x04,0x04,0x78}; return g[col]; }
        case 'i': { static const uint8_t g[5]={0x00,0x44,0x7D,0x40,0x00}; return g[col]; }
        case 'j': { static const uint8_t g[5]={0x20,0x40,0x44,0x3D,0x00}; return g[col]; }
        case 'k': { static const uint8_t g[5]={0x7F,0x10,0x28,0x44,0x00}; return g[col]; }
        case 'l': { static const uint8_t g[5]={0x00,0x41,0x7F,0x40,0x00}; return g[col]; }
        case 'm': { static const uint8_t g[5]={0x7C,0x04,0x18,0x04,0x78}; return g[col]; }
        case 'n': { static const uint8_t g[5]={0x7C,0x08,0x04,0x04,0x78}; return g[col]; }
        case 'o': { static const uint8_t g[5]={0x38,0x44,0x44,0x44,0x38}; return g[col]; }
        case 'p': { static const uint8_t g[5]={0x7C,0x14,0x14,0x14,0x08}; return g[col]; }
        case 'q': { static const uint8_t g[5]={0x08,0x14,0x14,0x18,0x7C}; return g[col]; }
        case 'r': { static const uint8_t g[5]={0x7C,0x08,0x04,0x04,0x08}; return g[col]; }
        case 's': { static const uint8_t g[5]={0x48,0x54,0x54,0x54,0x20}; return g[col]; }
        case 't': { static const uint8_t g[5]={0x04,0x3F,0x44,0x40,0x20}; return g[col]; }
        case 'u': { static const uint8_t g[5]={0x3C,0x40,0x40,0x20,0x7C}; return g[col]; }
        case 'v': { static const uint8_t g[5]={0x1C,0x20,0x40,0x20,0x1C}; return g[col]; }
        case 'w': { static const uint8_t g[5]={0x3C,0x40,0x30,0x40,0x3C}; return g[col]; }
        case 'x': { static const uint8_t g[5]={0x44,0x28,0x10,0x28,0x44}; return g[col]; }
        case 'y': { static const uint8_t g[5]={0x0C,0x50,0x50,0x50,0x3C}; return g[col]; }
        case 'z': { static const uint8_t g[5]={0x44,0x64,0x54,0x4C,0x44}; return g[col]; }

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

        case ' ': return 0;
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
        case '%': { static const uint8_t g[5]={0x63,0x13,0x08,0x64,0x63}; return g[col]; }
        case '#': { static const uint8_t g[5]={0x14,0x7F,0x14,0x7F,0x14}; return g[col]; }
        case '|': { static const uint8_t g[5]={0x00,0x00,0x7F,0x00,0x00}; return g[col]; }
        case '?': { static const uint8_t g[5]={0x02,0x01,0x51,0x09,0x06}; return g[col]; }
        case '!': { static const uint8_t g[5]={0x00,0x00,0x5F,0x00,0x00}; return g[col]; }
        case '$': { static const uint8_t g[5]={0x24,0x2A,0x7F,0x2A,0x12}; return g[col]; }
        case '"': { static const uint8_t g[5]={0x00,0x07,0x00,0x07,0x00}; return g[col]; }
        case '\'': { static const uint8_t g[5]={0x00,0x05,0x03,0x00,0x00}; return g[col]; }
        case '(': { static const uint8_t g[5]={0x00,0x1C,0x22,0x41,0x00}; return g[col]; }
        case ')': { static const uint8_t g[5]={0x00,0x41,0x22,0x1C,0x00}; return g[col]; }
        default: return 0;
    }
}

static int text_width(const char *s, int scale)
{
    int w = 0;
    while (*s) {
        w += 6 * scale;
        s++;
    }
    return w;
}

static void draw_text(int x, int y, const char *s, uint8_t color, int scale)
{
    int px0 = x;

    while (*s) {
        char c = *s++;

        if (c == '\n') {
            y += 9 * scale;
            px0 = x;
            continue;
        }

        if (c == ' ') {
            px0 += 6 * scale;
            continue;
        }

        for (int col = 0; col < 5; col++) {
            uint8_t bits = font_col(c, col);
            for (int row = 0; row < 7; row++) {
                if (bits & (1U << row)) {
                    rgb_gfx_rectfill(px0 + col * scale, y + row * scale,
                                     scale, scale, color);
                }
            }
        }

        px0 += 6 * scale;
    }
}

static void draw_text_clip(int x, int y, const char *s, uint8_t color, int scale, int max_px)
{
    int px0 = x;
    int used = 0;

    while (*s && used + 6 * scale <= max_px) {
        char c = *s++;

        if (c == ' ') {
            px0 += 6 * scale;
            used += 6 * scale;
            continue;
        }

        for (int col = 0; col < 5; col++) {
            uint8_t bits = font_col(c, col);
            for (int row = 0; row < 7; row++) {
                if (bits & (1U << row)) {
                    rgb_gfx_rectfill(px0 + col * scale, y + row * scale,
                                     scale, scale, color);
                }
            }
        }

        px0 += 6 * scale;
        used += 6 * scale;
    }
}

static void draw_text_center(int x, int y, int w, const char *s, uint8_t color, int scale)
{
    int tw = text_width(s, scale);
    int tx = x + (w - tw) / 2;
    if (tx < x + 2) tx = x + 2;
    draw_text(tx, y, s, color, scale);
}

static void draw_box(int x, int y, int w, int h, uint8_t fill, uint8_t border)
{
    rgb_gfx_rectfill(x, y, w, h, fill);
    rgb_gfx_rectfill(x,         y,         w, 1, border);
    rgb_gfx_rectfill(x,         y + h - 1, w, 1, border);
    rgb_gfx_rectfill(x,         y,         1, h, border);
    rgb_gfx_rectfill(x + w - 1, y,         1, h, border);
}

static int inside_rect(int px, int py, int x, int y, int w, int h)
{
    return (px >= x && px < x + w && py >= y && py < y + h);
}

static void set_status(const char *s)
{
    strncpy(status_line, s ? s : "", sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = 0;
}

static int ascii_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

static int ascii_isalpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static char ascii_toupper_char(char c)
{
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}

static void skip_spaces(const char **pp)
{
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    *pp = p;
}

static int parse_int_simple(const char **pp, int *out)
{
    const char *p = *pp;
    int sign = 1;
    int val = 0;
    int any = 0;

    skip_spaces(&p);

    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    while (ascii_isdigit(*p)) {
        any = 1;
        val = val * 10 + (*p - '0');
        p++;
    }

    if (!any) return 0;

    *out = val * sign;
    *pp = p;
    return 1;
}

static int parse_cell_ref(const char **pp, int *row, int *col);
static int scaled_abs_safe(int v)
{
    if (v < 0) {
        if (v < -SHEET_MAXVAL) return SHEET_MAXVAL;
        return -v;
    }
    return v;
}

static int scaled_apply_sign(int v, int neg)
{
    if (v > SHEET_MAXVAL) v = SHEET_MAXVAL;
    return neg ? -v : v;
}

static int scaled_add_safe(int a, int b)
{
    if (b > 0 && a > SHEET_MAXVAL - b) return SHEET_MAXVAL;
    if (b < 0 && a < -SHEET_MAXVAL - b) return -SHEET_MAXVAL;
    return a + b;
}

static int scaled_sub_safe(int a, int b)
{
    if (b < 0 && a > SHEET_MAXVAL + b) return SHEET_MAXVAL;
    if (b > 0 && a < -SHEET_MAXVAL + b) return -SHEET_MAXVAL;
    return a - b;
}

static int scaled_mul_int_clamped(int a, int b)
{
    if (a < 0 || b < 0) return 0;
    if (a == 0 || b == 0) return 0;
    if (a > SHEET_MAXVAL / b) return SHEET_MAXVAL;
    return a * b;
}

static int scaled_mul_safe(int a, int b)
{
    int neg = 0;
    int ai = 0, af = 0;
    int bi = 0, bf = 0;
    int part1 = 0, part2 = 0, part3 = 0, part4 = 0;
    int res = 0;

    if (a < 0) { neg = !neg; a = scaled_abs_safe(a); }
    if (b < 0) { neg = !neg; b = scaled_abs_safe(b); }

    ai = a / SHEET_SCALE;
    af = a % SHEET_SCALE;
    bi = b / SHEET_SCALE;
    bf = b % SHEET_SCALE;

    // (ai*bi)*1000 + ai*bf + bi*af + (af*bf)/1000
    part1 = scaled_mul_int_clamped(ai, bi);
    part1 = scaled_mul_int_clamped(part1, SHEET_SCALE);
    part2 = scaled_mul_int_clamped(ai, bf);
    part3 = scaled_mul_int_clamped(bi, af);
    part4 = (af * bf) / SHEET_SCALE; // af,bf < 1000: seguro

    res = scaled_add_safe(part1, part2);
    res = scaled_add_safe(res, part3);
    res = scaled_add_safe(res, part4);

    return scaled_apply_sign(res, neg);
}

static int scaled_div_safe(int a, int b)
{
    int neg = 0;
    int q = 0;
    int rem = 0;
    int dec = 0;
    int res = 0;

    if (b == 0) return 0;

    if (a < 0) { neg = !neg; a = scaled_abs_safe(a); }
    if (b < 0) { neg = !neg; b = scaled_abs_safe(b); }

    q = a / b;
    rem = a % b;

    if (q > SHEET_MAXVAL / SHEET_SCALE) {
        return scaled_apply_sign(SHEET_MAXVAL, neg);
    }

    res = q * SHEET_SCALE;

    // Tres decimales sin usar 64 bits: long division base 10.
    for (int i = 0; i < 3; i++) {
        if (rem > SHEET_MAXVAL / 10) break;
        rem *= 10;
        dec = dec * 10 + (rem / b);
        rem = rem % b;
    }

    res = scaled_add_safe(res, dec);
    return scaled_apply_sign(res, neg);
}

static int eval_sum_range_text(const char *s, int depth, int *out);
static void draw_ui(int mx, int my);

static int parse_number_scaled(const char **pp, int *out_scaled)
{
    const char *p = *pp;
    int sign = 1;
    int ip = 0;
    int fp = 0;
    int fd = 0;
    int any = 0;

    skip_spaces(&p);

    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    while (ascii_isdigit(*p)) {
        any = 1;
        ip = ip * 10 + (*p - '0');
        p++;
    }

    if (*p == '.' || *p == ',') {
        p++;

        while (ascii_isdigit(*p) && fd < 3) {
            any = 1;
            fp = fp * 10 + (*p - '0');
            fd++;
            p++;
        }

        // Saltar decimales sobrantes sin usarlos.
        while (ascii_isdigit(*p)) p++;
    }

    if (!any) return 0;

    while (fd < 3) {
        fp *= 10;
        fd++;
    }

    *out_scaled = sign * (ip * 1000 + fp);
    *pp = p;
    return 1;
}

static void format_scaled_value(char *out, size_t out_sz, int scaled)
{
    int neg = 0;
    int ip = 0;
    int fp = 0;

    if (!out || out_sz == 0) return;

    if (scaled < 0) {
        neg = 1;
        scaled = -scaled;
    }

    ip = scaled / 1000;
    fp = scaled % 1000;

    if (sheet_decimals <= 0) {
        snprintf(out, out_sz, "%s%d", neg ? "-" : "", ip);
    } else if (sheet_decimals == 1) {
        snprintf(out, out_sz, "%s%d.%01d", neg ? "-" : "", ip, fp / 100);
    } else if (sheet_decimals == 2) {
        snprintf(out, out_sz, "%s%d.%02d", neg ? "-" : "", ip, fp / 10);
    } else {
        snprintf(out, out_sz, "%s%d.%03d", neg ? "-" : "", ip, fp);
    }
}

static int ref_text_valid(const char *s)
{
    const char *p = s;
    int r = 0;
    int c = 0;

    return parse_cell_ref(&p, &r, &c) && *p == 0;
}

static int operand_text_valid(const char *s)
{
    const char *p = s;
    int r = 0;
    int c = 0;
    int v = 0;

    if (!s || !s[0]) return 0;

    if (parse_cell_ref(&p, &r, &c) && *p == 0) {
        return 1;
    }

    p = s;
    if (parse_number_scaled(&p, &v)) {
        skip_spaces(&p);
        return *p == 0;
    }

    return 0;
}

static int parse_cell_ref(const char **pp, int *row, int *col)
{
    const char *p = *pp;
    int c = 0;
    int r = 0;

    skip_spaces(&p);

    if (!ascii_isalpha(*p)) return 0;

    c = ascii_toupper_char(*p) - 'A';
    p++;

    if (c < 0 || c >= SHEET_COLS) return 0;

    if (!ascii_isdigit(*p)) return 0;

    while (ascii_isdigit(*p)) {
        r = r * 10 + (*p - '0');
        p++;
    }

    r--;

    if (r < 0 || r >= SHEET_ROWS) return 0;

    *row = r;
    *col = c;
    *pp = p;
    return 1;
}

static int eval_cell(int row, int col, int depth, int *out);

static int parse_operand(const char **pp, int depth, int *out)
{
    const char *p = *pp;
    int r = 0;
    int c = 0;
    int v = 0;

    skip_spaces(&p);

    if (parse_cell_ref(&p, &r, &c)) {
        if (!eval_cell(r, c, depth + 1, &v)) return 0;
        *out = v;
        *pp = p;
        return 1;
    }

    if (parse_number_scaled(&p, &v)) {
        *out = v;
        *pp = p;
        return 1;
    }

    return 0;
}

static int eval_sum_range_text(const char *s, int depth, int *out)
{
    const char *p = s;
    int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
    int r_start = 0, r_end = 0, c_start = 0, c_end = 0;
    int total = 0;

    if (!s || !out) return 0;

    // Formato exacto sencillo: =SUM(A1:A10)
    if (!(p[0] == '=' &&
          (p[1] == 'S' || p[1] == 's') &&
          (p[2] == 'U' || p[2] == 'u') &&
          (p[3] == 'M' || p[3] == 'm') &&
          p[4] == '(')) {
        return 0;
    }

    p += 5;

    if (!parse_cell_ref(&p, &r1, &c1)) return 0;
    if (*p != ':') return 0;
    p++;
    if (!parse_cell_ref(&p, &r2, &c2)) return 0;
    if (*p != ')') return 0;
    p++;
    skip_spaces(&p);
    if (*p != 0) return 0;

    r_start = (r1 < r2) ? r1 : r2;
    r_end   = (r1 > r2) ? r1 : r2;
    c_start = (c1 < c2) ? c1 : c2;
    c_end   = (c1 > c2) ? c1 : c2;

    for (int r = r_start; r <= r_end; r++) {
        for (int c = c_start; c <= c_end; c++) {
            int v = 0;

            // Evitar autorreferencia simple: si no evalua, cuenta como 0.
            if (eval_cell(r, c, depth + 1, &v)) {
                total = scaled_add_safe(total, v);
            }
        }
    }

    *out = total;
    return 1;
}

static int eval_formula_text(const char *s, int depth, int *out)
{
    const char *p = s;
    int a = 0;
    int b = 0;
    char op = 0;

    if (!s || s[0] != '=') return 0;

    if (eval_sum_range_text(s, depth, out)) {
        return 1;
    }

    p++;

    if (!parse_operand(&p, depth, &a)) return 0;

    skip_spaces(&p);

    op = *p;

    if (op == 0) {
        *out = a;
        return 1;
    }

    if (!(op == '+' || op == '-' || op == '*' || op == '/')) return 0;

    p++;

    if (!parse_operand(&p, depth, &b)) return 0;

    skip_spaces(&p);

    if (*p != 0) return 0;

    switch (op) {
        case '+':
            *out = scaled_add_safe(a, b);
            return 1;
        case '-':
            *out = scaled_sub_safe(a, b);
            return 1;
        case '*':
            *out = scaled_mul_safe(a, b);
            return 1;
        case '/':
            if (b == 0) return 0;
            *out = scaled_div_safe(a, b);
            return 1;
        default:
            return 0;
    }
}

static int eval_cell(int row, int col, int depth, int *out)
{
    const char *s;

    if (depth > 8) return 0;
    if (row < 0 || row >= SHEET_ROWS || col < 0 || col >= SHEET_COLS) return 0;

    s = cells[row][col];

    if (!s[0]) {
        *out = 0;
        return 1;
    }

    if (s[0] == '=') {
        return eval_formula_text(s, depth, out);
    }

    {
        const char *p = s;
        int v = 0;
        if (!parse_number_scaled(&p, &v)) return 0;
        skip_spaces(&p);
        if (*p != 0) return 0;
        *out = v;
        return 1;
    }
}

static void cell_display(int row, int col, char *out, size_t out_sz)
{
    int v = 0;

    if (!out || out_sz == 0) return;

    out[0] = 0;

    if (row < 0 || row >= SHEET_ROWS || col < 0 || col >= SHEET_COLS) return;

    if (!cells[row][col][0]) return;

    if (cells[row][col][0] == '=') {
        if (eval_cell(row, col, 0, &v)) format_scaled_value(out, out_sz, v);
        else snprintf(out, out_sz, "ERR");
        return;
    }

    snprintf(out, out_sz, "%s", cells[row][col]);
}

static void cell_name(int row, int col, char *out, size_t out_sz)
{
    snprintf(out, out_sz, "%c%d", (char)('A' + col), row + 1);
}

static void ensure_visible(void)
{
    if (cur_col < col_scroll) col_scroll = cur_col;
    if (cur_col >= col_scroll + VISIBLE_COLS) col_scroll = cur_col - VISIBLE_COLS + 1;
    if (cur_row < row_scroll) row_scroll = cur_row;
    if (cur_row >= row_scroll + VISIBLE_ROWS) row_scroll = cur_row - VISIBLE_ROWS + 1;

    if (col_scroll < 0) col_scroll = 0;
    if (row_scroll < 0) row_scroll = 0;
    if (col_scroll > SHEET_COLS - VISIBLE_COLS) col_scroll = SHEET_COLS - VISIBLE_COLS;
    if (row_scroll > SHEET_ROWS - VISIBLE_ROWS) row_scroll = SHEET_ROWS - VISIBLE_ROWS;
}

static void move_cell(int dr, int dc)
{
    cur_row += dr;
    cur_col += dc;

    if (cur_row < 0) cur_row = 0;
    if (cur_row >= SHEET_ROWS) cur_row = SHEET_ROWS - 1;
    if (cur_col < 0) cur_col = 0;
    if (cur_col >= SHEET_COLS) cur_col = SHEET_COLS - 1;

    ensure_visible();
}

static void new_sheet(void)
{
    memset(cells, 0, sizeof(cells));
    cur_row = 0;
    cur_col = 0;
    row_scroll = 0;
    col_scroll = 0;
    dirty_sheet = 0;
    ui_mode = MODE_NAV;

    set_status("nueva hoja limpia");
}

static void cycle_decimals(void)
{
    sheet_decimals++;
    if (sheet_decimals > 3) sheet_decimals = 0;

    if (sheet_decimals == 0) set_status("decimales 0");
    else if (sheet_decimals == 1) set_status("decimales 1");
    else if (sheet_decimals == 2) set_status("decimales 2");
    else set_status("decimales 3");
}

static void begin_formula_builder(void)
{
    memset(form_a, 0, sizeof(form_a));
    memset(form_b, 0, sizeof(form_b));
    form_a_len = 0;
    form_b_len = 0;
    form_op = '+';
    form_sum_mode = 0;
    form_step = 0;
    ui_mode = MODE_FORM;
    set_status("FORM: op1 celda/num, ej A1");
}

static void begin_sum_builder(void)
{
    memset(form_a, 0, sizeof(form_a));
    memset(form_b, 0, sizeof(form_b));
    form_a_len = 0;
    form_b_len = 0;
    form_op = '+';
    form_sum_mode = 1;
    form_step = 0;
    ui_mode = MODE_FORM;
    set_status("SUM: primera celda del rango");
}

static void cancel_formula_builder(void)
{
    ui_mode = MODE_NAV;
    set_status("formula cancelada");
}

static void form_insert_char(char c)
{
    char *buf = NULL;
    int *len = NULL;
    int max_len = 0;

    if (c >= 'a' && c <= 'z') c = (char)(c - 32);

    if (form_step == 1) {
        if (c == '+' || c == '-' || c == '*' || c == '/') {
            form_op = c;
            form_step = 2;
            set_status("FORM: op2 celda/num, ej B1 o 7");
        }
        return;
    }

    if (!((c >= 'A' && c <= 'L') ||
          (c >= '0' && c <= '9') ||
          c == '.' || c == ',' || c == '-')) {
        return;
    }

    if (form_step == 0) {
        buf = form_a;
        len = &form_a_len;
        max_len = (int)sizeof(form_a) - 1;
    } else {
        buf = form_b;
        len = &form_b_len;
        max_len = (int)sizeof(form_b) - 1;
    }

    if (*len < max_len) {
        buf[*len] = c;
        (*len)++;
        buf[*len] = 0;
    }
}

static void form_backspace(void)
{
    if (form_step == 0 && form_a_len > 0) {
        form_a[--form_a_len] = 0;
    } else if (form_step == 2 && form_b_len > 0) {
        form_b[--form_b_len] = 0;
    }
}

static void form_next_or_commit(void)
{
    char formula[CELL_LEN];

    if (form_sum_mode) {
        if (form_step == 0) {
            if (!ref_text_valid(form_a)) {
                set_status("SUM: primera celda invalida");
                return;
            }
            form_step = 2;
            set_status("SUM: ultima celda del rango");
            return;
        }

        if (!ref_text_valid(form_b)) {
            set_status("SUM: ultima celda invalida");
            return;
        }

        snprintf(formula, sizeof(formula), "=SUM(%s:%s)", form_a, form_b);
        strncpy(cells[cur_row][cur_col], formula, CELL_LEN - 1);
        cells[cur_row][cur_col][CELL_LEN - 1] = 0;
        dirty_sheet = 1;
        ui_mode = MODE_NAV;
        set_status("sumatoria creada");
        return;
    }

    if (form_step == 0) {
        if (!operand_text_valid(form_a)) {
            set_status("FORM: operando 1 invalido");
            return;
        }
        form_step = 1;
        set_status("FORM: elige + - * /");
        return;
    }

    if (form_step == 1) {
        form_step = 2;
        set_status("FORM: op2 celda/num, ej B1 o 7");
        return;
    }

    if (!operand_text_valid(form_b)) {
        set_status("FORM: operando 2 invalido");
        return;
    }

    snprintf(formula, sizeof(formula), "=%s%c%s", form_a, form_op, form_b);
    strncpy(cells[cur_row][cur_col], formula, CELL_LEN - 1);
    cells[cur_row][cur_col][CELL_LEN - 1] = 0;
    dirty_sheet = 1;
    ui_mode = MODE_NAV;
    set_status("formula creada");
}

static void form_pick_cell(int row, int col)
{
    char name[8];

    cell_name(row, col, name, sizeof(name));

    if (form_step == 0) {
        strncpy(form_a, name, sizeof(form_a) - 1);
        form_a[sizeof(form_a) - 1] = 0;
        form_a_len = (int)strlen(form_a);

        if (form_sum_mode) {
            form_step = 2;
            set_status("SUM: ultima celda del rango");
        } else {
            form_step = 1;
            set_status("FORM: elige + - * /");
        }
    } else if (form_step == 2) {
        strncpy(form_b, name, sizeof(form_b) - 1);
        form_b[sizeof(form_b) - 1] = 0;
        form_b_len = (int)strlen(form_b);
        set_status(form_sum_mode ? "SUM: Enter para aceptar" : "FORM: Enter para aceptar");
    }
}

static void begin_edit(int clear_first, char first)
{
    memset(edit_buf, 0, sizeof(edit_buf));

    if (clear_first) {
        edit_len = 0;
    } else {
        strncpy(edit_buf, cells[cur_row][cur_col], CELL_LEN - 1);
        edit_len = (int)strlen(edit_buf);
    }

    if (first >= 32 && first <= 126 && edit_len < CELL_LEN - 1) {
        edit_buf[edit_len++] = first;
        edit_buf[edit_len] = 0;
    }

    ui_mode = MODE_EDIT;
    set_status("editando celda");
}

static void cancel_edit(void)
{
    ui_mode = MODE_NAV;
    set_status("edicion cancelada");
}

static void commit_edit(void)
{
    strncpy(cells[cur_row][cur_col], edit_buf, CELL_LEN - 1);
    cells[cur_row][cur_col][CELL_LEN - 1] = 0;
    ui_mode = MODE_NAV;
    dirty_sheet = 1;
    set_status("celda actualizada");
}

static void edit_insert_char(char c)
{
    if (c < 32 || c > 126) return;
    if (edit_len >= CELL_LEN - 1) return;

    edit_buf[edit_len++] = c;
    edit_buf[edit_len] = 0;
}

static void edit_backspace(void)
{
    if (edit_len <= 0) return;

    edit_len--;
    edit_buf[edit_len] = 0;
}

static char ascii_key_edge(void)
{
    int sh = shift_down();

    for (uint8_t k = BT_KEY_A; k <= BT_KEY_Z; k++) {
        if (key_edge(k)) {
            char c = (char)('a' + (k - BT_KEY_A));

            if ((caps_lock && !sh) || (!caps_lock && sh)) {
                c = (char)(c - 32);
            }

            return c;
        }
    }

    if (key_layout_es) {
        // Distribucion ES-Espana aproximada para los simbolos importantes.
        // Letras iguales; cambiamos fila numerica y teclas de simbolos.
        if (key_edge(BT_KEY_1)) return sh ? '!' : '1';
        if (key_edge(BT_KEY_2)) return sh ? '"' : '2';
        if (key_edge(BT_KEY_3)) return sh ? '#' : '3'; // en ES real suele ser punto medio
        if (key_edge(BT_KEY_4)) return sh ? '$' : '4';
        if (key_edge(BT_KEY_5)) return sh ? '%' : '5';
        if (key_edge(BT_KEY_6)) return sh ? '&' : '6';
        if (key_edge(BT_KEY_7)) return sh ? '/' : '7';
        if (key_edge(BT_KEY_8)) return sh ? '(' : '8';
        if (key_edge(BT_KEY_9)) return sh ? ')' : '9';
        if (key_edge(BT_KEY_0)) return sh ? '=' : '0';

        if (key_edge(BT_KEY_SPACE)) return ' ';
        if (key_edge(BT_KEY_DOT)) return sh ? ':' : '.';
        if (key_edge(BT_KEY_COMMA)) return sh ? ';' : ',';
        if (key_edge(BT_KEY_SLASH)) return sh ? '_' : '-';

        // Tecla a la derecha de P en muchos teclados ES: + / *
        if (key_edge(BT_KEY_RBRACKET)) return sh ? '*' : '+';

        // Teclas de signos de la fila superior ES.
        if (key_edge(BT_KEY_MINUS)) return sh ? '?' : '\'';
        if (key_edge(BT_KEY_EQUAL)) return sh ? '?' : '!';

        // Tecla ISO < >
        if (key_edge(BT_KEY_BACKSLASH)) return sh ? '>' : '<';

        if (key_edge(BT_KEY_LBRACKET)) return sh ? '^' : '`';
        if (key_edge(BT_KEY_SEMICOLON)) return sh ? 'N' : 'n'; // aproximacion para Ñ
        if (key_edge(BT_KEY_APOSTROPHE)) return sh ? '"' : '\'';
        if (key_edge(BT_KEY_GRAVE)) return sh ? '>' : '<';

        return 0;
    }

    // Mapa US original.
    if (key_edge(BT_KEY_1)) return sh ? '!' : '1';
    if (key_edge(BT_KEY_2)) return sh ? '@' : '2';
    if (key_edge(BT_KEY_3)) return sh ? '#' : '3';
    if (key_edge(BT_KEY_4)) return sh ? '$' : '4';
    if (key_edge(BT_KEY_5)) return sh ? '%' : '5';
    if (key_edge(BT_KEY_6)) return sh ? '^' : '6';
    if (key_edge(BT_KEY_7)) return sh ? '&' : '7';
    if (key_edge(BT_KEY_8)) return sh ? '*' : '8';
    if (key_edge(BT_KEY_9)) return sh ? '(' : '9';
    if (key_edge(BT_KEY_0)) return sh ? ')' : '0';

    if (key_edge(BT_KEY_SPACE)) return ' ';
    if (key_edge(BT_KEY_DOT)) return sh ? '>' : '.';
    if (key_edge(BT_KEY_COMMA)) return sh ? '<' : ',';
    if (key_edge(BT_KEY_SLASH)) return sh ? '?' : '/';
    if (key_edge(BT_KEY_MINUS)) return sh ? '_' : '-';
    if (key_edge(BT_KEY_EQUAL)) return sh ? '+' : '=';
    if (key_edge(BT_KEY_SEMICOLON)) return sh ? ':' : ';';
    if (key_edge(BT_KEY_APOSTROPHE)) return sh ? '"' : '\'';
    if (key_edge(BT_KEY_LBRACKET)) return sh ? '{' : '[';
    if (key_edge(BT_KEY_RBRACKET)) return sh ? '}' : ']';
    if (key_edge(BT_KEY_BACKSLASH)) return sh ? '|' : '\\';
    if (key_edge(BT_KEY_GRAVE)) return sh ? '~' : '`';

    return 0;
}

static const char *file_base_path(int dev)
{
    if (dev == 1) return "/sdcard";
    if (dev == 2) return "/usb";
    return "/root";
}

static const char *file_device_label(int dev)
{
    if (dev == 1) return "SD";
    if (dev == 2) return "USB";
    return "ROOT";
}

static int ascii_lower_char(int c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int name_ends_csv(const char *name)
{
    int n = 0;

    if (!name) return 0;

    n = (int)strlen(name);
    if (n < 5) return 0;

    return name[n - 4] == '.' &&
           ascii_lower_char(name[n - 3]) == 'c' &&
           ascii_lower_char(name[n - 2]) == 's' &&
           ascii_lower_char(name[n - 1]) == 'v';
}

static void file_select_csv_index(int idx)
{
    if (file_csv_count <= 0) return;

    if (idx < 0) idx = 0;
    if (idx >= file_csv_count) idx = file_csv_count - 1;

    file_csv_selected = idx;

    strncpy(sheet_filename, file_csv_names[idx], sizeof(sheet_filename) - 1);
    sheet_filename[sizeof(sheet_filename) - 1] = 0;
    sheet_filename_len = (int)strlen(sheet_filename);
}

static void file_scan_csvs(void)
{
    DIR *d = NULL;
    struct dirent *e = NULL;
    const char *base = file_base_path(file_device);

    file_csv_count = 0;
    file_csv_selected = 0;

    d = opendir(base);
    if (!d) {
        return;
    }

    while ((e = readdir(d)) != NULL && file_csv_count < CSV_MAX_FILES) {
        const char *name = e->d_name;

        if (!name || name[0] == '.') continue;
        if (!name_ends_csv(name)) continue;

        strncpy(file_csv_names[file_csv_count], name, CSV_NAME_LEN - 1);
        file_csv_names[file_csv_count][CSV_NAME_LEN - 1] = 0;
        file_csv_count++;
    }

    closedir(d);

    if (file_action && file_csv_count > 0) {
        file_select_csv_index(0);
    }
}

static int file_list_index_at(int mx, int my)
{
    int x = 84;
    int y = 147;
    int w = 286;
    int row_h = 9;

    if (!file_action) return -1;
    if (file_csv_count <= 0) return -1;

    if (mx < x || mx >= x + w) return -1;
    if (my < y || my >= y + row_h * CSV_MAX_FILES) return -1;

    int idx = (my - y) / row_h;
    if (idx < 0 || idx >= file_csv_count) return -1;

    return idx;
}

static void file_select_delta(int delta)
{
    if (!file_action || file_csv_count <= 0) return;

    file_select_csv_index(file_csv_selected + delta);
}

static int file_char_ok(char c)
{
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    if (c == '.' || c == '_' || c == '-') return 1;
    return 0;
}

static void normalize_filename_to(char *dst, size_t dst_sz, const char *src)
{
    int dot_seen = 0;
    int n = 0;

    if (!dst || dst_sz == 0) return;

    dst[0] = 0;

    if (!src || !src[0]) {
        snprintf(dst, dst_sz, "sheet.csv");
        return;
    }

    for (int i = 0; src[i] && n < (int)dst_sz - 1; i++) {
        char c = src[i];

        if (c == ' ') c = '_';

        if (!file_char_ok(c)) continue;

        if (c == '.') dot_seen = 1;

        dst[n++] = c;
    }

    dst[n] = 0;

    if (n == 0) {
        snprintf(dst, dst_sz, "sheet.csv");
        return;
    }

    if (!dot_seen && n < (int)dst_sz - 5) {
        dst[n++] = '.';
        dst[n++] = 'c';
        dst[n++] = 's';
        dst[n++] = 'v';
        dst[n] = 0;
    }
}

static void build_sheet_path(char *out, size_t out_sz)
{
    char clean[40];

    normalize_filename_to(clean, sizeof(clean), sheet_filename);
    snprintf(out, out_sz, "%s/%s", file_base_path(file_device), clean);
}

static void begin_file_dialog(int action)
{
    file_action = action;
    sheet_filename_len = (int)strlen(sheet_filename);
    if (sheet_filename_len < 0) sheet_filename_len = 0;
    if (sheet_filename_len > (int)sizeof(sheet_filename) - 1) {
        sheet_filename_len = (int)sizeof(sheet_filename) - 1;
        sheet_filename[sheet_filename_len] = 0;
    }

    ui_mode = MODE_FILE;

    if (file_action) {
        file_scan_csvs();
        if (file_csv_count > 0) set_status("LOAD: escoge CSV o escribe nombre");
        else set_status("LOAD: no hay CSV, escribe nombre");
    } else {
        file_csv_count = 0;
        file_csv_selected = 0;
        set_status("SAVE: elige unidad y nombre");
    }
}

static void cancel_file_dialog(void)
{
    ui_mode = MODE_NAV;
    set_status("archivo cancelado");
}

static void file_set_device(int dev)
{
    file_device = dev;
    if (file_device < 0) file_device = 0;
    if (file_device > 2) file_device = 2;

    if (file_action) {
        file_scan_csvs();
        if (file_csv_count > 0) set_status("LOAD: CSV encontrados");
        else set_status("LOAD: sin CSV en unidad");
    } else {
        file_csv_count = 0;
        file_csv_selected = 0;
        set_status("SAVE: unidad cambiada");
    }
}

static void file_insert_char(char c)
{
    if (c == ' ') c = '_';

    if (!file_char_ok(c)) return;
    if (sheet_filename_len >= (int)sizeof(sheet_filename) - 1) return;

    sheet_filename[sheet_filename_len++] = c;
    sheet_filename[sheet_filename_len] = 0;
}

static void file_backspace(void)
{
    if (sheet_filename_len <= 0) return;

    sheet_filename[--sheet_filename_len] = 0;
}

static int save_csv_to_path(const char *path)
{
    FILE *f = NULL;
    int max_r = 0;
    int max_c = 0;
    int any = 0;
    char msg[96];

    snprintf(msg, sizeof(msg), "guardando %s...", path ? path : "?");
    set_status(msg);
    draw_ui(last_pointer_x < 0 ? 0 : last_pointer_x, last_pointer_y < 0 ? 0 : last_pointer_y);
    rgb_display_wait_vsync();

    for (int r = 0; r < SHEET_ROWS; r++) {
        for (int c = 0; c < SHEET_COLS; c++) {
            if (cells[r][c][0]) {
                any = 1;
                if (r > max_r) max_r = r;
                if (c > max_c) max_c = c;
            }
        }
    }

    if (!any) {
        max_r = 0;
        max_c = 0;
    }

    f = fopen(path, "w");

    if (!f) {
        snprintf(msg, sizeof(msg), "no pude guardar %s", path ? path : "?");
        set_status(msg);
        return -1;
    }

    for (int r = 0; r <= max_r; r++) {
        for (int c = 0; c <= max_c; c++) {
            const char *s = cells[r][c];

            for (int i = 0; s[i]; i++) {
                char ch = s[i];
                if (ch == ',') ch = ';';
                if (ch == '\n' || ch == '\r') ch = ' ';
                fputc(ch, f);
            }

            if (c < max_c) fputc(',', f);
        }

        fputc('\n', f);
    }

    fclose(f);
    dirty_sheet = 0;

    normalize_filename_to(sheet_filename, sizeof(sheet_filename), sheet_filename);
    sheet_filename_len = (int)strlen(sheet_filename);

    snprintf(msg, sizeof(msg), "guardado %s", path ? path : "?");
    set_status(msg);
    return 0;
}

static int save_csv(void)
{
    char path[96];

    build_sheet_path(path, sizeof(path));
    return save_csv_to_path(path);
}

static int load_csv_from_path(const char *path)
{
    FILE *f = fopen(path, "r");
    int r = 0;
    int c = 0;
    int pos = 0;
    int ch = 0;
    char msg[96];

    if (!f) {
        snprintf(msg, sizeof(msg), "no pude abrir %s", path ? path : "?");
        set_status(msg);
        return -1;
    }

    memset(cells, 0, sizeof(cells));

    while ((ch = fgetc(f)) != EOF && r < SHEET_ROWS) {
        if (ch == '\r') continue;

        if (ch == ',' || ch == '\n') {
            cells[r][c][pos] = 0;
            pos = 0;

            if (ch == ',') {
                c++;
                if (c >= SHEET_COLS) {
                    while ((ch = fgetc(f)) != EOF && ch != '\n') {
                        // saltar resto de linea
                    }
                    r++;
                    c = 0;
                }
            } else {
                r++;
                c = 0;
            }

            continue;
        }

        if (c < SHEET_COLS && pos < CELL_LEN - 1 && ch >= 32 && ch <= 126) {
            cells[r][c][pos++] = (char)ch;
        }
    }

    fclose(f);
    cur_row = 0;
    cur_col = 0;
    row_scroll = 0;
    col_scroll = 0;
    dirty_sheet = 0;
    ui_mode = MODE_NAV;

    normalize_filename_to(sheet_filename, sizeof(sheet_filename), sheet_filename);
    sheet_filename_len = (int)strlen(sheet_filename);

    snprintf(msg, sizeof(msg), "cargado %s", path ? path : "?");
    set_status(msg);
    return 0;
}

static int load_csv(void)
{
    char path[96];

    build_sheet_path(path, sizeof(path));
    return load_csv_from_path(path);
}

static void handle_touch_as_pointer(int *mx, int *my, uint8_t *buttons)
{
    int16_t tx = 0;
    int16_t ty = 0;

    if (!mx || !my || !buttons) return;

    if (minipc_touch_ok() && minipc_touch_read(&tx, &ty)) {
        *mx = (int)tx / 2;
        *my = (int)ty / 2;

        if (*mx < 0) *mx = 0;
        if (*mx >= SW) *mx = SW - 1;
        if (*my < 0) *my = 0;
        if (*my >= SH) *my = SH - 1;

        *buttons |= 0x01;
        usb_hid_mouse_set_position(*mx, *my);
    }
}

static void read_pointer(int *mx, int *my, uint8_t *buttons)
{
    if (!mx || !my || !buttons) return;

    *mx = 0;
    *my = 0;
    *buttons = 0;

    usb_hid_mouse_get_state(mx, my, buttons);

    if (*mx < 0) *mx = 0;
    if (*mx >= SW) *mx = SW - 1;
    if (*my < 0) *my = 0;
    if (*my >= SH) *my = SH - 1;

    handle_touch_as_pointer(mx, my, buttons);

    last_pointer_x = *mx;
    last_pointer_y = *my;
}

static int hit_new (int x, int y) { return inside_rect(x, y,  18, 215, 48, 18); }
static int hit_load(int x, int y) { return inside_rect(x, y,  72, 215, 54, 18); }
static int hit_save(int x, int y) { return inside_rect(x, y, 132, 215, 54, 18); }
static int hit_edit(int x, int y) { return inside_rect(x, y, 238, 215, 54, 18); }
static int hit_exit(int x, int y) { return inside_rect(x, y, 330, 215, 54, 18); }

static int grid_cell_at(int mx, int my, int *row, int *col)
{
    int gx = mx - GRID_X;
    int gy = my - GRID_Y;

    if (gx < ROW_HEAD_W || gy < ROW_H) return 0;
    if (gx >= ROW_HEAD_W + VISIBLE_COLS * COL_W) return 0;
    if (gy >= ROW_H + VISIBLE_ROWS * ROW_H) return 0;

    *col = col_scroll + ((gx - ROW_HEAD_W) / COL_W);
    *row = row_scroll + ((gy - ROW_H) / ROW_H);

    if (*col < 0 || *col >= SHEET_COLS || *row < 0 || *row >= SHEET_ROWS) return 0;

    return 1;
}

static void draw_button(int x, int y, int w, int h, const char *txt, int hover)
{
    draw_box(x, y, w, h, hover ? COL_SEL : COL_PANEL2, COL_WHITE);
    draw_text_center(x, y + 5, w, txt, COL_TEXT, 1);
}

static void draw_grid(void)
{
    char tmp[48];

    // Cabecera tabla exterior
    draw_box(GRID_X, GRID_Y, ROW_HEAD_W + VISIBLE_COLS * COL_W, ROW_H + VISIBLE_ROWS * ROW_H,
             COL_PANEL, COL_BORDER);

    // Esquina vacia
    draw_box(GRID_X, GRID_Y, ROW_HEAD_W, ROW_H, COL_PANEL2, COL_GRID);

    // Columnas
    for (int vc = 0; vc < VISIBLE_COLS; vc++) {
        int col = col_scroll + vc;
        int x = GRID_X + ROW_HEAD_W + vc * COL_W;

        draw_box(x, GRID_Y, COL_W, ROW_H, COL_PANEL2, COL_GRID);
        snprintf(tmp, sizeof(tmp), "%c", (char)('A' + col));
        draw_text_center(x, GRID_Y + 3, COL_W, tmp, COL_ACCENT, 1);
    }

    // Filas y celdas
    for (int vr = 0; vr < VISIBLE_ROWS; vr++) {
        int row = row_scroll + vr;
        int y = GRID_Y + ROW_H + vr * ROW_H;

        draw_box(GRID_X, y, ROW_HEAD_W, ROW_H, COL_PANEL2, COL_GRID);
        snprintf(tmp, sizeof(tmp), "%d", row + 1);
        draw_text_center(GRID_X, y + 3, ROW_HEAD_W, tmp, COL_TEXT_DIM, 1);

        for (int vc = 0; vc < VISIBLE_COLS; vc++) {
            int col = col_scroll + vc;
            int x = GRID_X + ROW_HEAD_W + vc * COL_W;
            int selected = (row == cur_row && col == cur_col);
            char disp[48];

            draw_box(x, y, COL_W, ROW_H, selected ? COL_SEL : COL_BG, COL_GRID);
            cell_display(row, col, disp, sizeof(disp));

            if (cells[row][col][0] == '=') {
                draw_text_clip(x + 3, y + 3, disp, selected ? COL_WHITE : COL_GOOD, 1, COL_W - 6);
            } else {
                draw_text_clip(x + 3, y + 3, disp, selected ? COL_WHITE : COL_TEXT, 1, COL_W - 6);
            }
        }
    }
}

static void draw_ui(int mx, int my)
{
    char name[16];
    char raw[64];
    char val[48];
    char line[120];

    rgb_gfx_clear(COL_BG);

    draw_box(8, 6, 384, 24, COL_PANEL2, COL_BORDER);
    draw_text(18, 14, "ARIELO SHEET", COL_ACCENT, 1);
    draw_text(292, 14, "01F", COL_WHITE, 1);

    cell_name(cur_row, cur_col, name, sizeof(name));
    cell_display(cur_row, cur_col, val, sizeof(val));
    snprintf(raw, sizeof(raw), "%s", cells[cur_row][cur_col]);

    draw_box(12, 34, 376, 20, COL_FORMULA, COL_BORDER);

    if (ui_mode == MODE_FILE) {
        char fpath[96];

        build_sheet_path(fpath, sizeof(fpath));
        snprintf(line, sizeof(line), "%s: %s", file_action ? "LOAD" : "SAVE", fpath);
        draw_text_clip(20, 41, line, COL_WARN, 1, 320);
    } else if (ui_mode == MODE_FORM) {
        if (form_sum_mode) {
            snprintf(line, sizeof(line), "SUM(%s:%s)  DEC:%d",
                     form_a[0] ? form_a : "_",
                     form_b[0] ? form_b : "_",
                     sheet_decimals);
        } else {
            snprintf(line, sizeof(line), "FORM %s%c%s  DEC:%d",
                     form_a[0] ? form_a : "_",
                     form_op,
                     form_b[0] ? form_b : "_",
                     sheet_decimals);
        }

        draw_text_clip(20, 41, line, COL_WARN, 1, 254);

        if (form_sum_mode) {
            if (form_step == 0) draw_text(300, 41, "INI", COL_ACCENT, 1);
            else draw_text(300, 41, "FIN", COL_ACCENT, 1);
        } else if (form_step == 0) draw_text(300, 41, "OP1", COL_ACCENT, 1);
        else if (form_step == 1) draw_text(300, 41, "OP", COL_ACCENT, 1);
        else draw_text(300, 41, "OP2", COL_ACCENT, 1);
    } else {
        snprintf(line, sizeof(line), "%s  RAW:", name);
        draw_text(20, 41, line, COL_ACCENT, 1);

        if (ui_mode == MODE_EDIT) {
            draw_text_clip(80, 41, edit_buf, COL_WARN, 1, 210);
            draw_text(302, 41, "EDIT", COL_WARN, 1);
        } else {
            draw_text_clip(80, 41, raw, COL_TEXT, 1, 154);
            draw_text(244, 41, "VAL:", COL_TEXT_DIM, 1);
            draw_text_clip(274, 41, val, COL_GOOD, 1, 82);
        }
    }

    if (ui_mode == MODE_FILE) {
        char fpath2[96];

        build_sheet_path(fpath2, sizeof(fpath2));

        draw_box(22, 56, 356, 138, COL_PANEL, COL_BORDER);
        draw_text(34, 66, file_action ? "CARGAR HOJA CSV" : "GUARDAR HOJA CSV", COL_ACCENT, 1);

        draw_text(34, 84, "UNIDAD:", COL_TEXT_DIM, 1);
        draw_text(96, 84, file_device_label(file_device), COL_GOOD, 1);

        draw_text(34, 102, "NOMBRE:", COL_TEXT_DIM, 1);
        draw_box(96, 97, 220, 18, COL_BG, COL_GRID);
        draw_text_clip(102, 103, sheet_filename, COL_WHITE, 1, 190);
        draw_text(298, 103, "_", COL_WARN, 1);

        draw_text(34, 122, "RUTA:", COL_TEXT_DIM, 1);
        draw_text_clip(76, 122, fpath2, COL_TEXT, 1, 286);

        if (file_action) {
            draw_text(34, 140, "CSV:", COL_TEXT_DIM, 1);

            if (file_csv_count <= 0) {
                draw_text(84, 140, "sin .csv en esta unidad", COL_WARN, 1);
            } else {
                for (int i = 0; i < file_csv_count; i++) {
                    int y = 147 + i * 9;
                    int sel = (i == file_csv_selected);

                    if (sel) {
                        draw_box(82, y - 2, 288, 10, COL_SEL, COL_GRID);
                    }

                    draw_text_clip(88, y, file_csv_names[i], sel ? COL_WHITE : COL_TEXT, 1, 250);
                }
            }

            draw_text(34, 184, "UP/DOWN o toque=CSV  ENTER=OK", COL_TEXT_DIM, 1);
        } else {
            draw_text(34, 148, "ENTER=OK  ESC=CANCEL  TAB=UNIDAD", COL_TEXT_DIM, 1);
            draw_text(34, 166, "si no pones .csv, se anade solo", COL_TEXT_DIM, 1);
        }

        draw_box(12, 196, 376, 15, COL_PANEL, COL_BORDER);
        draw_text_clip(20, 201, status_line, COL_WARN, 1, 280);

        draw_button(18, 215, 48, 18, "ROOT", hit_new(mx, my));
        draw_button(72, 215, 54, 18, "SD", hit_load(mx, my));
        draw_button(132, 215, 54, 18, "USB", hit_save(mx, my));
        draw_button(238, 215, 54, 18, "OK", hit_edit(mx, my));
        draw_button(330, 215, 54, 18, "CAN", hit_exit(mx, my));

        if (last_pointer_x >= 0 && last_pointer_y >= 0) {
            rgb_gfx_rectfill(last_pointer_x, last_pointer_y, 5, 1, COL_WHITE);
            rgb_gfx_rectfill(last_pointer_x, last_pointer_y, 1, 5, COL_WHITE);
        }

        return;
    }

    draw_grid();

    draw_box(12, 196, 376, 15, COL_PANEL, COL_BORDER);
    draw_text_clip(20, 201, status_line, dirty_sheet ? COL_WARN : COL_TEXT_DIM, 1, 280);

    snprintf(line, sizeof(line), "%s D%d", key_layout_es ? "ES" : "US", sheet_decimals);
    draw_text(286, 201, line, COL_TEXT_DIM, 1);

    if (caps_lock) draw_text(338, 201, "CAP", COL_WARN, 1);
    else if (dirty_sheet) draw_text(338, 201, "MOD", COL_WARN, 1);
    else draw_text(350, 201, "OK", COL_GOOD, 1);

    if (ui_mode == MODE_FORM) {
        draw_button(18, 215, 48, 18, "+", hit_new(mx, my));
        draw_button(72, 215, 54, 18, "-", hit_load(mx, my));
        draw_button(132, 215, 54, 18, "*", hit_save(mx, my));
        draw_button(238, 215, 54, 18, "/", hit_edit(mx, my));
        draw_button(330, 215, 54, 18, "SUM", hit_exit(mx, my));
    } else {
        draw_button(18, 215, 48, 18, "NEW",  hit_new(mx, my));
        draw_button(72, 215, 54, 18, "LOAD", hit_load(mx, my));
        draw_button(132, 215, 54, 18, "SAVE", hit_save(mx, my));
        draw_button(238, 215, 54, 18, "FORM", hit_edit(mx, my));
        draw_button(330, 215, 54, 18, "EXIT", hit_exit(mx, my));
    }

    if (last_pointer_x >= 0 && last_pointer_y >= 0) {
        rgb_gfx_rectfill(last_pointer_x, last_pointer_y, 5, 1, COL_WHITE);
        rgb_gfx_rectfill(last_pointer_x, last_pointer_y, 1, 5, COL_WHITE);
    }
}

static int pointer_action_edge(int mx, int my, uint8_t buttons)
{
    int pressed = (buttons & 0x01) ? 1 : 0;
    int action = 0;

    if (pointer_cooldown > 0) {
        pointer_cooldown--;
    }

    if (!pressed) {
        if (pointer_release_stable < 8) pointer_release_stable++;
        if (pointer_release_stable >= 3 && pointer_cooldown == 0) {
            pointer_locked = 0;
        }
        prev_pointer_buttons = buttons;
        return 0;
    }

    pointer_release_stable = 0;

    if (pointer_locked || pointer_cooldown > 0) {
        prev_pointer_buttons = buttons;
        return 0;
    }

    pointer_locked = 1;
    pointer_cooldown = 12;

    if (ui_mode == MODE_FILE) {
        int idx = file_list_index_at(mx, my);

        if (hit_new(mx, my))  action = 'R';
        else if (hit_load(mx, my)) action = 'T';
        else if (hit_save(mx, my)) action = 'V';
        else if (hit_edit(mx, my)) action = 'K';
        else if (hit_exit(mx, my)) action = 'X';
        else if (idx >= 0) {
            file_select_csv_index(idx);
            action = 'C';
        }
    } else if (ui_mode == MODE_FORM) {
        if (hit_new(mx, my))  action = '+';
        else if (hit_load(mx, my)) action = '-';
        else if (hit_save(mx, my)) action = '*';
        else if (hit_edit(mx, my)) action = '/';
        else if (hit_exit(mx, my)) action = 'U';
    } else {
        if (hit_new(mx, my))  action = 'N';
        else if (hit_load(mx, my)) action = 'O';
        else if (hit_save(mx, my)) {
            action = 'S';
            pointer_cooldown = 24; // SAVE es mas lento: evitar re-disparo por touch
        }
        else if (hit_edit(mx, my)) action = 'F';
        else if (hit_exit(mx, my)) action = 'Q';
    }

    if (!action && ui_mode != MODE_FILE) {
        int r = 0;
        int c = 0;
        if (grid_cell_at(mx, my, &r, &c)) {
            if (ui_mode == MODE_FORM) {
                form_pick_cell(r, c);
                action = 'C';
            } else {
                cur_row = r;
                cur_col = c;
                ensure_visible();
                action = 'C';
            }
        }
    }

    prev_pointer_buttons = buttons;
    return action;
}

static void apply_action(char a)
{
    switch (a) {
        case 'N':
            new_sheet();
            dirty_sheet = 1;
            break;
        case 'O':
            begin_file_dialog(1);
            break;
        case 'S':
            begin_file_dialog(0);
            break;
        case 'E':
            begin_edit(0, 0);
            break;
        case 'F':
            begin_formula_builder();
            break;
        case 'D':
            cycle_decimals();
            break;
        case 'U':
            begin_sum_builder();
            break;
        case 'R':
            file_set_device(0);
            break;
        case 'T':
            file_set_device(1);
            break;
        case 'V':
            file_set_device(2);
            break;
        case 'X':
            cancel_file_dialog();
            break;
        case 'K':
            {
                char clean[40];

                normalize_filename_to(clean, sizeof(clean), sheet_filename);
                strncpy(sheet_filename, clean, sizeof(sheet_filename) - 1);
                sheet_filename[sizeof(sheet_filename) - 1] = 0;
                sheet_filename_len = (int)strlen(sheet_filename);

                if (file_action) {
                    load_csv();
                } else {
                    if (save_csv() == 0) {
                        ui_mode = MODE_NAV;
                    }
                }
            }
            break;
        case '+':
        case '-':
        case '*':
        case '/':
            if (ui_mode == MODE_FORM) {
                form_sum_mode = 0;
                form_op = a;
                form_step = 2;
                set_status("FORM: op2 celda/num, ej B1 o 7");
            }
            break;
        default:
            break;
    }
}

static int handle_keyboard(void)
{
    int dirty = 0;
    int ctrl = ctrl_down();
    char c = 0;

    if (key_edge(BT_KEY_CAPSLOCK)) {
        caps_lock = !caps_lock;
        set_status(caps_lock ? "CAPSLOCK ON" : "capslock off");
        return 1;
    }

    if (key_edge(BT_KEY_F9)) {
        key_layout_es = !key_layout_es;
        set_status(key_layout_es ? "teclado ES" : "teclado US");
        return 1;
    }

    if (key_edge(BT_KEY_F4)) {
        cycle_decimals();
        return 1;
    }

    if (ui_mode == MODE_FILE) {
        c = ascii_key_edge();

        if (key_edge(BT_KEY_ESC)) {
            cancel_file_dialog();
            return 1;
        }

        if (file_action && key_edge(BT_KEY_UP)) {
            file_select_delta(-1);
            return 1;
        }

        if (file_action && key_edge(BT_KEY_DOWN)) {
            file_select_delta(1);
            return 1;
        }

        if (key_edge(BT_KEY_ENTER)) {
            apply_action('K');
            return 1;
        }

        if (key_edge(BT_KEY_TAB)) {
            file_device++;
            if (file_device > 2) file_device = 0;
            file_set_device(file_device);
            return 1;
        }

        if (key_edge(BT_KEY_BACKSPACE)) {
            file_backspace();
            return 1;
        }

        if (c) {
            file_insert_char(c);
            return 1;
        }

        return 0;
    }

    if (ui_mode == MODE_FORM) {
        c = ascii_key_edge();

        if (key_edge(BT_KEY_ESC)) {
            cancel_formula_builder();
            return 1;
        }

        if (key_edge(BT_KEY_ENTER) || key_edge(BT_KEY_TAB)) {
            form_next_or_commit();
            return 1;
        }

        if (key_edge(BT_KEY_BACKSPACE)) {
            form_backspace();
            return 1;
        }

        if (c) {
            if (c == 's' || c == 'S') {
                begin_sum_builder();
            } else {
                form_insert_char(c);
            }
            return 1;
        }

        return 0;
    }

    if (ui_mode == MODE_EDIT) {
        if (key_edge(BT_KEY_ESC)) {
            cancel_edit();
            return 1;
        }

        if (key_edge(BT_KEY_ENTER) || key_edge(BT_KEY_TAB)) {
            commit_edit();
            if (key_down(BT_KEY_TAB)) move_cell(0, 1);
            return 1;
        }

        if (key_edge(BT_KEY_BACKSPACE)) {
            edit_backspace();
            return 1;
        }

        c = ascii_key_edge();
        if (c) {
            edit_insert_char(c);
            return 1;
        }

        return 0;
    }

    if (ctrl) {
        if (key_edge(BT_KEY_S)) { apply_action('S'); return 1; }
        if (key_edge(BT_KEY_O)) { apply_action('O'); return 1; }
        if (key_edge(BT_KEY_N)) { apply_action('N'); return 1; }
    }

    if (key_edge(BT_KEY_LEFT))  { move_cell(0, -1); dirty = 1; }
    if (key_edge(BT_KEY_RIGHT)) { move_cell(0,  1); dirty = 1; }
    if (key_edge(BT_KEY_UP))    { move_cell(-1, 0); dirty = 1; }
    if (key_edge(BT_KEY_DOWN))  { move_cell( 1, 0); dirty = 1; }

    if (key_edge(BT_KEY_TAB)) {
        move_cell(0, 1);
        dirty = 1;
    }

    if (key_edge(BT_KEY_ENTER) || key_edge(BT_KEY_F2)) {
        begin_edit(0, 0);
        dirty = 1;
    }

    if (key_edge(BT_KEY_BACKSPACE)) {
        cells[cur_row][cur_col][0] = 0;
        dirty_sheet = 1;
        set_status("celda borrada");
        dirty = 1;
    }

    if (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q)) {
        return 2;
    }

    c = ascii_key_edge();
    if (c) {
        if (c == '=') {
            begin_formula_builder();
        } else {
            begin_edit(1, c);
        }
        dirty = 1;
    }

    return dirty;
}

int main(void)
{
    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("SHEET 01F: no pudo entrar en SM_400X240\n");
        return 1;
    }

    setup_palette();
    sync_start_keys();
    new_sheet();

    int mx = 0, my = 0;
    uint8_t buttons = 0;
    read_pointer(&mx, &my, &buttons);
    draw_ui(mx, my);

    int running = 1;

    while (running) {
        int dirty = 0;
        int k = 0;

        read_pointer(&mx, &my, &buttons);

        k = handle_keyboard();
        if (k == 2) {
            running = 0;
            break;
        } else if (k) {
            dirty = 1;
        }

        {
            int a = pointer_action_edge(mx, my, buttons);
            if (a == 'Q') {
                running = 0;
                break;
            } else if (a == 'C') {
                dirty = 1;
            } else if (a) {
                apply_action((char)a);
                dirty = 1;
            }
        }

        if (dirty || last_pointer_x != mx || last_pointer_y != my) {
            redraw_count++;
            draw_ui(mx, my);
        }

        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    printf("SHEET 01F: salida limpia, redraws=%d dirty=%d\n", redraw_count, dirty_sheet);
    return 0;
}
