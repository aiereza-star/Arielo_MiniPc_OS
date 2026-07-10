/*
 * minipc_bootgfx_03c_logo_pro.c
 *
 * Arielo MiniPC OS - splash grafico por celdas con logo PRO.
 *
 * Base tecnica:
 *   - Misma arquitectura validada en 03B.
 *   - No usa printf/stdout durante splash.
 *   - No usa VTerm.
 *   - No hace scroll.
 *   - Dibuja en s_boot_cells y rgb_display_set_buffer().
 *
 * Cuando termina el splash, main llama my_console_init() y el VTerm
 * recupera el display con su buffer normal.
 */

#include "minipc_bootgfx_03c_logo_pro.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rgb_display.h"

#define CELL_COUNT (DISPLAY_COLS * DISPLAY_ROWS)

#define MAX_ITEMS  8
#define NAME_LEN   20
#define STAT_LEN   32

/*
 * Atributos tipo VGA/CGA:
 * attr = (background << 4) | foreground
 *
 * Se usan valores prudentes porque ya estaban funcionando en 03B.
 */
#define A_BG         0x10
#define A_FRAME      0x1B
#define A_FRAME2     0x18
#define A_TITLE      0x1F
#define A_CYAN       0x1B
#define A_TEXT       0x17
#define A_DIM        0x18
#define A_OK         0x1A
#define A_WAIT       0x1E
#define A_FAIL       0x1C
#define A_CHIP       0x1F
#define A_CHIP_DARK  0x18
#define A_TRACE      0x1B
#define A_BAR_OFF    0x18
#define A_BAR_ON     0x2F
#define A_BADGE      0x1E

typedef struct {
    char name[NAME_LEN];
    char status[STAT_LEN];
} boot_item_t;

static lcd_cell_t s_boot_cells[CELL_COUNT];
static boot_item_t s_items[MAX_ITEMS];
static int s_count = 0;
static char s_message[64] = "Starting system...";
static int s_ready = 0;

static void boot_delay(void)
{
    vTaskDelay(pdMS_TO_TICKS(120));
}

static void safe_copy(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_len, "%s", src);
}

static void cell_clear(uint8_t attr)
{
    for (int i = 0; i < CELL_COUNT; i++) {
        s_boot_cells[i].ch = ' ';
        s_boot_cells[i].attr = attr;
    }
}

static void putc_xy(int x, int y, uint8_t attr, char ch)
{
    if (x < 0 || y < 0 || x >= DISPLAY_COLS || y >= DISPLAY_ROWS) return;
    s_boot_cells[y * DISPLAY_COLS + x].ch = ch;
    s_boot_cells[y * DISPLAY_COLS + x].attr = attr;
}

static void puts_xy_clip(int x, int y, uint8_t attr, const char *s, int max_chars)
{
    if (!s || y < 0 || y >= DISPLAY_ROWS || max_chars <= 0) return;

    int n = 0;
    while (*s && x < DISPLAY_COLS && n < max_chars) {
        if (x >= 0) putc_xy(x, y, attr, *s);
        x++;
        s++;
        n++;
    }
}

static void puts_xy(int x, int y, uint8_t attr, const char *s)
{
    puts_xy_clip(x, y, attr, s, DISPLAY_COLS);
}

static void puts_center(int y, uint8_t attr, const char *s)
{
    if (!s) return;
    int len = (int)strlen(s);
    int x = (DISPLAY_COLS - len) / 2;
    if (x < 0) x = 0;
    puts_xy(x, y, attr, s);
}

static void hline(int x, int y, int w, uint8_t attr, char ch)
{
    for (int i = 0; i < w; i++) putc_xy(x + i, y, attr, ch);
}

static void vline(int x, int y, int h, uint8_t attr, char ch)
{
    for (int i = 0; i < h; i++) putc_xy(x, y + i, attr, ch);
}

static void box(int x, int y, int w, int h, uint8_t attr)
{
    if (w < 2 || h < 2) return;

    putc_xy(x, y, attr, '+');
    putc_xy(x + w - 1, y, attr, '+');
    putc_xy(x, y + h - 1, attr, '+');
    putc_xy(x + w - 1, y + h - 1, attr, '+');

    hline(x + 1, y, w - 2, attr, '-');
    hline(x + 1, y + h - 1, w - 2, attr, '-');
    vline(x, y + 1, h - 2, attr, '|');
    vline(x + w - 1, y + 1, h - 2, attr, '|');
}

static int find_item(const char *name)
{
    if (!name) return -1;

    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_items[i].name, name) == 0) return i;
    }

    if (s_count >= MAX_ITEMS) return -1;

    safe_copy(s_items[s_count].name, sizeof(s_items[s_count].name), name);
    safe_copy(s_items[s_count].status, sizeof(s_items[s_count].status), "...");
    return s_count++;
}

static uint8_t attr_for_status(const char *status)
{
    if (!status) return A_TEXT;
    if (strncmp(status, "OK", 2) == 0) return A_OK;
    if (strncmp(status, "FAIL", 4) == 0) return A_FAIL;
    if (strncmp(status, "starting", 8) == 0) return A_WAIT;
    if (strncmp(status, "connecting", 10) == 0) return A_WAIT;
    if (strncmp(status, "background", 10) == 0) return A_WAIT;
    return A_CYAN;
}

static void draw_corner_marks(void)
{
    // Detalles tipo HUD
    puts_xy(4, 2, A_FRAME2,  "+----");
    puts_xy(DISPLAY_COLS - 9, 2, A_FRAME2, "----+");
    puts_xy(4, DISPLAY_ROWS - 3, A_FRAME2,  "+----");
    puts_xy(DISPLAY_COLS - 9, DISPLAY_ROWS - 3, A_FRAME2, "----+");

    putc_xy(4, 3, A_FRAME2, '|');
    putc_xy(DISPLAY_COLS - 5, 3, A_FRAME2, '|');
    putc_xy(4, DISPLAY_ROWS - 4, A_FRAME2, '|');
    putc_xy(DISPLAY_COLS - 5, DISPLAY_ROWS - 4, A_FRAME2, '|');
}

static void draw_logo_chip(void)
{
    /*
     * Logo central:
     * chip + trazas + "A" / "OS".
     * Todo en ASCII seguro.
     */
    const int x = 36;
    const int y = 4;
    const int w = 28;
    const int h = 9;

    // Trazas izquierda/derecha
    hline(x - 13, y + 2, 12, A_TRACE, '-');
    hline(x - 13, y + 5, 12, A_TRACE, '-');
    hline(x + w + 1, y + 2, 12, A_TRACE, '-');
    hline(x + w + 1, y + 5, 12, A_TRACE, '-');

    putc_xy(x - 14, y + 2, A_TRACE, '<');
    putc_xy(x - 14, y + 5, A_TRACE, '<');
    putc_xy(x + w + 13, y + 2, A_TRACE, '>');
    putc_xy(x + w + 13, y + 5, A_TRACE, '>');

    // Pines chip
    for (int i = 1; i < h - 1; i += 2) {
        puts_xy(x - 3, y + i, A_CHIP_DARK, "---");
        puts_xy(x + w, y + i, A_CHIP_DARK, "---");
    }

    // Cuerpo chip
    box(x, y, w, h, A_CHIP);

    // Relleno interior sutil
    for (int yy = y + 1; yy < y + h - 1; yy++) {
        for (int xx = x + 1; xx < x + w - 1; xx++) {
            putc_xy(xx, yy, A_CHIP_DARK, ' ');
        }
    }

    // Mini icono "A" estilo consola
    puts_xy(x + 5,  y + 2, A_TITLE, "  /\\  ");
    puts_xy(x + 5,  y + 3, A_TITLE, " /--\\ ");
    puts_xy(x + 5,  y + 4, A_TITLE, "/    \\");

    // Badge OS
    box(x + 16, y + 2, 8, 4, A_BADGE);
    puts_xy(x + 18, y + 3, A_BADGE, "OS");
    puts_xy(x + 17, y + 4, A_CYAN, "S3");

    // Linea inferior de chip
    hline(x + 4, y + 7, w - 8, A_TRACE, '=');
}

static void draw_status_panel(void)
{
    const int panel_x = 25;
    const int panel_y = 17;
    const int panel_w = 50;
    const int panel_h = 8;

    box(panel_x, panel_y, panel_w, panel_h, A_FRAME);
    puts_xy(panel_x + 3, panel_y, A_BADGE, " BOOT STATUS ");

    int rows = panel_h - 2;
    for (int i = 0; i < s_count && i < rows; i++) {
        int row = panel_y + 1 + i;

        puts_xy_clip(panel_x + 4, row, A_TEXT, s_items[i].name, 17);
        puts_xy(panel_x + 22, row, A_DIM, "................");

        uint8_t a = attr_for_status(s_items[i].status);
        puts_xy_clip(panel_x + 33, row, a, s_items[i].status, 13);
    }
}

static void draw_progress(void)
{
    const int total = 58;
    const int x = (DISPLAY_COLS - total) / 2;
    const int y = 26;

    int filled;
    if (s_ready) {
        filled = total;
    } else {
        filled = (s_count * total) / 7;
        if (filled < 6) filled = 6;
        if (filled > total) filled = total;
    }

    putc_xy(x - 1, y, A_FRAME, '[');
    for (int i = 0; i < total; i++) {
        putc_xy(x + i, y, i < filled ? A_BAR_ON : A_BAR_OFF, i < filled ? '=' : '-');
    }
    putc_xy(x + total, y, A_FRAME, ']');
}

static void redraw(void)
{
    cell_clear(A_BG);

    // Marco principal
    box(1, 1, DISPLAY_COLS - 2, DISPLAY_ROWS - 2, A_FRAME);
    draw_corner_marks();

    // Cabecera fina
    hline(8, 3, DISPLAY_COLS - 16, A_FRAME2, '-');
    puts_xy(10, 3, A_DIM, " ESP32-S3 TOUCH LCD 7 ");
    puts_xy(DISPLAY_COLS - 30, 3, A_DIM, " 800x480 RGB ");

    draw_logo_chip();

    puts_center(14, A_TITLE, "Arielo MiniPC OS");
    puts_center(15, A_CYAN,  "BreezyBox Edition");

    puts_center(16, A_TEXT, s_message);

    draw_status_panel();
    draw_progress();

    if (s_ready) {
        puts_center(28, A_OK, "System ready - entering shell");
    } else {
        puts_center(28, A_DIM, "Loading components in foreground splash");
    }
}

void minipc_bootgfx_begin(void)
{
    memset(s_items, 0, sizeof(s_items));
    s_count = 0;
    s_ready = 0;
    safe_copy(s_message, sizeof(s_message), "Starting system...");

    rgb_display_set_buffer(s_boot_cells);

    minipc_bootgfx_ok("Display");
}

void minipc_bootgfx_status(const char *name, const char *status)
{
    int idx = find_item(name);
    if (idx >= 0) {
        safe_copy(s_items[idx].status, sizeof(s_items[idx].status), status);
    }

    redraw();
    boot_delay();
}

void minipc_bootgfx_ok(const char *name)
{
    minipc_bootgfx_status(name, "OK");
}

void minipc_bootgfx_wait(const char *name)
{
    minipc_bootgfx_status(name, "starting...");
}

void minipc_bootgfx_fail(const char *name)
{
    minipc_bootgfx_status(name, "FAIL");
}

void minipc_bootgfx_message(const char *msg)
{
    safe_copy(s_message, sizeof(s_message), msg);
    redraw();
    boot_delay();
}

void minipc_bootgfx_ready(void)
{
    s_ready = 1;
    safe_copy(s_message, sizeof(s_message), "Boot sequence complete.");
    redraw();
    vTaskDelay(pdMS_TO_TICKS(1000));
}
