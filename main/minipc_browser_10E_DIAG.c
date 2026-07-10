// minipc_browser.c
// Mini-navegador grafico para la MiniPC. Descarga una URL HTTP y parsea el
// HTML a texto legible para mostrarlo en una ventana del escritorio con scroll.
//
// BROWSER_10E_FIX3_GOOGLE_DDG_GUARD:
//
//   Quinta mejora prudente del navegador.
//   Parte de 10D FIX5 validada y añade redirecciones cliente sencillas SAFE con guard para buscadores:
//     * estado visible HTML10E
//     * mantiene HTTP/HTTPS, busqueda lite y formularios GET
//     * detecta <meta http-equiv="refresh" content="0;url=...">
//     * detecta JavaScript basico de redireccion:
//       window.location = "..."
//       location.href = "..."
//       document.location = "..."
//       location.replace("...")
//       location.assign("...")
//     * no ejecuta JavaScript real, solo extrae destinos claros
//
// 09C_NAVIGATION_CORE:
//   - Parte de 09B_FIX1_LATIN1_CP1252.
//   - Mantiene parser plus, enlaces y correccion Latin1/CP1252.
//   - Añade nucleo de navegacion:
//       * HOME
//       * RELOAD
//       * BACK
//       * FORWARD
//       * historial RAM simple
//       * URL actual y pagina de inicio configurable
//   - No toca todavia el visor grafico: deja la API lista para conectar botones.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "minipc_browser.h"
#include "breezybox.h"
#include "esp_http_client.h"
#include "esp_netif.h"

#define BROWSER_HTTP_TIMEOUT_MS  10000

// Descarga a este fichero temporal (SD si esta montada, si no LittleFS /root)
#define BROWSER_TMP_PATH   "/sdcard/.browser_tmp.html"
#define BROWSER_TMP_ALT    "/root/.browser_tmp.html"

#define BROWSER_REDIRECT_MAX 5
#define BROWSER_DOWNLOAD_REDIRECT 1

// Limites de memoria
#define RAW_MAX     (96 * 1024)
#define TEXT_MAX    (48 * 1024)
#define MAX_LINES   2000

// Ancho de texto aproximado para partir parrafos largos.
// Ajustable si el visor muestra mas/menos caracteres por linea.
#define WRAP_COL    76

#define MAX_LINKS      80
#define LINK_URL_MAX   192
#define LINK_TEXT_MAX  80

#define BROWSER_HISTORY_MAX 16
#define BROWSER_DEFAULT_HOME "http://example.com/"

#define URL_NORM_MAX 256

static const char *TAG = "minipc_browser";

typedef struct {
    FILE *file;
    size_t total;
    char *location;
    size_t location_sz;
} dl_ctx_t;

static char *s_raw  = NULL;
static char *s_text = NULL;
static int   s_text_len = 0;
static int  *s_line_off = NULL;
static int   s_line_count = 0;

static browser_state_t s_state = BROWSER_IDLE;
static char s_status[128] = "Escribe una URL y pulsa Enter";
static char s_url_norm[URL_NORM_MAX];

static char *s_link_url = NULL;   // MAX_LINKS * LINK_URL_MAX
static char *s_link_txt = NULL;   // MAX_LINKS * LINK_TEXT_MAX
static int   s_link_count = 0;
static int   s_active_link = -1;

// ---------------- Formularios GET simples (10D) ----------------
// Solo un formulario/campo activo por pagina. Es deliberadamente simple.
static char s_form_action[URL_NORM_MAX];
static char s_form_input[64];
static int  s_form_available = 0;
static int  s_form_active = 0;

static char s_current_url[URL_NORM_MAX] = "";
static char s_home_url[URL_NORM_MAX] = BROWSER_DEFAULT_HOME;
static char s_history[BROWSER_HISTORY_MAX][URL_NORM_MAX];
static int  s_history_count = 0;
static int  s_history_pos = -1;
static int  s_nav_suppress_history = 0;

// ---------------- Favoritos (persistentes en la SD) ----------------
#define BROWSER_FAV_MAX      32
#define BROWSER_FAV_FILE     "/sdcard/browser_favs.txt"
#define BROWSER_FAV_FILE_ALT "/root/browser_favs.txt"
static char s_fav[BROWSER_FAV_MAX][URL_NORM_MAX];
static int  s_fav_count = 0;
static int  s_fav_loaded = 0;

static int s_last_http_status = 0;
static size_t s_last_http_bytes = 0;
static char s_last_location[URL_NORM_MAX];


static char decode_entity(const char **pp);
static char decode_utf8_ascii(const char **pp);
static void browser_nav_record_success(const char *url);
static void browser_nav_reset_if_needed(void);

// 10C FIX2: prototipos necesarios porque las funciones REDIR se colocan
// antes que estas utilidades dentro del mismo fichero.
static char *link_url_at(int idx);
static void resolve_href(const char *href, char *out, size_t out_sz);
static char *bz_strcasestr(const char *haystack, const char *needle);

// 10D FIX4 FIX1:
// browser_extract_query_param() usa link_trim_inplace() antes de su definicion real.
// En C hace falta prototipo para evitar implicit declaration / conflicting types.
static void link_trim_inplace(char *s);
static void href_decode_ascii(char *dst, size_t dst_sz, const char *src);
static int browser_resolve_redirect_url(const char *loc, char *out, size_t out_sz);

static void browser_forms_reset(void);
static int browser_build_query_url(const char *query, char *out, size_t out_sz);
static void browser_form_begin(const char *tag_start, const char *tag_end);
static void browser_form_end(void);
static int browser_form_capture_input(const char *tag_start, const char *tag_end);

static void browser_percent_decode_ascii(char *dst, size_t dst_sz, const char *src);
static int browser_extract_query_param(const char *url, const char *key, char *out, size_t out_sz);
static int browser_unwrap_google_redirect(const char *url, char *out, size_t out_sz);
static int browser_unwrap_duckduckgo_redirect(const char *url, char *out, size_t out_sz);
static int browser_link_is_noise(const char *url);
static void browser_clean_href_for_display(const char *in, char *out, size_t out_sz);
static int browser_current_search_query(char *out, size_t out_sz);

static int browser_find_client_redirect(char *out, size_t out_sz, const char **kind_out);
static int browser_find_meta_refresh_redirect(char *out, size_t out_sz);
static int browser_find_js_redirect(char *out, size_t out_sz);
static int browser_extract_attr_lite(const char *tag_start, const char *tag_end,
                                     const char *attr, char *out, size_t out_sz);
static int browser_extract_quoted_url_near(const char *p, char *out, size_t out_sz);
static int browser_js_pattern_has_redirect_syntax(const char *p, const char *pattern);
static int browser_client_redirect_url_allowed(const char *url);
static int browser_client_redirect_page_allowed(void);

static void minipc_browser_load_internal(const char *url, int redir_depth);
static int browser_status_is_redirect(int status);
static int browser_resolve_redirect_url(const char *in, char *out, size_t out_sz);
static int browser_page_looks_like_redirect(void);
static int browser_first_redirect_link(char *out, size_t out_sz);

static void browser_status_downloading(const char *url, int downgraded)
{
    if (downgraded) {
        // 10A: queda solo por compatibilidad. El HTTPS ya pasa intacto.
        snprintf(s_status, sizeof(s_status), "HTTPS LAB...");
        return;
    }

    const char *prefix = "Descargando ";
    const char *suffix = " ...";
    size_t pos = 0;

    s_status[0] = 0;

    for (size_t i = 0; prefix[i] && pos < sizeof(s_status) - 1; i++) {
        s_status[pos++] = prefix[i];
    }

    if (url) {
        for (size_t i = 0; url[i] && pos < sizeof(s_status) - strlen(suffix) - 1; i++) {
            char ch = url[i];
            if ((unsigned char)ch < 32 || (unsigned char)ch > 126) ch = '?';
            s_status[pos++] = ch;
        }
    }

    for (size_t i = 0; suffix[i] && pos < sizeof(s_status) - 1; i++) {
        s_status[pos++] = suffix[i];
    }

    s_status[pos] = 0;
}


static int browser_extract_attr_lite(const char *tag_start, const char *tag_end,
                                     const char *attr, char *out, size_t out_sz)
{
    if (!tag_start || !tag_end || !attr || !out || out_sz == 0) return 0;
    out[0] = 0;

    int alen = (int)strlen(attr);
    const char *p = tag_start;

    while (p < tag_end) {
        if ((tag_end - p) >= alen && strncasecmp(p, attr, alen) == 0) {
            const char *q = p + alen;

            if (p > tag_start) {
                char prev = p[-1];
                if (isalnum((unsigned char)prev) || prev == '_' || prev == '-') {
                    p++;
                    continue;
                }
            }

            if (q < tag_end) {
                char next = *q;
                if (isalnum((unsigned char)next) || next == '_' || next == '-') {
                    p++;
                    continue;
                }
            }

            while (q < tag_end && isspace((unsigned char)*q)) q++;
            if (q >= tag_end || *q != '=') {
                p++;
                continue;
            }

            q++;
            while (q < tag_end && isspace((unsigned char)*q)) q++;

            char quote = 0;
            if (q < tag_end && (*q == '"' || *q == '\'')) quote = *q++;

            char raw[URL_NORM_MAX];
            size_t pos = 0;

            while (q < tag_end && pos < sizeof(raw) - 1) {
                if (quote) {
                    if (*q == quote) break;
                } else {
                    if (isspace((unsigned char)*q) || *q == '>') break;
                }
                raw[pos++] = *q++;
            }
            raw[pos] = 0;

            href_decode_ascii(out, out_sz, raw);
            link_trim_inplace(out);
            return out[0] != 0;
        }

        p++;
    }

    return 0;
}

static int browser_client_redirect_page_allowed(void)
{
    // FIX3:
    // Google y DuckDuckGo incluyen scripts internos, doodles, preferencias,
    // mediciones y rutas auxiliares. En esas paginas no conviene aplicar
    // JS redirect automatico porque se puede pescar una navegacion interna
    // que no representa la pagina que el usuario pidio.
    if (bz_strcasestr(s_url_norm, "google.")) {
        printf("[BROWSER_10E] client redirect guard: Google/DDG page, skip JS/meta auto\n");
        return 0;
    }

    if (bz_strcasestr(s_url_norm, "doodles.google")) {
        printf("[BROWSER_10E] client redirect guard: doodles.google, skip JS/meta auto\n");
        return 0;
    }

    if (bz_strcasestr(s_url_norm, "duckduckgo.com")) {
        printf("[BROWSER_10E] client redirect guard: DuckDuckGo page, skip JS/meta auto\n");
        return 0;
    }

    return 1;
}

static int browser_client_redirect_url_allowed(const char *url)
{
    if (!url || !url[0]) return 0;

    // Rechazar destinos que no son paginas navegables.
    if (url[0] == '#') return 0;
    if (strncasecmp(url, "javascript:", 11) == 0) return 0;
    if (strncasecmp(url, "mailto:", 7) == 0) return 0;
    if (strncasecmp(url, "tel:", 4) == 0) return 0;
    if (strncasecmp(url, "data:", 5) == 0) return 0;
    if (strncasecmp(url, "about:", 6) == 0) return 0;

    // Evitar redirecciones a recursos estaticos que suelen dar 404 si se
    // interpretan como pagina.
    const char *q = strchr(url, '?');
    size_t n = q ? (size_t)(q - url) : strlen(url);

    const char *exts[] = {
        ".css", ".js", ".mjs", ".json", ".xml",
        ".png", ".jpg", ".jpeg", ".gif", ".webp", ".svg", ".ico",
        ".woff", ".woff2", ".ttf", ".eot",
        ".mp4", ".webm", ".mp3", ".pdf", ".zip"
    };

    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        size_t el = strlen(exts[i]);
        if (n >= el && strncasecmp(url + n - el, exts[i], el) == 0) {
            return 0;
        }
    }

    return 1;
}

static int browser_js_pattern_has_redirect_syntax(const char *p, const char *pattern)
{
    if (!p || !pattern) return 0;

    const char *q = p + strlen(pattern);

    // location.replace("...") / location.assign("...")
    if (strstr(pattern, "replace") || strstr(pattern, "assign")) {
        while (*q && isspace((unsigned char)*q)) q++;
        return (*q == '(');
    }

    // window.location = "..."
    // location.href = "..."
    // document.location = "..."
    // top.location = "..."
    // self.location = "..."
    while (*q && isspace((unsigned char)*q)) q++;
    return (*q == '=');
}

static int browser_extract_quoted_url_near(const char *p, char *out, size_t out_sz)
{
    if (!p || !out || out_sz == 0) return 0;
    out[0] = 0;

    // Buscar una cadena entre comillas cerca del patrón JS.
    // FIX2: limite mas corto para no pescar texto ajeno dentro de scripts grandes.
    const char *limit = p + 120;

    while (*p && p < limit) {
        if (*p == '"' || *p == '\'') {
            char quote = *p++;
            char raw[URL_NORM_MAX];
            size_t pos = 0;

            while (*p && p < limit && *p != quote && pos < sizeof(raw) - 1) {
                raw[pos++] = *p++;
            }
            raw[pos] = 0;

            link_trim_inplace(raw);

            if (strncasecmp(raw, "http://", 7) == 0 ||
                strncasecmp(raw, "https://", 8) == 0 ||
                strncmp(raw, "//", 2) == 0 ||
                raw[0] == '/' ||
                strncmp(raw, "./", 2) == 0 ||
                strncmp(raw, "../", 3) == 0) {
                href_decode_ascii(out, out_sz, raw);
                link_trim_inplace(out);
                return out[0] != 0;
            }
        }
        p++;
    }

    return 0;
}

static int browser_find_meta_refresh_redirect(char *out, size_t out_sz)
{
    if (!s_raw || !out || out_sz == 0) return 0;
    out[0] = 0;

    const char *p = s_raw;

    while ((p = bz_strcasestr(p, "<meta")) != NULL) {
        const char *gt = strchr(p, '>');
        if (!gt) break;

        char http_equiv[80];
        char content[URL_NORM_MAX];

        http_equiv[0] = 0;
        content[0] = 0;

        browser_extract_attr_lite(p, gt, "http-equiv", http_equiv, sizeof(http_equiv));
        browser_extract_attr_lite(p, gt, "content", content, sizeof(content));

        if (content[0]) {
            const char *u = bz_strcasestr(content, "url");
            if (u) {
                const char *eq = strchr(u, '=');
                if (eq) {
                    eq++;

                    while (*eq && isspace((unsigned char)*eq)) eq++;

                    char raw[URL_NORM_MAX];
                    size_t pos = 0;

                    while (*eq && *eq != ';' && pos < sizeof(raw) - 1) {
                        raw[pos++] = *eq++;
                    }
                    raw[pos] = 0;

                    link_trim_inplace(raw);

                    // FIX2: solo aceptar refresh explicito.
                    // La 10E original era demasiado permisiva y podia pescar
                    // content="...url=..." que no fuese una redireccion real.
                    if (strcasecmp(http_equiv, "refresh") == 0 &&
                        browser_client_redirect_url_allowed(raw) &&
                        browser_resolve_redirect_url(raw, out, out_sz)) {
                        printf("[BROWSER_10E] META refresh SAFE -> %s\n", out);
                        return 1;
                    }
                }
            }
        }

        p = gt + 1;
    }

    return 0;
}

static int browser_find_js_redirect(char *out, size_t out_sz)
{
    if (!s_raw || !out || out_sz == 0) return 0;
    out[0] = 0;

    const char *patterns[] = {
        "window.location",
        "location.href",
        "document.location",
        "location.replace",
        "location.assign",
        "top.location",
        "self.location"
    };

    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++) {
        const char *p = s_raw;

        while ((p = bz_strcasestr(p, patterns[i])) != NULL) {
            char raw[URL_NORM_MAX];

            // FIX2: no basta con encontrar la palabra location.
            // Exigimos sintaxis clara de redireccion:
            //   location.href = "..."
            //   window.location = "..."
            //   location.replace("...")
            if (browser_js_pattern_has_redirect_syntax(p, patterns[i]) &&
                browser_extract_quoted_url_near(p, raw, sizeof(raw)) &&
                browser_client_redirect_url_allowed(raw) &&
                browser_resolve_redirect_url(raw, out, out_sz)) {
                printf("[BROWSER_10E] JS redirect SAFE %s -> %s\n", patterns[i], out);
                return 1;
            }

            p += strlen(patterns[i]);
        }
    }

    return 0;
}

static int browser_find_client_redirect(char *out, size_t out_sz, const char **kind_out)
{
    if (!out || out_sz == 0) return 0;
    out[0] = 0;

    // FIX3: no aplicar redirect cliente automatico en buscadores grandes.
    // La portada de Google puede contener rutas a doodles.google y otros
    // elementos internos que no deben tratarse como navegacion principal.
    if (!browser_client_redirect_page_allowed()) {
        return 0;
    }

    if (browser_find_meta_refresh_redirect(out, out_sz)) {
        if (kind_out) *kind_out = "META";
        return 1;
    }

    if (browser_find_js_redirect(out, out_sz)) {
        if (kind_out) *kind_out = "JS";
        return 1;
    }

    return 0;
}



static int browser_status_is_redirect(int status)
{
    return status == 301 || status == 302 || status == 303 ||
           status == 307 || status == 308;
}

static int browser_resolve_redirect_url(const char *in, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return 0;
    out[0] = 0;

    if (!in || !*in) return 0;

    resolve_href(in, out, out_sz);

    if (!out[0]) return 0;

    if (strcmp(out, s_url_norm) == 0) {
        printf("[BROWSER_10C] redir ignorada: misma URL\n");
        return 0;
    }

    if (strncasecmp(out, "http://", 7) != 0 &&
        strncasecmp(out, "https://", 8) != 0) {
        printf("[BROWSER_10C] redir ignorada: esquema no navegable %s\n", out);
        return 0;
    }

    return 1;
}

static int browser_page_looks_like_redirect(void)
{
    if (!s_raw || !s_raw[0]) return 0;

    if (bz_strcasestr(s_raw, "301 Moved")) return 1;
    if (bz_strcasestr(s_raw, "302 Found")) return 1;
    if (bz_strcasestr(s_raw, "303 See Other")) return 1;
    if (bz_strcasestr(s_raw, "307 Temporary Redirect")) return 1;
    if (bz_strcasestr(s_raw, "308 Permanent Redirect")) return 1;
    if (bz_strcasestr(s_raw, "Moved Permanently")) return 1;
    if (bz_strcasestr(s_raw, "The document has moved")) return 1;
    if (bz_strcasestr(s_raw, "This document has moved")) return 1;
    if (bz_strcasestr(s_raw, "has moved")) return 1;

    return 0;
}

static int browser_first_redirect_link(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return 0;
    out[0] = 0;

    for (int i = 0; i < s_link_count; i++) {
        const char *u = link_url_at(i);
        if (!u || !u[0]) continue;

        if (strncasecmp(u, "javascript:", 11) == 0) continue;
        if (strncasecmp(u, "mailto:", 7) == 0) continue;
        if (strncasecmp(u, "tel:", 4) == 0) continue;

        if (browser_resolve_redirect_url(u, out, out_sz)) return 1;
    }

    return 0;
}


static char *link_url_at(int idx)
{
    if (!s_link_url || idx < 0 || idx >= MAX_LINKS) return NULL;
    return s_link_url + (idx * LINK_URL_MAX);
}

static char *link_txt_at(int idx)
{
    if (!s_link_txt || idx < 0 || idx >= MAX_LINKS) return NULL;
    return s_link_txt + (idx * LINK_TEXT_MAX);
}

static void links_reset(void)
{
    s_link_count = 0;
    s_active_link = -1;

    if (s_link_url) memset(s_link_url, 0, MAX_LINKS * LINK_URL_MAX);
    if (s_link_txt) memset(s_link_txt, 0, MAX_LINKS * LINK_TEXT_MAX);
}

static void link_capture_char(char c)
{
    if (s_active_link < 0 || s_active_link >= s_link_count) return;

    char *txt = link_txt_at(s_active_link);
    if (!txt) return;

    int len = strlen(txt);
    if (len >= LINK_TEXT_MAX - 1) return;

    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';

    // Evitar espacios duplicados en el texto del enlace.
    if (c == ' ' && (len == 0 || txt[len - 1] == ' ')) return;

    txt[len] = c;
    txt[len + 1] = 0;
}

static void safe_copy_ascii(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0) return;
    dst[0] = 0;
    if (!src) return;

    size_t pos = 0;
    for (size_t i = 0; src[i] && pos < dst_sz - 1; i++) {
        char c = src[i];
        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
        dst[pos++] = c;
    }
    dst[pos] = 0;
}

static void href_decode_ascii(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0) return;
    dst[0] = 0;
    if (!src) return;

    size_t pos = 0;
    const char *p = src;

    while (*p && pos < dst_sz - 1) {
        char c;

        if (*p == '&') {
            const char *q = p;
            char dec = decode_entity(&q);
            if (dec) {
                c = dec;
                p = q;
            } else {
                c = *p++;
            }
        } else if (((unsigned char)*p) >= 0x80) {
            c = decode_utf8_ascii(&p);
        } else {
            c = *p++;
        }

        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
        dst[pos++] = c;
    }

    dst[pos] = 0;
}

static int browser_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static void browser_percent_decode_ascii(char *dst, size_t dst_sz, const char *src)
{
    if (!dst || dst_sz == 0) return;
    dst[0] = 0;
    if (!src) return;

    size_t pos = 0;

    for (size_t i = 0; src[i] && pos < dst_sz - 1; i++) {
        char c = src[i];

        if (c == '+') {
            c = ' ';
        } else if (c == '%' && src[i + 1] && src[i + 2]) {
            int h1 = browser_hexval(src[i + 1]);
            int h2 = browser_hexval(src[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                c = (char)((h1 << 4) | h2);
                i += 2;
            }
        }

        // Mantener texto sencillo ASCII. UTF-8 quedara simplificado por el parser.
        if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
        dst[pos++] = c;
    }

    dst[pos] = 0;
}

static int browser_extract_query_param(const char *url, const char *key, char *out, size_t out_sz)
{
    if (!url || !key || !out || out_sz == 0) return 0;
    out[0] = 0;

    size_t klen = strlen(key);
    const char *q = strchr(url, '?');
    if (!q) q = url;

    while (*q) {
        if (*q == '?' || *q == '&') q++;

        if (strncasecmp(q, key, klen) == 0 && q[klen] == '=') {
            const char *v = q + klen + 1;
            char raw[URL_NORM_MAX];
            size_t pos = 0;

            while (*v && *v != '&' && *v != '#' && pos < sizeof(raw) - 1) {
                raw[pos++] = *v++;
            }
            raw[pos] = 0;

            browser_percent_decode_ascii(out, out_sz, raw);
            link_trim_inplace(out);
            return out[0] != 0;
        }

        const char *amp = strchr(q, '&');
        if (!amp) break;
        q = amp;
    }

    return 0;
}

static int browser_unwrap_google_redirect(const char *url, char *out, size_t out_sz)
{
    if (!url || !out || out_sz == 0) return 0;
    out[0] = 0;

    if (!bz_strcasestr(url, "google.")) return 0;
    if (!bz_strcasestr(url, "/url?")) return 0;

    char q[URL_NORM_MAX];
    if (!browser_extract_query_param(url, "q", q, sizeof(q))) return 0;

    if (strncasecmp(q, "http://", 7) == 0 ||
        strncasecmp(q, "https://", 8) == 0) {
        safe_copy_ascii(out, out_sz, q);
        return out[0] != 0;
    }

    return 0;
}

static int browser_unwrap_duckduckgo_redirect(const char *url, char *out, size_t out_sz)
{
    if (!url || !out || out_sz == 0) return 0;
    out[0] = 0;

    if (!bz_strcasestr(url, "duckduckgo.com")) return 0;

    // DuckDuckGo HTML usa a menudo /l/?uddg=https%3A%2F%2F...
    char q[URL_NORM_MAX];
    if (!browser_extract_query_param(url, "uddg", q, sizeof(q))) return 0;

    if (strncasecmp(q, "http://", 7) == 0 ||
        strncasecmp(q, "https://", 8) == 0) {
        safe_copy_ascii(out, out_sz, q);
        return out[0] != 0;
    }

    return 0;
}

static int browser_link_is_noise(const char *url)
{
    if (!url || !*url) return 1;

    if (strncasecmp(url, "javascript:", 11) == 0) return 1;
    if (strncasecmp(url, "mailto:", 7) == 0) return 1;
    if (strncasecmp(url, "tel:", 4) == 0) return 1;
    if (url[0] == '#') return 1;

    // En paginas de busqueda Google, quitar parte del ruido de navegacion.
    if (bz_strcasestr(s_url_norm, "google.") && bz_strcasestr(s_url_norm, "/search")) {
        if (bz_strcasestr(url, "accounts.google.")) return 1;
        if (bz_strcasestr(url, "support.google.")) return 1;
        if (bz_strcasestr(url, "policies.google.")) return 1;
        if (bz_strcasestr(url, "privacy")) return 1;
        if (bz_strcasestr(url, "terms")) return 1;
        if (bz_strcasestr(url, "preferences")) return 1;
        if (bz_strcasestr(url, "advanced_search")) return 1;
        if (bz_strcasestr(url, "setprefs")) return 1;
        if (bz_strcasestr(url, "tbm=isch")) return 1;
        if (bz_strcasestr(url, "google.com/webhp")) return 1;
    }

    // Ruido tipico de DuckDuckGo HTML.
    if (bz_strcasestr(s_url_norm, "duckduckgo.com/html")) {
        if (bz_strcasestr(url, "duckduckgo.com/settings")) return 1;
        if (bz_strcasestr(url, "duckduckgo.com/feedback")) return 1;
        if (bz_strcasestr(url, "duckduckgo.com/params")) return 1;
        if (bz_strcasestr(url, "duckduckgo.com/privacy")) return 1;
        if (bz_strcasestr(url, "duckduckgo.com/duckduckgo-help-pages")) return 1;
    }

    return 0;
}

static void browser_clean_href_for_display(const char *in, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    out[0] = 0;

    if (!in || !*in) return;

    char unwrapped[URL_NORM_MAX];
    if (browser_unwrap_google_redirect(in, unwrapped, sizeof(unwrapped))) {
        safe_copy_ascii(out, out_sz, unwrapped);
        printf("[BROWSER_10D] GOOGLE URL unwrap: %s\n", out);
        return;
    }

    if (browser_unwrap_duckduckgo_redirect(in, unwrapped, sizeof(unwrapped))) {
        safe_copy_ascii(out, out_sz, unwrapped);
        printf("[BROWSER_10D] DDG URL unwrap: %s\n", out);
        return;
    }

    safe_copy_ascii(out, out_sz, in);
}

static int browser_current_search_query(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return 0;
    out[0] = 0;

    // Google: /search?q=...
    // DuckDuckGo HTML: /html/?q=...
    if (!(bz_strcasestr(s_url_norm, "/search") ||
          bz_strcasestr(s_url_norm, "duckduckgo.com/html"))) {
        return 0;
    }

    if (browser_extract_query_param(s_url_norm, "q", out, out_sz)) {
        return out[0] != 0;
    }

    return 0;
}

static int browser_url_base(char *scheme_host, size_t sh_sz, char *dir, size_t dir_sz)
{
    if (!s_url_norm[0]) return 0;

    const char *p = strstr(s_url_norm, "://");
    const char *after_host = NULL;

    if (p) after_host = strchr(p + 3, '/');
    else after_host = strchr(s_url_norm, '/');

    if (!after_host) {
        safe_copy_ascii(scheme_host, sh_sz, s_url_norm);
        safe_copy_ascii(dir, dir_sz, s_url_norm);
        return 1;
    }

    size_t host_len = after_host - s_url_norm;
    if (host_len >= sh_sz) host_len = sh_sz - 1;
    memcpy(scheme_host, s_url_norm, host_len);
    scheme_host[host_len] = 0;

    const char *last_slash = strrchr(s_url_norm, '/');
    if (!last_slash || last_slash < after_host) last_slash = after_host;

    size_t dir_len = (last_slash - s_url_norm) + 1;
    if (dir_len >= dir_sz) dir_len = dir_sz - 1;
    memcpy(dir, s_url_norm, dir_len);
    dir[dir_len] = 0;

    return 1;
}

static void resolve_href(const char *href, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    out[0] = 0;
    if (!href || !*href) return;

    while (*href == ' ' || *href == '\t' || *href == '\r' || *href == '\n') href++;

    // Ignorar esquemas que no vamos a navegar.
    if (strncasecmp(href, "javascript:", 11) == 0 ||
        strncasecmp(href, "mailto:", 7) == 0 ||
        strncasecmp(href, "tel:", 4) == 0) {
        safe_copy_ascii(out, out_sz, href);
        return;
    }

    if (strncasecmp(href, "http://", 7) == 0 ||
        strncasecmp(href, "https://", 8) == 0) {
        safe_copy_ascii(out, out_sz, href);
        return;
    }

    if (href[0] == '/' && href[1] == '/') {
        // 10A: si estamos en HTTPS, mantener HTTPS tambien en enlaces //host/ruta.
        const char *scheme = (strncasecmp(s_url_norm, "https://", 8) == 0) ? "https:" : "http:";
        snprintf(out, out_sz, "%s%s", scheme, href);
        return;
    }

    char scheme_host[URL_NORM_MAX];
    char base_dir[URL_NORM_MAX];
    scheme_host[0] = 0;
    base_dir[0] = 0;

    if (!browser_url_base(scheme_host, sizeof(scheme_host), base_dir, sizeof(base_dir))) {
        safe_copy_ascii(out, out_sz, href);
        return;
    }

    if (href[0] == '#') {
        snprintf(out, out_sz, "%s%s", s_url_norm, href);
    } else if (href[0] == '/') {
        snprintf(out, out_sz, "%s%s", scheme_host, href);
    } else {
        snprintf(out, out_sz, "%s%s", base_dir, href);
    }
}

static int extract_href(const char *tag_start, const char *tag_end, char *out, size_t out_sz)
{
    if (!tag_start || !tag_end || !out || out_sz == 0) return 0;
    out[0] = 0;

    const char *p = tag_start;
    while (p < tag_end) {
        if ((tag_end - p) >= 4 && strncasecmp(p, "href", 4) == 0) {
            const char *q = p + 4;
            while (q < tag_end && isspace((unsigned char)*q)) q++;
            if (q >= tag_end || *q != '=') {
                p++;
                continue;
            }

            q++;
            while (q < tag_end && isspace((unsigned char)*q)) q++;

            char quote = 0;
            if (q < tag_end && (*q == '"' || *q == '\'')) quote = *q++;

            char raw[LINK_URL_MAX];
            size_t pos = 0;

            while (q < tag_end && pos < sizeof(raw) - 1) {
                if (quote) {
                    if (*q == quote) break;
                } else {
                    if (isspace((unsigned char)*q) || *q == '>') break;
                }
                raw[pos++] = *q++;
            }
            raw[pos] = 0;

            char decoded[LINK_URL_MAX];
            href_decode_ascii(decoded, sizeof(decoded), raw);
            resolve_href(decoded, out, out_sz);
            return out[0] != 0;
        }
        p++;
    }

    return 0;
}

static int links_add(const char *href)
{
    if (!href || !*href) return -1;
    if (!s_link_url || !s_link_txt) return -1;
    if (s_link_count >= MAX_LINKS) return -1;

    char clean[LINK_URL_MAX];
    browser_clean_href_for_display(href, clean, sizeof(clean));

    if (browser_link_is_noise(clean)) {
        return -1;
    }

    int idx = s_link_count++;
    safe_copy_ascii(link_url_at(idx), LINK_URL_MAX, clean);

    char *txt = link_txt_at(idx);
    if (txt) txt[0] = 0;

    return idx;
}

static void emit_literal(char *out, int *out_len, const char *s,
                         int *last_was_space, int *newline_run, int *col);

static int extract_attr_ascii(const char *tag_start, const char *tag_end,
                              const char *attr, char *out, size_t out_sz);
static void browser_get_host_label(char *out, size_t out_sz);
static void emit_page_header(char *out, int *out_len,
                             int *last_was_space, int *newline_run, int *col);
static void emit_img_alt_if_any(const char *tag_start, const char *tag_end,
                                char *out, int *out_len,
                                int *last_was_space, int *newline_run, int *col);



// -----------------------------------------------------------------------------
// Utilidades
// -----------------------------------------------------------------------------

static char *bz_strcasestr(const char *haystack, const char *needle)
{
    size_t nl = strlen(needle);
    if (nl == 0) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, nl) == 0) return (char *)haystack;
    }
    return NULL;
}

static int browser_has_network(void)
{
    esp_netif_t *netif = esp_netif_get_default_netif();
    if (!netif) return 0;

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK) return 0;

    return ip.ip.addr != 0;
}

static void browser_url_encode_component(const char *in, char *out, size_t out_sz)
{
    static const char hex[] = "0123456789ABCDEF";

    if (!out || out_sz == 0) return;
    out[0] = 0;
    if (!in) return;

    size_t pos = 0;

    for (size_t i = 0; in[i] && pos < out_sz - 1; i++) {
        unsigned char c = (unsigned char)in[i];

        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[pos++] = (char)c;
        } else if (c == ' ') {
            out[pos++] = '+';
        } else {
            if (pos + 3 >= out_sz) break;
            out[pos++] = '%';
            out[pos++] = hex[(c >> 4) & 0x0F];
            out[pos++] = hex[c & 0x0F];
        }
    }

    out[pos] = 0;
}

static int browser_build_query_url(const char *query, char *out, size_t out_sz)
{
    if (!query || !*query || !out || out_sz < 32) return -1;

    while (*query == ' ' || *query == '\t') query++;
    if (!*query) return -1;

    char enc[192];
    browser_url_encode_component(query, enc, sizeof(enc));

    if (!enc[0]) return -1;

    if (s_form_available && s_form_action[0] && s_form_input[0]) {
        const char *sep = strchr(s_form_action, '?') ? "&" : "?";

        // FIX5:
        // Si el formulario detectado es Google Search, redirigimos la busqueda
        // al motor HTML ligero para evitar paginas intermedias de Google.
        if (bz_strcasestr(s_form_action, "google.") &&
            bz_strcasestr(s_form_action, "/search") &&
            strcmp(s_form_input, "q") == 0) {
            snprintf(out, out_sz, "https://html.duckduckgo.com/html/?q=%s", enc);
            printf("[BROWSER_10D] FORM GOOGLE -> SEARCH LITE: %s\n", out);
            return 0;
        }

        snprintf(out, out_sz, "%s%s%s=%s", s_form_action, sep, s_form_input, enc);

        printf("[BROWSER_10D] FORM GET: %s\n", out);
        return 0;
    }

    // FIX5:
    // Busqueda rapida por motor HTML ligero.
    // Google a veces devuelve paginas intermedias/avisos aun con gbv=1.
    // DuckDuckGo HTML es mas amable con navegadores sin JavaScript.
    snprintf(out, out_sz, "https://html.duckduckgo.com/html/?q=%s", enc);
    printf("[BROWSER_10D] QUICK SEARCH LITE: %s\n", out);
    return 0;
}

static int browser_prepare_url(const char *in, char *out, size_t out_sz, int *https_downgraded)
{
    if (https_downgraded) *https_downgraded = 0;
    if (!in || !out || out_sz < 16) return -1;

    while (*in == ' ' || *in == '\t' || *in == '\r' || *in == '\n') in++;
    if (!*in) return -1;

    char tmp[URL_NORM_MAX];
    size_t n = strnlen(in, sizeof(tmp) - 1);
    memcpy(tmp, in, n);
    tmp[n] = 0;

    while (n > 0 && (tmp[n-1] == ' ' || tmp[n-1] == '\t' || tmp[n-1] == '\r' || tmp[n-1] == '\n')) {
        tmp[--n] = 0;
    }
    if (n == 0) return -1;

    // 10D: busqueda/formulario desde barra URL.
    // Escribir ?texto usa el formulario GET detectado en la pagina actual.
    // Si no hay formulario, usa Google como buscador rapido.
    if (tmp[0] == '?' && tmp[1]) {
        return browser_build_query_url(tmp + 1, out, out_sz);
    }

    if ((tmp[0] == 'g' || tmp[0] == 'G') && tmp[1] == ' ' && tmp[2]) {
        return browser_build_query_url(tmp + 2, out, out_sz);
    }

    if (strncasecmp(tmp, "http://", 7) == 0) {
        snprintf(out, out_sz, "%s", tmp);
        return 0;
    }

    if (strncasecmp(tmp, "https://", 8) == 0) {
        // HTTPS ya no se rebaja a HTTP: camino despejado.
        if (https_downgraded) *https_downgraded = 0;
        snprintf(out, out_sz, "%s", tmp);
        return 0;
    }

    // Si tiene otro esquema raro, no lo aceptamos.
    if (strstr(tmp, "://")) return -2;

    // URL simple tipo example.com -> http://example.com
    snprintf(out, out_sz, "http://%s", tmp);
    return 0;
}


// -----------------------------------------------------------------------------
// Navegacion / historial RAM
// -----------------------------------------------------------------------------

static void browser_nav_reset_if_needed(void)
{
    if (!s_home_url[0]) {
        safe_copy_ascii(s_home_url, sizeof(s_home_url), BROWSER_DEFAULT_HOME);
    }
}

static void browser_nav_set_current(const char *url)
{
    if (!url || !*url) return;
    safe_copy_ascii(s_current_url, sizeof(s_current_url), url);
}

static void browser_history_push(const char *url)
{
    if (!url || !*url) return;

    if (s_history_pos >= 0 && s_history_pos < s_history_count &&
        strcmp(s_history[s_history_pos], url) == 0) {
        return;
    }

    // Si venimos de BACK y cargamos una URL nueva, cortamos el futuro.
    if (s_history_pos >= 0 && s_history_pos < s_history_count - 1) {
        s_history_count = s_history_pos + 1;
    }

    if (s_history_count < BROWSER_HISTORY_MAX) {
        s_history_pos = s_history_count;
        s_history_count++;
    } else {
        memmove(s_history[0], s_history[1], (BROWSER_HISTORY_MAX - 1) * URL_NORM_MAX);
        s_history_pos = BROWSER_HISTORY_MAX - 1;
    }

    safe_copy_ascii(s_history[s_history_pos], URL_NORM_MAX, url);
}

static void browser_nav_record_success(const char *url)
{
    browser_nav_reset_if_needed();
    browser_nav_set_current(url);

    if (!s_nav_suppress_history) {
        browser_history_push(url);
    }
}

static void browser_status_literal(const char *msg)
{
    safe_copy_ascii(s_status, sizeof(s_status), msg ? msg : "");
}

// -----------------------------------------------------------------------------
// Descarga HTTP
// -----------------------------------------------------------------------------

static esp_err_t browser_http_event(esp_http_client_event_t *evt)
{
    dl_ctx_t *ctx = (dl_ctx_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_HEADER &&
        ctx && ctx->location && ctx->location_sz > 0 &&
        evt->header_key && evt->header_value &&
        strcasecmp(evt->header_key, "Location") == 0) {
        safe_copy_ascii(ctx->location, ctx->location_sz, evt->header_value);
        printf("[BROWSER_10C] Location: %s\n", ctx->location);
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx && ctx->file && evt->data_len > 0) {
        size_t wr = fwrite(evt->data, 1, evt->data_len, ctx->file);
        ctx->total += wr;
    }
    return ESP_OK;
}

static int browser_download(const char *url, const char *dest_path)
{
    if (!url || !dest_path) return -1;

    s_last_http_status = 0;
    s_last_http_bytes = 0;
    s_last_location[0] = 0;

    if (!browser_has_network()) return -2;

    FILE *f = fopen(dest_path, "wb");
    if (!f) return -3;

    dl_ctx_t ctx = {
        .file = f,
        .total = 0,
        .location = s_last_location,
        .location_sz = sizeof(s_last_location),
    };

    int is_https = (strncasecmp(url, "https://", 8) == 0);

    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = browser_http_event,
        .user_data = &ctx,
        .timeout_ms = is_https ? 15000 : BROWSER_HTTP_TIMEOUT_MS,
        // FIX3:
        // Queremos ver/capturar nosotros los 301/302 y su Location.
        // Si el cliente redirige por dentro, a veces acabamos viendo solo error 302.
        .max_redirection_count = 0,
        .disable_auto_redirect = true,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,

        // 10A: HTTP sigue plano; HTTPS entra por SSL si sdkconfig lo permite.
        .transport_type = is_https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP,

        // Modo LAB: se apoya en sdkconfig para saltar verificacion si procede.
        .skip_cert_common_name_check = is_https ? true : false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        fclose(f);
        unlink(dest_path);
        return -4;
    }

    esp_http_client_set_header(client, "User-Agent", "ArieloMiniPC/1.0");
    esp_http_client_set_header(client, "Accept", "text/html,text/plain,*/*");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);

    if (err != ESP_OK) {
        // BROWSER_10E_DIAG: fallo aleatorio de descarga.
        // Registramos causa real + estado de heap para poder cazar el patron
        // (DNS, timeout, fragmentacion de memoria, etc.) en el siguiente fallo.
        printf("[BROWSER_10E_DIAG] fallo descarga: %s (err=0x%x) heap8bit=%u heapspiram=%u https=%d url=%s\n",
               esp_err_to_name(err),
               (unsigned)err,
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
               is_https,
               url);
    }

    esp_http_client_cleanup(client);
    fclose(f);

    s_last_http_status = status;
    s_last_http_bytes = ctx.total;

    if (browser_status_is_redirect(status)) {
        printf("[BROWSER_10D] HTTP REDIR status=%d location=%s bytes=%u\n",
               status,
               s_last_location[0] ? s_last_location : "(sin Location)",
               (unsigned)ctx.total);
    }

    if (err != ESP_OK) {
        unlink(dest_path);
        return -5;
    }

    // 10C: si el servidor entrega Location en 3xx, lo seguimos en minipc_browser_load_internal().
    if (browser_status_is_redirect(status) && s_last_location[0]) {
        unlink(dest_path);
        return BROWSER_DOWNLOAD_REDIRECT;
    }

    // status 0 se mantiene por compatibilidad con algunos servidores/transportes.
    // 3xx sin Location, pero con HTML, se deja pasar para detectar "document has moved".
    if (!(status == 0 || (status >= 200 && status < 300) || browser_status_is_redirect(status))) {
        unlink(dest_path);
        return -5;
    }

    if (ctx.total == 0) {
        unlink(dest_path);
        return -6;
    }

    return 0;
}

// -----------------------------------------------------------------------------
// Inicializacion
// -----------------------------------------------------------------------------

void minipc_browser_init(void)
{
    if (!s_raw)      s_raw  = heap_caps_malloc(RAW_MAX,  MALLOC_CAP_SPIRAM);
    if (!s_text)     s_text = heap_caps_malloc(TEXT_MAX, MALLOC_CAP_SPIRAM);
    if (!s_line_off) s_line_off = heap_caps_malloc(MAX_LINES * sizeof(int), MALLOC_CAP_SPIRAM);
    if (!s_link_url) s_link_url = heap_caps_malloc(MAX_LINKS * LINK_URL_MAX, MALLOC_CAP_SPIRAM);
    if (!s_link_txt) s_link_txt = heap_caps_malloc(MAX_LINKS * LINK_TEXT_MAX, MALLOC_CAP_SPIRAM);

    if (!s_raw || !s_text || !s_line_off || !s_link_url || !s_link_txt) {
        ESP_LOGE(TAG, "Sin memoria para buffers del navegador");
        s_state = BROWSER_ERROR;
        snprintf(s_status, sizeof(s_status), "Sin memoria navegador");
        return;
    }

    s_raw[0] = 0;
    s_text[0] = 0;
    s_text_len = 0;
    s_line_count = 0;
    links_reset();
    links_reset();
    browser_nav_reset_if_needed();
    s_state = BROWSER_IDLE;
    snprintf(s_status, sizeof(s_status), "Escribe una URL y pulsa Enter");
}

// -----------------------------------------------------------------------------
// Decodificacion HTML / UTF-8 basico
// -----------------------------------------------------------------------------

static char decode_entity(const char **pp)
{
    const char *p = *pp;
    const char *semi = strchr(p, ';');
    if (!semi || (semi - p) > 12) return 0;

    int len = semi - p - 1;
    const char *name = p + 1;

    struct { const char *n; char c; } table[] = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'},
        {"apos", '\''}, {"nbsp", ' '}, {"middot", '*'}, {"hellip", '.'},
        {"mdash", '-'}, {"ndash", '-'}, {"copy", 'c'}, {"reg", 'r'},
        {"trade", 't'}, {"deg", 'o'}, {"euro", 'E'},

        // Espanol / latin basico como ASCII legible
        {"aacute", 'a'}, {"eacute", 'e'}, {"iacute", 'i'}, {"oacute", 'o'}, {"uacute", 'u'},
        {"Aacute", 'A'}, {"Eacute", 'E'}, {"Iacute", 'I'}, {"Oacute", 'O'}, {"Uacute", 'U'},
        {"agrave", 'a'}, {"egrave", 'e'}, {"igrave", 'i'}, {"ograve", 'o'}, {"ugrave", 'u'},
        {"Agrave", 'A'}, {"Egrave", 'E'}, {"Igrave", 'I'}, {"Ograve", 'O'}, {"Ugrave", 'U'},
        {"auml", 'a'}, {"euml", 'e'}, {"iuml", 'i'}, {"ouml", 'o'}, {"uuml", 'u'},
        {"Auml", 'A'}, {"Euml", 'E'}, {"Iuml", 'I'}, {"Ouml", 'O'}, {"Uuml", 'U'},
        {"ntilde", 'n'}, {"Ntilde", 'N'}, {"ccedil", 'c'}, {"Ccedil", 'C'},
        {"iquest", '?'}, {"iexcl", '!'}, {"laquo", '"'}, {"raquo", '"'},
    };

    for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if ((int)strlen(table[i].n) == len && strncmp(name, table[i].n, len) == 0) {
            *pp = semi + 1;
            return table[i].c;
        }
    }

    if (name[0] == '#') {
        int code;
        if (name[1] == 'x' || name[1] == 'X') code = (int)strtol(name + 2, NULL, 16);
        else code = atoi(name + 1);

        *pp = semi + 1;

        if (code >= 32 && code < 127) return (char)code;
        if (code == 160) return ' ';

        switch (code) {
            case 161: return '!';
            case 191: return '?';
            case 193: case 192: case 196: case 195: return 'A';
            case 201: case 200: case 203: return 'E';
            case 205: case 204: case 207: return 'I';
            case 211: case 210: case 214: case 213: return 'O';
            case 218: case 217: case 220: return 'U';
            case 209: return 'N';
            case 225: case 224: case 228: case 227: return 'a';
            case 233: case 232: case 235: return 'e';
            case 237: case 236: case 239: return 'i';
            case 243: case 242: case 246: case 245: return 'o';
            case 250: case 249: case 252: return 'u';
            case 241: return 'n';
            case 8211: case 8212: return '-';
            case 8216: case 8217: return '\'';
            case 8220: case 8221: return '"';
            case 8230: return '.';
            default: return '?';
        }
    }

    return 0;
}

static char decode_latin1_cp1252_ascii(unsigned char b)
{
    // ISO-8859-1 / Windows-1252 como ASCII legible para el visor actual.
    // El visor/font actual no garantiza glifos acentuados, así que se translitera.
    switch (b) {
        // Windows-1252
        case 0x80: return 'E';   // euro
        case 0x82: return ',';
        case 0x84: return '"';
        case 0x85: return '.';
        case 0x91: case 0x92: return '\'';
        case 0x93: case 0x94: return '"';
        case 0x96: case 0x97: return '-';
        case 0xA0: return ' ';
        case 0xA1: return '!';
        case 0xA9: return 'c';
        case 0xAA: return 'a';
        case 0xAB: return '"';
        case 0xAE: return 'r';
        case 0xB0: return 'o';
        case 0xB2: return '2';
        case 0xB3: return '3';
        case 0xB7: return '*';
        case 0xB9: return '1';
        case 0xBA: return 'o';
        case 0xBB: return '"';
        case 0xBF: return '?';

        // Mayusculas acentuadas ISO-8859-1
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5: return 'A';
        case 0xC7: return 'C';
        case 0xC8: case 0xC9: case 0xCA: case 0xCB: return 'E';
        case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'I';
        case 0xD1: return 'N';
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 'O';
        case 0xD9: case 0xDA: case 0xDB: case 0xDC: return 'U';

        // Minusculas acentuadas ISO-8859-1
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
        case 0xE7: return 'c';
        case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
        case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
        case 0xF1: return 'n';
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
        case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
        case 0xFD: case 0xFF: return 'y';

        default:
            return '?';
    }
}

static char decode_utf8_ascii(const char **pp)
{
    const unsigned char *p = (const unsigned char *)(*pp);

    if (p[0] < 0x80) return 0;

    if (p[0] == 0xC2) {
        *pp += 2;
        if (p[1] == 0xA0) return ' ';
        if (p[1] == 0xA1) return '!';
        if (p[1] == 0xBF) return '?';
        if (p[1] == 0xB7) return '*';
        if (p[1] == 0xA9) return 'c';
        if (p[1] == 0xAE) return 'r';
        if (p[1] == 0xB0) return 'o';
        return '?';
    }

    if (p[0] == 0xC3) {
        *pp += 2;
        switch (p[1]) {
            case 0x81: case 0x80: case 0x84: case 0x83: return 'A';
            case 0x89: case 0x88: case 0x8B: return 'E';
            case 0x8D: case 0x8C: case 0x8F: return 'I';
            case 0x93: case 0x92: case 0x96: case 0x95: return 'O';
            case 0x9A: case 0x99: case 0x9C: return 'U';
            case 0x91: return 'N';
            case 0xA1: case 0xA0: case 0xA4: case 0xA3: return 'a';
            case 0xA9: case 0xA8: case 0xAB: return 'e';
            case 0xAD: case 0xAC: case 0xAF: return 'i';
            case 0xB3: case 0xB2: case 0xB6: case 0xB5: return 'o';
            case 0xBA: case 0xB9: case 0xBC: return 'u';
            case 0xB1: return 'n';
            case 0x87: return 'C';
            case 0xA7: return 'c';
            default: return '?';
        }
    }

    if (p[0] == 0xE2 && p[1] == 0x80) {
        *pp += 3;
        switch (p[2]) {
            case 0x93: case 0x94: return '-';
            case 0x98: case 0x99: return '\'';
            case 0x9C: case 0x9D: return '"';
            case 0xA6: return '.';
            default: return '?';
        }
    }

    if (p[0] == 0xE2 && p[1] == 0x82 && p[2] == 0xAC) {
        *pp += 3;
        return 'E';
    }

    // Algunas paginas HTTP antiguas llegan como ISO-8859-1 / Windows-1252.
    // Si no era una secuencia UTF-8 reconocida, interpretamos el byte suelto.
    {
        char latin = decode_latin1_cp1252_ascii(p[0]);
        (*pp)++;
        return latin;
    }
}

static int tag_is_block(const char *tag, int len)
{
    static const char *blocks[] = {
        "p","br","div","h1","h2","h3","h4","h5","h6",
        "li","tr","ul","ol","table","section","article",
        "header","footer","title","hr","blockquote","pre",
        "main","nav","aside","figure","figcaption","address","dl","dt","dd",
    };

    for (size_t i = 0; i < sizeof(blocks)/sizeof(blocks[0]); i++) {
        int bl = strlen(blocks[i]);
        if (bl == len && strncasecmp(tag, blocks[i], len) == 0) return 1;
    }
    return 0;
}

static void emit_newline(char *out, int *out_len, int *last_was_space, int *newline_run, int *col)
{
    if (*out_len <= 0) return;

    while (*out_len > 0 && out[*out_len - 1] == ' ') {
        (*out_len)--;
    }

    if (*out_len <= 0) return;
    if (out[*out_len - 1] == '\n') {
        *last_was_space = 1;
        *col = 0;
        return;
    }

    if (*newline_run < 2 && *out_len < TEXT_MAX - 2) {
        out[(*out_len)++] = '\n';
        (*newline_run)++;
        *last_was_space = 1;
        *col = 0;
    }
}

static void emit_char_wrapped(char *out, int *out_len, char c,
                              int *last_was_space, int *newline_run, int *col)
{
    if (*out_len >= TEXT_MAX - 2) return;

    if (c == '\r' || c == '\n' || c == '\t') c = ' ';

    if (c == ' ') {
        if (*last_was_space) return;
        if (*col >= WRAP_COL) {
            emit_newline(out, out_len, last_was_space, newline_run, col);
            return;
        }
        out[(*out_len)++] = ' ';
        link_capture_char(' ');
        *last_was_space = 1;
        (*col)++;
        return;
    }

    if (*col >= WRAP_COL) {
        emit_newline(out, out_len, last_was_space, newline_run, col);
    }

    out[(*out_len)++] = c;
    link_capture_char(c);
    *last_was_space = 0;
    *newline_run = 0;
    (*col)++;
}

static void emit_literal(char *out, int *out_len, const char *s,
                         int *last_was_space, int *newline_run, int *col)
{
    int saved_link = s_active_link;
    s_active_link = -1;

    while (s && *s) {
        emit_char_wrapped(out, out_len, *s++, last_was_space, newline_run, col);
    }

    s_active_link = saved_link;
}


static int tag_name_eq(const char *name, int nlen, const char *s)
{
    int sl = (int)strlen(s);
    return (nlen == sl && strncasecmp(name, s, sl) == 0);
}

static int tag_is_heading(const char *name, int nlen)
{
    return (nlen == 2 &&
            (name[0] == 'h' || name[0] == 'H') &&
            name[1] >= '1' && name[1] <= '6');
}

static int tag_is_table_cell(const char *name, int nlen)
{
    return tag_name_eq(name, nlen, "td") || tag_name_eq(name, nlen, "th");
}

static int tag_is_noise_block_open(const char *src, const char **end_out)
{
    struct { const char *open; const char *close; int close_len; } noise[] = {
        {"<script", "</script>", 9},
        {"<style",  "</style>",  8},
        {"<noscript", "</noscript>", 11},
        {"<svg", "</svg>", 6},
        {"<canvas", "</canvas>", 9},
    };

    for (size_t i = 0; i < sizeof(noise)/sizeof(noise[0]); i++) {
        int ol = (int)strlen(noise[i].open);
        if (strncasecmp(src, noise[i].open, ol) == 0) {
            const char *end = bz_strcasestr(src, noise[i].close);
            *end_out = end ? end + noise[i].close_len : src + strlen(src);
            return 1;
        }
    }

    return 0;
}

static void link_trim_inplace(char *s)
{
    if (!s) return;

    int len = (int)strlen(s);
    int start = 0;

    while (start < len && s[start] == ' ') start++;
    while (len > start && s[len - 1] == ' ') s[--len] = 0;

    if (start > 0) memmove(s, s + start, (size_t)(len - start + 1));
}

static void browser_finalize_link_texts(void)
{
    for (int i = 0; i < s_link_count; i++) {
        char *txt = link_txt_at(i);
        if (!txt) continue;
        link_trim_inplace(txt);

        if (!txt[0]) {
            safe_copy_ascii(txt, LINK_TEXT_MAX, link_url_at(i));
        }
    }
}

static void emit_section_break(char *out, int *out_len,
                               int *last_was_space, int *newline_run, int *col)
{
    emit_newline(out, out_len, last_was_space, newline_run, col);
    emit_newline(out, out_len, last_was_space, newline_run, col);
}


static void emit_soft_rule(char *out, int *out_len,
                           int *last_was_space, int *newline_run, int *col)
{
    emit_literal(out, out_len, "--------------------------------", last_was_space, newline_run, col);
}

static void browser_get_host_label(char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return;
    out[0] = 0;

    const char *p = strstr(s_url_norm, "://");
    p = p ? p + 3 : s_url_norm;

    if (!p || !*p) {
        safe_copy_ascii(out, out_sz, "sin-url");
        return;
    }

    const char *end = p;
    while (*end && *end != '/' && *end != '?' && *end != '#') end++;

    size_t n = (size_t)(end - p);
    if (n >= out_sz) n = out_sz - 1;

    memcpy(out, p, n);
    out[n] = 0;
}

static void emit_page_header(char *out, int *out_len,
                             int *last_was_space, int *newline_run, int *col)
{
    char host[96];
    browser_get_host_label(host, sizeof(host));

    emit_literal(out, out_len, "WEB: ", last_was_space, newline_run, col);
    emit_literal(out, out_len, host, last_was_space, newline_run, col);
    emit_newline(out, out_len, last_was_space, newline_run, col);

    char q[128];
    if (browser_current_search_query(q, sizeof(q))) {
        emit_literal(out, out_len, "BUSQUEDA: ", last_was_space, newline_run, col);
        emit_literal(out, out_len, q, last_was_space, newline_run, col);
        emit_newline(out, out_len, last_was_space, newline_run, col);
    }

    emit_soft_rule(out, out_len, last_was_space, newline_run, col);
    emit_section_break(out, out_len, last_was_space, newline_run, col);
}

static int extract_attr_ascii(const char *tag_start, const char *tag_end,
                              const char *attr, char *out, size_t out_sz)
{
    if (!tag_start || !tag_end || !attr || !out || out_sz == 0) return 0;
    out[0] = 0;

    int alen = (int)strlen(attr);
    const char *p = tag_start;

    while (p < tag_end) {
        if ((tag_end - p) >= alen && strncasecmp(p, attr, alen) == 0) {
            const char *q = p + alen;

            // Asegurar que es nombre de atributo completo, no prefijo.
            if (p > tag_start) {
                char prev = p[-1];
                if (isalnum((unsigned char)prev) || prev == '_' || prev == '-') {
                    p++;
                    continue;
                }
            }

            if (q < tag_end) {
                char next = *q;
                if (isalnum((unsigned char)next) || next == '_' || next == '-') {
                    p++;
                    continue;
                }
            }

            while (q < tag_end && isspace((unsigned char)*q)) q++;
            if (q >= tag_end || *q != '=') {
                p++;
                continue;
            }

            q++;
            while (q < tag_end && isspace((unsigned char)*q)) q++;

            char quote = 0;
            if (q < tag_end && (*q == '"' || *q == '\'')) quote = *q++;

            char raw[160];
            size_t pos = 0;

            while (q < tag_end && pos < sizeof(raw) - 1) {
                if (quote) {
                    if (*q == quote) break;
                } else {
                    if (isspace((unsigned char)*q) || *q == '>') break;
                }
                raw[pos++] = *q++;
            }
            raw[pos] = 0;

            href_decode_ascii(out, out_sz, raw);
            link_trim_inplace(out);
            return out[0] != 0;
        }
        p++;
    }

    return 0;
}

static void emit_img_alt_if_any(const char *tag_start, const char *tag_end,
                                char *out, int *out_len,
                                int *last_was_space, int *newline_run, int *col)
{
    char txt[128];

    if (!extract_attr_ascii(tag_start, tag_end, "alt", txt, sizeof(txt))) {
        if (!extract_attr_ascii(tag_start, tag_end, "title", txt, sizeof(txt))) {
            return;
        }
    }

    if (!txt[0]) return;

    emit_literal(out, out_len, " [IMG: ", last_was_space, newline_run, col);
    emit_literal(out, out_len, txt, last_was_space, newline_run, col);
    emit_literal(out, out_len, "] ", last_was_space, newline_run, col);
}


static void browser_forms_reset(void)
{
    s_form_action[0] = 0;
    s_form_input[0] = 0;
    s_form_available = 0;
    s_form_active = 0;
}

static void browser_form_begin(const char *tag_start, const char *tag_end)
{
    char method[24];

    // Si no hay method, HTML asume GET. POST se ignora en 10D.
    if (extract_attr_ascii(tag_start, tag_end, "method", method, sizeof(method))) {
        if (strcasecmp(method, "get") != 0) {
            s_form_active = 0;
            return;
        }
    }

    char action_raw[URL_NORM_MAX];
    char action_abs[URL_NORM_MAX];

    if (extract_attr_ascii(tag_start, tag_end, "action", action_raw, sizeof(action_raw))) {
        resolve_href(action_raw, action_abs, sizeof(action_abs));
    } else {
        safe_copy_ascii(action_abs, sizeof(action_abs), s_url_norm);
    }

    if (!action_abs[0]) {
        s_form_active = 0;
        return;
    }

    // Guardamos accion aunque aun falte encontrar input.
    safe_copy_ascii(s_form_action, sizeof(s_form_action), action_abs);
    s_form_active = 1;

    printf("[BROWSER_10D] FORM begin action=%s\n", s_form_action);
}

static void browser_form_end(void)
{
    s_form_active = 0;
}

static int browser_form_capture_input(const char *tag_start, const char *tag_end)
{
    if (!s_form_active || s_form_available) return 0;

    char type[32];
    char name[64];

    type[0] = 0;
    name[0] = 0;

    extract_attr_ascii(tag_start, tag_end, "type", type, sizeof(type));

    // Aceptar text/search o input sin type. Ignorar submit/hidden/password/etc.
    if (type[0] &&
        strcasecmp(type, "text") != 0 &&
        strcasecmp(type, "search") != 0 &&
        strcasecmp(type, "email") != 0) {
        return 0;
    }

    if (!extract_attr_ascii(tag_start, tag_end, "name", name, sizeof(name))) {
        return 0;
    }

    if (!name[0] || !s_form_action[0]) return 0;

    safe_copy_ascii(s_form_input, sizeof(s_form_input), name);
    s_form_available = 1;

    printf("[BROWSER_10D] FORM input name=%s action=%s\n",
           s_form_input, s_form_action);

    return 1;
}


// -----------------------------------------------------------------------------
// Parser HTML -> texto
// -----------------------------------------------------------------------------

static void parse_html_to_text(void)
{
    const char *src = s_raw;
    char *out = s_text;
    int out_len = 0;
    int last_was_space = 1;
    int newline_run = 0;
    int col = 0;

    links_reset();
    browser_forms_reset();

    // 10B: cabecera visual fija para orientar al usuario.
    emit_page_header(out, &out_len, &last_was_space, &newline_run, &col);

    while (*src && out_len < TEXT_MAX - 2) {

        const char *noise_end = NULL;
        if (tag_is_noise_block_open(src, &noise_end)) {
            src = noise_end;
            continue;
        }

        if (strncmp(src, "<!--", 4) == 0) {
            const char *end = strstr(src, "-->");
            src = end ? end + 3 : src + strlen(src);
            continue;
        }

        if (*src == '<') {
            const char *t = src + 1;
            int closing = 0;

            if (*t == '/') {
                closing = 1;
                t++;
            }

            const char *name = t;
            int nlen = 0;
            while (t[nlen] && isalnum((unsigned char)t[nlen])) nlen++;

            const char *gt = strchr(src, '>');
            if (!gt) {
                src += strlen(src);
                continue;
            }

            int is_block = tag_is_block(name, nlen);
            int is_a = tag_name_eq(name, nlen, "a");
            int is_li = tag_name_eq(name, nlen, "li");
            int is_title = tag_name_eq(name, nlen, "title");
            int is_h = tag_is_heading(name, nlen);
            int is_td = tag_is_table_cell(name, nlen);
            int is_tr = tag_name_eq(name, nlen, "tr");
            int is_table = tag_name_eq(name, nlen, "table");
            int is_img = tag_name_eq(name, nlen, "img");
            int is_form = tag_name_eq(name, nlen, "form");
            int is_input = tag_name_eq(name, nlen, "input");
            int is_hr = tag_name_eq(name, nlen, "hr");
            int is_p = tag_name_eq(name, nlen, "p");
            int is_br = tag_name_eq(name, nlen, "br");
            int is_sectionish = tag_name_eq(name, nlen, "section") ||
                                tag_name_eq(name, nlen, "article") ||
                                tag_name_eq(name, nlen, "ul") ||
                                tag_name_eq(name, nlen, "ol");

            if (!closing && is_a) {
                char href[LINK_URL_MAX];
                if (extract_href(src, gt, href, sizeof(href))) {
                    int idx = links_add(href);
                    if (idx >= 0) {
                        char mark[18];
                        snprintf(mark, sizeof(mark), "[%02d] ", idx + 1);
                        emit_literal(out, &out_len, mark, &last_was_space, &newline_run, &col);
                        s_active_link = idx;
                    }
                }
            } else if (closing && is_a) {
                int idx = s_active_link;
                if (idx >= 0 && idx < s_link_count) {
                    char *txt = link_txt_at(idx);
                    link_trim_inplace(txt);
                    if (!txt || !txt[0]) {
                        const char *lu = link_url_at(idx);
                        emit_literal(out, &out_len, lu, &last_was_space, &newline_run, &col);
                        safe_copy_ascii(txt, LINK_TEXT_MAX, lu);
                    }
                }
                s_active_link = -1;
            } else if (!closing && is_li) {
                emit_newline(out, &out_len, &last_was_space, &newline_run, &col);
                emit_literal(out, &out_len, "- ", &last_was_space, &newline_run, &col);
            } else if (!closing && is_title) {
                emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
                emit_literal(out, &out_len, "TITULO: ", &last_was_space, &newline_run, &col);
            } else if (!closing && is_h) {
                emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
                emit_literal(out, &out_len, "== ", &last_was_space, &newline_run, &col);
            } else if (closing && is_title) {
                emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
            } else if (closing && is_h) {
                emit_literal(out, &out_len, " ==", &last_was_space, &newline_run, &col);
                emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
            } else if (!closing && is_form) {
                browser_form_begin(src, gt);
            } else if (closing && is_form) {
                browser_form_end();
            } else if (!closing && is_input) {
                if (browser_form_capture_input(src, gt)) {
                    emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
                    emit_literal(out, &out_len, "[FORM] BUSCAR: toca aqui o Ctrl+F",
                                 &last_was_space, &newline_run, &col);
                    emit_newline(out, &out_len, &last_was_space, &newline_run, &col);
                    emit_literal(out, &out_len, "       escribe texto y pulsa Enter",
                                 &last_was_space, &newline_run, &col);
                    emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
                }
            } else if (!closing && is_hr) {
                emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
                emit_soft_rule(out, &out_len, &last_was_space, &newline_run, &col);
                emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
            } else if (!closing && is_table) {
                emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
            } else if (!closing && is_tr) {
                emit_newline(out, &out_len, &last_was_space, &newline_run, &col);
            } else if (closing && is_tr) {
                emit_newline(out, &out_len, &last_was_space, &newline_run, &col);
            } else if (!closing && is_img) {
                emit_img_alt_if_any(src, gt, out, &out_len, &last_was_space, &newline_run, &col);
            } else if (!closing && is_td) {
                if (!last_was_space && col > 0) {
                    emit_literal(out, &out_len, " | ", &last_was_space, &newline_run, &col);
                }
            } else if (!closing && is_sectionish) {
                emit_section_break(out, &out_len, &last_was_space, &newline_run, &col);
            } else if (!closing && (is_p || is_br)) {
                emit_newline(out, &out_len, &last_was_space, &newline_run, &col);
            } else if (is_block) {
                emit_newline(out, &out_len, &last_was_space, &newline_run, &col);
            }

            src = gt + 1;
            continue;
        }

        char c;

        if (*src == '&') {
            const char *p = src;
            char dec = decode_entity(&p);
            if (dec) {
                c = dec;
                src = p;
            } else {
                c = '&';
                src++;
            }
        } else if (((unsigned char)*src) >= 0x80) {
            c = decode_utf8_ascii(&src);
        } else {
            c = *src++;
        }

        emit_char_wrapped(out, &out_len, c, &last_was_space, &newline_run, &col);
    }

    while (out_len > 0 && (out[out_len - 1] == ' ' || out[out_len - 1] == '\n')) {
        out_len--;
    }

    browser_finalize_link_texts();

    out[out_len] = '\0';
    s_text_len = out_len;

    s_line_count = 0;

    if (out_len > 0) {
        s_line_off[s_line_count++] = 0;
        for (int i = 0; i < out_len && s_line_count < MAX_LINES; i++) {
            if (s_text[i] == '\n') {
                s_text[i] = '\0';
                if (i + 1 < out_len) s_line_off[s_line_count++] = i + 1;
            }
        }
    }
}

// -----------------------------------------------------------------------------
// API publica
// -----------------------------------------------------------------------------

void minipc_browser_set_loading(const char *url)
{
    int downgraded = 0;
    int rc = browser_prepare_url(url, s_url_norm, sizeof(s_url_norm), &downgraded);

    s_state = BROWSER_LOADING;

    if (rc == 0) {
        browser_status_downloading(s_url_norm, downgraded);
    } else if (rc == -2) {
        snprintf(s_status, sizeof(s_status), "Esquema no soportado. Usa http://");
    } else {
        snprintf(s_status, sizeof(s_status), "URL vacia");
    }
}

static void minipc_browser_load_internal(const char *url, int redir_depth)
{
    if (!s_raw || !s_text || !s_line_off || !s_link_url || !s_link_txt) {
        s_state = BROWSER_ERROR;
        snprintf(s_status, sizeof(s_status), "Sin memoria");
        return;
    }

    s_raw[0] = 0;
    s_text[0] = 0;
    s_text_len = 0;
    s_line_count = 0;

    int downgraded = 0;
    int prep = browser_prepare_url(url, s_url_norm, sizeof(s_url_norm), &downgraded);
    if (prep != 0) {
        s_state = BROWSER_ERROR;
        if (prep == -2) snprintf(s_status, sizeof(s_status), "Esquema no soportado. Usa http://");
        else snprintf(s_status, sizeof(s_status), "URL vacia");
        return;
    }

    s_state = BROWSER_LOADING;
    browser_status_downloading(s_url_norm, downgraded);

    ESP_LOGI(TAG, "GET %s", s_url_norm);

    const char *tmp = BROWSER_TMP_PATH;
    int rc = browser_download(s_url_norm, tmp);

    if (rc == BROWSER_DOWNLOAD_REDIRECT) {
        char redir[URL_NORM_MAX];
        if (redir_depth < BROWSER_REDIRECT_MAX &&
            browser_resolve_redirect_url(s_last_location, redir, sizeof(redir))) {
            snprintf(s_status, sizeof(s_status), "REDIR %d/%d...",
                     redir_depth + 1, BROWSER_REDIRECT_MAX);
            printf("[BROWSER_10C] REDIR header %d/%d: %s -> %s\n",
                   redir_depth + 1, BROWSER_REDIRECT_MAX, s_url_norm, redir);
            minipc_browser_load_internal(redir, redir_depth + 1);
            return;
        }
        rc = -5;
    }

    if (rc != 0) {
        tmp = BROWSER_TMP_ALT;
        rc = browser_download(s_url_norm, tmp);

        if (rc == BROWSER_DOWNLOAD_REDIRECT) {
            char redir[URL_NORM_MAX];
            if (redir_depth < BROWSER_REDIRECT_MAX &&
                browser_resolve_redirect_url(s_last_location, redir, sizeof(redir))) {
                snprintf(s_status, sizeof(s_status), "REDIR %d/%d...",
                         redir_depth + 1, BROWSER_REDIRECT_MAX);
                printf("[BROWSER_10C] REDIR header alt %d/%d: %s -> %s\n",
                       redir_depth + 1, BROWSER_REDIRECT_MAX, s_url_norm, redir);
                minipc_browser_load_internal(redir, redir_depth + 1);
                return;
            }
            rc = -5;
        }
    }

    if (rc != 0) {
        s_state = BROWSER_ERROR;

        if (rc == -2) {
            snprintf(s_status, sizeof(s_status), "Sin red/IP WiFi");
        } else if (rc == -3) {
            snprintf(s_status, sizeof(s_status), "No pude crear temporal");
        } else if (s_last_http_status > 0) {
            snprintf(s_status, sizeof(s_status), "HTTP error %d", s_last_http_status);
        } else {
            snprintf(s_status, sizeof(s_status), "Error de descarga. Usa HTTP plano");
        }
        return;
    }

    s_state = BROWSER_RENDERING;
    snprintf(s_status, sizeof(s_status), "Procesando pagina...");

    FILE *f = fopen(tmp, "rb");
    if (!f) {
        s_state = BROWSER_ERROR;
        snprintf(s_status, sizeof(s_status), "No pude abrir la descarga");
        return;
    }

    int n = fread(s_raw, 1, RAW_MAX - 1, f);
    fclose(f);
    remove(tmp);

    if (n <= 0) {
        s_state = BROWSER_ERROR;
        snprintf(s_status, sizeof(s_status), "Pagina vacia");
        return;
    }

    s_raw[n] = '\0';

    // 10E: redirecciones cliente sencillas antes de parsear/renderizar.
    // No ejecuta JavaScript; solo extrae destinos evidentes de meta refresh
    // y patrones location.* muy comunes.
    if (redir_depth < BROWSER_REDIRECT_MAX) {
        char redir[URL_NORM_MAX];
        const char *kind = NULL;

        if (browser_find_client_redirect(redir, sizeof(redir), &kind)) {
            snprintf(s_status, sizeof(s_status), "REDIR %s %d/%d...",
                     kind ? kind : "CLIENT",
                     redir_depth + 1,
                     BROWSER_REDIRECT_MAX);
            printf("[BROWSER_10E] REDIR %s %d/%d: %s -> %s\n",
                   kind ? kind : "CLIENT",
                   redir_depth + 1,
                   BROWSER_REDIRECT_MAX,
                   s_url_norm,
                   redir);
            minipc_browser_load_internal(redir, redir_depth + 1);
            return;
        }
    }

    parse_html_to_text();

    // 10C: algunas webs devuelven una pagina HTML "301 Moved" con un enlace "here"
    // en vez de una cabecera Location usable. Si se detecta, seguimos el primer enlace util.
    if (redir_depth < BROWSER_REDIRECT_MAX && browser_page_looks_like_redirect()) {
        char redir[URL_NORM_MAX];
        if (browser_first_redirect_link(redir, sizeof(redir))) {
            snprintf(s_status, sizeof(s_status), "REDIR HTML %d/%d...",
                     redir_depth + 1, BROWSER_REDIRECT_MAX);
            printf("[BROWSER_10C] REDIR html %d/%d: %s -> %s\n",
                   redir_depth + 1, BROWSER_REDIRECT_MAX, s_url_norm, redir);
            minipc_browser_load_internal(redir, redir_depth + 1);
            return;
        }
    }

    s_state = BROWSER_DONE;
    browser_nav_record_success(s_url_norm);

    if (n >= RAW_MAX - 1) {
        snprintf(s_status, sizeof(s_status), "HTML10E: %d lineas, %d enlaces (recortado)", s_line_count, s_link_count);
    } else {
        snprintf(s_status, sizeof(s_status), "HTML10E: %d lineas, %d enlaces", s_line_count, s_link_count);
    }
}

void minipc_browser_load(const char *url)
{
    minipc_browser_load_internal(url, 0);
}

browser_state_t minipc_browser_state(void)
{
    return s_state;
}

const char *minipc_browser_status_text(void)
{
    return s_status;
}

const char *minipc_browser_text(void)
{
    return s_text ? s_text : "";
}

int minipc_browser_line_count(void)
{
    return s_line_count;
}

const char *minipc_browser_get_line(int n, int *out_len)
{
    if (!s_text || n < 0 || n >= s_line_count) {
        if (out_len) *out_len = 0;
        return "";
    }

    const char *line = s_text + s_line_off[n];
    if (out_len) *out_len = strlen(line);
    return line;
}


int minipc_browser_link_count(void)
{
    return s_link_count;
}

const char *minipc_browser_get_link_url(int n)
{
    if (!s_link_url || n < 0 || n >= s_link_count) return "";
    return link_url_at(n);
}

const char *minipc_browser_get_link_text(int n)
{
    if (!s_link_txt || n < 0 || n >= s_link_count) return "";
    return link_txt_at(n);
}



int minipc_browser_form_available(void)
{
    return s_form_available;
}

const char *minipc_browser_form_action(void)
{
    return s_form_action;
}

const char *minipc_browser_form_input_name(void)
{
    return s_form_input;
}

const char *minipc_browser_current_url(void)
{
    if (s_current_url[0]) return s_current_url;
    if (s_history_pos >= 0 && s_history_pos < s_history_count) return s_history[s_history_pos];
    return "";
}

const char *minipc_browser_home_url(void)
{
    browser_nav_reset_if_needed();
    return s_home_url;
}

void minipc_browser_set_home(const char *url)
{
    char norm[URL_NORM_MAX];
    int downgraded = 0;

    if (browser_prepare_url(url, norm, sizeof(norm), &downgraded) != 0) {
        browser_status_literal("HOME invalida");
        return;
    }

    safe_copy_ascii(s_home_url, sizeof(s_home_url), norm);
    browser_status_literal("HOME actualizada");
}

void minipc_browser_home(void)
{
    browser_nav_reset_if_needed();
    minipc_browser_load(s_home_url);
}

void minipc_browser_reload(void)
{
    const char *u = minipc_browser_current_url();

    if (!u || !*u) {
        u = s_home_url;
    }

    s_nav_suppress_history = 1;
    minipc_browser_load(u);
    s_nav_suppress_history = 0;
}

int minipc_browser_can_back(void)
{
    return s_history_pos > 0;
}

int minipc_browser_can_forward(void)
{
    return (s_history_pos >= 0 && s_history_pos < s_history_count - 1);
}

void minipc_browser_back(void)
{
    if (!minipc_browser_can_back()) {
        browser_status_literal("Sin pagina anterior");
        return;
    }

    int old_pos = s_history_pos;
    s_history_pos--;

    s_nav_suppress_history = 1;
    minipc_browser_load(s_history[s_history_pos]);
    s_nav_suppress_history = 0;

    if (s_state != BROWSER_DONE) {
        s_history_pos = old_pos;
    }
}

void minipc_browser_forward(void)
{
    if (!minipc_browser_can_forward()) {
        browser_status_literal("Sin pagina siguiente");
        return;
    }

    int old_pos = s_history_pos;
    s_history_pos++;

    s_nav_suppress_history = 1;
    minipc_browser_load(s_history[s_history_pos]);
    s_nav_suppress_history = 0;

    if (s_state != BROWSER_DONE) {
        s_history_pos = old_pos;
    }
}

int minipc_browser_history_count(void)
{
    return s_history_count;
}

int minipc_browser_history_index(void)
{
    return s_history_pos;
}

const char *minipc_browser_history_url(int n)
{
    if (n < 0 || n >= s_history_count) return "";
    return s_history[n];
}

// Salta directamente a la posicion n del historial (para el listado HIST).
// Mismo patron que back()/forward(), pero a un indice arbitrario.
int minipc_browser_history_goto(int n)
{
    if (n < 0 || n >= s_history_count) return -1;
    if (n == s_history_pos) return 0;

    int old_pos = s_history_pos;
    s_history_pos = n;

    s_nav_suppress_history = 1;
    minipc_browser_load(s_history[n]);
    s_nav_suppress_history = 0;

    if (s_state != BROWSER_DONE) {
        s_history_pos = old_pos;
        return -1;
    }
    return 0;
}

// ---------------- Favoritos ----------------

// Carga favoritos desde la SD (o LittleFS como respaldo). Se llama sola la
// primera vez que se necesitan (init perezoso), asi no afecta al arranque
// si el navegador no llega a usarse.
static void browser_fav_load(void)
{
    if (s_fav_loaded) return;
    s_fav_loaded = 1;
    s_fav_count = 0;

    FILE *f = fopen(BROWSER_FAV_FILE, "r");
    if (!f) f = fopen(BROWSER_FAV_FILE_ALT, "r");
    if (!f) return;   // sin favoritos guardados todavia, no es un error

    char line[URL_NORM_MAX];
    while (s_fav_count < BROWSER_FAV_MAX && fgets(line, sizeof(line), f)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;
        safe_copy_ascii(s_fav[s_fav_count], URL_NORM_MAX, line);
        s_fav_count++;
    }
    fclose(f);
}

// Guarda la lista completa de favoritos a la SD (o LittleFS si no hay SD).
static void browser_fav_save(void)
{
    FILE *f = fopen(BROWSER_FAV_FILE, "w");
    if (!f) f = fopen(BROWSER_FAV_FILE_ALT, "w");
    if (!f) return;

    for (int i = 0; i < s_fav_count; i++) {
        fprintf(f, "%s\n", s_fav[i]);
    }
    fclose(f);
}

// Anade la URL actual a favoritos. Devuelve: 0=OK, 1=ya estaba, -1=lista llena.
int minipc_browser_fav_add(const char *url)
{
    browser_fav_load();
    if (!url || !*url) return -1;

    for (int i = 0; i < s_fav_count; i++) {
        if (strcmp(s_fav[i], url) == 0) return 1;   // ya estaba
    }
    if (s_fav_count >= BROWSER_FAV_MAX) return -1;   // lista llena

    safe_copy_ascii(s_fav[s_fav_count], URL_NORM_MAX, url);
    s_fav_count++;
    browser_fav_save();
    return 0;
}

// Elimina el favorito en la posicion n.
int minipc_browser_fav_remove(int n)
{
    browser_fav_load();
    if (n < 0 || n >= s_fav_count) return -1;

    for (int i = n; i < s_fav_count - 1; i++) {
        memcpy(s_fav[i], s_fav[i + 1], URL_NORM_MAX);
    }
    s_fav_count--;
    browser_fav_save();
    return 0;
}

int minipc_browser_fav_count(void)
{
    browser_fav_load();
    return s_fav_count;
}

const char *minipc_browser_fav_url(int n)
{
    browser_fav_load();
    if (n < 0 || n >= s_fav_count) return "";
    return s_fav[n];
}

// Devuelve 1 si la URL actual ya esta en favoritos (para pintar el boton
// +FAV distinto cuando la pagina ya esta guardada).
int minipc_browser_fav_is_current(void)
{
    browser_fav_load();
    const char *cur = s_current_url;
    for (int i = 0; i < s_fav_count; i++) {
        if (strcmp(s_fav[i], cur) == 0) return 1;
    }
    return 0;
}
