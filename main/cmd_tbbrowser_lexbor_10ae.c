/*
 * Arielo MiniPC OS - TactileBrowser builtin local browser 10AE
 *
 * Base: 10AA_TACTILEBROWSER_BUILTIN_RENDER_TEST validada.
 *
 * Objetivo 10AE:
 *   - Mantener Lexbor integrado en firmware normal, sin ELF externo.
 *   - Cargar HTML desde sample / SD / USB / root.
 *   - Parsear con Lexbor.
 *   - Recorrer DOM basico.
 *   - Convertir a lineas de texto.
 *   - Mostrar visor paginado con scroll basico por teclado.
 *   - Detectar enlaces <a href=...> y permitir abrir enlaces locales.
 *
 * Objetivo 10AE:
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
 *   p         : subir una pagina
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

#define TBB10AE_FILE_MAX          (32 * 1024)
#define TBB10AE_MAX_LINES         512
#define TBB10AE_LINE_CAP          112
#define TBB10AE_WRAP_COL          96
#define TBB10AE_PAGE_LINES        22
#define TBB10AE_MAX_LINKS         64
#define TBB10AE_HREF_CAP          192
#define TBB10AE_PATH_CAP          256
#define TBB10AE_HISTORY_MAX       12
#define TBB10AE_TITLE_CAP         96
#define TBB10AE_BOOKMARKS_FILE    "/root/tbbrowser_favs.txt"
#define TBB10AE_BOOKMARKS_MAX     64

static void tbb10ae_safe_copy(char *dst, size_t dst_cap, const char *src)
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


static void tbb10ae_safe_append(char *dst, size_t dst_cap, const char *src)
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

static void *tbb10ae_lexbor_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbb10ae_lexbor_calloc(size_t num, size_t size)
{
    size_t total = num * size;
    if (num != 0 && total / num != size) return NULL;

    void *p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_calloc(num, size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbb10ae_lexbor_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    return p;
}

static void tbb10ae_lexbor_free(void *ptr)
{
    heap_caps_free(ptr);
}

static void tbb10ae_lexbor_memory_setup(void)
{
    static bool done = false;
    if (done) return;

    lxb_status_t st = lexbor_memory_setup(tbb10ae_lexbor_malloc,
                                          tbb10ae_lexbor_realloc,
                                          tbb10ae_lexbor_calloc,
                                          tbb10ae_lexbor_free);
    printf("[TBBROWSER10AE] lexbor_memory_setup PSRAM-first st=%d\n", (int) st);
    done = true;
}

static void tbb10ae_heap_print(const char *tag)
{
    printf("[TBBROWSER10AE] %-12s heap8=%u psram=%u min8=%u\n",
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
    char title[TBB10AE_TITLE_CAP];
    int count;
    int col;
    bool last_space;
    bool stopped;
} tbb10ae_doc_t;

static char *tbb10ae_line_ptr(tbb10ae_doc_t *doc, int idx)
{
    if (doc == NULL || doc->buf == NULL || idx < 0 || idx >= doc->max_lines) return NULL;
    return doc->buf + ((size_t) idx * (size_t) doc->line_cap);
}

static char *tbb10ae_link_ptr(tbb10ae_doc_t *doc, int idx)
{
    if (doc == NULL || doc->links == NULL || idx < 0 || idx >= TBB10AE_MAX_LINKS) return NULL;
    return doc->links + ((size_t) idx * (size_t) doc->link_href_cap);
}

static int tbb10ae_add_link(tbb10ae_doc_t *doc, const lxb_char_t *href, size_t href_len)
{
    if (doc == NULL || href == NULL || href_len == 0 || doc->links == NULL) return 0;
    if (doc->link_count >= TBB10AE_MAX_LINKS) return 0;

    char *dst = tbb10ae_link_ptr(doc, doc->link_count);
    if (dst == NULL) return 0;

    size_t n = href_len;
    if (n >= (size_t) doc->link_href_cap) n = (size_t) doc->link_href_cap - 1;
    memcpy(dst, href, n);
    dst[n] = '\0';

    doc->link_count++;
    return doc->link_count;
}

static bool tbb10ae_doc_alloc(tbb10ae_doc_t *doc)
{
    if (doc == NULL) return false;
    memset(doc, 0, sizeof(*doc));

    doc->max_lines = TBB10AE_MAX_LINES;
    doc->line_cap = TBB10AE_LINE_CAP;
    doc->link_href_cap = TBB10AE_HREF_CAP;

    size_t bytes = (size_t) doc->max_lines * (size_t) doc->line_cap;
    doc->buf = (char *) heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (doc->buf == NULL) {
        doc->buf = (char *) heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    }
    if (doc->buf == NULL) {
        printf("[TBBROWSER10AE][ERR] sin memoria para line buffer bytes=%u\n", (unsigned) bytes);
        return false;
    }

    size_t lbytes = (size_t) TBB10AE_MAX_LINKS * (size_t) TBB10AE_HREF_CAP;
    doc->links = (char *) heap_caps_calloc(1, lbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (doc->links == NULL) {
        doc->links = (char *) heap_caps_calloc(1, lbytes, MALLOC_CAP_8BIT);
    }
    if (doc->links == NULL) {
        printf("[TBBROWSER10AE][WARN] sin memoria para links bytes=%u; continuo sin enlaces\n", (unsigned) lbytes);
    }

    doc->link_count = 0;
    doc->count = 1;
    doc->col = 0;
    doc->last_space = false;
    doc->stopped = false;
    return true;
}

static void tbb10ae_doc_free(tbb10ae_doc_t *doc)
{
    if (doc == NULL) return;
    if (doc->buf != NULL) heap_caps_free(doc->buf);
    if (doc->links != NULL) heap_caps_free(doc->links);
    memset(doc, 0, sizeof(*doc));
}

static bool tbb10ae_char_is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static bool tbb10ae_tag_eq(const lxb_char_t *name, size_t len, const char *lit)
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

static void tbb10ae_newline(tbb10ae_doc_t *doc, bool force)
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

static void tbb10ae_put_char(tbb10ae_doc_t *doc, char c)
{
    if (doc == NULL || doc->stopped) return;
    if (doc->count <= 0) doc->count = 1;

    if (c == '\n') {
        tbb10ae_newline(doc, true);
        return;
    }

    if (doc->col >= TBB10AE_WRAP_COL || doc->col >= (doc->line_cap - 1)) {
        tbb10ae_newline(doc, true);
        if (doc->stopped) return;
    }

    char *line = tbb10ae_line_ptr(doc, doc->count - 1);
    if (line == NULL) {
        doc->stopped = true;
        return;
    }

    line[doc->col++] = c;
    line[doc->col] = '\0';
    doc->last_space = (c == ' ');
}

static void tbb10ae_puts(tbb10ae_doc_t *doc, const char *s)
{
    if (doc == NULL || s == NULL || doc->stopped) return;
    while (*s && !doc->stopped) {
        tbb10ae_put_char(doc, *s++);
    }
}

static void tbb10ae_text(tbb10ae_doc_t *doc, const lxb_char_t *data, size_t len)
{
    if (doc == NULL || data == NULL || len == 0 || doc->stopped) return;

    for (size_t i = 0; i < len && !doc->stopped; i++) {
        unsigned char c = (unsigned char) data[i];

        if (tbb10ae_char_is_space(c)) {
            if (doc->col > 0 && !doc->last_space) {
                tbb10ae_put_char(doc, ' ');
            }
            continue;
        }

        tbb10ae_put_char(doc, (char) c);
    }
}

static void tbb10ae_trim_trailing(tbb10ae_doc_t *doc)
{
    if (doc == NULL || doc->buf == NULL) return;

    while (doc->count > 1) {
        char *line = tbb10ae_line_ptr(doc, doc->count - 1);
        if (line == NULL || line[0] != '\0') break;
        doc->count--;
    }
}


static void tbb10ae_title_append(tbb10ae_doc_t *doc, const lxb_char_t *data, size_t len)
{
    if (doc == NULL || data == NULL || len == 0) return;

    size_t cur = strlen(doc->title);
    if (cur >= TBB10AE_TITLE_CAP - 1) return;

    bool last_space = false;
    if (cur > 0) last_space = (doc->title[cur - 1] == ' ');

    for (size_t i = 0; i < len && cur < TBB10AE_TITLE_CAP - 1; i++) {
        unsigned char c = (unsigned char) data[i];
        if (tbb10ae_char_is_space(c)) {
            if (cur > 0 && !last_space && cur < TBB10AE_TITLE_CAP - 1) {
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

static void tbb10ae_extract_title_dom(lxb_dom_node_t *node, tbb10ae_doc_t *doc, bool in_title, int depth)
{
    if (node == NULL || doc == NULL || depth > 96) return;
    if (strlen(doc->title) >= TBB10AE_TITLE_CAP - 2) return;

    bool now_title = in_title;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        size_t nlen = 0;
        const lxb_char_t *name = lxb_dom_node_name(node, &nlen);
        if (tbb10ae_tag_eq(name, nlen, "title")) now_title = true;
    }

    if (now_title && node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *txt = lxb_dom_interface_text(node);
        if (txt != NULL && txt->char_data.data.data != NULL) {
            tbb10ae_title_append(doc, txt->char_data.data.data, txt->char_data.data.length);
        }
    }

    for (lxb_dom_node_t *child = node->first_child; child != NULL; child = child->next) {
        tbb10ae_extract_title_dom(child, doc, now_title, depth + 1);
    }
}

static void tbb10ae_walk_dom(lxb_dom_node_t *node, tbb10ae_doc_t *doc, int depth)
{
    if (node == NULL || doc == NULL || doc->stopped || depth > 96) return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *txt = lxb_dom_interface_text(node);
        if (txt != NULL && txt->char_data.data.data != NULL) {
            tbb10ae_text(doc, txt->char_data.data.data, txt->char_data.data.length);
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

        is_h1 = tbb10ae_tag_eq(name, nlen, "h1");
        is_h2 = tbb10ae_tag_eq(name, nlen, "h2");
        is_h3 = tbb10ae_tag_eq(name, nlen, "h3");
        is_p = tbb10ae_tag_eq(name, nlen, "p");
        is_li = tbb10ae_tag_eq(name, nlen, "li");
        is_br = tbb10ae_tag_eq(name, nlen, "br");
        is_a = tbb10ae_tag_eq(name, nlen, "a");
        is_pre = tbb10ae_tag_eq(name, nlen, "pre");
        is_script = tbb10ae_tag_eq(name, nlen, "script");
        is_style = tbb10ae_tag_eq(name, nlen, "style");
        is_title = tbb10ae_tag_eq(name, nlen, "title");

        if (is_script || is_style || is_title) return;

        if (is_br) {
            tbb10ae_newline(doc, true);
            return;
        }

        if (is_h1) {
            tbb10ae_newline(doc, false);
            tbb10ae_puts(doc, "# ");
        }
        else if (is_h2) {
            tbb10ae_newline(doc, false);
            tbb10ae_puts(doc, "## ");
        }
        else if (is_h3) {
            tbb10ae_newline(doc, false);
            tbb10ae_puts(doc, "### ");
        }
        else if (is_p || is_pre) {
            tbb10ae_newline(doc, false);
        }
        else if (is_li) {
            tbb10ae_newline(doc, false);
            tbb10ae_puts(doc, "- ");
        }

        if (is_a) {
            lxb_dom_element_t *el = lxb_dom_interface_element(node);
            if (el != NULL) {
                size_t href_len = 0;
                const lxb_char_t *href = lxb_dom_element_get_attribute(el, (const lxb_char_t *) "href", 4, &href_len);
                if (href != NULL && href_len > 0) {
                    link_id = tbb10ae_add_link(doc, href, href_len);
                    if (link_id > 0) {
                        char mark[20];
                        snprintf(mark, sizeof(mark), "[%d] ", link_id);
                        tbb10ae_puts(doc, mark);
                    }
                }
            }
        }
    }

    for (lxb_dom_node_t *child = node->first_child; child != NULL && !doc->stopped; child = child->next) {
        tbb10ae_walk_dom(child, doc, depth + 1);
    }

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        if (is_h1 || is_h2 || is_h3 || is_p || is_li || is_pre) {
            tbb10ae_newline(doc, false);
        }
    }
}

static char *tbb10ae_load_file(const char *path, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (path == NULL || path[0] == '\0') return NULL;

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("[TBBROWSER10AE][ERR] stat fallo: %s\n", path);
        return NULL;
    }

    if (st.st_size <= 0 || st.st_size > TBB10AE_FILE_MAX) {
        printf("[TBBROWSER10AE][ERR] tamano no valido: %ld bytes, max=%d\n",
               (long) st.st_size, TBB10AE_FILE_MAX);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("[TBBROWSER10AE][ERR] fopen fallo: %s\n", path);
        return NULL;
    }

    char *buf = (char *) heap_caps_malloc((size_t) st.st_size + 1,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) buf = (char *) heap_caps_malloc((size_t) st.st_size + 1, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        fclose(f);
        printf("[TBBROWSER10AE][ERR] sin memoria para archivo\n");
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t) st.st_size, f);
    fclose(f);
    buf[rd] = '\0';

    if (out_len) *out_len = rd;
    printf("[TBBROWSER10AE] archivo cargado %s bytes=%u\n", path, (unsigned) rd);
    return buf;
}

static const char *tbb10ae_sample_html(void)
{
    return "<!doctype html><html><head><title>Arielo MiniPC OS</title></head>"
           "<body>"
           "<h1>Arielo MiniPC OS</h1>"
           "<h2>TactileBrowser 10AE</h2>"
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

static int tbb10ae_build_doc_from_html(const char *label, const char *html, size_t html_len, tbb10ae_doc_t *out_doc)
{
    if (html == NULL) html = "";
    if (out_doc == NULL) return 1;

    tbb10ae_lexbor_memory_setup();
    tbb10ae_heap_print("before");

    printf("[TBBROWSER10AE] build begin label=%s len=%u\n", label ? label : "?", (unsigned) html_len);

    if (!tbb10ae_doc_alloc(out_doc)) {
        tbb10ae_heap_print("after_buferr");
        return 1;
    }

    lxb_html_document_t *doc = lxb_html_document_create();
    printf("[TBBROWSER10AE] doc=%p\n", (void *) doc);
    if (doc == NULL) {
        printf("[TBBROWSER10AE][ERR] create NULL\n");
        tbb10ae_doc_free(out_doc);
        tbb10ae_heap_print("after_null");
        return 1;
    }

    int64_t t0 = esp_timer_get_time();
    lxb_status_t st = lxb_html_document_parse(doc, (const lxb_char_t *) html, html_len);
    int64_t t1 = esp_timer_get_time();

    printf("[TBBROWSER10AE] parse status=%d time_us=%" PRId64 "\n", (int) st, (int64_t)(t1 - t0));

    if (st != LXB_STATUS_OK) {
        printf("[TBBROWSER10AE][ERR] parse failed\n");
        lxb_html_document_destroy(doc);
        tbb10ae_doc_free(out_doc);
        tbb10ae_heap_print("after_fail");
        return 1;
    }

    lxb_html_body_element_t *body = lxb_html_document_body_element(doc);
    lxb_dom_node_t *root = NULL;
    if (body != NULL) root = lxb_dom_interface_node(body);
    else root = lxb_dom_interface_node(doc);

    tbb10ae_extract_title_dom(lxb_dom_interface_node(doc), out_doc, false, 0);
    if (out_doc->title[0] == '\0' && label != NULL && label[0] != '\0') {
        tbb10ae_safe_copy(out_doc->title, sizeof(out_doc->title), label);
    }

    tbb10ae_walk_dom(root, out_doc, 0);
    tbb10ae_trim_trailing(out_doc);

    printf("[TBBROWSER10AE] DOM->lineas OK title='%s' lines=%d links=%d stopped=%d\n", out_doc->title, out_doc->count, out_doc->link_count, out_doc->stopped ? 1 : 0);

    lxb_html_document_destroy(doc);
    printf("[TBBROWSER10AE] destroy OK\n");
    tbb10ae_heap_print("after_build");
    return 0;
}

static void tbb10ae_clear_screen(void)
{
    /* VTerm suele aceptar ANSI. Si algun terminal no lo interpreta, no afecta a la prueba. */
    printf("\033[2J\033[H");
}

static void tbb10ae_pause_enter(const char *msg)
{
    if (msg != NULL && msg[0] != '\0') printf("%s", msg);
    else printf("Pulse ENTER para continuar... ");
    fflush(stdout);
    char tmp[32];
    (void) fgets(tmp, sizeof(tmp), stdin);
}

static void tbb10ae_show_help(void)
{
    printf("\n[TBBROWSER10AE] Controles:\n");
    printf("  n o ENTER : bajar una pagina\n");
    printf("  p         : subir una pagina\n");
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
    tbb10ae_pause_enter("Pulse ENTER para volver al navegador... ");
}

static void tbb10ae_show_links(tbb10ae_doc_t *doc)
{
    printf("\n[TBBROWSER10AE] Enlaces detectados: %d\n", doc ? doc->link_count : 0);
    if (doc != NULL) {
        for (int i = 0; i < doc->link_count; i++) {
            char *href = tbb10ae_link_ptr(doc, i);
            printf("  [%d] %s\n", i + 1, href ? href : "?");
        }
    }
    tbb10ae_pause_enter("Pulse ENTER para volver al navegador... ");
}


static void tbb10ae_chomp(char *s);

static void tbb10ae_bookmark_sanitize_field(const char *in, char *out, size_t out_cap)
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

static bool tbb10ae_bookmark_path_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') return false;

    FILE *f = fopen(TBB10AE_BOOKMARKS_FILE, "r");
    if (f == NULL) return false;

    char line[TBB10AE_PATH_CAP + TBB10AE_TITLE_CAP + 16];
    bool found = false;
    while (fgets(line, sizeof(line), f) != NULL) {
        tbb10ae_chomp(line);
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

static int tbb10ae_bookmark_add(const char *path, const char *title)
{
    if (path == NULL || path[0] == '\0' || strcmp(path, "sample") == 0) {
        printf("[TBBROWSER10AE] La pagina sample no se guarda como favorito. Abra un HTML real.\n");
        return 1;
    }

    char clean_path[TBB10AE_PATH_CAP];
    char clean_title[TBB10AE_TITLE_CAP];
    tbb10ae_bookmark_sanitize_field(path, clean_path, sizeof(clean_path));
    tbb10ae_bookmark_sanitize_field((title && title[0]) ? title : path, clean_title, sizeof(clean_title));

    if (clean_path[0] == '\0') {
        printf("[TBBROWSER10AE][ERR] ruta vacia, no guardo favorito\n");
        return 1;
    }

    if (tbb10ae_bookmark_path_exists(clean_path)) {
        printf("[TBBROWSER10AE] favorito ya existia: %s\n", clean_path);
        return 0;
    }

    FILE *f = fopen(TBB10AE_BOOKMARKS_FILE, "a");
    if (f == NULL) {
        printf("[TBBROWSER10AE][ERR] no pude abrir favoritos para escribir: %s\n", TBB10AE_BOOKMARKS_FILE);
        printf("[TBBROWSER10AE] Nota: compruebe que /root esta montado y con escritura.\n");
        return 1;
    }

    fprintf(f, "%s|%s\n", clean_path, clean_title[0] ? clean_title : clean_path);
    fclose(f);
    printf("[TBBROWSER10AE] favorito guardado: %s\n", clean_path);
    return 0;
}

static bool tbb10ae_bookmarks_choose(char *out_path, size_t out_cap)
{
    if (out_path == NULL || out_cap == 0) return false;
    out_path[0] = '\0';

    FILE *f = fopen(TBB10AE_BOOKMARKS_FILE, "r");
    if (f == NULL) {
        printf("[TBBROWSER10AE] No hay favoritos todavia: %s\n", TBB10AE_BOOKMARKS_FILE);
        tbb10ae_pause_enter("ENTER para volver... ");
        return false;
    }

    printf("\n[TBBROWSER10AE] Favoritos guardados en %s\n", TBB10AE_BOOKMARKS_FILE);
    char line[TBB10AE_PATH_CAP + TBB10AE_TITLE_CAP + 16];
    int count = 0;
    while (fgets(line, sizeof(line), f) != NULL && count < TBB10AE_BOOKMARKS_MAX) {
        tbb10ae_chomp(line);
        if (line[0] == '\0') continue;
        char path[TBB10AE_PATH_CAP];
        char title[TBB10AE_TITLE_CAP];
        char *sep = strchr(line, '|');
        if (sep != NULL) {
            *sep = '\0';
            tbb10ae_safe_copy(path, sizeof(path), line);
            tbb10ae_safe_copy(title, sizeof(title), sep + 1);
        }
        else {
            tbb10ae_safe_copy(path, sizeof(path), line);
            tbb10ae_safe_copy(title, sizeof(title), line);
        }
        count++;
        printf("  [%d] %s\n      %s\n", count, title[0] ? title : path, path);
    }
    fclose(f);

    if (count <= 0) {
        printf("[TBBROWSER10AE] Lista de favoritos vacia.\n");
        tbb10ae_pause_enter("ENTER para volver... ");
        return false;
    }

    printf("Abrir favorito numero, o ENTER para cancelar: ");
    fflush(stdout);
    char cmd[32];
    if (fgets(cmd, sizeof(cmd), stdin) == NULL) return false;
    tbb10ae_chomp(cmd);
    if (cmd[0] == '\0') return false;

    int wanted = atoi(cmd);
    if (wanted < 1 || wanted > count) {
        printf("[TBBROWSER10AE] favorito fuera de rango: %d\n", wanted);
        tbb10ae_pause_enter("ENTER para volver... ");
        return false;
    }

    f = fopen(TBB10AE_BOOKMARKS_FILE, "r");
    if (f == NULL) return false;

    int idx = 0;
    bool ok = false;
    while (fgets(line, sizeof(line), f) != NULL && idx < TBB10AE_BOOKMARKS_MAX) {
        tbb10ae_chomp(line);
        if (line[0] == '\0') continue;
        idx++;
        if (idx == wanted) {
            char *sep = strchr(line, '|');
            if (sep != NULL) *sep = '\0';
            tbb10ae_safe_copy(out_path, out_cap, line);
            ok = (out_path[0] != '\0');
            break;
        }
    }
    fclose(f);
    return ok;
}

static bool tbb10ae_is_digits(const char *s)
{
    if (s == NULL || s[0] == '\0') return false;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '\r' || s[i] == '\n') break;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

static void tbb10ae_chomp(char *s)
{
    if (s == NULL) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

static bool tbb10ae_is_external_url(const char *s)
{
    if (s == NULL) return false;
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0 ||
           strncmp(s, "mailto:", 7) == 0 || strncmp(s, "ftp://", 6) == 0;
}

static void tbb10ae_strip_fragment_query(char *s)
{
    if (s == NULL) return;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '#' || s[i] == '?') {
            s[i] = '\0';
            break;
        }
    }
}


static void tbb10ae_normalize_abs_path(char *path)
{
    if (path == NULL || path[0] != '/') return;

    char src[TBB10AE_PATH_CAP];
    tbb10ae_safe_copy(src, sizeof(src), path);

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
        tbb10ae_safe_append(path, TBB10AE_PATH_CAP, parts[i]);
        if (strlen(path) == before) break;
        if (i != count - 1) {
            tbb10ae_safe_append(path, TBB10AE_PATH_CAP, "/");
        }
    }
}

static bool tbb10ae_resolve_path(const char *current, const char *href, char *out, size_t out_cap)
{
    if (out == NULL || out_cap == 0) return false;
    out[0] = '\0';
    if (href == NULL || href[0] == '\0') return false;

    char tmp[TBB10AE_PATH_CAP];
    tbb10ae_safe_copy(tmp, sizeof(tmp), href);
    tbb10ae_chomp(tmp);
    tbb10ae_strip_fragment_query(tmp);
    if (tmp[0] == '\0') return false;

    if (tbb10ae_is_external_url(tmp)) {
        printf("[TBBROWSER10AE] URL externa aun no soportada: %s\n", tmp);
        return false;
    }

    if (tmp[0] == '/') {
        tbb10ae_safe_copy(out, out_cap, tmp);
        tbb10ae_normalize_abs_path(out);
        return true;
    }

    if (current != NULL && current[0] == '/') {
        char base[TBB10AE_PATH_CAP];
        tbb10ae_safe_copy(base, sizeof(base), current);
        char *slash = strrchr(base, '/');
        if (slash != NULL) slash[1] = '\0';
        else strcpy(base, "/");
        tbb10ae_safe_copy(out, out_cap, base);
        tbb10ae_safe_append(out, out_cap, tmp);
        tbb10ae_normalize_abs_path(out);
        return true;
    }

    tbb10ae_safe_copy(out, out_cap, "/sdcard/");
    tbb10ae_safe_append(out, out_cap, tmp);
    tbb10ae_normalize_abs_path(out);
    return true;
}

static void tbb10ae_draw_page(tbb10ae_doc_t *doc, const char *label, int top, int hist_count)
{
    if (doc == NULL) return;
    if (top < 0) top = 0;
    int max_top = doc->count - TBB10AE_PAGE_LINES;
    if (max_top < 0) max_top = 0;
    if (top > max_top) top = max_top;

    int end_line = (top + TBB10AE_PAGE_LINES < doc->count) ? (top + TBB10AE_PAGE_LINES) : doc->count;
    int pct = 100;
    if (max_top > 0) pct = (top * 100) / max_top;

    tbb10ae_clear_screen();
    printf("Arielo MiniPC OS - TBROWSER 10AE LOCAL HTML NAVIGATOR\n");
    printf("Titulo: %s\n", (doc->title[0] != '\0') ? doc->title : "(sin titulo)");
    printf("Ruta  : %s\n", label ? label : "sample");
    printf("Estado: lineas=%d vista=%d-%d scroll=%d%% links=%d back=%d | n/p j/k b l m v o r h q\n",
           doc->count, top + 1, end_line, pct, doc->link_count, hist_count);
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < TBB10AE_PAGE_LINES; i++) {
        int idx = top + i;
        if (idx < doc->count) {
            char *line = tbb10ae_line_ptr(doc, idx);
            printf("%s\n", line ? line : "");
        }
        else {
            printf("~\n");
        }
    }

    printf("--------------------------------------------------------------------------------\n");
}

static int tbb10ae_viewer_loop(tbb10ae_doc_t *doc, const char *label, int hist_count,
                                char *out_open_path, size_t out_cap, bool *out_reload, bool *out_back)
{
    if (out_open_path != NULL && out_cap > 0) out_open_path[0] = '\0';
    if (out_reload != NULL) *out_reload = false;
    if (out_back != NULL) *out_back = false;
    if (doc == NULL) return 1;

    int top = 0;
    int max_top = doc->count - TBB10AE_PAGE_LINES;
    if (max_top < 0) max_top = 0;

    char cmd[96];
    while (true) {
        if (top < 0) top = 0;
        if (top > max_top) top = max_top;

        tbb10ae_draw_page(doc, label, top, hist_count);
        printf("TBBROWSER10AE> ");
        fflush(stdout);

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            printf("\n[TBBROWSER10AE] stdin cerrado, salgo\n");
            break;
        }

        tbb10ae_chomp(cmd);
        char c = cmd[0];
        if (c == '\0' || c == '\n' || c == '\r' || c == 'n' || c == 'N' || c == ' ') {
            top += TBB10AE_PAGE_LINES;
        }
        else if (c == 'p' || c == 'P') {
            top -= TBB10AE_PAGE_LINES;
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
            tbb10ae_show_help();
        }
        else if (c == 'l' || c == 'L') {
            tbb10ae_show_links(doc);
        }
        else if (c == 'm' || c == 'M') {
            (void) tbb10ae_bookmark_add(label, (doc->title[0] != '\0') ? doc->title : label);
            tbb10ae_pause_enter("ENTER para volver al navegador... ");
        }
        else if (c == 'v' || c == 'V') {
            if (out_open_path != NULL && out_cap > 0) {
                if (tbb10ae_bookmarks_choose(out_open_path, out_cap)) {
                    return 2;
                }
            }
        }
        else if (c == 'r' || c == 'R') {
            if (out_reload != NULL) *out_reload = true;
            return 2;
        }
        else if (c == 'b' || c == 'B') {
            if (hist_count <= 0) {
                printf("[TBBROWSER10AE] no hay pagina anterior en historial\n");
                tbb10ae_pause_enter(NULL);
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
                tbb10ae_safe_copy(out_open_path, out_cap, p);
                return 2;
            }
        }
        else if (tbb10ae_is_digits(cmd)) {
            int n = atoi(cmd);
            if (n >= 1 && n <= doc->link_count) {
                char *href = tbb10ae_link_ptr(doc, n - 1);
                if (href != NULL && out_open_path != NULL && out_cap > 0) {
                    tbb10ae_safe_copy(out_open_path, out_cap, href);
                    return 2;
                }
            }
            else {
                printf("[TBBROWSER10AE] enlace fuera de rango: %d\n", n);
                tbb10ae_pause_enter(NULL);
            }
        }
        else if (c == 'q' || c == 'Q' || c == 27) {
            break;
        }
    }

    tbb10ae_clear_screen();
    printf("[TBBROWSER10AE] navegador cerrado.\n");
    return 0;
}

static int tbb10ae_build_and_view_once(const char *label, const char *html, size_t html_len, int hist_count,
                                      char *out_open_path, size_t out_cap, bool *out_reload, bool *out_back)
{
    tbb10ae_doc_t doc;
    int r = tbb10ae_build_doc_from_html(label, html, html_len, &doc);
    if (r != 0) return r;

    r = tbb10ae_viewer_loop(&doc, label, hist_count, out_open_path, out_cap, out_reload, out_back);
    tbb10ae_doc_free(&doc);
    tbb10ae_heap_print("after_view");
    return r;
}

static void tbb10ae_history_push(char hist[TBB10AE_HISTORY_MAX][TBB10AE_PATH_CAP], int *count, const char *path)
{
    if (hist == NULL || count == NULL || path == NULL || path[0] == '\0') return;

    if (*count >= TBB10AE_HISTORY_MAX) {
        /*
         * FIX 10AE-1:
         * GCC con -Werror=restrict avisa en snprintf(hist[i-1], ..., hist[i])
         * porque son filas del mismo array. Aunque en la practica son filas
         * distintas, usamos memmove() para desplazar el historial completo
         * de una vez y evitar cualquier duda de solape.
         */
        memmove(hist[0], hist[1], (TBB10AE_HISTORY_MAX - 1) * TBB10AE_PATH_CAP);
        hist[TBB10AE_HISTORY_MAX - 1][0] = '\0';
        *count = TBB10AE_HISTORY_MAX - 1;
    }

    tbb10ae_safe_copy(hist[*count], TBB10AE_PATH_CAP, path);
    (*count)++;
}

static bool tbb10ae_history_pop(char hist[TBB10AE_HISTORY_MAX][TBB10AE_PATH_CAP], int *count,
                                char *out, size_t out_cap)
{
    if (hist == NULL || count == NULL || out == NULL || out_cap == 0) return false;
    if (*count <= 0) return false;

    (*count)--;
    tbb10ae_safe_copy(out, out_cap, hist[*count]);
    hist[*count][0] = '\0';
    return true;
}

static int tbb10ae_browser_session(const char *initial_path, bool initial_sample)
{
    char current[TBB10AE_PATH_CAP];
    char next[TBB10AE_PATH_CAP];
    char history[TBB10AE_HISTORY_MAX][TBB10AE_PATH_CAP];
    bool sample = initial_sample;
    int history_count = 0;
    int guard = 0;

    memset(history, 0, sizeof(history));

    if (initial_path != NULL && initial_path[0] != '\0') {
        tbb10ae_safe_copy(current, sizeof(current), initial_path);
        tbb10ae_normalize_abs_path(current);
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
            const char *html = tbb10ae_sample_html();
            r = tbb10ae_build_and_view_once("sample", html, strlen(html), history_count,
                                            next, sizeof(next), &reload, &back);
        }
        else {
            size_t len = 0;
            char *html = tbb10ae_load_file(current, &len);
            if (html == NULL) {
                printf("[TBBROWSER10AE][ERR] no pude abrir: %s\n", current);
                tbb10ae_pause_enter(NULL);
                return 1;
            }
            r = tbb10ae_build_and_view_once(current, html, len, history_count,
                                            next, sizeof(next), &reload, &back);
            heap_caps_free(html);
        }

        if (r != 2) return r;

        if (reload) {
            continue;
        }

        if (back) {
            char prev[TBB10AE_PATH_CAP];
            if (tbb10ae_history_pop(history, &history_count, prev, sizeof(prev))) {
                tbb10ae_safe_copy(current, sizeof(current), prev);
                sample = (strcmp(current, "sample") == 0);
            }
            continue;
        }

        if (strcmp(next, "sample") == 0) {
            tbb10ae_history_push(history, &history_count, current);
            tbb10ae_safe_copy(current, sizeof(current), "sample");
            sample = true;
            continue;
        }

        char resolved[TBB10AE_PATH_CAP];
        if (!tbb10ae_resolve_path(sample ? NULL : current, next, resolved, sizeof(resolved))) {
            tbb10ae_pause_enter("No se pudo resolver/abrir ese enlace. ENTER... ");
            continue;
        }

        tbb10ae_history_push(history, &history_count, current);
        tbb10ae_safe_copy(current, sizeof(current), resolved);
        sample = false;
    }

    printf("[TBBROWSER10AE][WARN] limite de navegacion alcanzado\n");
    return 0;
}

int cmd_tbbrowser_lexbor_10ae(int argc, char **argv)
{
    printf("\n[TBBROWSER10AE] TactileBrowser LOCAL NAVIGATOR 10AE - no ELF externo\n");

    if (argc <= 1 || strcmp(argv[1], "sample") == 0) {
        return tbb10ae_browser_session(NULL, true);
    }

    if (strcmp(argv[1], "bookmarks") == 0 || strcmp(argv[1], "favs") == 0 || strcmp(argv[1], "favoritos") == 0) {
        char fav_path[TBB10AE_PATH_CAP];
        if (tbb10ae_bookmarks_choose(fav_path, sizeof(fav_path))) {
            return tbb10ae_browser_session(fav_path, false);
        }
        return 0;
    }

    if (strcmp(argv[1], "file") == 0) {
        if (argc < 3) {
            printf("Uso: tbbrowser file /sdcard/pagina.html\n");
            return 1;
        }
        return tbb10ae_browser_session(argv[2], false);
    }

    if (argv[1][0] == '/') {
        return tbb10ae_browser_session(argv[1], false);
    }

    printf("Uso: tbbrowser [sample|file PATH|/ruta/pagina.html|bookmarks]\n");
    return 0;
}
