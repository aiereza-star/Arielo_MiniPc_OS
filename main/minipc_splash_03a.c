/*
 * minipc_splash_03a.c
 *
 * Splash textual seguro para Arielo MiniPC OS.
 *
 * Regla de oro:
 *   Llamar SOLO después de my_console_init(),
 *   cuando stdout ya apunta al VTerm/LCD.
 *
 * No usa caracteres extendidos para evitar problemas de fuente/encoding.
 */

#include "minipc_splash_03a.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SPLASH_USE_ANSI_COLOR 1

#if SPLASH_USE_ANSI_COLOR
#define C_RESET   "\033[0m"
#define C_BLUE    "\033[36m"
#define C_WHITE   "\033[97m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_DIM     "\033[2m"
#else
#define C_RESET   ""
#define C_BLUE    ""
#define C_WHITE   ""
#define C_GREEN   ""
#define C_YELLOW  ""
#define C_DIM     ""
#endif

static void splash_delay_short(void)
{
    vTaskDelay(pdMS_TO_TICKS(120));
}

void minipc_splash_clear(void)
{
    /*
     * Clear screen + cursor home.
     * Si el VTerm no interpretara ANSI, no rompe nada: solo se verian
     * los codigos, pero en esta rama colortest/ANSI ya estaban previstos.
     */
    printf("\033[2J\033[H");
}

void minipc_splash_title(void)
{
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
    printf("Starting system...\r\n");
    printf("\r\n");

    splash_delay_short();
}

void minipc_splash_status(const char *name, const char *status)
{
    if (!name) name = "?";
    if (!status) status = "?";

    printf("  %-18s ", name);

    if (status[0] == 'O' && status[1] == 'K') {
        printf(C_GREEN "%s" C_RESET, status);
    } else if (status[0] == 'W' || status[0] == 'S') {
        printf(C_YELLOW "%s" C_RESET, status);
    } else {
        printf("%s", status);
    }

    printf("\r\n");
    splash_delay_short();
}

void minipc_splash_status_ok(const char *name)
{
    minipc_splash_status(name, "OK");
}

void minipc_splash_status_wait(const char *name)
{
    minipc_splash_status(name, "starting...");
}

void minipc_splash_done(void)
{
    printf("\r\n");
    printf(C_BLUE "[==========" C_RESET);
    printf(C_BLUE "====================" C_RESET);
    printf(C_BLUE "====================" C_RESET);
    printf(C_BLUE "==========]" C_RESET);
    printf("\r\n");

    printf(C_GREEN "System ready." C_RESET "\r\n");
    printf("Loading shell...\r\n");
    printf("\r\n");

    vTaskDelay(pdMS_TO_TICKS(400));
}
