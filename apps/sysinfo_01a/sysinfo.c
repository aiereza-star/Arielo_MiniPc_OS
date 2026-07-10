/*
 * sysinfo.c - Herramienta SYSINFO externa para Arielo MiniPC OS
 *
 * SYSINFO_01A_FIX2_UI_ORDER
 *
 * Chasis:
 *   - Basado en el chasis REAL de Calculator / Notepad:
 *       rgb_display_set_mode(SM_400X240)
 *       setup_palette()
 *       rgb_gfx_clear()
 *       rgb_gfx_rectfill()
 *       rgb_display_wait_vsync()
 *   - Entrada por teclado BLE/USB via bt_keyboard_is_pressed()
 *   - Ratón USB via usb_hid_mouse_get_state()
 *   - Touch GT911 via minipc_touch_read()
 *   - Sin framebuffer directo
 *   - Sin tocar el SO base
 *
 * Controles:
 *   R            : refrescar
 *   S            : guardar snapshot en /root/sysinfo.txt
 *   ESC / Q      : salir
 *   Touch/mouse  : botones REFRESH / SAVE / EXIT
 *
 * Nota:
 *   Esta 01A evita dependencias de WiFi/IP/FS free porque no queremos pedir
 *   símbolos nuevos al firmware. Muestra lo seguro desde app externa.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>

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
#define COL_RED       10
#define COL_BLUE      11
#define COL_BAR       12
#define COL_BAR2      13

#define BT_KEY_ESC          0x29
#define BT_KEY_ENTER        0x28
#define BT_KEY_BACKSPACE    0x2A

#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT      (1U << 2)
#endif
#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM    (1U << 10)
#endif
#ifndef MALLOC_CAP_INTERNAL
#define MALLOC_CAP_INTERNAL  (1U << 11)
#endif

extern size_t heap_caps_get_free_size(uint32_t caps);
extern size_t heap_caps_get_largest_free_block(uint32_t caps);

typedef struct {
    uint32_t int_free;
    uint32_t int_big;
    uint32_t ps_free;
    uint32_t ps_big;
    int uptime_s;

    int root_ok;
    int rootbin_ok;
    int sd_ok;
    int usb_ok;

    int root_items;
    int rootbin_items;
    int sd_items;
    int usb_items;

    int bt_ok;
    int touch_ok;
    int pointer_x;
    int pointer_y;
    uint8_t pointer_buttons;
} sysinfo_t;

static uint8_t prev_keys[32];
static uint8_t prev_buttons;
static int last_pointer_x = -1;
static int last_pointer_y = -1;
static char status_line[80] = "SYSINFO 01A listo";
static sysinfo_t info;
static int redraw_count;
static int approx_uptime_s;
static int approx_frame_counter;

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
    pal[COL_RED]      = 0xF800;
    pal[COL_BLUE]     = 0x001F;
    pal[COL_BAR]      = 0x03EF;
    pal[COL_BAR2]     = 0x7BEF;
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

    if (w <= 0 || h <= 0) return;

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

static void format_kb(char *out, size_t out_sz, uint32_t bytes)
{
    if (bytes >= (1024U * 1024U)) {
        snprintf(out, out_sz, "%lu.%02lu MB",
                 (unsigned long)(bytes / (1024U * 1024U)),
                 (unsigned long)((bytes % (1024U * 1024U)) * 100U / (1024U * 1024U)));
    } else {
        snprintf(out, out_sz, "%lu KB", (unsigned long)(bytes / 1024U));
    }
}

static int path_exists_dir(const char *path)
{
    DIR *d = opendir(path);
    if (d) {
        closedir(d);
        return 1;
    }

    return 0;
}

static int count_dir_limited(const char *path, int limit)
{
    int n = 0;
    DIR *d = opendir(path);

    if (!d) return -1;

    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < limit) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        n++;
    }

    closedir(d);
    return n;
}

static void refresh_info(void)
{
    memset(&info, 0, sizeof(info));

    info.int_free = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info.int_big  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info.ps_free  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    info.ps_big   = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    // FIX1: evitar reloj de 64 bits en app ELF externa.
    // El linker Xtensa nuevo puede atragantarse con helper de 64 bits en -shared.
    // Uptime aproximado por contador de frames/refrescos.
    info.uptime_s = approx_uptime_s;

    info.root_ok    = path_exists_dir("/root");
    info.rootbin_ok = path_exists_dir("/root/bin");
    info.sd_ok      = path_exists_dir("/sdcard");
    info.usb_ok     = path_exists_dir("/usb");

    info.root_items    = info.root_ok    ? count_dir_limited("/root", 999)     : -1;
    info.rootbin_items = info.rootbin_ok ? count_dir_limited("/root/bin", 999) : -1;
    info.sd_items      = info.sd_ok      ? count_dir_limited("/sdcard", 999)   : -1;
    info.usb_items     = info.usb_ok     ? count_dir_limited("/usb", 999)      : -1;

    info.bt_ok = bt_keyboard_connected() ? 1 : 0;
    info.touch_ok = minipc_touch_ok() ? 1 : 0;

    int mx = 0, my = 0;
    uint8_t mb = 0;
    usb_hid_mouse_get_state(&mx, &my, &mb);
    info.pointer_x = mx;
    info.pointer_y = my;
    info.pointer_buttons = mb;

    set_status("refrescado");
}

static void fmt_uptime(char *out, size_t out_sz, int sec)
{
    int h = sec / 3600;
    int m = (sec / 60) % 60;
    int s = sec % 60;

    snprintf(out, out_sz, "%02d:%02d:%02d", h, m, s);
}

static void draw_led(int x, int y, const char *label, int ok)
{
    rgb_gfx_rectfill(x, y, 8, 8, ok ? COL_GOOD : COL_RED);
    draw_text(x + 12, y + 1, label, ok ? COL_TEXT : COL_TEXT_DIM, 1);
}

static void draw_button(int x, int y, int w, int h, const char *txt, int hover)
{
    draw_box(x, y, w, h, hover ? COL_BLUE : COL_PANEL2, COL_WHITE);
    draw_text_center(x, y + 6, w, txt, COL_TEXT, 1);
}

static void draw_bar(int x, int y, int w, int h, uint32_t value, uint32_t max_hint)
{
    int fill = 0;

    // FIX1: sin calculos de 64 bits para evitar problemas de linker Xtensa.
    // Calculo en KB, suficiente para una barra visual.
    if (max_hint > 0 && w > 0) {
        uint32_t v_kb = value / 1024U;
        uint32_t m_kb = max_hint / 1024U;

        if (m_kb == 0) m_kb = 1;

        if (v_kb >= m_kb) {
            fill = w;
        } else {
            uint32_t step = m_kb / (uint32_t)w;
            if (step == 0) step = 1;
            fill = (int)(v_kb / step);
            if (fill > w) fill = w;
        }
    }

    draw_box(x, y, w, h, COL_PANEL, COL_BORDER);
    if (fill > 2) {
        rgb_gfx_rectfill(x + 1, y + 1, fill - 2, h - 2, COL_BAR);
    }
}

static void draw_metric(int x, int y, const char *label, uint32_t free_b, uint32_t big_b, uint32_t max_hint)
{
    char a[24];
    char b[24];
    char line[80];

    format_kb(a, sizeof(a), free_b);
    format_kb(b, sizeof(b), big_b);

    snprintf(line, sizeof(line), "%s libre %s", label, a);
    draw_text(x, y, line, COL_TEXT, 1);

    snprintf(line, sizeof(line), "bloque mayor %s", b);
    draw_text(x, y + 10, line, COL_TEXT_DIM, 1);

    draw_bar(x + 230, y + 1, 100, 8, free_b, max_hint);
}

static void draw_mount_line(int x, int y, const char *label, const char *path, int ok, int items)
{
    char line[96];

    draw_text(x, y, label, ok ? COL_TEXT : COL_TEXT_DIM, 1);
    draw_text(x + 88, y, ok ? "OK" : "NO", ok ? COL_GOOD : COL_RED, 1);
    draw_text(x + 172, y, path, ok ? COL_TEXT : COL_TEXT_DIM, 1);

    if (ok) {
        snprintf(line, sizeof(line), "%d", items < 0 ? 0 : items);
        draw_text(x + 292, y, line, COL_ACCENT, 1);
    } else {
        draw_text(x + 292, y, "-", COL_TEXT_DIM, 1);
    }
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

static int hit_refresh(int x, int y) { return inside_rect(x, y,  94, 211, 76, 18); }
static int hit_save   (int x, int y) { return inside_rect(x, y, 176, 211, 64, 18); }
static int hit_exit   (int x, int y) { return inside_rect(x, y, 306, 211, 60, 18); }

static int save_snapshot(void)
{
    FILE *f = fopen("/root/sysinfo.txt", "w");
    char up[24];
    char a[24];
    char b[24];

    if (!f) {
        set_status("no pude guardar /root/sysinfo.txt");
        return -1;
    }

    fmt_uptime(up, sizeof(up), info.uptime_s);

    fprintf(f, "Arielo MiniPC OS - SYSINFO 01A\n");
    fprintf(f, "uptime=%s\n", up);

    format_kb(a, sizeof(a), info.int_free);
    format_kb(b, sizeof(b), info.int_big);
    fprintf(f, "internal_free=%s internal_big=%s\n", a, b);

    format_kb(a, sizeof(a), info.ps_free);
    format_kb(b, sizeof(b), info.ps_big);
    fprintf(f, "psram_free=%s psram_big=%s\n", a, b);

    fprintf(f, "root=%s items=%d\n", info.root_ok ? "OK" : "NO", info.root_items);
    fprintf(f, "rootbin=%s items=%d\n", info.rootbin_ok ? "OK" : "NO", info.rootbin_items);
    fprintf(f, "sdcard=%s items=%d\n", info.sd_ok ? "OK" : "NO", info.sd_items);
    fprintf(f, "usb=%s items=%d\n", info.usb_ok ? "OK" : "NO", info.usb_items);
    fprintf(f, "bt_keyboard=%s touch=%s pointer=%d,%d btn=%u\n",
            info.bt_ok ? "OK" : "NO",
            info.touch_ok ? "OK" : "NO",
            info.pointer_x,
            info.pointer_y,
            (unsigned)info.pointer_buttons);

    fclose(f);
    set_status("guardado /root/sysinfo.txt");
    return 0;
}

static void draw_ui(int mx, int my)
{
    char line[96];
    char up[24];

    rgb_gfx_clear(COL_BG);

    // Cabecera
    draw_box(8, 6, 384, 24, COL_PANEL2, COL_BORDER);
    draw_text(18, 14, "ARIELO MINIPC OS", COL_ACCENT, 1);
    draw_text(296, 14, "SYS 01A2", COL_WHITE, 1);

    // Panel memoria: mas alto y con titulo propio para no pisar bordes.
    draw_box(14, 36, 372, 68, COL_PANEL, COL_BORDER);
    draw_text(24, 45, "MEMORIA", COL_ACCENT, 1);

    fmt_uptime(up, sizeof(up), info.uptime_s);
    snprintf(line, sizeof(line), "uptime %s", up);
    draw_text(242, 45, line, COL_TEXT_DIM, 1);

    draw_metric(24, 60, "INT  ", info.int_free, info.int_big, 370U * 1024U);
    draw_metric(24, 82, "PSRAM", info.ps_free,  info.ps_big,  8U * 1024U * 1024U);

    // Panel mounts: mas alto y con lineas separadas del borde inferior.
    draw_box(14, 110, 372, 68, COL_PANEL, COL_BORDER);
    draw_text(24, 120, "MOUNTS", COL_ACCENT, 1);
    draw_text(112, 120, "ESTADO", COL_TEXT_DIM, 1);
    draw_text(196, 120, "RUTA", COL_TEXT_DIM, 1);
    draw_text(312, 120, "ITEMS", COL_TEXT_DIM, 1);

    draw_mount_line(24, 134, "ROOT   ", "/root",     info.root_ok,    info.root_items);
    draw_mount_line(24, 145, "ROOTBIN", "/root/bin", info.rootbin_ok, info.rootbin_items);
    draw_mount_line(24, 156, "SDCARD ", "/sdcard",   info.sd_ok,      info.sd_items);
    draw_mount_line(24, 167, "USB    ", "/usb",      info.usb_ok,     info.usb_items);

    // Panel entrada/hardware.
    draw_box(14, 184, 372, 20, COL_PANEL, COL_BORDER);
    draw_led(24, 190, "BTKEY", info.bt_ok);
    draw_led(92, 190, "TOUCH", info.touch_ok);
    snprintf(line, sizeof(line), "PTR %03d,%03d B%u",
             info.pointer_x, info.pointer_y, (unsigned)info.pointer_buttons);
    draw_text(182, 191, line, COL_TEXT_DIM, 1);

    // Barra inferior.
    draw_box(14, 208, 372, 26, COL_PANEL, COL_BORDER);
    draw_text(24, 217, status_line, COL_TEXT_DIM, 1);
    draw_button(94, 211, 76, 18, "REFRESH", hit_refresh(mx, my));
    draw_button(176, 211, 64, 18, "SAVE", hit_save(mx, my));
    draw_button(306, 211, 60, 18, "EXIT", hit_exit(mx, my));

    if (last_pointer_x >= 0 && last_pointer_y >= 0) {
        rgb_gfx_rectfill(last_pointer_x, last_pointer_y, 5, 1, COL_WHITE);
        rgb_gfx_rectfill(last_pointer_x, last_pointer_y, 1, 5, COL_WHITE);
    }
}

static int pointer_action_edge(int mx, int my, uint8_t buttons)
{
    int press = (buttons & 0x01) && !(prev_buttons & 0x01);
    prev_buttons = buttons;

    if (!press) return 0;

    if (hit_refresh(mx, my)) return 'R';
    if (hit_save(mx, my))    return 'S';
    if (hit_exit(mx, my))    return 'Q';

    return 0;
}

int main(void)
{
    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("SYSINFO 01A: no pudo entrar en SM_400X240\n");
        return 1;
    }

    setup_palette();
    sync_start_keys();
    refresh_info();

    int mx = 0, my = 0;
    uint8_t buttons = 0;
    read_pointer(&mx, &my, &buttons);
    draw_ui(mx, my);

    int running = 1;
    int frame = 0;

    while (running) {
        int dirty = 0;

        read_pointer(&mx, &my, &buttons);

        int pact = pointer_action_edge(mx, my, buttons);
        if (pact == 'R') {
            refresh_info();
            dirty = 1;
        } else if (pact == 'S') {
            save_snapshot();
            dirty = 1;
        } else if (pact == 'Q') {
            running = 0;
            break;
        }

        if (key_edge(BT_KEY_R)) {
            refresh_info();
            dirty = 1;
        }

        if (key_edge(BT_KEY_S)) {
            save_snapshot();
            dirty = 1;
        }

        if (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q)) {
            running = 0;
            break;
        }

        // Refresco lento automatico para ver cambios de memoria y uptime.
        if ((frame % 50) == 0) {
            refresh_info();
            dirty = 1;
        }

        if (dirty || last_pointer_x != mx || last_pointer_y != my) {
            redraw_count++;
            draw_ui(mx, my);
        }

        rgb_display_wait_vsync();
        frame++;
        approx_frame_counter++;

        // Aproximacion conservadora: en SM_400X240 suele ir cerca de 50/60 Hz.
        // No necesitamos reloj exacto para diagnostico de memoria.
        if (approx_frame_counter >= 50) {
            approx_frame_counter = 0;
            approx_uptime_s++;
        }
    }

    rgb_display_set_mode(SM_TEXT);
    printf("SYSINFO 01A: salida limpia, redraws=%d int_free=%u int_big=%u ps_free=%u ps_big=%u\n",
           redraw_count,
           (unsigned)info.int_free,
           (unsigned)info.int_big,
           (unsigned)info.ps_free,
           (unsigned)info.ps_big);
    return 0;
}
