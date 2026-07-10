/*
 * minipc_bootpixel_03d.c - 03D_FIX8_PANEL_TALL_NO_S3
 *
 * Arielo MiniPC OS - splash grafico real.
 *
 * Esta rama sube del splash por celdas a modo grafico:
 *   - rgb_display_set_mode(SM_400X240)
 *   - framebuffer indexado 8bpp
 *   - paleta RGB565 propia
 *   - dibujo directo por pixeles en SM_400X240 con driver fast path x2
 *   - panel de estado mas alto y sin texto S3 en el logo
 *
 * Es una prueba segura:
 *   - No toca BreezyBox.
 *   - No usa VTerm durante splash.
 *   - Al terminar vuelve a SM_TEXT y my_console_init() recupera la pantalla.
 */

#include "minipc_bootpixel_03d.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rgb_display.h"

#define MAX_ITEMS  8
#define NAME_LEN   20
#define STAT_LEN   32

// Paleta Arielo MiniPC OS
enum {
    C_BG0 = 0,
    C_BG1,
    C_BG2,
    C_GRID,
    C_TRACE,
    C_CYAN,
    C_CYAN_BRIGHT,
    C_WHITE,
    C_DIM,
    C_GREEN,
    C_YELLOW,
    C_RED,
    C_BLUE,
    C_PURPLE,
    C_CHIP,
    C_BAR
};

typedef struct {
    char name[NAME_LEN];
    char status[STAT_LEN];
} boot_item_t;

static uint8_t *s_fb = NULL;
static int s_w = 0;
static int s_h = 0;

static boot_item_t s_items[MAX_ITEMS];
static int s_count = 0;
static int s_ready = 0;
static int s_graphics_ok = 0;
static char s_message[64] = "STARTING SYSTEM";

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r & 0xF8) << 8) |
           ((uint16_t)(g & 0xFC) << 3) |
           ((uint16_t)(b) >> 3);
}

static void setup_palette(void)
{
    rgb_display_set_vga_palette_entry(C_BG0,        rgb565(2,   6,  18));
    rgb_display_set_vga_palette_entry(C_BG1,        rgb565(4,  18,  42));
    rgb_display_set_vga_palette_entry(C_BG2,        rgb565(8,  34,  74));
    rgb_display_set_vga_palette_entry(C_GRID,       rgb565(12, 42,  84));
    rgb_display_set_vga_palette_entry(C_TRACE,      rgb565(0, 150, 190));
    rgb_display_set_vga_palette_entry(C_CYAN,       rgb565(0, 190, 220));
    rgb_display_set_vga_palette_entry(C_CYAN_BRIGHT,rgb565(70,240, 255));
    rgb_display_set_vga_palette_entry(C_WHITE,      rgb565(230,245,255));
    rgb_display_set_vga_palette_entry(C_DIM,        rgb565(80, 120,150));
    rgb_display_set_vga_palette_entry(C_GREEN,      rgb565(40, 230,120));
    rgb_display_set_vga_palette_entry(C_YELLOW,     rgb565(250,210,70));
    rgb_display_set_vga_palette_entry(C_RED,        rgb565(255,80, 70));
    rgb_display_set_vga_palette_entry(C_BLUE,       rgb565(20, 90, 190));
    rgb_display_set_vga_palette_entry(C_PURPLE,     rgb565(120,70, 210));
    rgb_display_set_vga_palette_entry(C_CHIP,       rgb565(16, 45,  82));
    rgb_display_set_vga_palette_entry(C_BAR,        rgb565(25, 180,220));
}

static void boot_delay(void)
{
    vTaskDelay(pdMS_TO_TICKS(80));
}

static void safe_copy_upper(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) return;
    if (!src) src = "";

    size_t j = 0;
    while (*src && j + 1 < dst_len) {
        char c = *src++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        dst[j++] = c;
    }
    dst[j] = 0;
}

static void px(int x, int y, uint8_t c)
{
    if (!s_fb) return;
    if (x < 0 || y < 0 || x >= s_w || y >= s_h) return;
    s_fb[y * s_w + x] = c;
}

static void rect_fill(int x, int y, int w, int h, uint8_t c)
{
    if (!s_fb) return;
    if (w <= 0 || h <= 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_w) w = s_w - x;
    if (y + h > s_h) h = s_h - y;
    if (w <= 0 || h <= 0) return;

    for (int yy = y; yy < y + h; yy++) {
        memset(&s_fb[yy * s_w + x], c, (size_t)w);
    }
}

static void rect(int x, int y, int w, int h, uint8_t c)
{
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; i++) {
        px(x + i, y, c);
        px(x + i, y + h - 1, c);
    }
    for (int i = 0; i < h; i++) {
        px(x, y + i, c);
        px(x + w - 1, y + i, c);
    }
}

static void line(int x0, int y0, int x1, int y1, uint8_t c)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void circle(int cx, int cy, int r, uint8_t c)
{
    int x = -r;
    int y = 0;
    int err = 2 - 2 * r;

    do {
        px(cx - x, cy + y, c);
        px(cx - y, cy - x, c);
        px(cx + x, cy - y, c);
        px(cx + y, cy + x, c);

        int e2 = err;
        if (e2 <= y) {
            y++;
            err += y * 2 + 1;
        }
        if (e2 > x || err > y) {
            x++;
            err += x * 2 + 1;
        }
    } while (x < 0);
}

static const uint8_t *glyph5x7(char ch)
{
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t qmark[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};

    switch (ch) {
    case 'A': { static const uint8_t g[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g; }
    case 'B': { static const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return g; }
    case 'C': { static const uint8_t g[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g; }
    case 'D': { static const uint8_t g[7]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}; return g; }
    case 'E': { static const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return g; }
    case 'F': { static const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return g; }
    case 'G': { static const uint8_t g[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; return g; }
    case 'H': { static const uint8_t g[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return g; }
    case 'I': { static const uint8_t g[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}; return g; }
    case 'J': { static const uint8_t g[7]={0x07,0x02,0x02,0x02,0x12,0x12,0x0C}; return g; }
    case 'K': { static const uint8_t g[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g; }
    case 'L': { static const uint8_t g[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g; }
    case 'M': { static const uint8_t g[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g; }
    case 'N': { static const uint8_t g[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return g; }
    case 'O': { static const uint8_t g[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g; }
    case 'P': { static const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g; }
    case 'Q': { static const uint8_t g[7]={0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return g; }
    case 'R': { static const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g; }
    case 'S': { static const uint8_t g[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g; }
    case 'T': { static const uint8_t g[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g; }
    case 'U': { static const uint8_t g[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g; }
    case 'V': { static const uint8_t g[7]={0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return g; }
    case 'W': { static const uint8_t g[7]={0x11,0x11,0x11,0x15,0x15,0x15,0x0A}; return g; }
    case 'X': { static const uint8_t g[7]={0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return g; }
    case 'Y': { static const uint8_t g[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return g; }
    case 'Z': { static const uint8_t g[7]={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return g; }

    case '0': { static const uint8_t g[7]={0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return g; }
    case '1': { static const uint8_t g[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; return g; }
    case '2': { static const uint8_t g[7]={0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; return g; }
    case '3': { static const uint8_t g[7]={0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}; return g; }
    case '4': { static const uint8_t g[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return g; }
    case '5': { static const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}; return g; }
    case '6': { static const uint8_t g[7]={0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}; return g; }
    case '7': { static const uint8_t g[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return g; }
    case '8': { static const uint8_t g[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return g; }
    case '9': { static const uint8_t g[7]={0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}; return g; }

    case ' ': return blank;
    case '-': { static const uint8_t g[7]={0,0,0,0x1F,0,0,0}; return g; }
    case '.': { static const uint8_t g[7]={0,0,0,0,0,0x0C,0x0C}; return g; }
    case ':': { static const uint8_t g[7]={0,0x0C,0x0C,0,0x0C,0x0C,0}; return g; }
    case '/': { static const uint8_t g[7]={0x01,0x02,0x02,0x04,0x08,0x08,0x10}; return g; }
    case '[': { static const uint8_t g[7]={0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}; return g; }
    case ']': { static const uint8_t g[7]={0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}; return g; }
    case '_': { static const uint8_t g[7]={0,0,0,0,0,0,0x1F}; return g; }
    default: return qmark;
    }
}

static void draw_char(int x, int y, char ch, uint8_t color, int scale)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    const uint8_t *g = glyph5x7(ch);

    if (scale < 1) scale = 1;
    for (int row = 0; row < 7; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (1 << (4 - col))) {
                rect_fill(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void draw_text(int x, int y, const char *s, uint8_t color, int scale)
{
    if (!s) return;
    int step = 6 * scale;
    while (*s) {
        draw_char(x, y, *s, color, scale);
        x += step;
        s++;
    }
}

static int text_width(const char *s, int scale)
{
    if (!s) return 0;
    return (int)strlen(s) * 6 * scale;
}

static void draw_text_center(int y, const char *s, uint8_t color, int scale)
{
    int x = (s_w - text_width(s, scale)) / 2;
    if (x < 0) x = 0;
    draw_text(x, y, s, color, scale);
}

static int status_index(const char *name)
{
    if (!name) return -1;

    char up[NAME_LEN];
    safe_copy_upper(up, sizeof(up), name);

    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_items[i].name, up) == 0) return i;
    }

    if (s_count >= MAX_ITEMS) return -1;

    safe_copy_upper(s_items[s_count].name, sizeof(s_items[s_count].name), name);
    safe_copy_upper(s_items[s_count].status, sizeof(s_items[s_count].status), "...");
    return s_count++;
}

static uint8_t color_for_status(const char *status)
{
    if (!status) return C_WHITE;
    if (strncmp(status, "OK", 2) == 0) return C_GREEN;
    if (strncmp(status, "FAIL", 4) == 0) return C_RED;
    if (strncmp(status, "START", 5) == 0) return C_YELLOW;
    if (strncmp(status, "CONNECT", 7) == 0) return C_YELLOW;
    if (strncmp(status, "BACKGROUND", 10) == 0) return C_YELLOW;
    return C_CYAN_BRIGHT;
}

static void draw_background(void)
{
    // Bandas horizontales, simulando degradado sencillo sin coste alto
    for (int y = 0; y < s_h; y++) {
        uint8_t c = C_BG0;
        if (y > s_h / 3) c = C_BG1;
        if (y > (s_h * 2) / 3) c = C_BG2;
        rect_fill(0, y, s_w, 1, c);
    }

    // Grid discreto
    for (int x = 0; x < s_w; x += 16) line(x, 0, x, s_h - 1, C_GRID);
    for (int y = 0; y < s_h; y += 16) line(0, y, s_w - 1, y, C_GRID);

    // Trazas electronicas
    line(20, 42, 88, 42, C_TRACE);
    line(88, 42, 110, 60, C_TRACE);
    line(300, 48, 232, 48, C_TRACE);
    line(232, 48, 210, 66, C_TRACE);
    line(28, 156, 100, 156, C_TRACE);
    line(100, 156, 124, 138, C_TRACE);
    line(292, 154, 220, 154, C_TRACE);
    line(220, 154, 196, 136, C_TRACE);

    circle(20, 42, 3, C_CYAN_BRIGHT);
    circle(300, 48, 3, C_CYAN_BRIGHT);
    circle(28, 156, 3, C_CYAN_BRIGHT);
    circle(292, 154, 3, C_CYAN_BRIGHT);
}

static void draw_chip_logo(void)
{
    const int cx = s_w / 2;
    const int cy = 58;

    // Halo
    circle(cx, cy, 48, C_BLUE);
    circle(cx, cy, 38, C_PURPLE);
    circle(cx, cy, 28, C_TRACE);

    // Chip
    int x = cx - 48;
    int y = cy - 28;
    int w = 96;
    int h = 56;

    rect_fill(x, y, w, h, C_CHIP);
    rect(x, y, w, h, C_CYAN_BRIGHT);
    rect(x + 3, y + 3, w - 6, h - 6, C_TRACE);

    // Pines
    for (int i = 0; i < 8; i++) {
        int py = y + 8 + i * 5;
        rect_fill(x - 10, py, 10, 2, C_DIM);
        rect_fill(x + w, py, 10, 2, C_DIM);
    }
    for (int i = 0; i < 8; i++) {
        int px0 = x + 10 + i * 10;
        rect_fill(px0, y - 7, 2, 7, C_DIM);
        rect_fill(px0, y + h, 2, 7, C_DIM);
    }

    // Letra A pixel-art
    line(cx - 22, cy + 14, cx - 4, cy - 16, C_WHITE);
    line(cx + 22, cy + 14, cx + 4, cy - 16, C_WHITE);
    line(cx - 12, cy + 1, cx + 12, cy + 1, C_CYAN_BRIGHT);
    rect_fill(cx - 3, cy - 16, 6, 6, C_CYAN_BRIGHT);

    // Badge OS
    rect_fill(cx + 27, cy - 19, 28, 18, C_BLUE);
    rect(cx + 27, cy - 19, 28, 18, C_CYAN_BRIGHT);
    draw_text(cx + 33, cy - 14, "OS", C_WHITE, 1);

    // FIX8: se quita el texto "S3" del logo para dejarlo mas limpio.
}

static const char *short_status_name(const char *name)
{
    if (!name) return "";
    if (strcmp(name, "DISPLAY") == 0) return "DISP";
    if (strcmp(name, "STORAGE") == 0) return "SD";
    if (strcmp(name, "USB HOST") == 0) return "USB";
    if (strcmp(name, "KEYBOARD") == 0) return "KBD";
    if (strcmp(name, "WIFI") == 0) return "WIFI";
    if (strcmp(name, "NVS") == 0) return "NVS";
    return name;
}

static void draw_text_clip(int x, int y, const char *s, uint8_t color, int scale, int max_chars)
{
    if (!s || max_chars <= 0) return;

    int step = 6 * scale;
    int n = 0;
    while (*s && n < max_chars) {
        draw_char(x, y, *s, color, scale);
        x += step;
        s++;
        n++;
    }
}

static void draw_status_panel(void)
{
    /*
     * FIX7:
     * En 400x240 las letras 5x7 ocupan bastante. Antes se metian 6 filas
     * dentro de una caja de ~52px y los textos se montaban. Ahora usamos
     * panel mas bajo, filas de 14px y etiquetas cortas.
     */
    int x = 34;
    int y = 116;
    int w = s_w - 68;
    int h = 84;

    rect_fill(x, y, w, h, C_BG1);
    rect(x, y, w, h, C_CYAN);
    rect(x + 2, y + 2, w - 4, h - 4, C_GRID);

    draw_text(x + 10, y + 8, "BOOT STATUS", C_YELLOW, 1);

    const int row_y0 = y + 26;
    const int row_step = 14;
    const int max_rows = 4;

    /*
     * Mostramos solo las ultimas 4 lineas: el arranque se entiende mejor
     * y evitamos saturar el panel. La barra de progreso sigue mostrando
     * el avance total.
     */
    int first = 0;
    if (s_count > max_rows) {
        first = s_count - max_rows;
    }

    for (int i = first; i < s_count && (i - first) < max_rows; i++) {
        int row = i - first;
        int yy = row_y0 + row * row_step;

        const char *label = short_status_name(s_items[i].name);

        draw_text_clip(x + 12, yy, label, C_WHITE, 1, 8);

        // Separador corto, por debajo de la mitad de la letra para no pisarla.
        for (int px0 = x + 78; px0 < x + 166; px0 += 8) {
            px(px0, yy + 5, C_DIM);
        }

        draw_text_clip(x + 176, yy, s_items[i].status,
                       color_for_status(s_items[i].status), 1, 12);
    }
}

static void draw_progress(void)
{
    int x = 38;
    int y = 208;
    int w = s_w - 76;
    int h = 8;

    rect(x, y, w, h, C_CYAN);
    rect_fill(x + 2, y + 2, w - 4, h - 4, C_BG0);

    int filled;
    if (s_ready) {
        filled = w - 4;
    } else {
        filled = ((s_count > 0 ? s_count : 1) * (w - 4)) / 7;
        if (filled < 16) filled = 16;
        if (filled > w - 4) filled = w - 4;
    }

    rect_fill(x + 2, y + 2, filled, h - 4, s_ready ? C_GREEN : C_BAR);
}

static void redraw(void)
{
    if (!s_graphics_ok || !s_fb) return;

    draw_background();
    draw_chip_logo();

    draw_text_center(92, "ARIELO MINIPC OS", C_WHITE, 2);
    draw_text_center(109, s_message, C_CYAN_BRIGHT, 1);

    draw_status_panel();
    draw_progress();

    if (s_ready) {
        draw_text_center(224, "SYSTEM READY - ENTERING SHELL", C_GREEN, 1);
    } else {
        draw_text_center(224, "GRAPHIC BOOT SPLASH ACTIVE", C_DIM, 1);
    }

    rgb_display_wait_vsync();
}

void minipc_bootpixel_begin(void)
{
    memset(s_items, 0, sizeof(s_items));
    s_count = 0;
    s_ready = 0;
    s_graphics_ok = 0;
    safe_copy_upper(s_message, sizeof(s_message), "STARTING SYSTEM");

    if (rgb_display_set_mode(SM_400X240) != 0) {
        return;
    }

    s_fb = rgb_display_get_framebuffer();
    s_w = rgb_display_get_fb_width();
    s_h = rgb_display_get_fb_height();

    if (!s_fb || s_w <= 0 || s_h <= 0) {
        return;
    }

    setup_palette();
    s_graphics_ok = 1;

    minipc_bootpixel_ok("Display");
}

void minipc_bootpixel_status(const char *name, const char *status)
{
    int idx = status_index(name);
    if (idx < 0) return;

    char up[STAT_LEN];
    safe_copy_upper(up, sizeof(up), status);

    // 03D_FIX6: si el estado no cambia, no redibujamos.
    if (strcmp(s_items[idx].status, up) == 0) {
        return;
    }

    safe_copy_upper(s_items[idx].status, sizeof(s_items[idx].status), status);

    redraw();
    boot_delay();
}

void minipc_bootpixel_ok(const char *name)
{
    minipc_bootpixel_status(name, "OK");
}

void minipc_bootpixel_wait(const char *name)
{
    minipc_bootpixel_status(name, "starting");
}

void minipc_bootpixel_fail(const char *name)
{
    minipc_bootpixel_status(name, "FAIL");
}

void minipc_bootpixel_message(const char *msg)
{
    char up[sizeof(s_message)];
    safe_copy_upper(up, sizeof(up), msg);

    // 03D_FIX6: si el mensaje no cambia, no redibujamos.
    if (strcmp(s_message, up) == 0) {
        return;
    }

    safe_copy_upper(s_message, sizeof(s_message), msg);
    redraw();
    boot_delay();
}

void minipc_bootpixel_ready(void)
{
    s_ready = 1;
    safe_copy_upper(s_message, sizeof(s_message), "BOOT SEQUENCE COMPLETE");
    redraw();
    vTaskDelay(pdMS_TO_TICKS(1100));
}

void minipc_bootpixel_finish_to_text(void)
{
    /*
     * Volver a texto ANTES de my_console_init().
     * Luego la consola enlaza el buffer VTerm como siempre.
     */
    rgb_display_set_mode(SM_TEXT);
    s_fb = NULL;
    s_w = 0;
    s_h = 0;
    s_graphics_ok = 0;
    vTaskDelay(pdMS_TO_TICKS(80));
}
