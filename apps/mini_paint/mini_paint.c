/*
 * mini_paint.c - Mini Paint externo para Arielo MiniPC OS
 *
 * 03C_LOAD_PICKER:
 *   - App externa pura ELF Xtensa ESP32-S3.
 *   - Modo grafico SM_400X240.
 *   - Entrada por teclado + touch GT911 + raton USB HID.
 *   - Lienzo 384x184, pincel, goma, color, limpiar.
 *   - Guardado BMP 8bpp con nombre configurable.
 *   - Carga BMP 8bpp generado por la propia app.
 *   - Mini ventana LOAD para escoger BMP existente en USB/SD.
 *
 * Filosofia:
 *   - No toca el SO base.
 *   - No usa rgb_gfx_rect(), solo rgb_gfx_rectfill().
 *   - Reutiliza las puertas ya abiertas para apps externas:
 *       usb_hid_mouse_get_state, usb_hid_mouse_set_position,
 *       minipc_touch_ok, minipc_touch_read.
 */

#include <stdint.h>
#include <stdbool.h>
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

#define CANVAS_X 8
#define CANVAS_Y 32
#define CANVAS_W 384
#define CANVAS_H 184
#define STATUS_Y 220

#define LOAD_DLG_X 28
#define LOAD_DLG_Y 48
#define LOAD_DLG_W 344
#define LOAD_DLG_H 158
#define LOAD_ROW_X 42
#define LOAD_ROW_Y 76
#define LOAD_ROW_H 14
#define LOAD_VISIBLE_ROWS 7
#define LOAD_MAX_FILES 32
#define LOAD_NAME_LEN 32

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
#define COL_HILITE    13
#define COL_BLACK     14
#define COL_CANVAS    15

/* Colores de dibujo: se mantienen fuera de la UI para guardar BMP limpio. */
#define DRAW_FIRST    32
#define DRAW_COUNT    12

#define TOOL_PEN      0
#define TOOL_ERASER   1

static uint16_t vga_pal[256];
static uint8_t canvas[CANVAS_W * CANVAS_H];

static uint8_t prev_keys[32];
static uint8_t prev_pointer_buttons;
static int cursor_x = CANVAS_W / 2;
static int cursor_y = CANVAS_H / 2;
static int old_cursor_x = -1;
static int old_cursor_y = -1;
static int tool = TOOL_PEN;
static int brush_size = 3;
static int draw_color_i = 1;
static int dirty = 0;
static int need_status = 1;
static char status_line[96] = "READY";
static char paint_name[24] = "paint";
static char edit_name[24] = "paint";
static int storage_dev = 0; /* 0=/usb, 1=/sdcard */
static int name_mode = 0;
static int load_picker_mode = 0;
static int need_load_dialog = 0;
static char bmp_files[LOAD_MAX_FILES][LOAD_NAME_LEN];
static int bmp_count = 0;
static int bmp_sel = 0;
static int bmp_scroll = 0;

/* Botones toolbar. */
typedef struct {
    int x, y, w, h;
    const char *label;
    uint8_t normal;
    uint8_t active;
} Button;

enum {
    BTN_ESC,
    BTN_PEN,
    BTN_ERS,
    BTN_CLR,
    BTN_MINUS,
    BTN_PLUS,
    BTN_COL,
    BTN_DEV,
    BTN_NAME,
    BTN_SAVE,
    BTN_LOAD,
    BTN_COUNT
};

static Button buttons[BTN_COUNT] = {
    {  4,  4, 28, 22, "ESC",  COL_RED,   COL_RED    },
    { 35,  4, 34, 22, "PEN",  COL_KEY,   COL_ACCENT },
    { 72,  4, 34, 22, "ERS",  COL_KEY,   COL_WARN   },
    {109,  4, 34, 22, "CLR",  COL_KEY,   COL_WARN   },
    {146,  4, 22, 22, "-",    COL_KEY,   COL_HILITE },
    {171,  4, 22, 22, "+",    COL_KEY,   COL_HILITE },
    {196,  4, 34, 22, "COL",  COL_KEY,   COL_HILITE },
    {233,  4, 34, 22, "USB",  COL_KEY2,  COL_GOOD   },
    {270,  4, 34, 22, "NAM",  COL_KEY2,  COL_HILITE },
    {307,  4, 38, 22, "SAVE", COL_KEY2,  COL_GOOD   },
    {348,  4, 38, 22, "LOAD", COL_KEY2,  COL_GOOD   },
};

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

static void draw_border(int x, int y, int w, int h, uint8_t color)
{
    rgb_gfx_rectfill(x, y, w, 1, color);
    rgb_gfx_rectfill(x, y + h - 1, w, 1, color);
    rgb_gfx_rectfill(x, y, 1, h, color);
    rgb_gfx_rectfill(x + w - 1, y, 1, h, color);
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void setup_palette(void)
{
    memset(vga_pal, 0, sizeof(vga_pal));
    vga_pal[COL_BG]       = rgb565(  0,   0,   0);
    vga_pal[COL_PANEL]    = rgb565( 12,  18,  28);
    vga_pal[COL_PANEL2]   = rgb565( 24,  32,  44);
    vga_pal[COL_BORDER]   = rgb565( 90, 120, 140);
    vga_pal[COL_TEXT]     = rgb565(210, 220, 220);
    vga_pal[COL_TEXT_DIM] = rgb565(130, 145, 145);
    vga_pal[COL_WHITE]    = rgb565(255, 255, 255);
    vga_pal[COL_ACCENT]   = rgb565(  0, 220, 255);
    vga_pal[COL_WARN]     = rgb565(255, 220,   0);
    vga_pal[COL_GOOD]     = rgb565(  0, 220,  70);
    vga_pal[COL_KEY]      = rgb565( 42,  54,  70);
    vga_pal[COL_KEY2]     = rgb565( 55,  65,  86);
    vga_pal[COL_RED]      = rgb565(220,  40,  32);
    vga_pal[COL_HILITE]   = rgb565(120,  90, 220);
    vga_pal[COL_BLACK]    = rgb565(  0,   0,   0);
    vga_pal[COL_CANVAS]   = rgb565(255, 255, 255);

    vga_pal[DRAW_FIRST +  0] = rgb565(  0,   0,   0); /* negro */
    vga_pal[DRAW_FIRST +  1] = rgb565(220,  30,  30); /* rojo */
    vga_pal[DRAW_FIRST +  2] = rgb565( 20, 170,  40); /* verde */
    vga_pal[DRAW_FIRST +  3] = rgb565( 30,  80, 230); /* azul */
    vga_pal[DRAW_FIRST +  4] = rgb565(240, 210,   0); /* amarillo */
    vga_pal[DRAW_FIRST +  5] = rgb565(255, 120,   0); /* naranja */
    vga_pal[DRAW_FIRST +  6] = rgb565(190,  60, 210); /* violeta */
    vga_pal[DRAW_FIRST +  7] = rgb565(  0, 210, 210); /* cian */
    vga_pal[DRAW_FIRST +  8] = rgb565(110,  70,  30); /* marron */
    vga_pal[DRAW_FIRST +  9] = rgb565(120, 120, 120); /* gris */
    vga_pal[DRAW_FIRST + 10] = rgb565(255, 255, 255); /* blanco */
    vga_pal[DRAW_FIRST + 11] = rgb565(255, 120, 170); /* rosa */

    rgb_display_set_vga_palette(vga_pal);
}

static uint8_t current_draw_color(void)
{
    return (uint8_t)(DRAW_FIRST + (draw_color_i % DRAW_COUNT));
}

static void set_status(const char *s)
{
    strncpy(status_line, s, sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = 0;
    need_status = 1;
}

static void make_bmp_path(char *out, int out_len)
{
    const char *root = storage_dev == 0 ? "/usb" : "/sdcard";
    (void)out_len; sprintf(out, "%s/%s.bmp", root, paint_name);
}

static void make_raw_path(char *out, int out_len)
{
    const char *root = storage_dev == 0 ? "/usb" : "/sdcard";
    (void)out_len; sprintf(out, "%s/%s.raw", root, paint_name);
}

static void format_status(const char *prefix)
{
    char tmp[96];
    sprintf(tmp, "%s %s %s.BMP B%d C%d", prefix,
            storage_dev == 0 ? "USB" : "SD", paint_name, brush_size, draw_color_i + 1);
    set_status(tmp);
}

static void draw_name_status(void)
{
    char tmp[96];
    sprintf(tmp, "NOMBRE: %s_  ENTER OK  BKSP  ESC", edit_name);
    set_status(tmp);
}

static void draw_button(int id)
{
    Button *b = &buttons[id];
    uint8_t fill = b->normal;
    uint8_t txt = COL_WHITE;
    const char *label = b->label;

    if ((id == BTN_PEN && tool == TOOL_PEN) || (id == BTN_ERS && tool == TOOL_ERASER)) {
        fill = b->active;
        txt = COL_BLACK;
    }
    if (id == BTN_COL) {
        fill = current_draw_color();
        txt = (draw_color_i == 0 || draw_color_i == 3 || draw_color_i == 8) ? COL_WHITE : COL_BLACK;
    }
    if (id == BTN_DEV) {
        label = storage_dev == 0 ? "USB" : "SD";
        fill = storage_dev == 0 ? COL_KEY2 : COL_ACCENT;
        txt = storage_dev == 0 ? COL_WHITE : COL_BLACK;
    }

    rgb_gfx_rectfill(b->x, b->y, b->w, b->h, fill);
    draw_border(b->x, b->y, b->w, b->h, COL_BORDER);
    draw_text_center(b->x, b->y + 7, b->w, label, txt, 1);
}

static void draw_toolbar(void)
{
    rgb_gfx_rectfill(0, 0, SW, 30, COL_PANEL);
    for (int i = 0; i < BTN_COUNT; i++) draw_button(i);
}

static void draw_canvas_full(void)
{
    for (int y = 0; y < CANVAS_H; y++) {
        int off = y * CANVAS_W;
        for (int x = 0; x < CANVAS_W; x++) {
            rgb_gfx_rectfill(CANVAS_X + x, CANVAS_Y + y, 1, 1, canvas[off + x]);
        }
    }
    draw_border(CANVAS_X - 1, CANVAS_Y - 1, CANVAS_W + 2, CANVAS_H + 2, COL_BORDER);
}

static void redraw_canvas_region(int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > CANVAS_W) w = CANVAS_W - x;
    if (y + h > CANVAS_H) h = CANVAS_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; yy++) {
        int off = (y + yy) * CANVAS_W + x;
        for (int xx = 0; xx < w; xx++) {
            rgb_gfx_rectfill(CANVAS_X + x + xx, CANVAS_Y + y + yy, 1, 1, canvas[off + xx]);
        }
    }
}

static void clear_canvas(void)
{
    memset(canvas, COL_CANVAS, sizeof(canvas));
    draw_canvas_full();
    dirty = 0;
    set_status("CANVAS LIMPIO");
}

static void draw_status(void)
{
    if (!need_status) return;
    need_status = 0;
    rgb_gfx_rectfill(0, STATUS_Y, SW, SH - STATUS_Y, COL_PANEL);
    draw_text(6, STATUS_Y + 5, status_line, COL_TEXT, 1);
    char tmp[48];
    sprintf(tmp, "X%d Y%d", cursor_x, cursor_y);
    draw_text(318, STATUS_Y + 5, tmp, COL_TEXT_DIM, 1);
}

static void put_canvas_pixel(int x, int y, uint8_t color)
{
    if (x < 0 || y < 0 || x >= CANVAS_W || y >= CANVAS_H) return;
    canvas[y * CANVAS_W + x] = color;
    rgb_gfx_rectfill(CANVAS_X + x, CANVAS_Y + y, 1, 1, color);
}

static void draw_brush(int x, int y)
{
    uint8_t c = (tool == TOOL_ERASER) ? COL_CANVAS : current_draw_color();
    int r = brush_size / 2;
    for (int yy = -r; yy <= r; yy++) {
        for (int xx = -r; xx <= r; xx++) {
            if (brush_size <= 2 || (xx * xx + yy * yy <= r * r + r)) {
                put_canvas_pixel(x + xx, y + yy, c);
            }
        }
    }
    dirty = 1;
}

static void draw_line_canvas(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        draw_brush(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_cursor(void)
{
    if (old_cursor_x >= 0) {
        redraw_canvas_region(old_cursor_x - 5, old_cursor_y - 5, 11, 11);
    }
    int sx = CANVAS_X + cursor_x;
    int sy = CANVAS_Y + cursor_y;
    rgb_gfx_rectfill(sx - 5, sy, 11, 1, COL_RED);
    rgb_gfx_rectfill(sx, sy - 5, 1, 11, COL_RED);
    old_cursor_x = cursor_x;
    old_cursor_y = cursor_y;
}

static int point_in_button(int x, int y)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        Button *b = &buttons[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) return i;
    }
    return -1;
}

static int point_in_canvas(int x, int y)
{
    return (x >= CANVAS_X && x < CANVAS_X + CANVAS_W &&
            y >= CANVAS_Y && y < CANVAS_Y + CANVAS_H);
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void rgb565_to_bgr(uint16_t c, uint8_t *bgr)
{
    uint8_t r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
    uint8_t g = (uint8_t)(((c >> 5)  & 0x3F) * 255 / 63);
    uint8_t b = (uint8_t)(( c        & 0x1F) * 255 / 31);
    bgr[0] = b;
    bgr[1] = g;
    bgr[2] = r;
    bgr[3] = 0;
}

static int save_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        set_status("ERROR ABRIENDO FICHERO");
        return 0;
    }

    const uint32_t palette_bytes = 256U * 4U;
    const uint32_t header_bytes = 54U;
    const uint32_t data_bytes = (uint32_t)CANVAS_W * (uint32_t)CANVAS_H;
    const uint32_t off_bits = header_bytes + palette_bytes;
    const uint32_t file_size = off_bits + data_bytes;

    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B';
    hdr[1] = 'M';
    le32(&hdr[2], file_size);
    le32(&hdr[10], off_bits);
    le32(&hdr[14], 40);               /* BITMAPINFOHEADER */
    le32(&hdr[18], CANVAS_W);
    le32(&hdr[22], CANVAS_H);         /* positivo = bottom-up */
    le16(&hdr[26], 1);                /* planes */
    le16(&hdr[28], 8);                /* 8 bpp indexado */
    le32(&hdr[34], data_bytes);
    le32(&hdr[38], 2835);             /* 72 dpi aprox */
    le32(&hdr[42], 2835);
    le32(&hdr[46], 256);
    le32(&hdr[50], 0);

    if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); set_status("ERROR ESCRIBIENDO BMP"); return 0; }

    for (int i = 0; i < 256; i++) {
        uint8_t bgr[4];
        rgb565_to_bgr(vga_pal[i], bgr);
        if (fwrite(bgr, 1, 4, f) != 4) { fclose(f); set_status("ERROR ESCRIBIENDO PALETA"); return 0; }
    }

    for (int y = CANVAS_H - 1; y >= 0; y--) {
        const uint8_t *row = &canvas[y * CANVAS_W];
        if (fwrite(row, 1, CANVAS_W, f) != CANVAS_W) { fclose(f); set_status("ERROR ESCRIBIENDO PIXELES"); return 0; }
    }

    fclose(f);
    dirty = 0;
    {
        char tmp[96];
        sprintf(tmp, "GUARDADO %s", path);
        set_status(tmp);
    }
    return 1;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t rd32s(const uint8_t *p)
{
    return (int32_t)rd32(p);
}

static int load_bmp(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { set_status("ERROR ABRIENDO BMP"); return 0; }

    uint8_t hdr[54];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); set_status("BMP MUY CORTO"); return 0; }
    if (hdr[0] != 'B' || hdr[1] != 'M') { fclose(f); set_status("NO ES BMP"); return 0; }

    uint32_t off_bits = rd32(&hdr[10]);
    uint32_t dib_size = rd32(&hdr[14]);
    int32_t w = rd32s(&hdr[18]);
    int32_t h = rd32s(&hdr[22]);
    uint16_t planes = rd16(&hdr[26]);
    uint16_t bpp = rd16(&hdr[28]);
    uint32_t comp = rd32(&hdr[30]);

    int top_down = 0;
    if (h < 0) { top_down = 1; h = -h; }
    if (dib_size < 40 || w != CANVAS_W || h != CANVAS_H || planes != 1 || bpp != 8 || comp != 0) {
        fclose(f);
        set_status("BMP NO COMPATIBLE 384X184 8BPP");
        return 0;
    }
    if (off_bits < 54) { fclose(f); set_status("BMP HEADER INVALIDO"); return 0; }

    uint32_t skip = off_bits - 54;
    uint8_t tmp_skip[64];
    while (skip > 0) {
        uint32_t n = skip > sizeof(tmp_skip) ? sizeof(tmp_skip) : skip;
        if (fread(tmp_skip, 1, n, f) != n) { fclose(f); set_status("BMP SIN DATOS"); return 0; }
        skip -= n;
    }

    const int row_stride = ((CANVAS_W + 3) / 4) * 4;
    uint8_t row[row_stride];
    for (int fy = 0; fy < CANVAS_H; fy++) {
        if (fread(row, 1, row_stride, f) != row_stride) { fclose(f); set_status("BMP PIXELES CORTOS"); return 0; }
        int cy = top_down ? fy : (CANVAS_H - 1 - fy);
        memcpy(&canvas[cy * CANVAS_W], row, CANVAS_W);
    }

    fclose(f);
    dirty = 0;
    old_cursor_x = -1;
    old_cursor_y = -1;
    draw_canvas_full();
    {
        char tmp[96];
        sprintf(tmp, "CARGADO %s", path);
        set_status(tmp);
    }
    return 1;
}


static int is_bmp_name(const char *name)
{
    int len = (int)strlen(name);
    if (len < 5) return 0;
    const char *e = name + len - 4;
    char b0 = e[0], b1 = e[1], b2 = e[2], b3 = e[3];
    if (b0 != '.') return 0;
    if (b1 >= 'A' && b1 <= 'Z') b1 = (char)(b1 + 32);
    if (b2 >= 'A' && b2 <= 'Z') b2 = (char)(b2 + 32);
    if (b3 >= 'A' && b3 <= 'Z') b3 = (char)(b3 + 32);
    return (b1 == 'b' && b2 == 'm' && b3 == 'p');
}

static void set_paint_name_from_bmp(const char *filename)
{
    int n = 0;
    while (filename[n] && filename[n] != '.' && n < (int)sizeof(paint_name) - 1) {
        char c = filename[n];
        if (c == ' ') c = '_';
        paint_name[n] = c;
        n++;
    }
    if (n <= 0) { strcpy(paint_name, "paint"); return; }
    paint_name[n] = 0;
}

static void scan_bmp_files(void)
{
    const char *root = storage_dev == 0 ? "/usb" : "/sdcard";
    bmp_count = 0;
    bmp_sel = 0;
    bmp_scroll = 0;

    DIR *d = opendir(root);
    if (!d) {
        char tmp[96];
        sprintf(tmp, "NO PUEDO ABRIR %s", root);
        set_status(tmp);
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != 0 && bmp_count < LOAD_MAX_FILES) {
        const char *nm = de->d_name;
        if (!nm || nm[0] == '.') continue;
        if (!is_bmp_name(nm)) continue;
        strncpy(bmp_files[bmp_count], nm, LOAD_NAME_LEN - 1);
        bmp_files[bmp_count][LOAD_NAME_LEN - 1] = 0;
        bmp_count++;
    }
    closedir(d);

    if (bmp_count <= 0) {
        char tmp[96];
        sprintf(tmp, "NO HAY BMP EN %s", root);
        set_status(tmp);
    } else {
        char tmp[96];
        sprintf(tmp, "%d BMP EN %s", bmp_count, root);
        set_status(tmp);
    }
}

static void draw_main_screen_keep_canvas(void)
{
    rgb_gfx_clear(COL_BG);
    draw_toolbar();
    draw_canvas_full();
    old_cursor_x = -1;
    need_status = 1;
    draw_status();
    draw_cursor();
}

static void draw_load_dialog(void)
{
    char title[64];
    rgb_gfx_rectfill(LOAD_DLG_X, LOAD_DLG_Y, LOAD_DLG_W, LOAD_DLG_H, COL_PANEL2);
    draw_border(LOAD_DLG_X, LOAD_DLG_Y, LOAD_DLG_W, LOAD_DLG_H, COL_WHITE);
    draw_border(LOAD_DLG_X + 2, LOAD_DLG_Y + 2, LOAD_DLG_W - 4, LOAD_DLG_H - 4, COL_BORDER);

    sprintf(title, "LOAD BMP - %s", storage_dev == 0 ? "USB" : "SD");
    draw_text(LOAD_DLG_X + 10, LOAD_DLG_Y + 8, title, COL_WHITE, 1);
    draw_text(LOAD_DLG_X + 230, LOAD_DLG_Y + 8, "ESC CANCEL", COL_TEXT_DIM, 1);

    rgb_gfx_rectfill(LOAD_DLG_X + 8, LOAD_ROW_Y - 4, LOAD_DLG_W - 16, LOAD_VISIBLE_ROWS * LOAD_ROW_H + 8, COL_BG);
    draw_border(LOAD_DLG_X + 8, LOAD_ROW_Y - 4, LOAD_DLG_W - 16, LOAD_VISIBLE_ROWS * LOAD_ROW_H + 8, COL_BORDER);

    if (bmp_count <= 0) {
        draw_text(LOAD_ROW_X, LOAD_ROW_Y + 22, "NO HAY ARCHIVOS .BMP", COL_WARN, 1);
        draw_text(LOAD_ROW_X, LOAD_ROW_Y + 38, "GUARDE UNO CON SAVE", COL_TEXT_DIM, 1);
    } else {
        for (int r = 0; r < LOAD_VISIBLE_ROWS; r++) {
            int idx = bmp_scroll + r;
            int y = LOAD_ROW_Y + r * LOAD_ROW_H;
            if (idx >= bmp_count) break;
            if (idx == bmp_sel) {
                rgb_gfx_rectfill(LOAD_ROW_X - 4, y - 2, LOAD_DLG_W - 28, LOAD_ROW_H, COL_ACCENT);
                draw_text(LOAD_ROW_X, y, bmp_files[idx], COL_BLACK, 1);
            } else {
                draw_text(LOAD_ROW_X, y, bmp_files[idx], COL_TEXT, 1);
            }
        }
    }

    rgb_gfx_rectfill(LOAD_DLG_X + 18, LOAD_DLG_Y + LOAD_DLG_H - 25, 45, 18, COL_KEY);
    draw_border(LOAD_DLG_X + 18, LOAD_DLG_Y + LOAD_DLG_H - 25, 45, 18, COL_BORDER);
    draw_text_center(LOAD_DLG_X + 18, LOAD_DLG_Y + LOAD_DLG_H - 19, 45, "UP", COL_WHITE, 1);

    rgb_gfx_rectfill(LOAD_DLG_X + 70, LOAD_DLG_Y + LOAD_DLG_H - 25, 45, 18, COL_KEY);
    draw_border(LOAD_DLG_X + 70, LOAD_DLG_Y + LOAD_DLG_H - 25, 45, 18, COL_BORDER);
    draw_text_center(LOAD_DLG_X + 70, LOAD_DLG_Y + LOAD_DLG_H - 19, 45, "DN", COL_WHITE, 1);

    rgb_gfx_rectfill(LOAD_DLG_X + 200, LOAD_DLG_Y + LOAD_DLG_H - 25, 50, 18, COL_GOOD);
    draw_border(LOAD_DLG_X + 200, LOAD_DLG_Y + LOAD_DLG_H - 25, 50, 18, COL_BORDER);
    draw_text_center(LOAD_DLG_X + 200, LOAD_DLG_Y + LOAD_DLG_H - 19, 50, "OK", COL_BLACK, 1);

    rgb_gfx_rectfill(LOAD_DLG_X + 258, LOAD_DLG_Y + LOAD_DLG_H - 25, 55, 18, COL_RED);
    draw_border(LOAD_DLG_X + 258, LOAD_DLG_Y + LOAD_DLG_H - 25, 55, 18, COL_BORDER);
    draw_text_center(LOAD_DLG_X + 258, LOAD_DLG_Y + LOAD_DLG_H - 19, 55, "CANCEL", COL_WHITE, 1);

    need_load_dialog = 0;
}

static void load_move_selection(int delta)
{
    if (bmp_count <= 0) return;
    bmp_sel += delta;
    if (bmp_sel < 0) bmp_sel = 0;
    if (bmp_sel >= bmp_count) bmp_sel = bmp_count - 1;
    if (bmp_sel < bmp_scroll) bmp_scroll = bmp_sel;
    if (bmp_sel >= bmp_scroll + LOAD_VISIBLE_ROWS) bmp_scroll = bmp_sel - LOAD_VISIBLE_ROWS + 1;
    need_load_dialog = 1;
}

static void start_load_picker(void)
{
    scan_bmp_files();
    load_picker_mode = 1;
    need_load_dialog = 1;
}

static void cancel_load_picker(void)
{
    load_picker_mode = 0;
    set_status("LOAD CANCELADO");
    draw_main_screen_keep_canvas();
}

static void load_selected_bmp(void)
{
    if (bmp_count <= 0) {
        set_status("NO HAY BMP PARA CARGAR");
        need_load_dialog = 1;
        return;
    }
    char path[80];
    const char *root = storage_dev == 0 ? "/usb" : "/sdcard";
    sprintf(path, "%s/%s", root, bmp_files[bmp_sel]);
    load_picker_mode = 0;
    if (load_bmp(path)) {
        set_paint_name_from_bmp(bmp_files[bmp_sel]);
        draw_toolbar();
        old_cursor_x = -1;
        need_status = 1;
        draw_status();
        draw_cursor();
    } else {
        draw_main_screen_keep_canvas();
    }
}

static int point_in_rect(int x, int y, int rx, int ry, int rw, int rh)
{
    return (x >= rx && x < rx + rw && y >= ry && y < ry + rh);
}

static int handle_load_keyboard(void)
{
    if (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q)) { cancel_load_picker(); return 0; }
    if (key_edge(BT_KEY_UP)) load_move_selection(-1);
    if (key_edge(BT_KEY_DOWN)) load_move_selection(1);
    if (key_edge(BT_KEY_ENTER)) load_selected_bmp();
    if (key_edge(BT_KEY_R)) { scan_bmp_files(); need_load_dialog = 1; }
    return 0;
}

static int handle_load_pointer_xy(int mx, int my, uint8_t buttons_now)
{
    int left_edge = ((buttons_now & 0x01) && !(prev_pointer_buttons & 0x01)) ? 1 : 0;
    int right_edge = ((buttons_now & 0x02) && !(prev_pointer_buttons & 0x02)) ? 1 : 0;
    if (right_edge) { cancel_load_picker(); return 0; }
    if (!left_edge) return 0;

    int by = LOAD_DLG_Y + LOAD_DLG_H - 25;
    if (point_in_rect(mx, my, LOAD_DLG_X + 18, by, 45, 18)) { load_move_selection(-1); return 0; }
    if (point_in_rect(mx, my, LOAD_DLG_X + 70, by, 45, 18)) { load_move_selection(1); return 0; }
    if (point_in_rect(mx, my, LOAD_DLG_X + 200, by, 50, 18)) { load_selected_bmp(); return 0; }
    if (point_in_rect(mx, my, LOAD_DLG_X + 258, by, 55, 18)) { cancel_load_picker(); return 0; }

    if (point_in_rect(mx, my, LOAD_DLG_X + 8, LOAD_ROW_Y - 4, LOAD_DLG_W - 16, LOAD_VISIBLE_ROWS * LOAD_ROW_H + 8)) {
        int row = (my - LOAD_ROW_Y) / LOAD_ROW_H;
        int idx = bmp_scroll + row;
        if (row >= 0 && row < LOAD_VISIBLE_ROWS && idx >= 0 && idx < bmp_count) {
            bmp_sel = idx;
            load_selected_bmp();
        }
    }
    return 0;
}

static int save_raw(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) { set_status("ERROR ABRIENDO RAW"); return 0; }
    if (fwrite(canvas, 1, sizeof(canvas), f) != sizeof(canvas)) {
        fclose(f);
        set_status("ERROR ESCRIBIENDO RAW");
        return 0;
    }
    fclose(f);
    {
        char tmp[96];
        sprintf(tmp, "GUARDADO %s", path);
        set_status(tmp);
    }
    return 1;
}

static void handle_button(int id)
{
    switch (id) {
        case BTN_ESC:
            set_status("SALIENDO");
            break;
        case BTN_PEN:
            tool = TOOL_PEN;
            draw_toolbar();
            format_status("MODO");
            break;
        case BTN_ERS:
            tool = TOOL_ERASER;
            draw_toolbar();
            format_status("MODO");
            break;
        case BTN_CLR:
            clear_canvas();
            break;
        case BTN_MINUS:
            if (brush_size > 1) brush_size--;
            format_status("BRUSH");
            break;
        case BTN_PLUS:
            if (brush_size < 15) brush_size++;
            format_status("BRUSH");
            break;
        case BTN_COL:
            draw_color_i = (draw_color_i + 1) % DRAW_COUNT;
            tool = TOOL_PEN;
            draw_toolbar();
            format_status("COLOR");
            break;
        case BTN_DEV:
            storage_dev = !storage_dev;
            draw_toolbar();
            format_status("DESTINO");
            break;
        case BTN_NAME:
            strncpy(edit_name, paint_name, sizeof(edit_name) - 1);
            edit_name[sizeof(edit_name) - 1] = 0;
            name_mode = 1;
            draw_name_status();
            break;
        case BTN_SAVE:
            { char path[64]; make_bmp_path(path, sizeof(path)); save_bmp(path); }
            break;
        case BTN_LOAD:
            start_load_picker();
            break;
        default:
            break;
    }
}

static void name_add_char(char c)
{
    int len = (int)strlen(edit_name);
    if (len >= (int)sizeof(edit_name) - 1) return;
    edit_name[len] = c;
    edit_name[len + 1] = 0;
    draw_name_status();
}

static int handle_name_keyboard(void)
{
    if (key_edge(BT_KEY_ESC)) {
        name_mode = 0;
        set_status("NOMBRE CANCELADO");
        return 0;
    }
    if (key_edge(BT_KEY_ENTER)) {
        if (edit_name[0]) {
            strncpy(paint_name, edit_name, sizeof(paint_name) - 1);
            paint_name[sizeof(paint_name) - 1] = 0;
            name_mode = 0;
            format_status("NOMBRE OK");
        } else {
            draw_name_status();
        }
        return 0;
    }
    if (key_edge(BT_KEY_BACKSPACE)) {
        int len = (int)strlen(edit_name);
        if (len > 0) edit_name[len - 1] = 0;
        draw_name_status();
        return 0;
    }
    if (key_edge(BT_KEY_SPACE)) { name_add_char('_'); return 0; }

    for (int k = BT_KEY_A; k <= BT_KEY_Z; k++) {
        if (key_edge((uint8_t)k)) {
            name_add_char((char)('a' + (k - BT_KEY_A)));
            return 0;
        }
    }
    for (int k = BT_KEY_1; k <= BT_KEY_9; k++) {
        if (key_edge((uint8_t)k)) {
            name_add_char((char)('1' + (k - BT_KEY_1)));
            return 0;
        }
    }
    if (key_edge(BT_KEY_0)) { name_add_char('0'); return 0; }
    return 0;
}

static int handle_keyboard(void)
{
    int moved = 0;
    if (load_picker_mode) return handle_load_keyboard();
    if (name_mode) return handle_name_keyboard();
    if (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q)) return 1;
    if (key_edge(BT_KEY_P)) { tool = TOOL_PEN; draw_toolbar(); format_status("MODO"); }
    if (key_edge(BT_KEY_E)) { tool = TOOL_ERASER; draw_toolbar(); format_status("MODO"); }
    if (key_edge(BT_KEY_C)) { draw_color_i = (draw_color_i + 1) % DRAW_COUNT; tool = TOOL_PEN; draw_toolbar(); format_status("COLOR"); }
    if (key_edge(BT_KEY_X)) { clear_canvas(); }
    if (key_edge(BT_KEY_N)) { strncpy(edit_name, paint_name, sizeof(edit_name) - 1); edit_name[sizeof(edit_name) - 1] = 0; name_mode = 1; draw_name_status(); }
    if (key_edge(BT_KEY_U)) { storage_dev = 0; draw_toolbar(); format_status("DESTINO"); }
    if (key_edge(BT_KEY_D)) { storage_dev = 1; draw_toolbar(); format_status("DESTINO"); }
    if (key_edge(BT_KEY_R)) { char path[64]; make_raw_path(path, sizeof(path)); save_raw(path); }
    if (key_edge(BT_KEY_B) || key_edge(BT_KEY_S)) { char path[64]; make_bmp_path(path, sizeof(path)); save_bmp(path); }
    if (key_edge(BT_KEY_L)) { start_load_picker(); }
    if (key_edge(BT_KEY_1)) { if (brush_size > 1) brush_size--; format_status("BRUSH"); }
    if (key_edge(BT_KEY_2)) { if (brush_size < 15) brush_size++; format_status("BRUSH"); }

    int step = (bt_keyboard_get_modifiers() & BT_MOD_SHIFT) ? 6 : 2;
    if (key_down(BT_KEY_LEFT))  { cursor_x -= step; moved = 1; }
    if (key_down(BT_KEY_RIGHT)) { cursor_x += step; moved = 1; }
    if (key_down(BT_KEY_UP))    { cursor_y -= step; moved = 1; }
    if (key_down(BT_KEY_DOWN))  { cursor_y += step; moved = 1; }
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= CANVAS_W) cursor_x = CANVAS_W - 1;
    if (cursor_y >= CANVAS_H) cursor_y = CANVAS_H - 1;

    if (key_down(BT_KEY_SPACE) || key_down(BT_KEY_ENTER)) {
        draw_brush(cursor_x, cursor_y);
        moved = 1;
    }
    if (moved) need_status = 1;
    return 0;
}

static int handle_pointer(void)
{
    if (name_mode) return 0;
    int mx = 0, my = 0;
    uint8_t buttons_now = 0;
    static int last_draw_x = -1;
    static int last_draw_y = -1;

    usb_hid_mouse_get_state(&mx, &my, &buttons_now);

    int16_t tx = 0, ty = 0;
    if (minipc_touch_ok() && minipc_touch_read(&tx, &ty)) {
        mx = ((int)tx) / 2;
        my = ((int)ty) / 2;
        buttons_now |= 0x01;
        usb_hid_mouse_set_position(mx, my);
    }

    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx >= SW) mx = SW - 1;
    if (my >= SH) my = SH - 1;

    if (load_picker_mode) {
        handle_load_pointer_xy(mx, my, buttons_now);
        prev_pointer_buttons = buttons_now;
        return 0;
    }

    int left_now = (buttons_now & 0x01) ? 1 : 0;
    int right_edge = ((buttons_now & 0x02) && !(prev_pointer_buttons & 0x02)) ? 1 : 0;
    int left_edge = (left_now && !(prev_pointer_buttons & 0x01)) ? 1 : 0;

    if (right_edge) {
        prev_pointer_buttons = buttons_now;
        return 1;
    }

    if (left_edge) {
        int bid = point_in_button(mx, my);
        if (bid >= 0) {
            handle_button(bid);
            prev_pointer_buttons = buttons_now;
            if (bid == BTN_ESC) return 1;
            return 0;
        }
    }

    if (left_now && point_in_canvas(mx, my)) {
        int cx = mx - CANVAS_X;
        int cy = my - CANVAS_Y;
        cursor_x = cx;
        cursor_y = cy;
        if (last_draw_x < 0 || !((prev_pointer_buttons & 0x01) != 0)) {
            draw_brush(cx, cy);
        } else {
            draw_line_canvas(last_draw_x, last_draw_y, cx, cy);
        }
        last_draw_x = cx;
        last_draw_y = cy;
        need_status = 1;
    } else {
        last_draw_x = -1;
        last_draw_y = -1;
    }

    prev_pointer_buttons = buttons_now;
    return 0;
}

static void draw_initial_screen(void)
{
    rgb_gfx_clear(COL_BG);
    draw_toolbar();
    clear_canvas();
    set_status("MINI PAINT 03C - LOAD LIST USB/SD");
    draw_status();
    draw_cursor();
}

int main(void)
{
    rgb_display_set_mode(SM_400X240);
    setup_palette();
    sync_start_keys();
    prev_pointer_buttons = 0;
    old_cursor_x = -1;
    old_cursor_y = -1;
    draw_initial_screen();

    int exit_app = 0;
    while (!exit_app) {
        exit_app = handle_keyboard();
        if (!exit_app) exit_app = handle_pointer();
        if (load_picker_mode) {
            if (need_load_dialog) draw_load_dialog();
        } else {
            draw_status();
            draw_cursor();
        }
        rgb_display_wait_vsync();
    }

    rgb_gfx_clear(COL_BG);
    rgb_display_wait_vsync();
    rgb_display_set_mode(SM_TEXT);
    return 0;
}
