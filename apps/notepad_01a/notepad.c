/*
 * notepad.c - Bloc de notas externo para Arielo MiniPC OS
 *
 * NOTEPAD_01K_FONT_CAPSLOCK_FIX
 *   - Rehecho desde el chasis REAL de Calculator 01H.
 *   - Mismo patrón gráfico:
 *       rgb_display_set_mode(SM_400X240)
 *       setup_palette()
 *       rgb_gfx_clear()
 *       rgb_gfx_rectfill()
 *       rgb_display_wait_vsync()
 *   - Mismo patrón de teclado USB/BLE, ratón y touch GT911.
 *   - Sin framebuffer directo.
 *   - Sin direct framebuffer API.
 *   - Sin cambios raros de modo respecto a la calculadora.
 *
 * Controles:
 *   Teclado normal        : escribir
 *   ENTER                 : nueva línea / aceptar ruta
 *   BACKSPACE             : borrar
 *   Flechas               : mover cursor
 *   Ctrl+S                : guardar
 *   Ctrl+O                : abrir
 *   Ctrl+N                : nuevo
 *   Ctrl+P                : editar ruta / guardar como
 *   ESC / Q               : salir
 *
 * Botones:
 *   NEW OPEN SAVE AS
 *   ROOT SD USB EXIT
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

#define MAX_TEXT      16384
#define MAX_PATH      96
#define MAX_STATUS    64
#define MAX_FILES     64
#define MAX_NAME      64

#define MODE_EDIT     0
#define MODE_PICKER   1

/* Teclas USB HID no incluidas en bt_keyboard.h original. */
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

static char text_buf[MAX_TEXT];
static int text_len;
static int cursor_pos;
static int scroll_line;
static int dirty_doc;

static char path_buf[MAX_PATH] = "/root/nota.txt";
static int path_len = 14;
static int path_edit;
static int pending_save_after_path;
static int caps_lock;

static char status_line[MAX_STATUS];

static int file_open_current(void);

static int ui_mode = MODE_EDIT;
static char current_dir[MAX_PATH] = "/root";
static char file_names[MAX_FILES][MAX_NAME];
static int file_count;
static int file_sel;
static int file_scroll;

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

    /*
     * 01K:
     * Glifos minúsculos reales 5x7. La 01I/01J generaba minúsculas
     * desplazando mayúsculas y quedaban poco legibles.
     */
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
    }

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
        if (*s == ' ') w += 6 * scale;
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
        if (c == '\n') { y += 9 * scale; px0 = x; continue; }
        if (c == ' ') { px0 += 6 * scale; continue; }
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


static void draw_box(int x, int y, int w, int h, uint8_t fill, uint8_t border)
{
    /*
     * Copiado del criterio de Calculator 01H:
     * no usar rgb_gfx_rect(), solo rgb_gfx_rectfill() exportada.
     */
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

static void set_status(const char *s)
{
    strncpy(status_line, s ? s : "", sizeof(status_line) - 1);
    status_line[sizeof(status_line) - 1] = 0;
}


static int ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int str_ends_with_ci(const char *s, const char *ext)
{
    int ls = (int)strlen(s);
    int le = (int)strlen(ext);
    if (le <= 0 || ls < le) return 0;
    s += ls - le;

    for (int i = 0; i < le; i++) {
        if (ascii_tolower((unsigned char)s[i]) != ascii_tolower((unsigned char)ext[i])) {
            return 0;
        }
    }
    return 1;
}

static int is_text_compatible(const char *name)
{
    if (!name || !name[0]) return 0;
    if (name[0] == '.') return 0;

    if (str_ends_with_ci(name, ".txt")) return 1;
    if (str_ends_with_ci(name, ".log")) return 1;
    if (str_ends_with_ci(name, ".md")) return 1;
    if (str_ends_with_ci(name, ".ini")) return 1;
    if (str_ends_with_ci(name, ".cfg")) return 1;
    if (str_ends_with_ci(name, ".csv")) return 1;
    if (str_ends_with_ci(name, ".json")) return 1;
    if (str_ends_with_ci(name, ".xml")) return 1;
    if (str_ends_with_ci(name, ".c")) return 1;
    if (str_ends_with_ci(name, ".h")) return 1;
    if (str_ends_with_ci(name, ".cpp")) return 1;
    if (str_ends_with_ci(name, ".ino")) return 1;

    return 0;
}

static void sort_file_names(void)
{
    for (int i = 0; i < file_count; i++) {
        for (int j = i + 1; j < file_count; j++) {
            if (strcmp(file_names[j], file_names[i]) < 0) {
                char tmp[MAX_NAME];

                strncpy(tmp, file_names[i], sizeof(tmp) - 1);
                tmp[sizeof(tmp) - 1] = 0;

                strncpy(file_names[i], file_names[j], MAX_NAME - 1);
                file_names[i][MAX_NAME - 1] = 0;

                strncpy(file_names[j], tmp, MAX_NAME - 1);
                file_names[j][MAX_NAME - 1] = 0;
            }
        }
    }
}

static int picker_scan_current_dir(void)
{
    file_count = 0;
    file_sel = 0;
    file_scroll = 0;

    DIR *d = opendir(current_dir);
    if (!d) {
        ui_mode = MODE_EDIT;
        set_status("NO PUDE LISTAR CARPETA");
        return 0;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL && file_count < MAX_FILES) {
        const char *name = e->d_name;
        if (!is_text_compatible(name)) continue;

        strncpy(file_names[file_count], name, MAX_NAME - 1);
        file_names[file_count][MAX_NAME - 1] = 0;
        file_count++;
    }

    closedir(d);
    sort_file_names();

    ui_mode = MODE_PICKER;
    if (file_count <= 0) {
        set_status("SIN FICHEROS DE TEXTO");
    } else {
        set_status("ELIGE FICHERO Y ENTER/OPEN");
    }

    return file_count;
}

static void picker_open_selected(void)
{
    if (file_count <= 0 || file_sel < 0 || file_sel >= file_count) {
        set_status("NO HAY FICHERO SELECCIONADO");
        return;
    }

    snprintf(path_buf, sizeof(path_buf), "%s/%s", current_dir, file_names[file_sel]);
    path_buf[sizeof(path_buf) - 1] = 0;
    path_len = (int)strlen(path_buf);

    ui_mode = MODE_EDIT;
    path_edit = 0;
    pending_save_after_path = 0;
    file_open_current();
}

static void picker_move(int delta)
{
    if (file_count <= 0) return;

    file_sel += delta;
    if (file_sel < 0) file_sel = 0;
    if (file_sel >= file_count) file_sel = file_count - 1;

    if (file_sel < file_scroll) file_scroll = file_sel;
    if (file_sel >= file_scroll + 10) file_scroll = file_sel - 9;
    if (file_scroll < 0) file_scroll = 0;
}

static int line_of_pos(int pos)
{
    int line = 0;
    if (pos < 0) pos = 0;
    if (pos > text_len) pos = text_len;
    for (int i = 0; i < pos; i++) {
        if (text_buf[i] == '\n') line++;
    }
    return line;
}

static int pos_of_line(int line)
{
    int cur = 0;
    if (line <= 0) return 0;
    for (int i = 0; i < text_len; i++) {
        if (text_buf[i] == '\n') {
            cur++;
            if (cur == line) return i + 1;
        }
    }
    return text_len;
}

static int line_start(int pos)
{
    if (pos < 0) pos = 0;
    if (pos > text_len) pos = text_len;
    while (pos > 0 && text_buf[pos - 1] != '\n') pos--;
    return pos;
}

static int line_end(int pos)
{
    if (pos < 0) pos = 0;
    if (pos > text_len) pos = text_len;
    while (pos < text_len && text_buf[pos] != '\n') pos++;
    return pos;
}

static void ensure_cursor_visible(void)
{
    int line = line_of_pos(cursor_pos);
    if (line < scroll_line) scroll_line = line;
    if (line >= scroll_line + 12) scroll_line = line - 11;
    if (scroll_line < 0) scroll_line = 0;
}


static int path_has_filename(void)
{
    if (path_len <= 0 || path_buf[0] == 0) return 0;
    if (path_buf[path_len - 1] == '/') return 0;
    return 1;
}

static void begin_path_prompt_for_save(void)
{
    if (path_len <= 0 || path_buf[0] == 0 || path_buf[path_len - 1] == '/') {
        snprintf(path_buf, sizeof(path_buf), "%s/", current_dir);
        path_buf[sizeof(path_buf) - 1] = 0;
        path_len = (int)strlen(path_buf);
    }

    ui_mode = MODE_EDIT;
    path_edit = 1;
    pending_save_after_path = 1;
    set_status("PON NOMBRE Y ENTER PARA GUARDAR");
}

static void begin_path_prompt_as(void)
{
    if (path_len <= 0 || path_buf[0] == 0) {
        snprintf(path_buf, sizeof(path_buf), "%s/", current_dir);
        path_buf[sizeof(path_buf) - 1] = 0;
        path_len = (int)strlen(path_buf);
    }

    ui_mode = MODE_EDIT;
    path_edit = 1;
    pending_save_after_path = 0;
    set_status("GUARDAR COMO: EDITA RUTA");
}

static void save_request(void);

static void doc_new(void)
{
    text_len = 0;
    cursor_pos = 0;
    scroll_line = 0;
    dirty_doc = 0;
    path_edit = 0;
    pending_save_after_path = 0;

    text_buf[0] = 0;

    /*
     * 01J:
     * Documento nuevo = sin nombre.
     * No lo asociamos automáticamente a /root/nota.txt.
     */
    path_buf[0] = 0;
    path_len = 0;

    set_status("NUEVO SIN NOMBRE - SAVE");
}

static void set_default_path(const char *root)
{
    strncpy(current_dir, root, sizeof(current_dir) - 1);
    current_dir[sizeof(current_dir) - 1] = 0;

    snprintf(path_buf, sizeof(path_buf), "%s/", current_dir);
    path_buf[sizeof(path_buf) - 1] = 0;
    path_len = (int)strlen(path_buf);

    ui_mode = MODE_EDIT;
    path_edit = 1;
    pending_save_after_path = 0;
    set_status("UBICACION CAMBIADA - ESCRIBE NOMBRE");
}

static int file_open_current(void)
{
    pending_save_after_path = 0;

    if (!path_has_filename()) {
        set_status("SIN FICHERO SELECCIONADO");
        return 0;
    }

    FILE *f = fopen(path_buf, "rb");
    if (!f) {
        set_status("NO PUDE ABRIR");
        return 0;
    }

    size_t n = fread(text_buf, 1, MAX_TEXT - 1, f);
    fclose(f);

    text_len = (int)n;
    text_buf[text_len] = 0;
    cursor_pos = text_len;
    scroll_line = 0;
    dirty_doc = 0;

    if (n >= MAX_TEXT - 1) set_status("CARGADO TRUNCADO");
    else set_status("CARGADO OK");
    return 1;
}

static int file_save_current(void)
{
    if (!path_has_filename()) {
        begin_path_prompt_for_save();
        return 0;
    }

    FILE *f = fopen(path_buf, "wb");
    if (!f) {
        set_status("NO PUDE GUARDAR");
        return 0;
    }

    size_t wr = fwrite(text_buf, 1, (size_t)text_len, f);
    fflush(f);
    fclose(f);

    if (wr != (size_t)text_len) {
        set_status("GUARDADO INCOMPLETO");
        return 0;
    }

    dirty_doc = 0;
    pending_save_after_path = 0;
    set_status("GUARDADO OK");
    return 1;
}


static void save_request(void)
{
    if (!path_has_filename()) {
        begin_path_prompt_for_save();
        return;
    }

    path_edit = 0;
    pending_save_after_path = 0;
    file_save_current();
}

static void insert_char(char c)
{
    if (text_len >= MAX_TEXT - 1) {
        set_status("BUFFER LLENO 16K");
        return;
    }

    if (cursor_pos < 0) cursor_pos = 0;
    if (cursor_pos > text_len) cursor_pos = text_len;

    memmove(text_buf + cursor_pos + 1, text_buf + cursor_pos, (size_t)(text_len - cursor_pos + 1));
    text_buf[cursor_pos] = c;
    cursor_pos++;
    text_len++;
    dirty_doc = 1;
    ensure_cursor_visible();
}

static void backspace_char(void)
{
    if (path_edit) {
        if (path_len > 0) {
            path_len--;
            path_buf[path_len] = 0;
        }
        return;
    }

    if (cursor_pos <= 0 || text_len <= 0) return;
    memmove(text_buf + cursor_pos - 1, text_buf + cursor_pos, (size_t)(text_len - cursor_pos + 1));
    cursor_pos--;
    text_len--;
    dirty_doc = 1;
    ensure_cursor_visible();
}

static void move_left(void)
{
    if (cursor_pos > 0) cursor_pos--;
    ensure_cursor_visible();
}

static void move_right(void)
{
    if (cursor_pos < text_len) cursor_pos++;
    ensure_cursor_visible();
}

static void move_up(void)
{
    int line = line_of_pos(cursor_pos);
    if (line <= 0) return;

    int start = line_start(cursor_pos);
    int col = cursor_pos - start;
    int prev_start = pos_of_line(line - 1);
    int prev_end = line_end(prev_start);
    int prev_len = prev_end - prev_start;
    if (col > prev_len) col = prev_len;
    cursor_pos = prev_start + col;
    ensure_cursor_visible();
}

static void move_down(void)
{
    int line = line_of_pos(cursor_pos);
    int next_start = pos_of_line(line + 1);
    if (next_start >= text_len) return;

    int start = line_start(cursor_pos);
    int col = cursor_pos - start;
    int next_end = line_end(next_start);
    int next_len = next_end - next_start;
    if (col > next_len) col = next_len;
    cursor_pos = next_start + col;
    ensure_cursor_visible();
}

static char ascii_key_edge(void)
{
    int sh = shift_down();

    for (uint8_t k = BT_KEY_A; k <= BT_KEY_Z; k++) {
        if (key_edge(k)) {
            char c = (char)('a' + (k - BT_KEY_A));

            /*
             * 01K:
             * CapsLock interno fijo. Con CapsLock activo escribe mayúsculas.
             * Shift invierte temporalmente, como en un PC.
             */
            if ((caps_lock && !sh) || (!caps_lock && sh)) {
                c = (char)(c - 32);
            }

            return c;
        }
    }

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

static void path_insert_char(char c)
{
    if (path_len >= MAX_PATH - 1) return;
    if (c < 32 || c > 126) return;
    path_buf[path_len++] = c;
    path_buf[path_len] = 0;
}

static void apply_action(char a)
{
    switch (a) {
        case 'N': doc_new(); break;
        case 'O':
            if (ui_mode == MODE_PICKER) picker_open_selected();
            else picker_scan_current_dir();
            break;
        case 'S': ui_mode = MODE_EDIT; save_request(); break;
        case 'A': begin_path_prompt_as(); break;
        case 'R': set_default_path("/root"); break;
        case 'D': set_default_path("/sdcard"); break;
        case 'U': set_default_path("/usb"); break;
        default: break;
    }
}

static char button_action_at(int mx, int my)
{
    static const char action[2][4] = {
        {'N', 'O', 'S', 'A'},
        {'R', 'D', 'U', 'Q'}
    };

    const int x0 = 20;
    const int y0 = 188;
    const int bw = 62;
    const int bh = 20;
    const int gap = 4;

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 4; c++) {
            int x = x0 + c * (bw + gap);
            int y = y0 + r * (bh + gap);
            if (inside_rect(mx, my, x, y, bw, bh)) {
                return action[r][c];
            }
        }
    }

    if (inside_rect(mx, my, 20, 56, 360, 104)) {
        int row = (my - 60) / 8;
        if (row < 0) row = 0;

        if (ui_mode == MODE_PICKER) {
            int idx = file_scroll + row;
            if (idx >= 0 && idx < file_count) {
                file_sel = idx;
                return 'M';
            }
            return 0;
        }

        int col = (mx - 24) / 6;
        if (col < 0) col = 0;
        if (col > 58) col = 58;

        int line = scroll_line + row;
        int p = pos_of_line(line);
        int e = line_end(p);
        if (p + col > e) col = e - p;
        if (col < 0) col = 0;
        cursor_pos = p + col;
        ensure_cursor_visible();
        return 'M';
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
    if (left_edge) return button_action_at(mx, my);
    return 0;
}

static void draw_button(int x, int y, int w, int h, const char *label, uint8_t fill, uint8_t text)
{
    draw_box(x, y, w, h, fill, COL_BORDER);
    int scale = 1;
    int ty = y + (h - 7 * scale) / 2;
    draw_text_center(x, ty, w, label, text, scale);
}

static void draw_buttons(void)
{
    static const char *lbl[2][4] = {
        {"NEW", "OPEN", "SAVE", "AS"},
        {"ROOT", "SD", "USB", "EXIT"}
    };

    int x0 = 20;
    int y0 = 188;
    int bw = 62;
    int bh = 20;
    int gap = 4;

    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 4; c++) {
            uint8_t fill = COL_KEY;
            uint8_t txt = COL_WHITE;
            if (strcmp(lbl[r][c], "SAVE") == 0 || strcmp(lbl[r][c], "AS") == 0) fill = COL_KEY2;
            if (strcmp(lbl[r][c], "EXIT") == 0) txt = COL_WARN;
            if (path_edit && strcmp(lbl[r][c], "AS") == 0) txt = COL_WARN;
            draw_button(x0 + c * (bw + gap), y0 + r * (bh + gap), bw, bh, lbl[r][c], fill, txt);
        }
    }
}


static void draw_picker(void)
{
    draw_box(20, 56, 360, 104, COL_BG, COL_BORDER);

    char title[96];
    snprintf(title, sizeof(title), "OPEN: %s", current_dir);
    draw_text(28, 62, title, COL_ACCENT, 1);

    if (file_count <= 0) {
        draw_text(42, 92, "NO HAY FICHEROS COMPATIBLES", COL_WARN, 1);
        draw_text(42, 106, ".TXT .LOG .MD .INI .CFG .CSV", COL_TEXT_DIM, 1);
        return;
    }

    for (int row = 0; row < 10; row++) {
        int idx = file_scroll + row;
        if (idx >= file_count) break;

        int y = 78 + row * 8;
        if (idx == file_sel) {
            rgb_gfx_rectfill(22, y - 1, 356, 8, COL_PANEL);
            draw_text(28, y, ">", COL_WARN, 1);
            draw_text(40, y, file_names[idx], COL_WHITE, 1);
        } else {
            draw_text(40, y, file_names[idx], COL_TEXT, 1);
        }
    }

    char foot[80];
    snprintf(foot, sizeof(foot), "%d/%d ENTER/OPEN ABRE ESC VUELVE", file_sel + 1, file_count);
    draw_text(28, 150, foot, COL_TEXT_DIM, 1);
}

static void draw_editor(void)
{
    draw_box(20, 56, 360, 104, COL_BG, COL_BORDER);

    int cur_line = line_of_pos(cursor_pos);
    int cur_start = line_start(cursor_pos);
    int cur_col = cursor_pos - cur_start;

    for (int row = 0; row < 12; row++) {
        int line = scroll_line + row;
        int p = pos_of_line(line);
        if (p > text_len) break;

        int e = line_end(p);
        int len = e - p;
        if (len > 58) len = 58;

        char tmp[64];
        if (len > 0) memcpy(tmp, text_buf + p, (size_t)len);
        tmp[len] = 0;

        int y = 60 + row * 8;
        if (line == cur_line) {
            rgb_gfx_rectfill(22, y - 1, 356, 8, COL_PANEL);
        }
        draw_text(24, y, tmp, COL_TEXT, 1);
    }

    int row = cur_line - scroll_line;
    if (row >= 0 && row < 12) {
        if (cur_col > 58) cur_col = 58;
        rgb_gfx_rectfill(24 + cur_col * 6, 60 + row * 8, 2, 7, COL_WHITE);
    }
}

static void draw_ui(void)
{
    rgb_gfx_clear(COL_BG);

    draw_box(8, 6, 384, 24, COL_PANEL2, COL_BORDER);
    draw_text(18, 14, "ARIELO MINIPC OS", COL_ACCENT, 1);
    draw_text(284, 14, "NOTE 01K", COL_WHITE, 1);

    draw_box(20, 34, 360, 18, path_edit ? COL_PANEL2 : COL_PANEL, path_edit ? COL_WARN : COL_BORDER);
    draw_text(28, 40, path_edit ? "PATH>" : "FILE:", path_edit ? COL_WARN : COL_TEXT_DIM, 1);
    if (path_len <= 0 || path_buf[0] == 0) {
        draw_text(68, 40, "<sin nombre>", COL_TEXT_DIM, 1);
    } else {
        draw_text(68, 40, path_buf, path_edit ? COL_WARN : COL_TEXT, 1);
    }

    if (ui_mode == MODE_PICKER) draw_picker();
    else draw_editor();

    draw_box(20, 164, 360, 18, COL_PANEL2, COL_BORDER);
    draw_text(28, 170, status_line, dirty_doc ? COL_WARN : COL_TEXT, 1);
    if (caps_lock) draw_text(260, 170, "CAP", COL_WARN, 1);

    if (ui_mode == MODE_PICKER) draw_text(304, 170, "PICK", COL_ACCENT, 1);
    else if (dirty_doc) draw_text(315, 170, "MOD", COL_WARN, 1);
    else draw_text(323, 170, "OK", COL_GOOD, 1);

    draw_buttons();

    if (last_pointer_x >= 0 && last_pointer_y >= 0) {
        rgb_gfx_rectfill(last_pointer_x, last_pointer_y, 5, 1, COL_WHITE);
        rgb_gfx_rectfill(last_pointer_x, last_pointer_y, 1, 5, COL_WHITE);
    }
}

static int handle_keyboard(void)
{
    int dirty = 0;
    int ctrl = ctrl_down();

    if (ui_mode == MODE_PICKER) {
        if (key_edge(BT_KEY_UP))    { picker_move(-1); return 1; }
        if (key_edge(BT_KEY_DOWN))  { picker_move(1); return 1; }
        if (key_edge(BT_KEY_LEFT))  { picker_move(-10); return 1; }
        if (key_edge(BT_KEY_RIGHT)) { picker_move(10); return 1; }
        if (key_edge(BT_KEY_ENTER)) { picker_open_selected(); return 1; }
        if (key_edge(BT_KEY_BACKSPACE)) { ui_mode = MODE_EDIT; set_status("OPEN CANCELADO"); return 1; }
    }

    if (key_edge(BT_KEY_CAPSLOCK)) {
        caps_lock = !caps_lock;
        set_status(caps_lock ? "CAPSLOCK ON" : "capslock off");
        return 1;
    }

    if (ctrl) {
        if (key_edge(BT_KEY_S)) { apply_action('S'); return 1; }
        if (key_edge(BT_KEY_O)) { apply_action('O'); return 1; }
        if (key_edge(BT_KEY_N)) { apply_action('N'); return 1; }
        if (key_edge(BT_KEY_P)) { apply_action('A'); return 1; }
    }

    if (key_edge(BT_KEY_LEFT))  { move_left(); dirty = 1; }
    if (key_edge(BT_KEY_RIGHT)) { move_right(); dirty = 1; }
    if (key_edge(BT_KEY_UP))    { move_up(); dirty = 1; }
    if (key_edge(BT_KEY_DOWN))  { move_down(); dirty = 1; }

    if (key_edge(BT_KEY_BACKSPACE)) {
        backspace_char();
        dirty = 1;
    }

    if (key_edge(BT_KEY_ENTER)) {
        if (path_edit) {
            path_edit = 0;

            if (pending_save_after_path) {
                pending_save_after_path = 0;
                file_save_current();
            } else {
                set_status("RUTA ACEPTADA");
            }
        } else {
            insert_char('\n');
        }
        dirty = 1;
    }

    char c = ascii_key_edge();
    if (c) {
        if (path_edit) path_insert_char(c);
        else insert_char(c);
        dirty = 1;
    }

    return dirty;
}

int main(void)
{
    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("Notepad 01K: no pudo entrar en SM_400X240\n");
        return 1;
    }

    setup_palette();
    doc_new();
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
        if (paction && paction != 'M') {
            apply_action(paction);
            dirty = 1;
        } else if (paction == 'M') {
            dirty = 1;
        }

        if (!dirty && (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q))) {
            if (ui_mode == MODE_PICKER) {
                ui_mode = MODE_EDIT;
                set_status("OPEN CANCELADO");
                dirty = 1;
            } else {
                running = 0;
                break;
            }
        }

        if (!dirty) {
            dirty = handle_keyboard();
        }

        if (dirty) {
            redraw_count++;
            draw_ui();
        }

        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    printf("Notepad 01K: salida limpia, redraws=%d len=%d dirty=%d path=%s\n",
           redraw_count, text_len, dirty_doc, path_buf);
    return 0;
}
