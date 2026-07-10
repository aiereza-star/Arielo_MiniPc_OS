
/*
 * image_viewer.c - Visor de imagenes BMP externo para Arielo MiniPC OS
 *
 * 04C_FLICKERFIX_DIRTY_REDRAW
 *   - App externa pura ELF Xtensa ESP32-S3.
 *   - Modo grafico SM_400X240.
 *   - Entrada por teclado + touch GT911 + raton USB HID.
 *   - Abre BMP 8bpp indexado o 24bpp sin compresion.
 *   - Lista .BMP de /usb o /sdcard en una mini ventana LOAD.
 *   - Modo FIT y modo 1:1 con desplazamiento.
 *   - PREV/NEXT para recorrer imagenes en la carpeta activa.
 *   - 04C: redibujado solo cuando cambia algo para eliminar parpadeo.
 *
 * Filosofia:
 *   - No toca el SO base.
 *   - No usa rgb_gfx_rect(), solo rgb_gfx_rectfill().
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#include "rgb_gfx.h"
#include "rgb_display.h"
#include "bt_keyboard.h"
#include "usb_hid_keyboard_02d.h"
#include "minipc_touch_gt911.h"

#define SW 400
#define SH 240

#define VIEW_X 4
#define VIEW_Y 32
#define VIEW_W 392
#define VIEW_H 188
#define STATUS_Y 222

#define PICK_DLG_X 22
#define PICK_DLG_Y 44
#define PICK_DLG_W 356
#define PICK_DLG_H 160
#define PICK_ROW_X 34
#define PICK_ROW_Y 74
#define PICK_ROW_H 14
#define PICK_VISIBLE_ROWS 8
#define PICK_MAX_FILES 48
#define PICK_NAME_LEN 48

#define IMG_MAX_W 800
#define IMG_MAX_H 480

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
#define COL_CURSOR    16

#define IMG_FIRST     32

#define MODE_FIT      0
#define MODE_ORIG     1

typedef struct {
    int x, y, w, h;
    const char *label;
    uint8_t normal;
} Button;

enum {
    BTN_ESC,
    BTN_DEV,
    BTN_LOAD,
    BTN_PREV,
    BTN_NEXT,
    BTN_FIT,
    BTN_ORIG,
    BTN_COUNT
};

static Button buttons[BTN_COUNT] = {
    {  4, 4, 28, 22, "ESC",  COL_RED   },
    { 35, 4, 34, 22, "USB",  COL_KEY2  },
    { 72, 4, 40, 22, "LOAD", COL_KEY2  },
    {115, 4, 40, 22, "PREV", COL_KEY   },
    {158, 4, 40, 22, "NEXT", COL_KEY   },
    {201, 4, 34, 22, "FIT",  COL_KEY   },
    {238, 4, 34, 22, "1:1",  COL_KEY   },
};

static uint16_t vga_pal[256];
static uint8_t img_pixels[IMG_MAX_W * IMG_MAX_H];
static uint8_t prev_keys[32];
static uint8_t prev_pointer_buttons;
static char status_line[96] = "READY";
static char current_path[96] = "";
static char current_name[48] = "";
static int current_storage = 0; /* 0=/usb 1=/sdcard */
static int picker_mode = 0;
static int picker_need_redraw = 0;
static int screen_dirty = 1;
static int app_quit = 0;
static char bmp_files[PICK_MAX_FILES][PICK_NAME_LEN];
static int bmp_count = 0;
static int bmp_sel = 0;
static int bmp_scroll = 0;
static int img_ok = 0;
static int img_w = 0;
static int img_h = 0;
static int view_mode = MODE_FIT;
static int pan_x = 0;
static int pan_y = 0;
static int last_mx = SW / 2;
static int last_my = SH / 2;

#pragma pack(push,1)
typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t res1;
    uint16_t res2;
    uint32_t off_bits;
} BMPFileHeader;

typedef struct {
    uint32_t size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t size_image;
    int32_t  xppm;
    int32_t  yppm;
    uint32_t clr_used;
    uint32_t clr_important;
} BMPInfoHeader;
#pragma pack(pop)

static int key_down(uint8_t key) { return bt_keyboard_is_pressed(key) ? 1 : 0; }
static int key_edge(uint8_t key)
{
    uint8_t mask = (uint8_t)(1U << (key & 7));
    uint8_t *byte = &prev_keys[key >> 3];
    int now = key_down(key);
    int was = ((*byte) & mask) != 0;
    if (now) *byte |= mask; else *byte &= (uint8_t)~mask;
    return now && !was;
}

static void sync_start_keys(void)
{
    memset(prev_keys, 0, sizeof(prev_keys));
    for (int k = 0; k < 128; k++) {
        if (key_down((uint8_t)k)) prev_keys[k >> 3] |= (uint8_t)(1U << (k & 7));
    }
}

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
        case '(': { static const uint8_t g[5]={0x00,0x1C,0x22,0x41,0x00}; return g[col]; }
        case ')': { static const uint8_t g[5]={0x00,0x41,0x22,0x1C,0x00}; return g[col]; }
        default: return 0;
    }
}

static int text_width(const char *s, int scale)
{
    int w = 0;
    while (*s) {
        if (*s == ' ') w += 4 * scale; else w += 6 * scale;
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
                if (bits & (1U << row)) rgb_gfx_rectfill(px0 + col * scale, y + row * scale, scale, scale, color);
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

static uint8_t rgb332_index(uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t idx = (uint8_t)(((r >> 5) << 5) | ((g >> 5) << 2) | (b >> 6));
    return (uint8_t)(IMG_FIRST + idx);
}

static void setup_palette(void)
{
    memset(vga_pal, 0, sizeof(vga_pal));
    vga_pal[COL_BG]       = rgb565(0,0,0);
    vga_pal[COL_PANEL]    = rgb565(12,18,28);
    vga_pal[COL_PANEL2]   = rgb565(24,32,44);
    vga_pal[COL_BORDER]   = rgb565(90,120,140);
    vga_pal[COL_TEXT]     = rgb565(210,220,220);
    vga_pal[COL_TEXT_DIM] = rgb565(130,145,145);
    vga_pal[COL_WHITE]    = rgb565(255,255,255);
    vga_pal[COL_ACCENT]   = rgb565(0,220,255);
    vga_pal[COL_WARN]     = rgb565(255,220,0);
    vga_pal[COL_GOOD]     = rgb565(0,220,70);
    vga_pal[COL_KEY]      = rgb565(42,54,70);
    vga_pal[COL_KEY2]     = rgb565(55,65,86);
    vga_pal[COL_RED]      = rgb565(220,40,32);
    vga_pal[COL_HILITE]   = rgb565(120,90,220);
    vga_pal[COL_BLACK]    = rgb565(0,0,0);
    vga_pal[COL_CANVAS]   = rgb565(250,250,250);
    vga_pal[COL_CURSOR]   = rgb565(255,90,90);

    for (int r = 0; r < 8; r++) {
        for (int g = 0; g < 8; g++) {
            for (int b = 0; b < 4; b++) {
                int i = IMG_FIRST + (r << 5) + (g << 2) + b;
                uint8_t rr = (uint8_t)((r * 255) / 7);
                uint8_t gg = (uint8_t)((g * 255) / 7);
                uint8_t bb = (uint8_t)((b * 255) / 3);
                vga_pal[i] = rgb565(rr, gg, bb);
            }
        }
    }
    rgb_display_set_vga_palette(vga_pal);
}

static void set_status(const char *s)
{
    strncpy(status_line, s, sizeof(status_line)-1);
    status_line[sizeof(status_line)-1] = 0;
    screen_dirty = 1;
}

static int eq_icase(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
    if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
    return a == b;
}

static int ends_bmp(const char *s)
{
    size_t n = strlen(s);
    if (n < 4) return 0;
    return eq_icase(s[n-4], '.') && eq_icase(s[n-3], 'b') && eq_icase(s[n-2], 'm') && eq_icase(s[n-1], 'p');
}

static void root_path(char *out, int out_len)
{
    snprintf(out, out_len, "%s", current_storage == 0 ? "/usb" : "/sdcard");
}

static void make_path(char *out, int out_len, const char *name)
{
    snprintf(out, out_len, "%s/%s", current_storage == 0 ? "/usb" : "/sdcard", name);
}

static int cmp_names(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static void sort_file_list(void)
{
    for (int i = 0; i < bmp_count - 1; i++) {
        for (int j = i + 1; j < bmp_count; j++) {
            if (cmp_names(bmp_files[i], bmp_files[j]) > 0) {
                char tmp[PICK_NAME_LEN];
                memcpy(tmp, bmp_files[i], PICK_NAME_LEN);
                memcpy(bmp_files[i], bmp_files[j], PICK_NAME_LEN);
                memcpy(bmp_files[j], tmp, PICK_NAME_LEN);
            }
        }
    }
}

static void refresh_file_list(void)
{
    char root[32];
    root_path(root, sizeof(root));
    bmp_count = 0;
    bmp_sel = 0;
    bmp_scroll = 0;
    DIR *d = opendir(root);
    if (!d) {
        set_status("NO SE PUEDE ABRIR USB/SD");
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (bmp_count >= PICK_MAX_FILES) break;
        if (de->d_name[0] == '.') continue;
        if (!ends_bmp(de->d_name)) continue;
        strncpy(bmp_files[bmp_count], de->d_name, PICK_NAME_LEN - 1);
        bmp_files[bmp_count][PICK_NAME_LEN - 1] = 0;
        bmp_count++;
    }
    closedir(d);
    sort_file_list();
    if (bmp_count == 0) set_status("NO HAY BMP EN LA UBICACION ACTUAL");
}

static void sync_current_selection(void)
{
    bmp_sel = 0;
    for (int i = 0; i < bmp_count; i++) {
        if (strcmp(bmp_files[i], current_name) == 0) {
            bmp_sel = i;
            break;
        }
    }
    if (bmp_sel < bmp_scroll) bmp_scroll = bmp_sel;
    if (bmp_sel >= bmp_scroll + PICK_VISIBLE_ROWS) bmp_scroll = bmp_sel - PICK_VISIBLE_ROWS + 1;
    if (bmp_scroll < 0) bmp_scroll = 0;
}

static void draw_button(int id)
{
    Button *b = &buttons[id];
    uint8_t fill = b->normal;
    uint8_t txt = COL_WHITE;
    const char *label = b->label;
    if (id == BTN_DEV) {
        label = current_storage == 0 ? "USB" : "SD";
        fill = current_storage == 0 ? COL_KEY2 : COL_ACCENT;
        txt = current_storage == 0 ? COL_WHITE : COL_BLACK;
    }
    if (id == BTN_FIT && view_mode == MODE_FIT) { fill = COL_GOOD; txt = COL_BLACK; }
    if (id == BTN_ORIG && view_mode == MODE_ORIG) { fill = COL_GOOD; txt = COL_BLACK; }
    rgb_gfx_rectfill(b->x, b->y, b->w, b->h, fill);
    draw_border(b->x, b->y, b->w, b->h, COL_BORDER);
    draw_text_center(b->x, b->y + 7, b->w, label, txt, 1);
}

static void draw_toolbar(void)
{
    rgb_gfx_rectfill(0, 0, SW, 30, COL_PANEL);
    for (int i = 0; i < BTN_COUNT; i++) draw_button(i);
}

static void draw_status_bar(void)
{
    rgb_gfx_rectfill(0, STATUS_Y, SW, SH - STATUS_Y, COL_PANEL);
    draw_border(0, STATUS_Y, SW, SH - STATUS_Y, COL_BORDER);
    draw_text(6, STATUS_Y + 6, status_line, COL_TEXT, 1);
}

static void clear_viewport(void)
{
    rgb_gfx_rectfill(VIEW_X, VIEW_Y, VIEW_W, VIEW_H, COL_PANEL2);
    draw_border(VIEW_X - 1, VIEW_Y - 1, VIEW_W + 2, VIEW_H + 2, COL_BORDER);
}

static void format_status(void)
{
    char tmp[96];
    if (img_ok) {
        snprintf(tmp, sizeof(tmp), "%s %dx%d  %s  %s", current_name, img_w, img_h,
                view_mode == MODE_FIT ? "FIT" : "1:1",
                current_storage == 0 ? "USB" : "SD");
    } else {
        snprintf(tmp, sizeof(tmp), "LOAD PARA ABRIR BMP  (%s)", current_storage == 0 ? "USB" : "SD");
    }
    set_status(tmp);
}

static void draw_empty_message(const char *title, const char *msg)
{
    draw_text_center(VIEW_X, VIEW_Y + 70, VIEW_W, title, COL_WHITE, 2);
    draw_text_center(VIEW_X, VIEW_Y + 94, VIEW_W, msg, COL_TEXT_DIM, 1);
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void render_image_fit(void)
{
    int dw = VIEW_W;
    int dh = VIEW_H;
    int rw = (img_w * dh) / img_h;
    int rh = (img_h * dw) / img_w;
    if (rw <= dw) { dw = rw; }
    else { dh = rh; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int ox = VIEW_X + (VIEW_W - dw) / 2;
    int oy = VIEW_Y + (VIEW_H - dh) / 2;
    for (int y = 0; y < dh; y++) {
        int sy = (y * img_h) / dh;
        int src_off = sy * img_w;
        for (int x = 0; x < dw; x++) {
            int sx = (x * img_w) / dw;
            rgb_gfx_pixel(ox + x, oy + y, img_pixels[src_off + sx]);
        }
    }
}

static void render_image_orig(void)
{
    int max_pan_x = img_w > VIEW_W ? (img_w - VIEW_W) : 0;
    int max_pan_y = img_h > VIEW_H ? (img_h - VIEW_H) : 0;
    pan_x = clampi(pan_x, 0, max_pan_x);
    pan_y = clampi(pan_y, 0, max_pan_y);
    int copy_w = img_w - pan_x; if (copy_w > VIEW_W) copy_w = VIEW_W;
    int copy_h = img_h - pan_y; if (copy_h > VIEW_H) copy_h = VIEW_H;
    int ox = VIEW_X + (VIEW_W - copy_w) / 2;
    int oy = VIEW_Y + (VIEW_H - copy_h) / 2;
    for (int y = 0; y < copy_h; y++) {
        int src_off = (pan_y + y) * img_w + pan_x;
        for (int x = 0; x < copy_w; x++) rgb_gfx_pixel(ox + x, oy + y, img_pixels[src_off + x]);
    }
}

static void draw_viewer(void)
{
    draw_toolbar();
    clear_viewport();
    if (!img_ok) draw_empty_message("IMAGE VIEWER", "SIN IMAGEN - PULSE LOAD");
    else if (view_mode == MODE_FIT) render_image_fit();
    else render_image_orig();
    draw_status_bar();
}

static void draw_picker(void)
{
    rgb_gfx_rectfill(PICK_DLG_X, PICK_DLG_Y, PICK_DLG_W, PICK_DLG_H, COL_PANEL);
    draw_border(PICK_DLG_X, PICK_DLG_Y, PICK_DLG_W, PICK_DLG_H, COL_BORDER);
    draw_text(PICK_DLG_X + 8, PICK_DLG_Y + 8, "LOAD BMP", COL_WHITE, 1);
    draw_text(PICK_DLG_X + 94, PICK_DLG_Y + 8, current_storage == 0 ? "USB" : "SD", COL_ACCENT, 1);

    rgb_gfx_rectfill(PICK_DLG_X + 8, PICK_DLG_Y + 24, PICK_DLG_W - 16, 1, COL_BORDER);

    for (int i = 0; i < PICK_VISIBLE_ROWS; i++) {
        int idx = bmp_scroll + i;
        int y = PICK_ROW_Y + i * PICK_ROW_H;
        uint8_t fill = (idx == bmp_sel) ? COL_HILITE : COL_PANEL2;
        rgb_gfx_rectfill(PICK_ROW_X, y, 270, 12, fill);
        draw_border(PICK_ROW_X, y, 270, 12, COL_BORDER);
        if (idx < bmp_count) draw_text(PICK_ROW_X + 4, y + 2, bmp_files[idx], idx == bmp_sel ? COL_WHITE : COL_TEXT, 1);
    }

    /* Botones del dialogo. */
    rgb_gfx_rectfill(312, 76, 54, 18, COL_KEY);   draw_border(312, 76, 54, 18, COL_BORDER);   draw_text_center(312, 82, 54, "UP", COL_WHITE, 1);
    rgb_gfx_rectfill(312, 98, 54, 18, COL_KEY);   draw_border(312, 98, 54, 18, COL_BORDER);   draw_text_center(312, 104, 54, "DN", COL_WHITE, 1);
    rgb_gfx_rectfill(312, 142, 54, 18, COL_GOOD); draw_border(312, 142, 54, 18, COL_BORDER);  draw_text_center(312, 148, 54, "OK", COL_BLACK, 1);
    rgb_gfx_rectfill(312, 164, 54, 18, COL_RED);  draw_border(312, 164, 54, 18, COL_BORDER);  draw_text_center(312, 170, 54, "CANCEL", COL_WHITE, 1);

    if (bmp_count == 0) draw_text(PICK_ROW_X + 10, PICK_ROW_Y + 30, "NO HAY ARCHIVOS BMP", COL_WARN, 1);
}

/* 04C: no dibujamos puntero propio ni refrescamos toda la pantalla en cada frame.
   El cursor del sistema/desktop sigue visible y evitamos parpadeos. */
static void full_redraw(void)
{
    /* 04C: no hacemos clear global. Cada zona limpia su propio fondo.
       Asi evitamos la barra negra y los pantallazos durante el repintado. */
    draw_viewer();
    if (picker_mode || picker_need_redraw) draw_picker();
    picker_need_redraw = 0;
    screen_dirty = 0;
}

static int hit_button(Button *b, int mx, int my)
{
    return mx >= b->x && my >= b->y && mx < b->x + b->w && my < b->y + b->h;
}

static void open_picker(void)
{
    picker_mode = 1;
    refresh_file_list();
    sync_current_selection();
    picker_need_redraw = 1;
    screen_dirty = 1;
}

static void close_picker(void)
{
    picker_mode = 0;
    picker_need_redraw = 1;
    screen_dirty = 1;
}

static void copy_basename_noext(char *dst, int dst_len, const char *src)
{
    int n = (int)strlen(src);
    if (n >= 4 && src[n-4] == '.') n -= 4;
    if (n >= dst_len) n = dst_len - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int load_bmp_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { set_status("NO SE PUDO ABRIR BMP"); return 0; }
    BMPFileHeader fh;
    BMPInfoHeader ih;
    if (fread(&fh, sizeof(fh), 1, f) != 1 || fread(&ih, sizeof(ih), 1, f) != 1) {
        fclose(f); set_status("CABECERA BMP INVALIDA"); return 0;
    }
    if (fh.type != 0x4D42) { fclose(f); set_status("NO ES BMP"); return 0; }
    if (ih.planes != 1 || (ih.bit_count != 8 && ih.bit_count != 24) || ih.compression != 0) {
        fclose(f); set_status("BMP SOPORTADO: 8/24BPP SIN COMPRESION"); return 0;
    }
    int w = ih.width;
    int h = ih.height < 0 ? -ih.height : ih.height;
    int top_down = ih.height < 0;
    if (w <= 0 || h <= 0 || w > IMG_MAX_W || h > IMG_MAX_H) {
        fclose(f); set_status("BMP FUERA DE LIMITE (MAX 800x480)"); return 0;
    }

    uint8_t pal8[256];
    memset(pal8, IMG_FIRST, sizeof(pal8));
    if (ih.bit_count == 8) {
        uint32_t colors = ih.clr_used ? ih.clr_used : 256U;
        if (colors > 256U) colors = 256U;
        for (uint32_t i = 0; i < colors; i++) {
            uint8_t bgrx[4];
            if (fread(bgrx, 1, 4, f) != 4) { fclose(f); set_status("PALETA BMP INVALIDA"); return 0; }
            pal8[i] = rgb332_index(bgrx[2], bgrx[1], bgrx[0]);
        }
    }

    if (fseek(f, (long)fh.off_bits, SEEK_SET) != 0) { fclose(f); set_status("OFFSET BMP INVALIDO"); return 0; }
    int row_size = ((ih.bit_count * w + 31) / 32) * 4;
    uint8_t rowbuf[2400 + 16];
    if (row_size > (int)sizeof(rowbuf)) { fclose(f); set_status("FILA BMP DEMASIADO ANCHA"); return 0; }

    for (int y = 0; y < h; y++) {
        int dy = top_down ? y : (h - 1 - y);
        if (fread(rowbuf, 1, row_size, f) != (size_t)row_size) { fclose(f); set_status("DATOS BMP INCOMPLETOS"); return 0; }
        uint8_t *dst = &img_pixels[dy * w];
        if (ih.bit_count == 8) {
            for (int x = 0; x < w; x++) dst[x] = pal8[rowbuf[x]];
        } else {
            for (int x = 0; x < w; x++) {
                uint8_t b = rowbuf[x * 3 + 0];
                uint8_t g = rowbuf[x * 3 + 1];
                uint8_t r = rowbuf[x * 3 + 2];
                dst[x] = rgb332_index(r, g, b);
            }
        }
    }

    fclose(f);
    img_w = w;
    img_h = h;
    img_ok = 1;
    view_mode = MODE_FIT;
    pan_x = 0;
    pan_y = 0;
    format_status();
    return 1;
}

static int load_selected(void)
{
    if (bmp_count <= 0 || bmp_sel < 0 || bmp_sel >= bmp_count) {
        set_status("NO HAY BMP PARA CARGAR");
        return 0;
    }
    make_path(current_path, sizeof(current_path), bmp_files[bmp_sel]);
    strncpy(current_name, bmp_files[bmp_sel], sizeof(current_name) - 1);
    current_name[sizeof(current_name) - 1] = 0;
    if (load_bmp_file(current_path)) {
        close_picker();
        return 1;
    }
    return 0;
}

static void prev_next_image(int dir)
{
    refresh_file_list();
    if (bmp_count <= 0) return;
    sync_current_selection();
    bmp_sel += dir;
    if (bmp_sel < 0) bmp_sel = bmp_count - 1;
    if (bmp_sel >= bmp_count) bmp_sel = 0;
    load_selected();
}

static void handle_picker_click(int mx, int my)
{
    for (int i = 0; i < PICK_VISIBLE_ROWS; i++) {
        int idx = bmp_scroll + i;
        int y = PICK_ROW_Y + i * PICK_ROW_H;
        if (mx >= PICK_ROW_X && mx < PICK_ROW_X + 270 && my >= y && my < y + 12) {
            if (idx < bmp_count) { bmp_sel = idx; load_selected(); }
            return;
        }
    }
    if (mx >= 312 && mx < 366 && my >= 76 && my < 94) {
        if (bmp_scroll > 0) bmp_scroll--; if (bmp_sel > 0) bmp_sel--; picker_need_redraw = 1; screen_dirty = 1; return;
    }
    if (mx >= 312 && mx < 366 && my >= 98 && my < 116) {
        if (bmp_scroll + PICK_VISIBLE_ROWS < bmp_count) bmp_scroll++; if (bmp_sel + 1 < bmp_count) bmp_sel++; picker_need_redraw = 1; screen_dirty = 1; return;
    }
    if (mx >= 312 && mx < 366 && my >= 142 && my < 160) { load_selected(); return; }
    if (mx >= 312 && mx < 366 && my >= 164 && my < 182) { close_picker(); return; }
}

static void handle_main_click(int mx, int my)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        if (!hit_button(&buttons[i], mx, my)) continue;
        switch (i) {
            case BTN_ESC: app_quit = 1; break;
            case BTN_DEV: current_storage ^= 1; format_status(); break;
            case BTN_LOAD: open_picker(); break;
            case BTN_PREV: prev_next_image(-1); break;
            case BTN_NEXT: prev_next_image(+1); break;
            case BTN_FIT: view_mode = MODE_FIT; format_status(); break;
            case BTN_ORIG: view_mode = MODE_ORIG; format_status(); break;
        }
        screen_dirty = 1;
        return;
    }
}

int app_main(void)
{
    rgb_display_set_mode(SM_400X240);
    setup_palette();
    sync_start_keys();
    format_status();
    full_redraw();

    int running = 1;
    while (running) {
        int mx = last_mx, my = last_my;
        uint8_t buttons_now = 0;
        usb_hid_mouse_get_state(&mx, &my, &buttons_now);
        if (minipc_touch_ok()) {
            int16_t tx, ty;
            if (minipc_touch_read(&tx, &ty)) {
                mx = tx / 2;
                my = ty / 2;
                buttons_now |= 0x01;
                usb_hid_mouse_set_position(mx, my);
            }
        }
        mx = clampi(mx, 0, SW - 1);
        my = clampi(my, 0, SH - 1);
        last_mx = mx; last_my = my;

        int left_pressed = ((buttons_now & 0x01) != 0) && ((prev_pointer_buttons & 0x01) == 0);
        int right_pressed = ((buttons_now & 0x02) != 0) && ((prev_pointer_buttons & 0x02) == 0);
        prev_pointer_buttons = buttons_now;

        if (key_edge(BT_KEY_Q) || key_edge(BT_KEY_ESC) || right_pressed) running = 0;

        if (picker_mode) {
            if (key_edge(BT_KEY_UP)) { if (bmp_sel > 0) bmp_sel--; if (bmp_sel < bmp_scroll) bmp_scroll = bmp_sel; picker_need_redraw = 1; screen_dirty = 1; }
            if (key_edge(BT_KEY_DOWN)) { if (bmp_sel + 1 < bmp_count) bmp_sel++; if (bmp_sel >= bmp_scroll + PICK_VISIBLE_ROWS) bmp_scroll = bmp_sel - PICK_VISIBLE_ROWS + 1; picker_need_redraw = 1; screen_dirty = 1; }
            if (key_edge(BT_KEY_ENTER)) { load_selected(); }
            if (key_edge(BT_KEY_BACKSPACE)) { close_picker(); }
            if (left_pressed) handle_picker_click(mx, my);
        } else {
            if (key_edge(BT_KEY_L)) open_picker();
            if (key_edge(BT_KEY_U)) { current_storage = 0; format_status(); }
            if (key_edge(BT_KEY_D)) { current_storage = 1; format_status(); }
            if (key_edge(BT_KEY_F)) { view_mode = MODE_FIT; format_status(); }
            if (key_edge(BT_KEY_O)) { view_mode = MODE_ORIG; format_status(); }
            if (key_edge(BT_KEY_P)) prev_next_image(-1);
            if (key_edge(BT_KEY_N)) prev_next_image(+1);
            if (key_edge(BT_KEY_LEFT))  { if (view_mode == MODE_ORIG) { pan_x -= 16; screen_dirty = 1; } }
            if (key_edge(BT_KEY_RIGHT)) { if (view_mode == MODE_ORIG) { pan_x += 16; screen_dirty = 1; } }
            if (key_edge(BT_KEY_UP))    { if (view_mode == MODE_ORIG) { pan_y -= 12; screen_dirty = 1; } }
            if (key_edge(BT_KEY_DOWN))  { if (view_mode == MODE_ORIG) { pan_y += 12; screen_dirty = 1; } }
            if (left_pressed) handle_main_click(mx, my);
        }

        if (app_quit) running = 0;
        if (screen_dirty || picker_need_redraw) {
            full_redraw();
        }
        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    return 0;
}
