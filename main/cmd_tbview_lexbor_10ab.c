/*
 * Arielo MiniPC OS - TactileBrowser builtin text viewer 10AB
 *
 * Base: 10AA_TACTILEBROWSER_BUILTIN_RENDER_TEST validada.
 *
 * Objetivo 10AB:
 *   - Mantener Lexbor integrado en firmware normal, sin ELF externo.
 *   - Cargar HTML desde sample / SD / USB / root.
 *   - Parsear con Lexbor.
 *   - Recorrer DOM basico.
 *   - Convertir a lineas de texto.
 *   - Mostrar un visor paginado con scroll basico por teclado.
 *
 * Comandos:
 *   tbview
 *   tbview sample
 *   tbview file /sdcard/test.html
 *   tbview /sdcard/test.html
 *
 * Controles dentro del visor:
 *   n / ENTER : bajar una pagina
 *   p         : subir una pagina
 *   j         : bajar una linea
 *   k         : subir una linea
 *   g         : ir al inicio
 *   G         : ir al final
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

#define TBV10AB_FILE_MAX          (32 * 1024)
#define TBV10AB_MAX_LINES         512
#define TBV10AB_LINE_CAP          112
#define TBV10AB_WRAP_COL          96
#define TBV10AB_PAGE_LINES        22

static void *tbv10ab_lexbor_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbv10ab_lexbor_calloc(size_t num, size_t size)
{
    size_t total = num * size;
    if (num != 0 && total / num != size) return NULL;

    void *p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_calloc(num, size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbv10ab_lexbor_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    return p;
}

static void tbv10ab_lexbor_free(void *ptr)
{
    heap_caps_free(ptr);
}

static void tbv10ab_lexbor_memory_setup(void)
{
    static bool done = false;
    if (done) return;

    lxb_status_t st = lexbor_memory_setup(tbv10ab_lexbor_malloc,
                                          tbv10ab_lexbor_realloc,
                                          tbv10ab_lexbor_calloc,
                                          tbv10ab_lexbor_free);
    printf("[TBVIEW10AB] lexbor_memory_setup PSRAM-first st=%d\n", (int) st);
    done = true;
}

static void tbv10ab_heap_print(const char *tag)
{
    printf("[TBVIEW10AB] %-12s heap8=%u psram=%u min8=%u\n",
           tag ? tag : "?",
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

typedef struct {
    char *buf;
    int max_lines;
    int line_cap;
    int count;
    int col;
    bool last_space;
    bool stopped;
} tbv10ab_doc_t;

static char *tbv10ab_line_ptr(tbv10ab_doc_t *doc, int idx)
{
    if (doc == NULL || doc->buf == NULL || idx < 0 || idx >= doc->max_lines) return NULL;
    return doc->buf + ((size_t) idx * (size_t) doc->line_cap);
}

static bool tbv10ab_doc_alloc(tbv10ab_doc_t *doc)
{
    if (doc == NULL) return false;
    memset(doc, 0, sizeof(*doc));

    doc->max_lines = TBV10AB_MAX_LINES;
    doc->line_cap = TBV10AB_LINE_CAP;

    size_t bytes = (size_t) doc->max_lines * (size_t) doc->line_cap;
    doc->buf = (char *) heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (doc->buf == NULL) {
        doc->buf = (char *) heap_caps_calloc(1, bytes, MALLOC_CAP_8BIT);
    }
    if (doc->buf == NULL) {
        printf("[TBVIEW10AB][ERR] sin memoria para line buffer bytes=%u\n", (unsigned) bytes);
        return false;
    }

    doc->count = 1;
    doc->col = 0;
    doc->last_space = false;
    doc->stopped = false;
    return true;
}

static void tbv10ab_doc_free(tbv10ab_doc_t *doc)
{
    if (doc == NULL) return;
    if (doc->buf != NULL) heap_caps_free(doc->buf);
    memset(doc, 0, sizeof(*doc));
}

static bool tbv10ab_char_is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static bool tbv10ab_tag_eq(const lxb_char_t *name, size_t len, const char *lit)
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

static void tbv10ab_newline(tbv10ab_doc_t *doc, bool force)
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

static void tbv10ab_put_char(tbv10ab_doc_t *doc, char c)
{
    if (doc == NULL || doc->stopped) return;
    if (doc->count <= 0) doc->count = 1;

    if (c == '\n') {
        tbv10ab_newline(doc, true);
        return;
    }

    if (doc->col >= TBV10AB_WRAP_COL || doc->col >= (doc->line_cap - 1)) {
        tbv10ab_newline(doc, true);
        if (doc->stopped) return;
    }

    char *line = tbv10ab_line_ptr(doc, doc->count - 1);
    if (line == NULL) {
        doc->stopped = true;
        return;
    }

    line[doc->col++] = c;
    line[doc->col] = '\0';
    doc->last_space = (c == ' ');
}

static void tbv10ab_puts(tbv10ab_doc_t *doc, const char *s)
{
    if (doc == NULL || s == NULL || doc->stopped) return;
    while (*s && !doc->stopped) {
        tbv10ab_put_char(doc, *s++);
    }
}

static void tbv10ab_text(tbv10ab_doc_t *doc, const lxb_char_t *data, size_t len)
{
    if (doc == NULL || data == NULL || len == 0 || doc->stopped) return;

    for (size_t i = 0; i < len && !doc->stopped; i++) {
        unsigned char c = (unsigned char) data[i];

        if (tbv10ab_char_is_space(c)) {
            if (doc->col > 0 && !doc->last_space) {
                tbv10ab_put_char(doc, ' ');
            }
            continue;
        }

        tbv10ab_put_char(doc, (char) c);
    }
}

static void tbv10ab_trim_trailing(tbv10ab_doc_t *doc)
{
    if (doc == NULL || doc->buf == NULL) return;

    while (doc->count > 1) {
        char *line = tbv10ab_line_ptr(doc, doc->count - 1);
        if (line == NULL || line[0] != '\0') break;
        doc->count--;
    }
}

static void tbv10ab_walk_dom(lxb_dom_node_t *node, tbv10ab_doc_t *doc, int depth)
{
    if (node == NULL || doc == NULL || doc->stopped || depth > 96) return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *txt = lxb_dom_interface_text(node);
        if (txt != NULL && txt->char_data.data.data != NULL) {
            tbv10ab_text(doc, txt->char_data.data.data, txt->char_data.data.length);
        }
        return;
    }

    bool is_h1 = false, is_h2 = false, is_h3 = false;
    bool is_p = false, is_li = false, is_br = false;
    bool is_script = false, is_style = false, is_title = false, is_pre = false;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        size_t nlen = 0;
        const lxb_char_t *name = lxb_dom_node_name(node, &nlen);

        is_h1 = tbv10ab_tag_eq(name, nlen, "h1");
        is_h2 = tbv10ab_tag_eq(name, nlen, "h2");
        is_h3 = tbv10ab_tag_eq(name, nlen, "h3");
        is_p = tbv10ab_tag_eq(name, nlen, "p");
        is_li = tbv10ab_tag_eq(name, nlen, "li");
        is_br = tbv10ab_tag_eq(name, nlen, "br");
        is_pre = tbv10ab_tag_eq(name, nlen, "pre");
        is_script = tbv10ab_tag_eq(name, nlen, "script");
        is_style = tbv10ab_tag_eq(name, nlen, "style");
        is_title = tbv10ab_tag_eq(name, nlen, "title");

        if (is_script || is_style || is_title) return;

        if (is_br) {
            tbv10ab_newline(doc, true);
            return;
        }

        if (is_h1) {
            tbv10ab_newline(doc, false);
            tbv10ab_puts(doc, "# ");
        }
        else if (is_h2) {
            tbv10ab_newline(doc, false);
            tbv10ab_puts(doc, "## ");
        }
        else if (is_h3) {
            tbv10ab_newline(doc, false);
            tbv10ab_puts(doc, "### ");
        }
        else if (is_p || is_pre) {
            tbv10ab_newline(doc, false);
        }
        else if (is_li) {
            tbv10ab_newline(doc, false);
            tbv10ab_puts(doc, "- ");
        }
    }

    for (lxb_dom_node_t *child = node->first_child; child != NULL && !doc->stopped; child = child->next) {
        tbv10ab_walk_dom(child, doc, depth + 1);
    }

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        if (is_h1 || is_h2 || is_h3 || is_p || is_li || is_pre) {
            tbv10ab_newline(doc, false);
        }
    }
}

static char *tbv10ab_load_file(const char *path, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (path == NULL || path[0] == '\0') return NULL;

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("[TBVIEW10AB][ERR] stat fallo: %s\n", path);
        return NULL;
    }

    if (st.st_size <= 0 || st.st_size > TBV10AB_FILE_MAX) {
        printf("[TBVIEW10AB][ERR] tamano no valido: %ld bytes, max=%d\n",
               (long) st.st_size, TBV10AB_FILE_MAX);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("[TBVIEW10AB][ERR] fopen fallo: %s\n", path);
        return NULL;
    }

    char *buf = (char *) heap_caps_malloc((size_t) st.st_size + 1,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) buf = (char *) heap_caps_malloc((size_t) st.st_size + 1, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        fclose(f);
        printf("[TBVIEW10AB][ERR] sin memoria para archivo\n");
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t) st.st_size, f);
    fclose(f);
    buf[rd] = '\0';

    if (out_len) *out_len = rd;
    printf("[TBVIEW10AB] archivo cargado %s bytes=%u\n", path, (unsigned) rd);
    return buf;
}

static const char *tbv10ab_sample_html(void)
{
    return "<!doctype html><html><head><title>Arielo MiniPC OS</title></head>"
           "<body>"
           "<h1>Arielo MiniPC OS</h1>"
           "<h2>TactileBrowser 10AB</h2>"
           "<p>Primer visor interno con scroll basico por teclado.</p>"
           "<p>Lexbor esta integrado en firmware normal, sin ELF externo.</p>"
           "<ul>"
           "<li>Carga HTML desde SD, USB o root.</li>"
           "<li>Parsea DOM con Lexbor.</li>"
           "<li>Convierte el DOM a lineas de texto.</li>"
           "<li>Muestra paginas y permite subir/bajar.</li>"
           "</ul>"
           "<h2>Controles</h2>"
           "<p>n o ENTER baja pagina. p sube pagina. j/k linea. g inicio. G final. q salir.</p>"
           "<p>Esta es una base prudente. Sin CSS, sin imagenes y sin URL parser.</p>"
           "</body></html>";
}

static int tbv10ab_build_doc_from_html(const char *label, const char *html, size_t html_len, tbv10ab_doc_t *out_doc)
{
    if (html == NULL) html = "";
    if (out_doc == NULL) return 1;

    tbv10ab_lexbor_memory_setup();
    tbv10ab_heap_print("before");

    printf("[TBVIEW10AB] build begin label=%s len=%u\n", label ? label : "?", (unsigned) html_len);

    if (!tbv10ab_doc_alloc(out_doc)) {
        tbv10ab_heap_print("after_buferr");
        return 1;
    }

    lxb_html_document_t *doc = lxb_html_document_create();
    printf("[TBVIEW10AB] doc=%p\n", (void *) doc);
    if (doc == NULL) {
        printf("[TBVIEW10AB][ERR] create NULL\n");
        tbv10ab_doc_free(out_doc);
        tbv10ab_heap_print("after_null");
        return 1;
    }

    int64_t t0 = esp_timer_get_time();
    lxb_status_t st = lxb_html_document_parse(doc, (const lxb_char_t *) html, html_len);
    int64_t t1 = esp_timer_get_time();

    printf("[TBVIEW10AB] parse status=%d time_us=%" PRId64 "\n", (int) st, (int64_t)(t1 - t0));

    if (st != LXB_STATUS_OK) {
        printf("[TBVIEW10AB][ERR] parse failed\n");
        lxb_html_document_destroy(doc);
        tbv10ab_doc_free(out_doc);
        tbv10ab_heap_print("after_fail");
        return 1;
    }

    lxb_html_body_element_t *body = lxb_html_document_body_element(doc);
    lxb_dom_node_t *root = NULL;
    if (body != NULL) root = lxb_dom_interface_node(body);
    else root = lxb_dom_interface_node(doc);

    tbv10ab_walk_dom(root, out_doc, 0);
    tbv10ab_trim_trailing(out_doc);

    printf("[TBVIEW10AB] DOM->lineas OK lines=%d stopped=%d\n", out_doc->count, out_doc->stopped ? 1 : 0);

    lxb_html_document_destroy(doc);
    printf("[TBVIEW10AB] destroy OK\n");
    tbv10ab_heap_print("after_build");
    return 0;
}

static void tbv10ab_clear_screen(void)
{
    /* VTerm suele aceptar ANSI. Si algun terminal no lo interpreta, no afecta a la prueba. */
    printf("\033[2J\033[H");
}

static void tbv10ab_show_help(void)
{
    printf("\n[TBVIEW10AB] Controles:\n");
    printf("  n o ENTER : bajar una pagina\n");
    printf("  p         : subir una pagina\n");
    printf("  j         : bajar una linea\n");
    printf("  k         : subir una linea\n");
    printf("  g         : inicio\n");
    printf("  G         : final\n");
    printf("  h o ?     : ayuda\n");
    printf("  q         : salir\n");
    printf("Pulse ENTER para volver al visor... ");
    fflush(stdout);
    char tmp[16];
    (void) fgets(tmp, sizeof(tmp), stdin);
}

static void tbv10ab_draw_page(tbv10ab_doc_t *doc, const char *label, int top)
{
    if (doc == NULL) return;
    if (top < 0) top = 0;
    int max_top = doc->count - TBV10AB_PAGE_LINES;
    if (max_top < 0) max_top = 0;
    if (top > max_top) top = max_top;

    tbv10ab_clear_screen();
    printf("Arielo MiniPC OS - TBROWSER 10AB TEXT VIEWER\n");
    printf("Archivo: %s | lineas=%d | vista %d-%d | n/p/j/k/g/G/q\n",
           label ? label : "sample",
           doc->count,
           top + 1,
           (top + TBV10AB_PAGE_LINES < doc->count) ? (top + TBV10AB_PAGE_LINES) : doc->count);
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < TBV10AB_PAGE_LINES; i++) {
        int idx = top + i;
        if (idx < doc->count) {
            char *line = tbv10ab_line_ptr(doc, idx);
            printf("%s\n", line ? line : "");
        }
        else {
            printf("~\n");
        }
    }

    printf("--------------------------------------------------------------------------------\n");
}

static int tbv10ab_viewer_loop(tbv10ab_doc_t *doc, const char *label)
{
    if (doc == NULL) return 1;

    int top = 0;
    int max_top = doc->count - TBV10AB_PAGE_LINES;
    if (max_top < 0) max_top = 0;

    char cmd[32];
    while (true) {
        if (top < 0) top = 0;
        if (top > max_top) top = max_top;

        tbv10ab_draw_page(doc, label, top);
        printf("TBVIEW10AB> ");
        fflush(stdout);

        if (fgets(cmd, sizeof(cmd), stdin) == NULL) {
            printf("\n[TBVIEW10AB] stdin cerrado, salgo\n");
            break;
        }

        char c = cmd[0];
        if (c == '\0' || c == '\n' || c == '\r' || c == 'n' || c == 'N' || c == ' ') {
            top += TBV10AB_PAGE_LINES;
        }
        else if (c == 'p' || c == 'P' || c == 'b' || c == 'B') {
            top -= TBV10AB_PAGE_LINES;
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
            tbv10ab_show_help();
        }
        else if (c == 'q' || c == 'Q' || c == 27) {
            break;
        }
    }

    tbv10ab_clear_screen();
    printf("[TBVIEW10AB] visor cerrado.\n");
    return 0;
}

static int tbv10ab_view_html(const char *label, const char *html, size_t html_len)
{
    tbv10ab_doc_t doc;
    int r = tbv10ab_build_doc_from_html(label, html, html_len, &doc);
    if (r != 0) return r;

    r = tbv10ab_viewer_loop(&doc, label);
    tbv10ab_doc_free(&doc);
    tbv10ab_heap_print("after_view");
    return r;
}

static int tbv10ab_view_sample(void)
{
    const char *html = tbv10ab_sample_html();
    return tbv10ab_view_html("sample", html, strlen(html));
}

static int tbv10ab_view_file(const char *path)
{
    size_t len = 0;
    char *html = tbv10ab_load_file(path, &len);
    if (html == NULL) return 1;

    int r = tbv10ab_view_html(path, html, len);
    heap_caps_free(html);
    return r;
}

int cmd_tbview_lexbor_10ab(int argc, char **argv)
{
    printf("\n[TBVIEW10AB] TactileBrowser TEXT VIEWER 10AB - no ELF externo\n");

    if (argc <= 1 || strcmp(argv[1], "sample") == 0) {
        return tbv10ab_view_sample();
    }

    if (strcmp(argv[1], "file") == 0) {
        if (argc < 3) {
            printf("Uso: tbview file /sdcard/pagina.html\n");
            return 1;
        }
        return tbv10ab_view_file(argv[2]);
    }

    if (argv[1][0] == '/') {
        return tbv10ab_view_file(argv[1]);
    }

    printf("Uso: tbview [sample|file PATH|/ruta/pagina.html]\n");
    return 0;
}
