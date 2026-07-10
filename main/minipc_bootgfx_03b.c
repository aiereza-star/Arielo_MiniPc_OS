/*
 * minipc_bootgfx_03b.c
 *
 * Arielo MiniPC OS - splash grafico seguro.
 *
 * Esta version NO escribe en stdout.
 * Dibuja directamente sobre un buffer lcd_cell_t propio y se lo entrega
 * a rgb_display_set_buffer().
 *
 * Ventaja:
 *   - No hay scroll.
 *   - Los logs/printf de fondo no pisan la pantalla.
 *   - my_console_init() recupera luego el display con el buffer del VTerm.
 *
 * Es un "grafico de texto por celdas", seguro antes de pasar a pixel/fb.
 */

#include "minipc_bootgfx_03b.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rgb_display.h"

#define CELL_COUNT (DISPLAY_COLS * DISPLAY_ROWS)
#define MAX_ITEMS  10
#define NAME_LEN   20
#define STAT_LEN   32

// CGA-ish attrs: attr = (bg << 4) | fg
#define A_BG        0x10  // blue background, black-ish base
#define A_TITLE     0x1F  // blue bg, bright white fg
#define A_SUB       0x1B  // blue bg, bright cyan fg
#define A_FRAME     0x1B
#define A_TEXT      0x17
#define A_DIM       0x18
#define A_OK        0x1A  // green
#define A_WAIT      0x1E  // yellow
#define A_FAIL      0x1C  // red
#define A_BAR       0x1B
#define A_BAR_ON    0x2F  // green bg white fg

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

static void puts_xy(int x, int y, uint8_t attr, const char *s)
{
    if (!s || y < 0 || y >= DISPLAY_ROWS) return;
    while (*s && x < DISPLAY_COLS) {
        if (x >= 0) putc_xy(x, y, attr, *s);
        x++;
        s++;
    }
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
    return A_SUB;
}

static void draw_logo(void)
{
    /*
     * Logo simple tipo microchip/miniPC usando celdas y color.
     * Coordenadas centradas.
     */
    int x = 42;
    int y = 5;

    box(x, y, 16, 7, A_FRAME);

    puts_xy(x + 5, y + 1, A_SUB, "MINI");
    puts_xy(x + 4, y + 2, A_TITLE, "PC-OS");

    hline(x + 3, y + 4, 10, A_BAR, '=');
    puts_xy(x + 5, y + 5, A_OK, "READY");

    // pines laterales
    for (int i = 1; i <= 5; i++) {
        putc_xy(x - 2, y + i, A_DIM, '-');
        putc_xy(x - 1, y + i, A_DIM, '-');
        putc_xy(x + 16, y + i, A_DIM, '-');
        putc_xy(x + 17, y + i, A_DIM, '-');
    }
}

static void draw_progress(void)
{
    int total = 54;
    int x = (DISPLAY_COLS - total) / 2;
    int y = 25;

    int filled;
    if (s_ready) {
        filled = total;
    } else {
        filled = (s_count * total) / 7;
        if (filled < 5) filled = 5;
        if (filled > total) filled = total;
    }

    putc_xy(x - 1, y, A_FRAME, '[');
    for (int i = 0; i < total; i++) {
        putc_xy(x + i, y, i < filled ? A_BAR_ON : A_DIM, i < filled ? '=' : '-');
    }
    putc_xy(x + total, y, A_FRAME, ']');
}

static void redraw(void)
{
    cell_clear(A_BG);

    // Marco exterior
    box(1, 1, DISPLAY_COLS - 2, DISPLAY_ROWS - 2, A_FRAME);

    // Decoracion superior
    hline(4, 3, DISPLAY_COLS - 8, A_DIM, '-');

    draw_logo();

    puts_center(13, A_TITLE, "Arielo MiniPC OS");
    puts_center(14, A_SUB,   "BreezyBox Edition");
    puts_center(16, A_TEXT,  s_message);

    // Panel de estados
    int panel_x = 29;
    int panel_y = 18;
    int panel_w = 42;
    int panel_h = 6;
    box(panel_x, panel_y, panel_w, panel_h, A_FRAME);

    int max_rows = panel_h - 2;
    for (int i = 0; i < s_count && i < max_rows; i++) {
        int row = panel_y + 1 + i;
        puts_xy(panel_x + 3, row, A_TEXT, s_items[i].name);

        /*
         * FIX1:
         * Con -Werror=format-truncation, "%-18s" puede avisar si status
         * mide mas que el buffer. Limitamos a 18 caracteres siempre.
         */
        char stat[19];
        snprintf(stat, sizeof(stat), "%-18.18s", s_items[i].status);
        puts_xy(panel_x + 21, row, attr_for_status(s_items[i].status), stat);
    }

    draw_progress();

    if (s_ready) {
        puts_center(27, A_OK, "System ready - loading shell");
    } else {
        puts_center(27, A_DIM, "Loading components in background");
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
    vTaskDelay(pdMS_TO_TICKS(900));
}
