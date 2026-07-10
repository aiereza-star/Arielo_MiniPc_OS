/*
 * cmd_desktop_07A_GUI_SHELL.c
 *
 * Arielo MiniPC OS - 09D GUI NAV BUTTONS.
 *
 * Entra en SM_400X240, que el driver escala x2 a 800x480.
 * Desktop grafico + editor TXT + navegador con botones HOME/RELOAD/BACK/FORWARD.
 *
 * Uso:
 *   desktop
 *   desktop -t 15
 *
 * Salida:
 *   ESC, q/Q, click en X, o boton derecho del raton.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

#include "rgb_display.h"

/* 10AN_FIX2_LOCAL_BUSY_PROTO: blindaje si CMake toma otro rgb_display.h */
void rgb_display_set_mouse_busy(int busy);
#include "rgb_gfx.h"
#include "vterm.h"

#include "usb_hid_keyboard_02d.h"
#include "minipc_touch_gt911.h"
#include "minipc_browser.h"
#include "minipc_ntp.h"
#include "breezy_exec.h"
#include <time.h>
#include "minipc_wifi_02c.h"
#include "minipc_sd_02b.h"
#include "minipc_usb_msc_02d.h"

#define DESK_W 400
#define DESK_H 240

static uint8_t *s_desk_back = NULL;

static inline uint8_t *desk_fb(void)
{
    return s_desk_back;
}

static void desk_gfx_clear(uint8_t color)
{
    uint8_t *fb = desk_fb();
    if (fb) memset(fb, color, DESK_W * DESK_H);
}

static void desk_gfx_pixel(int x, int y, uint8_t color)
{
    uint8_t *fb = desk_fb();
    if (fb && x >= 0 && x < DESK_W && y >= 0 && y < DESK_H) {
        fb[y * DESK_W + x] = color;
    }
}

static void desk_gfx_hline(int x, int y, int w, uint8_t color)
{
    uint8_t *fb = desk_fb();
    if (!fb || y < 0 || y >= DESK_H || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > DESK_W) w = DESK_W - x;
    if (w <= 0) return;
    memset(&fb[y * DESK_W + x], color, w);
}

static void desk_gfx_vline(int x, int y, int h, uint8_t color)
{
    uint8_t *fb = desk_fb();
    if (!fb || x < 0 || x >= DESK_W || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > DESK_H) h = DESK_H - y;
    if (h <= 0) return;
    uint8_t *p = &fb[y * DESK_W + x];
    for (int i = 0; i < h; i++) {
        *p = color;
        p += DESK_W;
    }
}

static void desk_gfx_rect(int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    desk_gfx_hline(x, y, w, color);
    desk_gfx_hline(x, y + h - 1, w, color);
    if (h > 2) {
        desk_gfx_vline(x, y + 1, h - 2, color);
        desk_gfx_vline(x + w - 1, y + 1, h - 2, color);
    }
}

static void desk_gfx_rectfill(int x, int y, int w, int h, uint8_t color)
{
    uint8_t *fb = desk_fb();
    if (!fb || w <= 0 || h <= 0) return;

    int x0 = x, y0 = y;
    int x1 = x + w, y1 = y + h;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > DESK_W) x1 = DESK_W;
    if (y1 > DESK_H) y1 = DESK_H;

    int cw = x1 - x0;
    if (cw <= 0 || y1 <= y0) return;

    for (int row = y0; row < y1; row++) {
        memset(&fb[row * DESK_W + x0], color, cw);
    }
}

static void desktop_present_backbuffer(void)
{
    uint8_t *front = rgb_display_get_framebuffer();
    if (!front || !s_desk_back) return;

    // Presentar en VSYNC reduce las rayas negras/tearing.
    rgb_display_wait_vsync();
    memcpy(front, s_desk_back, DESK_W * DESK_H);
}


// Paleta 8bpp de escritorio.
enum {
    C_BLACK      = 0,
    C_BG0        = 1,
    C_BG1        = 2,
    C_TOP        = 3,
    C_PANEL      = 4,
    C_PANEL2     = 5,
    C_BORDER     = 6,
    C_TEXT       = 7,
    C_MUTED      = 8,
    C_GREEN      = 9,
    C_YELLOW     = 10,
    C_RED        = 11,
    C_BLUE       = 12,
    C_CYAN       = 13,
    C_WHITE      = 14,
    C_SHADOW     = 15,
};

typedef struct {
    int x;
    int y;
    int w;
    int h;
    const char *title;
    const char *sub;
    uint8_t color;
} desk_card_t;

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

static void desktop_palette(void)
{
    uint16_t pal[256] = {0};

    pal[C_BLACK]  = rgb565(0, 0, 0);
    pal[C_BG0]    = rgb565(8, 18, 34);
    pal[C_BG1]    = rgb565(14, 40, 72);
    pal[C_TOP]    = rgb565(20, 28, 46);
    pal[C_PANEL]  = rgb565(24, 38, 60);
    pal[C_PANEL2] = rgb565(36, 56, 88);
    pal[C_BORDER] = rgb565(85, 120, 160);
    pal[C_TEXT]   = rgb565(236, 242, 255);
    pal[C_MUTED]  = rgb565(150, 165, 185);
    pal[C_GREEN]  = rgb565(70, 220, 120);
    pal[C_YELLOW] = rgb565(245, 210, 70);
    pal[C_RED]    = rgb565(235, 85, 85);
    pal[C_BLUE]   = rgb565(60, 140, 245);
    pal[C_CYAN]   = rgb565(80, 210, 235);
    pal[C_WHITE]  = rgb565(255, 255, 255);
    pal[C_SHADOW] = rgb565(4, 9, 16);

    // Gradiente adicional por si luego se usa.
    for (int i = 32; i < 256; i++) {
        uint8_t v = (uint8_t)i;
        pal[i] = rgb565(v / 4, v / 3, v / 2);
    }

    rgb_display_set_vga_palette(pal);
}

/*
 * Fuente 5x7 muy compacta. Soporta mayusculas, minusculas por conversión,
 * numeros y algunos signos.
 */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0 space
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
};

static uint8_t glyph_for_char(char c, uint8_t col)
{
    // Minusculas con glyphs propios (a-z) en 5x7, column-major (LSB = arriba).
    // Asi se ven minusculas de verdad, no convertidas a mayuscula.
    static const uint8_t font_low[26][5] = {
        {0x20,0x54,0x54,0x54,0x78}, // a
        {0x7F,0x48,0x44,0x44,0x38}, // b
        {0x38,0x44,0x44,0x44,0x20}, // c
        {0x38,0x44,0x44,0x48,0x7F}, // d
        {0x38,0x54,0x54,0x54,0x18}, // e
        {0x08,0x7E,0x09,0x01,0x02}, // f
        {0x0C,0x52,0x52,0x52,0x3E}, // g
        {0x7F,0x08,0x04,0x04,0x78}, // h
        {0x00,0x44,0x7D,0x40,0x00}, // i
        {0x20,0x40,0x44,0x3D,0x00}, // j
        {0x7F,0x10,0x28,0x44,0x00}, // k
        {0x00,0x41,0x7F,0x40,0x00}, // l
        {0x7C,0x04,0x18,0x04,0x78}, // m
        {0x7C,0x08,0x04,0x04,0x78}, // n
        {0x38,0x44,0x44,0x44,0x38}, // o
        {0x7C,0x14,0x14,0x14,0x08}, // p
        {0x08,0x14,0x14,0x18,0x7C}, // q
        {0x7C,0x08,0x04,0x04,0x08}, // r
        {0x48,0x54,0x54,0x54,0x20}, // s
        {0x04,0x3F,0x44,0x40,0x20}, // t
        {0x3C,0x40,0x40,0x20,0x7C}, // u
        {0x1C,0x20,0x40,0x20,0x1C}, // v
        {0x3C,0x40,0x30,0x40,0x3C}, // w
        {0x44,0x28,0x10,0x28,0x44}, // x
        {0x0C,0x50,0x50,0x50,0x3C}, // y
        {0x44,0x64,0x54,0x4C,0x44}, // z
    };
    if (c >= 'a' && c <= 'z') return font_low[c - 'a'][col];
    if (c >= 'A' && c <= 'Z') return font5x7[1 + (c - 'A')][col];

    static const uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},
        {0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10},
        {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30},
        {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},
        {0x06,0x49,0x49,0x29,0x1E},
    };
    if (c >= '0' && c <= '9') return digits[c - '0'][col];

    switch (c) {
        case '.': { static const uint8_t g[5]={0x00,0x60,0x60,0x00,0x00}; return g[col]; }
        case ':': { static const uint8_t g[5]={0x00,0x36,0x36,0x00,0x00}; return g[col]; }
        case '/': { static const uint8_t g[5]={0x20,0x10,0x08,0x04,0x02}; return g[col]; }
        case '-': { static const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; return g[col]; }
        case '_': { static const uint8_t g[5]={0x40,0x40,0x40,0x40,0x40}; return g[col]; }
        case '+': { static const uint8_t g[5]={0x08,0x08,0x3E,0x08,0x08}; return g[col]; }
        case '%': { static const uint8_t g[5]={0x23,0x13,0x08,0x64,0x62}; return g[col]; }
        case '[': { static const uint8_t g[5]={0x00,0x7F,0x41,0x41,0x00}; return g[col]; }
        case ']': { static const uint8_t g[5]={0x00,0x41,0x41,0x7F,0x00}; return g[col]; }
        case '|': { static const uint8_t g[5]={0x00,0x00,0x7F,0x00,0x00}; return g[col]; }
        case '=': { static const uint8_t g[5]={0x14,0x14,0x14,0x14,0x14}; return g[col]; }
        case '>': { static const uint8_t g[5]={0x00,0x41,0x22,0x14,0x08}; return g[col]; }
        case '<': { static const uint8_t g[5]={0x08,0x14,0x22,0x41,0x00}; return g[col]; }
        case '!': { static const uint8_t g[5]={0x00,0x00,0x5F,0x00,0x00}; return g[col]; }
        case '*': { static const uint8_t g[5]={0x14,0x08,0x3E,0x08,0x14}; return g[col]; }
        case '?': { static const uint8_t g[5]={0x02,0x01,0x51,0x09,0x06}; return g[col]; }
        case '(': { static const uint8_t g[5]={0x00,0x1C,0x22,0x41,0x00}; return g[col]; }
        case ')': { static const uint8_t g[5]={0x00,0x41,0x22,0x1C,0x00}; return g[col]; }
        default: return font5x7[0][col];
    }
}

static void draw_char5(int x, int y, char c, uint8_t color)
{
    for (int cx = 0; cx < 5; cx++) {
        uint8_t bits = glyph_for_char(c, cx);
        for (int cy = 0; cy < 7; cy++) {
            if (bits & (1 << cy)) {
                desk_gfx_pixel(x + cx, y + cy, color);
            }
        }
    }
}

static void draw_text5(int x, int y, const char *s, uint8_t color)
{
    int px = x;
    while (*s) {
        draw_char5(px, y, *s++, color);
        px += 6;
    }
}

static void draw_text5_center(int x, int y, int w, const char *s, uint8_t color)
{
    int len = (int)strlen(s);
    int tw = len * 6;
    draw_text5(x + (w - tw) / 2, y, s, color);
}

static void rect_shadow(int x, int y, int w, int h)
{
    desk_gfx_rectfill(x + 2, y + 2, w, h, C_SHADOW);
}

static void panel(int x, int y, int w, int h, uint8_t fill)
{
    rect_shadow(x, y, w, h);
    desk_gfx_rectfill(x, y, w, h, fill);
    desk_gfx_rect(x, y, w, h, C_BORDER);
    desk_gfx_hline(x + 1, y + 1, w - 2, C_PANEL2);
}

static int inside(int mx, int my, int x, int y, int w, int h)
{
    return (mx >= x && mx < x + w && my >= y && my < y + h);
}

static void draw_led(int x, int y, int on, const char *label)
{
    desk_gfx_rectfill(x, y, 8, 8, on ? C_GREEN : C_RED);
    desk_gfx_rect(x, y, 8, 8, C_WHITE);
    draw_text5(x + 12, y + 1, label, C_TEXT);
}

static void draw_icon_card(const desk_card_t *c, int hover)
{
    panel(c->x, c->y, c->w, c->h, hover ? C_PANEL2 : C_PANEL);

    // icon square
    desk_gfx_rectfill(c->x + 8, c->y + 9, 24, 24, c->color);
    desk_gfx_rect(c->x + 8, c->y + 9, 24, 24, C_WHITE);

    // simple icon glyph
    if (strcmp(c->title, "FILES") == 0) {
        desk_gfx_rectfill(c->x + 12, c->y + 15, 16, 12, C_YELLOW);
        desk_gfx_rect(c->x + 12, c->y + 15, 16, 12, C_BLACK);
        desk_gfx_hline(c->x + 12, c->y + 14, 9, C_YELLOW);
    } else if (strcmp(c->title, "TERMINAL") == 0) {
        draw_text5(c->x + 12, c->y + 17, ">", C_BLACK);
        desk_gfx_hline(c->x + 20, c->y + 25, 8, C_BLACK);
    } else if (strcmp(c->title, "USB") == 0) {
        desk_gfx_vline(c->x + 20, c->y + 13, 16, C_BLACK);
        desk_gfx_hline(c->x + 16, c->y + 18, 8, C_BLACK);
        desk_gfx_hline(c->x + 20, c->y + 24, 6, C_BLACK);
    } else if (strcmp(c->title, "WIFI") == 0) {
        desk_gfx_rect(c->x + 14, c->y + 17, 12, 10, C_BLACK);
        desk_gfx_hline(c->x + 12, c->y + 15, 16, C_BLACK);
        desk_gfx_hline(c->x + 15, c->y + 12, 10, C_BLACK);
    } else if (strcmp(c->title, "SYSTEM") == 0) {
        desk_gfx_rect(c->x + 14, c->y + 15, 12, 12, C_BLACK);
        desk_gfx_hline(c->x + 12, c->y + 21, 16, C_BLACK);
        desk_gfx_vline(c->x + 20, c->y + 13, 16, C_BLACK);
    } else {
        draw_text5(c->x + 17, c->y + 17, "i", C_BLACK);
    }

    draw_text5(c->x + 40, c->y + 12, c->title, C_TEXT);
    draw_text5(c->x + 40, c->y + 24, c->sub, C_MUTED);
}

static void draw_background(void)
{
    // simple vertical banding background
    for (int y = 0; y < DESK_H; y++) {
        uint8_t c = (y < 70) ? C_BG1 : C_BG0;
        desk_gfx_hline(0, y, DESK_W, c);
    }

    // decorative horizon
    desk_gfx_hline(0, 69, DESK_W, C_BORDER);
    desk_gfx_hline(0, 70, DESK_W, C_SHADOW);
}

static void draw_topbar(int mx, int my)
{
    (void)mx; (void)my;
    desk_gfx_rectfill(0, 0, DESK_W, 18, C_TOP);
    desk_gfx_hline(0, 17, DESK_W, C_BORDER);

    draw_text5(8, 5, "Arielo MiniPC OS", C_TEXT);

    // Reloj NTP (dd/mmm/aaaa hh:mm) a la derecha.
    char clk[24];
    minipc_ntp_format(clk, sizeof(clk));
    draw_text5(266, 5, clk, minipc_ntp_synced() ? C_GREEN : C_MUTED);
}

static void draw_status_panel(void)
{
    panel(12, 29, 376, 22, C_PANEL);

    draw_text5(22, 35, "STATUS", C_CYAN);
    draw_led(82, 34, minipc_wifi_is_connected(), "WiFi");
    draw_led(144, 34, minipc_sd_is_mounted(), "SD");
    draw_led(194, 34, minipc_usb_msc_mounted(), "USB");

    const char *ip = minipc_wifi_get_ip();
    if (ip && ip[0]) {
        draw_text5(252, 35, ip, C_MUTED);
    } else {
        draw_text5(252, 35, "0.0.0.0", C_MUTED);
    }
}

static void draw_footer(void)
{
    desk_gfx_rectfill(0, 224, DESK_W, 16, C_TOP);
    desk_gfx_hline(0, 223, DESK_W, C_BORDER);
    draw_text5(8, 229, "SM_400X240 x2  |   mouse activo   |   ARIELO_MINIPC_OS_10BO_OK", C_MUTED);
}

typedef enum {
    VIEW_DESKTOP = 0,
    VIEW_FILES,
    VIEW_APPS,
    VIEW_WIFI,
    VIEW_SYSTEM,
    VIEW_ABOUT,
    VIEW_BROWSER,
    VIEW_TEXTEDIT,
} desktop_view_t;

static const desk_card_t g_cards[] = {
    {  18,  76, 112, 50, "FILES",    "SD / USB",    C_YELLOW },
    { 144,  76, 112, 50, "TERMINAL", "BreezyBox",   C_GREEN  },
    { 270,  76, 112, 50, "WIFI",     "Network",     C_CYAN   },
    {  18, 142, 112, 50, "APPS",     "Launcher",    C_BLUE   },
    { 144, 142, 112, 50, "WEB",      "Lexbor 10BO",  C_CYAN   },
    { 270, 142, 112, 50, "ABOUT",    "Arielo OS",   C_WHITE  },
};

#define CARD_COUNT ((int)(sizeof(g_cards) / sizeof(g_cards[0])))

static int card_at(int mx, int my)
{
    for (int i = 0; i < CARD_COUNT; i++) {
        if (inside(mx, my, g_cards[i].x, g_cards[i].y, g_cards[i].w, g_cards[i].h)) {
            return i;
        }
    }
    return -1;
}

static int back_button_hover(int mx, int my)
{
    return inside(mx, my, 16, 30, 50, 18);
}

static int refresh_button_hover(int mx, int my)
{
    return inside(mx, my, 322, 30, 60, 18);
}

static void draw_button(int x, int y, int w, int h, const char *txt, int hover)
{
    desk_gfx_rectfill(x, y, w, h, hover ? C_BLUE : C_PANEL2);
    desk_gfx_rect(x, y, w, h, C_WHITE);
    draw_text5_center(x, y + 6, w, txt, C_TEXT);
}

// Boton pequeño para el selector de ubicacion del FILES manager.
// El draw_button normal centra a y+6 y en botones de 11/14 px el texto se sale.
static void draw_button_tiny(int x, int y, int w, int h, const char *txt, int hover)
{
    desk_gfx_rectfill(x, y, w, h, hover ? C_BLUE : C_PANEL2);
    desk_gfx_rect(x, y, w, h, C_WHITE);
    draw_text5_center(x, y + 3, w, txt, C_TEXT);
}

static void draw_window_base(int mx, int my, const char *title, int refresh_button)
{
    draw_background();
    draw_topbar(mx, my);

    panel(12, 25, 376, 194, C_PANEL);
    draw_button(16, 30, 50, 18, "BACK", back_button_hover(mx, my));

    if (refresh_button) {
        draw_button(322, 30, 60, 18, "REFRESH", refresh_button_hover(mx, my));
    }

    draw_text5(76, 36, title, C_CYAN);
    desk_gfx_hline(18, 54, 364, C_BORDER);
    draw_footer();
}


static void desk_safe_path_join(char *out, size_t out_sz, const char *dir, const char *name)
{
    if (!out || out_sz == 0) return;

    out[0] = 0;

    if (!dir) dir = "";
    if (!name) name = "";

    strncat(out, dir, out_sz - 1);

    size_t len = strlen(out);
    if (len > 0 && out[len - 1] != '/' && len + 1 < out_sz) {
        strncat(out, "/", out_sz - strlen(out) - 1);
    }

    strncat(out, name, out_sz - strlen(out) - 1);
}

static void desk_trunc_copy(char *out, size_t out_sz, const char *in, size_t max_chars)
{
    if (!out || out_sz == 0) return;
    if (!in) in = "";

    size_t n = strlen(in);
    if (n > max_chars && max_chars >= 3 && max_chars + 1 <= out_sz) {
        memcpy(out, in, max_chars - 3);
        out[max_chars - 3] = '.';
        out[max_chars - 2] = '.';
        out[max_chars - 1] = '.';
        out[max_chars] = 0;
    } else {
        strncpy(out, in, out_sz - 1);
        out[out_sz - 1] = 0;
    }
}


static void draw_dir_list(const char *path, int x, int y, int max_lines)
{
    DIR *d = opendir(path);
    char line[64];

    snprintf(line, sizeof(line), "%s", path);
    draw_text5(x, y, line, C_YELLOW);
    y += 12;

    if (!d) {
        draw_text5(x, y, "not mounted or empty", C_RED);
        return;
    }

    struct dirent *e;
    int n = 0;
    while ((e = readdir(d)) != NULL && n < max_lines) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }

        char full[192];
        struct stat st;
        int is_dir = 0;
        desk_safe_path_join(full, sizeof(full), path, e->d_name);
        if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            is_dir = 1;
        }

        char name[40];
        strncpy(name, e->d_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        if (strlen(name) > 32) {
            name[29] = '.';
            name[30] = '.';
            name[31] = '.';
            name[32] = 0;
        }

        snprintf(line, sizeof(line), "%s %s", is_dir ? "[D]" : "[F]", name);
        draw_text5(x, y + n * 10, line, C_TEXT);
        n++;
    }
    closedir(d);

    if (n == 0) {
        draw_text5(x, y, "no entries", C_MUTED);
    }
}

/* --------------------------------------------------------------------------
 * 08A OK TXT
 * Dos paneles: izquierda /sdcard, derecha /usb.
 * Acciones: abrir carpetas, subir, copiar, mover, borrar con confirmación.
 * -------------------------------------------------------------------------- */

#define FM_MAX_ENTRIES 128
#define FM_NAME_LEN    44
#define FM_PATH_LEN    160
#define FM_VISIBLE     8

typedef enum {
    FM_LOC_ROOT = 0,      // /root
    FM_LOC_ROOTBIN,       // /root/bin
    FM_LOC_SDCARD,        // /sdcard
    FM_LOC_USB,           // /usb
} fm_loc_t;

typedef struct {
    char name[FM_NAME_LEN];
    int is_dir;
    uint32_t size;
} fm_entry_t;

typedef struct {
    char path[FM_PATH_LEN];
    fm_loc_t loc;
    fm_entry_t e[FM_MAX_ENTRIES];
    int count;
    int selected;
    int offset;
    int valid;
} fm_panel_t;

static fm_panel_t g_fm[2];
static int g_fm_active = 0;
static int g_fm_need_scan = 1;
static int g_fm_last_sd_mounted = -1;
static int g_fm_last_usb_mounted = -1;
static char g_fm_status[96] = "ready";
static char g_fm_delete_armed[FM_PATH_LEN * 2] = "";

static void fm_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_fm_status, sizeof(g_fm_status), fmt, ap);
    va_end(ap);
}

static const char *fm_loc_base(fm_loc_t loc)
{
    switch (loc) {
        case FM_LOC_ROOT:    return "/root";
        case FM_LOC_ROOTBIN: return "/root/bin";
        case FM_LOC_SDCARD:  return "/sdcard";
        case FM_LOC_USB:     return "/usb";
        default:             return "/root";
    }
}

static const char *fm_loc_label(fm_loc_t loc)
{
    switch (loc) {
        case FM_LOC_ROOT:    return "ROOT";
        case FM_LOC_ROOTBIN: return "BIN";
        case FM_LOC_SDCARD:  return "SD";
        case FM_LOC_USB:     return "USB";
        default:             return "?";
    }
}

static int fm_path_equal_noslash(const char *a, const char *b)
{
    if (!a || !b) return 0;

    char aa[FM_PATH_LEN];
    char bb[FM_PATH_LEN];

    strncpy(aa, a, sizeof(aa) - 1);
    aa[sizeof(aa) - 1] = 0;
    strncpy(bb, b, sizeof(bb) - 1);
    bb[sizeof(bb) - 1] = 0;

    size_t la = strlen(aa);
    size_t lb = strlen(bb);

    while (la > 1 && aa[la - 1] == '/') {
        aa[--la] = 0;
    }
    while (lb > 1 && bb[lb - 1] == '/') {
        bb[--lb] = 0;
    }

    return strcmp(aa, bb) == 0;
}

static int path_is_root(const char *p)
{
    // Usado para probar variante con/sin barra final en los roots conocidos.
    return fm_path_equal_noslash(p, "/root") ||
           fm_path_equal_noslash(p, "/root/bin") ||
           fm_path_equal_noslash(p, "/sdcard") ||
           fm_path_equal_noslash(p, "/usb");
}

static int fm_panel_at_base(int idx)
{
    if (idx < 0 || idx > 1) return 1;
    return fm_path_equal_noslash(g_fm[idx].path, fm_loc_base(g_fm[idx].loc));
}

static void path_join(char *out, size_t out_sz, const char *dir, const char *name)
{
    desk_safe_path_join(out, out_sz, dir, name);
}

static void path_parent(char *p, size_t p_sz)
{
    (void)p_sz;
    if (!p || !p[0]) return;
    if (strcmp(p, "/") == 0) return;

    char *slash = strrchr(p, '/');
    if (!slash) return;

    if (slash == p) {
        slash[1] = 0;
        return;
    }

    *slash = 0;
    if (p[0] == 0) {
        strncpy(p, "/", p_sz - 1);
        p[p_sz - 1] = 0;
    }
}

static int file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

static void fm_init_once(void)
{
    if (g_fm[0].path[0] == 0) {
        g_fm[0].loc = FM_LOC_SDCARD;
        g_fm[1].loc = FM_LOC_USB;

        strncpy(g_fm[0].path, fm_loc_base(g_fm[0].loc), sizeof(g_fm[0].path) - 1);
        strncpy(g_fm[1].path, fm_loc_base(g_fm[1].loc), sizeof(g_fm[1].path) - 1);
        g_fm[0].path[sizeof(g_fm[0].path) - 1] = 0;
        g_fm[1].path[sizeof(g_fm[1].path) - 1] = 0;

        g_fm[0].selected = g_fm[1].selected = -1;
        g_fm[0].offset = g_fm[1].offset = 0;
        g_fm_need_scan = 1;
    }
}

static void fm_set_panel_location(int idx, fm_loc_t loc)
{
    if (idx < 0 || idx > 1) return;

    fm_panel_t *p = &g_fm[idx];
    p->loc = loc;

    strncpy(p->path, fm_loc_base(loc), sizeof(p->path) - 1);
    p->path[sizeof(p->path) - 1] = 0;

    p->selected = -1;
    p->offset = 0;
    p->valid = 0;
    p->count = 0;

    g_fm_active = idx;
    g_fm_delete_armed[0] = 0;
    g_fm_need_scan = 1;

    fm_status("%s -> %s", idx == 0 ? "LEFT" : "RIGHT", fm_loc_base(loc));
}

static void fm_sort_panel(fm_panel_t *p)
{
    for (int i = 0; i < p->count; i++) {
        for (int j = i + 1; j < p->count; j++) {
            int swap = 0;
            if (p->e[j].is_dir && !p->e[i].is_dir) {
                swap = 1;
            } else if (p->e[j].is_dir == p->e[i].is_dir &&
                       strcmp(p->e[j].name, p->e[i].name) < 0) {
                swap = 1;
            }

            if (swap) {
                fm_entry_t tmp = p->e[i];
                p->e[i] = p->e[j];
                p->e[j] = tmp;
            }
        }
    }
}

static int fm_scan_panel_try(int idx, const char *scan_path)
{
    fm_panel_t *p = &g_fm[idx];
    p->count = 0;
    p->valid = 0;

    DIR *d = opendir(scan_path);
    if (!d) {
        int err = errno;
        p->selected = -1;
        p->offset = 0;
        printf("[07C_FM] opendir FAIL panel=%d path=%s errno=%d\n", idx, scan_path, err);
        return 0;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL && p->count < FM_MAX_ENTRIES) {
        if (de->d_name[0] == 0) {
            continue;
        }

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        fm_entry_t *e = &p->e[p->count];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, de->d_name, sizeof(e->name) - 1);

        char full[FM_PATH_LEN * 2];
        struct stat st;
        path_join(full, sizeof(full), scan_path, e->name);

        if (stat(full, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
            e->size = (uint32_t)st.st_size;
        } else {
            // Aunque stat falle en alguna entrada FAT, la mostramos igual.
            e->is_dir = 0;
            e->size = 0;
        }

        printf("[07C_FM] entry panel=%d path=%s name=%s dir=%d\n",
               idx, scan_path, e->name, e->is_dir);

        p->count++;
    }

    closedir(d);
    fm_sort_panel(p);
    p->valid = 1;

    if (p->count <= 0) {
        p->selected = -1;
        p->offset = 0;
    } else {
        if (p->selected < 0) p->selected = 0;
        if (p->selected >= p->count) p->selected = p->count - 1;
        if (p->offset > p->selected) p->offset = p->selected;
        if (p->selected >= p->offset + FM_VISIBLE) p->offset = p->selected - FM_VISIBLE + 1;
        if (p->offset < 0) p->offset = 0;
    }

    printf("[07C_FM] scan panel=%d path=%s valid=%d entries=%d\n",
           idx, scan_path, p->valid, p->count);
    return 1;
}

static void fm_make_alt_root_path(const char *in, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    out[0] = 0;

    if (!in) return;

    size_t n = strlen(in);
    if (n > 1 && in[n - 1] == '/') {
        strncpy(out, in, out_sz - 1);
        out[out_sz - 1] = 0;
        out[strlen(out) - 1] = 0;
    } else {
        strncpy(out, in, out_sz - 1);
        out[out_sz - 1] = 0;
        if (strlen(out) + 1 < out_sz) {
            strncat(out, "/", out_sz - strlen(out) - 1);
        }
    }
}

static void fm_probe_sd_when_empty(void)
{
    struct stat st;
    int rc_logs = stat("/sdcard/logs", &st);
    int rc_logs_slash = stat("/sdcard/logs/", &st);

    printf("[07C_FM] SD empty probe: stat(/sdcard/logs)=%d stat(/sdcard/logs/)=%d errno=%d\n",
           rc_logs, rc_logs_slash, errno);

    DIR *d = opendir("/sdcard/logs");
    if (d) {
        int n = 0;
        struct dirent *de;
        while ((de = readdir(d)) != NULL && n < 8) {
            if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
                printf("[07C_FM] SD logs entry: %s\n", de->d_name);
                n++;
            }
        }
        closedir(d);
        printf("[07C_FM] SD logs entries probe=%d\n", n);
    } else {
        printf("[07C_FM] opendir(/sdcard/logs) FAIL errno=%d\n", errno);
    }
}

static void fm_scan_panel(int idx)
{
    fm_panel_t *p = &g_fm[idx];

    int ok1 = fm_scan_panel_try(idx, p->path);
    if (ok1 && p->count > 0) {
        return;
    }

    // En algunas rutas VFS/FATFS el root es mas fiable con barra final.
    // Probamos la variante contraria /sdcard <-> /sdcard/ y /usb <-> /usb/.
    if (path_is_root(p->path)) {
        char alt[FM_PATH_LEN];
        fm_make_alt_root_path(p->path, alt, sizeof(alt));

        if (alt[0] && strcmp(alt, p->path) != 0) {
            int ok2 = fm_scan_panel_try(idx, alt);

            if (ok2) {
                // Si la variante funciona mejor, la dejamos como ruta actual.
                if (p->count > 0) {
                    strncpy(p->path, alt, sizeof(p->path) - 1);
                    p->path[sizeof(p->path) - 1] = 0;
                }

                if (g_fm[idx].loc == FM_LOC_SDCARD && p->count == 0) {
                    fm_probe_sd_when_empty();
                }
                return;
            }

            // Si la primera variante abrió pero estaba vacía, conservamos ese estado.
            if (ok1) {
                p->valid = 1;
                p->count = 0;
                p->selected = -1;
                p->offset = 0;
            }
        }
    }

    if (g_fm[idx].loc == FM_LOC_SDCARD && p->valid && p->count == 0) {
        fm_probe_sd_when_empty();
    }
}

static void fm_scan_all(void)
{
    fm_init_once();
    fm_scan_panel(0);
    fm_scan_panel(1);
    g_fm_need_scan = 0;

    if (!g_fm[0].valid && !g_fm[1].valid) {
        fm_status("scan: LEFT/RIGHT not open");
    } else if (!g_fm[0].valid) {
        fm_status("scan: LEFT fail, RIGHT %d", g_fm[1].count);
    } else if (!g_fm[1].valid) {
        fm_status("scan: LEFT %d, RIGHT fail", g_fm[0].count);
    } else {
        fm_status("scan: L %s %d, R %s %d",
                  fm_loc_label(g_fm[0].loc), g_fm[0].count,
                  fm_loc_label(g_fm[1].loc), g_fm[1].count);
    }
}

static int fm_selected_path(int idx, char *out, size_t out_sz)
{
    fm_panel_t *p = &g_fm[idx];
    if (!p->valid || p->selected < 0 || p->selected >= p->count) return 0;
    path_join(out, out_sz, p->path, p->e[p->selected].name);
    return 1;
}

static int fm_copy_file_data(const char *src, const char *dst)
{
    FILE *fi = fopen(src, "rb");
    if (!fi) return -1;

    FILE *fo = fopen(dst, "wb");
    if (!fo) {
        fclose(fi);
        return -2;
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(fi);
        fclose(fo);
        return -3;
    }

    int rc = 0;
    while (1) {
        size_t n = fread(buf, 1, 4096, fi);
        if (n > 0) {
            if (fwrite(buf, 1, n, fo) != n) {
                rc = -4;
                break;
            }
        }

        if (n < 4096) {
            if (ferror(fi)) rc = -5;
            break;
        }
    }

    heap_caps_free(buf);
    fclose(fi);
    fclose(fo);

    if (rc != 0) {
        unlink(dst);
    }

    return rc;
}

// --- Estado de progreso de copia (overlay) ---
static int  g_copy_total = 0;     // ficheros totales a copiar
static int  g_copy_done  = 0;     // ficheros ya copiados
static char g_copy_curr[FM_NAME_LEN] = {0};  // fichero en curso

// Cuenta recursivamente cuantos ficheros (no directorios) hay bajo 'path'.
// Si 'path' es un fichero, cuenta 1.
static int fm_count_files(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;

    if (!S_ISDIR(st.st_mode)) return 1;

    DIR *d = opendir(path);
    if (!d) return 0;

    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char child[FM_PATH_LEN * 2];
        path_join(child, sizeof(child), path, de->d_name);
        n += fm_count_files(child);
    }
    closedir(d);
    return n;
}

// Dibuja el overlay de progreso y lo vuelca a pantalla.
static void fm_draw_progress_overlay(void)
{
    // Ventana centrada en 400x240
    int w = 300, h = 96;
    int x = (DESK_W - w) / 2;     // 50
    int y = (DESK_H - h) / 2;     // 72

    // Marco
    panel(x, y, w, h, C_PANEL2);
    draw_text5_center(x, y + 10, w, "Copiando...", C_TEXT);

    // Texto "n/total ficheros"
    char info[48];
    snprintf(info, sizeof(info), "%d / %d ficheros", g_copy_done, g_copy_total);
    draw_text5_center(x, y + 26, w, info, C_MUTED);

    // Nombre del fichero actual (recortado)
    draw_text5_center(x, y + 40, w, g_copy_curr, C_MUTED);

    // Barra de progreso
    int bx = x + 16, by = y + 58, bw = w - 32, bh = 14;
    desk_gfx_rectfill(bx, by, bw, bh, C_PANEL);          // fondo
    int pct_w = 0;
    if (g_copy_total > 0) {
        pct_w = (int)((int64_t)bw * g_copy_done / g_copy_total);
        if (pct_w < 0) pct_w = 0;
        if (pct_w > bw) pct_w = bw;
    }
    if (pct_w > 0) {
        desk_gfx_rectfill(bx, by, pct_w, bh, C_GREEN);   // relleno
    }
    // borde de la barra
    desk_gfx_hline(bx, by, bw, C_TEXT);
    desk_gfx_hline(bx, by + bh - 1, bw, C_TEXT);
    desk_gfx_vline(bx, by, bh, C_TEXT);
    desk_gfx_vline(bx + bw - 1, by, bh, C_TEXT);

    // Porcentaje numerico
    char pc[8];
    int pct = (g_copy_total > 0) ? (100 * g_copy_done / g_copy_total) : 0;
    snprintf(pc, sizeof(pc), "%d%%", pct);
    draw_text5_center(x, y + 76, w, pc, C_TEXT);

    desktop_present_backbuffer();
}

// Copia recursiva: si 'src' es fichero, copia datos; si es directorio, crea
// 'dst' y copia todo su contenido (subcarpetas incluidas).
// Devuelve 0 si OK, negativo si error.
static int fm_copy_tree(const char *src, const char *dst)
{
    struct stat st;
    if (stat(src, &st) != 0) {
        return -10;
    }

    // --- Fichero ---
    if (!S_ISDIR(st.st_mode)) {
        // Actualizar nombre actual y repintar ANTES de copiar
        const char *base = strrchr(src, '/');
        snprintf(g_copy_curr, sizeof(g_copy_curr), "%s", base ? base + 1 : src);
        fm_draw_progress_overlay();

        int rc = fm_copy_file_data(src, dst);

        if (rc == 0) {
            g_copy_done++;
            fm_draw_progress_overlay();   // repintar tras copiar
        }
        return rc;
    }

    // --- Directorio ---
    // Crear el directorio destino (si ya existe, seguimos).
    if (mkdir(dst, 0775) != 0 && errno != EEXIST) {
        return -11;
    }

    DIR *d = opendir(src);
    if (!d) {
        return -12;
    }

    int rc = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        char src_child[FM_PATH_LEN * 2];
        char dst_child[FM_PATH_LEN * 2];
        path_join(src_child, sizeof(src_child), src, de->d_name);
        path_join(dst_child, sizeof(dst_child), dst, de->d_name);

        rc = fm_copy_tree(src_child, dst_child);
        if (rc != 0) {
            break;   // abortar al primer fallo
        }
    }

    closedir(d);
    return rc;
}

// Borrado recursivo: necesario para mover carpetas entre VFS (copiar+borrar).
static int fm_delete_tree(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -20;
    }

    if (!S_ISDIR(st.st_mode)) {
        return (unlink(path) == 0) ? 0 : -21;
    }

    DIR *d = opendir(path);
    if (!d) {
        return -22;
    }

    int rc = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        char child[FM_PATH_LEN * 2];
        path_join(child, sizeof(child), path, de->d_name);
        rc = fm_delete_tree(child);
        if (rc != 0) break;
    }
    closedir(d);

    if (rc == 0) {
        if (rmdir(path) != 0) rc = -23;
    }
    return rc;
}

// ===================== EDITOR TXT 08A (VIEW_TEXTEDIT) =====================
#define TE_MAX_SIZE       (16 * 1024)
#define TE_VISIBLE_LINES  18
#define TE_CHARS_LINE     62
#define TE_X              14
#define TE_Y              64
#define TE_LINE_H         8

static char  g_te_path[FM_PATH_LEN * 2] = "";
static char *g_te_buf = NULL;
static int   g_te_len = 0;
static int   g_te_cursor = 0;
static int   g_te_scroll_line = 0;
static int   g_te_dirty = 0;
static int   g_te_loaded = 0;
static int   g_te_truncated = 0;
static int   g_te_open_request = 0;
static char  g_te_msg[96] = "ready";

static void te_msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_te_msg, sizeof(g_te_msg), fmt, ap);
    va_end(ap);
}

static int te_endswith_ci(const char *s, const char *ext)
{
    if (!s || !ext) return 0;
    size_t ls = strlen(s), le = strlen(ext);
    if (ls < le) return 0;
    s += (ls - le);
    for (size_t i = 0; i < le; i++) {
        char a = s[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) return 0;
    }
    return 1;
}

static int te_is_supported_text(const char *path)
{
    return te_endswith_ci(path, ".txt") ||
           te_endswith_ci(path, ".log") ||
           te_endswith_ci(path, ".cfg") ||
           te_endswith_ci(path, ".ini") ||
           te_endswith_ci(path, ".json") ||
           te_endswith_ci(path, ".md") ||
           te_endswith_ci(path, ".c") ||
           te_endswith_ci(path, ".h");
}

static int te_alloc(void)
{
    if (g_te_buf) return 1;
    g_te_buf = (char *)heap_caps_malloc(TE_MAX_SIZE + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!g_te_buf) g_te_buf = (char *)heap_caps_malloc(TE_MAX_SIZE + 1, MALLOC_CAP_8BIT);
    if (!g_te_buf) {
        te_msg("no memory for editor");
        return 0;
    }
    g_te_buf[0] = 0;
    return 1;
}

static void te_reset_state(void)
{
    if (g_te_buf) g_te_buf[0] = 0;
    g_te_len = 0;
    g_te_cursor = 0;
    g_te_scroll_line = 0;
    g_te_dirty = 0;
    g_te_loaded = 0;
    g_te_truncated = 0;
    g_te_open_request = 0;
}

static void te_make_tmp_path(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    out[0] = 0;
    strncat(out, g_te_path, out_sz - 1);
    strncat(out, ".tmp", out_sz - strlen(out) - 1);
}

static int te_load_file(const char *path)
{
    if (!path || !path[0]) return 0;
    if (!te_is_supported_text(path)) {
        te_msg("unsupported file type");
        return 0;
    }
    if (!te_alloc()) return 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        te_msg("open fail errno=%d", errno);
        return 0;
    }

    te_reset_state();
    strncpy(g_te_path, path, sizeof(g_te_path) - 1);
    g_te_path[sizeof(g_te_path) - 1] = 0;

    size_t n = fread(g_te_buf, 1, TE_MAX_SIZE - 1, f);
    g_te_len = (int)n;
    g_te_buf[g_te_len] = 0;

    int extra = fgetc(f);
    if (extra != EOF) g_te_truncated = 1;
    fclose(f);

    // Normalizacion CRLF/CR/LF y limpieza ASCII.
    // CRLF se convierte en un solo LF para no duplicar lineas.
    int w = 0;
    for (int i = 0; i < g_te_len; i++) {
        unsigned char c = (unsigned char)g_te_buf[i];
        if (c == '\r') {
            if (i + 1 < g_te_len && g_te_buf[i + 1] == '\n') {
                continue;
            }
            c = '\n';
        } else if (c == '\t') {
            c = ' ';
        } else if (c < 32 && c != '\n') {
            c = ' ';
        } else if (c >= 127) {
            c = '?';
        }
        g_te_buf[w++] = (char)c;
    }
    g_te_len = w;
    g_te_buf[g_te_len] = 0;

    g_te_loaded = 1;
    g_te_open_request = 1;
    g_te_cursor = 0;
    g_te_scroll_line = 0;
    g_te_dirty = 0;

    if (g_te_truncated) te_msg("loaded first %d bytes READONLY", TE_MAX_SIZE - 1);
    else te_msg("loaded %d bytes", g_te_len);

    printf("[08A_TE] loaded path=%s bytes=%d truncated=%d\n", g_te_path, g_te_len, g_te_truncated);
    return 1;
}

static int te_consume_open_request(void)
{
    if (g_te_open_request) {
        g_te_open_request = 0;
        return 1;
    }
    return 0;
}

static int te_count_lines(void)
{
    if (!g_te_loaded || !g_te_buf) return 1;
    int lines = 1;
    for (int i = 0; i < g_te_len; i++) if (g_te_buf[i] == '\n') lines++;
    return lines;
}

static int te_line_start(int line)
{
    if (!g_te_buf || line <= 0) return 0;
    int cur = 0;
    for (int i = 0; i < g_te_len; i++) {
        if (cur == line) return i;
        if (g_te_buf[i] == '\n') {
            cur++;
            if (cur == line) return i + 1;
        }
    }
    return g_te_len;
}

static int te_line_end_from_start(int start)
{
    int i = start;
    while (i < g_te_len && g_te_buf[i] != '\n') i++;
    return i;
}

static void te_cursor_line_col(int *line, int *col)
{
    int l = 0, c = 0;
    if (!g_te_buf) {
        if (line) *line = 0;
        if (col) *col = 0;
        return;
    }
    for (int i = 0; i < g_te_cursor && i < g_te_len; i++) {
        if (g_te_buf[i] == '\n') { l++; c = 0; }
        else c++;
    }
    if (line) *line = l;
    if (col) *col = c;
}

static void te_ensure_cursor_visible(void)
{
    int l = 0, c = 0;
    te_cursor_line_col(&l, &c);
    (void)c;
    if (l < g_te_scroll_line) g_te_scroll_line = l;
    if (l >= g_te_scroll_line + TE_VISIBLE_LINES) g_te_scroll_line = l - TE_VISIBLE_LINES + 1;
    if (g_te_scroll_line < 0) g_te_scroll_line = 0;
}

static void te_insert_char(char ch)
{
    if (!g_te_loaded || !g_te_buf || g_te_truncated) {
        te_msg(g_te_truncated ? "readonly: file too large" : "no file");
        return;
    }
    if (g_te_len >= TE_MAX_SIZE - 2) {
        te_msg("editor buffer full");
        return;
    }
    if (ch == '\r') ch = '\n';
    if (ch == '\t') ch = ' ';
    if ((unsigned char)ch < 32 && ch != '\n') return;
    if ((unsigned char)ch >= 127) ch = '?';
    memmove(&g_te_buf[g_te_cursor + 1], &g_te_buf[g_te_cursor], (size_t)(g_te_len - g_te_cursor + 1));
    g_te_buf[g_te_cursor] = ch;
    g_te_cursor++;
    g_te_len++;
    g_te_dirty = 1;
    te_ensure_cursor_visible();
    te_msg("modified");
}

static void te_backspace(void)
{
    if (!g_te_loaded || !g_te_buf || g_te_truncated) {
        te_msg(g_te_truncated ? "readonly: file too large" : "no file");
        return;
    }
    if (g_te_cursor <= 0) return;
    memmove(&g_te_buf[g_te_cursor - 1], &g_te_buf[g_te_cursor], (size_t)(g_te_len - g_te_cursor + 1));
    g_te_cursor--;
    g_te_len--;
    g_te_dirty = 1;
    te_ensure_cursor_visible();
    te_msg("modified");
}

static void te_move_left(void)  { if (g_te_cursor > 0) g_te_cursor--; te_ensure_cursor_visible(); }
static void te_move_right(void) { if (g_te_cursor < g_te_len) g_te_cursor++; te_ensure_cursor_visible(); }

static void te_move_up(void)
{
    int line = 0, col = 0;
    te_cursor_line_col(&line, &col);
    if (line <= 0) return;
    int prev_start = te_line_start(line - 1);
    int prev_end = te_line_end_from_start(prev_start);
    int prev_len = prev_end - prev_start;
    if (col > prev_len) col = prev_len;
    g_te_cursor = prev_start + col;
    te_ensure_cursor_visible();
}

static void te_move_down(void)
{
    int line = 0, col = 0;
    te_cursor_line_col(&line, &col);
    int lines = te_count_lines();
    if (line >= lines - 1) return;
    int next_start = te_line_start(line + 1);
    int next_end = te_line_end_from_start(next_start);
    int next_len = next_end - next_start;
    if (col > next_len) col = next_len;
    g_te_cursor = next_start + col;
    te_ensure_cursor_visible();
}

static void te_scroll_page(int dir)
{
    int lines = te_count_lines();
    g_te_scroll_line += dir * (TE_VISIBLE_LINES - 2);
    if (g_te_scroll_line < 0) g_te_scroll_line = 0;
    if (g_te_scroll_line > lines - 1) g_te_scroll_line = (lines > 0) ? lines - 1 : 0;
}

static int te_save_file(void)
{
    if (!g_te_loaded || !g_te_buf || !g_te_path[0]) {
        te_msg("no file loaded");
        return 0;
    }
    if (g_te_truncated) {
        te_msg("save disabled: file too large");
        return 0;
    }
    char tmp[sizeof(g_te_path) + 8];
    te_make_tmp_path(tmp, sizeof(tmp));
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        te_msg("tmp open fail errno=%d", errno);
        return 0;
    }
    size_t wr = fwrite(g_te_buf, 1, (size_t)g_te_len, f);
    int ferr = ferror(f);
    fclose(f);
    if (wr != (size_t)g_te_len || ferr) {
        unlink(tmp);
        te_msg("write fail");
        return 0;
    }
    unlink(g_te_path);
    if (rename(tmp, g_te_path) != 0) {
        te_msg("rename fail errno=%d", errno);
        return 0;
    }
    g_te_dirty = 0;
    te_msg("saved %d bytes", g_te_len);
    printf("[08A_TE] saved path=%s bytes=%d\n", g_te_path, g_te_len);
    return 1;
}

static int te_save_button_hover(int mx, int my)
{
    return inside(mx, my, 322, 30, 60, 18);
}

static void draw_textedit_page(int mx, int my)
{
    draw_background();
    draw_topbar(mx, my);
    panel(8, 25, 384, 194, C_PANEL);
    draw_button(16, 30, 50, 18, "BACK", back_button_hover(mx, my));
    draw_button(322, 30, 60, 18, "SAVE", te_save_button_hover(mx, my));
    char title[64];
    char short_path[42];
    desk_trunc_copy(short_path, sizeof(short_path), g_te_path, 38);
    strncpy(title, "TXT ", sizeof(title) - 1);
    title[sizeof(title) - 1] = 0;
    strncat(title, short_path[0] ? short_path : "no file", sizeof(title) - strlen(title) - 1);
    draw_text5(76, 36, title, C_CYAN);
    desk_gfx_hline(14, 54, 372, C_BORDER);
    if (!g_te_loaded || !g_te_buf) {
        draw_text5(TE_X, TE_Y, "No TXT loaded", C_RED);
        draw_footer();
        return;
    }
    char info[64];
    snprintf(info, sizeof(info), "%s L:%d/%d B:%d", g_te_dirty ? "MOD" : "OK", g_te_scroll_line + 1, te_count_lines(), g_te_len);
    draw_text5(14, 56, info, g_te_dirty ? C_YELLOW : C_MUTED);
    char msg_short[40];
    desk_trunc_copy(msg_short, sizeof(msg_short), g_te_msg, 36);
    draw_text5(178, 56, msg_short, C_MUTED);
    desk_gfx_rectfill(10, 62, 380, 152, C_BG0);
    desk_gfx_rect(10, 62, 380, 152, C_BORDER);
    int cursor_line = 0, cursor_col = 0;
    te_cursor_line_col(&cursor_line, &cursor_col);
    for (int row = 0; row < TE_VISIBLE_LINES; row++) {
        int line_no = g_te_scroll_line + row;
        int start = te_line_start(line_no);
        int end = te_line_end_from_start(start);
        if (start > g_te_len) break;
        char linebuf[TE_CHARS_LINE + 1];
        int n = end - start;
        if (n > TE_CHARS_LINE) n = TE_CHARS_LINE;
        if (n < 0) n = 0;
        for (int i = 0; i < n; i++) {
            char c = g_te_buf[start + i];
            if (c < 32 || c >= 127) c = ' ';
            linebuf[i] = c;
        }
        linebuf[n] = 0;
        draw_text5(TE_X, TE_Y + row * TE_LINE_H, linebuf, C_TEXT);
    }
    if (cursor_line >= g_te_scroll_line && cursor_line < g_te_scroll_line + TE_VISIBLE_LINES) {
        int cx = TE_X + cursor_col * 6;
        int cy = TE_Y + (cursor_line - g_te_scroll_line) * TE_LINE_H;
        if (cx > 384) cx = 384;
        desk_gfx_rectfill(cx, cy, 2, 7, C_YELLOW);
    }
    if (g_te_truncated) draw_text5(14, 206, "READONLY: fichero grande/truncado", C_RED);
    else draw_text5(14, 206, "ESC/BACK vuelve  Ctrl+S/SAVE guarda  flechas cursor", C_MUTED);
    draw_footer();
}

static int textedit_handle_click(int mx, int my, desktop_view_t *view)
{
    if (back_button_hover(mx, my)) {
        if (view) *view = VIEW_FILES;
        return 1;
    }
    if (te_save_button_hover(mx, my)) {
        te_save_file();
        return 1;
    }
    if (inside(mx, my, 10, 62, 380, 152)) {
        int row = (my - TE_Y) / TE_LINE_H;
        int col = (mx - TE_X) / 6;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        if (col > TE_CHARS_LINE) col = TE_CHARS_LINE;
        int line_no = g_te_scroll_line + row;
        int start = te_line_start(line_no);
        int end = te_line_end_from_start(start);
        int len = end - start;
        if (col > len) col = len;
        g_te_cursor = start + col;
        te_ensure_cursor_visible();
        return 1;
    }
    if (my > 160) {
        te_scroll_page(1);
        return 1;
    } else if (my > 54) {
        te_scroll_page(-1);
        return 1;
    }
    return 0;
}

static void textedit_handle_arrow(int arrow)
{
    if (arrow == 'U') te_move_up();
    else if (arrow == 'D') te_move_down();
    else if (arrow == 'L') te_move_left();
    else if (arrow == 'R') te_move_right();
}

static void textedit_handle_key(int ch, desktop_view_t *view)
{
    if (ch < 0) return;
    if (ch == 27) {
        if (view) *view = VIEW_FILES;
        return;
    }
    if (ch == 19) { te_save_file(); return; }  // Ctrl+S
    if (ch == 8 || ch == 127) { te_backspace(); return; }
    if (ch == '\r' || ch == '\n') { te_insert_char('\n'); return; }
    if (ch >= 32 && ch < 127) { te_insert_char((char)ch); return; }
}

static void fm_open_active(void)
{
    fm_panel_t *p = &g_fm[g_fm_active];

    if (!p->valid || p->selected < 0 || p->selected >= p->count) {
        fm_status("nothing selected");
        return;
    }

    if (!p->e[p->selected].is_dir) {
        char fpath[FM_PATH_LEN * 2];
        path_join(fpath, sizeof(fpath), p->path, p->e[p->selected].name);

        if (te_is_supported_text(fpath)) {
            if (te_load_file(fpath)) {
                fm_status("edit: %s", p->e[p->selected].name);
            } else {
                fm_status("edit open failed");
            }
        } else {
            fm_status("not text: %s", p->e[p->selected].name);
        }
        return;
    }

    char next[FM_PATH_LEN];
    path_join(next, sizeof(next), p->path, p->e[p->selected].name);

    strncpy(p->path, next, sizeof(p->path) - 1);
    p->path[sizeof(p->path) - 1] = 0;
    p->selected = -1;
    p->offset = 0;
    g_fm_delete_armed[0] = 0;
    g_fm_need_scan = 1;
    fm_status("opened %s", p->path);
}

static void fm_up_active(void)
{
    fm_panel_t *p = &g_fm[g_fm_active];
    if (fm_panel_at_base(g_fm_active)) {
        fm_status("already at %s", fm_loc_label(p->loc));
        return;
    }

    path_parent(p->path, sizeof(p->path));

    // Seguridad: si por cualquier motivo se sube por encima del root elegido,
    // volver al root de esa ventana.
    const char *base = fm_loc_base(p->loc);
    if (!fm_path_equal_noslash(p->path, base) &&
        strncmp(p->path, base, strlen(base)) != 0) {
        strncpy(p->path, base, sizeof(p->path) - 1);
        p->path[sizeof(p->path) - 1] = 0;
    }

    p->selected = -1;
    p->offset = 0;
    g_fm_delete_armed[0] = 0;
    g_fm_need_scan = 1;
    fm_status("up: %s", p->path);
}

static void fm_copy_active_to_other(void)
{
    int src_idx = g_fm_active;
    int dst_idx = 1 - g_fm_active;
    fm_panel_t *srcp = &g_fm[src_idx];

    char src[FM_PATH_LEN * 2];
    char dst[FM_PATH_LEN * 2];

    if (!fm_selected_path(src_idx, src, sizeof(src))) {
        fm_status("copy: nothing selected");
        return;
    }

    path_join(dst, sizeof(dst), g_fm[dst_idx].path, srcp->e[srcp->selected].name);

    if (file_exists(dst)) {
        fm_status("copy: destination exists");
        return;
    }

    int es_dir = srcp->e[srcp->selected].is_dir;

    // Preparar progreso
    g_copy_total = fm_count_files(src);
    g_copy_done  = 0;
    g_copy_curr[0] = 0;
    fm_draw_progress_overlay();   // mostrar overlay inicial (0%)

    int rc = fm_copy_tree(src, dst);
    if (rc == 0) {
        g_fm_need_scan = 1;
        fm_status(es_dir ? "copied folder %s" : "copied %s",
                  srcp->e[srcp->selected].name);
    } else {
        fm_status("copy error %d", rc);
    }
}

static void fm_move_active_to_other(void)
{
    int src_idx = g_fm_active;
    int dst_idx = 1 - g_fm_active;
    fm_panel_t *srcp = &g_fm[src_idx];

    char src[FM_PATH_LEN * 2];
    char dst[FM_PATH_LEN * 2];

    if (!fm_selected_path(src_idx, src, sizeof(src))) {
        fm_status("move: nothing selected");
        return;
    }

    path_join(dst, sizeof(dst), g_fm[dst_idx].path, srcp->e[srcp->selected].name);

    if (file_exists(dst)) {
        fm_status("move: destination exists");
        return;
    }

    char moved_name[FM_NAME_LEN];
    strncpy(moved_name, srcp->e[srcp->selected].name, sizeof(moved_name) - 1);
    moved_name[sizeof(moved_name) - 1] = 0;

    if (rename(src, dst) == 0) {
        srcp->selected = -1;
        g_fm_need_scan = 1;
        fm_status("moved %s", moved_name);
        return;
    }

    // Entre VFS distintos (SD <-> USB) rename falla: copiar + borrar.
    int es_dir = srcp->e[srcp->selected].is_dir;

    g_copy_total = fm_count_files(src);
    g_copy_done  = 0;
    g_copy_curr[0] = 0;
    fm_draw_progress_overlay();

    int rc = fm_copy_tree(src, dst);
    if (rc == 0) {
        if (es_dir) {
            fm_delete_tree(src);
        } else {
            unlink(src);
        }
        srcp->selected = -1;
        g_fm_need_scan = 1;
        fm_status(es_dir ? "moved folder by copy" : "moved by copy");
    } else {
        // limpiar destino parcial si quedo a medias
        if (es_dir) fm_delete_tree(dst); else unlink(dst);
        fm_status("move error %d", rc);
    }
}

static void fm_delete_active(void)
{
    int idx = g_fm_active;
    fm_panel_t *p = &g_fm[idx];

    char target[FM_PATH_LEN * 2];

    if (!fm_selected_path(idx, target, sizeof(target))) {
        fm_status("delete: nothing selected");
        return;
    }

    if (strcmp(g_fm_delete_armed, target) != 0) {
        strncpy(g_fm_delete_armed, target, sizeof(g_fm_delete_armed) - 1);
        g_fm_delete_armed[sizeof(g_fm_delete_armed) - 1] = 0;
        fm_status("click DEL again to confirm");
        return;
    }

    int rc;
    if (p->e[p->selected].is_dir) {
        rc = fm_delete_tree(target);   // Borrado recursivo: vacia y borra.
    } else {
        rc = unlink(target);
    }

    if (rc == 0) {
        p->selected = -1;
        g_fm_delete_armed[0] = 0;
        g_fm_need_scan = 1;
        fm_status("deleted");
    } else {
        fm_status("delete failed errno=%d", errno);
    }
}

static void fm_prev_active(void)
{
    fm_panel_t *p = &g_fm[g_fm_active];
    if (p->offset > 0) {
        p->offset -= FM_VISIBLE;
        if (p->offset < 0) p->offset = 0;
        p->selected = p->offset;
    }
}

static void fm_next_active(void)
{
    fm_panel_t *p = &g_fm[g_fm_active];
    if (p->offset + FM_VISIBLE < p->count) {
        p->offset += FM_VISIBLE;
        if (p->offset >= p->count) p->offset = p->count - 1;
        p->selected = p->offset;
    }
}

// Mover la seleccion UNA fila (no una pagina entera). Usadas por las
// flechas arriba/abajo del teclado, para recorrer fila a fila como en
// cualquier gestor de archivos. Los botones "<"/">" (pagina) siguen usando
// fm_prev_active()/fm_next_active() de arriba, sin tocar.
static void fm_select_prev_row(void)
{
    fm_panel_t *p = &g_fm[g_fm_active];
    if (p->count <= 0) return;
    if (p->selected > 0) {
        p->selected--;
        if (p->selected < p->offset) p->offset = p->selected;
    }
}

static void fm_select_next_row(void)
{
    fm_panel_t *p = &g_fm[g_fm_active];
    if (p->count <= 0) return;
    if (p->selected < p->count - 1) {
        p->selected++;
        if (p->selected >= p->offset + FM_VISIBLE) p->offset = p->selected - FM_VISIBLE + 1;
    }
}

static int fm_hit_location_button(int mx, int my, int *panel_idx, fm_loc_t *loc)
{
    const int px[2] = {22, 204};
    const int py = 64;

    for (int i = 0; i < 2; i++) {
        int bx = px[i] + 6;
        int by = py + 18;

        if (inside(mx, my, bx,      by, 22, 14)) { *panel_idx = i; *loc = FM_LOC_ROOT;    return 1; }
        if (inside(mx, my, bx + 25, by, 32, 14)) { *panel_idx = i; *loc = FM_LOC_ROOTBIN; return 1; }
        if (inside(mx, my, bx + 60, by, 26, 14)) { *panel_idx = i; *loc = FM_LOC_SDCARD;  return 1; }
        if (inside(mx, my, bx + 89, by, 34, 14)) { *panel_idx = i; *loc = FM_LOC_USB;     return 1; }
    }

    return 0;
}

static int fm_location_button_hover(int mx, int my, int panel_idx, fm_loc_t loc)
{
    int pidx = -1;
    fm_loc_t hloc = FM_LOC_ROOT;
    if (!fm_hit_location_button(mx, my, &pidx, &hloc)) return 0;
    return (pidx == panel_idx && hloc == loc);
}

static int fm_hit_panel(int mx, int my, int *panel_idx, int *entry_idx)
{
    const int px[2] = {22, 204};
    const int py = 64;
    const int pw = 174;
    const int ph = 132;
    const int list_y = 98;
    const int row_h = 11;

    for (int i = 0; i < 2; i++) {
        if (inside(mx, my, px[i], py, pw, ph)) {
            *panel_idx = i;

            int row = (my - list_y) / row_h;
            if (my >= list_y && row >= 0 && row < FM_VISIBLE) {
                int abs_idx = g_fm[i].offset + row;
                if (abs_idx >= 0 && abs_idx < g_fm[i].count) {
                    *entry_idx = abs_idx;
                } else {
                    *entry_idx = -1;
                }
            } else {
                *entry_idx = -1;
            }
            return 1;
        }
    }

    return 0;
}

typedef enum {
    FM_BTN_NONE = 0,
    FM_BTN_OPEN,
    FM_BTN_UP,
    FM_BTN_COPY,
    FM_BTN_MOVE,
    FM_BTN_DEL,
    FM_BTN_PREV,
    FM_BTN_NEXT,
    FM_BTN_GZIP,
} fm_btn_t;

static fm_btn_t fm_hit_button(int mx, int my)
{
    const int y = 200;
    if (inside(mx, my,  16, y, 42, 18)) return FM_BTN_OPEN;
    if (inside(mx, my,  60, y, 26, 18)) return FM_BTN_UP;
    if (inside(mx, my,  88, y, 42, 18)) return FM_BTN_COPY;
    if (inside(mx, my, 132, y, 42, 18)) return FM_BTN_MOVE;
    if (inside(mx, my, 176, y, 30, 18)) return FM_BTN_DEL;
    if (inside(mx, my, 208, y, 22, 18)) return FM_BTN_PREV;
    if (inside(mx, my, 232, y, 22, 18)) return FM_BTN_NEXT;
    if (inside(mx, my, 256, y, 44, 18)) return FM_BTN_GZIP;
    return FM_BTN_NONE;
}

static int fm_button_hover(int mx, int my, fm_btn_t b)
{
    return fm_hit_button(mx, my) == b;
}

static void fm_draw_panel(int idx, int x, int y, int mx, int my)
{
    fm_panel_t *p = &g_fm[idx];
    int active = (idx == g_fm_active);

    panel(x, y, 174, 132, active ? C_PANEL2 : C_PANEL);

    char short_path[32];
    char title[48];

    desk_trunc_copy(short_path, sizeof(short_path), p->path, 22);

    strncpy(title, idx == 0 ? "LEFT " : "RIGHT ", sizeof(title) - 1);
    title[sizeof(title) - 1] = 0;
    strncat(title, short_path, sizeof(title) - strlen(title) - 1);

    draw_text5(x + 6, y + 7, title, active ? C_YELLOW : C_MUTED);

    // Selector de ubicación propio de cada ventana/panel.
    // Cada panel puede estar en /root, /root/bin, /sdcard o /usb.
    int by = y + 18;
    draw_button_tiny(x + 6,  by, 22, 14, "/",   fm_location_button_hover(mx, my, idx, FM_LOC_ROOT)    || p->loc == FM_LOC_ROOT);
    draw_button_tiny(x + 31, by, 32, 14, "BIN", fm_location_button_hover(mx, my, idx, FM_LOC_ROOTBIN) || p->loc == FM_LOC_ROOTBIN);
    draw_button_tiny(x + 66, by, 26, 14, "SD",  fm_location_button_hover(mx, my, idx, FM_LOC_SDCARD)  || p->loc == FM_LOC_SDCARD);
    draw_button_tiny(x + 95, by, 34, 14, "USB", fm_location_button_hover(mx, my, idx, FM_LOC_USB)     || p->loc == FM_LOC_USB);

    if (!p->valid) {
        draw_text5(x + 8, y + 44, "not open", C_RED);
        draw_text5(x + 8, y + 58, fm_loc_base(p->loc), C_MUTED);
        return;
    }

    if (p->count == 0) {
        draw_text5(x + 8, y + 44, "empty", C_MUTED);
        draw_text5(x + 8, y + 58, "REFRESH to rescan", C_MUTED);
        return;
    }

    int list_y = y + 34;
    for (int r = 0; r < FM_VISIBLE; r++) {
        int ei = p->offset + r;
        if (ei >= p->count) break;

        int ry = list_y + r * 11;
        int sel = (ei == p->selected);
        int hover = inside(mx, my, x + 3, ry - 1, 168, 11);

        if (sel) {
            desk_gfx_rectfill(x + 3, ry - 1, 168, 11, C_BLUE);
        } else if (hover) {
            desk_gfx_rectfill(x + 3, ry - 1, 168, 11, C_TOP);
        }

        char nm[40];
        strncpy(nm, p->e[ei].name, sizeof(nm) - 1);
        nm[sizeof(nm) - 1] = 0;
        if (strlen(nm) > 20) {
            nm[17] = '.';
            nm[18] = '.';
            nm[19] = '.';
            nm[20] = 0;
        }

        char line[48];
        snprintf(line, sizeof(line), "%s %s", p->e[ei].is_dir ? "[D]" : "[F]", nm);
        draw_text5(x + 7, ry + 1, line, sel ? C_WHITE : C_TEXT);
    }

    char info[32];
    snprintf(info, sizeof(info), "%d items", p->count);
    draw_text5(x + 118, y + 119, info, C_MUTED);
}

static void draw_files_page(int mx, int my)
{
    fm_init_once();

    int sd_mounted = minipc_sd_is_mounted() ? 1 : 0;
    int usb_mounted = minipc_usb_msc_mounted() ? 1 : 0;

    if (sd_mounted != g_fm_last_sd_mounted ||
        usb_mounted != g_fm_last_usb_mounted) {
        g_fm_last_sd_mounted = sd_mounted;
        g_fm_last_usb_mounted = usb_mounted;
        g_fm_need_scan = 1;
    }

    if (g_fm_need_scan) {
        fm_scan_all();
    }

    draw_window_base(mx, my, "FILES - manager multiroot", 1);

    draw_text5(98, 56, g_fm_status, C_MUTED);

    fm_draw_panel(0, 22, 64, mx, my);
    fm_draw_panel(1, 204, 64, mx, my);

    int y = 200;
    draw_button( 16, y, 42, 18, "OPEN", fm_button_hover(mx, my, FM_BTN_OPEN));
    draw_button( 60, y, 26, 18, "UP",   fm_button_hover(mx, my, FM_BTN_UP));
    draw_button( 88, y, 42, 18, "COPY", fm_button_hover(mx, my, FM_BTN_COPY));
    draw_button(132, y, 42, 18, "MOVE", fm_button_hover(mx, my, FM_BTN_MOVE));
    draw_button(176, y, 30, 18, "DEL",  fm_button_hover(mx, my, FM_BTN_DEL));
    draw_button(208, y, 22, 18, "<",    fm_button_hover(mx, my, FM_BTN_PREV));
    draw_button(232, y, 22, 18, ">",    fm_button_hover(mx, my, FM_BTN_NEXT));
    draw_button(256, y, 44, 18, "GZIP", fm_button_hover(mx, my, FM_BTN_GZIP));

    draw_text5(306, 204, g_fm_active == 0 ? "L>R" : "R>L", C_CYAN);
}

// ---------------------------------------------------------------------------
// TAR helper para comprimir carpetas completas.
// gzip solo comprime un fichero/stream; para una carpeta primero hay que
// empaquetarla como .tar y luego comprimir ese .tar a .tar.gz.
// Implementamos TAR aqui para no depender de que exista un comando "tar" externo.
// ---------------------------------------------------------------------------

#define FM_TAR_BLOCK 512
#define FM_TAR_MAX_DEPTH 12

static void fm_tar_octal(char *dst, size_t len, unsigned long long value)
{
    if (!dst || len == 0) return;

    // Campo octal tradicional TAR: ceros a la izquierda y NUL final.
    // Para chksum se ajusta aparte.
    snprintf(dst, len, "%0*llo", (int)len - 1, value);
}

static int fm_tar_write_zeros(FILE *f, size_t n)
{
    static const uint8_t zeros[FM_TAR_BLOCK] = {0};

    while (n > 0) {
        size_t chunk = n > sizeof(zeros) ? sizeof(zeros) : n;
        if (fwrite(zeros, 1, chunk, f) != chunk) return -1;
        n -= chunk;
    }

    return 0;
}

static const char *fm_basename_ptr(const char *path)
{
    if (!path || !path[0]) return "item";

    const char *end = path + strlen(path);
    while (end > path && end[-1] == '/') end--;

    const char *p = end;
    while (p > path && p[-1] != '/') p--;

    return p;
}

static void fm_copy_basename(char *out, size_t out_sz, const char *path)
{
    if (!out || out_sz == 0) return;

    const char *bn = fm_basename_ptr(path);
    strncpy(out, bn, out_sz - 1);
    out[out_sz - 1] = 0;

    // Quitar barra final si la hubiera.
    size_t l = strlen(out);
    while (l > 1 && out[l - 1] == '/') {
        out[--l] = 0;
    }
}

static int fm_tar_write_header(FILE *tar, const char *tar_name,
                               const struct stat *st, int is_dir)
{
    if (!tar || !tar_name || !tar_name[0] || !st) return -1;

    uint8_t h[FM_TAR_BLOCK];
    memset(h, 0, sizeof(h));

    char name[128];
    strncpy(name, tar_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;

    if (is_dir) {
        size_t l = strlen(name);
        if (l > 0 && l < sizeof(name) - 1 && name[l - 1] != '/') {
            name[l] = '/';
            name[l + 1] = 0;
        }
    }

    // Nuestro empaquetador compacto soporta nombres <= 100 bytes.
    // Para el MiniPC es suficiente y evita complicar prefijos USTAR largos.
    if (strlen(name) > 99) {
        fm_status("tar: nombre largo omitido");
        return 0;
    }

    memcpy(h + 0, name, strlen(name));
    fm_tar_octal((char *)h + 100, 8,  is_dir ? 0755 : 0644);
    fm_tar_octal((char *)h + 108, 8,  0);
    fm_tar_octal((char *)h + 116, 8,  0);
    fm_tar_octal((char *)h + 124, 12, is_dir ? 0ULL : (unsigned long long)st->st_size);
    fm_tar_octal((char *)h + 136, 12, (unsigned long long)st->st_mtime);

    memset(h + 148, ' ', 8);
    h[156] = is_dir ? '5' : '0';

    memcpy(h + 257, "ustar", 5);
    memcpy(h + 263, "00", 2);
    memcpy(h + 265, "arielo", 6);
    memcpy(h + 297, "arielo", 6);

    unsigned int sum = 0;
    for (int i = 0; i < FM_TAR_BLOCK; i++) {
        sum += h[i];
    }

    snprintf((char *)h + 148, 8, "%06o", sum);
    h[154] = '\0';
    h[155] = ' ';

    return fwrite(h, 1, FM_TAR_BLOCK, tar) == FM_TAR_BLOCK ? 0 : -1;
}

static int fm_tar_add_recursive(FILE *tar, const char *fs_path,
                                const char *tar_name, int depth, int *items)
{
    if (!tar || !fs_path || !tar_name || !items) return -1;

    if (depth > FM_TAR_MAX_DEPTH) {
        fm_status("tar: demasiada profundidad");
        return -1;
    }

    struct stat st;
    if (stat(fs_path, &st) != 0) {
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        if (fm_tar_write_header(tar, tar_name, &st, 1) != 0) return -1;
        (*items)++;

        DIR *d = opendir(fs_path);
        if (!d) return -1;

        struct dirent *de;
        int rc = 0;

        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

            char child_fs[FM_PATH_LEN * 2];
            char child_tar[FM_PATH_LEN * 2];

            path_join(child_fs, sizeof(child_fs), fs_path, de->d_name);

            snprintf(child_tar, sizeof(child_tar), "%s/%s", tar_name, de->d_name);

            if (fm_tar_add_recursive(tar, child_fs, child_tar, depth + 1, items) != 0) {
                rc = -1;
                break;
            }
        }

        closedir(d);
        return rc;
    }

    if (!S_ISREG(st.st_mode)) {
        // Saltar entradas especiales si aparecieran.
        return 0;
    }

    if (fm_tar_write_header(tar, tar_name, &st, 0) != 0) return -1;

    FILE *in = fopen(fs_path, "rb");
    if (!in) return -1;

    uint8_t buf[1024];
    size_t rd;
    int rc = 0;
    unsigned long long written = 0;

    while ((rd = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, rd, tar) != rd) {
            rc = -1;
            break;
        }
        written += rd;
    }

    if (ferror(in)) rc = -1;
    fclose(in);

    if (rc == 0) {
        size_t pad = (FM_TAR_BLOCK - (written % FM_TAR_BLOCK)) % FM_TAR_BLOCK;
        if (pad && fm_tar_write_zeros(tar, pad) != 0) rc = -1;
    }

    if (rc == 0) (*items)++;
    return rc;
}

static unsigned long long fm_tar_parse_octal(const uint8_t *p, size_t len)
{
    unsigned long long v = 0;
    if (!p || len == 0) return 0;

    for (size_t i = 0; i < len; i++) {
        uint8_t c = p[i];

        if (c == 0 || c == ' ') {
            if (v != 0) break;
            continue;
        }

        if (c >= '0' && c <= '7') {
            v = (v << 3) + (unsigned long long)(c - '0');
        } else {
            break;
        }
    }

    return v;
}

static int fm_tar_header_is_empty(const uint8_t *h)
{
    for (int i = 0; i < FM_TAR_BLOCK; i++) {
        if (h[i] != 0) return 0;
    }
    return 1;
}

static int fm_tar_relpath_safe(const char *name)
{
    if (!name || !name[0]) return 0;
    if (name[0] == '/') return 0;
    if (strstr(name, "..")) return 0;
    if (strchr(name, ':')) return 0;
    return 1;
}

static int fm_mkdir_p(const char *path)
{
    if (!path || !path[0]) return -1;

    char tmp[FM_PATH_LEN * 2];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;

    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') {
        tmp[--len] = 0;
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (strlen(tmp) > 0) {
                struct stat st;
                if (stat(tmp, &st) != 0) {
                    if (mkdir(tmp, 0775) != 0) return -1;
                } else if (!S_ISDIR(st.st_mode)) {
                    return -1;
                }
            }
            *p = '/';
        }
    }

    struct stat st;
    if (stat(tmp, &st) != 0) {
        if (mkdir(tmp, 0775) != 0) return -1;
    } else if (!S_ISDIR(st.st_mode)) {
        return -1;
    }

    return 0;
}

static int fm_make_parent_dirs(const char *file_path)
{
    if (!file_path || !file_path[0]) return -1;

    char dir[FM_PATH_LEN * 2];
    strncpy(dir, file_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;

    char *slash = strrchr(dir, '/');
    if (!slash) return 0;
    if (slash == dir) return 0;

    *slash = 0;
    return fm_mkdir_p(dir);
}

static void fm_tar_join_extract_path(char *out, size_t out_sz,
                                     const char *base_dir, const char *rel)
{
    if (!out || out_sz == 0) return;
    out[0] = 0;

    if (!base_dir || !base_dir[0]) {
        snprintf(out, out_sz, "%s", rel ? rel : "");
        return;
    }

    if (base_dir[strlen(base_dir) - 1] == '/') {
        snprintf(out, out_sz, "%s%s", base_dir, rel ? rel : "");
    } else {
        snprintf(out, out_sz, "%s/%s", base_dir, rel ? rel : "");
    }
}

static int fm_tar_first_root_exists(FILE *tar, const char *dest_dir)
{
    if (!tar || !dest_dir) return 1;

    long old_pos = ftell(tar);
    if (old_pos < 0) old_pos = 0;
    fseek(tar, 0, SEEK_SET);

    uint8_t h[FM_TAR_BLOCK];
    int exists = 0;

    if (fread(h, 1, FM_TAR_BLOCK, tar) == FM_TAR_BLOCK && !fm_tar_header_is_empty(h)) {
        char name[128];
        memcpy(name, h, 100);
        name[100] = 0;

        if (fm_tar_relpath_safe(name)) {
            char root[128];
            strncpy(root, name, sizeof(root) - 1);
            root[sizeof(root) - 1] = 0;

            char *slash = strchr(root, '/');
            if (slash) *slash = 0;

            size_t l = strlen(root);
            while (l > 0 && root[l - 1] == '/') {
                root[--l] = 0;
            }

            if (root[0]) {
                char full[FM_PATH_LEN * 2];
                fm_tar_join_extract_path(full, sizeof(full), dest_dir, root);

                struct stat st;
                if (stat(full, &st) == 0) exists = 1;
            }
        }
    }

    fseek(tar, old_pos, SEEK_SET);
    return exists;
}

static int fm_extract_tar_to_dir(const char *tar_path, const char *dest_dir, int *items)
{
    if (!tar_path || !dest_dir || !items) return -1;

    FILE *tar = fopen(tar_path, "rb");
    if (!tar) return -1;

    if (fm_tar_first_root_exists(tar, dest_dir)) {
        fclose(tar);
        fm_status("untar: destino existe");
        return -2;
    }

    *items = 0;
    uint8_t h[FM_TAR_BLOCK];
    int rc = 0;

    while (fread(h, 1, FM_TAR_BLOCK, tar) == FM_TAR_BLOCK) {
        if (fm_tar_header_is_empty(h)) {
            break;
        }

        char name[128];
        memcpy(name, h, 100);
        name[100] = 0;

        if (!fm_tar_relpath_safe(name)) {
            rc = -1;
            break;
        }

        unsigned long long size = fm_tar_parse_octal(h + 124, 12);
        char type = (char)h[156];

        char out_path[FM_PATH_LEN * 2];
        fm_tar_join_extract_path(out_path, sizeof(out_path), dest_dir, name);

        if (type == '5') {
            if (fm_mkdir_p(out_path) != 0) {
                rc = -1;
                break;
            }
            (*items)++;
        } else if (type == '0' || type == 0) {
            if (fm_make_parent_dirs(out_path) != 0) {
                rc = -1;
                break;
            }

            FILE *out = fopen(out_path, "wb");
            if (!out) {
                rc = -1;
                break;
            }

            unsigned long long left = size;
            uint8_t buf[1024];

            while (left > 0) {
                size_t want = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
                size_t rd = fread(buf, 1, want, tar);
                if (rd != want) {
                    rc = -1;
                    break;
                }
                if (fwrite(buf, 1, rd, out) != rd) {
                    rc = -1;
                    break;
                }
                left -= rd;
            }

            if (fclose(out) != 0 && rc == 0) rc = -1;
            if (rc != 0) break;

            size_t pad = (FM_TAR_BLOCK - (size % FM_TAR_BLOCK)) % FM_TAR_BLOCK;
            if (pad && fseek(tar, (long)pad, SEEK_CUR) != 0) {
                rc = -1;
                break;
            }

            (*items)++;
        } else {
            // Tipo no soportado; saltar contenido si lo tuviera.
            size_t skip = (size_t)size;
            size_t pad = (FM_TAR_BLOCK - (skip % FM_TAR_BLOCK)) % FM_TAR_BLOCK;
            if (fseek(tar, (long)(skip + pad), SEEK_CUR) != 0) {
                rc = -1;
                break;
            }
        }
    }

    fclose(tar);
    return rc;
}


static int fm_create_tar_from_dir(const char *src_dir, const char *tar_path, int *items)
{
    if (!src_dir || !tar_path || !items) return -1;

    FILE *tar = fopen(tar_path, "wb");
    if (!tar) return -1;

    char root_name[FM_NAME_LEN];
    fm_copy_basename(root_name, sizeof(root_name), src_dir);
    if (!root_name[0]) strncpy(root_name, "folder", sizeof(root_name) - 1);

    *items = 0;
    int rc = fm_tar_add_recursive(tar, src_dir, root_name, 0, items);

    // Dos bloques cero = final del TAR.
    if (rc == 0) {
        if (fm_tar_write_zeros(tar, FM_TAR_BLOCK * 2) != 0) rc = -1;
    }

    if (fclose(tar) != 0) rc = -1;

    if (rc != 0) {
        unlink(tar_path);
    }

    return rc;
}

// Comprime (gzip) o descomprime (gunzip) el fichero seleccionado.
// - Si el nombre acaba en ".gz" -> descomprime a fichero sin ".gz"
// - Si es carpeta -> crea .tar y luego .tar.gz
// - En otro caso -> comprime a fichero + ".gz"
// Usa gzip/gunzip externos via breezybox_exec (apps ELF de consola).
static void fm_gzip_active(void)
{
    int idx = g_fm_active;
    fm_panel_t *p = &g_fm[idx];

    char src[FM_PATH_LEN * 2];
    if (!fm_selected_path(idx, src, sizeof(src))) {
        fm_status("gzip: nada seleccionado");
        return;
    }
    if (p->e[p->selected].is_dir) {
        // Carpeta completa: TAR primero y luego GZIP.
        // Resultado: carpeta.tar.gz en la misma ubicacion.
        char tar_path[FM_PATH_LEN * 2 + 8];
        char gz_path[FM_PATH_LEN * 2 + 12];
        char cmd[FM_PATH_LEN * 5];
        int items = 0;

        snprintf(tar_path, sizeof(tar_path), "%s.tar", src);
        snprintf(gz_path, sizeof(gz_path), "%s.tar.gz", src);

        if (file_exists(tar_path) || file_exists(gz_path)) {
            fm_status("tar.gz: destino existe");
            return;
        }

        fm_status("tar: creando paquete...");
        desktop_present_backbuffer();

        if (fm_create_tar_from_dir(src, tar_path, &items) != 0) {
            fm_status("tar: error creando paquete");
            g_fm_need_scan = 1;
            return;
        }

        snprintf(cmd, sizeof(cmd), "gzip %s %s", tar_path, gz_path);
        breezybox_exec(cmd);

        // Si gzip creo bien el destino, borramos el .tar temporal.
        if (file_exists(gz_path)) {
            unlink(tar_path);
            fm_status("tar.gz creado (%d items)", items);
        } else {
            fm_status("gzip tar: revise .tar");
        }

        g_fm_need_scan = 1;
        return;
    }

    int slen = strlen(src);
    int is_gz = (slen > 3 && strcmp(src + slen - 3, ".gz") == 0);

    char dst[FM_PATH_LEN * 2 + 8];
    char cmd[FM_PATH_LEN * 5];

    if (is_gz) {
        // Descomprimir: quitar ".gz" del destino
        // NOTA: gunzip de BreezyApps NO usa redireccion stdin/stdout, sino
        // argumentos directos: "gunzip <file.gz> [outfile]". Con '<' '>' lo
        // que se capturaba era su mensaje de uso, no los datos (por eso el
        // fichero resultante salia siempre del mismo tamaño y vacio).
        snprintf(dst, sizeof(dst), "%.*s", slen - 3, src);
        if (file_exists(dst)) {
            fm_status("gunzip: ya existe el destino");
            return;
        }
        snprintf(cmd, sizeof(cmd), "gunzip %s %s", src, dst);
        breezybox_exec(cmd);

        // Si el resultado es .tar, extraerlo como carpeta.
        // Caso tipico:
        //   carpeta.tar.gz -> carpeta.tar -> carpeta/
        int dlen = strlen(dst);
        int is_tar = (dlen > 4 && strcmp(dst + dlen - 4, ".tar") == 0);

        if (is_tar && file_exists(dst)) {
            int items = 0;
            fm_status("untar: extrayendo carpeta...");
            desktop_present_backbuffer();

            int ur = fm_extract_tar_to_dir(dst, p->path, &items);
            if (ur == 0) {
                unlink(dst);
                fm_status("carpeta extraida (%d items)", items);
            } else if (ur == -2) {
                fm_status("untar: destino existe");
            } else {
                fm_status("untar: error, queda .tar");
            }
        } else {
            fm_status("descomprimido");
        }
    } else {
        // Comprimir: anadir ".gz"
        // NOTA: gzip de BreezyApps TAMPOCO usa redireccion, usa argumentos
        // directos: "gzip <file> [outfile]", igual que gunzip. Con '<' '>'
        // gzip no recibia argumentos y solo imprimia su mensaje de uso (28
        // caracteres + salto de linea = 29 bytes, que es lo que quedaba
        // "comprimido" siempre, sin importar el fichero real).
        snprintf(dst, sizeof(dst), "%s.gz", src);
        if (file_exists(dst)) {
            fm_status("gzip: ya existe el .gz");
            return;
        }
        snprintf(cmd, sizeof(cmd), "gzip %s %s", src, dst);
        breezybox_exec(cmd);
        fm_status("comprimido a .gz");
    }

    g_fm_need_scan = 1;
}

static int fm_handle_click(int mx, int my)
{
    int pidx = -1;
    int eidx = -1;
    fm_loc_t new_loc = FM_LOC_ROOT;

    if (fm_hit_location_button(mx, my, &pidx, &new_loc)) {
        fm_set_panel_location(pidx, new_loc);
        return 1;
    }

    if (fm_hit_panel(mx, my, &pidx, &eidx)) {
        g_fm_active = pidx;
        if (eidx >= 0) {
            // --- Deteccion de doble clic ---
            // Si este clic cae en la MISMA entrada y panel que el anterior
            // dentro de ~400 ms, lo tratamos como doble clic: abrir.
            static int      last_pidx = -1;
            static int      last_eidx = -1;
            static uint32_t last_tick = 0;

            uint32_t now = xTaskGetTickCount();
            uint32_t dt_ms = (now - last_tick) * portTICK_PERIOD_MS;

            int doble = (pidx == last_pidx && eidx == last_eidx && dt_ms <= 400);

            g_fm[pidx].selected = eidx;
            g_fm_delete_armed[0] = 0;

            if (doble) {
                // resetear para no encadenar triples clics
                last_pidx = -1;
                last_eidx = -1;
                last_tick = 0;

                if (g_fm[pidx].e[eidx].is_dir) {
                    fm_open_active();          // entrar en la carpeta
                } else {
                    fm_open_active();          // si es TXT soportado, abre editor
                }
            } else {
                last_pidx = pidx;
                last_eidx = eidx;
                last_tick = now;
                fm_status("selected %s", g_fm[pidx].e[eidx].name);
            }
        } else {
            fm_status("active panel %s", pidx == 0 ? "LEFT" : "RIGHT");
        }
        return 1;
    }

    fm_btn_t b = fm_hit_button(mx, my);
    switch (b) {
        case FM_BTN_OPEN: fm_open_active(); return 1;
        case FM_BTN_UP:   fm_up_active(); return 1;
        case FM_BTN_COPY: fm_copy_active_to_other(); return 1;
        case FM_BTN_MOVE: fm_move_active_to_other(); return 1;
        case FM_BTN_DEL:  fm_delete_active(); return 1;
        case FM_BTN_PREV: fm_prev_active(); return 1;
        case FM_BTN_NEXT: fm_next_active(); return 1;
        case FM_BTN_GZIP: fm_gzip_active(); return 1;
        default: break;
    }

    return 0;
}


// ===================== APPS LAUNCHER (VIEW_APPS) =====================
// Sustituye al antiguo boton USB Pendrive de la pantalla principal.
// Permite escoger ubicacion (/root/bin, /sdcard o /usb), listar apps y ejecutarlas.

#define APP_MAX_ENTRIES 48
#define APP_NAME_LEN    48
#define APP_PATH_LEN    192
#define APP_VISIBLE     10

typedef enum {
    APP_LOC_ROOTBIN = 0,
    APP_LOC_SDCARD,
    APP_LOC_USB,
} app_loc_t;

typedef struct {
    char name[APP_NAME_LEN];
    char path[APP_PATH_LEN];
    uint32_t size;
} app_entry_t;

static app_entry_t g_apps[APP_MAX_ENTRIES];
static int g_app_count = 0;
static int g_app_selected = -1;
static int g_app_offset = 0;
static int g_app_need_scan = 1;
static app_loc_t g_app_loc = APP_LOC_ROOTBIN;
static char g_app_status[96] = "seleccione ubicacion";

static const char *apps_loc_path(app_loc_t loc)
{
    switch (loc) {
        case APP_LOC_SDCARD:  return "/sdcard";
        case APP_LOC_USB:     return "/usb";
        case APP_LOC_ROOTBIN:
        default:              return "/root/bin";
    }
}

static const char *apps_loc_label(app_loc_t loc)
{
    switch (loc) {
        case APP_LOC_SDCARD:  return "SD";
        case APP_LOC_USB:     return "USB";
        case APP_LOC_ROOTBIN:
        default:              return "ROOT/BIN";
    }
}

static void apps_status(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_app_status, sizeof(g_app_status), fmt, ap);
    va_end(ap);
}

static int apps_is_blocked_ext(const char *name)
{
    // Evitamos listar formatos que sabemos que no son ejecutables en este contexto.
    return te_endswith_ci(name, ".rv32") ||  // otra arquitectura
           te_endswith_ci(name, ".txt")  ||
           te_endswith_ci(name, ".log")  ||
           te_endswith_ci(name, ".cfg")  ||
           te_endswith_ci(name, ".ini")  ||
           te_endswith_ci(name, ".json") ||
           te_endswith_ci(name, ".md")   ||
           te_endswith_ci(name, ".c")    ||
           te_endswith_ci(name, ".h")    ||
           te_endswith_ci(name, ".gz")   ||
           te_endswith_ci(name, ".zip");
}

static int apps_is_candidate_file(const char *name, const struct stat *st)
{
    if (!name || !name[0] || !st) return 0;
    if (name[0] == '.') return 0;
    if (S_ISDIR(st->st_mode)) return 0;
    if (apps_is_blocked_ext(name)) return 0;
    return 1;
}

static void apps_sort(void)
{
    for (int i = 0; i < g_app_count; i++) {
        for (int j = i + 1; j < g_app_count; j++) {
            if (strcmp(g_apps[j].name, g_apps[i].name) < 0) {
                app_entry_t tmp = g_apps[i];
                g_apps[i] = g_apps[j];
                g_apps[j] = tmp;
            }
        }
    }
}

static void apps_scan(void)
{
    const char *base_path = apps_loc_path(g_app_loc);
    DIR *d = opendir(base_path);

    g_app_count = 0;
    g_app_selected = -1;
    g_app_offset = 0;

    if (!d) {
        apps_status("%s no disponible", base_path);
        g_app_need_scan = 0;
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL && g_app_count < APP_MAX_ENTRIES) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;

        char full[APP_PATH_LEN];
        struct stat st;
        path_join(full, sizeof(full), base_path, de->d_name);

        if (stat(full, &st) != 0) continue;
        if (!apps_is_candidate_file(de->d_name, &st)) continue;

        app_entry_t *e = &g_apps[g_app_count++];
        memset(e, 0, sizeof(*e));
        strncpy(e->name, de->d_name, sizeof(e->name) - 1);
        strncpy(e->path, full, sizeof(e->path) - 1);
        e->size = (uint32_t)st.st_size;
    }
    closedir(d);

    apps_sort();

    if (g_app_count > 0) {
        g_app_selected = 0;
        apps_status("%s: %d apps", apps_loc_label(g_app_loc), g_app_count);
    } else {
        apps_status("%s: sin apps", apps_loc_label(g_app_loc));
    }

    g_app_need_scan = 0;
}

static int apps_hit_root(int mx, int my) { return inside(mx, my,  82, 56, 62, 16); }
static int apps_hit_sd  (int mx, int my) { return inside(mx, my, 148, 56, 36, 16); }
static int apps_hit_usb (int mx, int my) { return inside(mx, my, 188, 56, 42, 16); }
static int apps_hit_run (int mx, int my) { return inside(mx, my, 236, 56, 48, 16); }

static int apps_hit_entry(int mx, int my)
{
    const int x = 24;
    const int y = 84;
    const int w = 352;
    const int row_h = 12;

    if (!inside(mx, my, x, y, w, APP_VISIBLE * row_h)) return -1;

    int row = (my - y) / row_h;
    int idx = g_app_offset + row;
    if (idx < 0 || idx >= g_app_count) return -1;
    return idx;
}

static void apps_set_location(app_loc_t loc)
{
    if (g_app_loc != loc) {
        g_app_loc = loc;
        g_app_need_scan = 1;
        apps_status("ubicacion: %s", apps_loc_label(g_app_loc));
    }
}

static void apps_launch_selected(void)
{
    if (g_app_selected < 0 || g_app_selected >= g_app_count) {
        apps_status("no hay app seleccionada");
        return;
    }

    char cmd[APP_PATH_LEN + 8];
    snprintf(cmd, sizeof(cmd), "%s", g_apps[g_app_selected].path);

    apps_status("ejecutando %s", g_apps[g_app_selected].name);

    // Salimos temporalmente a texto para lanzar apps de consola o ELF externos.
    // Si la app es grafica (ej. Celeste), ella misma cambia a SM_150P.
    vterm_input_flush(vterm_get_active());
    rgb_display_set_mode(SM_TEXT);
    vTaskDelay(pdMS_TO_TICKS(80));

    printf("[APPS] exec: %s\n", cmd);
    breezybox_exec(cmd);

    // Volver al escritorio grafico al terminar la app.
    vterm_input_flush(vterm_get_active());
    rgb_display_set_mode(SM_400X240);
    desktop_palette();
    desk_gfx_clear(C_BG0);
    desktop_present_backbuffer();

    apps_status("volvio de %s", g_apps[g_app_selected].name);
}

static void apps_prev(void)
{
    if (g_app_offset > 0) {
        g_app_offset -= APP_VISIBLE;
        if (g_app_offset < 0) g_app_offset = 0;
        g_app_selected = g_app_offset;
    }
}

static void apps_next(void)
{
    if (g_app_offset + APP_VISIBLE < g_app_count) {
        g_app_offset += APP_VISIBLE;
        if (g_app_offset >= g_app_count) g_app_offset = g_app_count - 1;
        g_app_selected = g_app_offset;
    }
}

static int apps_handle_click(int mx, int my)
{
    if (apps_hit_root(mx, my)) { apps_set_location(APP_LOC_ROOTBIN); return 1; }
    if (apps_hit_sd(mx, my))   { apps_set_location(APP_LOC_SDCARD);  return 1; }
    if (apps_hit_usb(mx, my))  { apps_set_location(APP_LOC_USB);     return 1; }
    if (apps_hit_run(mx, my))  { apps_launch_selected(); return 1; }

    int idx = apps_hit_entry(mx, my);
    if (idx >= 0) {
        // En APPS, un toque/click sobre la app la ejecuta directamente.
        g_app_selected = idx;
        apps_launch_selected();
        return 1;
    }

    return 0;
}

static void draw_apps_page(int mx, int my)
{
    if (g_app_need_scan) {
        apps_scan();
    }

    draw_window_base(mx, my, "APPS - launcher", 1);

    draw_button( 82, 56, 62, 16, "ROOT", apps_hit_root(mx, my));
    draw_button(148, 56, 36, 16, "SD",   apps_hit_sd(mx, my));
    draw_button(188, 56, 42, 16, "USB",  apps_hit_usb(mx, my));
    draw_button(236, 56, 48, 16, "RUN",  apps_hit_run(mx, my));

    draw_text5(292, 61, apps_loc_label(g_app_loc), C_CYAN);
    draw_text5(24, 76, g_app_status, C_MUTED);

    panel(20, 82, 360, 128, C_PANEL2);

    if (g_app_count <= 0) {
        draw_text5(34, 104, "No hay apps aqui", C_YELLOW);
        draw_text5(34, 118, "Pruebe ROOT, SD o USB", C_MUTED);
        draw_text5(34, 132, "eget guarda en /root/bin", C_MUTED);
    } else {
        for (int r = 0; r < APP_VISIBLE; r++) {
            int idx = g_app_offset + r;
            if (idx >= g_app_count) break;

            int y = 90 + r * 12;
            int hover = inside(mx, my, 26, y - 1, 348, 11);
            int sel = (idx == g_app_selected);

            if (sel) {
                desk_gfx_rectfill(26, y - 1, 348, 11, C_BLUE);
            } else if (hover) {
                desk_gfx_rectfill(26, y - 1, 348, 11, C_TOP);
            }

            char nm[34];
            desk_trunc_copy(nm, sizeof(nm), g_apps[idx].name, 28);

            char line[64];
            snprintf(line, sizeof(line), "%02d  %s", idx + 1, nm);
            draw_text5(32, y + 1, line, sel ? C_WHITE : C_TEXT);

            char sz[20];
            if (g_apps[idx].size > 1024 * 1024) {
                snprintf(sz, sizeof(sz), "%uM", (unsigned)(g_apps[idx].size / (1024 * 1024)));
            } else {
                snprintf(sz, sizeof(sz), "%uK", (unsigned)((g_apps[idx].size + 1023) / 1024));
            }
            draw_text5(330, y + 1, sz, C_MUTED);
        }
    }

    draw_text5(24, 214, "Touch/click ejecuta  |  Enter=RUN  |  Tab cambia ubicacion", C_MUTED);
}


// ===================== EDITOR WIFI (VIEW_WIFI) =====================
static char g_wifi_edit_ssid[33] = "";
static char g_wifi_edit_pass[65] = "";
static int  g_wifi_edit_field = 0;   // 0=SSID, 1=password
static int  g_wifi_edit_init = 0;    // cargado el SSID actual ya?
static char g_wifi_edit_msg[48] = "";
static int  g_wifi_connecting = 0;   // mostrando "conectando..."

static void draw_wifi_page(int mx, int my)
{
    draw_window_base(mx, my, "WiFi network", 0);

    int ok = minipc_wifi_is_connected() ? 1 : 0;
    const char *ip = minipc_wifi_get_ip();

    // Estado actual
    draw_led(30, 64, ok, "WiFi link");
    draw_text5(150, 64, "IP:", C_CYAN);
    draw_text5(178, 64, (ip && ip[0]) ? ip : "0.0.0.0", ok ? C_GREEN : C_RED);
    draw_text5(30, 80, "Red actual:", C_MUTED);
    draw_text5(110, 80, minipc_wifi_get_ssid(), ok ? C_GREEN : C_YELLOW);

    // --- Campo SSID ---
    int ssid_active = (g_wifi_edit_field == 0);
    draw_text5(30, 102, ssid_active ? ">SSID:" : " SSID:", ssid_active ? C_GREEN : C_CYAN);
    desk_gfx_rectfill(80, 99, 250, 12, ssid_active ? C_PANEL2 : C_PANEL);
    desk_gfx_rect(80, 99, 250, 12, ssid_active ? C_GREEN : C_MUTED);
    draw_text5(84, 101, g_wifi_edit_ssid, C_TEXT);
    if (ssid_active) {
        int cx = 84 + (int)strlen(g_wifi_edit_ssid) * 6;
        if (cx < 326) desk_gfx_rectfill(cx, 101, 5, 7, C_GREEN);
    }

    // --- Campo password ---
    // Se muestra EN CLARO mientras editas (para teclearla sin errores) y se
    // OCULTA con asteriscos una vez la conexion esta guardada OK. (Idea Arielo)
    int pass_active = (g_wifi_edit_field == 1);
    int hide_pass = (strstr(g_wifi_edit_msg, "OK") != NULL);  // conectado y guardado
    draw_text5(30, 120, pass_active ? ">Pass:" : " Pass:", pass_active ? C_GREEN : C_CYAN);
    desk_gfx_rectfill(80, 117, 250, 12, pass_active ? C_PANEL2 : C_PANEL);
    desk_gfx_rect(80, 117, 250, 12, pass_active ? C_GREEN : C_MUTED);
    int plen = strlen(g_wifi_edit_pass);
    char pshow[70];
    if (hide_pass) {
        int k;
        for (k = 0; k < plen && k < 64; k++) pshow[k] = '*';
        pshow[k] = '\0';
    } else {
        snprintf(pshow, sizeof(pshow), "%s", g_wifi_edit_pass);
    }
    draw_text5(84, 119, pshow, C_TEXT);
    char pcount[16];
    snprintf(pcount, sizeof(pcount), "(%d)", plen);
    draw_text5(300, 119, pcount, C_MUTED);
    if (pass_active) {
        int cx = 84 + plen * 6;
        if (cx < 296) desk_gfx_rectfill(cx, 119, 5, 7, C_GREEN);
    }
    if (pass_active) {
        int cx = 84 + plen * 6;
        if (cx < 296) desk_gfx_rectfill(cx, 119, 5, 7, C_GREEN);
    }

    // Boton CONECTAR
    int bhover = inside(mx, my, 30, 140, 90, 16);
    desk_gfx_rectfill(30, 140, 90, 16, bhover ? C_BLUE : C_PANEL2);
    desk_gfx_rect(30, 140, 90, 16, C_WHITE);
    draw_text5_center(30, 144, 90, "CONECTAR", C_TEXT);

    // Ayuda + mensaje
    draw_text5(130, 144, "TAB cambia campo", C_MUTED);
    if (g_wifi_connecting) {
        draw_text5(30, 165, "Conectando, espera...", C_YELLOW);
    } else if (g_wifi_edit_msg[0]) {
        uint8_t mc = (strstr(g_wifi_edit_msg, "OK") || strstr(g_wifi_edit_msg, "Conectado"))
                     ? C_GREEN : C_RED;
        draw_text5(30, 165, g_wifi_edit_msg, mc);
    }
}

static void draw_system_page(int mx, int my)
{
    draw_window_base(mx, my, "System info", 0);

    char line[64];
    uint32_t heap = esp_get_free_heap_size();
    uint32_t heap_min = esp_get_minimum_free_heap_size();
    size_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    draw_text5(30, 70, "Arielo MiniPC OS", C_TEXT);
    draw_text5(30, 86, "Waveshare ESP32-S3 7 inch", C_MUTED);
    draw_text5(30, 102, "RGB 800x480 / SM_400X240 x2", C_MUTED);
    draw_text5(30, 118, "VTerm 100x29 + topbar", C_MUTED);

    snprintf(line, sizeof(line), "Heap free: %lu", (unsigned long)heap);
    draw_text5(30, 142, line, C_CYAN);

    snprintf(line, sizeof(line), "Heap min : %lu", (unsigned long)heap_min);
    draw_text5(30, 156, line, C_CYAN);

    snprintf(line, sizeof(line), "PSRAM free: %lu", (unsigned long)psram);
    draw_text5(30, 170, line, C_CYAN);

    snprintf(line, sizeof(line), "WiFi:%s  SD:%s  USB:%s",
             minipc_wifi_is_connected() ? "ON" : "OFF",
             minipc_sd_is_mounted() ? "ON" : "OFF",
             minipc_usb_msc_mounted() ? "ON" : "OFF");
    draw_text5(30, 194, line, C_GREEN);
}

static void draw_about_page(int mx, int my)
{
    draw_window_base(mx, my, "About", 0);

    draw_text5(34, 74, "Arielo MiniPC OS", C_YELLOW);
    draw_text5(34, 94, "ARIELO_MINIPC_OS_10BO_OK", C_WHITE);
    draw_text5(34, 114, "Splash, VTerm, WiFi, SD, USB MSC", C_MUTED);
    draw_text5(34, 130, "Mouse pointer and double buffer", C_MUTED);

    panel(34, 154, 332, 42, C_PANEL2);
    draw_text5(46, 168, "Sistema Operativo ligero para ESP32-S3", C_GREEN);
    draw_text5(46, 184, "WAVESHARE LCD 7 TOUCH", C_CYAN);
}

static void draw_desktop(int mx, int my)
{
    draw_background();
    draw_topbar(mx, my);
    draw_status_panel();

    for (int i = 0; i < CARD_COUNT; i++) {
        draw_icon_card(&g_cards[i], inside(mx, my, g_cards[i].x, g_cards[i].y, g_cards[i].w, g_cards[i].h));
    }

    draw_footer();
}

// ===================== MINI NAVEGADOR (VIEW_BROWSER) =====================
static char g_url[160] = "http://";
static int  g_url_len = 0;          // longitud actual ("http://")
static int  g_browser_scroll = 0;   // primera linea visible
static int  g_browser_inited = 0;

#define BROWSER_VISIBLE_LINES 17    // lineas visibles; 09D reserva espacio para botones nav
#define BROWSER_CHARS_PER_LINE 64   // recorte horizontal

#define BROWSER_NAV_Y 31
#define BROWSER_NAV_H 12
#define BROWSER_BTN_EXIT_X 4
#define BROWSER_BTN_EXIT_W 42
#define BROWSER_BTN_BACK_X 52
#define BROWSER_BTN_BACK_W 22
#define BROWSER_BTN_FWD_X 78
#define BROWSER_BTN_FWD_W 22
#define BROWSER_BTN_HOME_X 106
#define BROWSER_BTN_HOME_W 44
#define BROWSER_BTN_RELOAD_X 156
#define BROWSER_BTN_RELOAD_W 42
#define BROWSER_BTN_HIST_X 202
#define BROWSER_BTN_HIST_W 38
#define BROWSER_BTN_FAV_X 244
#define BROWSER_BTN_FAV_W 34
#define BROWSER_BTN_FAVADD_X 282
#define BROWSER_BTN_FAVADD_W 40
#define BROWSER_URL_Y 47
#define BROWSER_STATUS_Y 63
#define BROWSER_CONTENT_Y 80

// Listado emergente de HIST/FAV sobre el area de contenido.
// 0 = ninguno, 1 = historial, 2 = favoritos.
static int s_browser_overlay = 0;
static int s_browser_overlay_scroll = 0;


static void draw_browser_nav_button(int x, int w, const char *txt, int hover, int enabled)
{
    uint8_t fill = enabled ? (hover ? C_BLUE : C_PANEL2) : C_PANEL;
    uint8_t txtc = enabled ? C_TEXT : C_MUTED;

    desk_gfx_rectfill(x, BROWSER_NAV_Y, w, BROWSER_NAV_H, fill);
    desk_gfx_rect(x, BROWSER_NAV_Y, w, BROWSER_NAV_H, enabled ? C_WHITE : C_BORDER);
    draw_text5_center(x, BROWSER_NAV_Y + 3, w, txt, txtc);
}

static void browser_sync_url_from_engine(void)
{
    const char *u = minipc_browser_current_url();
    if (!u || !u[0]) return;

    int n = strlen(u);
    if (n >= (int)sizeof(g_url)) n = (int)sizeof(g_url) - 1;

    memcpy(g_url, u, n);
    g_url[n] = '\0';
    g_url_len = n;
}

static void browser_copy_home_to_url(void)
{
    const char *u = minipc_browser_home_url();
    if (!u || !u[0]) u = "http://example.com/";

    int n = strlen(u);
    if (n >= (int)sizeof(g_url)) n = (int)sizeof(g_url) - 1;

    memcpy(g_url, u, n);
    g_url[n] = '\0';
    g_url_len = n;
}


// 09E FIX1: prototipos necesarios porque draw_browser_page()
// usa estas funciones antes de su definicion real.
static int browser_find_link_marker(const char *line);
static int browser_line_has_clickable_link(const char *line);
static int browser_line_is_form_action(const char *line);
static void browser_prepare_form_query(void);
static int browser_try_open_form_line(int line_index);
static void browser_open_link_index(int link_idx, int mx, int my);
static int browser_try_open_link_line(int line_index, int mx, int my);
static void draw_browser_overlay_list(int y0);

static void draw_browser_page(int mx, int my)
{
    // 09E FIX2:
    // El navegador no llamaba a draw_topbar(), por eso el reloj quedaba
    // congelado en el minuto de entrada. El bucle principal ya marca dirty
    // cuando cambia el minuto; aquí redibujamos la barra superior para que
    // la hora avance sin recargar la pagina web.
    draw_topbar(mx, my);

    // Fondo del navegador, sin tocar la topbar.
    desk_gfx_rectfill(0, 28, DESK_W, DESK_H - 28, C_BG0);

    // Fila de navegacion
    draw_browser_nav_button(BROWSER_BTN_EXIT_X,   BROWSER_BTN_EXIT_W,   "EXIT",
                            inside(mx, my, BROWSER_BTN_EXIT_X, BROWSER_NAV_Y, BROWSER_BTN_EXIT_W, BROWSER_NAV_H), 1);
    draw_browser_nav_button(BROWSER_BTN_BACK_X,   BROWSER_BTN_BACK_W,   "<",
                            inside(mx, my, BROWSER_BTN_BACK_X, BROWSER_NAV_Y, BROWSER_BTN_BACK_W, BROWSER_NAV_H),
                            minipc_browser_can_back());
    draw_browser_nav_button(BROWSER_BTN_FWD_X,    BROWSER_BTN_FWD_W,    ">",
                            inside(mx, my, BROWSER_BTN_FWD_X, BROWSER_NAV_Y, BROWSER_BTN_FWD_W, BROWSER_NAV_H),
                            minipc_browser_can_forward());
    draw_browser_nav_button(BROWSER_BTN_HOME_X,   BROWSER_BTN_HOME_W,   "HOME",
                            inside(mx, my, BROWSER_BTN_HOME_X, BROWSER_NAV_Y, BROWSER_BTN_HOME_W, BROWSER_NAV_H), 1);
    draw_browser_nav_button(BROWSER_BTN_RELOAD_X, BROWSER_BTN_RELOAD_W, "RLD",
                            inside(mx, my, BROWSER_BTN_RELOAD_X, BROWSER_NAV_Y, BROWSER_BTN_RELOAD_W, BROWSER_NAV_H), 1);
    draw_browser_nav_button(BROWSER_BTN_HIST_X, BROWSER_BTN_HIST_W, "HIST",
                            inside(mx, my, BROWSER_BTN_HIST_X, BROWSER_NAV_Y, BROWSER_BTN_HIST_W, BROWSER_NAV_H), 1);
    draw_browser_nav_button(BROWSER_BTN_FAV_X, BROWSER_BTN_FAV_W, "FAV",
                            inside(mx, my, BROWSER_BTN_FAV_X, BROWSER_NAV_Y, BROWSER_BTN_FAV_W, BROWSER_NAV_H), 1);
    draw_browser_nav_button(BROWSER_BTN_FAVADD_X, BROWSER_BTN_FAVADD_W,
                            minipc_browser_fav_is_current() ? "-FAV" : "+FAV",
                            inside(mx, my, BROWSER_BTN_FAVADD_X, BROWSER_NAV_Y, BROWSER_BTN_FAVADD_W, BROWSER_NAV_H), 1);

    draw_text5(328, 35, "N.WEB:10AN", C_CYAN);

    // Barra de URL
    desk_gfx_rectfill(4, BROWSER_URL_Y, DESK_W - 8, 14, C_PANEL2);
    draw_text5(8, BROWSER_URL_Y + 3, "URL:", C_MUTED);

    // mostramos la URL, recortada por la izquierda si es larga
    char shown[60];
    int off = (g_url_len > 56) ? (g_url_len - 56) : 0;
    strncpy(shown, g_url + off, sizeof(shown) - 1);
    shown[sizeof(shown) - 1] = '\0';
    draw_text5(34, BROWSER_URL_Y + 3, shown, C_TEXT);

    // cursor simple al final de la URL escrita
    int cur_x = 34 + (int)strlen(shown) * 6;
    if (cur_x < DESK_W - 10) desk_gfx_rectfill(cur_x, BROWSER_URL_Y + 3, 5, 7, C_GREEN);

    // Barra de estado
    desk_gfx_rectfill(4, BROWSER_STATUS_Y, DESK_W - 8, 12, C_PANEL);
    const char *st = minipc_browser_status_text();
    uint8_t st_color = C_MUTED;
    if (minipc_browser_state() == BROWSER_ERROR) st_color = C_RED;
    else if (minipc_browser_state() == BROWSER_DONE) st_color = C_GREEN;
    draw_text5(8, BROWSER_STATUS_Y + 2, st, st_color);

    // Area de contenido: si HIST o FAV estan abiertos, mostramos el listado
    // en vez de la pagina cargada.
    int y0 = BROWSER_CONTENT_Y;
    if (s_browser_overlay != 0) {
        draw_browser_overlay_list(y0);
    } else if (minipc_browser_state() == BROWSER_DONE) {
        int total = minipc_browser_line_count();
        for (int i = 0; i < BROWSER_VISIBLE_LINES; i++) {
            int ln = g_browser_scroll + i;
            if (ln >= total) break;

            int llen = 0;
            const char *line = minipc_browser_get_line(ln, &llen);
            char buf[BROWSER_CHARS_PER_LINE + 1];
            int cpy = (llen > BROWSER_CHARS_PER_LINE) ? BROWSER_CHARS_PER_LINE : llen;
            memcpy(buf, line, cpy);
            buf[cpy] = '\0';
            uint8_t line_color =
                (strncmp(line, "[FORM]", 6) == 0 || browser_line_has_clickable_link(line))
                ? C_CYAN : C_TEXT;
            draw_text5(8, y0 + i * 8, buf, line_color);
        }

        // indicador de scroll
        char nav[64];
        if (minipc_browser_form_available()) {
            snprintf(nav, sizeof(nav), "Lin %d-%d/%d  Links:%d  FORM: toca [FORM] o Ctrl+F",
                     g_browser_scroll + 1,
                     (g_browser_scroll + BROWSER_VISIBLE_LINES > total) ? total : g_browser_scroll + BROWSER_VISIBLE_LINES,
                     total,
                     minipc_browser_link_count());
        } else {
            snprintf(nav, sizeof(nav), "Lin %d-%d/%d  Links:%d  Tap [n]",
                     g_browser_scroll + 1,
                     (g_browser_scroll + BROWSER_VISIBLE_LINES > total) ? total : g_browser_scroll + BROWSER_VISIBLE_LINES,
                     total,
                     minipc_browser_link_count());
        }
        draw_text5(8, DESK_H - 9, nav, C_MUTED);
    } else if (minipc_browser_state() == BROWSER_LOADING ||
               minipc_browser_state() == BROWSER_RENDERING) {
        draw_text5(8, y0, "Cargando, espera...", C_YELLOW);
    } else {
        draw_text5(8, y0, "Escribe la URL y pulsa Enter.", C_MUTED);
        draw_text5(8, y0 + 10, "Tambien puedes usar HOME, RLD, < y >.", C_MUTED);
        draw_text5(8, y0 + 20, "Ej: neverssl.com  o  example.com", C_MUTED);
    }
}

// Listado de HISTORIAL o FAVORITOS, dibujado encima del area de contenido.
// El historial resalta en verde la pagina actual; los favoritos no tienen
// posicion "actual" (es una lista plana guardada por el usuario).
#define BROWSER_OVERLAY_ROW_H  9
#define BROWSER_OVERLAY_VISIBLE 15

static void draw_browser_overlay_list(int y0)
{
    int is_hist = (s_browser_overlay == 1);
    int count = is_hist ? minipc_browser_history_count() : minipc_browser_fav_count();
    int cur = is_hist ? minipc_browser_history_index() : -1;

    desk_gfx_rectfill(4, y0, DESK_W - 8, DESK_H - y0 - 10, C_PANEL2);
    draw_text5(8, y0 + 2, is_hist ? "HISTORIAL" : "FAVORITOS", C_YELLOW);

    if (count == 0) {
        draw_text5(8, y0 + 18, is_hist ? "Historial vacio." : "Sin favoritos guardados.", C_MUTED);
        draw_text5(8, DESK_H - 9, "ESC cierra", C_MUTED);
        return;
    }

    for (int i = 0; i < BROWSER_OVERLAY_VISIBLE; i++) {
        int idx = s_browser_overlay_scroll + i;
        if (idx >= count) break;

        const char *u = is_hist ? minipc_browser_history_url(idx) : minipc_browser_fav_url(idx);
        char line[80];
        snprintf(line, sizeof(line), "[%d] %s", idx + 1, u);
        uint8_t color = (is_hist && idx == cur) ? C_GREEN : C_TEXT;
        draw_text5(8, y0 + 14 + i * BROWSER_OVERLAY_ROW_H, line, color);
    }

    draw_text5(8, DESK_H - 9, "Toca para ir - ESC cierra", C_MUTED);
}

static void browser_show_loading_and_present(int mx, int my)
{
    draw_browser_page(mx, my);
    desktop_present_backbuffer();
}

// 10AN: indicar visualmente que hay una carga bloqueante en curso.
// El cambio de forma lo aplica el driver RGB en el overlay del puntero,
// por eso sigue visible aunque minipc_browser_load() este bloqueando.
static void browser_busy_cursor(int busy)
{
    rgb_display_set_mouse_busy(busy);
}

static void browser_load_typed_url(int mx, int my)
{
    g_url[g_url_len] = '\0';
    g_browser_scroll = 0;

    minipc_browser_set_loading(g_url);
    browser_busy_cursor(1);
    browser_show_loading_and_present(mx, my);

    minipc_browser_load(g_url);   // bloqueante
    browser_busy_cursor(0);
    browser_sync_url_from_engine();
}

static void browser_reload_now(int mx, int my)
{
    g_browser_scroll = 0;

    minipc_browser_set_loading(minipc_browser_current_url());
    browser_busy_cursor(1);
    browser_show_loading_and_present(mx, my);

    minipc_browser_reload();      // bloqueante
    browser_busy_cursor(0);
    browser_sync_url_from_engine();
}

static void browser_home_now(int mx, int my)
{
    g_browser_scroll = 0;
    browser_copy_home_to_url();

    minipc_browser_set_loading(g_url);
    browser_busy_cursor(1);
    browser_show_loading_and_present(mx, my);

    minipc_browser_home();        // bloqueante
    browser_busy_cursor(0);
    browser_sync_url_from_engine();
}

static void browser_back_now(int mx, int my)
{
    if (!minipc_browser_can_back()) return;

    g_browser_scroll = 0;
    browser_busy_cursor(1);
    browser_show_loading_and_present(mx, my);

    minipc_browser_back();        // bloqueante
    browser_busy_cursor(0);
    browser_sync_url_from_engine();
}

static void browser_forward_now(int mx, int my)
{
    if (!minipc_browser_can_forward()) return;

    g_browser_scroll = 0;
    browser_busy_cursor(1);
    browser_show_loading_and_present(mx, my);

    minipc_browser_forward();     // bloqueante
    browser_busy_cursor(0);
    browser_sync_url_from_engine();
}


static int browser_find_link_marker(const char *line)
{
    if (!line) return -1;

    const char *p = line;
    while (*p) {
        if (*p == '[') {
            const char *q = p + 1;
            int n = 0;
            int digits = 0;

            while (*q >= '0' && *q <= '9') {
                n = n * 10 + (*q - '0');
                q++;
                digits++;
                if (digits > 3) break;
            }

            if (digits > 0 && *q == ']') {
                int idx = n - 1;
                if (idx >= 0 && idx < minipc_browser_link_count()) {
                    return idx;
                }
            }
        }
        p++;
    }

    return -1;
}

static int browser_line_has_clickable_link(const char *line)
{
    return browser_find_link_marker(line) >= 0;
}

static int browser_line_is_form_action(const char *line)
{
    return line && strncmp(line, "[FORM]", 6) == 0;
}

static void browser_prepare_form_query(void)
{
    // 10D FIX2:
    // Preparar la barra para escribir una busqueda/formulario.
    // El motor browser_prepare_url() convertira "?texto" en action?name=texto,
    // o en busqueda Google si no hay formulario disponible.
    g_url[0] = '?';
    g_url[1] = '\0';
    g_url_len = 1;
}

static int browser_try_open_form_line(int line_index)
{
    int llen = 0;
    const char *line = minipc_browser_get_line(line_index, &llen);
    if (!line || llen <= 0) return 0;

    if (!browser_line_is_form_action(line)) return 0;

    browser_prepare_form_query();
    return 1;
}

// 10D FIX2:
// Las lineas [FORM] no son enlaces numerados, pero ahora son accionables:
// tocar/clicar [FORM] prepara la barra con "?".
// Ctrl+F hace lo mismo.

static void browser_open_link_index(int link_idx, int mx, int my)
{
    if (link_idx < 0 || link_idx >= minipc_browser_link_count()) return;

    const char *u = minipc_browser_get_link_url(link_idx);
    if (!u || !u[0]) return;

    int n = strlen(u);
    if (n >= (int)sizeof(g_url)) n = (int)sizeof(g_url) - 1;

    memcpy(g_url, u, n);
    g_url[n] = '\0';
    g_url_len = n;

    g_browser_scroll = 0;

    minipc_browser_set_loading(g_url);
    browser_busy_cursor(1);
    browser_show_loading_and_present(mx, my);

    minipc_browser_load(g_url);   // bloqueante
    browser_busy_cursor(0);
    browser_sync_url_from_engine();
}

static int browser_try_open_link_line(int line_index, int mx, int my)
{
    int llen = 0;
    const char *line = minipc_browser_get_line(line_index, &llen);
    if (!line || llen <= 0) return 0;

    int idx = browser_find_link_marker(line);
    if (idx < 0) return 0;

    browser_open_link_index(idx, mx, my);
    return 1;
}


static int browser_handle_click(int mx, int my, desktop_view_t *view)
{
    if (inside(mx, my, BROWSER_BTN_EXIT_X, BROWSER_NAV_Y, BROWSER_BTN_EXIT_W, BROWSER_NAV_H)) {
        if (view) *view = VIEW_DESKTOP;
        return 1;
    }

    if (inside(mx, my, BROWSER_BTN_BACK_X, BROWSER_NAV_Y, BROWSER_BTN_BACK_W, BROWSER_NAV_H)) {
        browser_back_now(mx, my);
        return 1;
    }

    if (inside(mx, my, BROWSER_BTN_FWD_X, BROWSER_NAV_Y, BROWSER_BTN_FWD_W, BROWSER_NAV_H)) {
        browser_forward_now(mx, my);
        return 1;
    }

    if (inside(mx, my, BROWSER_BTN_HOME_X, BROWSER_NAV_Y, BROWSER_BTN_HOME_W, BROWSER_NAV_H)) {
        browser_home_now(mx, my);
        return 1;
    }

    if (inside(mx, my, BROWSER_BTN_RELOAD_X, BROWSER_NAV_Y, BROWSER_BTN_RELOAD_W, BROWSER_NAV_H)) {
        browser_reload_now(mx, my);
        return 1;
    }

    if (inside(mx, my, BROWSER_BTN_HIST_X, BROWSER_NAV_Y, BROWSER_BTN_HIST_W, BROWSER_NAV_H)) {
        // Alternar: si ya estaba mostrando el historial, cerrar; si no, abrirlo.
        s_browser_overlay = (s_browser_overlay == 1) ? 0 : 1;
        s_browser_overlay_scroll = 0;
        return 1;
    }

    if (inside(mx, my, BROWSER_BTN_FAV_X, BROWSER_NAV_Y, BROWSER_BTN_FAV_W, BROWSER_NAV_H)) {
        s_browser_overlay = (s_browser_overlay == 2) ? 0 : 2;
        s_browser_overlay_scroll = 0;
        return 1;
    }

    if (inside(mx, my, BROWSER_BTN_FAVADD_X, BROWSER_NAV_Y, BROWSER_BTN_FAVADD_W, BROWSER_NAV_H)) {
        const char *cur = minipc_browser_current_url();
        if (minipc_browser_fav_is_current()) {
            // Ya estaba en favoritos: buscar su indice y quitarlo.
            int n = minipc_browser_fav_count();
            for (int i = 0; i < n; i++) {
                if (strcmp(minipc_browser_fav_url(i), cur) == 0) {
                    minipc_browser_fav_remove(i);
                    break;
                }
            }
        } else {
            minipc_browser_fav_add(cur);
        }
        return 1;
    }

    // Listado HIST/FAV abierto: tocar una linea navega a esa URL y cierra.
    if (s_browser_overlay != 0 && my >= BROWSER_CONTENT_Y) {
        int is_hist = (s_browser_overlay == 1);
        int count = is_hist ? minipc_browser_history_count() : minipc_browser_fav_count();
        int row = (my - (BROWSER_CONTENT_Y + 14)) / BROWSER_OVERLAY_ROW_H;
        int idx = s_browser_overlay_scroll + row;
        if (row >= 0 && idx >= 0 && idx < count) {
            s_browser_overlay = 0;
            g_browser_scroll = 0;
            browser_busy_cursor(1);
            browser_show_loading_and_present(mx, my);
            if (is_hist) {
                minipc_browser_history_goto(idx);
            } else {
                const char *fu = minipc_browser_fav_url(idx);
                if (fu && fu[0]) minipc_browser_set_loading(fu);
                minipc_browser_load(fu);
            }
            browser_busy_cursor(0);
            browser_sync_url_from_engine();
        }
        return 1;
    }

    if (minipc_browser_state() == BROWSER_DONE && my >= BROWSER_CONTENT_Y) {
        // 09E: si se toca una linea con marcador [n], abrir ese enlace.
        int row = (my - BROWSER_CONTENT_Y) / 8;
        if (row >= 0 && row < BROWSER_VISIBLE_LINES) {
            int line_index = g_browser_scroll + row;
            if (browser_try_open_form_line(line_index)) {
                return 1;
            }
            if (browser_try_open_link_line(line_index, mx, my)) {
                return 1;
            }
        }

        // Si no era enlace, mantener el scroll por touch:
        // tocar mitad inferior baja, superior sube.
        int total = minipc_browser_line_count();

        if (my > (BROWSER_CONTENT_Y + DESK_H) / 2) {
            g_browser_scroll += BROWSER_VISIBLE_LINES / 2;
            if (g_browser_scroll + BROWSER_VISIBLE_LINES > total)
                g_browser_scroll = (total > BROWSER_VISIBLE_LINES) ? total - BROWSER_VISIBLE_LINES : 0;
        } else {
            g_browser_scroll -= BROWSER_VISIBLE_LINES / 2;
            if (g_browser_scroll < 0) g_browser_scroll = 0;
        }
        return 1;
    }

    return 0;
}


// ===================== 10AH: BOTON WEB -> TactileBrowser Lexbor GUI interno =====================
#define TBB10AH_HOME_FILE "/root/tbbrowser_home.html"

static void desktop_10ah_write_home_if_missing(void)
{
    struct stat st;
    if (stat(TBB10AH_HOME_FILE, &st) == 0 && st.st_size > 0) {
        return;
    }

    FILE *f = fopen(TBB10AH_HOME_FILE, "w");
    if (!f) {
        printf("[DESKTOP10AH][WARN] no pude crear %s\n", TBB10AH_HOME_FILE);
        return;
    }

    fputs("<!doctype html>\n", f);
    fputs("<html><head><title>Arielo MiniPC OS - Inicio</title></head><body>\n", f);
    fputs("<h1>Arielo MiniPC OS</h1>\n", f);
    fputs("<h2>TactileBrowser Lexbor GUI</h2>\n", f);
    fputs("<p>Pagina inicial creada por el escritorio 10AG.</p>\n", f);
    fputs("<p>Motor HTML Lexbor integrado en firmware normal, PSRAM-first.</p>\n", f);
    fputs("<ul>\n", f);
    fputs("<li><a href=\"/sdcard/test_10af_gui.html\">Prueba grafica 10AF en SD</a></li>\n", f);
    fputs("<li><a href=\"/sdcard/test_10ae_index.html\">Prueba favoritos 10AE en SD</a></li>\n", f);
    fputs("<li><a href=\"/sdcard/test_10ad_index.html\">Prueba historial 10AD en SD</a></li>\n", f);
    fputs("</ul>\n", f);
    fputs("<p>Controles: n/p scroll, 1-9 enlaces, b atras, r recarga, m favorito, l links, h ayuda, q salir.</p>\n", f);
    fputs("</body></html>\n", f);
    fclose(f);
    printf("[DESKTOP10AH] home creado: %s\n", TBB10AH_HOME_FILE);
}

static void desktop_10ah_restore_graphics(void)
{
    vterm_input_flush(vterm_get_active());
    rgb_display_set_mode(SM_400X240);
    desktop_palette();
    desk_gfx_clear(C_BG0);
    desktop_present_backbuffer();
}

static void desktop_10ah_launch_tbbrowser_gui(void)
{
    const char *cmd = "tbbgui home";

    printf("[DESKTOP10AH] WEB -> TactileBrowser Lexbor GUI\n");
    /* 10AH: tbbgui home usa pagina interna; dejamos el HTML antiguo como respaldo. */
    desktop_10ah_write_home_if_missing();

    /*
     * Salimos del desktop a texto para que breezybox_exec ejecute el comando
     * de consola tbbgui. El propio tbbgui entra en SM_400X240, pinta su GUI y
     * al salir vuelve a SM_TEXT. Al retornar restauramos el escritorio.
     */
    vterm_input_flush(vterm_get_active());
    rgb_display_set_mode(SM_TEXT);
    vTaskDelay(pdMS_TO_TICKS(80));

    printf("[DESKTOP10AH] exec: %s\n", cmd);
    breezybox_exec(cmd);

    desktop_10ah_restore_graphics();
    printf("[DESKTOP10AH] regreso de TactileBrowser GUI\n");
}

static void draw_current_view(desktop_view_t view, int mx, int my)
{
    switch (view) {
        case VIEW_FILES:  draw_files_page(mx, my); break;
        case VIEW_APPS:   draw_apps_page(mx, my); break;
        case VIEW_WIFI:   draw_wifi_page(mx, my); break;
        case VIEW_SYSTEM: draw_system_page(mx, my); break;
        case VIEW_ABOUT:  draw_about_page(mx, my); break;
        case VIEW_BROWSER: draw_browser_page(mx, my); break;
        case VIEW_TEXTEDIT: draw_textedit_page(mx, my); break;
        case VIEW_DESKTOP:
        default:          draw_desktop(mx, my); break;
    }
}

static int desktop_parse_seconds(int argc, char **argv)
{
    int seconds = 0; // 0 = hasta ESC/q/click X
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            seconds = atoi(argv[++i]);
            if (seconds < 1) seconds = 0;
        }
    }
    return seconds;
}

int cmd_desktop(int argc, char **argv)
{
    int seconds = desktop_parse_seconds(argc, argv);

    printf("Entrando en escritorio grafico 10AN navegador busy cursor...\n");
    printf("Iconos activos. FILES/TXT/GZIP; WEB navegador clasico 10AN; APPS lanza ELF.\n");
    printf("Editor TXT: OPEN/doble click sobre .txt/.log/.cfg/.ini/.json/.md/.c/.h, Ctrl+S/SAVE guarda.\n");
    printf("Salir: ESC, q/Q, click X. Volver: BACK o boton derecho.\n");
    vTaskDelay(pdMS_TO_TICKS(120));

    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("No se pudo entrar en SM_400X240.\n");
        return 1;
    }

    if (!s_desk_back) {
        s_desk_back = (uint8_t *)heap_caps_malloc(DESK_W * DESK_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_desk_back) {
            s_desk_back = (uint8_t *)heap_caps_malloc(DESK_W * DESK_H, MALLOC_CAP_8BIT);
        }
    }

    if (!s_desk_back) {
        rgb_display_set_mode(SM_TEXT);
        printf("No hay memoria para backbuffer desktop.\n");
        return 1;
    }

    desktop_palette();
    desk_gfx_clear(C_BG0);
    desktop_present_backbuffer();

    // Limpiar el buffer de entrada al entrar. Sin esto, el '\n' del Enter (o
    // restos de la linea de comando) quedan en el buffer y el primer
    // vterm_getchar los lee, provocando una salida espuria inmediata: por eso
    // hacia falta llamar a 'desktop' dos veces. Drenamos cualquier residuo.
    vterm_input_flush(vterm_get_active());
    {
        int drain;
        int guard = 0;
        do {
            drain = vterm_getchar(vterm_get_active(), 0);
        } while (drain >= 0 && ++guard < 64);
    }

    int64_t t_start = esp_timer_get_time();
    int last_mx = -1, last_my = -1;
    uint8_t last_draw_buttons = 0xFF;
    uint8_t prev_buttons = 0;
    int last_wifi = -1, last_sd = -1, last_usb = -1;
    int last_clock_min = -1;   // para refrescar el reloj cuando cambia el minuto, tambien en WEB
    int frame = 0;
    int exit_now = 0;
    int dirty = 1;
    desktop_view_t view = VIEW_DESKTOP;
    desktop_view_t last_view = (desktop_view_t)-1;

    while (!exit_now) {
        int mx = 0, my = 0;
        uint8_t buttons = 0;
        usb_hid_mouse_get_state(&mx, &my, &buttons);

        // Refrescar el reloj cuando cambia el minuto (sin gastar CPU repintando
        // cada frame). Solo en vistas con barra superior visible.
        {
            time_t now = 0; struct tm tmv;
            time(&now); localtime_r(&now, &tmv);
            if (tmv.tm_min != last_clock_min) {
                last_clock_min = tmv.tm_min;
                dirty = 1;
            }
        }

        // --- Touch GT911 como raton ---
        // Si hay toque, sobrescribe la posicion del cursor con la del dedo y
        // fuerza el boton izquierdo. El GT911 da 800x480; el escritorio trabaja
        // en 400x240, asi que dividimos entre 2. Cuando no hay toque, manda el
        // raton USB normalmente.
        {
            int16_t tx = 0, ty = 0;
            if (minipc_touch_ok() && minipc_touch_read(&tx, &ty)) {
                mx = tx / 2;
                my = ty / 2;
                if (mx < 0) mx = 0;
                if (mx >= DESK_W) mx = DESK_W - 1;
                if (my < 0) my = 0;
                if (my >= DESK_H) my = DESK_H - 1;
                buttons |= 0x01;   // dedo en pantalla = clic izquierdo
                // Sincronizar el puntero del raton USB para que no "salte"
                // a su posicion anterior al soltar el dedo.
                usb_hid_mouse_set_position(mx, my);
            }
        }

        int left_click  = ((buttons & 0x01) != 0) && ((prev_buttons & 0x01) == 0);
        int right_click = ((buttons & 0x02) != 0) && ((prev_buttons & 0x02) == 0);

        int wifi = minipc_wifi_is_connected() ? 1 : 0;
        int sd   = minipc_sd_is_mounted() ? 1 : 0;
        int usb  = minipc_usb_msc_mounted() ? 1 : 0;

        if (left_click) {
            if (view == VIEW_TEXTEDIT) {
                if (textedit_handle_click(mx, my, &view)) {
                    dirty = 1;
                }
            } else if (view != VIEW_DESKTOP && view != VIEW_BROWSER && back_button_hover(mx, my)) {
                view = VIEW_DESKTOP;
                dirty = 1;
            } else if (view == VIEW_FILES) {
                if (refresh_button_hover(mx, my)) {
                    g_fm_need_scan = 1;
                    fm_status("refresh");
                    dirty = 1;
                } else if (fm_handle_click(mx, my)) {
                    if (te_consume_open_request()) {
                        view = VIEW_TEXTEDIT;
                    }
                    dirty = 1;
                }
            } else if (view == VIEW_APPS) {
                if (refresh_button_hover(mx, my)) {
                    g_app_need_scan = 1;
                    apps_status("refresh");
                    dirty = 1;
                } else if (apps_handle_click(mx, my)) {
                    dirty = 1;
                }
            } else if (view == VIEW_WIFI) {
                // Botón CONECTAR
                if (inside(mx, my, 30, 140, 90, 16)) {
                    if (g_wifi_edit_ssid[0]) {
                        g_wifi_connecting = 1;
                        g_wifi_edit_msg[0] = '\0';
                        // Pintar "Conectando..." antes de bloquear
                        draw_current_view(view, mx, my);
                        desktop_present_backbuffer();
                        int r = minipc_wifi_reconnect_with(g_wifi_edit_ssid, g_wifi_edit_pass);
                        g_wifi_connecting = 0;
                        if (r == 0) {
                            snprintf(g_wifi_edit_msg, sizeof(g_wifi_edit_msg), "Conectado y guardado OK");
                        } else {
                            snprintf(g_wifi_edit_msg, sizeof(g_wifi_edit_msg), "Fallo. Revisa SSID/pass y reintenta");
                        }
                        dirty = 1;
                    } else {
                        snprintf(g_wifi_edit_msg, sizeof(g_wifi_edit_msg), "Escribe un SSID");
                        dirty = 1;
                    }
                } else if (inside(mx, my, 80, 99, 250, 12)) {
                    g_wifi_edit_field = 0;   // click en campo SSID
                    dirty = 1;
                } else if (inside(mx, my, 80, 117, 250, 12)) {
                    g_wifi_edit_field = 1;   // click en campo pass
                    dirty = 1;
                }
            } else if (view == VIEW_BROWSER) {
                if (browser_handle_click(mx, my, &view)) {
                    dirty = 1;
                }
            } else if (view == VIEW_DESKTOP) {
                int c = card_at(mx, my);
                if (c >= 0) {
                    if (strcmp(g_cards[c].title, "FILES") == 0) {
                        fm_init_once();
                        g_fm_need_scan = 1;
                        view = VIEW_FILES;
                    } else if (strcmp(g_cards[c].title, "TERMINAL") == 0) {
                        exit_now = 1;
                    } else if (strcmp(g_cards[c].title, "WIFI") == 0) {
                        // Precargar el SSID actual en el editor (solo la 1a vez)
                        if (!g_wifi_edit_init) {
                            snprintf(g_wifi_edit_ssid, sizeof(g_wifi_edit_ssid),
                                     "%s", minipc_wifi_get_ssid());
                            g_wifi_edit_pass[0] = '\0';
                            g_wifi_edit_field = 0;
                            g_wifi_edit_msg[0] = '\0';
                            g_wifi_edit_init = 1;
                        }
                        vterm_input_flush(vterm_get_active());
                        view = VIEW_WIFI;
                    } else if (strcmp(g_cards[c].title, "APPS") == 0) {
                        g_app_need_scan = 1;
                        view = VIEW_APPS;
                    } else if (strcmp(g_cards[c].title, "WEB") == 0) {
                        /* 10AH: el boton WEB abre el navegador Lexbor GUI interno.
                         * Lanza el TactileBrowser Lexbor GUI 10AI con home interno.
                         * El motor viejo queda compilado como respaldo, pero
                         * fuera del flujo principal del escritorio.
                         */
                        desktop_10ah_launch_tbbrowser_gui();
                        view = VIEW_DESKTOP;
                    } else if (strcmp(g_cards[c].title, "SYSTEM") == 0) {
                        view = VIEW_SYSTEM;
                    } else if (strcmp(g_cards[c].title, "ABOUT") == 0) {
                        view = VIEW_ABOUT;
                    }
                    dirty = 1;
                }
            } else if (refresh_button_hover(mx, my)) {
                dirty = 1;
            }
        }

        if (right_click) {
            if (view == VIEW_TEXTEDIT) {
                view = VIEW_FILES;
                dirty = 1;
            } else if (view != VIEW_DESKTOP) {
                view = VIEW_DESKTOP;
                dirty = 1;
            } else {
                exit_now = 1;
            }
        }

        int ch = vterm_getchar(vterm_get_active(), 0);

        // --- Deteccion de secuencias de escape (flechas) ---
        // Las flechas mandan ESC '[' 'A/B/C/D'. Sin esto, el ESC inicial
        // cerraba el escritorio en cada pulsacion de flecha. Distinguimos:
        // ESC sola = salir/volver; ESC + '[' + letra = tecla de navegacion.
        int arrow = 0;   // 0=ninguna, 'U'=up,'D'=down,'L'=left,'R'=right
        if (ch == 27) {
            int c2 = vterm_getchar(vterm_get_active(), 0);
            if (c2 == '[' || c2 == 'O') {
                int c3 = vterm_getchar(vterm_get_active(), 0);
                switch (c3) {
                    case 'A': arrow = 'U'; break;
                    case 'B': arrow = 'D'; break;
                    case 'C': arrow = 'R'; break;
                    case 'D': arrow = 'L'; break;
                    default: break;   // otra secuencia: ignorar
                }
                ch = -1;   // consumida como secuencia, no es ESC sola
            } else if (c2 >= 0) {
                // ESC + algo que no es secuencia conocida: tratar como ESC
                // (y descartar c2)
            }
            // si c2 < 0 (nada detras), ch sigue siendo 27 = ESC sola
        }

        // --- Navegacion con flechas segun la vista ---
        if (arrow) {
            if (view == VIEW_BROWSER) {
                int total = minipc_browser_line_count();
                if (arrow == 'D' && g_browser_scroll + BROWSER_VISIBLE_LINES < total) {
                    g_browser_scroll++; dirty = 1;
                } else if (arrow == 'U' && g_browser_scroll > 0) {
                    g_browser_scroll--; dirty = 1;
                }
            } else if (view == VIEW_FILES) {
                if (arrow == 'D') { fm_select_next_row(); dirty = 1; }
                else if (arrow == 'U') { fm_select_prev_row(); dirty = 1; }
                else if (arrow == 'R' || arrow == 'L') { g_fm_active = 1 - g_fm_active; dirty = 1; }
            } else if (view == VIEW_APPS) {
                if (arrow == 'D') {
                    if (g_app_selected + 1 < g_app_count) {
                        g_app_selected++;
                        if (g_app_selected >= g_app_offset + APP_VISIBLE) g_app_offset++;
                        dirty = 1;
                    }
                } else if (arrow == 'U') {
                    if (g_app_selected > 0) {
                        g_app_selected--;
                        if (g_app_selected < g_app_offset) g_app_offset = g_app_selected;
                        dirty = 1;
                    }
                } else if (arrow == 'R') {
                    apps_next(); dirty = 1;
                } else if (arrow == 'L') {
                    apps_prev(); dirty = 1;
                }
            } else if (view == VIEW_WIFI) {
                // Flechas arriba/abajo cambian entre campo SSID y password
                if (arrow == 'U' || arrow == 'D') {
                    g_wifi_edit_field = 1 - g_wifi_edit_field;
                    dirty = 1;
                }
            } else if (view == VIEW_TEXTEDIT) {
                textedit_handle_arrow(arrow);
                dirty = 1;
            }
            goto after_input;
        }

        // --- Manejo especial de teclado en el EDITOR WIFI ---
        if (view == VIEW_WIFI) {
            if (ch == 27) {
                view = VIEW_DESKTOP;
                dirty = 1;
            } else if (ch == '\t') {
                // TAB: cambiar entre campo SSID y password
                g_wifi_edit_field = 1 - g_wifi_edit_field;
                dirty = 1;
            } else if (ch == '\r' || ch == '\n') {
                // Enter: conectar (igual que el boton)
                if (g_wifi_edit_ssid[0]) {
                    g_wifi_connecting = 1;
                    g_wifi_edit_msg[0] = '\0';
                    draw_current_view(view, mx, my);
                    desktop_present_backbuffer();
                    int r = minipc_wifi_reconnect_with(g_wifi_edit_ssid, g_wifi_edit_pass);
                    g_wifi_connecting = 0;
                    if (r == 0) snprintf(g_wifi_edit_msg, sizeof(g_wifi_edit_msg), "Conectado y guardado OK");
                    else snprintf(g_wifi_edit_msg, sizeof(g_wifi_edit_msg), "Fallo. Revisa SSID/pass y reintenta");
                    dirty = 1;
                }
            } else if (ch == 8 || ch == 127) {
                // Backspace en el campo activo
                char *buf = (g_wifi_edit_field == 0) ? g_wifi_edit_ssid : g_wifi_edit_pass;
                int l = strlen(buf);
                if (l > 0) { buf[l-1] = '\0'; dirty = 1; }
            } else if (ch >= 32 && ch < 127) {
                // Caracter imprimible al campo activo
                char *buf = (g_wifi_edit_field == 0) ? g_wifi_edit_ssid : g_wifi_edit_pass;
                int cap = (g_wifi_edit_field == 0) ? 32 : 64;
                int l = strlen(buf);
                if (l < cap) { buf[l] = (char)ch; buf[l+1] = '\0'; dirty = 1; }
                // Si estabas editando tras una conexion OK, limpiar el mensaje
                // para que la contraseña vuelva a verse en claro mientras editas.
                g_wifi_edit_msg[0] = '\0';
            }
            goto after_input;
        }

        // --- Manejo especial de teclado en el NAVEGADOR ---
        if (view == VIEW_BROWSER) {
            if (ch == 27) {
                if (s_browser_overlay != 0) {
                    // Si HIST/FAV esta abierto, ESC lo cierra primero.
                    s_browser_overlay = 0;
                } else {
                    // ESC: volver al escritorio (no salir del desktop)
                    view = VIEW_DESKTOP;
                }
                dirty = 1;
            } else if (s_browser_overlay != 0) {
                // Mientras HIST/FAV esta abierto, ignoramos el resto de teclas
                // (evita escribir sin querer en la barra de URL de fondo).
            } else if (ch == 6) {   // Ctrl+F
                // Preparar busqueda/formulario: deja "?" en la barra.
                browser_prepare_form_query();
                dirty = 1;
            } else if (ch == '\r' || ch == '\n') {
                // Enter: cargar la URL escrita
                browser_load_typed_url(mx, my);
                dirty = 1;
            } else if (ch == 21) {   // Ctrl+U
    		// Limpiar URL completa
    		g_url_len = 0;
    		g_url[0] = '\0';
    		dirty = 1;
	   } else if (ch == 8 || ch == 127) {
    		// Backspace: borrar ultimo char.
    		// Antes protegía "http://"; ahora permitimos borrar todo para escribir https://
   		 if (g_url_len > 0) {
       		 g_url_len--;
       		 g_url[g_url_len] = '\0';
       		 dirty = 1;
    		}
            } else if (ch == '\t') {
                // TAB: scroll abajo (no se usa en URLs)
                int total = minipc_browser_line_count();
                if (g_browser_scroll + BROWSER_VISIBLE_LINES < total) {
                    g_browser_scroll++; dirty = 1;
                }
            } else if (ch >= 32 && ch < 127) {
                // caracter imprimible: añadir a la URL (TODAS las letras, incluida s)
                if (g_url_len < (int)sizeof(g_url) - 1) {
                    g_url[g_url_len++] = (char)ch;
                    g_url[g_url_len] = '\0';
                    dirty = 1;
                }
            }
            // En el navegador NO procesamos el resto (q no sale, etc.)
            goto after_input;
        }

        if (view == VIEW_TEXTEDIT) {
            textedit_handle_key(ch, &view);
            dirty = 1;
            goto after_input;
        }

        if (ch == 27 || ch == 'q' || ch == 'Q') {
            exit_now = 1;
        } else if (view == VIEW_FILES && (ch == '\r' || ch == '\n')) {
            fm_open_active();
            if (te_consume_open_request()) {
                view = VIEW_TEXTEDIT;
            }
            dirty = 1;
        } else if (view == VIEW_FILES && ch == '\t') {
            g_fm_active = 1 - g_fm_active;
            dirty = 1;
        } else if (view == VIEW_FILES && ch >= '1' && ch <= '4') {
            if (ch == '1') fm_set_panel_location(g_fm_active, FM_LOC_ROOT);
            else if (ch == '2') fm_set_panel_location(g_fm_active, FM_LOC_ROOTBIN);
            else if (ch == '3') fm_set_panel_location(g_fm_active, FM_LOC_SDCARD);
            else if (ch == '4') fm_set_panel_location(g_fm_active, FM_LOC_USB);
            dirty = 1;
        } else if (view == VIEW_APPS && (ch == '\r' || ch == '\n')) {
            apps_launch_selected();
            dirty = 1;
        } else if (view == VIEW_APPS && ch == '\t') {
            if (g_app_loc == APP_LOC_ROOTBIN) apps_set_location(APP_LOC_SDCARD);
            else if (g_app_loc == APP_LOC_SDCARD) apps_set_location(APP_LOC_USB);
            else apps_set_location(APP_LOC_ROOTBIN);
            dirty = 1;
        } else if (view == VIEW_APPS && (ch == 'r' || ch == 'R')) {
            g_app_need_scan = 1;
            dirty = 1;
        } else if (ch == 8 || ch == 127) {
            if (view == VIEW_FILES) {
                fm_up_active();
                dirty = 1;
            } else if (view != VIEW_DESKTOP) {
                view = VIEW_DESKTOP;
                dirty = 1;
            }
        }
after_input:;

        if (seconds > 0) {
            int64_t elapsed = (esp_timer_get_time() - t_start) / 1000000LL;
            if (elapsed >= seconds) exit_now = 1;
        }

        if (view != last_view ||
            mx != last_mx || my != last_my || buttons != last_draw_buttons ||
            wifi != last_wifi || sd != last_sd || usb != last_usb ||
            dirty || (frame % 20) == 0) {
            draw_current_view(view, mx, my);
            desktop_present_backbuffer();

            last_mx = mx;
            last_my = my;
            last_draw_buttons = buttons;
            last_wifi = wifi;
            last_sd = sd;
            last_usb = usb;
            last_view = view;
            dirty = 0;
        }

        prev_buttons = buttons;

        rgb_display_wait_vsync();
        vTaskDelay(pdMS_TO_TICKS(20));
        frame++;
    }

    // Vaciar el buffer de teclado antes de volver a la terminal. Sin esto, lo
    // que se tecleo en el navegador/escritorio quedaba en el buffer del stdio y
    // aparecia como una ristra al dar Enter en la terminal. (Detectado por Arielo.)
    vterm_input_flush(vterm_get_active());

    rgb_display_set_mode(SM_TEXT);

    if (s_desk_back) {
        heap_caps_free(s_desk_back);
        s_desk_back = NULL;
    }

    printf("Escritorio grafico 10AG cerrado.\n");
    return 0;
}
