
/*
 * Arielo MiniPC OS - TactileBrowser builtin render test 10AA
 *
 * Objetivo:
 *   Usar Lexbor integrado dentro del firmware normal ESP-IDF para:
 *     1) crear documento HTML
 *     2) parsear HTML
 *     3) recorrer DOM basico
 *     4) renderizar texto plano en la consola/LCD
 *
 * Nada de ELF externo. Nada de CSS. Nada de imagenes. Nada de URLs.
 * Esta es la primera prueba de render interno prudente tras la 10Z.
 *
 * Comandos:
 *   tbrender
 *   tbrender sample
 *   tbrender file /sdcard/test.html
 *   tbrender loop 20
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
#include "lexbor/html/interfaces/document.h"

#define TBR10AA_MAX_LINES       80
#define TBR10AA_WRAP_COL        92
#define TBR10AA_FILE_MAX        (16 * 1024)

static void *tbr10aa_lexbor_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbr10aa_lexbor_calloc(size_t num, size_t size)
{
    size_t total = num * size;
    if (num != 0 && total / num != size) return NULL;

    void *p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_calloc(num, size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbr10aa_lexbor_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    return p;
}

static void tbr10aa_lexbor_free(void *ptr)
{
    heap_caps_free(ptr);
}

static void tbr10aa_lexbor_memory_setup(void)
{
    static bool done = false;
    if (done) return;

    lxb_status_t st = lexbor_memory_setup(tbr10aa_lexbor_malloc,
                                          tbr10aa_lexbor_realloc,
                                          tbr10aa_lexbor_calloc,
                                          tbr10aa_lexbor_free);
    printf("[TBRENDER10AA] lexbor_memory_setup PSRAM-first st=%d\n", (int) st);
    done = true;
}

static void tbr10aa_heap_print(const char *tag)
{
    printf("[TBRENDER10AA] %-12s heap8=%u psram=%u min8=%u\n",
           tag,
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

typedef struct {
    int col;
    int lines;
    bool last_space;
    bool stopped;
} tbr10aa_render_ctx_t;

static bool tbr10aa_char_is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

static bool tbr10aa_tag_eq(const lxb_char_t *name, size_t len, const char *lit)
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

static void tbr10aa_newline(tbr10aa_render_ctx_t *ctx, bool force)
{
    if (ctx == NULL || ctx->stopped) return;

    if (!force && ctx->col == 0) return;

    putchar('\n');
    ctx->col = 0;
    ctx->last_space = false;
    ctx->lines++;

    if (ctx->lines >= TBR10AA_MAX_LINES) {
        printf("[TBRENDER10AA] ... salida cortada por seguridad ...\n");
        ctx->stopped = true;
    }
}

static void tbr10aa_puts_wrap(tbr10aa_render_ctx_t *ctx, const char *s)
{
    if (ctx == NULL || s == NULL || ctx->stopped) return;
    while (*s && !ctx->stopped) {
        char c = *s++;
        if (c == '\n') {
            tbr10aa_newline(ctx, true);
            continue;
        }
        if (ctx->col >= TBR10AA_WRAP_COL) {
            tbr10aa_newline(ctx, true);
        }
        putchar(c);
        ctx->col++;
        ctx->last_space = (c == ' ');
    }
}

static void tbr10aa_text(tbr10aa_render_ctx_t *ctx, const lxb_char_t *data, size_t len)
{
    if (ctx == NULL || data == NULL || len == 0 || ctx->stopped) return;

    for (size_t i = 0; i < len && !ctx->stopped; i++) {
        unsigned char c = (unsigned char) data[i];

        if (tbr10aa_char_is_space(c)) {
            if (ctx->col > 0 && !ctx->last_space) {
                if (ctx->col >= TBR10AA_WRAP_COL) tbr10aa_newline(ctx, true);
                if (!ctx->stopped) {
                    putchar(' ');
                    ctx->col++;
                    ctx->last_space = true;
                }
            }
            continue;
        }

        if (ctx->col >= TBR10AA_WRAP_COL) {
            tbr10aa_newline(ctx, true);
        }

        /* Para esta primera prueba mantenemos bytes UTF-8 tal cual. */
        putchar((char)c);
        ctx->col++;
        ctx->last_space = false;
    }
}

static void tbr10aa_walk_dom(lxb_dom_node_t *node, tbr10aa_render_ctx_t *ctx, int depth)
{
    if (node == NULL || ctx == NULL || ctx->stopped || depth > 64) return;

    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        lxb_dom_text_t *txt = lxb_dom_interface_text(node);
        if (txt != NULL && txt->char_data.data.data != NULL) {
            tbr10aa_text(ctx, txt->char_data.data.data, txt->char_data.data.length);
        }
        return;
    }

    bool is_h1 = false, is_h2 = false, is_p = false, is_li = false;
    bool is_br = false, is_script = false, is_style = false, is_title = false;

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        size_t nlen = 0;
        const lxb_char_t *name = lxb_dom_node_name(node, &nlen);

        is_h1 = tbr10aa_tag_eq(name, nlen, "h1");
        is_h2 = tbr10aa_tag_eq(name, nlen, "h2");
        is_p = tbr10aa_tag_eq(name, nlen, "p");
        is_li = tbr10aa_tag_eq(name, nlen, "li");
        is_br = tbr10aa_tag_eq(name, nlen, "br");
        is_script = tbr10aa_tag_eq(name, nlen, "script");
        is_style = tbr10aa_tag_eq(name, nlen, "style");
        is_title = tbr10aa_tag_eq(name, nlen, "title");

        if (is_script || is_style || is_title) return;

        if (is_br) {
            tbr10aa_newline(ctx, true);
            return;
        }

        if (is_h1) {
            tbr10aa_newline(ctx, false);
            tbr10aa_puts_wrap(ctx, "# ");
        }
        else if (is_h2) {
            tbr10aa_newline(ctx, false);
            tbr10aa_puts_wrap(ctx, "## ");
        }
        else if (is_p) {
            tbr10aa_newline(ctx, false);
        }
        else if (is_li) {
            tbr10aa_newline(ctx, false);
            tbr10aa_puts_wrap(ctx, "- ");
        }
    }

    for (lxb_dom_node_t *child = node->first_child; child != NULL && !ctx->stopped; child = child->next) {
        tbr10aa_walk_dom(child, ctx, depth + 1);
    }

    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        if (is_h1 || is_h2 || is_p || is_li) {
            tbr10aa_newline(ctx, false);
        }
    }
}

static int tbr10aa_render_html_buffer(const char *label, const char *html, size_t html_len)
{
    if (html == NULL) html = "";

    tbr10aa_lexbor_memory_setup();
    tbr10aa_heap_print("before");

    printf("[TBRENDER10AA] render begin label=%s len=%u\n",
           label ? label : "?", (unsigned) html_len);

    lxb_html_document_t *doc = lxb_html_document_create();
    printf("[TBRENDER10AA] doc=%p\n", (void *) doc);
    if (doc == NULL) {
        printf("[TBRENDER10AA][ERR] create NULL\n");
        tbr10aa_heap_print("after_null");
        return 1;
    }

    int64_t t0 = esp_timer_get_time();
    lxb_status_t st = lxb_html_document_parse(doc, (const lxb_char_t *) html, html_len);
    int64_t t1 = esp_timer_get_time();

    printf("[TBRENDER10AA] parse status=%d time_us=%" PRId64 "\n", (int) st, (int64_t)(t1 - t0));

    if (st != LXB_STATUS_OK) {
        printf("[TBRENDER10AA][ERR] parse failed\n");
        lxb_html_document_destroy(doc);
        tbr10aa_heap_print("after_fail");
        return 1;
    }

    lxb_html_body_element_t *body = lxb_html_document_body_element(doc);
    lxb_dom_node_t *root = NULL;
    if (body != NULL) {
        root = lxb_dom_interface_node(body);
    }
    else {
        root = lxb_dom_interface_node(doc);
    }

    printf("\n========== TBROWSER 10AA RENDER ==========%s%s\n",
           label ? " " : "", label ? label : "");

    tbr10aa_render_ctx_t ctx = {0};
    tbr10aa_walk_dom(root, &ctx, 0);
    tbr10aa_newline(&ctx, false);

    printf("========== FIN TBROWSER 10AA ==========\n\n");

    lxb_html_document_destroy(doc);
    printf("[TBRENDER10AA] destroy OK lines=%d\n", ctx.lines);
    tbr10aa_heap_print("after");
    return 0;
}

static int tbr10aa_render_sample(void)
{
    static const char html[] =
        "<!doctype html><html><head><title>Arielo MiniPC OS</title></head>"
        "<body>"
        "<h1>Arielo MiniPC OS</h1>"
        "<p>Lexbor integrado funcionando dentro del firmware normal.</p>"
        "<p>Primera prueba 10AA: parsear DOM y renderizar texto sin ELF externo.</p>"
        "<h2>Estado</h2>"
        "<ul>"
        "<li>HTML create OK</li>"
        "<li>HTML parse OK</li>"
        "<li>DOM walk OK</li>"
        "<li>Render texto plano OK</li>"
        "</ul>"
        "<p>Sin CSS, sin imagenes, sin URL parser. Paso prudente.</p>"
        "</body></html>";

    return tbr10aa_render_html_buffer("sample", html, sizeof(html) - 1);
}

static char *tbr10aa_load_file(const char *path, size_t *out_len)
{
    if (out_len) *out_len = 0;
    if (path == NULL || path[0] == '\0') return NULL;

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("[TBRENDER10AA][ERR] stat fallo: %s\n", path);
        return NULL;
    }

    if (st.st_size <= 0 || st.st_size > TBR10AA_FILE_MAX) {
        printf("[TBRENDER10AA][ERR] tamano no valido: %ld bytes, max=%d\n",
               (long) st.st_size, TBR10AA_FILE_MAX);
        return NULL;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("[TBRENDER10AA][ERR] fopen fallo: %s\n", path);
        return NULL;
    }

    char *buf = (char *) heap_caps_malloc((size_t) st.st_size + 1,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) buf = (char *) heap_caps_malloc((size_t) st.st_size + 1, MALLOC_CAP_8BIT);
    if (buf == NULL) {
        fclose(f);
        printf("[TBRENDER10AA][ERR] sin memoria para archivo\n");
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t) st.st_size, f);
    fclose(f);
    buf[rd] = '\0';

    if (out_len) *out_len = rd;
    printf("[TBRENDER10AA] archivo cargado %s bytes=%u\n", path, (unsigned) rd);
    return buf;
}

static int tbr10aa_render_file(const char *path)
{
    size_t len = 0;
    char *html = tbr10aa_load_file(path, &len);
    if (html == NULL) return 1;

    int r = tbr10aa_render_html_buffer(path, html, len);
    heap_caps_free(html);
    return r;
}

static int tbr10aa_loop(int n)
{
    if (n <= 0) n = 10;
    if (n > 200) n = 200;

    printf("[TBRENDER10AA] loop render sample n=%d\n", n);
    for (int i = 0; i < n; i++) {
        int r = tbr10aa_render_sample();
        if (r != 0) {
            printf("[TBRENDER10AA][ERR] loop fallo en %d\n", i);
            return r;
        }
        printf("[TBRENDER10AA] loop step %d/%d OK\n", i + 1, n);
    }
    printf("[TBRENDER10AA] loop OK\n");
    return 0;
}

int cmd_tbrender_lexbor_10aa(int argc, char **argv)
{
    printf("\n[TBRENDER10AA] TactileBrowser BUILTIN render test - no ELF externo\n");

    if (argc <= 1 || strcmp(argv[1], "sample") == 0) {
        return tbr10aa_render_sample();
    }

    if (strcmp(argv[1], "file") == 0) {
        if (argc < 3) {
            printf("Uso: tbrender file /sdcard/pagina.html\n");
            return 1;
        }
        return tbr10aa_render_file(argv[2]);
    }

    if (strcmp(argv[1], "loop") == 0) {
        int n = 20;
        if (argc >= 3) n = atoi(argv[2]);
        return tbr10aa_loop(n);
    }

    /* Atajo: tbrender /sdcard/pagina.html */
    if (argv[1][0] == '/') {
        return tbr10aa_render_file(argv[1]);
    }

    printf("Uso: tbrender [sample|file PATH|loop N|/ruta/pagina.html]\n");
    return 0;
}
