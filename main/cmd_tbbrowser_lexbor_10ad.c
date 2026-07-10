/*
 * Arielo MiniPC OS - TactileBrowser builtin local browser 10AD
 *
 * Base: 10AA_TACTILEBROWSER_BUILTIN_RENDER_TEST validada.
 *
 * Objetivo 10AD:
 *   - Mantener Lexbor integrado en firmware normal, sin ELF externo.
 *   - Cargar HTML desde sample / SD / USB / root.
 *   - Parsear con Lexbor.
 *   - Recorrer DOM basico.
 *   - Convertir a lineas de texto.
 *   - Mostrar visor paginado con scroll basico por teclado.
 *   - Detectar enlaces <a href=...> y permitir abrir enlaces locales.
 *
 * Objetivo 10AD:
 *   - Mantener todo lo validado en 10AC.
 *   - Cabecera mas tipo navegador con titulo/ruta/links/historial.
 *   - Historial BACK con tecla b.
 *   - Normalizacion basica de rutas relativas ./ y ../.
 *
 * Comandos:
 *   tbbrowser
 *   tbbrowser sample
 *   tbbrowser file /sdcard/test.html
 *   tbbrowser /sdcard/test.html
 *
 * Controles dentro del visor:
 *   n / ENTER : bajar una pagina
 *   p         : subir una pagina
 *   j/k       : bajar/subir una linea
 *   l         : listar enlaces detectados
 *   numero    : abrir enlace local [n]
 *   o RUTA    : abrir archivo local
 *   r         : recargar
 *   b         : volver atras en historial
 *   g / G     : inicio / final
 *   h / ?     : ayuda
 *   q         : salir
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "lexbor/html/html.h"
#include "lexbor/core/lexbor.h"
#include "lexbor/dom/interfaces/node.h"
#include "lexbor/dom/interfaces/text.h"
#include "lexbor/dom/interfaces/element.h"
#include "lexbor/html/interfaces/document.h"

#define TBB10AD_FILE_MAX          (32 * 1024)
#define TBB10AD_MAX_LINES         512
#define TBB10AD_LINE_CAP          112
#define TBB10AD_WRAP_COL          96
#define TBB10AD_PAGE_LINES        22
#define TBB10AD_MAX_LINKS         64
#define TBB10AD_HREF_CAP          192
#define TBB10AD_PATH_CAP          256
#define TBB10AD_HISTORY_MAX       12
#define TBB10AD_TITLE_CAP         96

static void *tbb10ad_lexbor_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbb10ad_lexbor_calloc(size_t num, size_t size)
{
    size_t total = num * size;
    if (num != 0 && total / num != size) return NULL;

    void *p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_calloc(num, size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbb10ad_lexbor_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    return p;
}

static void tbb10ad_lexbor_free(void *ptr)
{
    heap_caps_free(ptr);
}

static void tbb10ad_lexbor_memory_setup(void)
{
    static bool done = false;
    if (done) return;

    lxb_status_t st = lexbor_memory_setup(tbb10ad_lexbor_malloc,
                                          tbb10ad_lexbor_realloc,
                                          tbb10ad_lexbor_calloc,
                                          tbb10ad_lexbor_free);
    printf("[TBBROWSER10AD] lexbor_memory_setup PSRAM-first st=%d\n", (int) st);
    done = true;
}

static void tbb10ad_heap_print(const char *tag)
{
    printf("[TBBROWSER10AD] %-12s heap8=%u psram=%u min8=%u\n",
           tag ? tag : "?",
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

typedef struct {
    char *buf;
    char *links;
    int max_lines;
    int line_cap;
    int link_href_cap;
    int link_count;
    char title[TBB10AD_TITLE_CAP];
    int count;
    int col;
    bool last_space;
    bool stopped;
} tbb10ad_doc_t;

static char *tbb10ad_line_ptr(tbb10ad_doc_t *doc, int idx)
{
    if (doc == NULL || doc->buf == NULL || idx < 0 || idx >= doc->max_lines) return NULL;
    return doc->buf + ((size_t) idx * (size_t) doc->line_cap);
}

static char *tbb10ad_link_ptr(tbb10ad_doc_t *doc, int idx)
{
    if (doc == NULL || doc->links == NULL || idx < 0 || idx >= TBB10AD_MAX_LINKS) return NULL;
    return doc->links + ((size_t) idx * (size_t) doc->link_href_cap);
}

static int tbb10ad_add_link(tbb10ad_doc_t *doc, const lxb_char_t *href, size_t href_len)
{
    if (doc == NULL || href == NULL || href_len == 0 || doc->links == NULL) return 0;
    if (doc->link_count >= TBB10AD_MAX_LINKS) return 0;

    char *dst = tbb10ad_link_ptr(doc, doc->link_count);
    if (dst == NULL) return 0;

    size_t n = href_len;
    if (n >= (size_t) doc->link_href_cap) n = (size_t) doc->link_href_cap - 1;
    memcpy(dst, href, n);
    dst[n] = '\0';

    doc->link_count++;
    return doc->link_count;
}

static bool tbb10ad_doc_alloc(tbb10ad_doc_t *doc)
{
    if (doc == NULL) return false;
    memset(doc, 0, sizeof(*doc));

    doc->max_lines = TBB10AD_MAX_LINES;
    doc->line_cap = TBB10AD_LINE_CAP;
    doc->link_href_cap = TBB10AD_HREF_CAP;

    size_t bytes = (size_t) doc->max_lines * (size_t) doc->line_cap;
    doc->buf = (char *) heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (doc->buf == NULL) {
        doc->buf = (char *) heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    }
    if (doc->buf == NULL) {
        printf("[TBBROWSER10AD][ERR] sin memoria para line buffer bytes=%u\n", (unsigned) bytes);
        return false;
    }

    size_t lbytes = (size_t) TBB10AD_MAX_LINKS * (size_t) TBB10AD_HREF_CAP;
    doc->links = (char *) heap_caps_calloc(1, lbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (doc->links == NULL) {
        doc->links = (char *) heap_caps_calloc(1, lbytes, MALLOC_CAP_8BIT);
    }
    if (doc->links == NULL) {
        printf("[TBBROWSER10AD][WARN] sin memoria para links bytes=%u; continuo sin enlaces\n", (unsigned) lbytes);
    }

    doc->link_count = 0;
    doc->count = 1;
    doc->col = 0;
    doc->last_space = false;
    doc->stopped = false;
    return true;
}

static void tbb10ad_doc_free(tbb10ad_doc_t *doc)
{
    if (doc == NULL) return;
    if (doc->buf != NULL) heap_caps_free(doc->buf);
    if (doc->links != NULL) heap_caps_free(doc->links);
    memset(doc, 0, sizeof(*doc));
}

static bool tbb10ad_char_is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static bool tbb10ad_tag_eq(const lxb_char_t *name, size_t len, const char *lit)
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

static void tbb10ad_newline(tbb10ad_doc_t *doc, bool force)
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

static void tbb10ad_put_char(tbb10ad_doc_t *doc, char c)
{
    if (doc == NULL || doc->stopped) return;
    if (doc->count <= 0) doc->count = 1;

    if (c == '\n') {
        tbb10ad_newline(doc, true);
        return;
    }

    if (doc->col >= TBB10AD_WRAP_COL || doc->col >= (doc->line_cap - 1)) {
        tbb10ad_newline(doc, true);
        if (doc->stopped) return;
    }

    char *line = tbb10ad_line_ptr(doc, doc->count - 1);
    if (line == NULL) {
        doc->stopped = true;
        return;
    }

    line[doc->col++] = c;
    line[doc->col] = '\0';
    doc->last_space = (c == ' ');
}

static void tbb10ad_puts(tbb10ad_doc_t *doc, const char *s)
{
    if (doc == NULL || s == NULL || doc->stopped) return;
    while (*s && !doc->stopped) {
        tbb10ad_put_char(doc, *s++);
    }
}

static void tbb10ad_text(tbb10ad_doc_t *doc, const lxb_char_t *data, size_t len)
{
    if (doc == NULL || data == NULL || len == 0 || doc->stopped) return;

    for (size_t i = 0; i < len && !doc->stopped; i++) {
        unsigned char c = (unsigned char) data[i];

        if (tbb10ad_char_is_space(c)) {
            if (doc->col > 0 && !doc->last_space) {
                tbb10ad_put_char(doc, ' ');
            }
            continue;
        }

        tbb10ad_put_char(doc, (char) c);
    }
}

static void tbb10ad_trim_trailing(tbb10ad_doc_t *doc)
{
    if (doc == NULL || doc->buf == NULL) return;

    while (doc->count > 1) {
        char *line = tbb10ad_line_ptr(doc, doc->count - 1);
        if (line == NULL || line[0] != '\0') break;
        doc->count--;
    }
}


static void tbb10ad_title_append(tbb10ad_doc_t *doc, const lxb_char_t *data, size_t len)
{
    if (doc == NULL || data == NULL || len == 0) return;

    size_t cur = strlen(doc->title);
    if (cur >= TBB10AD_TITLE_CAP - 1) return;

    bool last_space = false;
    if (cur > 0) last_space = (doc->title[cur - 1] == ' ');

    for (size_t i = 0; i < len && cur < TBB10AD_TITLE_CAP - 1; i++) {
        unsigned char c = (unsigned char) data[i];
        if (tbb10ad_char_is_space(c)) {
            if (cur > 0 && !last_space && cur < TBB10AD_TITLE_CAP - 1) {
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

static void tbb10ad_extract_title_dom(lxb_dom_node_t *node, tbb10ad_doc_t *doc, bool in_title, int depth)
{
    if (node == NULL || doc == NULL || depth > 96) return;
    if (strlen(doc->title) >= TBB10AD_TITLE_CAP - 2) return;

    bool now_title = in_title;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        size_t nlen = 0;
        const lxb_char_t *name = lxb_dom_node_name(node, &nlen);
        if (tbb10ad_tag_eq(name, nlen, "title")) now_title = true;
    }

    if (now_title && node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *txt = lxb_dom_interface_text(node);
        if (txt != NULL && txt->char_data.data.data != NULL) {
            tbb10ad_title_append(doc, txt->char_data.data.data, txt->char_data.data.length);
        }
    }

    for (lxb_dom_node_t *child = node->first_child; child != NULL; child = child->next) {
        tbb10ad_extract_title_dom(child, doc, now_title, depth + 1);
    }
}

static void tbb10ad_walk_dom(lxb_dom_node_t *node, tbb10ad_doc_t *doc, int depth)
{
    if (node == NULL || doc == NULL || doc->stopped || depth > 96) return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *txt = lxb_dom_interface_text(node);
        if (txt != NULL && txt->char_data.data.data != NULL) {
            tbb10ad_text(doc, txt->char_data.data.data, txt->char_data.data.length);
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

        is_h1 = tbb10ad_tag_eq(name, nlen, "h1");
        is_h2 = tbb10ad_tag_eq(name, nlen, "h2");
        is_h3 = tbb10ad_tag_eq(name, nlen, "h3");
        is_p = tbb10ad_tag_eq(name, nlen, "p");
        is_li = tbb10ad_tag_eq(name, nlen, "li");
        is_br = tbb10ad_tag_eq(name, nlen, "br");
        is_a = tbb10ad_tag_eq(name, nlen, "a");
        is_pre = tbb10ad_tag_eq(name, nlen, "pre");
        is_script = tbb10ad_tag_eq(name, nlen, "script");
        is_style = tbb10ad_tag_eq(name, nlen, "style");
        is_title = tbb10ad_tag_eq(name, nlen, "title");

        if (is_script || is_style || is_title) return;

        if (is_br) {
            tbb10ad_newline(doc, true);
            return;
        }

        if (is_h1) {
            tbb10ad_newline(doc, false);
            tbb10ad_puts(doc, "# ");
        }
        else if (is_h2) {
            tbb10ad_newline(doc, false);
            tbb10ad_puts(doc, "## ");
        }
        else if (is_h3) {
            tbb10ad_newline(doc, false);
            tbb10ad_puts(doc, "### ");
        }
        else if (is_p || is_pre) {
            tbb10ad_newline(doc, false);
        }
        else if (is_li) {
            tbb10ad_newline(doc, false);
            tbb10ad_puts(doc, "- ");
        }

        if (is_a) {
            lxb_dom_element_t *el = lxb_dom_interface_element(node);
            if (el != NULL) {
                size_t href_len = 0;
                const lxb_char_t *href = lxb_dom_element_get_attribute(el, (const lxb_char_t *) "href", 4, &href_len);
                if (href != NULL && href_len > 0) {
                    link_id = tbb10ad_add_link(doc, href, href_len);
                    if (link_id > 0) {
                        char mark[20];
                        snprintf(mark, sizeof(mark), "[%d] ", link_id);
                        tbb10ad_puts(doc, mark);
                    }
                }
            }
        }
    }

    for (lxb_dom_node_t *child = node->first_child; child != NULL && !doc->stopped; child = child->next) {
        tbb10ad_walk_dom(child, doc, depth + 1);
    }

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        if (is_h1 || is_h2 || is_h3 || is_p || is_li || is_pre) {
            tbb10ad_newline(doc, false);
        }
    }
}

static char *tbb10ad_load_file(const char *path, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (path == NULL || path[0] == '\0') return NULL;

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("[TBBROWSER10AD][ERR] stat fallo: %s\n", path);
        return NULL;
    }

    if (st.st_size <= 0 || st.st_size > TBB10AD_FILE_MAX) {
        printf("[TBBROWSER10AD][ERR] tamano no valido: %ld bytes, max=%d\n",
               (long) st.st_size, TBB10AD_FILE_MAX);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("[TBBROWSER10AD][ERR] fopen fallo: %s\n", path);
        return NULL;
    }

    char *buf = (char *) heap_caps_malloc((size_t) st.st_size + 1,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) buf = (char *) heap_caps_malloc((size_t) st.st_size + 1, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        fclose(f);
        printf("[TBBROWSER10AD][ERR] sin memoria para archivo\n");
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t) st.st_size, f);
    fclose(f);
    buf[rd] = '\0';

    if (out_len) *out_len = rd;
    printf("[TBBROWSER10AD] archivo cargado %s bytes=%u\n", path, (unsigned) rd);
    return buf;
}

static const char *tbb10ad_sample_html(void)
{
    return "<!doctype html><html><head><title>Arielo MiniPC OS</title></head>"
           "<body>"
           "<h1>Arielo MiniPC OS</h1>"
           "<h2>TactileBrowser 10AD</h2>"
           "<p>Navegador local interno con cabecera, enlaces y BACK.</p>"
           "<p>Lexbor esta integrado en firmware normal, sin ELF externo.</p>"
           "<ul>"
           "<li>Carga HTML desde SD, USB o root.</li>"
           "<li>Parsea DOM con Lexbor.</li>"
           "<li>Convierte el DOM a lineas de texto.</li>"
           "<li>Muestra paginas y permite subir/bajar.</li>"
           "</ul>"
           "<h2>Controles</h2>"
           "<p>n/ENTER baja pagina. p sube. j/k linea. b atras. l enlaces. r recarga. q salir.</p>"
           "<p>Esta es una base prudente. Sin CSS ni imagenes. Navegacion local y rutas relativas basicas.</p>"
           "<p>Enlaces locales de prueba: <a href=\"/sdcard/test.html\">test SD</a> "
           "y <a href=\"/usb/test.html\">test USB</a>.</p>"
           "</body></html>";
}

static int tbb10ad_build_doc_from_html(const char *label, const char *html, size_t html_len, tbb10ad_doc_t *out_doc)
{
    if (html == NULL) html = "";
    if (out_doc == NULL) return 1;

    tbb10ad_lexbor_memory_setup();
    tbb10ad_heap_print("before");

    printf("[TBBROWSER10AD] build begin label=%s len=%u\n", label ? label : "?", (unsigned) html_len);

    if (!tbb10ad_doc_alloc(out_doc)) {
        tbb10ad_heap_print("after_buferr");
        return 1;
    }

    lxb_html_document_t *doc = lxb_html_document_create();
    printf("[TBBROWSER10AD] doc=%p\n", (void *) doc);
    if (doc == NULL) {
        printf("[TBBROWSER10AD][ERR] create NULL\n");
        tbb10ad_doc_free(out_doc);
        tbb10ad_heap_print("after_null");
        return 1;
    }

    int64_t t0 = esp_timer_get_time();
    lxb_status_t st = lxb_html_document_parse(doc, (const lxb_char_t *) html, html_len);
    int64_t t1 = esp_timer_get_time();

    printf("[TBBROWSER10AD] parse status=%d time_us=%" PRId64 "\n", (int) st, (int64_t)(t1 - t0));

    if (st != LXB_STATUS_OK) {
        printf("[TBBROWSER10AD][ERR] parse failed\n");
        lxb_html_document_destroy(doc);
        tbb10ad_doc_free(out_doc);
        tbb10ad_heap_print("after_fail");
        return 1;
    }

    lxb_html_body_element_t *body = lxb_html_document_body_element(doc);
    lxb_dom_node_t *root = NULL;
    if (body != NULL) root = lxb_dom_interface_node(body);
    else root = lxb_dom_interface_node(doc);

    tbb10ad_extract_title_dom(lxb_dom_interface_node(doc), out_doc, false, 0);
    if (out_doc->title[0] == '\0' && label != NULL && label[0] != '\0') {
        snprintf(out_doc->title, sizeof(out_doc->title), "%s", label);
    }

    tbb10ad_walk_dom(root, out_doc, 0);
    tbb10ad_trim_trailing(out_doc);

    printf("[TBBROWSER10AD] DOM->lineas OK title='%s' lines=%d links=%d stopped=%d\n", out_doc->title, out_doc->count, out_doc->link_count, out_doc->stopped ? 1 : 0);

    lxb_html_document_destroy(doc);
    printf("[TBBROWSER10AD] destroy OK\n");
    tbb10ad_heap_print("after_build");
    return 0;
}

static void tbb10ad_clear_screen(void)
{
    /* VTerm suele aceptar ANSI. Si algun terminal no lo interpreta, no afecta a la prueba. */
    printf("\033[2J\033[H");
}

static void tbb10ad_pause_enter(const char *msg)
{
    if (msg != NULL && msg[0] != '\0') printf("%s", msg);
    else printf("Pulse ENTER para continuar... ");
    fflush(stdout);
    char tmp[32];
    (void) fgets(tmp, sizeof(tmp), stdin);
}

static void tbb10ad_show_help(void)
{
    printf("\n[TBBROWSER10AD] Controles:\n");
    printf("  n o ENTER : bajar una pagina\n");
    printf("  p         : subir una pagina\n");
    printf("  j/k       : bajar/subir una linea\n");
    printf("  l         : listar enlaces detectados\n");
    printf("  numero    : abrir enlace local [n]\n");
    printf("  o RUTA    : abrir archivo local, ejemplo o /sdcard/test.html\n");
    printf("  r         : recargar documento actual\n");
    printf("  b         : volver atras en historial\n");
    printf("  g / G     : inicio / final\n");
    printf("  h o ?     : ayuda\n");
    printf("  q         : salir\n");
    tbb10ad_pause_enter("Pulse ENTER para volver al navegador... ");
}

static void tbb10ad_show_links(tbb10ad_doc_t *doc)
{
    printf("\n[TBBROWSER10AD] Enlaces detectados: %d\n", doc ? doc->link_count : 0);
    if (doc != NULL) {
        for (int i = 0; i < doc->link_count; i++) {
            char *href = tbb10ad_link_ptr(doc, i);
            printf("  [%d] %s\n", i + 1, href ? href : "?");
        }
    }
    tbb10ad_pause_enter("Pulse ENTER para volver al navegador... ");
}

static bool tbb10ad_is_digits(const char *s)
{
    if (s == NULL || s[0] == '\0') return false;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\r' || s[i] == '\n') break;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

static void tbb10ad_chomp(char *s)
{
    if (s == NULL) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static bool tbb10ad_is_external_url(const char *s)
{
    if (s == NULL) return false;
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0 ||
           strncmp(s, "mailto:", 7) == 0 || strncmp(s, "ftp://", 6) == 0;
}

static void tbb10ad_strip_fragment_query(char *s)
{
    if (s == NULL) return;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '#' || s[i] == '?') {
            s[i] = '\0';
            break;
        }
    }
}


static void tbb10ad_normalize_abs_path(char *path)
{
    if (path == NULL || path[0] != '/') return;

    char src[TBB10AD_PATH_CAP];
    snprintf(src, sizeof(src), "%s", path);

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
        size_t left = TBB10AD_PATH_CAP - pos;
        if (left <= 1) break;
        int n = snprintf(path + pos, left, "%s%s", parts[i], (i == count - 1) ? "" : "/");
        if (n < 0 || (size_t) n >= left) break;
        pos += (size_t) n;
    }
}

static bool tbb10ad_resolve_path(const char *current, const char *href, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    if (href == NULL || href[0] == '\0') return false;

    char tmp[TBB10AD_PATH_CAP];
    snprintf(tmp, sizeof(tmp), "%s", href);
    tbb10ad_chomp(tmp);
    tbb10ad_strip_fragment_query(tmp);
    if (tmp[0] == '\0') return false;

    if (tbb10ad_is_external_url(tmp)) {
        printf("[TBBROWSER10AD] URL externa aun no soportada: %s\n", tmp);
        return false;
    }

    if (tmp[0] == '/') {
        snprintf(out, out_cap, "%s", tmp);
        tbb10ad_normalize_abs_path(out);
        return true;
    }

    if (current != NULL && current[0] == '/') {
        char base[TBB10AD_PATH_CAP];
        snprintf(base, sizeof(base), "%s", current);
        char *slash = strrchr(base, '/');
        if (slash != NULL) slash[1] = '\0';
        else strcpy(base, "/");
        snprintf(out, out_cap, "%s%s", base, tmp);
        tbb10ad_normalize_abs_path(out);
        return true;
    }

    snprintf(out, out_cap, "/sdcard/%s", tmp);
    tbb10ad_normalize_abs_path(out);
    return true;
}

static void tbb10ad_draw_page(tbb10ad_doc_t *doc, const char *label, int top, int hist_count)
{
    if (doc == NULL) return;
    if (top < 0) top = 0;
    int max_top = doc->count - TBB10AD_PAGE_LINES;
    if (max_top < 0) max_top = 0;
    if (top > max_top) top = max_top;

    int end_line = (top + TBB10AD_PAGE_LINES < doc->count) ? (top + TBB10AD_PAGE_LINES) : doc->count;
    int pct = 100;
    if (max_top > 0) pct = (top * 100) / max_top;

    tbb10ad_clear_screen();
    printf("Arielo MiniPC OS - TBROWSER 10AD LOCAL HTML NAVIGATOR\n");
    printf("Titulo: %s\n", (doc->title[0] != '\0') ? doc->title : "(sin titulo)");
    printf("Ruta  : %s\n", label ? label : "sample");
    printf("Estado: lineas=%d vista=%d-%d scroll=%d%% links=%d back=%d | n/p j/k b l o r h q\n",
           doc->count, top + 1, end_line, pct, doc->link_count, hist_count);
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < TBB10AD_PAGE_LINES; i++) {
        int idx = top + i;
        if (idx < doc->count) {
            char *line = tbb10ad_line_ptr(doc, idx);
            printf("%s\n", line ? line : "");
        }
        else {
            printf("~\n");
        }
    }

    printf("--------------------------------------------------------------------------------\n");
}

static int tbb10ad_viewer_loop(tbb10ad_doc_t *doc, const char *label, int hist_count,
                                char *out_open_path, size_t out_cap, bool *out_reload, bool *out_back)
{
    if (out_open_path != NULL && out_cap > 0) out_open_path[0] = '\0';
    if (out_reload != NULL) *out_reload = false;
    if (out_back != NULL) *out_back = false;
    if (doc == NULL) return 1;

    int top = 0;
    int max_top = doc->count - TBB10AD_PAGE_LINES;
    if (max_top < 0) max_top = 0;

    char cmd[96];
    while (true) {
        if (top < 0) top = 0;
        if (top > max_top) top = max_top;

        tbb10ad_draw_page(doc, label, top, hist_count);
        printf("TBBROWSER10AD> ");
        fflush(stdout);

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            printf("\n[TBBROWSER10AD] stdin cerrado, salgo\n");
            break;
        }

        tbb10ad_chomp(cmd);
        char c = cmd[0];
        if (c == '\0' || c == '\n' || c == '\r' || c == 'n' || c == 'N' || c == ' ') {
            top += TBB10AD_PAGE_LINES;
        }
        else if (c == 'p' || c == 'P') {
            top -= TBB10AD_PAGE_LINES;
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
        else if (c == 'h' || c == 'H' || c == '?') {
            tbb10ad_show_help();
        }
        else if (c == 'l' || c == 'L') {
            tbb10ad_show_links(doc);
        }
        else if (c == 'r' || c == 'R') {
            if (out_reload != NULL) *out_reload = true;
            return 2;
        }
        else if (c == 'b' || c == 'B') {
            if (hist_count <= 0) {
                printf("[TBBROWSER10AD] no hay pagina anterior en historial\n");
                tbb10ad_pause_enter(NULL);
            }
            else {
                if (out_back != NULL) *out_back = true;
                return 2;
            }
        }
        else if ((c == 'o' || c == 'O') && (cmd[1] == ' ' || cmd[1] == '\t')) {
            const char *p = cmd + 2;
            while (*p == ' ' || *p == '\t') p++;
            if (p[0] != '\0' && out_open_path != NULL && out_cap > 0) {
                snprintf(out_open_path, out_cap, "%s", p);
                return 2;
            }
        }
        else if (tbb10ad_is_digits(cmd)) {
            int n = atoi(cmd);
            if (n >= 1 && n <= doc->link_count) {
                char *href = tbb10ad_link_ptr(doc, n - 1);
                if (href != NULL && out_open_path != NULL && out_cap > 0) {
                    snprintf(out_open_path, out_cap, "%s", href);
                    return 2;
                }
            }
            else {
                printf("[TBBROWSER10AD] enlace fuera de rango: %d\n", n);
                tbb10ad_pause_enter(NULL);
            }
        }
        else if (c == 'q' || c == 'Q' || c == 27) {
            break;
        }
    }

    tbb10ad_clear_screen();
    printf("[TBBROWSER10AD] navegador cerrado.\n");
    return 0;
}

static int tbb10ad_build_and_view_once(const char *label, const char *html, size_t html_len, int hist_count,
                                      char *out_open_path, size_t out_cap, bool *out_reload, bool *out_back)
{
    tbb10ad_doc_t doc;
    int r = tbb10ad_build_doc_from_html(label, html, html_len, &doc);
    if (r != 0) return r;

    r = tbb10ad_viewer_loop(&doc, label, hist_count, out_open_path, out_cap, out_reload, out_back);
    tbb10ad_doc_free(&doc);
    tbb10ad_heap_print("after_view");
    return r;
}

static void tbb10ad_history_push(char hist[TBB10AD_HISTORY_MAX][TBB10AD_PATH_CAP], int *count, const char *path)
{
    if (hist == NULL || count == NULL || path == NULL || path[0] == '\0') return;

    if (*count >= TBB10AD_HISTORY_MAX) {
        /*
         * FIX 10AD-1:
         * GCC con -Werror=restrict avisa en snprintf(hist[i-1], ..., hist[i])
         * porque son filas del mismo array. Aunque en la practica son filas
         * distintas, usamos memmove() para desplazar el historial completo
         * de una vez y evitar cualquier duda de solape.
         */
        memmove(hist[0], hist[1], (TBB10AD_HISTORY_MAX - 1) * TBB10AD_PATH_CAP);
        hist[TBB10AD_HISTORY_MAX - 1][0] = '\0';
        *count = TBB10AD_HISTORY_MAX - 1;
    }

    snprintf(hist[*count], TBB10AD_PATH_CAP, "%s", path);
    (*count)++;
}

static bool tbb10ad_history_pop(char hist[TBB10AD_HISTORY_MAX][TBB10AD_PATH_CAP], int *count,
                                char *out, size_t out_cap)
{
    if (hist == NULL || count == NULL || out == NULL || out_cap == 0) return false;
    if (*count <= 0) return false;

    (*count)--;
    snprintf(out, out_cap, "%s", hist[*count]);
    hist[*count][0] = '\0';
    return true;
}

static int tbb10ad_browser_session(const char *initial_path, bool initial_sample)
{
    char current[TBB10AD_PATH_CAP];
    char next[TBB10AD_PATH_CAP];
    char history[TBB10AD_HISTORY_MAX][TBB10AD_PATH_CAP];
    bool sample = initial_sample;
    int history_count = 0;
    int guard = 0;

    memset(history, 0, sizeof(history));

    if (initial_path != NULL && initial_path[0] != '\0') {
        snprintf(current, sizeof(current), "%s", initial_path);
        tbb10ad_normalize_abs_path(current);
        sample = false;
    }
    else {
        strcpy(current, "sample");
        sample = true;
    }

    while (guard++ < 96) {
        next[0] = '\0';
        bool reload = false;
        bool back = false;
        int r = 0;

        if (sample) {
            const char *html = tbb10ad_sample_html();
            r = tbb10ad_build_and_view_once("sample", html, strlen(html), history_count,
                                            next, sizeof(next), &reload, &back);
        }
        else {
            size_t len = 0;
            char *html = tbb10ad_load_file(current, &len);
            if (html == NULL) {
                printf("[TBBROWSER10AD][ERR] no pude abrir: %s\n", current);
                tbb10ad_pause_enter(NULL);
                return 1;
            }
            r = tbb10ad_build_and_view_once(current, html, len, history_count,
                                            next, sizeof(next), &reload, &back);
            heap_caps_free(html);
        }

        if (r != 2) return r;

        if (reload) {
            continue;
        }

        if (back) {
            char prev[TBB10AD_PATH_CAP];
            if (tbb10ad_history_pop(history, &history_count, prev, sizeof(prev))) {
                snprintf(current, sizeof(current), "%s", prev);
                sample = (strcmp(current, "sample") == 0);
            }
            continue;
        }

        char resolved[TBB10AD_PATH_CAP];
        if (!tbb10ad_resolve_path(sample ? NULL : current, next, resolved, sizeof(resolved))) {
            tbb10ad_pause_enter("No se pudo resolver/abrir ese enlace. ENTER... ");
            continue;
        }

        tbb10ad_history_push(history, &history_count, current);
        snprintf(current, sizeof(current), "%s", resolved);
        sample = false;
    }

    printf("[TBBROWSER10AD][WARN] limite de navegacion alcanzado\n");
    return 0;
}

int cmd_tbbrowser_lexbor_10ad(int argc, char **argv)
{
    printf("\n[TBBROWSER10AD] TactileBrowser LOCAL NAVIGATOR 10AD - no ELF externo\n");

    if (argc <= 1 || strcmp(argv[1], "sample") == 0) {
        return tbb10ad_browser_session(NULL, true);
    }

    if (strcmp(argv[1], "file") == 0) {
        if (argc < 3) {
            printf("Uso: tbbrowser file /sdcard/pagina.html\n");
            return 1;
        }
        return tbb10ad_browser_session(argv[2], false);
    }

    if (argv[1][0] == '/') {
        return tbb10ad_browser_session(argv[1], false);
    }

    printf("Uso: tbbrowser [sample|file PATH|/ruta/pagina.html]\n");
    return 0;
}
