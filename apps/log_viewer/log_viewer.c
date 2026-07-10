/*
 * log_viewer.c - Visor de logs externo para Arielo MiniPC OS
 *
 * 02A_FIXED_USB_MOUSE_TOUCH:
 *   - App externa pura ELF Xtensa ESP32-S3.
 *   - Modo grafico SM_400X240.
 *   - Abre por defecto /usb/log.txt.
 *   - Botones tactiles/raton: USB, SD, reload, scroll, pagina, ERROR, ESC.
 *   - Teclado: flechas, PgUp/PgDn, Home/End, Left/Right, R, U, S, E, Q/ESC.
 *   - Solo lectura. No modifica el fichero.
 *
 * Notas:
 *   - No usa rgb_gfx_rect(), solo rgb_gfx_rectfill(), para evitar simbolos
 *     no exportados en el loader ELF.
 *   - Reutiliza las puertas ya abiertas para futuras apps:
 *       usb_hid_mouse_get_state, usb_hid_mouse_set_position,
 *       minipc_touch_ok, minipc_touch_read.
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
#define COL_HILITE    13

#define MAX_LINES     360
#define MAX_LINE_CH   96
#define VIEW_LINES    18
#define VIEW_CHARS    43

/* Teclas HID extra. */
#define BT_KEY_HOME       0x4A
#define BT_KEY_PAGEUP     0x4B
#define BT_KEY_DELETE     0x4C
#define BT_KEY_END        0x4D
#define BT_KEY_PAGEDOWN   0x4E

static uint8_t prev_keys[32];
static uint8_t prev_pointer_buttons;
static int last_pointer_x = -1;
static int last_pointer_y = -1;

static char lines[MAX_LINES][MAX_LINE_CH + 1];
static int  line_count;
static int  top_line;
static int  hscroll;
static int  file_truncated;
static char current_path[64] = "/usb/log.txt";
static char status_line[96] = "READY";
static int  redraw_count;

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
    pal[COL_HILITE]   = 0x001F;
    rgb_display_set_vga_palette(pal);
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

static void add_status_num(const char *prefix, int n)
{
    char tmp[96];
    sprintf(tmp, "%s%d", prefix, n);
    set_status(tmp);
}

static int append_line(void)
{
    if (line_count >= MAX_LINES) {
        file_truncated = 1;
        return 0;
    }
    lines[line_count][0] = 0;
    line_count++;
    return 1;
}

static void sanitize_and_add_char(char c, int *col)
{
    if (line_count <= 0) append_line();
    if (line_count <= 0) return;

    char *dst = lines[line_count - 1];
    if (c == '\r') return;
    if (c == '\n') {
        append_line();
        *col = 0;
        return;
    }
    if (c == '\t') c = ' ';
    if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '.';

    if (*col < MAX_LINE_CH) {
        dst[*col] = c;
        (*col)++;
        dst[*col] = 0;
    }
}

static void load_log_file(const char *path)
{
    FILE *f;
    char buf[384];
    size_t n;
    int col = 0;

    line_count = 0;
    top_line = 0;
    hscroll = 0;
    file_truncated = 0;
    memset(lines, 0, sizeof(lines));

    strncpy(current_path, path, sizeof(current_path) - 1);
    current_path[sizeof(current_path) - 1] = 0;

    f = fopen(current_path, "rb");
    if (!f) {
        append_line(); strncpy(lines[0], "NO SE PUDO ABRIR", MAX_LINE_CH);
        append_line(); strncpy(lines[1], current_path, MAX_LINE_CH);
        append_line(); strncpy(lines[2], "COPIE log.txt AL USB", MAX_LINE_CH);
        append_line(); strncpy(lines[3], "O PULSE SD PARA /sdcard/log.txt", MAX_LINE_CH);
        set_status("ERROR ABRIENDO ARCHIVO");
        return;
    }

    append_line();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            sanitize_and_add_char(buf[i], &col);
            if (file_truncated) break;
        }
        if (file_truncated) break;
    }
    fclose(f);

    if (line_count > 1 && lines[line_count - 1][0] == 0) {
        line_count--;
    }
    if (line_count <= 0) {
        append_line();
        strncpy(lines[0], "ARCHIVO VACIO", MAX_LINE_CH);
    }

    char st[96];
    sprintf(st, "%d LINEAS%s", line_count, file_truncated ? " - TRUNCADO" : "");
    set_status(st);
}

static int line_contains_ci(const char *s, const char *pat)
{
    if (!s || !pat || !*pat) return 0;
    for (int i = 0; s[i]; i++) {
        int ok = 1;
        for (int j = 0; pat[j]; j++) {
            char a = s[i + j];
            char b = pat[j];
            if (!a) { ok = 0; break; }
            if (a >= 'a' && a <= 'z') a = (char)(a - 32);
            if (b >= 'a' && b <= 'z') b = (char)(b - 32);
            if (a != b) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static int line_has_error_token(const char *s)
{
    return line_contains_ci(s, "ERROR") || line_contains_ci(s, "FAIL") || line_contains_ci(s, "ELF") || line_contains_ci(s, "PANIC");
}

static void clamp_scroll(void)
{
    int max_top = line_count - VIEW_LINES;
    if (max_top < 0) max_top = 0;
    if (top_line < 0) top_line = 0;
    if (top_line > max_top) top_line = max_top;
    if (hscroll < 0) hscroll = 0;
    if (hscroll > MAX_LINE_CH - 1) hscroll = MAX_LINE_CH - 1;
}

static void scroll_lines(int delta)
{
    top_line += delta;
    clamp_scroll();
}

static void page_lines(int delta)
{
    top_line += delta * (VIEW_LINES - 2);
    clamp_scroll();
}

static void goto_home(void)
{
    top_line = 0;
    clamp_scroll();
}

static void goto_end(void)
{
    top_line = line_count;
    clamp_scroll();
}

static void find_next_error(void)
{
    if (line_count <= 0) return;
    int start = top_line + 1;
    for (int pass = 0; pass < 2; pass++) {
        int from = pass ? 0 : start;
        int to   = pass ? start : line_count;
        for (int i = from; i < to; i++) {
            if (line_has_error_token(lines[i])) {
                top_line = i;
                clamp_scroll();
                add_status_num("MARCA EN LINEA ", i + 1);
                return;
            }
        }
    }
    set_status("NO HAY ERROR/FAIL/ELF/PANIC");
}

static void draw_button(int x, int y, int w, int h, const char *label, uint8_t fill, uint8_t color)
{
    draw_box(x, y, w, h, fill, COL_BORDER);
    draw_text_center(x, y + 4, w, label, color, 1);
}

static void draw_log_line(int row, int line_index)
{
    int y = 55 + row * 9;
    int x = 10;
    char nbuf[8];
    char out[VIEW_CHARS + 1];
    const char *src = "";
    uint8_t col = COL_TEXT;

    if (line_index < 0 || line_index >= line_count) return;
    src = lines[line_index];

    sprintf(nbuf, "%03d ", line_index + 1);
    draw_text(x, y, nbuf, COL_TEXT_DIM, 1);
    x += 26;

    if (line_has_error_token(src)) {
        rgb_gfx_rectfill(8, y - 1, 296, 9, COL_HILITE);
        draw_text(10, y, nbuf, COL_WARN, 1);
        col = COL_WARN;
    }

    int len = (int)strlen(src);
    int start = hscroll;
    if (start > len) start = len;
    int k = 0;
    for (int i = start; src[i] && k < VIEW_CHARS; i++) {
        out[k++] = src[i];
    }
    out[k] = 0;
    draw_text(x, y, out, col, 1);
}

static void draw_ui(void)
{
    char info[96];
    rgb_gfx_clear(COL_BG);

    draw_box(6, 4, 388, 24, COL_PANEL2, COL_BORDER);
    draw_text(16, 12, "ARIELO MINIPC OS", COL_ACCENT, 1);
    draw_text(286, 12, "LOG VIEW 02A", COL_WHITE, 1);

    draw_box(6, 31, 388, 17, COL_PANEL, COL_BORDER);
    draw_text(12, 36, current_path, COL_GOOD, 1);
    sprintf(info, "L %d/%d H%d", line_count ? top_line + 1 : 0, line_count, hscroll);
    draw_text(292, 36, info, COL_TEXT_DIM, 1);

    draw_box(6, 52, 300, 162, COL_PANEL, COL_BORDER);
    for (int i = 0; i < VIEW_LINES; i++) {
        draw_log_line(i, top_line + i);
    }

    int bx = 312;
    int by = 52;
    int bw = 82;
    int bh = 16;
    int gap = 2;
    draw_button(bx, by + 0 * (bh + gap), bw, bh, "RLD", COL_KEY2, COL_WHITE);
    draw_button(bx, by + 1 * (bh + gap), bw, bh, "USB", COL_KEY, COL_GOOD);
    draw_button(bx, by + 2 * (bh + gap), bw, bh, "SD",  COL_KEY, COL_GOOD);
    draw_button(bx, by + 3 * (bh + gap), bw, bh, "UP",  COL_KEY, COL_WHITE);
    draw_button(bx, by + 4 * (bh + gap), bw, bh, "DN",  COL_KEY, COL_WHITE);
    draw_button(bx, by + 5 * (bh + gap), bw, bh, "PG+", COL_KEY2, COL_WHITE);
    draw_button(bx, by + 6 * (bh + gap), bw, bh, "PG-", COL_KEY2, COL_WHITE);
    draw_button(bx, by + 7 * (bh + gap), bw, bh, "ERR", COL_KEY2, COL_WARN);
    draw_button(bx, by + 8 * (bh + gap), bw, bh, "ESC", COL_KEY, COL_WARN);

    draw_box(6, 217, 388, 18, COL_PANEL2, COL_BORDER);
    draw_text(12, 223, status_line, COL_TEXT, 1);
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

static char ui_action_at(int mx, int my)
{
    int bx = 312;
    int by = 52;
    int bw = 82;
    int bh = 16;
    int gap = 2;
    static const char actions[9] = { 'R', 'U', 'S', '^', 'v', 'P', 'p', 'E', 'Q' };
    for (int i = 0; i < 9; i++) {
        int y = by + i * (bh + gap);
        if (inside_rect(mx, my, bx, y, bw, bh)) return actions[i];
    }
    return 0;
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
    if (left_edge) return ui_action_at(mx, my);
    return 0;
}

static int apply_action(char a)
{
    switch (a) {
        case 'R': load_log_file(current_path); return 1;
        case 'U': load_log_file("/usb/log.txt"); return 1;
        case 'S': load_log_file("/sdcard/log.txt"); return 1;
        case '^': scroll_lines(-1); return 1;
        case 'v': scroll_lines(1); return 1;
        case 'P': page_lines(1); return 1;
        case 'p': page_lines(-1); return 1;
        case 'H': goto_home(); return 1;
        case 'Z': goto_end(); return 1;
        case 'E': find_next_error(); return 1;
        case '<': hscroll -= 4; clamp_scroll(); return 1;
        case '>': hscroll += 4; clamp_scroll(); return 1;
        default: return 0;
    }
}

static char read_keyboard_action_edge(void)
{
    if (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q)) return 'Q';
    if (key_edge(BT_KEY_R)) return 'R';
    if (key_edge(BT_KEY_U)) return 'U';
    if (key_edge(BT_KEY_S)) return 'S';
    if (key_edge(BT_KEY_E)) return 'E';
    if (key_edge(BT_KEY_UP)) return '^';
    if (key_edge(BT_KEY_DOWN)) return 'v';
    if (key_edge(BT_KEY_PAGEUP)) return 'p';
    if (key_edge(BT_KEY_PAGEDOWN)) return 'P';
    if (key_edge(BT_KEY_HOME)) return 'H';
    if (key_edge(BT_KEY_END)) return 'Z';
    if (key_edge(BT_KEY_LEFT)) return '<';
    if (key_edge(BT_KEY_RIGHT)) return '>';
    return 0;
}

int main(void)
{
    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("LogViewer: no pudo entrar en SM_400X240\n");
        return 1;
    }

    setup_palette();
    sync_start_keys();
    load_log_file("/usb/log.txt");
    draw_ui();

    int running = 1;
    while (running) {
        int dirty = 0;
        char a = read_pointer_action_edge();
        if (!a) a = read_keyboard_action_edge();
        if (a == 'Q') {
            running = 0;
            break;
        }
        if (a) {
            if (apply_action(a)) dirty = 1;
        }
        if (dirty) {
            redraw_count++;
            draw_ui();
        }
        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    printf("LogViewer 02A: salida limpia, redraws=%d\n", redraw_count);
    return 0;
}
