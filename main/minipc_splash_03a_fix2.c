/*
 * minipc_splash_03a_fix2.c
 *
 * Splash fijo / foreground boot para Arielo MiniPC OS.
 *
 * Filosofia:
 *   - El splash se redibuja completo en posiciones fijas.
 *   - Si un modulo imprime durante su init, el siguiente redibujado
 *     vuelve a dejar la pantalla limpia.
 *   - Al final se limpia todo y se entrega la pantalla a BreezyBox.
 *
 * No usa caracteres extendidos ni acentos para evitar problemas de fuente.
 */

#include "minipc_splash_03a_fix2.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SPLASH_MAX_ITEMS 10
#define SPLASH_NAME_LEN  20
#define SPLASH_STAT_LEN  32

#define SPLASH_USE_ANSI_COLOR 1

#if SPLASH_USE_ANSI_COLOR
#define C_RESET   "\033[0m"
#define C_BLUE    "\033[36m"
#define C_WHITE   "\033[97m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_RED     "\033[31m"
#define C_DIM     "\033[2m"
#else
#define C_RESET   ""
#define C_BLUE    ""
#define C_WHITE   ""
#define C_GREEN   ""
#define C_YELLOW  ""
#define C_RED     ""
#define C_DIM     ""
#endif

typedef struct {
    char name[SPLASH_NAME_LEN];
    char status[SPLASH_STAT_LEN];
} splash_item_t;

static splash_item_t s_items[SPLASH_MAX_ITEMS];
static int s_count = 0;
static char s_message[64] = "Starting system...";
static int s_ready = 0;

static void safe_copy(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) return;
    if (!src) src = "";
    snprintf(dst, dst_len, "%s", src);
}

static int find_item(const char *name)
{
    if (!name) return -1;

    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_items[i].name, name) == 0) return i;
    }

    if (s_count >= SPLASH_MAX_ITEMS) return -1;

    safe_copy(s_items[s_count].name, sizeof(s_items[s_count].name), name);
    safe_copy(s_items[s_count].status, sizeof(s_items[s_count].status), "...");
    return s_count++;
}

static const char *status_color(const char *status)
{
    if (!status) return C_RESET;
    if (strncmp(status, "OK", 2) == 0) return C_GREEN;
    if (strncmp(status, "FAIL", 4) == 0) return C_RED;
    if (strncmp(status, "starting", 8) == 0) return C_YELLOW;
    if (strncmp(status, "connecting", 10) == 0) return C_YELLOW;
    if (strncmp(status, "waiting", 7) == 0) return C_YELLOW;
    return C_WHITE;
}

static void redraw(void)
{
    printf("\033[2J\033[H");       // clear + home
    printf("\033[?25l");           // cursor off

    printf(C_BLUE);
    printf("========================================================================\r\n");
    printf(C_RESET);

    printf(C_WHITE);
    printf("                         Arielo MiniPC OS\r\n");
    printf(C_RESET);

    printf(C_DIM);
    printf("                         BreezyBox Edition\r\n");
    printf(C_RESET);

    printf(C_BLUE);
    printf("========================================================================\r\n");
    printf(C_RESET);

    printf("\r\n");
    printf("  %s%s%s\r\n", C_WHITE, s_message, C_RESET);
    printf("\r\n");

    for (int i = 0; i < s_count; i++) {
        const char *col = status_color(s_items[i].status);
        printf("  %-18s %s%s%s\r\n",
               s_items[i].name,
               col,
               s_items[i].status,
               C_RESET);
    }

    int filled = s_ready ? 48 : (s_count * 48 / 7);
    if (filled < 4) filled = 4;
    if (filled > 48) filled = 48;

    printf("\r\n  [");
    for (int i = 0; i < 48; i++) {
        if (i < filled) {
            printf(C_BLUE "=" C_RESET);
        } else {
            printf(C_DIM "-" C_RESET);
        }
    }
    printf("]\r\n");

    if (s_ready) {
        printf("\r\n  %sSystem ready.%s\r\n", C_GREEN, C_RESET);
        printf("  Loading shell...\r\n");
    } else {
        printf("\r\n  %sLoading components in background...%s\r\n", C_DIM, C_RESET);
    }

    fflush(stdout);
}

void minipc_splash_fg_begin(void)
{
    memset(s_items, 0, sizeof(s_items));
    s_count = 0;
    s_ready = 0;
    safe_copy(s_message, sizeof(s_message), "Starting system...");

    minipc_splash_fg_set("Display", "OK");
}

void minipc_splash_fg_set(const char *name, const char *status)
{
    int idx = find_item(name);
    if (idx >= 0) {
        safe_copy(s_items[idx].status, sizeof(s_items[idx].status), status);
    }

    redraw();
    vTaskDelay(pdMS_TO_TICKS(120));
}

void minipc_splash_fg_ok(const char *name)
{
    minipc_splash_fg_set(name, "OK");
}

void minipc_splash_fg_wait(const char *name)
{
    minipc_splash_fg_set(name, "starting...");
}

void minipc_splash_fg_fail(const char *name)
{
    minipc_splash_fg_set(name, "FAIL");
}

void minipc_splash_fg_message(const char *message)
{
    safe_copy(s_message, sizeof(s_message), message);
    redraw();
    vTaskDelay(pdMS_TO_TICKS(120));
}

void minipc_splash_fg_ready(void)
{
    s_ready = 1;
    safe_copy(s_message, sizeof(s_message), "Boot sequence complete.");
    redraw();
    vTaskDelay(pdMS_TO_TICKS(900));
}

void minipc_splash_fg_finish_to_shell(void)
{
    printf("\033[2J\033[H");
    printf("\033[?25h");           // cursor on
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(80));
}
