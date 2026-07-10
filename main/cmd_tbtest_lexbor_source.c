/*
 * Arielo MiniPC OS - Lexbor builtin source test 10Z
 *
 * Objetivo:
 *   Probar Lexbor compilado dentro del firmware normal ESP-IDF,
 *   usando el repo completo fuente. No es ELF externo.
 *
 * Comandos:
 *   tbtest
 *   tbtest create
 *   tbtest parse
 *   tbtest loop 50
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "lexbor/html/html.h"
#include "lexbor/core/lexbor.h"


/*
 * 10Z: force Lexbor dynamic allocations to PSRAM first.
 * The document constructor needs several medium-sized pools. If those land
 * only in internal heap, create() can fail before owner_document is fully
 * initialized. PSRAM is 8-bit capable on this board and is safe for these
 * parser/data structures.
 */
static void *tbtest_lexbor_malloc_10z(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbtest_lexbor_calloc_10z(size_t num, size_t size)
{
    size_t total = num * size;
    if (num != 0 && total / num != size) return NULL;

    void *p = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_calloc(num, size, MALLOC_CAP_8BIT);
    return p;
}

static void *tbtest_lexbor_realloc_10z(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == NULL) p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    return p;
}

static void tbtest_lexbor_free_10z(void *ptr)
{
    heap_caps_free(ptr);
}

static void tbtest_lexbor_memory_setup_10z(void)
{
    static bool done = false;
    if (done) return;

    lxb_status_t st = lexbor_memory_setup(tbtest_lexbor_malloc_10z,
                                          tbtest_lexbor_realloc_10z,
                                          tbtest_lexbor_calloc_10z,
                                          tbtest_lexbor_free_10z);
    printf("[TBTEST10Z] lexbor_memory_setup PSRAM-first st=%d\n", (int) st);
    done = true;
}

static void tbtest_heap_print_10z(const char *tag)
{
    printf("[TBTEST10Z] %-12s heap8=%u psram=%u min8=%u\n",
           tag,
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

static int tbtest_create_once_10z(void)
{
    tbtest_lexbor_memory_setup_10z();
    tbtest_heap_print_10z("before");

    printf("[TBTEST10Z] create begin\n");
    lxb_html_document_t *doc = lxb_html_document_create();
    printf("[TBTEST10Z] doc=%p\n", (void *) doc);

    if (doc == NULL) {
        printf("[TBTEST10Z][ERR] lxb_html_document_create returned NULL\n");
        tbtest_heap_print_10z("after_null");
        return 1;
    }

    printf("[TBTEST10Z] destroy begin\n");
    lxb_html_document_destroy(doc);
    printf("[TBTEST10Z] destroy OK\n");

    tbtest_heap_print_10z("after");
    return 0;
}

static int tbtest_parse_once_10z(void)
{
    tbtest_lexbor_memory_setup_10z();
    static const lxb_char_t html[] =
        "<!doctype html><html><head><title>Arielo</title></head>"
        "<body><h1>Lexbor builtin OK</h1><p>Prueba desde firmware normal.</p></body></html>";

    const size_t html_len = sizeof(html) - 1;

    tbtest_heap_print_10z("before");

    printf("[TBTEST10Z] parse create begin\n");
    lxb_html_document_t *doc = lxb_html_document_create();
    printf("[TBTEST10Z] doc=%p\n", (void *) doc);

    if (doc == NULL) {
        printf("[TBTEST10Z][ERR] create NULL\n");
        tbtest_heap_print_10z("after_null");
        return 1;
    }

    printf("[TBTEST10Z] parse begin len=%u\n", (unsigned) html_len);
    int64_t t0 = esp_timer_get_time();
    lxb_status_t st = lxb_html_document_parse(doc, html, html_len);
    int64_t t1 = esp_timer_get_time();

    printf("[TBTEST10Z] parse status=%d time_us=%" PRId64 "\n",
           (int) st, (int64_t)(t1 - t0));

    if (st != LXB_STATUS_OK) {
        printf("[TBTEST10Z][ERR] parse failed\n");
        lxb_html_document_destroy(doc);
        tbtest_heap_print_10z("after_fail");
        return 1;
    }

    printf("[TBTEST10Z] parse OK, destroy begin\n");
    lxb_html_document_destroy(doc);
    printf("[TBTEST10Z] destroy OK\n");

    tbtest_heap_print_10z("after");
    return 0;
}

static int tbtest_loop_10z(int n)
{
    if (n <= 0) n = 10;
    if (n > 500) n = 500;

    printf("[TBTEST10Z] loop parse n=%d\n", n);
    tbtest_heap_print_10z("loop_start");

    for (int i = 0; i < n; i++) {
        int r = tbtest_parse_once_10z();
        if (r != 0) {
            printf("[TBTEST10Z][ERR] loop failed at %d\n", i);
            return r;
        }
        if ((i % 10) == 0) {
            printf("[TBTEST10Z] loop step %d/%d\n", i + 1, n);
        }
    }

    tbtest_heap_print_10z("loop_end");
    printf("[TBTEST10Z] loop OK\n");
    return 0;
}

int cmd_tbtest_lexbor_source(int argc, char **argv)
{
    printf("\n[TBTEST10Z] Lexbor BUILTIN SOURCE test - no ELF externo\n");

    if (argc <= 1 || strcmp(argv[1], "create") == 0) {
        return tbtest_create_once_10z();
    }

    if (strcmp(argv[1], "parse") == 0) {
        return tbtest_parse_once_10z();
    }

    if (strcmp(argv[1], "loop") == 0) {
        int n = 50;
        if (argc >= 3) n = atoi(argv[2]);
        return tbtest_loop_10z(n);
    }

    printf("Uso: tbtest [create|parse|loop N]\n");
    return 0;
}
