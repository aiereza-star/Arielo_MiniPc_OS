// minipc_browser.c
// 10BM_FIX2_NO_OLD_BROWSER_FLASH_CLEAN
//
// Sustituto minimo del navegador clasico.
// El boton WEB ya abre TactileBrowser/Lexbor, por lo que el motor antiguo
// completo no se necesita. Se conserva esta API minima para que el escritorio
// siga enlazando sin tocar su codigo ni CMakeLists.txt.

#include "minipc_browser.h"
#include <stddef.h>

static browser_state_t s_state = BROWSER_IDLE;
static const char s_empty[] = "";
static const char s_disabled[] = "Navegador antiguo desactivado; use TactileBrowser";
static const char s_home[] = "about:home";

void minipc_browser_init(void)
{
    s_state = BROWSER_IDLE;
}

void minipc_browser_load(const char *url)
{
    (void)url;
    s_state = BROWSER_ERROR;
}

void minipc_browser_set_loading(const char *url)
{
    (void)url;
    s_state = BROWSER_LOADING;
}

browser_state_t minipc_browser_state(void)
{
    return s_state;
}

const char *minipc_browser_status_text(void)
{
    return (s_state == BROWSER_IDLE) ? s_empty : s_disabled;
}

const char *minipc_browser_text(void)
{
    return s_empty;
}

int minipc_browser_line_count(void)
{
    return 0;
}

const char *minipc_browser_get_line(int n, int *out_len)
{
    (void)n;
    if (out_len) *out_len = 0;
    return s_empty;
}

int minipc_browser_link_count(void)
{
    return 0;
}

const char *minipc_browser_get_link_url(int n)
{
    (void)n;
    return s_empty;
}

const char *minipc_browser_get_link_text(int n)
{
    (void)n;
    return s_empty;
}

int minipc_browser_form_available(void)
{
    return 0;
}

const char *minipc_browser_form_action(void)
{
    return s_empty;
}

const char *minipc_browser_form_input_name(void)
{
    return s_empty;
}

const char *minipc_browser_current_url(void)
{
    return s_home;
}

const char *minipc_browser_home_url(void)
{
    return s_home;
}

void minipc_browser_set_home(const char *url)
{
    (void)url;
}

void minipc_browser_home(void)
{
    s_state = BROWSER_ERROR;
}

void minipc_browser_reload(void)
{
    s_state = BROWSER_ERROR;
}

int minipc_browser_can_back(void)
{
    return 0;
}

int minipc_browser_can_forward(void)
{
    return 0;
}

void minipc_browser_back(void)
{
}

void minipc_browser_forward(void)
{
}

int minipc_browser_history_count(void)
{
    return 0;
}

int minipc_browser_history_index(void)
{
    return -1;
}

const char *minipc_browser_history_url(int n)
{
    (void)n;
    return s_empty;
}

int minipc_browser_history_goto(int n)
{
    (void)n;
    return -1;
}

int minipc_browser_fav_add(const char *url)
{
    (void)url;
    return -1;
}

int minipc_browser_fav_remove(int n)
{
    (void)n;
    return -1;
}

int minipc_browser_fav_count(void)
{
    return 0;
}

const char *minipc_browser_fav_url(int n)
{
    (void)n;
    return s_empty;
}

int minipc_browser_fav_is_current(void)
{
    return 0;
}
