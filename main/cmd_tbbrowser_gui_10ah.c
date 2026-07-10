/*
 * Arielo MiniPC OS - TactileBrowser builtin GUI NET browser 10AL
 *
 * Base: 10AA_TACTILEBROWSER_BUILTIN_RENDER_TEST validada.
 *
 * Objetivo 10AH:
 *   - Mantener Lexbor integrado en firmware normal, sin ELF externo.
 *   - Cargar HTML desde sample / SD / USB / root.
 *   - Parsear con Lexbor.
 *   - Recorrer DOM basico.
 *   - Convertir a lineas de texto.
 *   - Mostrar visor paginado con scroll basico por teclado.
 *   - Detectar enlaces <a href=...> y permitir abrir enlaces locales.
 *
 * Objetivo 10AH:
 *   - Mantener todo lo validado en 10AD.
 *   - Anadir favoritos/marcadores locales persistentes en /root/tbbrowser_favs.txt.
 *   - Tecla m para guardar la pagina actual.
 *   - Tecla v para listar/abrir favoritos.
 *   - Mantener cabecera, historial BACK, recarga, enlaces y rutas relativas.
 *
 * Comandos:
 *   tbbrowser
 *   tbbrowser sample
 *   tbbrowser file /sdcard/test.html
 *   tbbrowser /sdcard/test.html
 *
 * Controles dentro del visor:
 *   n / ENTER : bajar una pagina
 *   p         : subir una pagina; flecha arriba = subir linea
 *   j/k       : bajar/subir una linea
 *   l         : listar enlaces detectados
 *   numero    : abrir enlace local [n]
 *   o RUTA    : abrir archivo local
 *   r         : recargar
 *   b         : volver atras en historial
 *   m         : guardar pagina actual como favorito
 *   v         : ver/abrir favoritos
 *   g / G     : inicio / final
 *   h / ?     : ayuda
 *   q         : salir
 *
 * Escalon 10AI:
 *   - Mantiene 10AH_FIX2 estable.
 *   - Anade botones tactiles en la barra inferior del navegador GUI.
 *   - Tocar contenido: arriba = pagina arriba, abajo = pagina abajo.
 *   - Tocar una linea [n] abre ese enlace.
 *
 * Escalon 10AK:
 *   - Mantiene 10AJ_OK con selectores FILE + FAVS.
 *   - No cambia motor Lexbor ni navegacion.
 *   - Pulido visual del render GUI:
 *       H1/H2 con bandas, listas con sangria, enlaces resaltados,
 *       barra de progreso lateral y cabecera mas limpia.
 *
 * Escalon 10AM:
 *   - Mantiene 10AL_FIX4 validada con barra clasica superior.
 *   - Anade boton adelante > con pila FORWARD.
 *   - Anade boton HIST con pagina interna about:history.
 *
 * Escalon 10AN_FIX3:
 *   - En vez de depender del puntero, muestra un reloj de arena fijo en
 *     la cabecera durante cargas bloqueantes HTTP/archivo/historial.
 *
 * Escalon 10AO:
 *   - Activa HTTPS en el navegador Lexbor GUI usando el camino ya validado
 *     por el navegador antiguo: esp_http_client + TLS de ESP-IDF.
 *   - Mantiene HTTP y redirecciones automaticas.
 *

 * Escalon 10AX_FIX2:
 *   - Evita WDT en paginas con mucho script/style o tags sin cierre:
 *     no se queda buscando indefinidamente y cede CPU durante parseo HTML10D.
 * Escalon 10AZ:
 *   - HTML10D Smart Cleaner: oculta CSS/JS basura por lineas, detecta
 *     login/cuenta/antibot y muestra mensajes entendibles sin tocar red.
 * Escalon 10BA:
 *   - Mejora interaccion: numeros de enlace de dos cifras, click real de raton,
 *     hover en botones/enlaces y resaltado solo del elemento apuntado.
 *
 * Escalon 10BK:
 *   - En Favoritos, apuntar un favorito convierte +FAV en -FAV y permite
 *     eliminarlo de /root/tbbrowser_favs.txt sin tocar red ni HTML10D.
 *
 * Escalon 10BB:
 *   - FORM-LITE portado desde minipc_browser.c antiguo: detecta formularios GET
 *     con input text/search, muestra [FORM] BUSCAR y permite buscar con ?texto.
 *
 * Escalon 10BG:
 *   - Aplanador legado minipc_browser.c: enlaces inline, [IMG: alt], tablas y [FORM].
 *   - JS-LITE seguro: no ejecuta JavaScript completo; solo detecta acciones
 *     simples de navegacion: meta refresh, location.href, window.location,
 *     location.replace/assign. Resuelve y sigue la URL con limite de saltos.
 *
 * Escalon 10BH:
 *   - Usabilidad segura sobre 10BG_FIX1_OK: estado de URL al pasar por enlaces,
 *     ayuda de botones, busqueda dentro de pagina (/ texto, n/N) y salto lector.
 *   - No toca red, HTTPS, Lexbor ni el aplanador profundo.
 *
 * Escalon 10AP:
 *   - Recupera la filosofia del navegador viejo como buscador practico:
 *     si en la barra se escribe texto normal, ddg: texto o buscar texto,
 *     abre DuckDuckGo HTML sin JavaScript: https://html.duckduckgo.com/html/?q=...
 *   - Si se escribe duckduckgo.com o duckduckgo, abre la version HTML sin JS.
 *
 * Escalon 10BO:
 *   - Cookies Lite en RAM: captura Set-Cookie, guarda por dominio/path y
 *     reenvia Cookie en siguientes peticiones HTTP/HTTPS.
 *   - Pagina interna about:cookies para diagnostico y about:cookies:clear.
 *   - No toca User-Agent ESP32-BreezyBox ni la huella de red validada.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <dirent.h>
#include <ctype.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rgb_display.h"
#include "vterm.h"
#include "minipc_touch_gt911.h"
#include "usb_hid_keyboard_02d.h"
#include "minipc_wifi_02c.h"

#include "lexbor/html/html.h"
#include "lexbor/core/lexbor.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/dom/interfaces/text.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/html/interfaces/document.h"

#define TBB10AH_FILE_MAX          (32 * 1024)
#define TBB10AL_HTTP_MAX          (128 * 1024)
#define TBB10AL_URL_CAP           256
#define TBB10AH_MAX_LINES         512
#define TBB10AH_LINE_CAP          112
#define TBB10AH_WRAP_COL          96
#define TBB10AH_PAGE_LINES        16
#define TBB10AH_MAX_LINKS         64
#define TBB10AH_HREF_CAP          192
#define TBB10AH_PATH_CAP          256
#define TBB10AH_HISTORY_MAX       12
#define TBB10AH_TITLE_CAP         96
#define TBB10AH_BOOKMARKS_FILE    "/root/tbbrowser_favs.txt"
#define TBB10AH_BOOKMARKS_MAX     64

#define TBB10BO_COOKIE_MAX        24
#define TBB10BO_COOKIE_NAME_CAP   48
#define TBB10BO_COOKIE_VALUE_CAP  192
#define TBB10BO_COOKIE_DOMAIN_CAP 96
#define TBB10BO_COOKIE_PATH_CAP   64
#define TBB10BO_COOKIE_HEADER_CAP 512
#define TBB10BO_SETCOOKIE_CAP     2048
#define TBB10AM_HISTORY_HTML_MAX  12288

static char s_tbb10am_hist_snapshot[TBB10AH_HISTORY_MAX][TBB10AH_PATH_CAP];
static char s_tbb10am_fwd_snapshot[TBB10AH_HISTORY_MAX][TBB10AH_PATH_CAP];
static int  s_tbb10am_hist_snapshot_count = 0;
static int  s_tbb10am_fwd_snapshot_count = 0;
static char s_tbb10am_current_snapshot[TBB10AH_PATH_CAP];

/* 10BB_FIX1: estado de sesion fuera de la pila de breezy_repl.
 * La combinacion HTML10D + FORM-LITE ya trabaja con paginas grandes;
 * mantener current/next/history/forward en stack dejaba muy poca pila libre.
 */
static char s_tbb10bb_session_current[TBB10AH_PATH_CAP];
static char s_tbb10bb_session_next[TBB10AH_PATH_CAP];
static char s_tbb10bb_session_history[TBB10AH_HISTORY_MAX][TBB10AH_PATH_CAP];
static char s_tbb10bb_session_forward[TBB10AH_HISTORY_MAX][TBB10AH_PATH_CAP];


static void tbb10ah_safe_copy(char *dst, size_t dst_cap, const char *src)
{
    if (dst == NULL || dst_cap == 0) return;
    if (src == NULL) src = "";

    size_t i = 0;
    while (i + 1 < dst_cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}


static void tbb10ah_safe_append(char *dst, size_t dst_cap, const char *src)
{
    if (dst == NULL || dst_cap == 0) return;
    if (src == NULL) return;

    size_t pos = 0;
    while (pos < dst_cap && dst[pos] != '\0') pos++;
    if (pos >= dst_cap) {
        dst[dst_cap - 1] = '\0';
        return;
    }

    size_t i = 0;
    while (pos + 1 < dst_cap && src[i] != '\0') {
        dst[pos++] = src[i++];
    }
    dst[pos] = '\0';
}

static void tbb10am_update_history_snapshot(const char *current,
                                             char hist[TBB10AH_HISTORY_MAX][TBB10AH_PATH_CAP], int hist_count,
                                             char fwd[TBB10AH_HISTORY_MAX][TBB10AH_PATH_CAP], int fwd_count)
{
    if (current == NULL) current = "";
    tbb10ah_safe_copy(s_tbb10am_current_snapshot, sizeof(s_tbb10am_current_snapshot), current);

    if (hist_count < 0) hist_count = 0;
    if (hist_count > TBB10AH_HISTORY_MAX) hist_count = TBB10AH_HISTORY_MAX;
    if (fwd_count < 0) fwd_count = 0;
    if (fwd_count > TBB10AH_HISTORY_MAX) fwd_count = TBB10AH_HISTORY_MAX;

    memset(s_tbb10am_hist_snapshot, 0, sizeof(s_tbb10am_hist_snapshot));
    memset(s_tbb10am_fwd_snapshot, 0, sizeof(s_tbb10am_fwd_snapshot));
    s_tbb10am_hist_snapshot_count = hist_count;
    s_tbb10am_fwd_snapshot_count = fwd_count;

    if (hist != NULL && hist_count > 0) {
        memcpy(s_tbb10am_hist_snapshot, hist, (size_t)hist_count * TBB10AH_PATH_CAP);
    }
    if (fwd != NULL && fwd_count > 0) {
        memcpy(s_tbb10am_fwd_snapshot, fwd, (size_t)fwd_count * TBB10AH_PATH_CAP);
    }
}

static void *tbb10ah_lexbor_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbb10ah_lexbor_calloc(size_t num, size_t size)
{
    size_t total = num * size;
    if (num != 0 && total / num != size) return NULL;

    void *p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_calloc(num, size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbb10ah_lexbor_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    return p;
}

static void tbb10ah_lexbor_free(void *ptr)
{
    heap_caps_free(ptr);
}

static void tbb10ah_lexbor_memory_setup(void)
{
    static bool done = false;
    if (done) return;

    lxb_status_t st = lexbor_memory_setup(tbb10ah_lexbor_malloc,
                                          tbb10ah_lexbor_realloc,
                                          tbb10ah_lexbor_calloc,
                                          tbb10ah_lexbor_free);
    printf("[TBBROWSER10AH] lexbor_memory_setup PSRAM-first st=%d\n", (int) st);
    done = true;
}

static void tbb10ah_heap_print(const char *tag)
{
    printf("[TBBROWSER10AH] %-12s heap8=%u psram=%u min8=%u\n",
           tag ? tag : "?",
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

typedef struct {
    char *buf;
    char *links;
    uint8_t *line_style; /* 10BP: 1 byte por linea, reservado en PSRAM */
    uint8_t current_style;
    int max_lines;
    int line_cap;
    int link_href_cap;
    int link_count;
    char title[TBB10AH_TITLE_CAP];
    int count;
    int col;
    bool last_space;
    bool stopped;
} tbb10ah_doc_t;

static char *tbb10ah_line_ptr(tbb10ah_doc_t *doc, int idx)
{
    if (doc == NULL || doc->buf == NULL || idx < 0 || idx >= doc->max_lines) return NULL;
    return doc->buf + ((size_t) idx * (size_t) doc->line_cap);
}

static char *tbb10ah_link_ptr(tbb10ah_doc_t *doc, int idx)
{
    if (doc == NULL || doc->links == NULL || idx < 0 || idx >= TBB10AH_MAX_LINKS) return NULL;
    return doc->links + ((size_t) idx * (size_t) doc->link_href_cap);
}

static int tbb10ah_add_link(tbb10ah_doc_t *doc, const lxb_char_t *href, size_t href_len)
{
    if (doc == NULL || href == NULL || href_len == 0 || doc->links == NULL) return 0;
    if (doc->link_count >= TBB10AH_MAX_LINKS) return 0;

    char *dst = tbb10ah_link_ptr(doc, doc->link_count);
    if (dst == NULL) return 0;

    size_t n = href_len;
    if (n >= (size_t) doc->link_href_cap) n = (size_t) doc->link_href_cap - 1;
    memcpy(dst, href, n);
    dst[n] = '\0';

    doc->link_count++;
    return doc->link_count;
}

static bool tbb10ah_doc_alloc(tbb10ah_doc_t *doc)
{
    if (doc == NULL) return false;
    memset(doc, 0, sizeof(*doc));

    doc->max_lines = TBB10AH_MAX_LINES;
    doc->line_cap = TBB10AH_LINE_CAP;
    doc->link_href_cap = TBB10AH_HREF_CAP;

    size_t bytes = (size_t) doc->max_lines * (size_t) doc->line_cap;
    doc->buf = (char *) heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (doc->buf == NULL) {
        doc->buf = (char *) heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    }
    if (doc->buf == NULL) {
        printf("[TBBROWSER10AH][ERR] sin memoria para line buffer bytes=%u\n", (unsigned) bytes);
        return false;
    }

    size_t lbytes = (size_t) TBB10AH_MAX_LINKS * (size_t) TBB10AH_HREF_CAP;
    doc->links = (char *) heap_caps_calloc(1, lbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (doc->links == NULL) {
        doc->links = (char *) heap_caps_calloc(1, lbytes, MALLOC_CAP_8BIT);
    }
    if (doc->links == NULL) {
        printf("[TBBROWSER10AH][WARN] sin memoria para links bytes=%u; continuo sin enlaces\n", (unsigned) lbytes);
    }

    /* 10BP RAM-safe: solo un byte por linea y PSRAM-first. */
    doc->line_style = (uint8_t *)heap_caps_calloc((size_t)doc->max_lines, 1,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (doc->line_style == NULL) {
        doc->line_style = (uint8_t *)heap_caps_calloc((size_t)doc->max_lines, 1, MALLOC_CAP_8BIT);
    }
    doc->current_style = 0;

    doc->link_count = 0;
    doc->count = 1;
    doc->col = 0;
    doc->last_space = false;
    doc->stopped = false;
    return true;
}

static void tbb10ah_doc_free(tbb10ah_doc_t *doc)
{
    if (doc == NULL) return;
    if (doc->buf != NULL) heap_caps_free(doc->buf);
    if (doc->links != NULL) heap_caps_free(doc->links);
    if (doc->line_style != NULL) heap_caps_free(doc->line_style);
    memset(doc, 0, sizeof(*doc));
}

static bool tbb10ah_char_is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static bool tbb10ah_tag_eq(const lxb_char_t *name, size_t len, const char *lit)
{
    if (name == NULL || lit == NULL) return false;
    size_t lit_len = strlen(lit);
    if (len != lit_len) return false;

    for (size_t i = 0; i < len; i++) {
        unsigned char a = (unsigned char) name[i];
        unsigned char b = (unsigned char) lit[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}


/* -------------------------------------------------------------------------
 * 10BP - STYLE LITE: color + negrita sin motor CSS
 * -------------------------------------------------------------------------
 * Un byte por linea. Bits 0..3 = color logico; bit 7 = bold.
 * La memoria vive junto al documento y se libera al salir del navegador.
 */
enum {
    TBB10BP_STYLE_COLOR_DEFAULT = 0,
    TBB10BP_STYLE_COLOR_RED,
    TBB10BP_STYLE_COLOR_GREEN,
    TBB10BP_STYLE_COLOR_BLUE,
    TBB10BP_STYLE_COLOR_YELLOW,
    TBB10BP_STYLE_COLOR_ORANGE,
    TBB10BP_STYLE_COLOR_GRAY,
    TBB10BP_STYLE_COLOR_WHITE,
    TBB10BP_STYLE_COLOR_CYAN,
    TBB10BP_STYLE_COLOR_MASK = 0x0F,
    TBB10BP_STYLE_BOLD = 0x80
};

static const char *tbb10bp_skip_css_ws(const char *p)
{
    while (p != NULL && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
    return p;
}

static bool tbb10bp_css_value_starts(const char *value, const char *name)
{
    if (value == NULL || name == NULL) return false;
    value = tbb10bp_skip_css_ws(value);
    size_t n = strlen(name);
    return strncasecmp(value, name, n) == 0;
}

static uint8_t tbb10bp_color_from_css_value(const char *v)
{
    if (v == NULL) return TBB10BP_STYLE_COLOR_DEFAULT;
    v = tbb10bp_skip_css_ws(v);
    if (*v == '#') {
        unsigned r = 0, g = 0, b = 0;
        if (sscanf(v + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
            if (r > 190 && g < 120 && b < 120) return TBB10BP_STYLE_COLOR_RED;
            if (g > 150 && r < 170 && b < 170) return TBB10BP_STYLE_COLOR_GREEN;
            if (b > 160 && r < 170 && g < 190) return TBB10BP_STYLE_COLOR_BLUE;
            if (r > 180 && g > 150 && b < 110) return TBB10BP_STYLE_COLOR_YELLOW;
            if (r > 190 && g > 90 && g < 190 && b < 90) return TBB10BP_STYLE_COLOR_ORANGE;
            if (r > 200 && g > 200 && b > 200) return TBB10BP_STYLE_COLOR_WHITE;
            if (r < 170 && g > 150 && b > 150) return TBB10BP_STYLE_COLOR_CYAN;
            return TBB10BP_STYLE_COLOR_GRAY;
        }
    }
    if (tbb10bp_css_value_starts(v, "red") || tbb10bp_css_value_starts(v, "crimson")) return TBB10BP_STYLE_COLOR_RED;
    if (tbb10bp_css_value_starts(v, "green") || tbb10bp_css_value_starts(v, "lime")) return TBB10BP_STYLE_COLOR_GREEN;
    if (tbb10bp_css_value_starts(v, "blue") || tbb10bp_css_value_starts(v, "navy")) return TBB10BP_STYLE_COLOR_BLUE;
    if (tbb10bp_css_value_starts(v, "yellow") || tbb10bp_css_value_starts(v, "gold")) return TBB10BP_STYLE_COLOR_YELLOW;
    if (tbb10bp_css_value_starts(v, "orange")) return TBB10BP_STYLE_COLOR_ORANGE;
    if (tbb10bp_css_value_starts(v, "gray") || tbb10bp_css_value_starts(v, "grey") || tbb10bp_css_value_starts(v, "silver")) return TBB10BP_STYLE_COLOR_GRAY;
    if (tbb10bp_css_value_starts(v, "white")) return TBB10BP_STYLE_COLOR_WHITE;
    if (tbb10bp_css_value_starts(v, "cyan") || tbb10bp_css_value_starts(v, "aqua")) return TBB10BP_STYLE_COLOR_CYAN;
    return TBB10BP_STYLE_COLOR_DEFAULT;
}

static uint8_t tbb10bp_style_parse_inline(const char *style, uint8_t inherited)
{
    if (style == NULL || style[0] == '\0') return inherited;
    uint8_t out = inherited;
    const char *p = style;
    while (*p) {
        while (*p == ';' || isspace((unsigned char)*p)) p++;
        const char *colon = strchr(p, ':');
        if (colon == NULL) break;
        const char *semi = strchr(colon + 1, ';');
        const char *end = semi ? semi : p + strlen(p);
        size_t klen = (size_t)(colon - p);
        while (klen > 0 && isspace((unsigned char)p[klen - 1])) klen--;
        char val[48];
        size_t vlen = (size_t)(end - (colon + 1));
        if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
        memcpy(val, colon + 1, vlen); val[vlen] = '\0';
        if (klen == 5 && strncasecmp(p, "color", 5) == 0) {
            uint8_t c = tbb10bp_color_from_css_value(val);
            if (c != TBB10BP_STYLE_COLOR_DEFAULT) out = (uint8_t)((out & ~TBB10BP_STYLE_COLOR_MASK) | c);
        }
        else if (klen == 11 && strncasecmp(p, "font-weight", 11) == 0) {
            const char *v = tbb10bp_skip_css_ws(val);
            if (strncasecmp(v, "bold", 4) == 0 || atoi(v) >= 600) out |= TBB10BP_STYLE_BOLD;
        }
        if (!semi) break;
        p = semi + 1;
    }
    return out;
}

static uint8_t tbb10bp_style_for_element(lxb_dom_node_t *node, uint8_t inherited,
                                         const lxb_char_t *name, size_t nlen)
{
    uint8_t st = inherited;
    if (tbb10ah_tag_eq(name, nlen, "b") || tbb10ah_tag_eq(name, nlen, "strong")) st |= TBB10BP_STYLE_BOLD;
    if (node != NULL && node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        lxb_dom_element_t *el = lxb_dom_interface_element(node);
        if (el != NULL) {
            size_t slen = 0;
            const lxb_char_t *sv = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"style", 5, &slen);
            if (sv != NULL && slen > 0) {
                char tmp[160];
                size_t n = slen < sizeof(tmp)-1 ? slen : sizeof(tmp)-1;
                memcpy(tmp, sv, n); tmp[n] = '\0';
                st = tbb10bp_style_parse_inline(tmp, st);
            }
            /* Compatibilidad HTML antigua: <font color="red"> */
            if (tbb10ah_tag_eq(name, nlen, "font")) {
                size_t clen = 0;
                const lxb_char_t *cv = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"color", 5, &clen);
                if (cv != NULL && clen > 0) {
                    char tmp[48]; size_t n = clen < sizeof(tmp)-1 ? clen : sizeof(tmp)-1;
                    memcpy(tmp, cv, n); tmp[n] = '\0';
                    uint8_t c = tbb10bp_color_from_css_value(tmp);
                    if (c) st = (uint8_t)((st & ~TBB10BP_STYLE_COLOR_MASK) | c);
                }
            }
        }
    }
    return st;
}

static void tbb10bp_mark_current_line(tbb10ah_doc_t *doc)
{
    if (doc == NULL || doc->line_style == NULL || doc->count <= 0 || doc->count > doc->max_lines) return;
    int idx = doc->count - 1;
    /* Primer estilo no-default gana; bold se acumula. */
    uint8_t old = doc->line_style[idx];
    uint8_t c = doc->current_style & TBB10BP_STYLE_COLOR_MASK;
    if ((old & TBB10BP_STYLE_COLOR_MASK) == 0 && c != 0) old = (uint8_t)((old & ~TBB10BP_STYLE_COLOR_MASK) | c);
    if (doc->current_style & TBB10BP_STYLE_BOLD) old |= TBB10BP_STYLE_BOLD;
    doc->line_style[idx] = old;
}

static void tbb10ah_newline(tbb10ah_doc_t *doc, bool force)
{
    if (doc == NULL || doc->stopped) return;
    if (!force && doc->col == 0) return;

    if (doc->count >= doc->max_lines) {
        doc->stopped = true;
        return;
    }

    doc->count++;
    doc->col = 0;
    doc->last_space = false;
}

static void tbb10ah_put_char(tbb10ah_doc_t *doc, char c)
{
    if (doc == NULL || doc->stopped) return;
    if (doc->count <= 0) doc->count = 1;

    if (c == '\n') {
        tbb10ah_newline(doc, true);
        return;
    }

    if (doc->col >= TBB10AH_WRAP_COL || doc->col >= (doc->line_cap - 1)) {
        tbb10ah_newline(doc, true);
        if (doc->stopped) return;
    }

    tbb10bp_mark_current_line(doc);
    char *line = tbb10ah_line_ptr(doc, doc->count - 1);
    if (line == NULL) {
        doc->stopped = true;
        return;
    }

    line[doc->col++] = c;
    line[doc->col] = '\0';
    doc->last_space = (c == ' ');
}

static void tbb10ah_puts(tbb10ah_doc_t *doc, const char *s)
{
    if (doc == NULL || s == NULL || doc->stopped) return;
    while (*s && !doc->stopped) {
        tbb10ah_put_char(doc, *s++);
    }
}

static void tbb10ah_text(tbb10ah_doc_t *doc, const lxb_char_t *data, size_t len)
{
    if (doc == NULL || data == NULL || len == 0 || doc->stopped) return;

    for (size_t i = 0; i < len && !doc->stopped; i++) {
        unsigned char c = (unsigned char) data[i];

        if (tbb10ah_char_is_space(c)) {
            if (doc->col > 0 && !doc->last_space) {
                tbb10ah_put_char(doc, ' ');
            }
            continue;
        }

        tbb10ah_put_char(doc, (char) c);
    }
}

static void tbb10ah_trim_trailing(tbb10ah_doc_t *doc)
{
    if (doc == NULL || doc->buf == NULL) return;

    while (doc->count > 1) {
        char *line = tbb10ah_line_ptr(doc, doc->count - 1);
        if (line == NULL || line[0] != '\0') break;
        doc->count--;
    }
}


static void tbb10ah_title_append(tbb10ah_doc_t *doc, const lxb_char_t *data, size_t len)
{
    if (doc == NULL || data == NULL || len == 0) return;

    size_t cur = strlen(doc->title);
    if (cur >= TBB10AH_TITLE_CAP - 1) return;

    bool last_space = false;
    if (cur > 0) last_space = (doc->title[cur - 1] == ' ');

    for (size_t i = 0; i < len && cur < TBB10AH_TITLE_CAP - 1; i++) {
        unsigned char c = (unsigned char) data[i];
        if (tbb10ah_char_is_space(c)) {
            if (cur > 0 && !last_space && cur < TBB10AH_TITLE_CAP - 1) {
                doc->title[cur++] = ' ';
                doc->title[cur] = '\0';
                last_space = true;
            }
            continue;
        }
        doc->title[cur++] = (char) c;
        doc->title[cur] = '\0';
        last_space = false;
    }
}

static void tbb10ah_extract_title_dom(lxb_dom_node_t *node, tbb10ah_doc_t *doc, bool in_title, int depth)
{
    if (node == NULL || doc == NULL || depth > 96) return;
    if (strlen(doc->title) >= TBB10AH_TITLE_CAP - 2) return;

    bool now_title = in_title;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        size_t nlen = 0;
        const lxb_char_t *name = lxb_dom_node_name(node, &nlen);
        if (tbb10ah_tag_eq(name, nlen, "title")) now_title = true;
    }

    if (now_title && node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *txt = lxb_dom_interface_text(node);
        if (txt != NULL && txt->char_data.data.data != NULL) {
            tbb10ah_title_append(doc, txt->char_data.data.data, txt->char_data.data.length);
        }
    }

    for (lxb_dom_node_t *child = node->first_child; child != NULL; child = child->next) {
        tbb10ah_extract_title_dom(child, doc, now_title, depth + 1);
    }
}

static void tbb10ah_walk_dom(lxb_dom_node_t *node, tbb10ah_doc_t *doc, int depth, uint8_t inherited_style)
{
    if (node == NULL || doc == NULL || doc->stopped || depth > 96) return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        doc->current_style = inherited_style;
        lxb_dom_text_t *txt = lxb_dom_interface_text(node);
        if (txt != NULL && txt->char_data.data.data != NULL) {
            tbb10ah_text(doc, txt->char_data.data.data, txt->char_data.data.length);
        }
        return;
    }

    bool is_h1 = false, is_h2 = false, is_h3 = false;
    bool is_p = false, is_li = false, is_br = false, is_a = false;
    bool is_script = false, is_style = false, is_title = false, is_pre = false;
    int link_id = 0;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        size_t nlen = 0;
        const lxb_char_t *name = lxb_dom_node_name(node, &nlen);
        inherited_style = tbb10bp_style_for_element(node, inherited_style, name, nlen);
        doc->current_style = inherited_style;

        is_h1 = tbb10ah_tag_eq(name, nlen, "h1");
        is_h2 = tbb10ah_tag_eq(name, nlen, "h2");
        is_h3 = tbb10ah_tag_eq(name, nlen, "h3");
        is_p = tbb10ah_tag_eq(name, nlen, "p");
        is_li = tbb10ah_tag_eq(name, nlen, "li");
        is_br = tbb10ah_tag_eq(name, nlen, "br");
        is_a = tbb10ah_tag_eq(name, nlen, "a");
        is_pre = tbb10ah_tag_eq(name, nlen, "pre");
        is_script = tbb10ah_tag_eq(name, nlen, "script");
        is_style = tbb10ah_tag_eq(name, nlen, "style");
        is_title = tbb10ah_tag_eq(name, nlen, "title");

        if (is_script || is_style || is_title) return;

        if (is_br) {
            tbb10ah_newline(doc, true);
            return;
        }

        if (is_h1) {
            tbb10ah_newline(doc, false);
            tbb10ah_puts(doc, "# ");
        }
        else if (is_h2) {
            tbb10ah_newline(doc, false);
            tbb10ah_puts(doc, "## ");
        }
        else if (is_h3) {
            tbb10ah_newline(doc, false);
            tbb10ah_puts(doc, "### ");
        }
        else if (is_p || is_pre) {
            tbb10ah_newline(doc, false);
        }
        else if (is_li) {
            tbb10ah_newline(doc, false);
            tbb10ah_puts(doc, "- ");
        }

        if (is_a) {
            lxb_dom_element_t *el = lxb_dom_interface_element(node);
            if (el != NULL) {
                size_t href_len = 0;
                const lxb_char_t *href = lxb_dom_element_get_attribute(el, (const lxb_char_t *) "href", 4, &href_len);
                if (href != NULL && href_len > 0) {
                    link_id = tbb10ah_add_link(doc, href, href_len);
                    if (link_id > 0) {
                        char mark[20];
                        snprintf(mark, sizeof(mark), "[%d] ", link_id);
                        tbb10ah_puts(doc, mark);
                    }
                }
            }
        }
    }

    for (lxb_dom_node_t *child = node->first_child; child != NULL && !doc->stopped; child = child->next) {
        tbb10ah_walk_dom(child, doc, depth + 1, inherited_style);
    }

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        if (is_h1 || is_h2 || is_h3 || is_p || is_li || is_pre) {
            tbb10ah_newline(doc, false);
        }
    }
}

static char *tbb10ah_load_file(const char *path, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (path == NULL || path[0] == '\0') return NULL;

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("[TBBROWSER10AH][ERR] stat fallo: %s\n", path);
        return NULL;
    }

    if (st.st_size <= 0 || st.st_size > TBB10AH_FILE_MAX) {
        printf("[TBBROWSER10AH][ERR] tamano no valido: %ld bytes, max=%d\n",
               (long) st.st_size, TBB10AH_FILE_MAX);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("[TBBROWSER10AH][ERR] fopen fallo: %s\n", path);
        return NULL;
    }

    char *buf = (char *) heap_caps_malloc((size_t) st.st_size + 1,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) buf = (char *) heap_caps_malloc((size_t) st.st_size + 1, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        fclose(f);
        printf("[TBBROWSER10AH][ERR] sin memoria para archivo\n");
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t) st.st_size, f);
    fclose(f);
    buf[rd] = '\0';

    if (out_len) *out_len = rd;
    printf("[TBBROWSER10AH] archivo cargado %s bytes=%u\n", path, (unsigned) rd);
    return buf;
}

static const char *tbb10ah_sample_html(void)
{
    return "<!doctype html><html><head><title>Arielo MiniPC OS</title></head>"
           "<body>"
           "<h1>Arielo MiniPC OS</h1>"
           "<h2>TactileBrowser 10AK</h2>"
           "<p>Navegador local interno con cabecera, enlaces y BACK.</p>"
           "<p>Lexbor esta integrado en firmware normal, sin ELF externo.</p>"
           "<ul>"
           "<li>Carga HTML desde SD, USB o root.</li>"
           "<li>Parsea DOM con Lexbor.</li>"
           "<li>Convierte el DOM a lineas de texto.</li>"
           "<li>Muestra paginas y permite subir/bajar.</li>"
           "</ul>"
           "<h2>Controles</h2>"
           "<p>n/ENTER baja pagina. p sube. j/k linea. b atras. l enlaces. m favorito. v favoritos. r recarga. q salir.</p>"
           "<p>Esta es una base prudente. Sin CSS ni imagenes. Navegacion local y rutas relativas basicas.</p>"
           "<p>Enlaces locales de prueba: <a href=\"/sdcard/test.html\">test SD</a> "
           "y <a href=\"/usb/test.html\">test USB</a>.</p>"
           "</body></html>";
}

static void tbb10ah_chomp(char *s);

static bool tbb10al_is_http_url(const char *s);
static bool tbb10al_is_https_url(const char *s);
static const char *tbb10au_strcasestr(const char *haystack, const char *needle);

/* -------------------------------------------------------------------------
 * 10BO - COOKIES LITE
 * -------------------------------------------------------------------------
 * Cookie jar pequeno en RAM. Objetivo prudente: comportarnos como el navegador
 * antiguo pero con continuidad de sesion entre peticiones, sin tocar User-Agent
 * ni Accept-Encoding. No ejecuta JS ni resuelve desafios; solo guarda y reenvia
 * cookies HTTP normales.
 */
typedef struct {
    bool used;
    bool host_only;
    bool secure;
    uint32_t age;
    char name[TBB10BO_COOKIE_NAME_CAP];
    char value[TBB10BO_COOKIE_VALUE_CAP];
    char domain[TBB10BO_COOKIE_DOMAIN_CAP];
    char path[TBB10BO_COOKIE_PATH_CAP];
} tbb10bo_cookie_t;

static tbb10bo_cookie_t *s_tbb10bo_cookies = NULL;
static uint32_t s_tbb10bo_cookie_age = 1;
static int s_tbb10bo_cookie_store_count = 0;

/* 10BN RAMSAFE:
 * El jar de cookies vive en PSRAM y se reserva solo al abrir/usar el navegador.
 * Nunca cae a RAM interna: si no hay PSRAM, cookies queda desactivado sin
 * comprometer USB Host, MSC ni WiFi. */
static bool tbb10bo_cookie_ensure_jar(void)
{
    if (s_tbb10bo_cookies != NULL) return true;
    size_t bytes = sizeof(tbb10bo_cookie_t) * TBB10BO_COOKIE_MAX;
    s_tbb10bo_cookies = (tbb10bo_cookie_t *)heap_caps_calloc(
        TBB10BO_COOKIE_MAX, sizeof(tbb10bo_cookie_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_tbb10bo_cookies == NULL) {
        printf("[TBBROWSER10BN][COOKIE] sin PSRAM para jar (%u bytes); cookies OFF\n",
               (unsigned)bytes);
        return false;
    }
    printf("[TBBROWSER10BN][COOKIE] jar PSRAM reservado: %u bytes\n",
           (unsigned)bytes);
    return true;
}

static char *tbb10bo_ltrim(char *s)
{
    if (s == NULL) return NULL;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    return s;
}

static void tbb10bo_rtrim(char *s)
{
    if (s == NULL) return;
    size_t n = strlen(s);
    while (n > 0) {
        unsigned char c = (unsigned char)s[n - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        s[--n] = '\0';
    }
}

static void tbb10bo_trim_inplace(char *s)
{
    if (s == NULL) return;
    char *p = tbb10bo_ltrim(s);
    if (p != s) memmove(s, p, strlen(p) + 1);
    tbb10bo_rtrim(s);
}

static void tbb10bo_tolower_inplace(char *s)
{
    if (s == NULL) return;
    for (int i = 0; s[i] != '\0'; i++) s[i] = (char)tolower((unsigned char)s[i]);
}

static bool tbb10bo_url_parts(const char *url, bool *is_https, char *host, size_t host_cap,
                              char *path, size_t path_cap)
{
    if (is_https) *is_https = false;
    if (host && host_cap) host[0] = '\0';
    if (path && path_cap) tbb10ah_safe_copy(path, path_cap, "/");
    if (url == NULL || !tbb10al_is_http_url(url)) return false;

    bool https = tbb10al_is_https_url(url);
    if (is_https) *is_https = https;

    const char *p = strstr(url, "://");
    if (p == NULL) return false;
    p += 3;
    const char *h0 = p;
    while (*p && *p != '/' && *p != '?' && *p != '#') p++;
    size_t hn = (size_t)(p - h0);
    if (hn == 0) return false;
    if (host != NULL && host_cap > 0) {
        if (hn >= host_cap) hn = host_cap - 1;
        memcpy(host, h0, hn);
        host[hn] = '\0';
        tbb10bo_tolower_inplace(host);
        char *colon = strchr(host, ':');
        if (colon != NULL) *colon = '\0';
    }

    if (path != NULL && path_cap > 0) {
        if (*p == '/') {
            const char *p0 = p;
            while (*p && *p != '?' && *p != '#') p++;
            size_t pn = (size_t)(p - p0);
            if (pn == 0) tbb10ah_safe_copy(path, path_cap, "/");
            else {
                if (pn >= path_cap) pn = path_cap - 1;
                memcpy(path, p0, pn);
                path[pn] = '\0';
            }
        } else {
            tbb10ah_safe_copy(path, path_cap, "/");
        }
    }
    return true;
}

static void tbb10bo_default_cookie_path(const char *req_path, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return;
    if (req_path == NULL || req_path[0] != '/') {
        tbb10ah_safe_copy(out, out_cap, "/");
        return;
    }
    tbb10ah_safe_copy(out, out_cap, req_path);
    char *q = strchr(out, '?');
    if (q != NULL) *q = '\0';
    char *last = strrchr(out, '/');
    if (last == NULL || last == out) tbb10ah_safe_copy(out, out_cap, "/");
    else *last = '\0';
}

static bool tbb10bo_domain_match(const char *host, const tbb10bo_cookie_t *ck)
{
    if (host == NULL || ck == NULL || !ck->used || ck->domain[0] == '\0') return false;
    if (ck->host_only) return strcasecmp(host, ck->domain) == 0;
    if (strcasecmp(host, ck->domain) == 0) return true;
    size_t hl = strlen(host);
    size_t dl = strlen(ck->domain);
    return (hl > dl && host[hl - dl - 1] == '.' && strcasecmp(host + hl - dl, ck->domain) == 0);
}

static bool tbb10bo_path_match(const char *req_path, const char *cookie_path)
{
    if (cookie_path == NULL || cookie_path[0] == '\0') cookie_path = "/";
    if (req_path == NULL || req_path[0] == '\0') req_path = "/";
    size_t cl = strlen(cookie_path);
    if (cl == 1 && cookie_path[0] == '/') return true;
    return strncmp(req_path, cookie_path, cl) == 0;
}

static void tbb10bo_cookie_clear_all(void)
{
    if (!tbb10bo_cookie_ensure_jar()) return;
    memset(s_tbb10bo_cookies, 0, sizeof(tbb10bo_cookie_t) * TBB10BO_COOKIE_MAX);
    s_tbb10bo_cookie_store_count = 0;
    s_tbb10bo_cookie_age++;
    printf("[TBBROWSER10BO][COOKIE] jar limpiado\n");
}

static int tbb10bo_cookie_count(void)
{
    if (s_tbb10bo_cookies == NULL) return 0;
    int n = 0;
    for (int i = 0; i < TBB10BO_COOKIE_MAX; i++) if (s_tbb10bo_cookies[i].used) n++;
    return n;
}

static int tbb10bo_find_cookie_slot(const char *domain, const char *path, const char *name)
{
    if (!tbb10bo_cookie_ensure_jar()) return -1;
    for (int i = 0; s_tbb10bo_cookies != NULL && i < TBB10BO_COOKIE_MAX; i++) {
        tbb10bo_cookie_t *ck = &s_tbb10bo_cookies[i];
        if (!ck->used) continue;
        if (strcasecmp(ck->domain, domain) == 0 && strcmp(ck->path, path) == 0 && strcmp(ck->name, name) == 0) return i;
    }
    return -1;
}

static int tbb10bo_pick_cookie_slot(void)
{
    if (!tbb10bo_cookie_ensure_jar()) return -1;
    int oldest = 0;
    uint32_t oldest_age = UINT32_MAX;
    for (int i = 0; i < TBB10BO_COOKIE_MAX; i++) {
        if (!s_tbb10bo_cookies[i].used) return i;
        if (s_tbb10bo_cookies[i].age < oldest_age) {
            oldest_age = s_tbb10bo_cookies[i].age;
            oldest = i;
        }
    }
    return oldest;
}

static bool tbb10bo_cookie_expire_attr(const char *attr)
{
    if (attr == NULL) return false;
    return (strncasecmp(attr, "max-age=0", 9) == 0 ||
            strncasecmp(attr, "max-age=-", 9) == 0 ||
            tbb10au_strcasestr(attr, "expires=thu, 01 jan 1970") != NULL ||
            tbb10au_strcasestr(attr, "expires=0") != NULL);
}

static void tbb10bo_cookie_store_one(const char *url, const char *set_cookie)
{
    if (url == NULL || set_cookie == NULL || set_cookie[0] == '\0') return;
    if (!tbb10bo_cookie_ensure_jar()) return;

    bool req_https = false;
    char req_host[TBB10BO_COOKIE_DOMAIN_CAP];
    char req_path[TBB10BO_COOKIE_PATH_CAP];
    if (!tbb10bo_url_parts(url, &req_https, req_host, sizeof(req_host), req_path, sizeof(req_path))) return;
    (void)req_https;

    char line[512];
    tbb10ah_safe_copy(line, sizeof(line), set_cookie);

    char *parts[16];
    int pc = 0;
    char *save = NULL;
    char *tok = strtok_r(line, ";", &save);
    while (tok != NULL && pc < 16) {
        parts[pc++] = tok;
        tok = strtok_r(NULL, ";", &save);
    }
    if (pc <= 0) return;

    char first[256];
    tbb10ah_safe_copy(first, sizeof(first), parts[0]);
    tbb10bo_trim_inplace(first);
    char *eq = strchr(first, '=');
    if (eq == NULL || eq == first) return;
    *eq = '\0';
    char *name = first;
    char *value = eq + 1;
    tbb10bo_trim_inplace(name);
    tbb10bo_trim_inplace(value);
    if (name[0] == '\0') return;

    char domain[TBB10BO_COOKIE_DOMAIN_CAP];
    char path[TBB10BO_COOKIE_PATH_CAP];
    bool host_only = true;
    bool secure = false;
    bool delete_cookie = false;
    tbb10ah_safe_copy(domain, sizeof(domain), req_host);
    tbb10bo_default_cookie_path(req_path, path, sizeof(path));

    for (int i = 1; i < pc; i++) {
        char attr[256];
        tbb10ah_safe_copy(attr, sizeof(attr), parts[i]);
        tbb10bo_trim_inplace(attr);
        if (attr[0] == '\0') continue;
        if (strcasecmp(attr, "secure") == 0) { secure = true; continue; }
        if (tbb10bo_cookie_expire_attr(attr)) { delete_cookie = true; continue; }
        if (strncasecmp(attr, "domain=", 7) == 0) {
            char *d = attr + 7;
            tbb10bo_trim_inplace(d);
            while (*d == '.') d++;
            if (*d != '\0') {
                tbb10ah_safe_copy(domain, sizeof(domain), d);
                tbb10bo_tolower_inplace(domain);
                host_only = false;
            }
        } else if (strncasecmp(attr, "path=", 5) == 0) {
            char *pa = attr + 5;
            tbb10bo_trim_inplace(pa);
            if (*pa == '/') tbb10ah_safe_copy(path, sizeof(path), pa);
        }
    }

    int slot = tbb10bo_find_cookie_slot(domain, path, name);
    if (delete_cookie || value[0] == '\0') {
        if (slot >= 0) {
            memset(&s_tbb10bo_cookies[slot], 0, sizeof(s_tbb10bo_cookies[slot]));
            printf("[TBBROWSER10BO][COOKIE] delete %s domain=%s path=%s\n", name, domain, path);
        }
        return;
    }

    if (slot < 0) slot = tbb10bo_pick_cookie_slot();
    tbb10bo_cookie_t *ck = &s_tbb10bo_cookies[slot];
    memset(ck, 0, sizeof(*ck));
    ck->used = true;
    ck->host_only = host_only;
    ck->secure = secure;
    ck->age = s_tbb10bo_cookie_age++;
    tbb10ah_safe_copy(ck->name, sizeof(ck->name), name);
    tbb10ah_safe_copy(ck->value, sizeof(ck->value), value);
    tbb10ah_safe_copy(ck->domain, sizeof(ck->domain), domain);
    tbb10ah_safe_copy(ck->path, sizeof(ck->path), path);
    s_tbb10bo_cookie_store_count++;
    printf("[TBBROWSER10BO][COOKIE] store %s domain=%s path=%s secure=%d total=%d\n",
           ck->name, ck->domain, ck->path, ck->secure ? 1 : 0, tbb10bo_cookie_count());
}

static void tbb10bo_cookie_store_lines_for_url(const char *url, const char *lines)
{
    if (url == NULL || lines == NULL || lines[0] == '\0') return;
    char tmp[TBB10BO_SETCOOKIE_CAP];
    tbb10ah_safe_copy(tmp, sizeof(tmp), lines);
    char *save = NULL;
    char *line = strtok_r(tmp, "\n", &save);
    while (line != NULL) {
        tbb10bo_trim_inplace(line);
        if (line[0] != '\0') tbb10bo_cookie_store_one(url, line);
        line = strtok_r(NULL, "\n", &save);
    }
}

static void tbb10bo_cookie_capture_append(char *dst, size_t dst_cap, size_t *dst_len, const char *value)
{
    if (dst == NULL || dst_cap == 0 || dst_len == NULL || value == NULL || value[0] == '\0') return;
    size_t len = *dst_len;
    if (len >= dst_cap) len = dst_cap - 1;
    size_t n = strlen(value);
    if (len + n + 2 >= dst_cap) n = (dst_cap > len + 2) ? (dst_cap - len - 2) : 0;
    if (n > 0) { memcpy(dst + len, value, n); len += n; }
    if (len + 1 < dst_cap) dst[len++] = '\n';
    dst[len] = '\0';
    *dst_len = len;
}

static bool tbb10bo_cookie_build_header(const char *url, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return false;
    if (!tbb10bo_cookie_ensure_jar()) { out[0] = '\0'; return false; }
    out[0] = '\0';
    bool https = false;
    char host[TBB10BO_COOKIE_DOMAIN_CAP];
    char path[TBB10BO_COOKIE_PATH_CAP];
    if (!tbb10bo_url_parts(url, &https, host, sizeof(host), path, sizeof(path))) return false;

    size_t pos = 0;
    int sent = 0;
    for (int i = 0; s_tbb10bo_cookies != NULL && i < TBB10BO_COOKIE_MAX; i++) {
        tbb10bo_cookie_t *ck = &s_tbb10bo_cookies[i];
        if (!ck->used) continue;
        if (ck->secure && !https) continue;
        if (!tbb10bo_domain_match(host, ck)) continue;
        if (!tbb10bo_path_match(path, ck->path)) continue;

        const char *sep = (sent > 0) ? "; " : "";
        size_t need = strlen(sep) + strlen(ck->name) + 1 + strlen(ck->value);
        if (pos + need + 1 >= out_cap) break;
        if (sep[0]) { strcpy(out + pos, sep); pos += strlen(sep); }
        strcpy(out + pos, ck->name); pos += strlen(ck->name);
        out[pos++] = '=';
        strcpy(out + pos, ck->value); pos += strlen(ck->value);
        out[pos] = '\0';
        sent++;
        ck->age = s_tbb10bo_cookie_age++;
    }
    return sent > 0;
}



/* 10BK_FIX2: canonicalizador de paginas internas.
 * Si por cualquier ruta llega "google.com/about:history" o
 * "https://.../about:bookmarks", lo convertimos de nuevo en about:...
 * antes de resolver como URL web. Esto blinda HOME/HIST/FAV.
 */
static bool tbb10bk_canonical_internal_url(const char *s, char *out, size_t out_cap)
{
    if (s == NULL || out == NULL || out_cap == 0) return false;

    char tmp[TBB10AH_PATH_CAP];
    tbb10ah_safe_copy(tmp, sizeof(tmp), s);
    tbb10ah_chomp(tmp);

    const char *p = tmp;
    if (strcmp(p, "home") == 0) {
        tbb10ah_safe_copy(out, out_cap, "about:home");
        return true;
    }

    if (strncmp(p, "about:", 6) != 0) {
        const char *q = strstr(p, "/about:");
        if (q != NULL) p = q + 1;
    }

    if (strcmp(p, "about:favorites") == 0 || strcmp(p, "about:favoritos") == 0) {
        tbb10ah_safe_copy(out, out_cap, "about:bookmarks");
        return true;
    }

    if (strcmp(p, "about:home") == 0 ||
        strcmp(p, "about:help") == 0 ||
        strcmp(p, "about:history") == 0 ||
        strcmp(p, "about:bookmarks") == 0 ||
        strcmp(p, "about:cookies") == 0 ||
        strcmp(p, "about:cookies:clear") == 0 ||
        strcmp(p, "about:posttest") == 0 ||
        strcmp(p, "about:files") == 0 ||
        strcmp(p, "about:archivos") == 0 ||
        strncmp(p, "about:files:", 12) == 0 ||
        strncmp(p, "about:archivos:", 15) == 0) {
        tbb10ah_safe_copy(out, out_cap, p);
        return true;
    }

    return false;
}

static bool tbb10ah_is_internal_url(const char *s)
{
    char canon[TBB10AH_PATH_CAP];
    return tbb10bk_canonical_internal_url(s, canon, sizeof(canon));
}

static void tbb10ah_html_append(char *dst, size_t cap, const char *src)
{
    tbb10ah_safe_append(dst, cap, src);
}

static void tbb10ah_html_escape_append(char *dst, size_t cap, const char *src)
{
    if (dst == NULL || cap == 0 || src == NULL) return;
    for (size_t i = 0; src[i] != '\0'; i++) {
        switch (src[i]) {
            case '&': tbb10ah_html_append(dst, cap, "&amp;"); break;
            case '<': tbb10ah_html_append(dst, cap, "&lt;"); break;
            case '>': tbb10ah_html_append(dst, cap, "&gt;"); break;
            case '"': tbb10ah_html_append(dst, cap, "&quot;"); break;
            default: {
                char tmp[2] = { src[i], '\0' };
                tbb10ah_html_append(dst, cap, tmp);
                break;
            }
        }
    }
}

static char *tbb10ah_alloc_html_buf(size_t cap)
{
    char *p = (char *) heap_caps_calloc(1, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = (char *) heap_caps_calloc(1, cap, MALLOC_CAP_8BIT);
    return p;
}

static void tbb10ah_chomp(char *s);

static char *tbb10ah_make_bookmarks_html(void)
{
    const size_t cap = 8192;
    char *html = tbb10ah_alloc_html_buf(cap);
    if (html == NULL) return NULL;

    tbb10ah_html_append(html, cap, "<!doctype html><html><head><title>Favoritos - TactileBrowser 10AL</title></head><body>");
    tbb10ah_html_append(html, cap, "<h1>TactileBrowser 10AL</h1><h2>Favoritos</h2>");
    tbb10ah_html_append(html, cap, "<p>Marcadores locales guardados en /root/tbbrowser_favs.txt.</p><ul>");

    FILE *f = fopen(TBB10AH_BOOKMARKS_FILE, "r");
    int count = 0;
    if (f != NULL) {
        char line[384];
        while (fgets(line, sizeof(line), f) != NULL && count < 40) {
            tbb10ah_chomp(line);
            if (line[0] == '\0') continue;
            char *sep = strchr(line, '|');
            const char *path = line;
            const char *title = line;
            if (sep != NULL) {
                *sep = '\0';
                title = sep + 1;
                if (title[0] == '\0') title = path;
            }
            tbb10ah_html_append(html, cap, "<li><a href=\"");
            tbb10ah_html_escape_append(html, cap, path);
            tbb10ah_html_append(html, cap, "\">");
            tbb10ah_html_escape_append(html, cap, title);
            tbb10ah_html_append(html, cap, "</a></li>");
            count++;
        }
        fclose(f);
    }

    if (count == 0) {
        tbb10ah_html_append(html, cap, "<li>No hay favoritos todavia. Abra una pagina y pulse m.</li>");
    }

    tbb10ah_html_append(html, cap, "</ul><p><a href=\"about:home\">Volver a inicio</a></p></body></html>");
    return html;
}


static bool tbb10aj_file_is_htmlish(const char *name)
{
    if (name == NULL || name[0] == '\0') return false;
    const char *dot = strrchr(name, '.');
    if (dot == NULL) return false;
    return (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0 || strcasecmp(dot, ".txt") == 0 ||
            strcasecmp(dot, ".md") == 0 || strcasecmp(dot, ".log") == 0);
}

static void tbb10aj_path_join(char *out, size_t out_cap, const char *dir, const char *name)
{
    if (out == NULL || out_cap == 0) return;
    out[0] = '\0';
    tbb10ah_safe_copy(out, out_cap, dir ? dir : "/");
    size_t n = strlen(out);
    if (n > 1 && out[n - 1] != '/') {
        tbb10ah_safe_append(out, out_cap, "/");
    }
    tbb10ah_safe_append(out, out_cap, name ? name : "");
}

static const char *tbb10aj_files_dir_from_about(const char *name)
{
    if (name == NULL) return "/sdcard";
    if (strncmp(name, "about:files:", 12) == 0) return name + 12;
    if (strncmp(name, "about:archivos:", 15) == 0) return name + 15;
    return "/sdcard";
}

static char *tbb10aj_make_files_html(const char *name)
{
    const size_t cap = 24576;
    char *html = tbb10ah_alloc_html_buf(cap);
    if (html == NULL) return NULL;

    const char *dir = tbb10aj_files_dir_from_about(name);
    if (dir == NULL || dir[0] == '\0') dir = "/sdcard";

    tbb10ah_html_append(html, cap, "<!doctype html><html><head><title>Archivos - TactileBrowser 10AL</title></head><body>");
    tbb10ah_html_append(html, cap, "<h1>TactileBrowser 10AL</h1><h2>Selector grafico de archivos</h2>");
    tbb10ah_html_append(html, cap, "<p>Directorio actual: ");
    tbb10ah_html_escape_append(html, cap, dir);
    tbb10ah_html_append(html, cap, "</p>");

    tbb10ah_html_append(html, cap, "<p>Raices: ");
    tbb10ah_html_append(html, cap, "<a href=\"about:files:/root\">/root</a> | ");
    tbb10ah_html_append(html, cap, "<a href=\"about:files:/sdcard\">/sdcard</a> | ");
    tbb10ah_html_append(html, cap, "<a href=\"about:files:/usb\">/usb</a></p>");

    /* Subir un nivel si no estamos en una raiz principal. */
    if (strcmp(dir, "/") != 0 && strcmp(dir, "/root") != 0 && strcmp(dir, "/sdcard") != 0 && strcmp(dir, "/usb") != 0) {
        char parent[TBB10AH_PATH_CAP];
        tbb10ah_safe_copy(parent, sizeof(parent), dir);
        char *slash = strrchr(parent, '/');
        if (slash != NULL && slash != parent) *slash = '\0';
        else tbb10ah_safe_copy(parent, sizeof(parent), "/");
        tbb10ah_html_append(html, cap, "<p><a href=\"about:files:");
        tbb10ah_html_escape_append(html, cap, parent);
        tbb10ah_html_append(html, cap, "\">.. subir</a></p>");
    }

    DIR *d = opendir(dir);
    if (d == NULL) {
        tbb10ah_html_append(html, cap, "<p>No pude abrir ese directorio.</p>");
        tbb10ah_html_append(html, cap, "<p><a href=\"about:home\">Volver a inicio</a></p></body></html>");
        return html;
    }

    tbb10ah_html_append(html, cap, "<ul>");
    int shown = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && shown < 80) {
        const char *nm = ent->d_name;
        if (nm == NULL || nm[0] == '\0') continue;
        if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;

        char full[TBB10AH_PATH_CAP];
        tbb10aj_path_join(full, sizeof(full), dir, nm);

        struct stat st;
        bool is_dir = false;
        if (stat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
        }

        if (is_dir) {
            tbb10ah_html_append(html, cap, "<li>[DIR] <a href=\"about:files:");
            tbb10ah_html_escape_append(html, cap, full);
            tbb10ah_html_append(html, cap, "\">");
            tbb10ah_html_escape_append(html, cap, nm);
            tbb10ah_html_append(html, cap, "/</a></li>");
            shown++;
        }
        else if (tbb10aj_file_is_htmlish(nm)) {
            tbb10ah_html_append(html, cap, "<li>[FILE] <a href=\"");
            tbb10ah_html_escape_append(html, cap, full);
            tbb10ah_html_append(html, cap, "\">");
            tbb10ah_html_escape_append(html, cap, nm);
            tbb10ah_html_append(html, cap, "</a></li>");
            shown++;
        }
    }
    closedir(d);
    if (shown == 0) {
        tbb10ah_html_append(html, cap, "<li>No hay archivos HTML/TXT visibles en esta carpeta.</li>");
    }
    tbb10ah_html_append(html, cap, "</ul><p><a href=\"about:home\">Volver a inicio</a></p></body></html>");
    return html;
}

static char *tbb10am_make_history_html(void)
{
    const size_t cap = TBB10AM_HISTORY_HTML_MAX;
    char *html = tbb10ah_alloc_html_buf(cap);
    if (html == NULL) return NULL;

    tbb10ah_html_append(html, cap, "<!doctype html><html><head><title>Historial - TactileBrowser 10AM</title></head><body>");
    tbb10ah_html_append(html, cap, "<h1>TactileBrowser 10AM</h1><h2>Historial de navegacion</h2>");
    tbb10ah_html_append(html, cap, "<p>Pagina actual: <b>");
    tbb10ah_html_escape_append(html, cap, s_tbb10am_current_snapshot);
    tbb10ah_html_append(html, cap, "</b></p>");

    tbb10ah_html_append(html, cap, "<h2>Atras</h2><ul>");
    if (s_tbb10am_hist_snapshot_count <= 0) {
        tbb10ah_html_append(html, cap, "<li>No hay paginas atras.</li>");
    }
    else {
        for (int i = s_tbb10am_hist_snapshot_count - 1; i >= 0; i--) {
            const char *path = s_tbb10am_hist_snapshot[i];
            if (path == NULL || path[0] == '\0') continue;
            tbb10ah_html_append(html, cap, "<li><a href=\"");
            tbb10ah_html_escape_append(html, cap, path);
            tbb10ah_html_append(html, cap, "\">");
            tbb10ah_html_escape_append(html, cap, path);
            tbb10ah_html_append(html, cap, "</a></li>");
        }
    }
    tbb10ah_html_append(html, cap, "</ul><h2>Adelante</h2><ul>");
    if (s_tbb10am_fwd_snapshot_count <= 0) {
        tbb10ah_html_append(html, cap, "<li>No hay paginas adelante.</li>");
    }
    else {
        for (int i = s_tbb10am_fwd_snapshot_count - 1; i >= 0; i--) {
            const char *path = s_tbb10am_fwd_snapshot[i];
            if (path == NULL || path[0] == '\0') continue;
            tbb10ah_html_append(html, cap, "<li><a href=\"");
            tbb10ah_html_escape_append(html, cap, path);
            tbb10ah_html_append(html, cap, "\">");
            tbb10ah_html_escape_append(html, cap, path);
            tbb10ah_html_append(html, cap, "</a></li>");
        }
    }
    tbb10ah_html_append(html, cap, "</ul><p><a href=\"about:home\">Volver a inicio</a></p></body></html>");
    return html;
}


static char *tbb10bo_make_cookies_html(bool do_clear)
{
    if (do_clear) tbb10bo_cookie_clear_all();

    const size_t cap = 12288;
    char *html = tbb10ah_alloc_html_buf(cap);
    if (html == NULL) return NULL;

    char tmp[256];
    tbb10ah_html_append(html, cap, "<!doctype html><html><head><title>Cookies Lite - TactileBrowser 10BP</title></head><body>");
    tbb10ah_html_append(html, cap, "<h1>TactileBrowser 10BP</h1><h2>Cookies Lite</h2>");
    snprintf(tmp, sizeof(tmp), "<p>Cookies activas en RAM: %d / %d. Set-Cookie guardadas desde arranque: %d.</p>",
             tbb10bo_cookie_count(), TBB10BO_COOKIE_MAX, s_tbb10bo_cookie_store_count);
    tbb10ah_html_append(html, cap, tmp);
    tbb10ah_html_append(html, cap, "<p><a href=\"about:cookies:clear\">Limpiar cookies</a> | <a href=\"about:home\">Volver a inicio</a></p><ul>");

    int shown = 0;
    for (int i = 0; i < TBB10BO_COOKIE_MAX; i++) {
        tbb10bo_cookie_t *ck = &s_tbb10bo_cookies[i];
        if (!ck->used) continue;
        tbb10ah_html_append(html, cap, "<li><b>");
        tbb10ah_html_escape_append(html, cap, ck->name);
        tbb10ah_html_append(html, cap, "</b> = ");
        char v[40];
        tbb10ah_safe_copy(v, sizeof(v), ck->value);
        if (strlen(ck->value) >= sizeof(v) - 1) {
            size_t n = strlen(v);
            if (n > 3) { v[n - 3] = '.'; v[n - 2] = '.'; v[n - 1] = '.'; }
        }
        tbb10ah_html_escape_append(html, cap, v);
        tbb10ah_html_append(html, cap, "<br>dominio: ");
        tbb10ah_html_escape_append(html, cap, ck->domain);
        tbb10ah_html_append(html, cap, ck->host_only ? " host-only" : " domain");
        tbb10ah_html_append(html, cap, "<br>path: ");
        tbb10ah_html_escape_append(html, cap, ck->path);
        if (ck->secure) tbb10ah_html_append(html, cap, " secure");
        tbb10ah_html_append(html, cap, "</li>");
        shown++;
    }
    if (shown == 0) tbb10ah_html_append(html, cap, "<li>No hay cookies guardadas todavia.</li>");
    tbb10ah_html_append(html, cap, "</ul><p>Prueba: abra una web, vuelva aqui y compruebe si llegaron Set-Cookie.</p></body></html>");
    return html;
}

static char *tbb10ah_make_internal_html(const char *name)
{
    if (name == NULL || strcmp(name, "home") == 0) name = "about:home";
    if (strcmp(name, "about:favorites") == 0 || strcmp(name, "about:favoritos") == 0) name = "about:bookmarks";
    if (strcmp(name, "about:archivos") == 0) name = "about:files";

    if (strcmp(name, "about:history") == 0) {
        return tbb10am_make_history_html();
    }

    if (strcmp(name, "about:bookmarks") == 0) {
        return tbb10ah_make_bookmarks_html();
    }
    if (strcmp(name, "about:cookies") == 0 || strcmp(name, "about:cookies:clear") == 0) {
        return tbb10bo_make_cookies_html(strcmp(name, "about:cookies:clear") == 0);
    }
    if (strcmp(name, "about:files") == 0 || strncmp(name, "about:files:", 12) == 0 ||
        strncmp(name, "about:archivos:", 15) == 0) {
        return tbb10aj_make_files_html(name);
    }

    if (strcmp(name, "about:posttest") == 0) {
        const size_t cap = 4096;
        char *html = tbb10ah_alloc_html_buf(cap);
        if (html == NULL) return NULL;
        tbb10ah_html_append(html, cap,
            "<!doctype html><html><head><title>POST local 10BQ</title></head><body>"
            "<h1>TactileBrowser 10BP STYLE LITE</h1>"
            "<h2>Autoprueba local POST</h2>"
            "<p>Esta prueba no usa Internet ni TLS. Escriba una palabra y envie el formulario.</p>"
            "<p><a href=\"form://search\">[FORM POST] ESCRIBIR Y ENVIAR</a></p>"
            "<form method=\"post\" action=\"http://127.0.0.1/tbb-post-echo\">"
            "<input type=\"hidden\" name=\"token\" value=\"10BQ_OK\">"
            "<input type=\"text\" name=\"mensaje\" value=\"\">"
            "<input type=\"submit\" value=\"Enviar POST\">"
            "</form>"
            "<p>Resultado esperado: una pagina POST RECIBIDO mostrando token=10BQ_OK y mensaje=...</p>"
            "<p><a href=\"about:home\">Volver a inicio</a></p>"
            "</body></html>");
        return html;
    }

    const size_t cap = 8192;
    char *html = tbb10ah_alloc_html_buf(cap);
    if (html == NULL) return NULL;

    if (strcmp(name, "about:help") == 0) {
        tbb10ah_html_append(html, cap,
            "<!doctype html><html><head><title>Ayuda - TactileBrowser 10AK</title></head><body>"
            "<h1>TactileBrowser 10AK</h1>"
            "<h2>Ayuda integrada</h2>"
            "<p>Este navegador ya tiene vida propia dentro de Arielo MiniPC OS: Lexbor integrado, GUI, historial, favoritos y paginas internas.</p>"
            "<ul>"
            "<li>n/ENTER/espacio: baja pagina.</li>"
            "<li>p: sube pagina.</li>"
            "<li>j/k: baja/sube una linea.</li>"
            "<li>1-9: abre enlaces visibles por numero.</li>"
            "<li>b: vuelve atras.</li>"
            "<li>r: recarga.</li>"
            "<li>m: guarda favorito.</li>"
            "<li>v: abre pagina interna de favoritos.</li>"
            "<li>u: barra de navegacion URL/ruta.</li>"
            "<li>h: overlay de ayuda.</li>"
            "<li>H: vuelve a la pagina inicio interna.</li>"
            "<li>q: salir al escritorio/consola.</li>"
            "</ul>"
            "<p><a href=\"about:home\">Volver a inicio</a></p>"
            "</body></html>");
        return html;
    }

    tbb10ah_html_append(html, cap,
        "<!doctype html><html><head><title>Arielo MiniPC OS - TactileBrowser Home</title></head><body>"
        "<h1>Arielo MiniPC OS</h1>"
        "<h2>TactileBrowser 10AP HTTPS + Buscador</h2>"
        "<p>Pagina inicial interna: navegador grafico con Lexbor integrado y salida a red HTTP/HTTPS.</p>"
        "<p>Motor Lexbor en firmware normal, memoria PSRAM-first, GUI propia, historial, favoritos, selectores y HTTP/HTTPS basico.</p>"
        "<h2>Paginas internas</h2>"
        "<ul>"
        "<li><a href=\"about:help\">Ayuda interna</a></li>"
        "<li><a href=\"about:bookmarks\">Favoritos guardados</a></li>"
        "</ul>"
        "<h2>Pruebas locales</h2>"
        "<ul>"
        "<li><a href=\"/sdcard/test_10af_gui.html\">Prueba grafica 10AF en SD</a></li>"
        "<li><a href=\"/sdcard/test_10ae_index.html\">Prueba favoritos 10AE en SD</a></li>"
        "<li><a href=\"/sdcard/test_10ad_index.html\">Prueba historial 10AD en SD</a></li>"
        "<li><a href=\"/sdcard/test.html\">test.html en SD</a></li>"
        "</ul>"
        "<p>Tecla H vuelve a esta pagina. u/URL abre barra de navegacion y buscador. Ejemplos: esp32 s3, ddg: waveshare, duckduckgo.</p>"
        "</body></html>");
    return html;
}

static int tbb10ah_build_doc_from_html(const char *label, const char *html, size_t html_len, tbb10ah_doc_t *out_doc)
{
    if (html == NULL) html = "";
    if (out_doc == NULL) return 1;

    tbb10ah_lexbor_memory_setup();
    tbb10ah_heap_print("before");

    printf("[TBBROWSER10AH] build begin label=%s len=%u\n", label ? label : "?", (unsigned) html_len);

    if (!tbb10ah_doc_alloc(out_doc)) {
        tbb10ah_heap_print("after_buferr");
        return 1;
    }

    lxb_html_document_t *doc = lxb_html_document_create();
    printf("[TBBROWSER10AH] doc=%p\n", (void *) doc);
    if (doc == NULL) {
        printf("[TBBROWSER10AH][ERR] create NULL\n");
        tbb10ah_doc_free(out_doc);
        tbb10ah_heap_print("after_null");
        return 1;
    }

    int64_t t0 = esp_timer_get_time();
    lxb_status_t st = lxb_html_document_parse(doc, (const lxb_char_t *) html, html_len);
    int64_t t1 = esp_timer_get_time();

    printf("[TBBROWSER10AH] parse status=%d time_us=%" PRId64 "\n", (int) st, (int64_t)(t1 - t0));

    if (st != LXB_STATUS_OK) {
        printf("[TBBROWSER10AH][ERR] parse failed\n");
        lxb_html_document_destroy(doc);
        tbb10ah_doc_free(out_doc);
        tbb10ah_heap_print("after_fail");
        return 1;
    }

    lxb_html_body_element_t *body = lxb_html_document_body_element(doc);
    lxb_dom_node_t *root = NULL;
    if (body != NULL) root = lxb_dom_interface_node(body);
    else root = lxb_dom_interface_node(doc);

    tbb10ah_extract_title_dom(lxb_dom_interface_node(doc), out_doc, false, 0);
    if (out_doc->title[0] == '\0' && label != NULL && label[0] != '\0') {
        tbb10ah_safe_copy(out_doc->title, sizeof(out_doc->title), label);
    }

    tbb10ah_walk_dom(root, out_doc, 0, 0);
    tbb10ah_trim_trailing(out_doc);

    printf("[TBBROWSER10AH] DOM->lineas OK title='%s' lines=%d links=%d stopped=%d\n", out_doc->title, out_doc->count, out_doc->link_count, out_doc->stopped ? 1 : 0);

    lxb_html_document_destroy(doc);
    printf("[TBBROWSER10AH] destroy OK\n");
    tbb10ah_heap_print("after_build");
    return 0;
}

static void __attribute__((unused)) tbb10ah_clear_screen(void)
{
    /* VTerm suele aceptar ANSI. Si algun terminal no lo interpreta, no afecta a la prueba. */
    printf("\033[2J\033[H");
}

static void tbb10ah_pause_enter(const char *msg)
{
    if (msg != NULL && msg[0] != '\0') printf("%s", msg);
    else printf("Pulse ENTER para continuar... ");
    fflush(stdout);
    char tmp[32];
    (void) fgets(tmp, sizeof(tmp), stdin);
}

static void __attribute__((unused)) tbb10ah_show_help(void)
{
    printf("\n[TBBROWSER10AH] Controles:\n");
    printf("  n o ENTER : bajar una pagina; flecha abajo = bajar linea\n");
    printf("  p         : subir una pagina; flecha arriba = subir linea\n");
    printf("  j/k       : bajar/subir una linea\n");
    printf("  l         : listar enlaces detectados\n");
    printf("  numero    : abrir enlace local [n]\n");
    printf("  o RUTA    : abrir archivo local, ejemplo o /sdcard/test.html\n");
    printf("  r         : recargar documento actual\n");
    printf("  b         : volver atras en historial\n");
    printf("  m         : guardar pagina actual como favorito en /root/tbbrowser_favs.txt\n");
    printf("  v         : ver/abrir favoritos guardados\n");
    printf("  g / G     : inicio / final\n");
    printf("  h o ?     : ayuda\n");
    printf("  q         : salir\n");
    tbb10ah_pause_enter("Pulse ENTER para volver al navegador... ");
}

static void __attribute__((unused)) tbb10ah_show_links(tbb10ah_doc_t *doc)
{
    printf("\n[TBBROWSER10AH] Enlaces detectados: %d\n", doc ? doc->link_count : 0);
    if (doc != NULL) {
        for (int i = 0; i < doc->link_count; i++) {
            char *href = tbb10ah_link_ptr(doc, i);
            printf("  [%d] %s\n", i + 1, href ? href : "?");
        }
    }
    tbb10ah_pause_enter("Pulse ENTER para volver al navegador... ");
}


static void tbb10ah_bookmark_sanitize_field(const char *in, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return;
    out[0] = '\0';
    if (in == NULL) return;

    size_t pos = 0;
    bool last_space = false;
    for (size_t i = 0; in[i] != '\0' && pos < out_cap - 1; i++) {
        unsigned char c = (unsigned char) in[i];
        if (c == '\r' || c == '\n' || c == '\t' || c == '|' || c < 32) {
            if (pos > 0 && !last_space && pos < out_cap - 1) {
                out[pos++] = ' ';
                last_space = true;
            }
            continue;
        }
        out[pos++] = (char) c;
        last_space = (c == ' ');
    }
    while (pos > 0 && out[pos - 1] == ' ') pos--;
    out[pos] = '\0';
}

static bool tbb10ah_bookmark_path_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;

    FILE *f = fopen(TBB10AH_BOOKMARKS_FILE, "r");
    if (f == NULL) return false;

    char line[TBB10AH_PATH_CAP + TBB10AH_TITLE_CAP + 16];
    bool found = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        tbb10ah_chomp(line);
        char *sep = strchr(line, '|');
        if (sep != NULL) *sep = '\0';
        if (strcmp(line, path) == 0) {
            found = true;
            break;
        }
    }

    fclose(f);
    return found;
}

static int tbb10ah_bookmark_add(const char *path, const char *title)
{
    if (path == NULL || path[0] == '\0' || strcmp(path, "sample") == 0) {
        printf("[TBBROWSER10AH] La pagina sample no se guarda como favorito. Abra un HTML real.\n");
        return 1;
    }

    char clean_path[TBB10AH_PATH_CAP];
    char clean_title[TBB10AH_TITLE_CAP];
    tbb10ah_bookmark_sanitize_field(path, clean_path, sizeof(clean_path));
    tbb10ah_bookmark_sanitize_field((title && title[0]) ? title : path, clean_title, sizeof(clean_title));

    if (clean_path[0] == '\0') {
        printf("[TBBROWSER10AH][ERR] ruta vacia, no guardo favorito\n");
        return 1;
    }

    if (tbb10ah_bookmark_path_exists(clean_path)) {
        printf("[TBBROWSER10AH] favorito ya existia: %s\n", clean_path);
        return 0;
    }

    FILE *f = fopen(TBB10AH_BOOKMARKS_FILE, "a");
    if (f == NULL) {
        printf("[TBBROWSER10AH][ERR] no pude abrir favoritos para escribir: %s\n", TBB10AH_BOOKMARKS_FILE);
        printf("[TBBROWSER10AH] Nota: compruebe que /root esta montado y con escritura.\n");
        return 1;
    }

    fprintf(f, "%s|%s\n", clean_path, clean_title[0] ? clean_title : clean_path);
    fclose(f);
    printf("[TBBROWSER10AH] favorito guardado: %s\n", clean_path);
    return 0;
}

static bool tbb10bk_label_is_bookmarks(const char *label)
{
    if (label == NULL) return false;
    return (strcmp(label, "about:bookmarks") == 0 ||
            strcmp(label, "about:favorites") == 0 ||
            strcmp(label, "about:favoritos") == 0);
}

static bool tbb10bk_href_is_deletable_fav(const char *href)
{
    if (href == NULL || href[0] == '\0') return false;
    if (strncmp(href, "about:", 6) == 0) return false;
    if (strncmp(href, "form://", 7) == 0) return false;
    return tbb10ah_bookmark_path_exists(href);
}

static int tbb10bk_bookmark_delete(const char *path)
{
    if (path == NULL || path[0] == '\0') return 1;

    char clean_path[TBB10AH_PATH_CAP];
    tbb10ah_bookmark_sanitize_field(path, clean_path, sizeof(clean_path));
    if (clean_path[0] == '\0') return 1;

    FILE *in = fopen(TBB10AH_BOOKMARKS_FILE, "r");
    if (in == NULL) {
        printf("[TBBROWSER10BK][ERR] no pude abrir favoritos: %s\n", TBB10AH_BOOKMARKS_FILE);
        return 1;
    }

    const char *tmp_path = "/root/tbbrowser_favs.tmp";
    FILE *out = fopen(tmp_path, "w");
    if (out == NULL) {
        fclose(in);
        printf("[TBBROWSER10BK][ERR] no pude crear temporal favoritos\n");
        return 1;
    }

    char line[TBB10AH_PATH_CAP + TBB10AH_TITLE_CAP + 16];
    bool removed = false;
    while (fgets(line, sizeof(line), in) != NULL) {
        char original[TBB10AH_PATH_CAP + TBB10AH_TITLE_CAP + 16];
        tbb10ah_safe_copy(original, sizeof(original), line);

        tbb10ah_chomp(line);
        char cmp[TBB10AH_PATH_CAP];
        char *sep = strchr(line, '|');
        if (sep != NULL) *sep = '\0';
        tbb10ah_bookmark_sanitize_field(line, cmp, sizeof(cmp));

        if (strcmp(cmp, clean_path) == 0) {
            removed = true;
            continue;
        }
        fputs(original, out);
    }

    fclose(in);
    fclose(out);

    if (!removed) {
        remove(tmp_path);
        printf("[TBBROWSER10BK] favorito no encontrado para borrar: %s\n", clean_path);
        return 1;
    }

    remove(TBB10AH_BOOKMARKS_FILE);
    if (rename(tmp_path, TBB10AH_BOOKMARKS_FILE) != 0) {
        printf("[TBBROWSER10BK][ERR] no pude reemplazar favoritos\n");
        return 1;
    }

    printf("[TBBROWSER10BK] favorito eliminado: %s\n", clean_path);
    return 0;
}

static bool __attribute__((unused)) tbb10ah_bookmarks_choose(char *out_path, size_t out_cap)
{
    if (out_path == NULL || out_cap == 0) return false;
    out_path[0] = '\0';

    FILE *f = fopen(TBB10AH_BOOKMARKS_FILE, "r");
    if (f == NULL) {
        printf("[TBBROWSER10AH] No hay favoritos todavia: %s\n", TBB10AH_BOOKMARKS_FILE);
        tbb10ah_pause_enter("ENTER para volver... ");
        return false;
    }

    printf("\n[TBBROWSER10AH] Favoritos guardados en %s\n", TBB10AH_BOOKMARKS_FILE);
    char line[TBB10AH_PATH_CAP + TBB10AH_TITLE_CAP + 16];
    int count = 0;
    while (fgets(line, sizeof(line), f) != NULL && count < TBB10AH_BOOKMARKS_MAX) {
        tbb10ah_chomp(line);
        if (line[0] == '\0') continue;
        char path[TBB10AH_PATH_CAP];
        char title[TBB10AH_TITLE_CAP];
        char *sep = strchr(line, '|');
        if (sep != NULL) {
            *sep = '\0';
            tbb10ah_safe_copy(path, sizeof(path), line);
            tbb10ah_safe_copy(title, sizeof(title), sep + 1);
        }
        else {
            tbb10ah_safe_copy(path, sizeof(path), line);
            tbb10ah_safe_copy(title, sizeof(title), line);
        }
        count++;
        printf("  [%d] %s\n      %s\n", count, title[0] ? title : path, path);
    }
    fclose(f);

    if (count <= 0) {
        printf("[TBBROWSER10AH] Lista de favoritos vacia.\n");
        tbb10ah_pause_enter("ENTER para volver... ");
        return false;
    }

    printf("Abrir favorito numero, o ENTER para cancelar: ");
    fflush(stdout);
    char cmd[32];
    if (fgets(cmd, sizeof(cmd), stdin) == NULL) return false;
    tbb10ah_chomp(cmd);
    if (cmd[0] == '\0') return false;

    int wanted = atoi(cmd);
    if (wanted < 1 || wanted > count) {
        printf("[TBBROWSER10AH] favorito fuera de rango: %d\n", wanted);
        tbb10ah_pause_enter("ENTER para volver... ");
        return false;
    }

    f = fopen(TBB10AH_BOOKMARKS_FILE, "r");
    if (f == NULL) return false;

    int idx = 0;
    bool ok = false;
    while (fgets(line, sizeof(line), f) != NULL && idx < TBB10AH_BOOKMARKS_MAX) {
        tbb10ah_chomp(line);
        if (line[0] == '\0') continue;
        idx++;
        if (idx == wanted) {
            char *sep = strchr(line, '|');
            if (sep != NULL) *sep = '\0';
            tbb10ah_safe_copy(out_path, out_cap, line);
            ok = (out_path[0] != '\0');
            break;
        }
    }
    fclose(f);
    return ok;
}

static bool __attribute__((unused)) tbb10ah_is_digits(const char *s)
{
    if (s == NULL || s[0] == '\0') return false;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\r' || s[i] == '\n') break;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

static void tbb10ah_chomp(char *s)
{
    if (s == NULL) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static bool tbb10ah_is_external_url(const char *s)
{
    if (s == NULL) return false;
    return strncmp(s, "mailto:", 7) == 0 || strncmp(s, "ftp://", 6) == 0;
}

static void tbb10ah_strip_fragment_query(char *s)
{
    if (s == NULL) return;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '#' || s[i] == '?') {
            s[i] = '\0';
            break;
        }
    }
}

static bool tbb10al_is_http_url(const char *s)
{
    if (s == NULL) return false;
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0;
}

static bool tbb10al_is_https_url(const char *s)
{
    return (s != NULL && strncmp(s, "https://", 8) == 0);
}

static bool tbb10ap_has_space(const char *s)
{
    if (s == NULL) return false;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == ' ' || s[i] == '\t') return true;
    }
    return false;
}

static bool tbb10ap_has_domain_dot(const char *s)
{
    if (s == NULL) return false;
    const char *p = strstr(s, "://");
    if (p != NULL) s = p + 3;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '/' || s[i] == '?' || s[i] == '#') break;
        if (s[i] == '.') return true;
    }
    return false;
}

static bool tbb10ap_is_ddg_main(const char *s)
{
    if (s == NULL) return false;
    while (*s == ' ' || *s == '\t') s++;
    if (strncasecmp(s, "https://", 8) == 0) s += 8;
    else if (strncasecmp(s, "http://", 7) == 0) s += 7;
    if (strncasecmp(s, "www.", 4) == 0) s += 4;

    if (strcasecmp(s, "duckduckgo") == 0) return true;
    if (strcasecmp(s, "duckduckgo.com") == 0) return true;
    if (strncasecmp(s, "duckduckgo.com/", 15) == 0) return true;
    return false;
}

static void tbb10ap_urlencode_query(const char *in, char *out, size_t out_cap)
{
    static const char hex[] = "0123456789ABCDEF";
    if (out == NULL || out_cap == 0) return;
    out[0] = '\0';
    if (in == NULL) return;

    size_t pos = 0;
    for (int i = 0; in[i] != '\0' && pos + 1 < out_cap; i++) {
        unsigned char c = (unsigned char) in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[pos++] = (char)c;
        }
        else if (c == ' ') {
            out[pos++] = '+';
        }
        else {
            if (pos + 3 >= out_cap) break;
            out[pos++] = '%';
            out[pos++] = hex[(c >> 4) & 0x0F];
            out[pos++] = hex[c & 0x0F];
        }
        out[pos] = '\0';
    }
}


/* -------------------------------------------------------------------------
 * 10BB - FORM-LITE: formularios GET simples estilo navegador antiguo HTML10D
 * -------------------------------------------------------------------------
 * Solo se guarda un formulario/campo activo por pagina. Es deliberadamente
 * simple y seguro: GET, input text/search o sin type. POST/login moderno se
 * ignora. La busqueda en formulario se dispara con ?texto o tocando [FORM].
 */
static const char *tbb10au_strcasestr(const char *haystack, const char *needle);
static char s_tbb10bb_form_action[TBB10AL_URL_CAP];
static char s_tbb10bb_form_input[64];
static bool s_tbb10bb_form_available = false;
static bool s_tbb10bb_prefer_form_once = false;

/* 10BP - POST FORMS LITE.
 * Conserva un solo formulario util por pagina, igual que FORM-LITE, pero
 * ahora recuerda method=POST y los input hidden sencillos del formulario.
 * El envio usa application/x-www-form-urlencoded y reutiliza Cookies Lite.
 */
#define TBB10BP_POST_BODY_CAP 1536
static bool s_tbb10bp_form_is_post = false;
static char *s_tbb10bp_form_hidden = NULL;
static bool s_tbb10bp_post_pending = false;
static char *s_tbb10bp_post_url = NULL;
static char *s_tbb10bp_post_body = NULL;

/* 10BO RAMSAFE: buffers POST exclusivamente en PSRAM y solo al necesitarlos. */
static bool tbb10bo_post_ensure_buffers(void)
{
    if (s_tbb10bp_form_hidden == NULL) {
        s_tbb10bp_form_hidden = (char *)heap_caps_calloc(1, TBB10BP_POST_BODY_CAP,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_tbb10bp_post_url == NULL) {
        s_tbb10bp_post_url = (char *)heap_caps_calloc(1, TBB10AL_URL_CAP,
                                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_tbb10bp_post_body == NULL) {
        s_tbb10bp_post_body = (char *)heap_caps_calloc(1, TBB10BP_POST_BODY_CAP,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_tbb10bp_form_hidden == NULL || s_tbb10bp_post_url == NULL || s_tbb10bp_post_body == NULL) {
        printf("[TBBROWSER10BO][POST] sin PSRAM para buffers; POST desactivado\n");
        return false;
    }
    return true;
}

#define TBB10BQ_LOCAL_POST_URL "http://127.0.0.1/tbb-post-echo"

static char *tbb10bq_make_local_post_echo_html(const char *post_body)
{
    const size_t cap = 4096;
    char *html = tbb10ah_alloc_html_buf(cap);
    if (html == NULL) return NULL;
    tbb10ah_html_append(html, cap,
        "<!doctype html><html><head><title>POST recibido 10BQ</title></head><body>"
        "<h1>TactileBrowser 10BP STYLE LITE</h1>"
        "<h2>POST RECIBIDO</h2>"
        "<p>Destino local interno: OK</p>"
        "<p>Cuerpo application/x-www-form-urlencoded:</p><p>");
    tbb10ah_html_escape_append(html, cap, post_body ? post_body : "");
    tbb10ah_html_append(html, cap,
        "</p><p>Si aparece token=10BQ_OK y mensaje=lo_escrito, la construccion POST esta validada.</p>"
        "<p><a href=\"about:posttest\">Repetir prueba</a> | <a href=\"about:home\">Inicio</a></p>"
        "</body></html>");
    return html;
}

static void tbb10bb_forms_reset(void)
{
    s_tbb10bb_form_action[0] = '\0';
    s_tbb10bb_form_input[0] = '\0';
    s_tbb10bb_form_available = false;
    s_tbb10bb_prefer_form_once = false;
    s_tbb10bp_form_is_post = false;
    if (s_tbb10bp_form_hidden != NULL) s_tbb10bp_form_hidden[0] = '\0';
}

static bool tbb10ap_make_ddg_search_url(const char *query, char *out, size_t out_cap)
{
    if (query == NULL || out == NULL || out_cap == 0) return false;
    while (*query == ' ' || *query == '\t') query++;
    if (*query == '\0') return false;

    char enc[TBB10AL_URL_CAP];
    tbb10ap_urlencode_query(query, enc, sizeof(enc));
    if (enc[0] == '\0') return false;

    tbb10ah_safe_copy(out, out_cap, "https://html.duckduckgo.com/html/?q=");
    tbb10ah_safe_append(out, out_cap, enc);
    return true;
}

/* 10BC: alternativas de busqueda sin intentar saltar pantallas anti-bot.
 * Si DDG devuelve desafio, mostramos enlaces de salida hacia paginas basicas.
 */
static bool tbb10bc_make_google_basic_search_url(const char *query, char *out, size_t out_cap)
{
    if (query == NULL || out == NULL || out_cap == 0) return false;
    while (*query == ' ' || *query == '\t') query++;
    if (*query == '\0') return false;

    char enc[TBB10AL_URL_CAP];
    tbb10ap_urlencode_query(query, enc, sizeof(enc));
    if (enc[0] == '\0') return false;

    tbb10ah_safe_copy(out, out_cap, "https://www.google.com/search?gbv=1&q=");
    tbb10ah_safe_append(out, out_cap, enc);
    return true;
}

static bool tbb10bc_make_wikipedia_search_url(const char *query, char *out, size_t out_cap)
{
    if (query == NULL || out == NULL || out_cap == 0) return false;
    while (*query == ' ' || *query == '\t') query++;
    if (*query == '\0') return false;

    char enc[TBB10AL_URL_CAP];
    tbb10ap_urlencode_query(query, enc, sizeof(enc));
    if (enc[0] == '\0') return false;

    tbb10ah_safe_copy(out, out_cap, "https://es.wikipedia.org/wiki/Especial:Buscar?search=");
    tbb10ah_safe_append(out, out_cap, enc);
    return true;
}




static bool tbb10bb_build_form_query_url(const char *query, char *out, size_t out_cap)
{
    if (query == NULL || out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    while (*query == ' ' || *query == '\t') query++;
    if (*query == '\0') return false;

    char enc[TBB10AL_URL_CAP];
    tbb10ap_urlencode_query(query, enc, sizeof(enc));
    if (enc[0] == '\0') return false;

    if (s_tbb10bb_form_available && s_tbb10bb_form_action[0] != '\0' && s_tbb10bb_form_input[0] != '\0') {
        /* Igual que el navegador viejo: si el formulario activo es Google Search,
         * no usamos el buscador Google moderno; redirigimos a DDG HTML sin JS.
         */
        if (tbb10au_strcasestr(s_tbb10bb_form_action, "google.") != NULL &&
            tbb10au_strcasestr(s_tbb10bb_form_action, "/search") != NULL &&
            strcasecmp(s_tbb10bb_form_input, "q") == 0) {
            printf("[TBBROWSER10BM_FIX1][FORM] Google form -> DDG HTML estable\n");
            return tbb10ap_make_ddg_search_url(query, out, out_cap);
        }

        if (s_tbb10bp_form_is_post) {
            if (!tbb10bo_post_ensure_buffers()) return false;
            s_tbb10bp_post_body[0] = '\0';
            if (s_tbb10bp_form_hidden[0] != '\0') {
                tbb10ah_safe_copy(s_tbb10bp_post_body, TBB10BP_POST_BODY_CAP, s_tbb10bp_form_hidden);
                tbb10ah_safe_append(s_tbb10bp_post_body, TBB10BP_POST_BODY_CAP, "&");
            }
            tbb10ah_safe_append(s_tbb10bp_post_body, TBB10BP_POST_BODY_CAP, s_tbb10bb_form_input);
            tbb10ah_safe_append(s_tbb10bp_post_body, TBB10BP_POST_BODY_CAP, "=");
            tbb10ah_safe_append(s_tbb10bp_post_body, TBB10BP_POST_BODY_CAP, enc);
            tbb10ah_safe_copy(s_tbb10bp_post_url, TBB10AL_URL_CAP, s_tbb10bb_form_action);
            s_tbb10bp_post_pending = true;
            tbb10ah_safe_copy(out, out_cap, s_tbb10bb_form_action);
            printf("[TBBROWSER10BP][FORM] POST url=%s body_bytes=%u\n",
                   s_tbb10bp_post_url, (unsigned)strlen(s_tbb10bp_post_body));
            return out[0] != '\0';
        }

        const char *sep = (strchr(s_tbb10bb_form_action, '?') != NULL) ? "&" : "?";
        tbb10ah_safe_copy(out, out_cap, s_tbb10bb_form_action);
        tbb10ah_safe_append(out, out_cap, sep);
        tbb10ah_safe_append(out, out_cap, s_tbb10bb_form_input);
        tbb10ah_safe_append(out, out_cap, "=");
        tbb10ah_safe_append(out, out_cap, enc);
        printf("[TBBROWSER10BB][FORM] GET: %s\n", out);
        return out[0] != '\0';
    }

    return tbb10ap_make_ddg_search_url(query, out, out_cap);
}

static bool tbb10ap_normalize_user_url_or_search(const char *input, char *out, size_t out_cap)
{
    if (input == NULL || out == NULL || out_cap == 0) return false;
    out[0] = '\0';

    char tmp[TBB10AL_URL_CAP];
    tbb10ah_safe_copy(tmp, sizeof(tmp), input);
    tbb10ah_chomp(tmp);
    if (tmp[0] == '\0') return false;

    /* 10AX: politica antigua para Google/dominios simples.
     * No forzamos google.com a https://www.google.com/ porque esa ruta
     * moderna devuelve muy poco texto util en modo sin JavaScript. Dejamos
     * que el servidor haga su redirect como en el navegador HTML10D viejo.
     */

    if (s_tbb10bb_prefer_form_once && s_tbb10bb_form_available &&
        !tbb10al_is_http_url(tmp) && tmp[0] != '/' && !tbb10ah_is_internal_url(tmp) &&
        (tbb10ap_has_space(tmp) || !tbb10ap_has_domain_dot(tmp))) {
        return tbb10bb_build_form_query_url(tmp, out, out_cap);
    }

    if (strncasecmp(tmp, "form:", 5) == 0) {
        return tbb10bb_build_form_query_url(tmp + 5, out, out_cap);
    }
    if (strncasecmp(tmp, "f:", 2) == 0) {
        return tbb10bb_build_form_query_url(tmp + 2, out, out_cap);
    }

    if ((tmp[0] == 'g' || tmp[0] == 'G') && tmp[1] == ' ' && tmp[2] != '\0') {
        return tbb10ap_make_ddg_search_url(tmp + 2, out, out_cap);
    }

    if (tbb10ap_is_ddg_main(tmp)) {
        tbb10ah_safe_copy(out, out_cap, "https://html.duckduckgo.com/html/");
        return true;
    }

    if (strncasecmp(tmp, "ddg:", 4) == 0) {
        return tbb10ap_make_ddg_search_url(tmp + 4, out, out_cap);
    }
    if (strncasecmp(tmp, "buscar ", 7) == 0) {
        return tbb10ap_make_ddg_search_url(tmp + 7, out, out_cap);
    }
    if (strncasecmp(tmp, "gb:", 3) == 0) {
        return tbb10bc_make_google_basic_search_url(tmp + 3, out, out_cap);
    }
    if (strncasecmp(tmp, "google:", 7) == 0) {
        return tbb10bc_make_google_basic_search_url(tmp + 7, out, out_cap);
    }
    if (strncasecmp(tmp, "wiki:", 5) == 0) {
        return tbb10bc_make_wikipedia_search_url(tmp + 5, out, out_cap);
    }
    if (tmp[0] == '?' && tmp[1] != '\0') {
        return tbb10bb_build_form_query_url(tmp + 1, out, out_cap);
    }

    if (tbb10al_is_http_url(tmp) || tmp[0] == '/' || tbb10ah_is_internal_url(tmp)) {
        tbb10ah_safe_copy(out, out_cap, tmp);
        return true;
    }

    /* Si no parece dominio/ruta, tratalo como busqueda tipo navegador viejo. */
    if (tbb10ap_has_space(tmp) || !tbb10ap_has_domain_dot(tmp)) {
        return tbb10ap_make_ddg_search_url(tmp, out, out_cap);
    }

    /* 10AX: politica clasica del navegador viejo: dominio simple -> HTTP.
     * Muchos servidores devuelven una version mas limpia/redirect manejable
     * cuando se entra por http://dominio. Para HTTPS directo, escribir
     * explicitamente https://dominio.
     */
    tbb10ah_safe_copy(out, out_cap, "http://");
    tbb10ah_safe_append(out, out_cap, tmp);
    return true;
}

static void tbb10al_strip_fragment_only(char *s)
{
    if (s == NULL) return;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '#') { s[i] = '\0'; break; }
    }
}

typedef struct {
    char *buf;
    size_t cap;
    size_t len;
    bool truncated;
    char *location;
    size_t location_cap;
    char *set_cookie_buf;
    size_t set_cookie_cap;
    size_t set_cookie_len;
} tbb10al_http_accum_t;

#define TBB10AU_REDIRECT_MAX 6
static char s_tbb10au_effective_url[TBB10AL_URL_CAP];
static bool tbb10al_http_origin(const char *url, char *out, size_t out_cap);

static char *tbb10al_make_error_html(const char *url, const char *msg)
{
    size_t cap = 4096;
    char *html = tbb10ah_alloc_html_buf(cap);
    if (html == NULL) return NULL;
    tbb10ah_html_append(html, cap, "<!doctype html><html><head><title>Error de red - TactileBrowser 10BP</title></head><body>");
    tbb10ah_html_append(html, cap, "<h1>TactileBrowser 10BP</h1><h2>Error de red</h2><p>URL: ");
    tbb10ah_html_escape_append(html, cap, url ? url : "");
    tbb10ah_html_append(html, cap, "</p><p>");
    tbb10ah_html_escape_append(html, cap, msg ? msg : "error desconocido");
    tbb10ah_html_append(html, cap, "</p><ul><li>HTTP, HTTPS y buscador DuckDuckGo HTML activos.</li><li>Algunas webs modernas pueden requerir JavaScript/CSS que este navegador no ejecuta.</li><li>Pruebe https://example.com/ o busque con ddg: esp32. DuckDuckGo HTML no requiere JavaScript.</li></ul><p><a href=\"about:home\">Volver a inicio</a></p></body></html>");
    return html;
}

static bool tbb10al_wait_wifi(char *err, size_t err_cap)
{
    if (minipc_wifi_is_connected()) return true;
    printf("[TBBROWSER10AO][NET] WiFi no conectada, intento init...\n");
    (void) minipc_wifi_init_appmain();
    for (int i = 0; i < 80; i++) {
        if (minipc_wifi_is_connected()) return true;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (err != NULL && err_cap > 0) {
        tbb10ah_safe_copy(err, err_cap, "WiFi no conectada. Conecte WiFi desde el escritorio antes de navegar.");
    }
    return false;
}

static esp_err_t tbb10al_http_event(esp_http_client_event_t *evt)
{
    if (evt == NULL) return ESP_OK;
    tbb10al_http_accum_t *acc = (tbb10al_http_accum_t *) evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_HEADER && acc != NULL &&
        acc->location != NULL && acc->location_cap > 0 &&
        evt->header_key != NULL && evt->header_value != NULL &&
        strcasecmp(evt->header_key, "Location") == 0) {
        tbb10ah_safe_copy(acc->location, acc->location_cap, evt->header_value);
        printf("[TBBROWSER10AW][NET] Location: %s\n", acc->location);
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_HEADER && acc != NULL &&
        evt->header_key != NULL && evt->header_value != NULL &&
        strcasecmp(evt->header_key, "Set-Cookie") == 0) {
        tbb10bo_cookie_capture_append(acc->set_cookie_buf, acc->set_cookie_cap,
                                      &acc->set_cookie_len, evt->header_value);
        printf("[TBBROWSER10BO][COOKIE] Set-Cookie recibido len=%u\n",
               (unsigned)strlen(evt->header_value));
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && acc != NULL && evt->data != NULL && evt->data_len > 0) {
        if (acc->buf == NULL || acc->cap == 0) return ESP_OK;
        size_t room = (acc->len + 1 < acc->cap) ? (acc->cap - acc->len - 1) : 0;
        size_t n = (size_t) evt->data_len;
        if (n > room) { n = room; acc->truncated = true; }
        if (n > 0) { memcpy(acc->buf + acc->len, evt->data, n); acc->len += n; acc->buf[acc->len] = '\0'; }
    }
    return ESP_OK;
}


static bool tbb10au_status_is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

static const char *tbb10au_strcasestr(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL) return NULL;
    size_t nl = strlen(needle);
    if (nl == 0) return haystack;

    /* 10AX_FIX2:
     * Esta funcion puede escanear HTML grande buscando </script>, </style>,
     * Location, enlaces, etc. Si la pagina trae mucho JS/CSS, hacerlo todo
     * seguido dentro de breezy_repl puede disparar el task watchdog.
     * Metemos una cesion muy ligera cada pocos KB.
     */
    uint32_t spin = 0;
    for (const char *p = haystack; *p != '\0'; p++) {
        if ((++spin & 0x0FFFu) == 0) {
            vTaskDelay(1);
        }
        if (strncasecmp(p, needle, nl) == 0) return p;
    }
    return NULL;
}

static int tbb10au_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void tbb10au_percent_decode_ascii(char *dst, size_t dst_cap, const char *src)
{
    if (dst == NULL || dst_cap == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;
    size_t pos = 0;
    for (size_t i = 0; src[i] != '\0' && pos + 1 < dst_cap; i++) {
        char c = src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && src[i + 1] != '\0' && src[i + 2] != '\0') {
            int h1 = tbb10au_hexval(src[i + 1]);
            int h2 = tbb10au_hexval(src[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                c = (char)((h1 << 4) | h2);
                i += 2;
            }
        }
        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
        dst[pos++] = c;
    }
    dst[pos] = '\0';
}

static void tbb10au_html_entity_decode_ascii(char *dst, size_t dst_cap, const char *src)
{
    if (dst == NULL || dst_cap == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;
    size_t pos = 0;
    for (size_t i = 0; src[i] != '\0' && pos + 1 < dst_cap; i++) {
        char c = src[i];
        if (c == '&') {
            if (strncmp(src + i, "&amp;", 5) == 0) { c = '&'; i += 4; }
            else if (strncmp(src + i, "&lt;", 4) == 0) { c = '<'; i += 3; }
            else if (strncmp(src + i, "&gt;", 4) == 0) { c = '>'; i += 3; }
            else if (strncmp(src + i, "&quot;", 6) == 0) { c = '"'; i += 5; }
            else if (strncmp(src + i, "&#39;", 5) == 0) { c = '\''; i += 4; }
        }
        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
        dst[pos++] = c;
    }
    dst[pos] = '\0';
}

static bool tbb10au_extract_query_param(const char *url, const char *key, char *out, size_t out_cap)
{
    if (url == NULL || key == NULL || out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *q = strchr(url, '?');
    if (q == NULL) q = strchr(url, '&');
    if (q == NULL) return false;
    while (*q != '\0') {
        if (*q == '?' || *q == '&') q++;
        if (strncasecmp(q, key, klen) == 0 && q[klen] == '=') {
            const char *v = q + klen + 1;
            char raw[TBB10AL_URL_CAP];
            size_t pos = 0;
            while (*v != '\0' && *v != '&' && *v != '#' && pos + 1 < sizeof(raw)) raw[pos++] = *v++;
            raw[pos] = '\0';
            tbb10au_percent_decode_ascii(out, out_cap, raw);
            tbb10ah_chomp(out);
            return out[0] != '\0';
        }
        const char *amp = strchr(q, '&');
        if (amp == NULL) break;
        q = amp;
    }
    return false;
}

static bool tbb10au_unwrap_google_or_ddg_url(const char *in, char *out, size_t out_cap)
{
    if (in == NULL || out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    char q[TBB10AL_URL_CAP];
    if (strstr(in, "google.") != NULL && strstr(in, "/url?") != NULL &&
        tbb10au_extract_query_param(in, "q", q, sizeof(q)) && tbb10al_is_http_url(q)) {
        tbb10ah_safe_copy(out, out_cap, q);
        return true;
    }
    if (strstr(in, "duckduckgo.com") != NULL &&
        tbb10au_extract_query_param(in, "uddg", q, sizeof(q)) && tbb10al_is_http_url(q)) {
        tbb10ah_safe_copy(out, out_cap, q);
        return true;
    }
    return false;
}

static bool tbb10au_page_looks_like_redirect(const char *html)
{
    if (html == NULL || html[0] == '\0') return false;
    return (strstr(html, "301 Moved") != NULL || strstr(html, "302 Found") != NULL ||
            strstr(html, "303 See Other") != NULL || strstr(html, "307 Temporary Redirect") != NULL ||
            strstr(html, "308 Permanent Redirect") != NULL || strstr(html, "Moved Permanently") != NULL ||
            strstr(html, "The document has moved") != NULL || strstr(html, "This document has moved") != NULL ||
            strstr(html, "has moved") != NULL);
}

static bool tbb10au_resolve_url_like_old(const char *base, const char *href, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    if (href == NULL || href[0] == '\0') return false;

    char tmp[TBB10AL_URL_CAP];
    tbb10ah_safe_copy(tmp, sizeof(tmp), href);
    tbb10ah_chomp(tmp);
    if (tmp[0] == '\0') return false;
    if (tmp[0] == '#' || strncasecmp(tmp, "javascript:", 11) == 0 ||
        strncasecmp(tmp, "mailto:", 7) == 0 || strncasecmp(tmp, "tel:", 4) == 0 ||
        strncasecmp(tmp, "data:", 5) == 0) return false;

    char unwrapped[TBB10AL_URL_CAP];
    if (tbb10au_unwrap_google_or_ddg_url(tmp, unwrapped, sizeof(unwrapped))) {
        tbb10ah_safe_copy(out, out_cap, unwrapped);
        return true;
    }

    if (tbb10al_is_http_url(tmp)) { tbb10ah_safe_copy(out, out_cap, tmp); return true; }
    if (base == NULL || !tbb10al_is_http_url(base)) return false;

    if (strncmp(tmp, "//", 2) == 0) {
        tbb10ah_safe_copy(out, out_cap, (strncmp(base, "https://", 8) == 0) ? "https:" : "http:");
        tbb10ah_safe_append(out, out_cap, tmp);
        return true;
    }

    char origin[TBB10AL_URL_CAP];
    if (!tbb10al_http_origin(base, origin, sizeof(origin))) return false;
    if (tmp[0] == '/') {
        tbb10ah_safe_copy(out, out_cap, origin);
        tbb10ah_safe_append(out, out_cap, tmp);
        return true;
    }

    char dir[TBB10AL_URL_CAP];
    tbb10ah_safe_copy(dir, sizeof(dir), base);
    char *scheme = strstr(dir, "://");
    char *last = NULL;
    if (scheme != NULL) {
        char *path = strchr(scheme + 3, '/');
        if (path != NULL) last = strrchr(path, '/');
    }
    if (last != NULL) last[1] = '\0';
    else { tbb10ah_safe_copy(dir, sizeof(dir), origin); tbb10ah_safe_append(dir, sizeof(dir), "/"); }
    tbb10ah_safe_copy(out, out_cap, dir);
    tbb10ah_safe_append(out, out_cap, tmp);
    return true;
}

static bool tbb10au_first_href_from_redirect_html(const char *base, const char *html, char *out, size_t out_cap)
{
    if (html == NULL || out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    const char *p = html;
    int scanned = 0;
    while ((p = tbb10au_strcasestr(p, "href")) != NULL && scanned++ < 16) {
        const char *q = p + 4;
        while (*q && isspace((unsigned char)*q)) q++;
        if (*q != '=') { p += 4; continue; }
        q++;
        while (*q && isspace((unsigned char)*q)) q++;
        char quote = 0;
        if (*q == '"' || *q == '\'') quote = *q++;
        char raw[TBB10AL_URL_CAP];
        size_t pos = 0;
        while (*q && pos + 1 < sizeof(raw)) {
            if (quote) { if (*q == quote) break; }
            else { if (isspace((unsigned char)*q) || *q == '>') break; }
            raw[pos++] = *q++;
        }
        raw[pos] = '\0';
        char dec[TBB10AL_URL_CAP];
        tbb10au_html_entity_decode_ascii(dec, sizeof(dec), raw);
        if (tbb10au_resolve_url_like_old(base, dec, out, out_cap)) return true;
        p += 4;
    }
    return false;
}


/* -------------------------------------------------------------------------
 * 10BF - JS-LITE / DOM-LITE de navegacion
 * -------------------------------------------------------------------------
 * No ejecuta JavaScript general. Solo interpreta acciones directas de
 * navegacion que muchas paginas simples usan para redirigir:
 *   - <meta http-equiv="refresh" content="0; url=...">
 *   - window.location = "..."
 *   - location.href = "..."
 *   - document.location = "..."
 *   - location.replace("...") / location.assign("...")
 * Todo queda limitado por TBB10AU_REDIRECT_MAX y resuelto con la misma logica
 * segura del navegador viejo, evitando recursividad y buffers grandes en pila.
 */

static bool tbb10bf_tag_attr_value(const char *tag, const char *end,
                                   const char *attr, char *out, size_t out_cap)
{
    if (tag == NULL || end == NULL || attr == NULL || out == NULL || out_cap == 0 || end <= tag) return false;
    out[0] = '\0';
    size_t alen = strlen(attr);
    const char *p = tag;
    while (p < end && *p != '\0') {
        if (strncasecmp(p, attr, alen) == 0) {
            char before = (p == tag) ? ' ' : p[-1];
            char after = (p + alen < end) ? p[alen] : ' ';
            if ((isspace((unsigned char)before) || before == '<') &&
                (isspace((unsigned char)after) || after == '=' || after == '/' || after == '>')) {
                const char *q = p + alen;
                while (q < end && isspace((unsigned char)*q)) q++;
                if (q >= end || *q != '=') { p += alen; continue; }
                q++;
                while (q < end && isspace((unsigned char)*q)) q++;
                char quote = 0;
                if (q < end && (*q == '\'' || *q == '"')) quote = *q++;
                size_t pos = 0;
                while (q < end && *q != '\0' && pos + 1 < out_cap) {
                    if (quote) { if (*q == quote) break; }
                    else { if (isspace((unsigned char)*q) || *q == '>' || *q == '/') break; }
                    out[pos++] = *q++;
                }
                out[pos] = '\0';
                return out[0] != '\0';
            }
        }
        p++;
    }
    return false;
}

static void tbb10bf_js_unescape_ascii(char *dst, size_t dst_cap, const char *src)
{
    if (dst == NULL || dst_cap == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;
    size_t pos = 0;
    for (size_t i = 0; src[i] != '\0' && pos + 1 < dst_cap; i++) {
        char c = src[i];
        if (c == '\\' && src[i + 1] != '\0') {
            char n = src[i + 1];
            if (n == '/' || n == '\\' || n == '\'' || n == '"' || n == '&' || n == '=') {
                c = n;
                i++;
            }
            else if (n == 'u' && src[i + 2] && src[i + 3] && src[i + 4] && src[i + 5]) {
                int h1 = tbb10au_hexval(src[i + 2]);
                int h2 = tbb10au_hexval(src[i + 3]);
                int h3 = tbb10au_hexval(src[i + 4]);
                int h4 = tbb10au_hexval(src[i + 5]);
                if (h1 >= 0 && h2 >= 0 && h3 >= 0 && h4 >= 0) {
                    int cp = (h1 << 12) | (h2 << 8) | (h3 << 4) | h4;
                    if (cp >= 32 && cp <= 126) c = (char)cp;
                    else c = '?';
                    i += 5;
                }
            }
        }
        if ((unsigned char)c < 32) c = ' ';
        dst[pos++] = c;
    }
    dst[pos] = '\0';
}

static bool tbb10bf_finish_nav_url(const char *base, const char *raw, char *out, size_t out_cap)
{
    if (raw == NULL || out == NULL || out_cap == 0) return false;
    char jsdec[TBB10AL_URL_CAP];
    char entdec[TBB10AL_URL_CAP];
    tbb10bf_js_unescape_ascii(jsdec, sizeof(jsdec), raw);
    tbb10au_html_entity_decode_ascii(entdec, sizeof(entdec), jsdec);
    tbb10ah_chomp(entdec);
    if (entdec[0] == '\0') return false;
    if (strncasecmp(entdec, "javascript:", 11) == 0) return false;
    if (strncasecmp(entdec, "mailto:", 7) == 0) return false;
    if (entdec[0] == '#') return false;
    return tbb10au_resolve_url_like_old(base, entdec, out, out_cap);
}

static bool tbb10bf_extract_meta_refresh_url(const char *base, const char *html, char *out, size_t out_cap)
{
    if (base == NULL || html == NULL || out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    const char *p = html;
    int scanned = 0;
    while ((p = tbb10au_strcasestr(p, "<meta")) != NULL && scanned++ < 32) {
        const char *end = strchr(p, '>');
        if (end == NULL) break;
        char http_equiv[64];
        char content[256];
        http_equiv[0] = '\0';
        content[0] = '\0';
        (void)tbb10bf_tag_attr_value(p, end, "http-equiv", http_equiv, sizeof(http_equiv));
        (void)tbb10bf_tag_attr_value(p, end, "content", content, sizeof(content));
        if (content[0] != '\0') {
            const char *u = tbb10au_strcasestr(content, "url=");
            bool is_refresh = (http_equiv[0] == '\0') || (tbb10au_strcasestr(http_equiv, "refresh") != NULL);
            if (u != NULL && is_refresh) {
                u += 4;
                while (*u == ' ' || *u == '\t' || *u == '\'' || *u == '"') u++;
                char raw[TBB10AL_URL_CAP];
                size_t pos = 0;
                while (*u != '\0' && pos + 1 < sizeof(raw)) {
                    if (*u == '\'' || *u == '"' || *u == ';') break;
                    raw[pos++] = *u++;
                }
                raw[pos] = '\0';
                if (tbb10bf_finish_nav_url(base, raw, out, out_cap)) return true;
            }
        }
        p = end + 1;
    }
    return false;
}

static bool tbb10bf_capture_quoted_after(const char *start, int max_scan, char *raw, size_t raw_cap)
{
    if (start == NULL || raw == NULL || raw_cap == 0) return false;
    raw[0] = '\0';
    const char *p = start;
    int guard = 0;
    while (*p != '\0' && guard++ < max_scan) {
        if (*p == '\'' || *p == '"') {
            char quote = *p++;
            size_t pos = 0;
            while (*p != '\0' && pos + 1 < raw_cap) {
                if (*p == quote) break;
                if (*p == '\\' && p[1] != '\0') {
                    raw[pos++] = *p++;
                    if (pos + 1 >= raw_cap) break;
                    raw[pos++] = *p++;
                    continue;
                }
                raw[pos++] = *p++;
            }
            raw[pos] = '\0';
            return raw[0] != '\0';
        }
        if (*p == ';' || *p == '<' || *p == '>') break;
        p++;
    }
    return false;
}

static bool tbb10bf_extract_js_location_url(const char *base, const char *html, char *out, size_t out_cap)
{
    if (base == NULL || html == NULL || out == NULL || out_cap == 0) return false;
    out[0] = '\0';

    static const char *const call_patterns[] = {
        "window.location.replace", "location.replace", "document.location.replace",
        "window.location.assign",  "location.assign",  "document.location.assign",
        NULL
    };
    static const char *const assign_patterns[] = {
        "window.location.href", "location.href", "document.location.href",
        "window.location",      "document.location",
        NULL
    };

    for (int i = 0; call_patterns[i] != NULL; i++) {
        const char *p = tbb10au_strcasestr(html, call_patterns[i]);
        if (p == NULL) continue;
        p += strlen(call_patterns[i]);
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '(') continue;
        p++;
        char raw[TBB10AL_URL_CAP];
        if (tbb10bf_capture_quoted_after(p, 160, raw, sizeof(raw)) &&
            tbb10bf_finish_nav_url(base, raw, out, out_cap)) return true;
    }

    for (int i = 0; assign_patterns[i] != NULL; i++) {
        const char *p = tbb10au_strcasestr(html, assign_patterns[i]);
        if (p == NULL) continue;
        p += strlen(assign_patterns[i]);
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '=') continue;
        p++;
        char raw[TBB10AL_URL_CAP];
        if (tbb10bf_capture_quoted_after(p, 160, raw, sizeof(raw)) &&
            tbb10bf_finish_nav_url(base, raw, out, out_cap)) return true;
    }
    return false;
}

static bool tbb10bf_extract_nav_action(const char *base, const char *html,
                                       char *out, size_t out_cap,
                                       char *reason, size_t reason_cap)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    if (reason != NULL && reason_cap > 0) reason[0] = '\0';

    if (tbb10bf_extract_meta_refresh_url(base, html, out, out_cap)) {
        if (reason != NULL && reason_cap > 0) tbb10ah_safe_copy(reason, reason_cap, "meta-refresh");
        return true;
    }
    if (tbb10bf_extract_js_location_url(base, html, out, out_cap)) {
        if (reason != NULL && reason_cap > 0) tbb10ah_safe_copy(reason, reason_cap, "js-location");
        return true;
    }
    return false;
}

static char *tbb10au_http_get_one(const char *url, size_t *out_len, char *err, size_t err_cap,
                                  int *out_status, char *location, size_t location_cap)
{
    if (out_len != NULL) *out_len = 0;
    if (out_status != NULL) *out_status = 0;
    if (location != NULL && location_cap > 0) location[0] = '\0';
    if (url == NULL || url[0] == '\0') { if (err) tbb10ah_safe_copy(err, err_cap, "URL vacia"); return NULL; }
    bool is_https = tbb10al_is_https_url(url);
    if (!tbb10al_wait_wifi(err, err_cap)) return NULL;
    char *buf = (char *) heap_caps_malloc(TBB10AL_HTTP_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) buf = (char *) heap_caps_malloc(TBB10AL_HTTP_MAX + 1, MALLOC_CAP_8BIT);
    if (buf == NULL) { if (err) tbb10ah_safe_copy(err, err_cap, "Sin memoria para buffer HTTP"); return NULL; }
    buf[0] = '\0';

    char *setcookie_capture = (char *)heap_caps_calloc(
        1, TBB10BO_SETCOOKIE_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tbb10al_http_accum_t acc = {
        .buf = buf, .cap = TBB10AL_HTTP_MAX + 1, .len = 0, .truncated = false,
        .location = location, .location_cap = location_cap,
        .set_cookie_buf = setcookie_capture,
        .set_cookie_cap = setcookie_capture ? TBB10BO_SETCOOKIE_CAP : 0,
        .set_cookie_len = 0
    };
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = tbb10al_http_event,
        .user_data = &acc,
        .timeout_ms = is_https ? 15000 : 12000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        /* 10BM_FIX1:
         * Volvemos al User-Agent exacto del navegador antiguo validado.
         * Simular Chrome sin ejecutar JS/cookies hacia sospechoso el cliente
         * y disparaba pantallas de "habilita JavaScript" / anti-bot.
         * Mantener el mismo UA en cfg.user_agent y en el header explicito. */
        .user_agent = "ESP32-BreezyBox",
        .transport_type = is_https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP,
        .skip_cert_common_name_check = is_https ? true : false,
        .disable_auto_redirect = true,
        .max_redirection_count = 0,
    };

    printf("[TBBROWSER10AW][NET] GET %s\n", url);
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) { if (setcookie_capture) heap_caps_free(setcookie_capture); heap_caps_free(buf); if (err) tbb10ah_safe_copy(err, err_cap, "esp_http_client_init fallo"); return NULL; }
    esp_http_client_set_header(client, "User-Agent", "ESP32-BreezyBox");
    esp_http_client_set_header(client, "Accept", "text/html,text/plain,*/*");
    esp_http_client_set_header(client, "Accept-Language", "es-ES,es;q=0.9,en;q=0.8");
    esp_http_client_set_header(client, "Connection", "close");
    char cookie_header[TBB10BO_COOKIE_HEADER_CAP];
    if (tbb10bo_cookie_build_header(url, cookie_header, sizeof(cookie_header))) {
        esp_http_client_set_header(client, "Cookie", cookie_header);
        printf("[TBBROWSER10BO][COOKIE] envio Cookie bytes=%u\n", (unsigned)strlen(cookie_header));
    }
    esp_err_t e = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int64_t clen = esp_http_client_get_content_length(client);
    printf("[TBBROWSER10AW][NET] status=%d len=%lld rx=%u%s\n", status, (long long)clen, (unsigned)acc.len, acc.truncated ? " TRUNC" : "");
    if (e == ESP_OK && setcookie_capture != NULL && setcookie_capture[0] != '\0') {
        tbb10bo_cookie_store_lines_for_url(url, setcookie_capture);
    }
    esp_http_client_cleanup(client);
    if (setcookie_capture != NULL) heap_caps_free(setcookie_capture);
    if (out_status != NULL) *out_status = status;
    if (e != ESP_OK) { heap_caps_free(buf); if (err) { tbb10ah_safe_copy(err, err_cap, "HTTP fallo: "); tbb10ah_safe_append(err, err_cap, esp_err_to_name(e)); } return NULL; }
    if (out_len != NULL) *out_len = acc.len;
    return buf;
}

static char *tbb10bp_http_post_one(const char *url, const char *post_body,
                                   size_t *out_len, char *err, size_t err_cap,
                                   int *out_status, char *location, size_t location_cap)
{
    if (out_len != NULL) *out_len = 0;
    if (out_status != NULL) *out_status = 0;
    if (location != NULL && location_cap > 0) location[0] = '\0';
    if (url == NULL || url[0] == '\0' || post_body == NULL) {
        if (err) tbb10ah_safe_copy(err, err_cap, "POST invalido");
        return NULL;
    }
    bool is_https = tbb10al_is_https_url(url);
    if (!tbb10al_wait_wifi(err, err_cap)) return NULL;

    char *buf = (char *)heap_caps_malloc(TBB10AL_HTTP_MAX + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) buf = (char *)heap_caps_malloc(TBB10AL_HTTP_MAX + 1, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        if (err) tbb10ah_safe_copy(err, err_cap, "Sin memoria para buffer POST");
        return NULL;
    }
    buf[0] = '\0';

    char *setcookie_capture = (char *)heap_caps_calloc(
        1, TBB10BO_SETCOOKIE_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    tbb10al_http_accum_t acc = {
        .buf = buf, .cap = TBB10AL_HTTP_MAX + 1, .len = 0, .truncated = false,
        .location = location, .location_cap = location_cap,
        .set_cookie_buf = setcookie_capture,
        .set_cookie_cap = setcookie_capture ? TBB10BO_SETCOOKIE_CAP : 0,
        .set_cookie_len = 0
    };
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = tbb10al_http_event,
        .user_data = &acc,
        .timeout_ms = is_https ? 18000 : 15000,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
        .user_agent = "ESP32-BreezyBox",
        .transport_type = is_https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP,
        .skip_cert_common_name_check = is_https ? true : false,
        .disable_auto_redirect = true,
        .max_redirection_count = 0,
    };

    printf("[TBBROWSER10BP][NET] POST %s body_bytes=%u\n", url, (unsigned)strlen(post_body));
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        if (setcookie_capture) heap_caps_free(setcookie_capture);
        heap_caps_free(buf);
        if (err) tbb10ah_safe_copy(err, err_cap, "esp_http_client_init POST fallo");
        return NULL;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "User-Agent", "ESP32-BreezyBox");
    esp_http_client_set_header(client, "Accept", "text/html,text/plain,*/*");
    esp_http_client_set_header(client, "Accept-Language", "es-ES,es;q=0.9,en;q=0.8");
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_post_field(client, post_body, (int)strlen(post_body));

    char cookie_header[TBB10BO_COOKIE_HEADER_CAP];
    if (tbb10bo_cookie_build_header(url, cookie_header, sizeof(cookie_header))) {
        esp_http_client_set_header(client, "Cookie", cookie_header);
        printf("[TBBROWSER10BP][COOKIE] envio Cookie bytes=%u\n", (unsigned)strlen(cookie_header));
    }

    esp_err_t e = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int64_t clen = esp_http_client_get_content_length(client);
    printf("[TBBROWSER10BP][NET] status=%d len=%lld rx=%u%s\n",
           status, (long long)clen, (unsigned)acc.len, acc.truncated ? " TRUNC" : "");
    if (e == ESP_OK && setcookie_capture != NULL && setcookie_capture[0] != '\0') {
        tbb10bo_cookie_store_lines_for_url(url, setcookie_capture);
    }
    esp_http_client_cleanup(client);
    if (setcookie_capture != NULL) heap_caps_free(setcookie_capture);
    if (out_status != NULL) *out_status = status;
    if (e != ESP_OK) {
        heap_caps_free(buf);
        if (err) {
            tbb10ah_safe_copy(err, err_cap, "HTTP POST fallo: ");
            tbb10ah_safe_append(err, err_cap, esp_err_to_name(e));
        }
        return NULL;
    }
    if (out_len != NULL) *out_len = acc.len;
    return buf;
}

static char *tbb10al_http_get(const char *url, size_t *out_len, char *err, size_t err_cap);

static char *tbb10bp_http_post(const char *url, const char *post_body,
                               size_t *out_len, char *err, size_t err_cap)
{
    if (out_len != NULL) *out_len = 0;
    if (err != NULL && err_cap > 0) err[0] = '\0';

    char location[TBB10AL_URL_CAP];
    char next_url[TBB10AL_URL_CAP];
    size_t len = 0;
    int status = 0;
    char *body = tbb10bp_http_post_one(url, post_body, &len, err, err_cap,
                                       &status, location, sizeof(location));
    if (body == NULL) return NULL;

    /* Semantica practica de formularios web: 301/302/303 pasan a GET.
     * Para 307/308 repetimos POST una sola vez en el nuevo destino. */
    if (tbb10au_status_is_redirect(status) && location[0] != '\0' &&
        tbb10au_resolve_url_like_old(url, location, next_url, sizeof(next_url))) {
        heap_caps_free(body);
        if (status == 307 || status == 308) {
            printf("[TBBROWSER10BP][REDIR] POST %d -> POST %s\n", status, next_url);
            body = tbb10bp_http_post_one(next_url, post_body, &len, err, err_cap,
                                         &status, location, sizeof(location));
            if (body == NULL) return NULL;
            tbb10ah_safe_copy(s_tbb10au_effective_url, sizeof(s_tbb10au_effective_url), next_url);
        } else {
            printf("[TBBROWSER10BP][REDIR] POST %d -> GET %s\n", status, next_url);
            body = tbb10al_http_get(next_url, &len, err, err_cap);
            if (body == NULL) return NULL;
        }
    } else {
        if (status < 200 || status >= 400) {
            heap_caps_free(body);
            if (err) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), "HTTP POST status %d", status);
                tbb10ah_safe_copy(err, err_cap, tmp);
            }
            return NULL;
        }
        tbb10ah_safe_copy(s_tbb10au_effective_url, sizeof(s_tbb10au_effective_url), url);
    }

    if (out_len != NULL) *out_len = len;
    return body;
}

static char *tbb10al_http_get(const char *url, size_t *out_len, char *err, size_t err_cap)
{
    if (out_len != NULL) *out_len = 0;
    if (err != NULL && err_cap > 0) err[0] = '\0';
    if (url == NULL || url[0] == '\0') { if (err) tbb10ah_safe_copy(err, err_cap, "URL vacia"); return NULL; }

    static char current_url[TBB10AL_URL_CAP];
    static char next_url[TBB10AL_URL_CAP];
    static char location[TBB10AL_URL_CAP];

    tbb10ah_safe_copy(current_url, sizeof(current_url), url);
    s_tbb10au_effective_url[0] = '\0';

    for (int depth = 0; depth <= TBB10AU_REDIRECT_MAX; depth++) {
        size_t len = 0;
        int status = 0;
        location[0] = '\0';
        char *body = tbb10au_http_get_one(current_url, &len, err, err_cap, &status, location, sizeof(location));

        if (body == NULL) return NULL;

        if (tbb10au_status_is_redirect(status)) {
            if (location[0] != '\0' && tbb10au_resolve_url_like_old(current_url, location, next_url, sizeof(next_url))) {
                printf("[TBBROWSER10AW][REDIR] header %d/%d: %s -> %s\n", depth + 1, TBB10AU_REDIRECT_MAX, current_url, next_url);
                heap_caps_free(body);
                tbb10ah_safe_copy(current_url, sizeof(current_url), next_url);
                continue;
            }
            if (body[0] != '\0' && tbb10au_first_href_from_redirect_html(current_url, body, next_url, sizeof(next_url))) {
                printf("[TBBROWSER10AW][REDIR] html-3xx %d/%d: %s -> %s\n", depth + 1, TBB10AU_REDIRECT_MAX, current_url, next_url);
                heap_caps_free(body);
                tbb10ah_safe_copy(current_url, sizeof(current_url), next_url);
                continue;
            }
            heap_caps_free(body);
            if (err) { char tmp[80]; snprintf(tmp, sizeof(tmp), "HTTP redirect %d sin destino valido", status); tbb10ah_safe_copy(err, err_cap, tmp); }
            return NULL;
        }

        if (status < 200 || status >= 400) {
            heap_caps_free(body);
            if (err) { char tmp[64]; snprintf(tmp, sizeof(tmp), "HTTP status %d", status); tbb10ah_safe_copy(err, err_cap, tmp); }
            return NULL;
        }

        if (body[0] != '\0' && tbb10au_page_looks_like_redirect(body) &&
            tbb10au_first_href_from_redirect_html(current_url, body, next_url, sizeof(next_url))) {
            printf("[TBBROWSER10AW][REDIR] html-moved %d/%d: %s -> %s\n", depth + 1, TBB10AU_REDIRECT_MAX, current_url, next_url);
            heap_caps_free(body);
            tbb10ah_safe_copy(current_url, sizeof(current_url), next_url);
            continue;
        }

        char nav_reason[32];
        nav_reason[0] = '\0';
        if (body[0] != '\0' &&
            tbb10bf_extract_nav_action(current_url, body, next_url, sizeof(next_url), nav_reason, sizeof(nav_reason))) {
            if (strcmp(current_url, next_url) != 0) {
                printf("[TBBROWSER10BH][JS-LITE] %s %d/%d: %s -> %s\n",
                       nav_reason[0] ? nav_reason : "nav-action", depth + 1, TBB10AU_REDIRECT_MAX,
                       current_url, next_url);
                heap_caps_free(body);
                tbb10ah_safe_copy(current_url, sizeof(current_url), next_url);
                continue;
            }
        }

        if (len == 0) {
            tbb10ah_safe_copy(body, TBB10AL_HTTP_MAX + 1, "<!doctype html><html><body><h1>TactileBrowser 10BP</h1><p>Respuesta HTTP vacia.</p></body></html>");
            len = strlen(body);
        }
        if (strlen(body) >= TBB10AL_HTTP_MAX - 128) {
            tbb10ah_safe_append(body, TBB10AL_HTTP_MAX + 1, "\n<p>[TactileBrowser 10BP: pagina truncada por limite de memoria]</p>\n");
            len = strlen(body);
        }
        tbb10ah_safe_copy(s_tbb10au_effective_url, sizeof(s_tbb10au_effective_url), current_url);
        if (out_len != NULL) *out_len = len;
        return body;
    }

    if (err) tbb10ah_safe_copy(err, err_cap, "Demasiadas redirecciones");
    return NULL;
}


/* -------------------------------------------------------------------------
 * 10AW - Capa de render clasico tipo navegador antiguo HTML10D
 * -------------------------------------------------------------------------
 * Objetivo: para paginas de red, usar Lexbor solo como visor de una pagina
 * HTML sencilla generada por nosotros. La extraccion de texto/enlaces se hace
 * con una capa ligera estilo minipc_browser.c, porque en webs grandes el DOM
 * moderno deja demasiada basura o demasiado poco texto visible.
 */
static void tbb10aw_append_c(char *dst, size_t cap, char c)
{
    char tmp[2]; tmp[0] = c; tmp[1] = '\0';
    tbb10ah_safe_append(dst, cap, tmp);
}

/* 10BE:
 * La 10BD mejoro mucho el layout de Wikipedia, pero aun se perdian acentos
 * porque el extractor HTML10D convertia cualquier byte >126 en espacio y las
 * entidades HTML numericas/nominadas no se pasaban a UTF-8. En esta capa de
 * texto mantenemos UTF-8 real y convertimos entidades latinas frecuentes.
 */
static void tbb10be_append_utf8_cp(char *dst, size_t cap, uint32_t cp)
{
    char tmp[5];
    size_t n = 0;
    if (cp == 0) return;
    if (cp < 0x80) {
        tmp[n++] = (char)cp;
    } else if (cp < 0x800) {
        tmp[n++] = (char)(0xC0 | (cp >> 6));
        tmp[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        tmp[n++] = (char)(0xE0 | (cp >> 12));
        tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        tmp[n++] = (char)(0xF0 | (cp >> 18));
        tmp[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
        tmp[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[n++] = (char)(0x80 | (cp & 0x3F));
    }
    tmp[n] = '\0';
    if (n > 0) tbb10ah_safe_append(dst, cap, tmp);
}

typedef struct {
    const char *name;
    uint32_t cp;
} tbb10be_entity_t;

static const tbb10be_entity_t s_tbb10be_entities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
    {"nbsp", ' '}, {"ndash", 0x2013}, {"mdash", 0x2014}, {"hellip", 0x2026},
    {"laquo", 0x00AB}, {"raquo", 0x00BB}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
    {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"copy", 0x00A9}, {"reg", 0x00AE},
    {"euro", 0x20AC}, {"middot", 0x00B7}, {"bull", 0x2022}, {"deg", 0x00B0},
    {"ordm", 0x00BA}, {"ordf", 0x00AA},

    {"Aacute", 0x00C1}, {"Eacute", 0x00C9}, {"Iacute", 0x00CD}, {"Oacute", 0x00D3}, {"Uacute", 0x00DA},
    {"aacute", 0x00E1}, {"eacute", 0x00E9}, {"iacute", 0x00ED}, {"oacute", 0x00F3}, {"uacute", 0x00FA},
    {"Agrave", 0x00C0}, {"Egrave", 0x00C8}, {"Igrave", 0x00CC}, {"Ograve", 0x00D2}, {"Ugrave", 0x00D9},
    {"agrave", 0x00E0}, {"egrave", 0x00E8}, {"igrave", 0x00EC}, {"ograve", 0x00F2}, {"ugrave", 0x00F9},
    {"Acirc", 0x00C2}, {"Ecirc", 0x00CA}, {"Icirc", 0x00CE}, {"Ocirc", 0x00D4}, {"Ucirc", 0x00DB},
    {"acirc", 0x00E2}, {"ecirc", 0x00EA}, {"icirc", 0x00EE}, {"ocirc", 0x00F4}, {"ucirc", 0x00FB},
    {"Auml", 0x00C4}, {"Euml", 0x00CB}, {"Iuml", 0x00CF}, {"Ouml", 0x00D6}, {"Uuml", 0x00DC},
    {"auml", 0x00E4}, {"euml", 0x00EB}, {"iuml", 0x00EF}, {"ouml", 0x00F6}, {"uuml", 0x00FC},
    {"Ntilde", 0x00D1}, {"ntilde", 0x00F1}, {"Ccedil", 0x00C7}, {"ccedil", 0x00E7},
    {"Yacute", 0x00DD}, {"yacute", 0x00FD}, {"yuml", 0x00FF},
};

static bool tbb10be_entity_append_utf8(char *dst, size_t cap, const char *s, int *consumed)
{
    if (consumed) *consumed = 0;
    if (s == NULL || s[0] != '&') return false;
    const char *semi = strchr(s, ';');
    if (semi == NULL || semi - s > 24) return false;
    int n = (int)(semi - s + 1);

    if (s[1] == '#') {
        uint32_t v = 0;
        int i = 2;
        bool hex = false;
        if (s[i] == 'x' || s[i] == 'X') { hex = true; i++; }
        for (; s[i] && s[i] != ';'; i++) {
            int d = -1;
            if (s[i] >= '0' && s[i] <= '9') d = s[i] - '0';
            else if (hex && s[i] >= 'a' && s[i] <= 'f') d = 10 + s[i] - 'a';
            else if (hex && s[i] >= 'A' && s[i] <= 'F') d = 10 + s[i] - 'A';
            else return false;
            v = hex ? (v * 16u + (uint32_t)d) : (v * 10u + (uint32_t)d);
            if (v > 0x10FFFFu) return false;
        }
        if (s[i] == ';' && v > 0) {
            if (consumed) *consumed = i + 1;
            tbb10be_append_utf8_cp(dst, cap, v);
            return true;
        }
        return false;
    }

    char name[25];
    int len = (int)(semi - s - 1);
    if (len <= 0 || len >= (int)sizeof(name)) return false;
    memcpy(name, s + 1, (size_t)len);
    name[len] = '\0';

    for (size_t i = 0; i < sizeof(s_tbb10be_entities) / sizeof(s_tbb10be_entities[0]); i++) {
        if (strcasecmp(name, s_tbb10be_entities[i].name) == 0) {
            if (consumed) *consumed = n;
            tbb10be_append_utf8_cp(dst, cap, s_tbb10be_entities[i].cp);
            return true;
        }
    }

    /* Entidad desconocida: consumirla y dejar un espacio para no ensuciar. */
    if (consumed) *consumed = n;
    tbb10aw_append_c(dst, cap, ' ');
    return true;
}

static bool tbb10aw_tag_starts(const char *p, const char *name)
{
    if (p == NULL || p[0] != '<' || name == NULL) return false;
    p++;
    if (*p == '/') p++;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t n = strlen(name);
    if (strncasecmp(p, name, n) != 0) return false;
    char c = p[n];
    return c == '\0' || c == '>' || c == '/' || isspace((unsigned char)c);
}

static bool __attribute__((unused)) tbb10aw_closing_tag(const char *p, const char *name)
{
    if (p == NULL || p[0] != '<' || p[1] != '/') return false;
    p += 2;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t n = strlen(name);
    if (strncasecmp(p, name, n) != 0) return false;
    char c = p[n];
    return c == '\0' || c == '>' || isspace((unsigned char)c);
}

static const char *tbb10aw_skip_until_close_tag(const char *p, const char *end, const char *name)
{
    if (p == NULL) return NULL;
    if (end == NULL) end = p + strlen(p);
    if (end <= p) return end;

    char pat[32];
    snprintf(pat, sizeof(pat), "</%s", name ? name : "");
    const char *q = tbb10au_strcasestr(p, pat);
    if (q != NULL && q < end) {
        const char *gt = strchr(q, '>');
        if (gt != NULL && gt < end) return gt + 1;
        return q + 1;
    }

    /* 10AY:
     * Algunas paginas modernas, sobre todo cuentas/login de Google, llegan
     * recortadas dentro del buffer y el <style> o <script> queda sin cierre.
     * En 10AX_FIX2 avanzabamos solo hasta el cierre del tag de apertura para
     * evitar bucle, pero eso dejaba entrar todo el CSS bruto en pantalla
     * (@keyframes, -webkit, transform, etc.).
     *
     * Si no aparece el cierre, saltamos hasta <body>, </head> o final del rango.
     * Es preferible perder basura CSS/JS antes que llenar la vista HTML10D con
     * texto incomprensible o bloquear breezy_repl.
     */
    const char *gt_open = strchr(p, '>');
    const char *from = (gt_open != NULL && gt_open < end) ? (gt_open + 1) : (p + 1);
    const char *body = tbb10au_strcasestr(from, "<body");
    if (body != NULL && body < end) return body;
    const char *head_end = tbb10au_strcasestr(from, "</head");
    if (head_end != NULL && head_end < end) {
        const char *gt = strchr(head_end, '>');
        if (gt != NULL && gt < end) return gt + 1;
        return head_end + 1;
    }
    return end;
}

static void tbb10aw_text_from_html_range(const char *begin, const char *end, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return;
    out[0] = '\0';
    if (begin == NULL) return;
    if (end == NULL) end = begin + strlen(begin);
    int last_space = 1;
    uint32_t loop_guard = 0;
    for (const char *p = begin; p < end && *p; ) {
        if ((++loop_guard & 0x07FFu) == 0) {
            vTaskDelay(1);
        }
        if (*p == '<') {
            if (tbb10aw_tag_starts(p, "script")) { p = tbb10aw_skip_until_close_tag(p, end, "script"); continue; }
            if (tbb10aw_tag_starts(p, "style"))  { p = tbb10aw_skip_until_close_tag(p, end, "style"); continue; }
            if (tbb10aw_tag_starts(p, "br") || tbb10aw_tag_starts(p, "p") ||
                tbb10aw_tag_starts(p, "div") || tbb10aw_tag_starts(p, "li") ||
                tbb10aw_tag_starts(p, "tr") || tbb10aw_tag_starts(p, "h1") ||
                tbb10aw_tag_starts(p, "h2") || tbb10aw_tag_starts(p, "h3") ||
                tbb10aw_tag_starts(p, "h4") || tbb10aw_tag_starts(p, "section") ||
                tbb10aw_tag_starts(p, "article") || tbb10aw_tag_starts(p, "header")) {
                /* 10BD: el navegador viejo era mucho mas legible porque no
                 * convertia todos los bloques HTML a una unica tira de texto.
                 * Para Wikipedia y paginas largas, un salto de linea por bloque
                 * evita que todo salga apiñado.
                 */
                size_t olen = strlen(out);
                if (olen > 0 && out[olen - 1] != '\n') {
                    tbb10aw_append_c(out, out_cap, '\n');
                }
                last_space = 1;
            }
            else if (tbb10aw_tag_starts(p, "td") || tbb10aw_tag_starts(p, "th")) {
                size_t olen = strlen(out);
                if (olen > 0 && out[olen - 1] != ' ' && out[olen - 1] != '\n') {
                    tbb10ah_safe_append(out, out_cap, " | ");
                }
                last_space = 1;
            }
            const char *gt = strchr(p, '>');
            p = gt ? gt + 1 : end;
            continue;
        }
        char c = *p++;
        if (c == '&') {
            int cons = 0;
            if (tbb10be_entity_append_utf8(out, out_cap, p - 1, &cons) && cons > 0) {
                p = (p - 1) + cons;
                last_space = 0;
                continue;
            }
        }

        unsigned char uc = (unsigned char)c;
        if (uc < 32) c = ' ';

        /* 10BE: conservar bytes UTF-8 reales. Antes todo >126 se convertia
         * en espacio, por eso aparecia "edificaci n" en vez de "edificación".
         * Si la pagina viene en UTF-8, copiamos esos bytes tal cual.
         */
        if ((unsigned char)c >= 128) {
            tbb10aw_append_c(out, out_cap, c);
            last_space = 0;
        } else if (isspace((unsigned char)c)) {
            if (!last_space) { tbb10aw_append_c(out, out_cap, ' '); last_space = 1; }
        } else {
            tbb10aw_append_c(out, out_cap, c);
            last_space = 0;
        }
    }
    tbb10ah_chomp(out);
}

/* 10BI_FIX2:
 * En 10BI el render ya podia dibujar UTF-8/acentos, pero algunos textos de
 * atributos (sobre todo alt/title de imagenes) seguian pasando por el
 * decodificador ASCII antiguo. Resultado: `España` dentro de [IMG: ...]
 * salia como `Espa??a`, aunque el titulo se viera bien.
 *
 * Este decodificador conserva bytes UTF-8 reales, acepta Latin-1/CP1252
 * suelto y convierte entidades HTML con la tabla 10BE.
 */
static uint32_t tbb10bi_cp1252_or_latin1(unsigned char uc)
{
    /* 10BI_FIX3:
     * Algunos atributos alt/title llegan en Latin-1/CP1252 aunque el resto de
     * la pagina vaya bien en UTF-8. Si copiamos byte 0xF1 directo dentro del
     * HTML generado, Lexbor lo ve como UTF-8 invalido y termina apareciendo
     * [IMG: Espa??a]. Convertimos esos bytes sueltos a UTF-8 real.
     */
    switch (uc) {
        case 0x80: return 0x20AC;
        case 0x82: return 0x201A;
        case 0x83: return 0x0192;
        case 0x84: return 0x201E;
        case 0x85: return 0x2026;
        case 0x86: return 0x2020;
        case 0x87: return 0x2021;
        case 0x88: return 0x02C6;
        case 0x89: return 0x2030;
        case 0x8A: return 0x0160;
        case 0x8B: return 0x2039;
        case 0x8C: return 0x0152;
        case 0x8E: return 0x017D;
        case 0x91: return 0x2018;
        case 0x92: return 0x2019;
        case 0x93: return 0x201C;
        case 0x94: return 0x201D;
        case 0x95: return 0x2022;
        case 0x96: return 0x2013;
        case 0x97: return 0x2014;
        case 0x98: return 0x02DC;
        case 0x99: return 0x2122;
        case 0x9A: return 0x0161;
        case 0x9B: return 0x203A;
        case 0x9C: return 0x0153;
        case 0x9E: return 0x017E;
        case 0x9F: return 0x0178;
        default: return (uint32_t)uc;
    }
}

static bool tbb10bi_attr_copy_valid_utf8(char *dst, size_t dst_cap, const char **pp)
{
    if (dst == NULL || dst_cap == 0 || pp == NULL || *pp == NULL) return false;
    const unsigned char *p = (const unsigned char *)(*pp);
    int n = 0;
    uint32_t cp = 0;

    if (p[0] >= 0xC2 && p[0] <= 0xDF) {
        if ((p[1] & 0xC0) != 0x80) return false;
        n = 2;
        cp = ((uint32_t)(p[0] & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
    } else if (p[0] >= 0xE0 && p[0] <= 0xEF) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return false;
        if (p[0] == 0xE0 && p[1] < 0xA0) return false;
        if (p[0] == 0xED && p[1] >= 0xA0) return false;
        n = 3;
        cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (uint32_t)(p[2] & 0x3F);
    } else if (p[0] >= 0xF0 && p[0] <= 0xF4) {
        if ((p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return false;
        if (p[0] == 0xF0 && p[1] < 0x90) return false;
        if (p[0] == 0xF4 && p[1] > 0x8F) return false;
        n = 4;
        cp = ((uint32_t)(p[0] & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
             ((uint32_t)(p[2] & 0x3F) << 6) | (uint32_t)(p[3] & 0x3F);
    }

    if (n <= 0 || cp == 0) return false;
    char tmp[5];
    for (int i = 0; i < n; i++) tmp[i] = (char)p[i];
    tmp[n] = '\0';
    tbb10ah_safe_append(dst, dst_cap, tmp);
    *pp += n;
    return true;
}

static void tbb10bi_attr_entity_decode_utf8(char *dst, size_t dst_cap, const char *src)
{
    if (dst == NULL || dst_cap == 0) return;
    dst[0] = '\0';
    if (src == NULL) return;

    for (const char *p = src; *p != '\0'; ) {
        if (*p == '&') {
            int cons = 0;
            if (tbb10be_entity_append_utf8(dst, dst_cap, p, &cons) && cons > 0) {
                p += cons;
                continue;
            }
        }

        unsigned char uc = (unsigned char)*p;
        if (uc < 32 && uc != '\t') {
            tbb10aw_append_c(dst, dst_cap, ' ');
            p++;
        } else if (uc < 128) {
            char tmp[2];
            tmp[0] = (char)uc;
            tmp[1] = '\0';
            tbb10ah_safe_append(dst, dst_cap, tmp);
            p++;
        } else if (tbb10bi_attr_copy_valid_utf8(dst, dst_cap, &p)) {
            /* UTF-8 real conservado. */
        } else {
            /* Byte alto suelto: Latin-1/CP1252 -> UTF-8 valido. */
            tbb10be_append_utf8_cp(dst, dst_cap, tbb10bi_cp1252_or_latin1(uc));
            p++;
        }
    }
    tbb10ah_chomp(dst);
}

static bool tbb10aw_extract_attr_from_tag(const char *tag, const char *tag_end, const char *attr, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    if (tag == NULL || tag_end == NULL || attr == NULL) return false;
    size_t alen = strlen(attr);
    for (const char *p = tag; p < tag_end && *p; p++) {
        if (strncasecmp(p, attr, alen) != 0) continue;
        const char before = (p == tag) ? ' ' : p[-1];
        const char after = p[alen];
        if ((isalnum((unsigned char)before) || before == '_' || before == '-') ||
            !(after == '=' || isspace((unsigned char)after))) continue;
        const char *q = p + alen;
        while (q < tag_end && isspace((unsigned char)*q)) q++;
        if (q >= tag_end || *q != '=') continue;
        q++;
        while (q < tag_end && isspace((unsigned char)*q)) q++;
        char quote = 0;
        if (q < tag_end && (*q == '"' || *q == '\'')) quote = *q++;
        char raw[TBB10AL_URL_CAP];
        size_t pos = 0;
        while (q < tag_end && *q && pos + 1 < sizeof(raw)) {
            if (quote) { if (*q == quote) break; }
            else { if (isspace((unsigned char)*q) || *q == '>') break; }
            raw[pos++] = *q++;
        }
        raw[pos] = '\0';
        tbb10bi_attr_entity_decode_utf8(out, out_cap, raw);
        return out[0] != '\0';
    }
    return false;
}


static bool tbb10bb_input_type_is_searchable(const char *type)
{
    if (type == NULL || type[0] == '\0') return true;
    return (strcasecmp(type, "text") == 0 || strcasecmp(type, "search") == 0);
}

static bool tbb10bb_input_name_is_better(const char *name, const char *old_name)
{
    if (name == NULL || name[0] == '\0') return false;
    if (old_name == NULL || old_name[0] == '\0') return true;
    if (strcasecmp(name, "q") == 0) return true;
    if (strcasecmp(name, "query") == 0 || strcasecmp(name, "search") == 0 || strcasecmp(name, "s") == 0) {
        if (strcasecmp(old_name, "q") != 0) return true;
    }
    return false;
}

static void tbb10bb_scan_forms_html10d(const char *url, const char *raw)
{
    tbb10bb_forms_reset();
    if (url == NULL || raw == NULL) return;

    bool active = false;
    bool active_post = false;
    char active_action[TBB10AL_URL_CAP];
    char active_hidden[TBB10BP_POST_BODY_CAP];
    active_action[0] = '\0';
    active_hidden[0] = '\0';

    const char *p = raw;
    int tags = 0;
    while (p != NULL && *p != '\0' && tags++ < 2200) {
        p = strchr(p, '<');
        if (p == NULL) break;
        const char *gt = strchr(p, '>');
        if (gt == NULL) break;

        bool closing = (p[1] == '/');
        if (!closing && tbb10aw_tag_starts(p, "form")) {
            char method[24];
            char action_raw[TBB10AL_URL_CAP];
            char action_abs[TBB10AL_URL_CAP];
            method[0] = action_raw[0] = action_abs[0] = '\0';
            active = false;
            active_post = false;
            active_hidden[0] = '\0';

            if (tbb10aw_extract_attr_from_tag(p, gt, "method", method, sizeof(method))) {
                active_post = (strcasecmp(method, "post") == 0);
                if (!active_post && strcasecmp(method, "get") != 0) {
                    p = gt + 1;
                    continue;
                }
            }

            if (tbb10aw_extract_attr_from_tag(p, gt, "action", action_raw, sizeof(action_raw))) {
                if (!tbb10au_resolve_url_like_old(url, action_raw, action_abs, sizeof(action_abs))) {
                    action_abs[0] = '\0';
                }
            } else {
                tbb10ah_safe_copy(action_abs, sizeof(action_abs), url);
            }

            if (action_abs[0] != '\0') {
                tbb10ah_safe_copy(active_action, sizeof(active_action), action_abs);
                active = true;
                printf("[TBBROWSER10BP][FORM] begin method=%s action=%s\n",
                       active_post ? "POST" : "GET", active_action);
            }
        }
        else if (closing && tbb10aw_tag_starts(p, "form")) {
            active = false;
            active_action[0] = '\0';
            active_hidden[0] = '\0';
        }
        else if (active && !closing && tbb10aw_tag_starts(p, "input")) {
            char type[32], name[64], value[256];
            type[0] = name[0] = value[0] = '\0';
            (void)tbb10aw_extract_attr_from_tag(p, gt, "type", type, sizeof(type));
            bool has_name = tbb10aw_extract_attr_from_tag(p, gt, "name", name, sizeof(name));

            if (active_post && has_name && strcasecmp(type, "hidden") == 0) {
                (void)tbb10aw_extract_attr_from_tag(p, gt, "value", value, sizeof(value));
                char en[TBB10AL_URL_CAP], ev[TBB10AL_URL_CAP];
                tbb10ap_urlencode_query(name, en, sizeof(en));
                tbb10ap_urlencode_query(value, ev, sizeof(ev));
                if (en[0] != '\0') {
                    if (active_hidden[0] != '\0') tbb10ah_safe_append(active_hidden, sizeof(active_hidden), "&");
                    tbb10ah_safe_append(active_hidden, sizeof(active_hidden), en);
                    tbb10ah_safe_append(active_hidden, sizeof(active_hidden), "=");
                    tbb10ah_safe_append(active_hidden, sizeof(active_hidden), ev);
                    /* Si el campo visible ya fue elegido, conserva tambien
                     * los hidden que aparezcan despues en el HTML. */
                    if (s_tbb10bb_form_available && s_tbb10bp_form_is_post &&
                        strcmp(s_tbb10bb_form_action, active_action) == 0) {
                        if (tbb10bo_post_ensure_buffers()) {
                            tbb10ah_safe_copy(s_tbb10bp_form_hidden, TBB10BP_POST_BODY_CAP, active_hidden);
                        }
                    }
                }
            }
            else if (tbb10bb_input_type_is_searchable(type) && has_name &&
                     tbb10bb_input_name_is_better(name, s_tbb10bb_form_input)) {
                tbb10ah_safe_copy(s_tbb10bb_form_action, sizeof(s_tbb10bb_form_action), active_action);
                tbb10ah_safe_copy(s_tbb10bb_form_input, sizeof(s_tbb10bb_form_input), name);
                s_tbb10bb_form_available = true;
                s_tbb10bp_form_is_post = active_post;
                if (tbb10bo_post_ensure_buffers())
                    tbb10ah_safe_copy(s_tbb10bp_form_hidden, TBB10BP_POST_BODY_CAP, active_hidden);
                printf("[TBBROWSER10BP][FORM] input name=%s method=%s hidden_bytes=%u action=%s\n",
                       s_tbb10bb_form_input, active_post ? "POST" : "GET",
                       (unsigned)(s_tbb10bp_form_hidden ? strlen(s_tbb10bp_form_hidden) : 0), s_tbb10bb_form_action);
                if (!active_post && strcasecmp(name, "q") == 0) break;
            }
        }

        p = gt + 1;
        if ((tags & 0x3Fu) == 0) vTaskDelay(1);
    }
}

static void tbb10aw_host_from_url(const char *url, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return;
    out[0] = '\0';
    if (url == NULL) return;
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    const char *e = p;
    while (*e && *e != '/' && *e != '?' && *e != '#') e++;
    size_t n = (size_t)(e - p);
    if (n >= out_cap) n = out_cap - 1;
    memcpy(out, p, n);
    out[n] = '\0';
}

static bool tbb10aw_extract_title(const char *raw, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    if (raw == NULL) return false;
    const char *p = tbb10au_strcasestr(raw, "<title");
    if (p == NULL) return false;
    const char *gt = strchr(p, '>');
    if (gt == NULL) return false;
    const char *end = tbb10au_strcasestr(gt + 1, "</title");
    if (end == NULL) return false;
    tbb10aw_text_from_html_range(gt + 1, end, out, out_cap);
    return out[0] != '\0';
}


static bool tbb10ax_link_is_noise_for_page(const char *page_url, const char *url)
{
    if (url == NULL || url[0] == '\0') return true;

    if (strncasecmp(url, "javascript:", 11) == 0) return true;
    if (strncasecmp(url, "mailto:", 7) == 0) return true;
    if (strncasecmp(url, "tel:", 4) == 0) return true;
    if (strncasecmp(url, "data:", 5) == 0) return true;
    if (url[0] == '#') return true;

    /* Filtro ligero heredado del navegador viejo: no cambia la red,
     * solo evita mostrar/abrir enlaces de menus, privacidad, cuentas,
     * preferencias y buscadores internos que suelen dar error o pantallas
     * inutiles en navegador sin JavaScript.
     */
    if (tbb10au_strcasestr(url, "accounts.google.") != NULL) return true;
    if (tbb10au_strcasestr(url, "consent.google.") != NULL) return true;
    if (tbb10au_strcasestr(url, "support.google.") != NULL) return true;
    if (tbb10au_strcasestr(url, "policies.google.") != NULL) return true;
    if (tbb10au_strcasestr(url, "privacy") != NULL) return true;
    if (tbb10au_strcasestr(url, "terms") != NULL) return true;
    if (tbb10au_strcasestr(url, "preferences") != NULL) return true;
    if (tbb10au_strcasestr(url, "advanced_search") != NULL) return true;
    if (tbb10au_strcasestr(url, "setprefs") != NULL) return true;
    if (tbb10au_strcasestr(url, "tbm=isch") != NULL) return true;
    if (tbb10au_strcasestr(url, "webhp") != NULL) return true;

    if (page_url != NULL && tbb10au_strcasestr(page_url, "duckduckgo.com") != NULL) {
        if (tbb10au_strcasestr(url, "duckduckgo.com/settings") != NULL) return true;
        if (tbb10au_strcasestr(url, "duckduckgo.com/feedback") != NULL) return true;
        if (tbb10au_strcasestr(url, "duckduckgo.com/params") != NULL) return true;
        if (tbb10au_strcasestr(url, "duckduckgo.com/privacy") != NULL) return true;
        if (tbb10au_strcasestr(url, "duckduckgo.com/duckduckgo-help-pages") != NULL) return true;
    }

    return false;
}

static bool tbb10ay_url_is_account_or_login_page(const char *url)
{
    if (url == NULL) return false;

    /* 10AZ SMART CLEANER:
     * Paginas de cuenta/login/consentimiento suelen depender de JavaScript,
     * cookies modernas y CSS enorme. En HTML10D no aportan navegacion util y
     * terminan mostrando basura tipo keyframes/webkit. Mejor explicarlo claro.
     */
    if (tbb10au_strcasestr(url, "accounts.google.") != NULL) return true;
    if (tbb10au_strcasestr(url, "consent.google.") != NULL) return true;
    if (tbb10au_strcasestr(url, "myaccount.google.") != NULL) return true;
    if (tbb10au_strcasestr(url, "ogs.google.") != NULL) return true;
    if (tbb10au_strcasestr(url, "/signin") != NULL) return true;
    if (tbb10au_strcasestr(url, "/login") != NULL) return true;
    if (tbb10au_strcasestr(url, "/oauth") != NULL) return true;
    if (tbb10au_strcasestr(url, "ServiceLogin") != NULL) return true;
    if (tbb10au_strcasestr(url, "identifier") != NULL &&
        tbb10au_strcasestr(url, "google") != NULL) return true;
    return false;
}

static bool tbb10az_page_looks_like_antibot(const char *raw)
{
    if (raw == NULL) return false;
    if (tbb10au_strcasestr(raw, "bots use DuckDuckGo too") != NULL) return true;
    if (tbb10au_strcasestr(raw, "search was made by a human") != NULL) return true;
    if (tbb10au_strcasestr(raw, "complete the following challenge") != NULL) return true;
    if (tbb10au_strcasestr(raw, "unusual traffic") != NULL) return true;
    return false;
}

static bool tbb10ay_text_looks_like_css_or_js_noise(const char *text)
{
    if (text == NULL || text[0] == '\0') return false;
    int hits = 0;
    if (tbb10au_strcasestr(text, "@keyframes") != NULL) hits += 3;
    if (tbb10au_strcasestr(text, "-webkit-") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "animation-") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "transform:") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "translate(") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "cubic-bezier") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "rgba(") != NULL) hits += 1;
    if (tbb10au_strcasestr(text, "var(--") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "function(") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "document.") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "window.") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "@media") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "@font-face") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "--mdc-") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "-moz-") != NULL) hits += 1;
    if (tbb10au_strcasestr(text, "addEventListener") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "JSON.parse") != NULL) hits += 2;
    if (tbb10au_strcasestr(text, "webpack") != NULL) hits += 2;

    int braces = 0;
    int semis = 0;
    for (const char *p = text; *p; p++) {
        if (*p == '{' || *p == '}') braces++;
        if (*p == ';') semis++;
    }
    if (braces >= 2) hits += 2;
    if (semis >= 6) hits += 1;
    return hits >= 3;
}

static bool tbb10az_line_looks_like_css_or_js_noise(const char *line)
{
    if (line == NULL || line[0] == '\0') return false;

    int hits = 0;
    if (tbb10au_strcasestr(line, "@keyframes") != NULL) hits += 3;
    if (tbb10au_strcasestr(line, "-webkit") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "-moz-") != NULL) hits += 1;
    if (tbb10au_strcasestr(line, "transform") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "translate(") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "cubic-bezier") != NULL) hits += 3;
    if (tbb10au_strcasestr(line, "animation") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "var(--") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "--mdc-") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "function(") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "document.") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "window.") != NULL) hits += 2;
    if (tbb10au_strcasestr(line, "!important") != NULL) hits += 1;

    int braces = 0;
    int semis = 0;
    int colons = 0;
    int useful = 0;
    for (const char *p = line; *p; p++) {
        if (*p == '{' || *p == '}') braces++;
        else if (*p == ';') semis++;
        else if (*p == ':') colons++;
        else if (isalnum((unsigned char)*p)) useful++;
    }
    if (braces >= 1 && semis >= 2) hits += 2;
    if (semis >= 5 && colons >= 3) hits += 2;
    if (useful < 4 && (braces + semis + colons) > 4) hits += 2;

    return hits >= 3;
}

static void tbb10bd_strip_wiki_edit_markers(char *line)
{
    if (line == NULL || line[0] == '\0') return;
    const char *marks[] = { "[editar]", "[edit]", "[Editar]", "[Edit]" };
    for (size_t m = 0; m < sizeof(marks) / sizeof(marks[0]); m++) {
        const char *mark = marks[m];
        size_t ml = strlen(mark);
        char *p = NULL;
        while ((p = strstr(line, mark)) != NULL) {
            memmove(p, p + ml, strlen(p + ml) + 1);
        }
    }
    /* Compacta espacios que queden tras quitar marcas. */
    char *dst = line;
    int last_sp = 1;
    for (char *src = line; *src; src++) {
        unsigned char c = (unsigned char)*src;
        if (isspace(c)) {
            if (!last_sp) *dst++ = ' ';
            last_sp = 1;
        } else {
            *dst++ = *src;
            last_sp = 0;
        }
    }
    while (dst > line && isspace((unsigned char)dst[-1])) dst--;
    *dst = '\0';
}

static bool tbb10bd_line_is_low_value_noise(const char *line)
{
    if (line == NULL) return true;
    while (*line && isspace((unsigned char)*line)) line++;
    if (*line == '\0') return true;
    if (strcasecmp(line, "editar") == 0) return true;
    if (strcasecmp(line, "edit") == 0) return true;
    if (strcasecmp(line, "[editar]") == 0) return true;
    if (strcasecmp(line, "[edit]") == 0) return true;
    if (strcasecmp(line, "skip to content") == 0) return true;
    if (strcasecmp(line, "toggle navigation") == 0) return true;
    if (strcasecmp(line, "appearance settings") == 0) return true;
    return false;
}

static void tbb10aw_append_wrapped_text_as_paras(char *html, size_t cap, const char *text)
{
    if (text == NULL || text[0] == '\0') return;

    /* 10BD:
     * El extractor HTML10D ahora conserva saltos por <p>/<div>/<li>/<h*>.
     * Aqui tratamos cada bloque por separado, quitamos controles de Wikipedia
     * como [editar] y envolvemos en lineas legibles. No tocamos red ni Lexbor.
     */
    const int chunk = 84;
    int emitted = 0;
    const char *p = text;

    while (*p && emitted < 90) {
        while (*p == '\r' || *p == '\n' || *p == '\t' || *p == ' ') p++;
        if (*p == '\0') break;

        const char *e = p;
        while (*e && *e != '\n' && *e != '\r') e++;

        char para[512];
        size_t len = (size_t)(e - p);
        if (len >= sizeof(para)) len = sizeof(para) - 1;
        memcpy(para, p, len);
        para[len] = '\0';
        tbb10ah_chomp(para);
        tbb10bd_strip_wiki_edit_markers(para);

        if (para[0] && !tbb10bd_line_is_low_value_noise(para) &&
            !tbb10az_line_looks_like_css_or_js_noise(para)) {
            int n = (int)strlen(para);
            int start = 0;
            while (start < n && emitted < 90) {
                while (start < n && isspace((unsigned char)para[start])) start++;
                if (start >= n) break;

                int end = start + chunk;
                if (end > n) end = n;
                else {
                    int best = end;
                    for (int i = end; i > start + 28; i--) {
                        if (isspace((unsigned char)para[i])) { best = i; break; }
                    }
                    end = best;
                }

                char line[128];
                int l = end - start;
                if (l > (int)sizeof(line) - 1) l = sizeof(line) - 1;
                memcpy(line, para + start, l);
                line[l] = '\0';
                tbb10ah_chomp(line);
                if (line[0] && !tbb10az_line_looks_like_css_or_js_noise(line)) {
                    tbb10ah_safe_append(html, cap, "<p>");
                    tbb10ah_html_escape_append(html, cap, line);
                    tbb10ah_safe_append(html, cap, "</p>\n");
                    emitted++;
                }
                start = end + 1;
            }
        }

        p = (*e) ? (e + 1) : e;
        if ((emitted & 0x07) == 0) vTaskDelay(1);
    }
}


/* -------------------------------------------------------------------------
 * 10BG - Aplanador HTML10D legado portado desde minipc_browser.c
 * -------------------------------------------------------------------------
 * El camino anterior extraia primero todos los enlaces y luego un bloque de
 * texto. El navegador viejo funcionaba mejor porque recorria el HTML en orden:
 * enlaces en su sitio, [IMG: alt], listas, tablas con separadores, saltos de
 * bloque y formularios. Esta capa genera una pagina HTML simple para que
 * Lexbor solo la pinte, pero la semantica viene del aplanador legado.
 */
typedef struct {
    char  *html;
    size_t cap;
    int    col;
    int    link_no;
    bool   line_open;
    bool   last_space;
    bool   in_anchor;
    int    emitted_lines;
} tbb10bg_flat_ctx_t;

static void tbb10bg_line_open(tbb10bg_flat_ctx_t *ctx)
{
    if (ctx == NULL || ctx->line_open) return;
    tbb10ah_safe_append(ctx->html, ctx->cap, "<p>");
    ctx->line_open = true;
    ctx->last_space = true;
    ctx->col = 0;
}

static void tbb10bg_line_break(tbb10bg_flat_ctx_t *ctx)
{
    if (ctx == NULL) return;
    if (ctx->in_anchor) return; /* no partir dentro de un <a> */
    if (ctx->line_open) {
        tbb10ah_safe_append(ctx->html, ctx->cap, "</p>\n");
        ctx->emitted_lines++;
    }
    ctx->line_open = false;
    ctx->last_space = true;
    ctx->col = 0;
}

static void tbb10bg_text_append_raw(tbb10bg_flat_ctx_t *ctx, const char *txt)
{
    if (ctx == NULL || txt == NULL || txt[0] == '\0') return;
    tbb10bg_line_open(ctx);
    tbb10ah_html_escape_append(ctx->html, ctx->cap, txt);
    ctx->last_space = false;
    ctx->col += (int)strlen(txt);
}

static void tbb10bg_text_char(tbb10bg_flat_ctx_t *ctx, char c)
{
    if (ctx == NULL) return;
    unsigned char uc = (unsigned char)c;
    if (uc < 32 || c == '\r' || c == '\n' || c == '\t') c = ' ';

    if (c == ' ') {
        if (ctx->last_space) return;
        if (!ctx->in_anchor && ctx->col >= 82) {
            tbb10bg_line_break(ctx);
            return;
        }
        tbb10bg_line_open(ctx);
        tbb10ah_safe_append(ctx->html, ctx->cap, " ");
        ctx->last_space = true;
        ctx->col++;
        return;
    }

    if (!ctx->in_anchor && ctx->col >= 90) {
        tbb10bg_line_break(ctx);
    }

    tbb10bg_line_open(ctx);
    char tmp[2] = { c, '\0' };
    tbb10ah_html_escape_append(ctx->html, ctx->cap, tmp);
    ctx->last_space = false;
    ctx->col++;
}

static void tbb10bg_entity_to_html(tbb10bg_flat_ctx_t *ctx, const char *entity, int *consumed)
{
    if (consumed) *consumed = 0;
    if (ctx == NULL || entity == NULL) return;
    char tmp[16];
    tmp[0] = '\0';
    int cons = 0;
    if (tbb10be_entity_append_utf8(tmp, sizeof(tmp), entity, &cons) && cons > 0) {
        tbb10bg_text_append_raw(ctx, tmp);
        if (consumed) *consumed = cons;
    }
}

static void tbb10bg_cell_separator(tbb10bg_flat_ctx_t *ctx)
{
    if (ctx == NULL) return;
    if (ctx->col > 0 && !ctx->last_space) {
        /* Separador de columna estilo navegador viejo, un poco alineado.
         *
         * IMPORTANTE 10BG_FIX1:
         * No usar tbb10bg_text_char(ctx, ' ') dentro de este bucle.
         * Esa funcion compacta espacios repetidos; tras el primer espacio
         * deja last_space=true y los siguientes retornan sin incrementar col.
         * Resultado: while eterno en tablas/celdas y salto del WDT.
         */
        int target = ((ctx->col / 18) + 1) * 18;
        tbb10bg_line_open(ctx);
        uint32_t guard = 0;
        while (ctx->col < target && ctx->col < 86) {
            tbb10ah_safe_append(ctx->html, ctx->cap, " ");
            ctx->col++;
            ctx->last_space = true;
            if ((++guard & 0x1Fu) == 0) vTaskDelay(1);
        }
        tbb10bg_text_append_raw(ctx, "| ");
    }
}

static bool tbb10bg_tag_name(const char *tag, const char *gt, char *out, size_t out_cap, bool *closing)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    if (closing) *closing = false;
    if (tag == NULL || gt == NULL || tag >= gt || *tag != '<') return false;
    const char *p = tag + 1;
    if (*p == '/') { if (closing) *closing = true; p++; }
    while (p < gt && isspace((unsigned char)*p)) p++;
    size_t n = 0;
    while (p < gt && (isalnum((unsigned char)*p) || *p == '-' || *p == '_') && n + 1 < out_cap) {
        out[n++] = (char)tolower((unsigned char)*p++);
    }
    out[n] = '\0';
    return n > 0;
}

static void tbb10bg_emit_img_alt(tbb10bg_flat_ctx_t *ctx, const char *tag, const char *gt)
{
    char alt[180];
    alt[0] = '\0';
    if (!tbb10aw_extract_attr_from_tag(tag, gt, "alt", alt, sizeof(alt))) {
        (void)tbb10aw_extract_attr_from_tag(tag, gt, "title", alt, sizeof(alt));
    }
    if (alt[0] == '\0') return;
    tbb10bg_text_append_raw(ctx, " [IMG: ");
    tbb10bg_text_append_raw(ctx, alt);
    tbb10bg_text_append_raw(ctx, "] ");
}

static int tbb10bg_append_legacy_flattened_html(char *html, size_t cap, const char *url, const char *raw, const char *end)
{
    if (html == NULL || cap == 0 || raw == NULL) return 0;
    if (end == NULL) end = raw + strlen(raw);

    tbb10bg_flat_ctx_t ctx = {
        .html = html, .cap = cap, .col = 0, .link_no = 0,
        .line_open = false, .last_space = true, .in_anchor = false, .emitted_lines = 0
    };

    const char *p = raw;
    uint32_t guard = 0;
    while (p < end && *p != '\0' && ctx.emitted_lines < 180) {
        if ((++guard & 0x07FFu) == 0) vTaskDelay(1);

        if (strncmp(p, "<!--", 4) == 0) {
            const char *ce = strstr(p + 4, "-->");
            p = (ce && ce < end) ? ce + 3 : end;
            continue;
        }

        if (*p == '<') {
            if (tbb10aw_tag_starts(p, "script")) { p = tbb10aw_skip_until_close_tag(p, end, "script"); continue; }
            if (tbb10aw_tag_starts(p, "style"))  { p = tbb10aw_skip_until_close_tag(p, end, "style"); continue; }
            if (tbb10aw_tag_starts(p, "noscript")) { p = tbb10aw_skip_until_close_tag(p, end, "noscript"); continue; }
            if (tbb10aw_tag_starts(p, "svg"))    { p = tbb10aw_skip_until_close_tag(p, end, "svg"); continue; }
            if (tbb10aw_tag_starts(p, "canvas")) { p = tbb10aw_skip_until_close_tag(p, end, "canvas"); continue; }

            const char *gt = strchr(p, '>');
            if (gt == NULL || gt >= end) break;

            char name[24];
            bool closing = false;
            if (tbb10bg_tag_name(p, gt, name, sizeof(name), &closing)) {
                if (!closing && strcmp(name, "a") == 0) {
                    char href[TBB10AL_URL_CAP];
                    char absu[TBB10AL_URL_CAP];
                    href[0] = absu[0] = '\0';
                    if (tbb10aw_extract_attr_from_tag(p, gt, "href", href, sizeof(href)) &&
                        tbb10au_resolve_url_like_old(url, href, absu, sizeof(absu))) {
                        char unwrapped[TBB10AL_URL_CAP];
                        if (tbb10au_unwrap_google_or_ddg_url(absu, unwrapped, sizeof(unwrapped))) {
                            tbb10ah_safe_copy(absu, sizeof(absu), unwrapped);
                        }
                        if (!tbb10ax_link_is_noise_for_page(url, absu) && ctx.link_no < TBB10AH_MAX_LINKS) {
                            char mark[32];
                            snprintf(mark, sizeof(mark), "<a href=\"");
                            tbb10bg_line_open(&ctx);
                            tbb10ah_safe_append(html, cap, mark);
                            tbb10ah_html_escape_append(html, cap, absu);
                            snprintf(mark, sizeof(mark), "\">[%02d] ", ctx.link_no + 1);
                            tbb10ah_safe_append(html, cap, mark);
                            ctx.in_anchor = true;
                            ctx.link_no++;
                        }
                    }
                }
                else if (closing && strcmp(name, "a") == 0) {
                    if (ctx.in_anchor) {
                        tbb10ah_safe_append(html, cap, "</a>");
                        ctx.in_anchor = false;
                        ctx.last_space = false;
                    }
                }
                else if (!closing && strcmp(name, "img") == 0) {
                    tbb10bg_emit_img_alt(&ctx, p, gt);
                }
                else if (!closing && strcmp(name, "input") == 0) {
                    /* El escaneo FORM-LITE real ya se hizo antes. Aqui solo
                     * evitamos volcar inputs/atributos como texto.
                     */
                }
                else if (!closing && strcmp(name, "li") == 0) {
                    tbb10bg_line_break(&ctx);
                    tbb10bg_text_append_raw(&ctx, "- ");
                }
                else if (!closing && (strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
                                      strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0)) {
                    tbb10bg_line_break(&ctx);
                    tbb10bg_text_append_raw(&ctx, "== ");
                }
                else if (closing && (strcmp(name, "h1") == 0 || strcmp(name, "h2") == 0 ||
                                     strcmp(name, "h3") == 0 || strcmp(name, "h4") == 0)) {
                    tbb10bg_text_append_raw(&ctx, " ==");
                    tbb10bg_line_break(&ctx);
                }
                else if (!closing && (strcmp(name, "tr") == 0 || strcmp(name, "table") == 0 ||
                                      strcmp(name, "section") == 0 || strcmp(name, "article") == 0 ||
                                      strcmp(name, "ul") == 0 || strcmp(name, "ol") == 0 ||
                                      strcmp(name, "div") == 0 || strcmp(name, "p") == 0)) {
                    tbb10bg_line_break(&ctx);
                }
                else if (closing && strcmp(name, "tr") == 0) {
                    tbb10bg_line_break(&ctx);
                }
                else if (!closing && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
                    tbb10bg_cell_separator(&ctx);
                }
                else if (!closing && (strcmp(name, "br") == 0 || strcmp(name, "hr") == 0)) {
                    tbb10bg_line_break(&ctx);
                }
            }

            p = gt + 1;
            continue;
        }

        if (*p == '&') {
            int cons = 0;
            tbb10bg_entity_to_html(&ctx, p, &cons);
            if (cons > 0) { p += cons; continue; }
        }

        tbb10bg_text_char(&ctx, *p++);
    }

    if (ctx.in_anchor) {
        tbb10ah_safe_append(html, cap, "</a>");
        ctx.in_anchor = false;
    }
    tbb10bg_line_break(&ctx);
    return ctx.link_no;
}

static char *tbb10aw_make_classic_html10d_page(const char *url, const char *raw, size_t raw_len)
{
    size_t cap = TBB10AL_HTTP_MAX + 1;
    char *html = (char *) heap_caps_calloc(1, cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (html == NULL) html = (char *) heap_caps_calloc(1, cap, MALLOC_CAP_8BIT);
    if (html == NULL) return NULL;

    char host[96];
    char title[160];

    /* 10AX:
     * NO poner 8 KB en la pila de breezy_repl.
     * En la 10AW inicial `char visible[8192]` iba en stack y en paginas
     * grandes podia corromper la tarea hasta acabar en InstrFetchProhibited
     * con PC tipo 0x11061106. Lo movemos a PSRAM/heap.
     */
    const size_t visible_cap = 8192;
    char *visible = (char *) heap_caps_calloc(1, visible_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (visible == NULL) visible = (char *) heap_caps_calloc(1, visible_cap, MALLOC_CAP_8BIT);
    if (visible == NULL) {
        free(html);
        return NULL;
    }

    tbb10aw_host_from_url(url, host, sizeof(host));
    if (!tbb10aw_extract_title(raw, title, sizeof(title))) tbb10ah_safe_copy(title, sizeof(title), host[0] ? host : "sin titulo");
    tbb10bb_scan_forms_html10d(url, raw);

    if (tbb10ay_url_is_account_or_login_page(url)) {
        tbb10ah_safe_append(html, cap, "<!doctype html><html><head><title>Pagina de cuenta/login</title></head><body>");
        tbb10ah_safe_append(html, cap, "<h2>HTML10D: pagina de cuenta/login</h2>");
        tbb10ah_safe_append(html, cap, "<p>WEB: ");
        tbb10ah_html_escape_append(html, cap, host);
        tbb10ah_safe_append(html, cap, "</p><h2>TITULO: ");
        tbb10ah_html_escape_append(html, cap, title);
        tbb10ah_safe_append(html, cap, "</h2><hr>");
        tbb10ah_safe_append(html, cap, "<p>Esta pagina es de login/cuenta y normalmente requiere JavaScript, cookies y formularios modernos.</p>");
        tbb10ah_safe_append(html, cap, "<p>La oculto en modo texto para no mostrar CSS/JS basura.</p>");
        tbb10ah_safe_append(html, cap, "<p>Use ATRAS o busque con gb: texto / wiki: texto. DDG queda disponible con ddg: texto.</p>");
        tbb10ah_safe_append(html, cap, "</body></html>");
        heap_caps_free(visible);
        return html;
    }

    if (tbb10az_page_looks_like_antibot(raw)) {
        tbb10ah_safe_append(html, cap, "<!doctype html><html><head><title>Antibot / desafio</title></head><body>");
        tbb10ah_safe_append(html, cap, "<h2>HTML10D: servidor pide verificacion</h2>");
        tbb10ah_safe_append(html, cap, "<p>WEB: ");
        tbb10ah_html_escape_append(html, cap, host);
        tbb10ah_safe_append(html, cap, "</p><h2>TITULO: ");
        tbb10ah_html_escape_append(html, cap, title);
        tbb10ah_safe_append(html, cap, "</h2><hr>");
        tbb10ah_safe_append(html, cap, "<p>El servidor ha devuelto una pantalla anti-bot/desafio. No es fallo de WiFi ni de Lexbor.</p>");
        tbb10ah_safe_append(html, cap, "<p>Este MiniPC no ejecuta JavaScript, asi que no puede completar ese desafio.</p>");

        char qanti[128];
        qanti[0] = '\0';
        if (!tbb10au_extract_query_param(url, "q", qanti, sizeof(qanti))) {
            (void)tbb10au_extract_query_param(url, "query", qanti, sizeof(qanti));
        }
        if (qanti[0] != '\0') {
            char alt[TBB10AL_URL_CAP];
            tbb10ah_safe_append(html, cap, "<p>Busqueda detectada: <b>");
            tbb10ah_html_escape_append(html, cap, qanti);
            tbb10ah_safe_append(html, cap, "</b></p><ul>");
            if (tbb10bc_make_google_basic_search_url(qanti, alt, sizeof(alt))) {
                tbb10ah_safe_append(html, cap, "<li><a href=\"");
                tbb10ah_html_escape_append(html, cap, alt);
                tbb10ah_safe_append(html, cap, "\">Buscar en Google basico</a></li>");
            }
            if (tbb10bc_make_wikipedia_search_url(qanti, alt, sizeof(alt))) {
                tbb10ah_safe_append(html, cap, "<li><a href=\"");
                tbb10ah_html_escape_append(html, cap, alt);
                tbb10ah_safe_append(html, cap, "\">Buscar en Wikipedia</a></li>");
            }
            tbb10ah_safe_append(html, cap, "</ul>");
        }
        tbb10ah_safe_append(html, cap, "<p>Tambien puede escribir en URL: <b>gb: texto</b> o <b>wiki: texto</b>.</p>");
        tbb10ah_safe_append(html, cap, "<p>Use ATRAS, espere un rato o abra una pagina directa.</p>");
        tbb10ah_safe_append(html, cap, "</body></html>");
        heap_caps_free(visible);
        return html;
    }

    tbb10aw_text_from_html_range(raw, raw ? raw + raw_len : NULL, visible, visible_cap);
    if (tbb10ay_text_looks_like_css_or_js_noise(visible)) {
        visible[0] = '\0';
    }

    tbb10ah_safe_append(html, cap, "<!doctype html><html><head><title>");
    tbb10ah_html_escape_append(html, cap, title);
    tbb10ah_safe_append(html, cap, "</title></head><body>");
    char tmp[192];
    snprintf(tmp, sizeof(tmp), "<h2>HTML10D: %u bytes</h2>", (unsigned)raw_len);
    tbb10ah_safe_append(html, cap, tmp);
    tbb10ah_safe_append(html, cap, "<p>WEB: ");
    tbb10ah_html_escape_append(html, cap, host);
    tbb10ah_safe_append(html, cap, "</p><h2>TITULO: ");
    tbb10ah_html_escape_append(html, cap, title);
    tbb10ah_safe_append(html, cap, "</h2><hr>");

    if (s_tbb10bb_form_available) {
        if (s_tbb10bp_form_is_post) {
            tbb10ah_safe_append(html, cap, "<p><a href=\"form://search\">[FORM POST] ESCRIBIR Y ENVIAR</a></p>\n<hr>");
        } else {
            tbb10ah_safe_append(html, cap, "<p><a href=\"form://search\">[FORM GET] BUSCAR: toque aqui o escriba ?texto en URL</a></p>\n<hr>");
        }
    }

    if (visible[0] == '\0') {
        tbb10ah_safe_append(html, cap, "<p>[HTML10D] Se ha ocultado contenido CSS/JavaScript sin texto util para esta pantalla.</p>");
    }

    /* 10BG: aplanador legado en orden de documento: enlaces en su sitio,
     * [IMG: alt], tablas con separador de columnas y saltos de bloque. */
    tbb10ah_safe_append(html, cap, "<hr>");
    int link_no = tbb10bg_append_legacy_flattened_html(html, cap, url, raw, raw ? raw + raw_len : NULL);

    if (link_no <= 0 && visible[0] != '\0') {
        tbb10aw_append_wrapped_text_as_paras(html, cap, visible);
    }

    snprintf(tmp, sizeof(tmp), "<p>HTML10D 10BH: %d enlaces extraidos</p>", link_no);
    tbb10ah_safe_append(html, cap, tmp);
    tbb10ah_safe_append(html, cap, "</body></html>");
    free(visible);
    return html;
}

static bool tbb10al_http_origin(const char *url, char *out, size_t out_cap)
{
    if (url == NULL || out == NULL || out_cap == 0 || !tbb10al_is_http_url(url)) return false;
    const char *p = strstr(url, "://");
    if (p == NULL) return false;
    p += 3;
    const char *slash = strchr(p, '/');
    size_t n = slash ? (size_t)(slash - url) : strlen(url);
    if (n >= out_cap) n = out_cap - 1;
    memcpy(out, url, n); out[n] = '\0';
    return true;
}

static bool __attribute__((unused)) tbb10al_resolve_http_url(const char *current, const char *href, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0 || current == NULL || href == NULL) return false;
    if (!tbb10al_is_http_url(current)) return false;
    char tmp[TBB10AL_URL_CAP];
    tbb10ah_safe_copy(tmp, sizeof(tmp), href);
    tbb10ah_chomp(tmp);
    tbb10al_strip_fragment_only(tmp);
    if (tmp[0] == '\0') return false;
    if (tbb10al_is_http_url(tmp)) { tbb10ah_safe_copy(out, out_cap, tmp); return true; }
    if (strncmp(tmp, "//", 2) == 0) { tbb10ah_safe_copy(out, out_cap, (strncmp(current, "https://", 8) == 0) ? "https:" : "http:"); tbb10ah_safe_append(out, out_cap, tmp); return true; }
    char origin[TBB10AL_URL_CAP];
    if (!tbb10al_http_origin(current, origin, sizeof(origin))) return false;
    if (tmp[0] == '/') { tbb10ah_safe_copy(out, out_cap, origin); tbb10ah_safe_append(out, out_cap, tmp); return true; }
    char base[TBB10AL_URL_CAP];
    tbb10ah_safe_copy(base, sizeof(base), current);
    char *scheme = strstr(base, "://");
    char *last = NULL;
    if (scheme != NULL) { char *path = strchr(scheme + 3, '/'); if (path != NULL) last = strrchr(path, '/'); }
    if (last != NULL) last[1] = '\0';
    else { tbb10ah_safe_copy(base, sizeof(base), origin); tbb10ah_safe_append(base, sizeof(base), "/"); }
    tbb10ah_safe_copy(out, out_cap, base); tbb10ah_safe_append(out, out_cap, tmp); return true;
}


static void tbb10ah_normalize_abs_path(char *path)
{
    if (path == NULL || path[0] != '/') return;

    char src[TBB10AH_PATH_CAP];
    tbb10ah_safe_copy(src, sizeof(src), path);

    const char *parts[32];
    int count = 0;
    char *p = src;

    while (*p != '\0') {
        while (*p == '/') p++;
        if (*p == '\0') break;

        char *start = p;
        while (*p != '\0' && *p != '/') p++;
        if (*p == '/') *p++ = '\0';

        if (strcmp(start, ".") == 0 || start[0] == '\0') {
            continue;
        }
        if (strcmp(start, "..") == 0) {
            if (count > 0) count--;
            continue;
        }
        if (count < (int)(sizeof(parts) / sizeof(parts[0]))) {
            parts[count++] = start;
        }
    }

    size_t pos = 0;
    path[pos++] = '/';
    path[pos] = '\0';

    for (int i = 0; i < count; i++) {
        size_t before = strlen(path);
        tbb10ah_safe_append(path, TBB10AH_PATH_CAP, parts[i]);
        if (strlen(path) == before) break;
        if (i != count - 1) {
            tbb10ah_safe_append(path, TBB10AH_PATH_CAP, "/");
        }
    }
}

static bool tbb10ah_resolve_path(const char *current, const char *href, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    if (href == NULL || href[0] == '\0') return false;

    char tmp[TBB10AH_PATH_CAP];
    tbb10ah_safe_copy(tmp, sizeof(tmp), href);

    /* 10AX_FIX1:
     * Los enlaces generados por la capa HTML10D pasan de nuevo por Lexbor.
     * Segun como entregue el atributo, una URL con query puede volver como
     * &amp; en vez de &. Antes eso acababa en peticiones raras y error de red
     * al tocar un link. Decodificamos entidades HTML justo antes de resolver.
     */
    char tmp_dec[TBB10AH_PATH_CAP];
    tbb10au_html_entity_decode_ascii(tmp_dec, sizeof(tmp_dec), tmp);
    if (tmp_dec[0] != '\0') tbb10ah_safe_copy(tmp, sizeof(tmp), tmp_dec);

    tbb10ah_chomp(tmp);
    if (tbb10al_is_http_url(tmp) || (current != NULL && tbb10al_is_http_url(current))) {
        tbb10al_strip_fragment_only(tmp);
    }
    else {
        tbb10ah_strip_fragment_query(tmp);
    }
    if (tmp[0] == '\0') return false;

    /* 10BK_FIX1:
     * Los botones/paginas internas (about:home, about:history,
     * about:bookmarks...) deben resolverse como internas SIEMPRE,
     * incluso si la pagina actual es http/https. Antes, al estar en
     * google.com, about:history se trataba como relativo y terminaba
     * intentando abrir google.com/about:history.
     */
    {
        char canon[TBB10AH_PATH_CAP];
        if (tbb10bk_canonical_internal_url(tmp, canon, sizeof(canon))) {
            tbb10ah_safe_copy(out, out_cap, canon);
            return true;
        }
    }

    if (tbb10al_is_http_url(tmp)) {
        char unwrapped[TBB10AL_URL_CAP];
        if (tbb10au_unwrap_google_or_ddg_url(tmp, unwrapped, sizeof(unwrapped))) tbb10ah_safe_copy(out, out_cap, unwrapped);
        else tbb10ah_safe_copy(out, out_cap, tmp);
        tbb10au_html_entity_decode_ascii(tmp_dec, sizeof(tmp_dec), out);
        if (tmp_dec[0] != '\0') tbb10ah_safe_copy(out, out_cap, tmp_dec);
        printf("[TBBROWSER10AX_FIX1][LINK] %s\n", out);
        return true;
    }

    if (current != NULL && tbb10al_is_http_url(current)) {
        /* Usar la resolucion de enlaces de la capa vieja tambien al abrir
         * enlaces tocados, no solo al construir la pagina HTML10D. */
        if (!tbb10au_resolve_url_like_old(current, tmp, out, out_cap)) return false;
        char unwrapped[TBB10AL_URL_CAP];
        if (tbb10au_unwrap_google_or_ddg_url(out, unwrapped, sizeof(unwrapped))) tbb10ah_safe_copy(out, out_cap, unwrapped);
        tbb10au_html_entity_decode_ascii(tmp_dec, sizeof(tmp_dec), out);
        if (tmp_dec[0] != '\0') tbb10ah_safe_copy(out, out_cap, tmp_dec);
        printf("[TBBROWSER10AX_FIX1][LINK] %s -> %s\n", tmp, out);
        return true;
    }

    if (tbb10ah_is_external_url(tmp)) {
        printf("[TBBROWSER10AL] Esquema externo no soportado: %s\n", tmp);
        return false;
    }

    if (tmp[0] == '/') {
        tbb10ah_safe_copy(out, out_cap, tmp);
        tbb10ah_normalize_abs_path(out);
        return true;
    }

    if (current != NULL && current[0] == '/') {
        char base[TBB10AH_PATH_CAP];
        tbb10ah_safe_copy(base, sizeof(base), current);
        char *slash = strrchr(base, '/');
        if (slash != NULL) slash[1] = '\0';
        else strcpy(base, "/");
        tbb10ah_safe_copy(out, out_cap, base);
        tbb10ah_safe_append(out, out_cap, tmp);
        tbb10ah_normalize_abs_path(out);
        return true;
    }

    tbb10ah_safe_copy(out, out_cap, "/sdcard/");
    tbb10ah_safe_append(out, out_cap, tmp);
    tbb10ah_normalize_abs_path(out);
    return true;
}


/* ========================= 10AH GUI FIRST LAYER =========================
 * Primer escalon grafico: usa el motor 10AE ya validado, pero pinta en
 * SM_400X240 con backbuffer 8bpp y teclado por vterm_getchar().
 * Sin CSS, sin imagenes, sin red. Solo grafica prudente sobre lo que vive.
 */
#define TBB10AH_GUI_W 400
#define TBB10AH_GUI_H 240

static uint8_t *s_tbb10ah_back = NULL;
/* 10AH_FIX2:
 * Mantener el modo grafico activo durante una sesion completa evita
 * entrar/salir de SM_400X240 en cada enlace. El crash observado venia
 * en el ISR del RGB bounce buffer tras varias navegaciones, compatible
 * con liberar el framebuffer mientras el LCD aun lo leia.
 */
static bool s_tbb10ah_keep_gui_mode = false;

/* 10AO: indicador de carga visible en cabecera.
 * No dependemos del overlay del puntero: pintamos un reloj de arena fijo
 * antes de entrar en operaciones bloqueantes y lo quitamos al terminar.
 */
static bool s_tbb10an_header_busy = false;

static void tbb10an_header_busy_set(bool busy)
{
    s_tbb10an_header_busy = busy;
}

static uint16_t tbb10ah_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);
}

enum {
    TBB10AH_C_BLACK  = 0,
    TBB10AH_C_BG     = 1,
    TBB10AH_C_TOP    = 2,
    TBB10AH_C_PANEL  = 3,
    TBB10AH_C_PANEL2 = 4,
    TBB10AH_C_BORDER = 5,
    TBB10AH_C_TEXT   = 6,
    TBB10AH_C_MUTED  = 7,
    TBB10AH_C_LINK   = 8,
    TBB10AH_C_GOOD   = 9,
    TBB10AH_C_WARN   = 10,
    TBB10AH_C_SHADOW = 11,
    TBB10AH_C_WHITE  = 12,
    TBB10BP_C_RED    = 13,
    TBB10BP_C_BLUE   = 14,
    TBB10BP_C_ORANGE = 15,
    TBB10BP_C_GRAY   = 16,
    TBB10BP_C_CYAN   = 17,
};

static void tbb10ah_gui_palette(void)
{
    uint16_t pal[256] = {0};
    pal[TBB10AH_C_BLACK]  = tbb10ah_rgb565(0, 0, 0);
    pal[TBB10AH_C_BG]     = tbb10ah_rgb565(9, 16, 30);
    pal[TBB10AH_C_TOP]    = tbb10ah_rgb565(18, 28, 48);
    pal[TBB10AH_C_PANEL]  = tbb10ah_rgb565(22, 34, 55);
    pal[TBB10AH_C_PANEL2] = tbb10ah_rgb565(34, 50, 78);
    pal[TBB10AH_C_BORDER] = tbb10ah_rgb565(80, 118, 160);
    pal[TBB10AH_C_TEXT]   = tbb10ah_rgb565(236, 242, 255);
    pal[TBB10AH_C_MUTED]  = tbb10ah_rgb565(150, 165, 185);
    pal[TBB10AH_C_LINK]   = tbb10ah_rgb565(90, 205, 245);
    pal[TBB10AH_C_GOOD]   = tbb10ah_rgb565(80, 220, 130);
    pal[TBB10AH_C_WARN]   = tbb10ah_rgb565(245, 205, 70);
    pal[TBB10AH_C_SHADOW] = tbb10ah_rgb565(3, 7, 14);
    pal[TBB10AH_C_WHITE]  = tbb10ah_rgb565(255, 255, 255);
    pal[TBB10BP_C_RED]    = tbb10ah_rgb565(245, 90, 90);
    pal[TBB10BP_C_BLUE]   = tbb10ah_rgb565(95, 150, 255);
    pal[TBB10BP_C_ORANGE] = tbb10ah_rgb565(255, 155, 65);
    pal[TBB10BP_C_GRAY]   = tbb10ah_rgb565(175, 180, 190);
    pal[TBB10BP_C_CYAN]   = tbb10ah_rgb565(80, 220, 235);
    for (int i = 32; i < 256; i++) {
        uint8_t v = (uint8_t)i;
        pal[i] = tbb10ah_rgb565(v / 4, v / 3, v / 2);
    }
    rgb_display_set_vga_palette(pal);
}

static inline uint8_t *tbb10ah_fb(void)
{
    return s_tbb10ah_back;
}

static void tbb10ah_gfx_pixel(int x, int y, uint8_t color)
{
    uint8_t *fb = tbb10ah_fb();
    if (fb != NULL && x >= 0 && x < TBB10AH_GUI_W && y >= 0 && y < TBB10AH_GUI_H) {
        fb[y * TBB10AH_GUI_W + x] = color;
    }
}

static void tbb10ah_gfx_clear(uint8_t color)
{
    uint8_t *fb = tbb10ah_fb();
    if (fb != NULL) memset(fb, color, TBB10AH_GUI_W * TBB10AH_GUI_H);
}

static void tbb10ah_gfx_hline(int x, int y, int w, uint8_t color)
{
    uint8_t *fb = tbb10ah_fb();
    if (fb == NULL || y < 0 || y >= TBB10AH_GUI_H || w <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > TBB10AH_GUI_W) w = TBB10AH_GUI_W - x;
    if (w <= 0) return;
    memset(&fb[y * TBB10AH_GUI_W + x], color, (size_t) w);
}

static void tbb10ah_gfx_vline(int x, int y, int h, uint8_t color)
{
    uint8_t *fb = tbb10ah_fb();
    if (fb == NULL || x < 0 || x >= TBB10AH_GUI_W || h <= 0) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > TBB10AH_GUI_H) h = TBB10AH_GUI_H - y;
    if (h <= 0) return;
    uint8_t *p = &fb[y * TBB10AH_GUI_W + x];
    for (int i = 0; i < h; i++) {
        *p = color;
        p += TBB10AH_GUI_W;
    }
}

static void tbb10ah_gfx_rect(int x, int y, int w, int h, uint8_t color)
{
    if (w <= 0 || h <= 0) return;
    tbb10ah_gfx_hline(x, y, w, color);
    tbb10ah_gfx_hline(x, y + h - 1, w, color);
    if (h > 2) {
        tbb10ah_gfx_vline(x, y + 1, h - 2, color);
        tbb10ah_gfx_vline(x + w - 1, y + 1, h - 2, color);
    }
}

static void tbb10ah_gfx_rectfill(int x, int y, int w, int h, uint8_t color)
{
    uint8_t *fb = tbb10ah_fb();
    if (fb == NULL || w <= 0 || h <= 0) return;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > TBB10AH_GUI_W) x1 = TBB10AH_GUI_W;
    if (y1 > TBB10AH_GUI_H) y1 = TBB10AH_GUI_H;
    int cw = x1 - x0;
    if (cw <= 0 || y1 <= y0) return;
    for (int row = y0; row < y1; row++) {
        memset(&fb[row * TBB10AH_GUI_W + x0], color, (size_t) cw);
    }
}

/*
 * Fuente 5x7 muy compacta. Soporta mayusculas, minusculas por conversión,
 * numeros y algunos signos.
 */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0 space
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
};

static uint8_t tbb10ah_glyph_for_char(char c, uint8_t col)
{
    // Minusculas con glyphs propios (a-z) en 5x7, column-major (LSB = arriba).
    // Asi se ven minusculas de verdad, no convertidas a mayuscula.
    static const uint8_t font_low[26][5] = {
        {0x20,0x54,0x54,0x54,0x78}, // a
        {0x7F,0x48,0x44,0x44,0x38}, // b
        {0x38,0x44,0x44,0x44,0x20}, // c
        {0x38,0x44,0x44,0x48,0x7F}, // d
        {0x38,0x54,0x54,0x54,0x18}, // e
        {0x08,0x7E,0x09,0x01,0x02}, // f
        {0x0C,0x52,0x52,0x52,0x3E}, // g
        {0x7F,0x08,0x04,0x04,0x78}, // h
        {0x00,0x44,0x7D,0x40,0x00}, // i
        {0x20,0x40,0x44,0x3D,0x00}, // j
        {0x7F,0x10,0x28,0x44,0x00}, // k
        {0x00,0x41,0x7F,0x40,0x00}, // l
        {0x7C,0x04,0x18,0x04,0x78}, // m
        {0x7C,0x08,0x04,0x04,0x78}, // n
        {0x38,0x44,0x44,0x44,0x38}, // o
        {0x7C,0x14,0x14,0x14,0x08}, // p
        {0x08,0x14,0x14,0x18,0x7C}, // q
        {0x7C,0x08,0x04,0x04,0x08}, // r
        {0x48,0x54,0x54,0x54,0x20}, // s
        {0x04,0x3F,0x44,0x40,0x20}, // t
        {0x3C,0x40,0x40,0x20,0x7C}, // u
        {0x1C,0x20,0x40,0x20,0x1C}, // v
        {0x3C,0x40,0x30,0x40,0x3C}, // w
        {0x44,0x28,0x10,0x28,0x44}, // x
        {0x0C,0x50,0x50,0x50,0x3C}, // y
        {0x44,0x64,0x54,0x4C,0x44}, // z
    };
    if (c >= 'a' && c <= 'z') return font_low[c - 'a'][col];
    if (c >= 'A' && c <= 'Z') return font5x7[1 + (c - 'A')][col];

    static const uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},
        {0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10},
        {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30},
        {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},
        {0x06,0x49,0x49,0x29,0x1E},
    };
    if (c >= '0' && c <= '9') return digits[c - '0'][col];

    switch (c) {
        case '.': { static const uint8_t g[5]={0x00,0x60,0x60,0x00,0x00}; return g[col]; }
        case ':': { static const uint8_t g[5]={0x00,0x36,0x36,0x00,0x00}; return g[col]; }
        case '/': { static const uint8_t g[5]={0x20,0x10,0x08,0x04,0x02}; return g[col]; }
        case '-': { static const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; return g[col]; }
        case '_': { static const uint8_t g[5]={0x40,0x40,0x40,0x40,0x40}; return g[col]; }
        case '+': { static const uint8_t g[5]={0x08,0x08,0x3E,0x08,0x08}; return g[col]; }
        case '%': { static const uint8_t g[5]={0x23,0x13,0x08,0x64,0x62}; return g[col]; }
        case '[': { static const uint8_t g[5]={0x00,0x7F,0x41,0x41,0x00}; return g[col]; }
        case ']': { static const uint8_t g[5]={0x00,0x41,0x41,0x7F,0x00}; return g[col]; }
        case '|': { static const uint8_t g[5]={0x00,0x00,0x7F,0x00,0x00}; return g[col]; }
        case '=': { static const uint8_t g[5]={0x14,0x14,0x14,0x14,0x14}; return g[col]; }
        case '>': { static const uint8_t g[5]={0x00,0x41,0x22,0x14,0x08}; return g[col]; }
        case '<': { static const uint8_t g[5]={0x08,0x14,0x22,0x41,0x00}; return g[col]; }
        case '!': { static const uint8_t g[5]={0x00,0x00,0x5F,0x00,0x00}; return g[col]; }
        case '*': { static const uint8_t g[5]={0x14,0x08,0x3E,0x08,0x14}; return g[col]; }
        case '?': { static const uint8_t g[5]={0x02,0x01,0x51,0x09,0x06}; return g[col]; }
        case '(': { static const uint8_t g[5]={0x00,0x1C,0x22,0x41,0x00}; return g[col]; }
        case ')': { static const uint8_t g[5]={0x00,0x41,0x22,0x1C,0x00}; return g[col]; }
        default: return font5x7[0][col];
    }
}

static void tbb10ah_draw_char5(int x, int y, char c, uint8_t color)
{
    for (int cx = 0; cx < 5; cx++) {
        uint8_t bits = tbb10ah_glyph_for_char(c, cx);
        for (int cy = 0; cy < 7; cy++) {
            if (bits & (1 << cy)) {
                tbb10ah_gfx_pixel(x + cx, y + cy, color);
            }
        }
    }
}

/* 10BI UTF8 RENDER:
 * Desde 10BE ya conservamos UTF-8 en la capa HTML10D, pero el visor GUI
 * seguia pintando byte a byte con una fuente 5x7 ASCII. Resultado: las
 * letras acentuadas no aparecian o salian como caracteres raros.
 * Aqui decodificamos UTF-8/Latin-1 en el render y dibujamos los acentos
 * basicos encima de la letra base. No toca red, Lexbor ni el aplanador.
 */
typedef enum {
    TBB10BI_ACC_NONE = 0,
    TBB10BI_ACC_ACUTE,
    TBB10BI_ACC_GRAVE,
    TBB10BI_ACC_CIRC,
    TBB10BI_ACC_TILDE,
    TBB10BI_ACC_UML,
    TBB10BI_ACC_CEDILLA
} tbb10bi_accent_t;

static uint32_t tbb10bi_next_codepoint(const char **ps)
{
    if (ps == NULL || *ps == NULL || **ps == '\0') return 0;
    const unsigned char *s = (const unsigned char *)(*ps);

    if (s[0] < 0x80) {
        *ps = (const char *)(s + 1);
        return s[0];
    }

    /* UTF-8 valido de 2/3/4 bytes. */
    if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(s[0] & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
        *ps = (const char *)(s + 2);
        return cp;
    }
    if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(s[0] & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (uint32_t)(s[2] & 0x3F);
        *ps = (const char *)(s + 3);
        return cp;
    }
    if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        uint32_t cp = ((uint32_t)(s[0] & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) |
                      ((uint32_t)(s[2] & 0x3F) << 6) | (uint32_t)(s[3] & 0x3F);
        *ps = (const char *)(s + 4);
        return cp;
    }

    /* Si no es UTF-8 valido, tratar como Latin-1/CP1252 de un byte. */
    *ps = (const char *)(s + 1);
    return s[0];
}

static bool tbb10bi_set_glyph(char *base, tbb10bi_accent_t *acc, char b, tbb10bi_accent_t a)
{
    if (base != NULL) {
        *base = b;
    }
    if (acc != NULL) {
        *acc = a;
    }
    return true;
}

static bool tbb10bi_set_plain(char *base, tbb10bi_accent_t *acc, char b)
{
    return tbb10bi_set_glyph(base, acc, b, TBB10BI_ACC_NONE);
}

static bool tbb10bi_cp_to_glyph(uint32_t cp, char *base, tbb10bi_accent_t *acc)
{
    if (base != NULL) {
        *base = '?';
    }
    if (acc != NULL) {
        *acc = TBB10BI_ACC_NONE;
    }

    if (cp < 0x80) {
        return tbb10bi_set_plain(base, acc, (char)cp);
    }

    switch (cp) {
        case 0x00E1: return tbb10bi_set_glyph(base, acc, 'a', TBB10BI_ACC_ACUTE);
        case 0x00E9: return tbb10bi_set_glyph(base, acc, 'e', TBB10BI_ACC_ACUTE);
        case 0x00ED: return tbb10bi_set_glyph(base, acc, 'i', TBB10BI_ACC_ACUTE);
        case 0x00F3: return tbb10bi_set_glyph(base, acc, 'o', TBB10BI_ACC_ACUTE);
        case 0x00FA: return tbb10bi_set_glyph(base, acc, 'u', TBB10BI_ACC_ACUTE);
        case 0x00C1: return tbb10bi_set_glyph(base, acc, 'A', TBB10BI_ACC_ACUTE);
        case 0x00C9: return tbb10bi_set_glyph(base, acc, 'E', TBB10BI_ACC_ACUTE);
        case 0x00CD: return tbb10bi_set_glyph(base, acc, 'I', TBB10BI_ACC_ACUTE);
        case 0x00D3: return tbb10bi_set_glyph(base, acc, 'O', TBB10BI_ACC_ACUTE);
        case 0x00DA: return tbb10bi_set_glyph(base, acc, 'U', TBB10BI_ACC_ACUTE);

        case 0x00E0: return tbb10bi_set_glyph(base, acc, 'a', TBB10BI_ACC_GRAVE);
        case 0x00E8: return tbb10bi_set_glyph(base, acc, 'e', TBB10BI_ACC_GRAVE);
        case 0x00EC: return tbb10bi_set_glyph(base, acc, 'i', TBB10BI_ACC_GRAVE);
        case 0x00F2: return tbb10bi_set_glyph(base, acc, 'o', TBB10BI_ACC_GRAVE);
        case 0x00F9: return tbb10bi_set_glyph(base, acc, 'u', TBB10BI_ACC_GRAVE);
        case 0x00C0: return tbb10bi_set_glyph(base, acc, 'A', TBB10BI_ACC_GRAVE);
        case 0x00C8: return tbb10bi_set_glyph(base, acc, 'E', TBB10BI_ACC_GRAVE);
        case 0x00CC: return tbb10bi_set_glyph(base, acc, 'I', TBB10BI_ACC_GRAVE);
        case 0x00D2: return tbb10bi_set_glyph(base, acc, 'O', TBB10BI_ACC_GRAVE);
        case 0x00D9: return tbb10bi_set_glyph(base, acc, 'U', TBB10BI_ACC_GRAVE);

        case 0x00E2: return tbb10bi_set_glyph(base, acc, 'a', TBB10BI_ACC_CIRC);
        case 0x00EA: return tbb10bi_set_glyph(base, acc, 'e', TBB10BI_ACC_CIRC);
        case 0x00EE: return tbb10bi_set_glyph(base, acc, 'i', TBB10BI_ACC_CIRC);
        case 0x00F4: return tbb10bi_set_glyph(base, acc, 'o', TBB10BI_ACC_CIRC);
        case 0x00FB: return tbb10bi_set_glyph(base, acc, 'u', TBB10BI_ACC_CIRC);
        case 0x00C2: return tbb10bi_set_glyph(base, acc, 'A', TBB10BI_ACC_CIRC);
        case 0x00CA: return tbb10bi_set_glyph(base, acc, 'E', TBB10BI_ACC_CIRC);
        case 0x00CE: return tbb10bi_set_glyph(base, acc, 'I', TBB10BI_ACC_CIRC);
        case 0x00D4: return tbb10bi_set_glyph(base, acc, 'O', TBB10BI_ACC_CIRC);
        case 0x00DB: return tbb10bi_set_glyph(base, acc, 'U', TBB10BI_ACC_CIRC);

        case 0x00E4: return tbb10bi_set_glyph(base, acc, 'a', TBB10BI_ACC_UML);
        case 0x00EB: return tbb10bi_set_glyph(base, acc, 'e', TBB10BI_ACC_UML);
        case 0x00EF: return tbb10bi_set_glyph(base, acc, 'i', TBB10BI_ACC_UML);
        case 0x00F6: return tbb10bi_set_glyph(base, acc, 'o', TBB10BI_ACC_UML);
        case 0x00FC: return tbb10bi_set_glyph(base, acc, 'u', TBB10BI_ACC_UML);
        case 0x00C4: return tbb10bi_set_glyph(base, acc, 'A', TBB10BI_ACC_UML);
        case 0x00CB: return tbb10bi_set_glyph(base, acc, 'E', TBB10BI_ACC_UML);
        case 0x00CF: return tbb10bi_set_glyph(base, acc, 'I', TBB10BI_ACC_UML);
        case 0x00D6: return tbb10bi_set_glyph(base, acc, 'O', TBB10BI_ACC_UML);
        case 0x00DC: return tbb10bi_set_glyph(base, acc, 'U', TBB10BI_ACC_UML);

        case 0x00F1: return tbb10bi_set_glyph(base, acc, 'n', TBB10BI_ACC_TILDE);
        case 0x00D1: return tbb10bi_set_glyph(base, acc, 'N', TBB10BI_ACC_TILDE);
        case 0x00E3: return tbb10bi_set_glyph(base, acc, 'a', TBB10BI_ACC_TILDE);
        case 0x00F5: return tbb10bi_set_glyph(base, acc, 'o', TBB10BI_ACC_TILDE);
        case 0x00C3: return tbb10bi_set_glyph(base, acc, 'A', TBB10BI_ACC_TILDE);
        case 0x00D5: return tbb10bi_set_glyph(base, acc, 'O', TBB10BI_ACC_TILDE);

        case 0x00E7: return tbb10bi_set_glyph(base, acc, 'c', TBB10BI_ACC_CEDILLA);
        case 0x00C7: return tbb10bi_set_glyph(base, acc, 'C', TBB10BI_ACC_CEDILLA);

        case 0x00BF: return tbb10bi_set_plain(base, acc, '?');
        case 0x00A1: return tbb10bi_set_plain(base, acc, '!');
        case 0x00AB:
        case 0x00BB:
        case 0x201C:
        case 0x201D:
        case 0x2018:
        case 0x2019:
            return tbb10bi_set_plain(base, acc, '"');
        case 0x2013:
        case 0x2014:
            return tbb10bi_set_plain(base, acc, '-');
        case 0x2026: return tbb10bi_set_plain(base, acc, '.');
        case 0x20AC: return tbb10bi_set_plain(base, acc, 'E');
        case 0x00B0: return tbb10bi_set_plain(base, acc, 'o');
        case 0x00BA: return tbb10bi_set_plain(base, acc, 'o');
        case 0x00AA: return tbb10bi_set_plain(base, acc, 'a');
        case 0x00B7:
        case 0x2022:
            return tbb10bi_set_plain(base, acc, '*');
        case 0x00A9: return tbb10bi_set_plain(base, acc, 'c');
        case 0x00AE: return tbb10bi_set_plain(base, acc, 'R');
        case 0x00A0: return tbb10bi_set_plain(base, acc, ' ');
        default: return tbb10bi_set_plain(base, acc, '?');
    }
}

static void tbb10bi_draw_accent5(int x, int y, tbb10bi_accent_t acc, uint8_t color)
{
    switch (acc) {
        case TBB10BI_ACC_ACUTE:
            tbb10ah_gfx_pixel(x + 3, y - 2, color);
            tbb10ah_gfx_pixel(x + 2, y - 1, color);
            break;
        case TBB10BI_ACC_GRAVE:
            tbb10ah_gfx_pixel(x + 1, y - 2, color);
            tbb10ah_gfx_pixel(x + 2, y - 1, color);
            break;
        case TBB10BI_ACC_CIRC:
            tbb10ah_gfx_pixel(x + 1, y - 1, color);
            tbb10ah_gfx_pixel(x + 2, y - 2, color);
            tbb10ah_gfx_pixel(x + 3, y - 1, color);
            break;
        case TBB10BI_ACC_TILDE:
            tbb10ah_gfx_pixel(x + 1, y - 2, color);
            tbb10ah_gfx_pixel(x + 2, y - 1, color);
            tbb10ah_gfx_pixel(x + 3, y - 2, color);
            tbb10ah_gfx_pixel(x + 4, y - 1, color);
            break;
        case TBB10BI_ACC_UML:
            tbb10ah_gfx_pixel(x + 1, y - 2, color);
            tbb10ah_gfx_pixel(x + 3, y - 2, color);
            break;
        case TBB10BI_ACC_CEDILLA:
            tbb10ah_gfx_pixel(x + 2, y + 7, color);
            tbb10ah_gfx_pixel(x + 1, y + 8, color);
            tbb10ah_gfx_pixel(x + 2, y + 8, color);
            break;
        default:
            break;
    }
}

static void tbb10bi_draw_codepoint5(int x, int y, uint32_t cp, uint8_t color)
{
    char base = '?';
    tbb10bi_accent_t acc = TBB10BI_ACC_NONE;
    tbb10bi_cp_to_glyph(cp, &base, &acc);
    tbb10ah_draw_char5(x, y, base, color);
    tbb10bi_draw_accent5(x, y, acc, color);
}

static int tbb10bi_text_display_len(const char *s)
{
    if (s == NULL) return 0;
    int n = 0;
    const char *p = s;
    while (*p) {
        (void)tbb10bi_next_codepoint(&p);
        n++;
    }
    return n;
}

static void tbb10ah_draw_text5(int x, int y, const char *s, uint8_t color)
{
    int px = x;
    const char *p = s ? s : "";
    while (*p) {
        uint32_t cp = tbb10bi_next_codepoint(&p);
        tbb10bi_draw_codepoint5(px, y, cp, color);
        px += 6;
    }
}

static void __attribute__((unused)) tbb10ah_draw_text5_center(int x, int y, int w, const char *s, uint8_t color)
{
    int len = tbb10bi_text_display_len(s);
    int tw = len * 6;
    tbb10ah_draw_text5(x + (w - tw) / 2, y, s, color);
}



static void tbb10ah_draw_text_clip(int x, int y, const char *s, uint8_t color, int max_chars)
{
    if (s == NULL || max_chars <= 0) return;
    int px = x;
    int drawn = 0;
    const char *p = s;
    while (*p != '\0' && drawn < max_chars) {
        uint32_t cp = tbb10bi_next_codepoint(&p);
        tbb10bi_draw_codepoint5(px, y, cp, color);
        px += 6;
        drawn++;
    }
}

static uint8_t tbb10bp_palette_color(uint8_t style, uint8_t fallback)
{
    switch (style & TBB10BP_STYLE_COLOR_MASK) {
        case TBB10BP_STYLE_COLOR_RED: return TBB10BP_C_RED;
        case TBB10BP_STYLE_COLOR_GREEN: return TBB10AH_C_GOOD;
        case TBB10BP_STYLE_COLOR_BLUE: return TBB10BP_C_BLUE;
        case TBB10BP_STYLE_COLOR_YELLOW: return TBB10AH_C_WARN;
        case TBB10BP_STYLE_COLOR_ORANGE: return TBB10BP_C_ORANGE;
        case TBB10BP_STYLE_COLOR_GRAY: return TBB10BP_C_GRAY;
        case TBB10BP_STYLE_COLOR_WHITE: return TBB10AH_C_WHITE;
        case TBB10BP_STYLE_COLOR_CYAN: return TBB10BP_C_CYAN;
        default: return fallback;
    }
}

static void tbb10bp_draw_text_clip_style(int x, int y, const char *s, uint8_t fallback,
                                         int max_chars, uint8_t style)
{
    uint8_t c = tbb10bp_palette_color(style, fallback);
    tbb10ah_draw_text_clip(x, y, s, c, max_chars);
    if (style & TBB10BP_STYLE_BOLD) {
        /* Negrita 5x7 muy barata: segunda pasada desplazada un pixel. */
        tbb10ah_draw_text_clip(x + 1, y, s, c, max_chars);
    }
}


static void tbb10ah_present(void)
{
    uint8_t *front = rgb_display_get_framebuffer();
    if (front == NULL || s_tbb10ah_back == NULL) return;
    rgb_display_wait_vsync();
    memcpy(front, s_tbb10ah_back, TBB10AH_GUI_W * TBB10AH_GUI_H);
}

/* ========================= 10BA HOVER / INPUT HELPERS =========================
 * El raton USB trabaja en coordenadas logicas 400x240. Guardamos aqui el
 * ultimo punto del cursor para poder resaltar SOLO el boton/enlace apuntado.
 */
#ifndef TBB10AR_TOUCH_LINK_BASE
#define TBB10AR_TOUCH_LINK_BASE 1000
#endif

static int s_tbb10ba_hover_x = -1;
static int s_tbb10ba_hover_y = -1;
static int s_tbb10ba_hover_key = -1;
static uint8_t s_tbb10ba_prev_mouse_buttons = 0;

/* 10BH: estado ligero de usabilidad local. No modifica red ni documento. */
static char s_tbb10bh_find_query[64];
static int  s_tbb10bh_find_line = -1;

/* 10BJ: panel LINK con scroll propio. No toca red ni parser. */
static int  s_tbb10bj_links_top = 0;

/* 10BK: en about:bookmarks, al apuntar un favorito queda seleccionado
 * y el boton +FAV se convierte en -FAV para borrarlo, como el nav viejo.
 */
static char s_tbb10bk_fav_delete_path[TBB10AH_PATH_CAP];
static bool s_tbb10bk_header_minus_fav = false;

/* 10BK_FIX3:
 * En about:bookmarks, un click sobre favorito solo lo selecciona
 * para poder borrarlo con -FAV. Un segundo click sobre el mismo favorito
 * lo abre. Tambien conservamos el origen del ultimo evento para no
 * afectar apertura por teclado/numeros.
 */
static int s_tbb10bk_selected_fav_idx = -1;
static int64_t s_tbb10bk_last_fav_click_us = 0;
static bool s_tbb10bk_last_input_was_pointer = false;

static int tbb10ai_parse_link_number_from_line(const char *line);

static bool tbb10ba_is_hover_rect(int x, int y, int w, int h)
{
    return (s_tbb10ba_hover_x >= x && s_tbb10ba_hover_x < x + w &&
            s_tbb10ba_hover_y >= y && s_tbb10ba_hover_y < y + h);
}

static void tbb10ai_draw_touch_button(int x, int y, int w, const char *label);

static void tbb10an_draw_hourglass_icon(int x, int y)
{
    /* Icono 16x13, sin depender de caracteres especiales del font. */
    tbb10ah_gfx_rectfill(x, y, 17, 13, TBB10AH_C_PANEL2);
    tbb10ah_gfx_rect(x, y, 17, 13, TBB10AH_C_WARN);

    /* tapas superior/inferior */
    tbb10ah_gfx_hline(x + 4, y + 2, 9, TBB10AH_C_WHITE);
    tbb10ah_gfx_hline(x + 4, y + 10, 9, TBB10AH_C_WHITE);

    /* cuerpo cruzado */
    for (int i = 0; i < 5; i++) {
        tbb10ah_gfx_pixel(x + 4 + i, y + 3 + i, TBB10AH_C_WARN);
        tbb10ah_gfx_pixel(x + 12 - i, y + 3 + i, TBB10AH_C_WARN);
    }

    /* arena */
    tbb10ah_gfx_pixel(x + 8, y + 6, TBB10AH_C_WHITE);
    tbb10ah_gfx_pixel(x + 7, y + 9, TBB10AH_C_WARN);
    tbb10ah_gfx_pixel(x + 8, y + 9, TBB10AH_C_WARN);
    tbb10ah_gfx_pixel(x + 9, y + 9, TBB10AH_C_WARN);
}

static void tbb10an_draw_header_busy_badge(void)
{
    if (!s_tbb10an_header_busy) return;

    /* Zona derecha de la cabecera superior: visible aunque la operacion
     * bloqueante deje congelado el bucle hasta que llegue la pagina. */
    tbb10ah_gfx_rectfill(334, 4, 60, 15, TBB10AH_C_PANEL2);
    tbb10ah_gfx_rect(334, 4, 60, 15, TBB10AH_C_WARN);
    tbb10ah_draw_text_clip(339, 8, "WAIT", TBB10AH_C_WARN, 4);
    tbb10an_draw_hourglass_icon(374, 5);
}

static void tbb10al_draw_header_navbar(const char *title, const char *url, bool editing)
{
    /* 10AV:
     * Cabecera separada: los botones y la barra URL son dos zonas visuales
     * distintas. En modo edicion solo se marca la URL, no todo el bloque.
     */
    (void) title;

    /* Fondo general de la zona navegador, sin resalte de edicion. */
    tbb10ah_gfx_rectfill(4, 24, 392, 40, TBB10AH_C_PANEL);

    /* Fila de botones independiente. */
    tbb10ah_gfx_rectfill(6, 26, 388, 18, TBB10AH_C_TOP);
    tbb10ah_gfx_rect(6, 26, 388, 18, TBB10AH_C_BORDER);

    tbb10ai_draw_touch_button(8,   28, 42, "EXIT");
    tbb10ai_draw_touch_button(54,  28, 26, "<");
    tbb10ai_draw_touch_button(84,  28, 26, ">");
    tbb10ai_draw_touch_button(114, 28, 48, "HOME");
    tbb10ai_draw_touch_button(166, 28, 38, "RLD");
    tbb10ai_draw_touch_button(208, 28, 44, "HIST");
    tbb10ai_draw_touch_button(256, 28, 38, "FAV");
    tbb10ai_draw_touch_button(298, 28, 46, s_tbb10bk_header_minus_fav ? "-FAV" : "+FAV");
    tbb10ai_draw_touch_button(348, 28, 42, "LINK");

    /* Barra URL separada. Solo este rectangulo cambia de color al editar. */
    bool url_hover = tbb10ba_is_hover_rect(6, 47, 388, 14);
    uint8_t url_bg = editing ? TBB10AH_C_TOP : TBB10AH_C_BG;
    uint8_t url_border = editing ? TBB10AH_C_WARN : (url_hover ? TBB10AH_C_LINK : TBB10AH_C_BORDER);
    uint8_t label_col = editing ? TBB10AH_C_WARN : (url_hover ? TBB10AH_C_LINK : TBB10AH_C_MUTED);
    uint8_t text_col = editing ? TBB10AH_C_LINK : TBB10AH_C_TEXT;

    tbb10ah_gfx_rectfill(6, 47, 388, 14, url_bg);
    tbb10ah_gfx_rect(6, 47, 388, 14, url_border);
    tbb10ah_draw_text_clip(11, 51, editing ? "URL>" : "URL:", label_col, 5);
    tbb10ah_draw_text_clip(43, 51, url ? url : "", text_col, 57);

    tbb10an_draw_header_busy_badge();
}

static void tbb10al_drain_enter_keys(void)
{
    /* 10AL_FIX3:
     * Algunos terminales envian CR+LF al pulsar ENTER. La barra URL consume
     * uno de ellos para aceptar la direccion y puede quedar el otro en cola;
     * si el visor lo lee despues, lo interpreta como PageDown. Drenamos la
     * cola al salir de la edicion para que ENTER solo signifique "abrir URL".
     */
    vterm_input_flush(vterm_get_active());
    for (int guard = 0; guard < 16; guard++) {
        int ch = vterm_getchar(vterm_get_active(), 0);
        if (ch < 0) break;
        if (ch != '\r' && ch != '\n') break;
    }
}

/* 10BJ_FIX1: la barra URL debe aceptar UTF-8 real (ñ, á, etc.).
 * vterm_getchar() entrega bytes, así que conservamos bytes >= 0x80 y
 * al borrar eliminamos el carácter UTF-8 completo, no solo el último byte.
 */
static void tbb10bj_utf8_backspace(char *input, int *len)
{
    if (input == NULL || len == NULL || *len <= 0) return;

    int n = *len - 1;
    while (n > 0 && (((unsigned char)input[n] & 0xC0) == 0x80)) {
        n--;
    }
    input[n] = '\0';
    *len = n;
}

static int tbb10bk_utf8_pending_continuations(const char *s, int len)
{
    if (s == NULL || len <= 0) return 0;

    int i = len - 1;
    int cont = 0;
    while (i >= 0 && (((unsigned char)s[i] & 0xC0) == 0x80)) {
        cont++;
        i--;
    }
    if (i < 0) return 0;

    unsigned char lead = (unsigned char)s[i];
    int need = 0;
    if ((lead & 0xE0) == 0xC0) need = 1;
    else if ((lead & 0xF0) == 0xE0) need = 2;
    else if ((lead & 0xF8) == 0xF0) need = 3;
    else return 0;

    return (cont < need) ? (need - cont) : 0;
}

static bool tbb10bk_cp850_to_unicode(unsigned char c, uint32_t *cp)
{
    if (cp == NULL) return false;

    /* Teclados/terminales Windows en consola espanola pueden entregar CP850
     * en vez de UTF-8. Convertimos los caracteres utiles a Unicode para que
     * la barra URL pueda escribir n/acentos correctamente.
     */
    switch (c) {
        case 0xA0: *cp = 0x00E1; return true; /* á */
        case 0x82: *cp = 0x00E9; return true; /* é */
        case 0xA1: *cp = 0x00ED; return true; /* í */
        case 0xA2: *cp = 0x00F3; return true; /* ó */
        case 0xA3: *cp = 0x00FA; return true; /* ú */
        case 0x81: *cp = 0x00FC; return true; /* ü */
        case 0xA4: *cp = 0x00F1; return true; /* ñ */
        case 0xA5: *cp = 0x00D1; return true; /* Ñ */
        case 0xAD: *cp = 0x00A1; return true; /* ¡ */
        case 0xA8: *cp = 0x00BF; return true; /* ¿ */
        case 0x87: *cp = 0x00E7; return true; /* ç */
        case 0x80: *cp = 0x00C7; return true; /* Ç */
        default: return false;
    }
}


/* 10BL_FIX1: atajos ASCII para escribir caracteres españoles cuando el
 * teclado/terminal no entrega directamente ñ o acentos. Es una capa de
 * seguridad: si llega UTF-8/CP850 real, se respeta; si no, se puede escribir:
 *   ~n -> ñ, ~N -> Ñ, 'a -> á, 'e -> é, 'i -> í, 'o -> ó, 'u -> ú,
 *   "u -> ü, ?! -> ¿, !! -> ¡
 */
static bool tbb10bl_try_compose_ascii_spanish(char *input, int *len, int cap, int ch)
{
    if (input == NULL || len == NULL || *len <= 0 || cap <= 1) return false;

    char prefix = input[*len - 1];
    uint32_t cp = 0;

    if (prefix == '~' && (ch == 'n' || ch == 'N')) {
        cp = (ch == 'n') ? 0x00F1 : 0x00D1;
    }
    else if (prefix == '\'' || prefix == '`') {
        switch (ch) {
            case 'a': cp = (prefix == '\'') ? 0x00E1 : 0x00E0; break;
            case 'e': cp = (prefix == '\'') ? 0x00E9 : 0x00E8; break;
            case 'i': cp = (prefix == '\'') ? 0x00ED : 0x00EC; break;
            case 'o': cp = (prefix == '\'') ? 0x00F3 : 0x00F2; break;
            case 'u': cp = (prefix == '\'') ? 0x00FA : 0x00F9; break;
            case 'A': cp = (prefix == '\'') ? 0x00C1 : 0x00C0; break;
            case 'E': cp = (prefix == '\'') ? 0x00C9 : 0x00C8; break;
            case 'I': cp = (prefix == '\'') ? 0x00CD : 0x00CC; break;
            case 'O': cp = (prefix == '\'') ? 0x00D3 : 0x00D2; break;
            case 'U': cp = (prefix == '\'') ? 0x00DA : 0x00D9; break;
            default: break;
        }
    }
    else if (prefix == '"' && (ch == 'u' || ch == 'U')) {
        cp = (ch == 'u') ? 0x00FC : 0x00DC;
    }
    else if (prefix == '?' && ch == '!') {
        cp = 0x00BF;
    }
    else if (prefix == '!' && ch == '!') {
        cp = 0x00A1;
    }

    if (cp == 0) return false;

    char tmp[8];
    tmp[0] = '\0';
    tbb10be_append_utf8_cp(tmp, sizeof(tmp), cp);
    size_t add = strlen(tmp);
    if (add == 0 || (*len - 1) + (int)add >= cap) return false;

    *len = *len - 1;
    memcpy(input + *len, tmp, add + 1);
    *len += (int)add;
    return true;
}

static bool tbb10bk_append_input_char_utf8(char *input, int *len, int cap, int ch)
{
    if (input == NULL || len == NULL || cap <= 1) return false;
    if (*len < 0) *len = 0;
    if (*len >= cap - 1) return false;

    unsigned char uc = (unsigned char)ch;

    if (ch >= 32 && ch < 0x80) {
        if (tbb10bl_try_compose_ascii_spanish(input, len, cap, ch)) {
            return true;
        }
        input[(*len)++] = (char)uc;
        input[*len] = '\0';
        return true;
    }

    if (ch < 0x80 || ch > 0xFF) return false;

    /* Si estamos en mitad de una secuencia UTF-8 real, aceptar el byte crudo. */
    if (tbb10bk_utf8_pending_continuations(input, *len) > 0 &&
        (uc & 0xC0) == 0x80) {
        input[(*len)++] = (char)uc;
        input[*len] = '\0';
        return true;
    }

    /* Inicio de UTF-8 real: conservarlo y esperar continuaciones. */
    if (uc >= 0xC2 && uc <= 0xF4) {
        input[(*len)++] = (char)uc;
        input[*len] = '\0';
        return true;
    }

    /* Byte suelto de consola CP850/Latin-1: convertir a UTF-8 valido. */
    uint32_t cp = 0;
    if (!tbb10bk_cp850_to_unicode(uc, &cp)) {
        cp = tbb10bi_cp1252_or_latin1(uc);
    }

    char tmp[8];
    tmp[0] = '\0';
    tbb10be_append_utf8_cp(tmp, sizeof(tmp), cp);
    size_t add = strlen(tmp);
    if (add == 0 || *len + (int)add >= cap) return false;
    memcpy(input + *len, tmp, add + 1);
    *len += (int)add;
    return true;
}

/* 10BL: vterm_getchar() a veces entrega bytes altos como int negativo
 * sign-extended (por ejemplo 0xF1 -> -15). Antes los tratabamos como
 * "sin tecla" y por eso no entraban n/acentos en la barra URL.
 */
static bool tbb10bl_vterm_key_to_byte(int ch, int *out_ch)
{
    if (out_ch == NULL) return false;
    if (ch == -1) return false;
    if (ch >= 0 && ch <= 255) {
        *out_ch = ch;
        return true;
    }
    if (ch < 0 && ch >= -255) {
        unsigned char uc = (unsigned char)ch;
        if (uc >= 0x80) {
            *out_ch = (int)uc;
            return true;
        }
    }
    return false;
}

static bool tbb10al_gui_read_url(char *out, size_t out_cap, const char *current)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';

    char input[TBB10AL_URL_CAP];
    input[0] = '\0';
    int len = 0;

    const char *hint = current ? current : "";
    char caption[96];
    tbb10ah_safe_copy(caption, sizeof(caption), "Barra navegacion / buscador 10BL-FIX1");

    while (true) {
        char shown[TBB10AL_URL_CAP + 8];
        if (input[0] != '\0') {
            tbb10ah_safe_copy(shown, sizeof(shown), input);
        }
        else {
            tbb10ah_safe_copy(shown, sizeof(shown), hint);
        }

        tbb10al_draw_header_navbar(caption, shown, true);

        /* Pequeña ayuda no modal en la línea de estado, sin tapar la página. */
        tbb10ah_gfx_rectfill(4, 214, 392, 20, TBB10AH_C_TOP);
        tbb10ah_gfx_rect(4, 214, 392, 20, TBB10AH_C_BORDER);
        tbb10ah_draw_text_clip(10, 220, "URL/busqueda. DDG por defecto. ~n=ñ  'a=á  ESC cancela.", TBB10AH_C_TEXT, 62);
        tbb10ah_present();

        int ch = vterm_getchar(vterm_get_active(), 200);
        int key_byte = 0;
        if (!tbb10bl_vterm_key_to_byte(ch, &key_byte)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        ch = key_byte;
        if (ch == 27) return false;
        if (ch == '\r' || ch == '\n') break;
        if (ch == 8 || ch == 127) {
            tbb10bj_utf8_backspace(input, &len);
            continue;
        }
        if (ch >= 32) {
            (void)tbb10bk_append_input_char_utf8(input, &len, (int)sizeof(input), ch);
        }
    }

    tbb10ah_chomp(input);
    if (input[0] == '\0') { tbb10al_drain_enter_keys(); return false; }
    tbb10al_drain_enter_keys();
    char normalized[TBB10AL_URL_CAP];
    if (tbb10ap_normalize_user_url_or_search(input, normalized, sizeof(normalized))) {
        tbb10ah_safe_copy(out, out_cap, normalized);
        return true;
    }
    return false;
}

static int tbb10ah_gui_begin(void)
{
    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("[TBBROWSER10AH][ERR] No pude entrar en SM_400X240\n");
        return 1;
    }

    if (s_tbb10ah_back == NULL) {
        s_tbb10ah_back = (uint8_t *) heap_caps_malloc(TBB10AH_GUI_W * TBB10AH_GUI_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_tbb10ah_back == NULL) {
            s_tbb10ah_back = (uint8_t *) heap_caps_malloc(TBB10AH_GUI_W * TBB10AH_GUI_H, MALLOC_CAP_8BIT);
        }
    }

    if (s_tbb10ah_back == NULL) {
        rgb_display_set_mode(SM_TEXT);
        printf("[TBBROWSER10AH][ERR] Sin memoria para backbuffer GUI\n");
        return 1;
    }

    tbb10ah_gui_palette();
    tbb10ah_gfx_clear(TBB10AH_C_BG);
    tbb10ah_present();

    vterm_input_flush(vterm_get_active());
    for (int guard = 0; guard < 64; guard++) {
        int drain = vterm_getchar(vterm_get_active(), 0);
        if (drain < 0) break;
    }
    return 0;
}

static void tbb10ah_gui_end(void)
{
    vterm_input_flush(vterm_get_active());

    /* 10AH_FIX2:
     * Durante navegacion interna NO salimos de SM_400X240.
     * Solo al cerrar definitivamente el navegador volvemos a texto.
     */
    if (s_tbb10ah_keep_gui_mode) {
        return;
    }

    /* Dar margen al callback RGB antes de liberar nuestro backbuffer local.
     * El framebuffer global de rgb_display se libera dentro de set_mode(SM_TEXT).
     */
    rgb_display_wait_vsync();
    vTaskDelay(pdMS_TO_TICKS(20));
    rgb_display_set_mode(SM_TEXT);
    vTaskDelay(pdMS_TO_TICKS(20));

    if (s_tbb10ah_back != NULL) {
        heap_caps_free(s_tbb10ah_back);
        s_tbb10ah_back = NULL;
    }
}

static void tbb10ai_draw_touch_button(int x, int y, int w, const char *label)
{
    if (w < 12) return;
    bool hov = tbb10ba_is_hover_rect(x, y - 2, w, 17);
    uint8_t bg = hov ? TBB10AH_C_LINK : TBB10AH_C_PANEL2;
    uint8_t br = hov ? TBB10AH_C_WHITE : TBB10AH_C_BORDER;
    uint8_t tx = hov ? TBB10AH_C_BLACK : TBB10AH_C_TEXT;
    tbb10ah_gfx_rectfill(x, y, w, 13, bg);
    tbb10ah_gfx_rect(x, y, w, 13, br);
    tbb10ah_draw_text_clip(x + 4, y + 3, label, tx, (w - 8) / 6);
}


static bool tbb10bb_is_form_search_href(const char *href)
{
    return (href != NULL && strcasecmp(href, "form://search") == 0);
}

/* 10BQ FIX2: lector RAW dedicado para el valor del campo de formulario.
 * No pasa por la barra URL ni por su normalizador; al pulsar ENTER entrega
 * exactamente el texto escrito y despues construye GET/POST con el name del
 * input detectado. Asi evitamos que el mensaje se interprete como URL/busqueda.
 */
static bool tbb10bq_gui_read_form_value(char *out, size_t out_cap, const char *label)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    int len = 0;

    while (true) {
        tbb10al_draw_header_navbar("Campo de formulario 10BQ-FIX2", out, true);
        tbb10ah_gfx_rectfill(4, 214, 392, 20, TBB10AH_C_TOP);
        tbb10ah_gfx_rect(4, 214, 392, 20, TBB10AH_C_BORDER);
        tbb10ah_draw_text_clip(10, 220,
            (label && label[0]) ? label : "Escriba SOLO el mensaje y pulse ENTER. ESC cancela.",
            TBB10AH_C_TEXT, 62);
        tbb10ah_present();

        int ch = vterm_getchar(vterm_get_active(), 200);
        int key_byte = 0;
        if (!tbb10bl_vterm_key_to_byte(ch, &key_byte)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        ch = key_byte;
        if (ch == 27) return false;
        if (ch == '\r' || ch == '\n') break;
        if (ch == 8 || ch == 127) {
            tbb10bj_utf8_backspace(out, &len);
            continue;
        }
        if (ch >= 32) {
            (void)tbb10bk_append_input_char_utf8(out, &len, (int)out_cap, ch);
        }
    }

    tbb10ah_chomp(out);
    tbb10al_drain_enter_keys();
    return out[0] != '\0';
}

static bool tbb10bb_prompt_form_search(const char *label, char *out_open_path, size_t out_cap)
{
    if (out_open_path == NULL || out_cap == 0) return false;
    out_open_path[0] = '\0';

    /* 10BQ FIX4: about:posttest es HTML interno y no pasa por el escaner
     * de formularios de paginas web. El enlace form://search ya se dibujaba,
     * pero el estado global seguia a false y por eso click/numero no hacian nada.
     * Al activarlo, cargamos explicitamente el formulario local controlado. */
    if (label != NULL && strcmp(label, "about:posttest") == 0) {
        if (!tbb10bo_post_ensure_buffers()) return false;
        tbb10ah_safe_copy(s_tbb10bb_form_action, sizeof(s_tbb10bb_form_action), TBB10BQ_LOCAL_POST_URL);
        tbb10ah_safe_copy(s_tbb10bb_form_input, sizeof(s_tbb10bb_form_input), "mensaje");
        tbb10ah_safe_copy(s_tbb10bp_form_hidden, TBB10BP_POST_BODY_CAP, "token=10BQ_OK");
        s_tbb10bb_form_available = true;
        s_tbb10bp_form_is_post = true;
        printf("[TBBROWSER10BQ_FIX4][FORM] autoprueba local activada action=%s\n",
               s_tbb10bb_form_action);
    }

    if (!s_tbb10bb_form_available) return false;

    char raw_value[TBB10AL_URL_CAP];
    const char *help = s_tbb10bp_form_is_post
        ? "POST: escriba SOLO el mensaje y pulse ENTER."
        : "GET: escriba SOLO el texto y pulse ENTER.";
    (void)label;
    if (!tbb10bq_gui_read_form_value(raw_value, sizeof(raw_value), help)) return false;

    if (!tbb10bb_build_form_query_url(raw_value, out_open_path, out_cap)) return false;
    printf("[TBBROWSER10BQ_FIX2][FORM] valor capturado bytes=%u campo=%s\n",
           (unsigned)strlen(raw_value), s_tbb10bb_form_input);
    return out_open_path[0] != '\0';
}

static void tbb10ah_draw_overlay_help(void)
{
    tbb10ah_gfx_rectfill(34, 54, 332, 112, TBB10AH_C_SHADOW);
    tbb10ah_gfx_rectfill(30, 50, 332, 112, TBB10AH_C_PANEL);
    tbb10ah_gfx_rect(30, 50, 332, 112, TBB10AH_C_BORDER);
    tbb10ah_draw_text_clip(40, 60, "Ayuda 10BK GUI + NET", TBB10AH_C_WARN, 40);
    tbb10ah_draw_text_clip(40, 76, "n/ENTER pagina abajo   p pagina arriba", TBB10AH_C_TEXT, 48);
    tbb10ah_draw_text_clip(40, 88, "j/k linea   g/G inicio/final", TBB10AH_C_TEXT, 48);
    tbb10ah_draw_text_clip(40, 100, "1-99 enlace   / busca   n/N repite", TBB10AH_C_TEXT, 48);
    tbb10ah_draw_text_clip(40, 112, "e lector   +FAV guarda   -FAV borra", TBB10AH_C_TEXT, 48);
    tbb10ah_draw_text_clip(40, 124, "u/URL direccion   o/FILE archivos   H inicio", TBB10AH_C_TEXT, 48);
    tbb10ah_draw_text_clip(40, 136, "Touch: EXIT < > HOME RLD HIST FAV +/-FAV LINK", TBB10AH_C_GOOD, 48);
    tbb10ah_draw_text_clip(40, 148, "Contenido: arriba/sube, abajo/baja, [n] abre", TBB10AH_C_MUTED, 48);
}


static const char *tbb10ak_skip_heading_prefix(const char *line)
{
    if (line == NULL) return "";
    if (line[0] == '#' && line[1] == '#' && line[2] == ' ') return line + 3;
    if (line[0] == '#' && line[1] == ' ') return line + 2;
    if (line[0] == '#') return line + 1;
    return line;
}

static const char *tbb10ak_skip_bullet_prefix(const char *line)
{
    if (line == NULL) return "";
    if (line[0] == '-' && line[1] == ' ') return line + 2;
    if (line[0] == '-') return line + 1;
    return line;
}

static bool tbb10ak_line_is_h1(const char *line)
{
    return (line != NULL && line[0] == '#' && line[1] == ' ');
}

static bool tbb10ak_line_is_h2(const char *line)
{
    return (line != NULL && line[0] == '#' && line[1] == '#' && line[2] == ' ');
}

static bool tbb10ak_line_is_link(const char *line)
{
    /* 10BK_FIX2: el aplanador viejo puede generar enlaces como
     * "[1] titulo", "- [1] titulo" o "> [1] titulo". Para que FAV
     * y las listas internas se resalten igual que las paginas normales,
     * consideramos enlace cualquier linea con patron [numero].
     */
    return (tbb10ai_parse_link_number_from_line(line) >= 1);
}

static bool tbb10ak_line_is_bullet(const char *line)
{
    return (line != NULL && line[0] == '-');
}

static void tbb10ak_draw_scrollbar(int x, int y, int h, int top, int total_lines)
{
    if (h <= 4) return;
    tbb10ah_gfx_rect(x, y, 4, h, TBB10AH_C_BORDER);
    if (total_lines <= TBB10AH_PAGE_LINES) {
        tbb10ah_gfx_rectfill(x + 1, y + 1, 2, h - 2, TBB10AH_C_GOOD);
        return;
    }
    int max_top = total_lines - TBB10AH_PAGE_LINES;
    if (max_top < 1) max_top = 1;
    int thumb_h = (h - 2) * TBB10AH_PAGE_LINES / total_lines;
    if (thumb_h < 10) thumb_h = 10;
    if (thumb_h > h - 2) thumb_h = h - 2;
    int thumb_y = y + 1 + ((h - 2 - thumb_h) * top) / max_top;
    tbb10ah_gfx_rectfill(x + 1, thumb_y, 2, thumb_h, TBB10AH_C_LINK);
}



static const char *tbb10bh_button_hint_from_key(int key)
{
    switch (key) {
        case 'q': return "EXIT: salir del navegador";
        case 'b': return "< : historial atras";
        case 'F': return "> : historial adelante";
        case 'H': return "HOME: pagina inicial";
        case 'r': return "RLD: recargar pagina";
        case 's': return "HIST: historial";
        case 'v': return "FAV: favoritos";
        case 'm': return "+FAV: guardar favorito";
        case 'l': return "LINK: lista de enlaces";
        case 'U': return "URL: escribir direccion o busqueda";
        default: return NULL;
    }
}

static void tbb10bh_hover_status(tbb10ah_doc_t *doc, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return;
    out[0] = '\0';

    if (s_tbb10ba_hover_key >= TBB10AR_TOUCH_LINK_BASE && doc != NULL) {
        int idx = s_tbb10ba_hover_key - TBB10AR_TOUCH_LINK_BASE;
        if (idx >= 0 && idx < doc->link_count) {
            const char *href = tbb10ah_link_ptr(doc, idx);
            char tmp[24];
            snprintf(tmp, sizeof(tmp), "LINK %d: ", idx + 1);
            tbb10ah_safe_copy(out, out_cap, tmp);
            tbb10ah_safe_append(out, out_cap, href ? href : "");
            return;
        }
    }

    if (s_tbb10ba_hover_key == 'm' && s_tbb10bk_header_minus_fav) {
        tbb10ah_safe_copy(out, out_cap, "-FAV: eliminar favorito seleccionado");
        return;
    }

    const char *hint = tbb10bh_button_hint_from_key(s_tbb10ba_hover_key);
    if (hint != NULL) {
        tbb10ah_safe_copy(out, out_cap, hint);
    }
}

static void tbb10bk_update_fav_delete_selection(tbb10ah_doc_t *doc, const char *label)
{
    s_tbb10bk_header_minus_fav = false;

    if (!tbb10bk_label_is_bookmarks(label)) {
        s_tbb10bk_fav_delete_path[0] = '\0';
        s_tbb10bk_selected_fav_idx = -1;
        s_tbb10bk_last_fav_click_us = 0;
        return;
    }

    /* Si hay favorito seleccionado por click, mantenerlo aunque el puntero
     * se mueva fuera. Esto permite pulsar -FAV despues de seleccionarlo.
     */
    if (s_tbb10bk_selected_fav_idx >= 0 && doc != NULL &&
        s_tbb10bk_selected_fav_idx < doc->link_count) {
        char *href = tbb10ah_link_ptr(doc, s_tbb10bk_selected_fav_idx);
        if (href != NULL && tbb10bk_href_is_deletable_fav(href)) {
            tbb10ah_safe_copy(s_tbb10bk_fav_delete_path, sizeof(s_tbb10bk_fav_delete_path), href);
            s_tbb10bk_header_minus_fav = true;
            return;
        }
    }

    if (s_tbb10ba_hover_key >= TBB10AR_TOUCH_LINK_BASE && doc != NULL) {
        int idx = s_tbb10ba_hover_key - TBB10AR_TOUCH_LINK_BASE;
        if (idx >= 0 && idx < doc->link_count) {
            char *href = tbb10ah_link_ptr(doc, idx);
            if (href != NULL && tbb10bk_href_is_deletable_fav(href)) {
                tbb10ah_safe_copy(s_tbb10bk_fav_delete_path, sizeof(s_tbb10bk_fav_delete_path), href);
                s_tbb10bk_header_minus_fav = true;
                return;
            }
        }
    }

    s_tbb10bk_fav_delete_path[0] = '\0';
}

static int tbb10bh_find_match(tbb10ah_doc_t *doc, const char *needle, int start, int dir)
{
    if (doc == NULL || needle == NULL || needle[0] == '\0' || doc->count <= 0) return -1;
    if (dir == 0) dir = 1;
    if (start < 0) start = 0;
    if (start >= doc->count) start = doc->count - 1;

    int idx = start;
    for (int scanned = 0; scanned < doc->count; scanned++) {
        const char *line = tbb10ah_line_ptr(doc, idx);
        if (line != NULL && tbb10au_strcasestr(line, needle) != NULL) {
            return idx;
        }
        idx += (dir > 0) ? 1 : -1;
        if (idx >= doc->count) idx = 0;
        if (idx < 0) idx = doc->count - 1;
        if ((scanned & 0x1Fu) == 0) vTaskDelay(1);
    }
    return -1;
}

static bool tbb10bh_gui_read_find(char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';

    char input[64];
    input[0] = '\0';
    int len = 0;

    while (true) {
        tbb10ah_gfx_rectfill(24, 88, 352, 48, TBB10AH_C_SHADOW);
        tbb10ah_gfx_rectfill(20, 84, 352, 48, TBB10AH_C_PANEL);
        tbb10ah_gfx_rect(20, 84, 352, 48, TBB10AH_C_WARN);
        tbb10ah_draw_text_clip(30, 94, "BUSCAR EN PAGINA", TBB10AH_C_WARN, 30);
        tbb10ah_draw_text_clip(30, 110, input[0] ? input : "texto...", input[0] ? TBB10AH_C_TEXT : TBB10AH_C_MUTED, 52);
        tbb10ah_draw_text_clip(30, 122, "ENTER busca, ESC cancela", TBB10AH_C_MUTED, 48);
        tbb10ah_present();

        int ch = vterm_getchar(vterm_get_active(), 200);
        if (ch < 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        if (ch == 27) return false;
        if (ch == '\r' || ch == '\n') break;
        if (ch == 8 || ch == 127) {
            if (len > 0) input[--len] = '\0';
            continue;
        }
        if (ch >= 32 && ch <= 126 && len + 1 < (int) sizeof(input)) {
            input[len++] = (char) ch;
            input[len] = '\0';
        }
    }

    tbb10ah_chomp(input);
    if (input[0] == '\0') return false;
    tbb10ah_safe_copy(out, out_cap, input);
    tbb10al_drain_enter_keys();
    return true;
}

static void tbb10ah_draw_overlay_links(tbb10ah_doc_t *doc)
{
    /* 10BJ:
     * Panel LINK mas comodo: scroll propio, hover/click con raton,
     * numeros reales de dos cifras y URL visible en barra de estado.
     */
    const int panel_x = 20;
    const int panel_y = 40;
    const int panel_w = 352;
    const int panel_h = 148;
    const int first_y = 68;
    const int row_h = 12;
    const int visible_rows = 9;

    int total = (doc != NULL) ? doc->link_count : 0;
    int max_top = total - visible_rows;
    if (max_top < 0) max_top = 0;
    if (s_tbb10bj_links_top < 0) s_tbb10bj_links_top = 0;
    if (s_tbb10bj_links_top > max_top) s_tbb10bj_links_top = max_top;

    tbb10ah_gfx_rectfill(panel_x + 4, panel_y + 4, panel_w, panel_h, TBB10AH_C_SHADOW);
    tbb10ah_gfx_rectfill(panel_x, panel_y, panel_w, panel_h, TBB10AH_C_PANEL);
    tbb10ah_gfx_rect(panel_x, panel_y, panel_w, panel_h, TBB10AH_C_BORDER);

    char title[64];
    if (total > 0) {
        int a = s_tbb10bj_links_top + 1;
        int b = s_tbb10bj_links_top + visible_rows;
        if (b > total) b = total;
        snprintf(title, sizeof(title), "LINK %d-%d/%d", a, b, total);
    }
    else {
        tbb10ah_safe_copy(title, sizeof(title), "LINK: sin enlaces");
    }
    tbb10ah_draw_text_clip(30, 50, title, TBB10AH_C_WARN, 42);

    if (doc != NULL && total > 0) {
        for (int row = 0; row < visible_rows; row++) {
            int idx = s_tbb10bj_links_top + row;
            if (idx >= total) break;

            const char *href = tbb10ah_link_ptr(doc, idx);
            if (href == NULL) href = "";

            char line[96];
            snprintf(line, sizeof(line), "[%02d] ", idx + 1);
            tbb10ah_safe_append(line, sizeof(line), href);

            int yy = first_y + row * row_h;
            bool hov = (s_tbb10ba_hover_key == TBB10AR_TOUCH_LINK_BASE + idx) ||
                       tbb10ba_is_hover_rect(24, yy - 2, 344, 11);
            if (hov) {
                tbb10ah_gfx_rectfill(26, yy - 2, 340, 11, TBB10AH_C_LINK);
                tbb10ah_gfx_rect(26, yy - 2, 340, 11, TBB10AH_C_WHITE);
                tbb10ah_draw_text_clip(30, yy, line, TBB10AH_C_BLACK, 54);
            }
            else {
                tbb10ah_draw_text_clip(30, yy, line, TBB10AH_C_LINK, 54);
            }
        }

        if (total > visible_rows) {
            /* Scrollbar local del panel LINK. */
            int track_x = panel_x + panel_w - 8;
            int track_y = first_y - 2;
            int track_h = visible_rows * row_h - 2;
            tbb10ah_gfx_rect(track_x, track_y, 4, track_h, TBB10AH_C_BORDER);
            int thumb_h = (visible_rows * track_h) / total;
            if (thumb_h < 10) thumb_h = 10;
            if (thumb_h > track_h - 2) thumb_h = track_h - 2;
            int thumb_y = track_y + 1;
            if (max_top > 0) {
                thumb_y = track_y + 1 + ((track_h - 2 - thumb_h) * s_tbb10bj_links_top) / max_top;
            }
            tbb10ah_gfx_rectfill(track_x + 1, thumb_y, 2, thumb_h, TBB10AH_C_LINK);
        }
    }
    else {
        tbb10ah_draw_text_clip(30, 70, "No hay enlaces en esta pagina", TBB10AH_C_MUTED, 48);
    }

    tbb10ah_draw_text_clip(30, 176, "j/k scroll  click abre  l cierra", TBB10AH_C_MUTED, 48);
}

static void tbb10ah_draw_page(tbb10ah_doc_t *doc, const char *label, int top, int hist_count, int forward_count,
                              const char *status, bool show_help, bool show_links)
{
    if (doc == NULL) return;
    if (top < 0) top = 0;
    int max_top = doc->count - TBB10AH_PAGE_LINES;
    if (max_top < 0) max_top = 0;
    if (top > max_top) top = max_top;

    int end_line = (top + TBB10AH_PAGE_LINES < doc->count) ? (top + TBB10AH_PAGE_LINES) : doc->count;
    int pct = 100;
    if (max_top > 0) pct = (top * 100) / max_top;

    tbb10ah_gfx_clear(TBB10AH_C_BG);
    tbb10ah_gfx_rectfill(0, 0, TBB10AH_GUI_W, 22, TBB10AH_C_TOP);
    tbb10ah_gfx_hline(0, 22, TBB10AH_GUI_W, TBB10AH_C_BORDER);
    tbb10ah_draw_text_clip(8, 6, "Arielo MiniPC OS - TactileBrowser 10BO RAMSAFE", TBB10AH_C_WHITE, 62);

    tbb10bk_update_fav_delete_selection(doc, label);
    tbb10al_draw_header_navbar((doc->title[0] != '\0') ? doc->title : "(sin titulo)",
                              label ? label : "sample",
                              false);

    tbb10ah_gfx_rectfill(4, 66, 392, 142, TBB10AH_C_PANEL);
    tbb10ah_gfx_rect(4, 66, 392, 142, TBB10AH_C_BORDER);
    tbb10ak_draw_scrollbar(389, 70, 134, top, doc->count);

    for (int i = 0; i < TBB10AH_PAGE_LINES; i++) {
        int idx = top + i;
        int y = 72 + i * 8;
        if (idx < doc->count) {
            char *line = tbb10ah_line_ptr(doc, idx);
            const char *draw = line ? line : "";
            uint8_t color = TBB10AH_C_TEXT;
            uint8_t line_style = (doc->line_style != NULL) ? doc->line_style[idx] : 0;
            int x = 10;
            int maxc = 61;

            if (idx == s_tbb10bh_find_line && s_tbb10bh_find_query[0] != '\0') {
                tbb10ah_gfx_rectfill(7, y - 1, 378, 9, TBB10AH_C_WARN);
                tbb10ah_gfx_rect(7, y - 1, 378, 9, TBB10AH_C_WHITE);
                color = TBB10AH_C_BLACK;
            }

            if (tbb10ak_line_is_h1(line)) {
                tbb10ah_gfx_rectfill(7, y - 2, 378, 10, TBB10AH_C_TOP);
                tbb10ah_gfx_hline(8, y + 8, 374, TBB10AH_C_BORDER);
                draw = tbb10ak_skip_heading_prefix(line);
                color = TBB10AH_C_WARN;
                x = 12;
                maxc = 58;
                line_style |= TBB10BP_STYLE_BOLD;
            }
            else if (tbb10ak_line_is_h2(line)) {
                tbb10ah_gfx_rectfill(8, y - 1, 374, 9, TBB10AH_C_PANEL2);
                draw = tbb10ak_skip_heading_prefix(line);
                color = TBB10AH_C_GOOD;
                x = 14;
                maxc = 57;
                line_style |= TBB10BP_STYLE_BOLD;
            }
            else if (tbb10ak_line_is_link(line)) {
                int link_n = tbb10ai_parse_link_number_from_line(line);
                bool hovered_link = (link_n >= 1 &&
                                     s_tbb10ba_hover_key == TBB10AR_TOUCH_LINK_BASE + (link_n - 1));
                bool selected_fav = (link_n >= 1 && tbb10bk_label_is_bookmarks(label) &&
                                     s_tbb10bk_selected_fav_idx == (link_n - 1));
                if (hovered_link || selected_fav) {
                    /* 10BA/10BK_FIX3: solo el enlace apuntado o el favorito
                     * seleccionado queda resaltado. */
                    tbb10ah_gfx_rectfill(8, y - 1, 374, 9, TBB10AH_C_LINK);
                    tbb10ah_gfx_rect(8, y - 1, 374, 9, TBB10AH_C_WHITE);
                    color = TBB10AH_C_BLACK;
                    x = 14;
                    maxc = 58;
                }
                else {
                    /* Sin caja permanente: el listado queda limpio. */
                    color = TBB10AH_C_LINK;
                    x = 10;
                    maxc = 61;
                }
            }
            else if (tbb10ak_line_is_bullet(line)) {
                draw = tbb10ak_skip_bullet_prefix(line);
                tbb10ah_draw_text_clip(14, y, ">", TBB10AH_C_GOOD, 1);
                color = TBB10AH_C_TEXT;
                x = 26;
                maxc = 56;
            }

            if (idx == s_tbb10bh_find_line && s_tbb10bh_find_query[0] != '\0') {
                color = TBB10AH_C_BLACK;
            }
            if (idx == s_tbb10bh_find_line && s_tbb10bh_find_query[0] != '\0') {
                line_style &= (uint8_t)~TBB10BP_STYLE_COLOR_MASK;
            }
            tbb10bp_draw_text_clip_style(x, y, draw, color, maxc, line_style);
        }
        else {
            tbb10ah_draw_text_clip(10, y, "~", TBB10AH_C_MUTED, 2);
        }
    }

    tbb10ah_gfx_rectfill(4, 212, 392, 24, TBB10AH_C_TOP);
    tbb10ah_gfx_rect(4, 212, 392, 24, TBB10AH_C_BORDER);
    char info[128];
    snprintf(info, sizeof(info), "ln %d-%d/%d %d%% links=%d back=%d fwd=%d", top + 1, end_line, doc->count, pct, doc->link_count, hist_count, forward_count);
    tbb10ah_draw_text_clip(10, 216, info, TBB10AH_C_TEXT, 62);

    char hover_info[128];
    tbb10bh_hover_status(doc, hover_info, sizeof(hover_info));
    if (hover_info[0] != '\0') {
        tbb10ah_draw_text_clip(10, 228, hover_info, TBB10AH_C_LINK, 62);
    }
    else if (s_tbb10bh_find_query[0] != '\0') {
        char fmsg[96];
        snprintf(fmsg, sizeof(fmsg), "/%.48s  n/N buscar  e lector", s_tbb10bh_find_query);
        tbb10ah_draw_text_clip(10, 228, fmsg, TBB10AH_C_GOOD, 62);
    }
    else if (status != NULL && status[0] != '\0') {
        /* 10BI_FIX3: los mensajes de estado ya no se pintan en la cabecera,
         * porque tapaban el titulo. Se aprovecha la barra inferior.
         */
        tbb10ah_draw_text_clip(10, 228, status, TBB10AH_C_GOOD, 62);
    }
    else {
        tbb10ah_draw_text_clip(10, 228, "/ buscar  e lector  n/SPACE pagina", TBB10AH_C_MUTED, 62);
    }

    if (show_help) tbb10ah_draw_overlay_help();
    if (show_links) tbb10ah_draw_overlay_links(doc);

    tbb10ah_present();
}


/* ========================= 10AI TOUCH BUTTONS =========================
 * El GT911 entrega 800x480 fisico; la GUI trabaja en SM_400X240, por eso
 * dividimos entre 2. Se devuelve una tecla virtual compatible con el bucle
 * existente, para no tocar la logica validada de enlaces/back/favoritos.
 */
static int64_t s_tbb10ai_last_touch_us = 0;

/* 10AR: tecla virtual interna para que el tactil pueda abrir enlaces
 * [10], [11]... sin limitarse a las teclas ASCII '1'..'9'.
 */
#ifndef TBB10AR_TOUCH_LINK_BASE
#define TBB10AR_TOUCH_LINK_BASE 1000
#endif

static int tbb10ai_parse_link_number_from_line(const char *line)
{
    if (line == NULL) return -1;

    /* 10AR: los enlaces pueden aparecer como "[1]", "- [1]",
     * "> [1]" o dentro de una linea de texto. Buscamos el primer
     * patron [numero] de forma segura.
     */
    for (const char *p = line; *p != '\0'; ++p) {
        if (*p != '[') continue;

        int n = 0;
        int digits = 0;
        const char *q = p + 1;
        while (*q >= '0' && *q <= '9' && digits < 3) {
            n = (n * 10) + (*q - '0');
            q++;
            digits++;
        }
        if (digits > 0 && *q == ']') {
            return n;
        }
    }

    return -1;
}

static int tbb10ai_map_touch_to_key(tbb10ah_doc_t *doc, int top, bool show_links, int x, int y)
{
    if (x < 0) x = 0;
    if (x >= TBB10AH_GUI_W) x = TBB10AH_GUI_W - 1;
    if (y < 0) y = 0;
    if (y >= TBB10AH_GUI_H) y = TBB10AH_GUI_H - 1;

    /* 10BJ_FIX1: botones con zona tactil/raton completa.
     * La fila visual mide ~18 px; antes solo aceptaba hasta y<43 y
     * clicks en la parte baja de HOME/HIST/FAV caian fuera o sobre URL.
     */
    if (y >= 24 && y < 47) {
        if (x >= 8   && x < 50)  return 'q';    /* EXIT */
        if (x >= 54  && x < 80)  return 'b';    /* < back */
        if (x >= 84  && x < 110) return 'F';    /* > forward */
        if (x >= 114 && x < 162) return 'H';    /* HOME */
        if (x >= 166 && x < 204) return 'r';    /* RLD */
        if (x >= 208 && x < 252) return 's';    /* HIST */
        if (x >= 256 && x < 294) return 'v';    /* FAV */
        if (x >= 298 && x < 344) return 'm';    /* +FAV */
        if (x >= 348 && x < 390) return 'l';    /* LINK */
        return -1;
    }

    /* Tocar la barra URL activa edicion. */
    if (y >= 47 && y < 64) {
        return 'U';
    }

    /* Overlay de enlaces: tocar una fila abre el enlace real,
     * sin depender de que sea [1]..[9].
     */
    if (show_links && x >= 20 && x < 372 && y >= 64 && y < 176) {
        int row = (y - 68) / 12;
        int idx = s_tbb10bj_links_top + row;
        if (row >= 0 && row < 9 && doc != NULL && idx >= 0 && idx < doc->link_count) {
            return TBB10AR_TOUCH_LINK_BASE + idx;
        }
        return -1;
    }

    /* Area de contenido: si se toca una linea que empieza con [n], abrir enlace. */
    if (x >= 4 && x < 396 && y >= 66 && y < 208) {
        if (doc != NULL) {
            int row = (y - 72) / 8;
            if (row >= 0 && row < TBB10AH_PAGE_LINES) {
                int idx = top + row;
                char *line = tbb10ah_line_ptr(doc, idx);
                int n = tbb10ai_parse_link_number_from_line(line);
                if (n >= 1 && n <= doc->link_count) {
                    return TBB10AR_TOUCH_LINK_BASE + (n - 1);
                }
            }
        }

        /* Si no toca enlace, zona alta sube pagina y zona baja baja pagina. */
        return (y < 128) ? 'p' : 'n';
    }

    /* Cabecera: tocar el titulo/ruta vuelve al home interno. */
    if (y < 24) {
        return 'H';
    }

    return -1;
}

static int tbb10ai_read_input_key(tbb10ah_doc_t *doc, int top, bool show_links, int timeout_ms)
{
    s_tbb10bk_last_input_was_pointer = false;
    int ch = vterm_getchar(vterm_get_active(), timeout_ms);

    /* 10AR: flechas de teclado. Muchas mini-teclas USB envian secuencias
     * ANSI: ESC [ A/B/C/D. Las traducimos a las teclas que ya entiende
     * el visor, sin tocar la logica estable.
     */
    if (ch == 27) {
        int ch2 = vterm_getchar(vterm_get_active(), 20);
        if (ch2 == '[') {
            int ch3 = vterm_getchar(vterm_get_active(), 20);
            if (ch3 == 'A') return 'k';  /* flecha arriba: una linea arriba */
            if (ch3 == 'B') return 'j';  /* flecha abajo: una linea abajo */
            if (ch3 == 'D') return 'b';  /* flecha izquierda: atras */
            if (ch3 == 'C') return 'F';  /* flecha derecha: adelante */

            /* PageUp/PageDown/Home/End: ESC [ 5~/6~/1~/4~ */
            if (ch3 == '5') { (void) vterm_getchar(vterm_get_active(), 20); return 'p'; }
            if (ch3 == '6') { (void) vterm_getchar(vterm_get_active(), 20); return 'n'; }
            if (ch3 == '1' || ch3 == 'H') { (void) vterm_getchar(vterm_get_active(), 20); return 'g'; }
            if (ch3 == '4' || ch3 == 'F') { (void) vterm_getchar(vterm_get_active(), 20); return 'G'; }
        }
        return 27;
    }

    if (ch >= 0) return ch;

    /* 10BA: raton USB real, no solo GT911. El mouse ya da coordenadas
     * logicas 400x240 en usb_hid_mouse_get_state(). Click izquierdo = accion.
     * Mover sin click solo actualiza hover para que el siguiente refresco pinte
     * botones/enlaces apuntados en azul.
     */
    if (usb_hid_mouse_connected()) {
        int mx = 0, my = 0;
        uint8_t buttons = 0;
        usb_hid_mouse_get_state(&mx, &my, &buttons);
        if (mx < 0) mx = 0;
        if (mx >= TBB10AH_GUI_W) mx = TBB10AH_GUI_W - 1;
        if (my < 0) my = 0;
        if (my >= TBB10AH_GUI_H) my = TBB10AH_GUI_H - 1;

        s_tbb10ba_hover_x = mx;
        s_tbb10ba_hover_y = my;
        s_tbb10ba_hover_key = tbb10ai_map_touch_to_key(doc, top, show_links, mx, my);

        bool left_click = ((buttons & 0x01) != 0) && ((s_tbb10ba_prev_mouse_buttons & 0x01) == 0);
        s_tbb10ba_prev_mouse_buttons = buttons;
        if (left_click && s_tbb10ba_hover_key >= 0) {
            if (s_tbb10ba_hover_key >= TBB10AR_TOUCH_LINK_BASE) {
                printf("[TBBROWSER10BB][MOUSE] x=%d y=%d -> link=%d\n",
                       mx, my, s_tbb10ba_hover_key - TBB10AR_TOUCH_LINK_BASE + 1);
            }
            else {
                printf("[TBBROWSER10BB][MOUSE] x=%d y=%d -> key=%c\n", mx, my, (char) s_tbb10ba_hover_key);
            }
            s_tbb10bk_last_input_was_pointer = true;
            return s_tbb10ba_hover_key;
        }
    }
    else {
        s_tbb10ba_prev_mouse_buttons = 0;
    }

    if (!minipc_touch_ok()) return -1;

    int16_t tx = 0, ty = 0;
    if (!minipc_touch_read(&tx, &ty)) return -1;

    int64_t now = esp_timer_get_time();
    if ((now - s_tbb10ai_last_touch_us) < 220000) {
        return -1;
    }
    s_tbb10ai_last_touch_us = now;

    int x = tx / 2;
    int y = ty / 2;
    s_tbb10ba_hover_x = x;
    s_tbb10ba_hover_y = y;
    int key = tbb10ai_map_touch_to_key(doc, top, show_links, x, y);
    s_tbb10ba_hover_key = key;
    if (key >= 0) {
        if (key >= TBB10AR_TOUCH_LINK_BASE) {
            printf("[TBBROWSER10BB][TOUCH] x=%d y=%d -> link=%d\n",
                   x, y, key - TBB10AR_TOUCH_LINK_BASE + 1);
        }
        else {
            printf("[TBBROWSER10BB][TOUCH] x=%d y=%d -> key=%c\n", x, y, (char) key);
        }
    }
    if (key >= 0) {
        s_tbb10bk_last_input_was_pointer = true;
    }
    return key;
}

static int tbb10ba_collect_link_number(int first_digit)
{
    int n = first_digit;
    int digits = 1;

    /* Permite abrir [13], [27], etc. sin que el primer '1' dispare
     * inmediatamente el enlace 1. ENTER confirma antes; si no hay mas teclas,
     * abre el numero escrito tras una pausa corta.
     */
    while (digits < 3) {
        int ch = vterm_getchar(vterm_get_active(), 450);
        if (ch >= '0' && ch <= '9') {
            n = (n * 10) + (ch - '0');
            digits++;
            continue;
        }
        if (ch == '\r' || ch == '\n') break;
        break;
    }
    return n;
}

/* 10BK_FIX4: restaura helper de modo lector.
 * En algunas ramas de paneles/FAV se mantuvo la llamada con tecla 'e',
 * pero el helper quedo fuera del fichero y C lo tomaba como declaracion
 * implicita. Lo dejamos aqui, antes de tbb10ah_viewer_loop().
 */
static int tbb10bh_reader_start_line(tbb10ah_doc_t *doc)
{
    if (doc == NULL || doc->count <= 0) return 0;

    int fallback = 0;
    for (int i = 0; i < doc->count; i++) {
        const char *line = tbb10ah_line_ptr(doc, i);
        if (line == NULL) continue;

        while (*line == ' ' || *line == '\t') line++;
        if (*line == '\0') continue;

        /* Saltar cabeceras/ruido tipico de HTML10D. */
        if (tbb10au_strcasestr(line, "WEB:") == line) continue;
        if (tbb10au_strcasestr(line, "TITULO:") == line) continue;
        if (tbb10au_strcasestr(line, "URL:") == line) continue;
        if (tbb10au_strcasestr(line, "Saltar al contenido") != NULL) continue;
        if (tbb10au_strcasestr(line, "Skip to content") != NULL) continue;
        if (tbb10au_strcasestr(line, "Toggle navigation") != NULL) continue;
        if (tbb10au_strcasestr(line, "Menú") == line || tbb10au_strcasestr(line, "Menu") == line) continue;

        int len = (int)strlen(line);
        if (len > 18 && fallback == 0) fallback = i;

        /* Preferir parrafos largos, no solo enlaces/listas cortas. */
        if (len > 40 && tbb10ai_parse_link_number_from_line(line) < 1) {
            return i;
        }
    }

    return fallback;
}


static void tbb10al_draw_loading_page(const char *url);

static int tbb10ah_viewer_loop(tbb10ah_doc_t *doc, const char *label, int hist_count, int forward_count,
                                char *out_open_path, size_t out_cap, bool *out_reload, bool *out_back, bool *out_forward)
{
    if (out_open_path != NULL && out_cap > 0) out_open_path[0] = '\0';
    if (out_reload != NULL) *out_reload = false;
    if (out_back != NULL) *out_back = false;
    if (out_forward != NULL) *out_forward = false;
    if (doc == NULL) return 1;

    int top = 0;
    int max_top = doc->count - TBB10AH_PAGE_LINES;
    if (max_top < 0) max_top = 0;

    if (tbb10ah_gui_begin() != 0) {
        printf("[TBBROWSER10AH][ERR] no pude iniciar GUI\n");
        return 1;
    }

    bool show_help = false;
    bool show_links = false;
    char status[128];
    s_tbb10bh_find_line = -1;
    status[0] = '\0';

    while (true) {
        if (top < 0) top = 0;
        if (top > max_top) top = max_top;

        tbb10ah_draw_page(doc, label, top, hist_count, forward_count, status, show_help, show_links);
        int ch = tbb10ai_read_input_key(doc, top, show_links, 80);
        if (ch < 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (ch >= TBB10AR_TOUCH_LINK_BASE && ch < TBB10AR_TOUCH_LINK_BASE + TBB10AH_MAX_LINKS) {
            int idx = ch - TBB10AR_TOUCH_LINK_BASE;
            status[0] = '\0';
            show_help = false;
            show_links = false;
            if (idx >= 0 && idx < doc->link_count) {
                char *href = tbb10ah_link_ptr(doc, idx);
                if (href != NULL && out_open_path != NULL && out_cap > 0) {
                    if (tbb10bk_label_is_bookmarks(label) &&
                        tbb10bk_href_is_deletable_fav(href) &&
                        s_tbb10bk_last_input_was_pointer) {
                        int64_t now_us = esp_timer_get_time();
                        bool double_click = (s_tbb10bk_selected_fav_idx == idx &&
                                             s_tbb10bk_last_fav_click_us > 0 &&
                                             (now_us - s_tbb10bk_last_fav_click_us) < 850000);
                        s_tbb10bk_selected_fav_idx = idx;
                        s_tbb10bk_last_fav_click_us = now_us;
                        tbb10ah_safe_copy(s_tbb10bk_fav_delete_path, sizeof(s_tbb10bk_fav_delete_path), href);
                        s_tbb10bk_header_minus_fav = true;
                        if (!double_click) {
                            tbb10ah_safe_copy(status, sizeof(status),
                                               "favorito seleccionado: -FAV borra, doble click abre");
                            continue;
                        }
                    }
                    if (tbb10bb_is_form_search_href(href)) {
                        if (tbb10bb_prompt_form_search(label, out_open_path, out_cap)) {
                            tbb10al_draw_loading_page(out_open_path);
                            tbb10ah_gui_end();
                            return 2;
                        }
                        tbb10ah_safe_copy(status, sizeof(status), "busqueda FORM cancelada");
                        continue;
                    }
                    tbb10ah_safe_copy(out_open_path, out_cap, href);
                    tbb10al_draw_loading_page(href);
                    tbb10ah_gui_end();
                    return 2;
                }
            }
            tbb10ah_safe_copy(status, sizeof(status), "enlace tactil fuera de rango");
            continue;
        }

        char c = (char) ch;
        status[0] = '\0';

        if (c == 'n' && s_tbb10bh_find_query[0] != '\0') {
            int start = (s_tbb10bh_find_line >= 0) ? (s_tbb10bh_find_line + 1) : top;
            int found = tbb10bh_find_match(doc, s_tbb10bh_find_query, start, 1);
            if (found >= 0) {
                s_tbb10bh_find_line = found;
                top = found;
                snprintf(status, sizeof(status), "encontrado: %s", s_tbb10bh_find_query);
            }
            else {
                tbb10ah_safe_copy(status, sizeof(status), "texto no encontrado");
            }
        }
        else if (c == 'N' && s_tbb10bh_find_query[0] != '\0') {
            int start = (s_tbb10bh_find_line >= 0) ? (s_tbb10bh_find_line - 1) : top;
            int found = tbb10bh_find_match(doc, s_tbb10bh_find_query, start, -1);
            if (found >= 0) {
                s_tbb10bh_find_line = found;
                top = found;
                snprintf(status, sizeof(status), "encontrado: %s", s_tbb10bh_find_query);
            }
            else {
                tbb10ah_safe_copy(status, sizeof(status), "texto no encontrado");
            }
        }
        else if (c == '\r' || c == '\n' || c == 'n' || c == 'N' || c == ' ') {
            top += TBB10AH_PAGE_LINES;
            show_help = false;
            show_links = false;
        }
        else if (c == 'p' || c == 'P') {
            top -= TBB10AH_PAGE_LINES;
            show_help = false;
            show_links = false;
        }
        else if (c == 'j' || c == 'd') {
            top += 1;
        }
        else if (c == 'k' || c == 'u') {
            top -= 1;
        }
        else if (c == 'g') {
            top = 0;
        }
        else if (c == 'G') {
            top = max_top;
        }
        else if (c == 'H') {
            if (out_open_path != NULL && out_cap > 0) {
                tbb10ah_safe_copy(out_open_path, out_cap, "about:home");
                tbb10al_draw_loading_page("about:home");
                tbb10ah_gui_end();
                return 2;
            }
        }
        else if (c == 'F') {
            if (forward_count <= 0) {
                tbb10ah_safe_copy(status, sizeof(status), "sin historial adelante");
            }
            else {
                if (out_forward != NULL) *out_forward = true;
                tbb10al_draw_loading_page("historial adelante");
                tbb10ah_gui_end();
                return 2;
            }
        }
        else if (c == 's' || c == 'S') {
            if (out_open_path != NULL && out_cap > 0) {
                tbb10ah_safe_copy(out_open_path, out_cap, "about:history");
                tbb10al_draw_loading_page("about:history");
                tbb10ah_gui_end();
                return 2;
            }
        }
        else if (c == '/') {
            char q[64];
            if (tbb10bh_gui_read_find(q, sizeof(q))) {
                tbb10ah_safe_copy(s_tbb10bh_find_query, sizeof(s_tbb10bh_find_query), q);
                int found = tbb10bh_find_match(doc, s_tbb10bh_find_query, top, 1);
                if (found >= 0) {
                    s_tbb10bh_find_line = found;
                    top = found;
                    snprintf(status, sizeof(status), "encontrado: %s", s_tbb10bh_find_query);
                }
                else {
                    s_tbb10bh_find_line = -1;
                    tbb10ah_safe_copy(status, sizeof(status), "texto no encontrado");
                }
            }
            else {
                tbb10ah_safe_copy(status, sizeof(status), "busqueda cancelada");
            }
            show_help = false;
            show_links = false;
        }
        else if (c == 'e' || c == 'E') {
            int start = tbb10bh_reader_start_line(doc);
            top = start;
            s_tbb10bh_find_line = -1;
            tbb10ah_safe_copy(status, sizeof(status), "modo lector: salto al contenido");
            show_help = false;
            show_links = false;
        }
        else if (c == 'h' || c == '?') {
            show_help = !show_help;
            show_links = false;
        }
        else if (c == 'l' || c == 'L') {
            show_links = !show_links;
            show_help = false;
            if (show_links) {
                s_tbb10bj_links_top = 0;
                tbb10ah_safe_copy(status, sizeof(status), "LINK: j/k scroll, click abre");
            }
        }
        else if (c == 'm' || c == 'M') {
            if (tbb10bk_label_is_bookmarks(label) && s_tbb10bk_header_minus_fav &&
                s_tbb10bk_fav_delete_path[0] != '\0') {
                char deleted_path[TBB10AH_PATH_CAP];
                tbb10ah_safe_copy(deleted_path, sizeof(deleted_path), s_tbb10bk_fav_delete_path);
                if (tbb10bk_bookmark_delete(deleted_path) == 0) {
                    s_tbb10bk_fav_delete_path[0] = '\0';
                    s_tbb10bk_selected_fav_idx = -1;
                    s_tbb10bk_last_fav_click_us = 0;
                    s_tbb10bk_header_minus_fav = false;
                    if (out_open_path != NULL && out_cap > 0) {
                        tbb10ah_safe_copy(out_open_path, out_cap, "about:bookmarks");
                        tbb10al_draw_loading_page("favorito eliminado");
                        tbb10ah_gui_end();
                        return 2;
                    }
                    tbb10ah_safe_copy(status, sizeof(status), "favorito eliminado");
                }
                else {
                    tbb10ah_safe_copy(status, sizeof(status), "no pude eliminar favorito");
                }
            }
            else {
                (void) tbb10ah_bookmark_add(label, (doc->title[0] != '\0') ? doc->title : label);
                tbb10ah_safe_copy(status, sizeof(status), "favorito guardado");
            }
        }
        else if (c == 'u' || c == 'U') {
            if (out_open_path != NULL && out_cap > 0) {
                char typed[TBB10AL_URL_CAP];
                if (tbb10al_gui_read_url(typed, sizeof(typed), label)) {
                    tbb10ah_safe_copy(out_open_path, out_cap, typed);
                    tbb10al_draw_loading_page(typed);
                    tbb10ah_gui_end();
                    return 2;
                }
                tbb10ah_safe_copy(status, sizeof(status), "URL cancelada");
            }
        }
        else if (c == 'o' || c == 'O') {
            if (out_open_path != NULL && out_cap > 0) {
                tbb10ah_safe_copy(out_open_path, out_cap, "about:files");
                tbb10al_draw_loading_page("about:files");
                tbb10ah_gui_end();
                return 2;
            }
        }
        else if (c == 'v' || c == 'V') {
            if (out_open_path != NULL && out_cap > 0) {
                tbb10ah_safe_copy(out_open_path, out_cap, "about:bookmarks");
                tbb10al_draw_loading_page("about:bookmarks");
                tbb10ah_gui_end();
                return 2;
            }
        }
        else if (c == 'r' || c == 'R') {
            if (out_reload != NULL) *out_reload = true;
            tbb10al_draw_loading_page(label ? label : "recargando");
            tbb10ah_gui_end();
            return 2;
        }
        else if (c == 'b' || c == 'B') {
            if (hist_count <= 0) {
                tbb10ah_safe_copy(status, sizeof(status), "sin historial atras");
            }
            else {
                if (out_back != NULL) *out_back = true;
                tbb10al_draw_loading_page("historial atras");
                tbb10ah_gui_end();
                return 2;
            }
        }
        else if (c >= '1' && c <= '9') {
            int n = tbb10ba_collect_link_number(c - '0');
            if (n >= 1 && n <= doc->link_count) {
                char *href = tbb10ah_link_ptr(doc, n - 1);
                if (href != NULL && out_open_path != NULL && out_cap > 0) {
                    if (tbb10bb_is_form_search_href(href)) {
                        if (tbb10bb_prompt_form_search(label, out_open_path, out_cap)) {
                            tbb10al_draw_loading_page(out_open_path);
                            tbb10ah_gui_end();
                            return 2;
                        }
                        tbb10ah_safe_copy(status, sizeof(status), "busqueda FORM cancelada");
                        continue;
                    }
                    tbb10ah_safe_copy(out_open_path, out_cap, href);
                    tbb10al_draw_loading_page(href);
                    tbb10ah_gui_end();
                    return 2;
                }
            }
            else {
                char msg[64];
                snprintf(msg, sizeof(msg), "enlace %d fuera de rango", n);
                tbb10ah_safe_copy(status, sizeof(status), msg);
            }
        }
        else if (c == 'q' || c == 'Q') {
            break;
        }
        else if (c == 27) {
            tbb10ah_safe_copy(status, sizeof(status), "ESC ignorado: q sale");
        }
        else {
            tbb10ah_safe_copy(status, sizeof(status), "tecla no usada");
        }
    }

    tbb10ah_gui_end();
    printf("[TBBROWSER10AH] navegador GUI cerrado.\n");
    return 0;
}

static int tbb10ah_build_and_view_once(const char *label, const char *html, size_t html_len, int hist_count, int forward_count,
                                      char *out_open_path, size_t out_cap, bool *out_reload, bool *out_back, bool *out_forward)
{
    tbb10ah_doc_t doc;
    int r = tbb10ah_build_doc_from_html(label, html, html_len, &doc);
    if (r != 0) return r;

    r = tbb10ah_viewer_loop(&doc, label, hist_count, forward_count, out_open_path, out_cap, out_reload, out_back, out_forward);
    tbb10ah_doc_free(&doc);
    tbb10ah_heap_print("after_view");
    return r;
}

static void tbb10ah_history_push(char hist[TBB10AH_HISTORY_MAX][TBB10AH_PATH_CAP], int *count, const char *path)
{
    if (hist == NULL || count == NULL || path == NULL || path[0] == '\0') return;

    if (*count >= TBB10AH_HISTORY_MAX) {
        /*
         * FIX 10AH-1:
         * GCC con -Werror=restrict avisa en snprintf(hist[i-1], ..., hist[i])
         * porque son filas del mismo array. Aunque en la practica son filas
         * distintas, usamos memmove() para desplazar el historial completo
         * de una vez y evitar cualquier duda de solape.
         */
        memmove(hist[0], hist[1], (TBB10AH_HISTORY_MAX - 1) * TBB10AH_PATH_CAP);
        hist[TBB10AH_HISTORY_MAX - 1][0] = '\0';
        *count = TBB10AH_HISTORY_MAX - 1;
    }

    tbb10ah_safe_copy(hist[*count], TBB10AH_PATH_CAP, path);
    (*count)++;
}

static bool tbb10ah_history_pop(char hist[TBB10AH_HISTORY_MAX][TBB10AH_PATH_CAP], int *count,
                                char *out, size_t out_cap)
{
    if (hist == NULL || count == NULL || out == NULL || out_cap == 0) return false;
    if (*count <= 0) return false;

    (*count)--;
    tbb10ah_safe_copy(out, out_cap, hist[*count]);
    hist[*count][0] = '\0';
    return true;
}


static void tbb10al_draw_loading_page(const char *url)
{
    /* 10AO: feedback grafico inmediato y reloj de arena en cabecera. */
    if (s_tbb10ah_back == NULL) return;
    tbb10an_header_busy_set(true);
    tbb10ah_gfx_clear(TBB10AH_C_BG);
    tbb10ah_gfx_rectfill(0, 0, TBB10AH_GUI_W, 22, TBB10AH_C_TOP);
    tbb10ah_gfx_hline(0, 22, TBB10AH_GUI_W, TBB10AH_C_BORDER);
    tbb10ah_draw_text_clip(8, 6, "Arielo MiniPC OS - TactileBrowser 10BO RAMSAFE", TBB10AH_C_WHITE, 62);
    tbb10al_draw_header_navbar("Cargando", url ? url : "", false);
    tbb10ah_gfx_rectfill(4, 66, 392, 142, TBB10AH_C_PANEL);
    tbb10ah_gfx_rect(4, 66, 392, 142, TBB10AH_C_BORDER);
    tbb10ah_draw_text_clip(20, 84, "Cargando pagina...", TBB10AH_C_WARN, 50);
    tbb10ah_draw_text_clip(20, 100, "Al terminar se mostrara el contenido automaticamente.", TBB10AH_C_TEXT, 58);
    tbb10ah_gfx_rectfill(4, 212, 392, 24, TBB10AH_C_TOP);
    tbb10ah_gfx_rect(4, 212, 392, 24, TBB10AH_C_BORDER);
    tbb10ah_draw_text_clip(10, 220, "Reloj de arena activo en cabecera. Espere...", TBB10AH_C_GOOD, 56);
    tbb10ah_present();
}

static int tbb10ah_browser_session(const char *initial_path, bool initial_sample)
{
    char *current = s_tbb10bb_session_current;
    char *next = s_tbb10bb_session_next;
    char (*history)[TBB10AH_PATH_CAP] = s_tbb10bb_session_history;
    char (*forward)[TBB10AH_PATH_CAP] = s_tbb10bb_session_forward;
    bool sample = initial_sample;
    int history_count = 0;
    int forward_count = 0;
    int guard = 0;

    memset(s_tbb10bb_session_history, 0, sizeof(s_tbb10bb_session_history));
    memset(s_tbb10bb_session_forward, 0, sizeof(s_tbb10bb_session_forward));

    /* 10AH_FIX2: una sesion = un solo periodo grafico.
     * Las paginas cambian, pero el modo LCD y sus framebuffers no se
     * desmontan hasta salir con q o error final.
     */
    s_tbb10ah_keep_gui_mode = true;

    if (initial_path != NULL && initial_path[0] != '\0') {
        if (strcmp(initial_path, "home") == 0) tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, "about:home");
        else tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, initial_path);
        if (current[0] == '/') tbb10ah_normalize_abs_path(current);
        sample = false;
    }
    else {
        strcpy(current, "sample");
        sample = true;
    }

    while (guard++ < 96) {
        {
            char canon_current[TBB10AH_PATH_CAP];
            if (tbb10bk_canonical_internal_url(current, canon_current, sizeof(canon_current)) &&
                strcmp(current, canon_current) != 0) {
                printf("[TBBROWSER10BK_FIX2][INTERNAL] %s -> %s\n", current, canon_current);
                tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, canon_current);
                sample = false;
            }
        }
        next[0] = '\0';
        bool reload = false;
        bool back = false;
        bool forward_req = false;
        int r = 0;

        tbb10am_update_history_snapshot(current, history, history_count, forward, forward_count);

        if (sample) {
            const char *html = tbb10ah_sample_html();
            tbb10an_header_busy_set(false);
            r = tbb10ah_build_and_view_once("sample", html, strlen(html), history_count, forward_count,
                                            next, TBB10AH_PATH_CAP, &reload, &back, &forward_req);
        }
        else if (tbb10ah_is_internal_url(current)) {
            char *html = tbb10ah_make_internal_html(current);
            if (html == NULL) {
                tbb10an_header_busy_set(false);
                s_tbb10ah_keep_gui_mode = false;
                tbb10ah_gui_end();
                printf("[TBBROWSER10AH][ERR] no pude crear pagina interna: %s\n", current);
                tbb10ah_pause_enter(NULL);
                return 1;
            }
            tbb10an_header_busy_set(false);
            r = tbb10ah_build_and_view_once(current, html, strlen(html), history_count, forward_count,
                                            next, TBB10AH_PATH_CAP, &reload, &back, &forward_req);
            heap_caps_free(html);
        }
        else {
            size_t len = 0;
            char *html = NULL;
            if (tbb10al_is_http_url(current)) {
                char err[192];
                err[0] = '\0';
                tbb10al_draw_loading_page(current);
                if (s_tbb10bp_post_pending && s_tbb10bp_post_url && s_tbb10bp_post_body &&
                    strcmp(current, TBB10BQ_LOCAL_POST_URL) == 0 && strcmp(current, s_tbb10bp_post_url) == 0) {
                    printf("[TBBROWSER10BO][LOCAL_POST] body=%s\n", s_tbb10bp_post_body);
                    s_tbb10bp_post_pending = false;
                    html = tbb10bq_make_local_post_echo_html(s_tbb10bp_post_body);
                    if (html != NULL) len = strlen(html);
                    s_tbb10bp_post_body[0] = '\0';
                    s_tbb10bp_post_url[0] = '\0';
                }
                else if (s_tbb10bp_post_pending && s_tbb10bp_post_url && s_tbb10bp_post_body &&
                         strcmp(current, s_tbb10bp_post_url) == 0) {
                    printf("[TBBROWSER10BP][NAV] ejecutando POST pendiente\n");
                    s_tbb10bp_post_pending = false; /* una sola vez; reload posterior sera GET */
                    html = tbb10bp_http_post(current, s_tbb10bp_post_body, &len, err, sizeof(err));
                    s_tbb10bp_post_body[0] = '\0';
                    s_tbb10bp_post_url[0] = '\0';
                } else {
                    html = tbb10al_http_get(current, &len, err, sizeof(err));
                }
                if (html != NULL && s_tbb10au_effective_url[0] != '\0' && strcmp(current, s_tbb10au_effective_url) != 0) {
                    printf("[TBBROWSER10AW][NAV] effective URL: %s -> %s\n", current, s_tbb10au_effective_url);
                    tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, s_tbb10au_effective_url);
                }
                if (html != NULL) {
                    char *classic = tbb10aw_make_classic_html10d_page(current, html, len);
                    if (classic != NULL) {
                        heap_caps_free(html);
                        html = classic;
                        len = strlen(html);
                    }
                }
                if (html == NULL) {
                    html = tbb10al_make_error_html(current, err[0] ? err : "fallo HTTP");
                    if (html != NULL) len = strlen(html);
                }
            }
            else {
                tbb10al_draw_loading_page(current);
                html = tbb10ah_load_file(current, &len);
            }

            if (html == NULL) {
                tbb10an_header_busy_set(false);
                s_tbb10ah_keep_gui_mode = false;
                tbb10ah_gui_end();
                printf("[TBBROWSER10AL][ERR] no pude abrir: %s\n", current);
                tbb10ah_pause_enter(NULL);
                return 1;
            }
            tbb10an_header_busy_set(false);
            r = tbb10ah_build_and_view_once(current, html, len, history_count, forward_count,
                                            next, TBB10AH_PATH_CAP, &reload, &back, &forward_req);
            heap_caps_free(html);
        }

        if (r != 2) {
            tbb10an_header_busy_set(false);
            s_tbb10ah_keep_gui_mode = false;
            tbb10ah_gui_end();
            return r;
        }

        if (reload) {
            continue;
        }

        if (back) {
            char prev[TBB10AH_PATH_CAP];
            if (tbb10ah_history_pop(history, &history_count, prev, sizeof(prev))) {
                tbb10ah_history_push(forward, &forward_count, current);
                tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, prev);
                sample = (strcmp(current, "sample") == 0);
            }
            continue;
        }

        if (forward_req) {
            char nxt[TBB10AH_PATH_CAP];
            if (tbb10ah_history_pop(forward, &forward_count, nxt, sizeof(nxt))) {
                tbb10ah_history_push(history, &history_count, current);
                tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, nxt);
                sample = (strcmp(current, "sample") == 0);
            }
            continue;
        }

        {
            char canon_next[TBB10AH_PATH_CAP];
            if (tbb10bk_canonical_internal_url(next, canon_next, sizeof(canon_next))) {
                tbb10ah_history_push(history, &history_count, current);
                memset(s_tbb10bb_session_forward, 0, sizeof(s_tbb10bb_session_forward));
                forward_count = 0;
                tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, canon_next);
                sample = false;
                continue;
            }
        }

        if (strcmp(next, "sample") == 0) {
            tbb10ah_history_push(history, &history_count, current);
            memset(s_tbb10bb_session_forward, 0, sizeof(s_tbb10bb_session_forward));
            forward_count = 0;
            tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, "sample");
            sample = true;
            continue;
        }

        char resolved[TBB10AH_PATH_CAP];
        if (!tbb10ah_resolve_path(sample ? NULL : current, next, resolved, sizeof(resolved))) {
            tbb10ah_pause_enter("No se pudo resolver/abrir ese enlace. ENTER... ");
            continue;
        }

        tbb10ah_history_push(history, &history_count, current);
        memset(s_tbb10bb_session_forward, 0, sizeof(s_tbb10bb_session_forward));
        forward_count = 0;
        tbb10ah_safe_copy(current, TBB10AH_PATH_CAP, resolved);
        sample = false;
    }

    tbb10an_header_busy_set(false);
    s_tbb10ah_keep_gui_mode = false;
    tbb10ah_gui_end();
    printf("[TBBROWSER10AH][WARN] limite de navegacion alcanzado\n");
    return 0;
}

int cmd_tbbrowser_gui_10ah(int argc, char **argv)
{
    printf("\n[TBBROWSER10AH] TactileBrowser GUI 10BL-FIX1 - DDG estable + URL UTF8\n");

    if (argc <= 1 || strcmp(argv[1], "home") == 0 || strcmp(argv[1], "inicio") == 0) {
        return tbb10ah_browser_session("about:home", false);
    }

    if (strcmp(argv[1], "sample") == 0) {
        return tbb10ah_browser_session(NULL, true);
    }

    if (strncmp(argv[1], "about:", 6) == 0) {
        return tbb10ah_browser_session(argv[1], false);
    }

    if (strcmp(argv[1], "net") == 0) {
        return tbb10ah_browser_session("http://neverssl.com/", false);
    }

    if (strcmp(argv[1], "url") == 0) {
        if (argc < 3) {
            printf("Uso: tbbgui url http://neverssl.com/\n");
            return 1;
        }
        return tbb10ah_browser_session(argv[2], false);
    }

    if (tbb10al_is_http_url(argv[1])) {
        return tbb10ah_browser_session(argv[1], false);
    }

    if (strcmp(argv[1], "bookmarks") == 0 || strcmp(argv[1], "favs") == 0 || strcmp(argv[1], "favoritos") == 0) {
        return tbb10ah_browser_session("about:bookmarks", false);
    }

    if (strcmp(argv[1], "files") == 0 || strcmp(argv[1], "archivos") == 0) {
        return tbb10ah_browser_session("about:files", false);
    }

    if (strcmp(argv[1], "file") == 0) {
        if (argc < 3) {
            printf("Uso: tbbrowser file /sdcard/pagina.html\n");
            return 1;
        }
        return tbb10ah_browser_session(argv[2], false);
    }

    if (argv[1][0] == '/') {
        return tbb10ah_browser_session(argv[1], false);
    }

    printf("Uso: tbbgui [home|sample|net|url URL|http://...|file PATH|/ruta/pagina.html|about:help|about:history|about:bookmarks|about:files|bookmarks|files]\n");
    return 0;
}
