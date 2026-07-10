/*
 * my_console_io.c - 04C_FIX7 input ENTER LF->CR, prompt margin fix
 *
 * 02C SAFE:
 * - Salida de consola solo por LCD/VTerm.
 * - USB Serial/JTAG NO se usa para escribir desde my_console_write().
 * - No se lee ni se escribe por USB Serial/JTAG desde la VFS.
 * - Entrada prevista: VTerm/teclado BLE; COM5 queda solo para logs IDF.
 * - Evita la recursión esp_log/vprintf -> my_console_write ->
 *   usb_serial_jtag_write_bytes -> esp_log/vprintf.
 */

#include "my_console_io.h"
#include "rgb_display.h"
#include "vterm.h"

#include "esp_vfs.h"
#include "esp_log.h"
#include "driver/usb_serial_jtag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if CONFIG_VFS_SUPPORT_TERMIOS
#include <termios.h>
#endif

#define CONSOLE_DEV_PATH "/dev/breezy"

// Track O_NONBLOCK flag per local fd.
// We support up to 4 open fds: stdin, stdout and stdio duplicates.
#define MAX_CONSOLE_FDS 4
static int s_fd_flags[MAX_CONSOLE_FDS] = {0};
static int s_next_local_fd = 0;

// 02C SAFE: salida por LCD. No arrancar en BOTH/USB.
static console_output_mode_t s_output_mode = CONSOLE_OUT_LCD;
static console_output_mode_t s_saved_output_mode = CONSOLE_OUT_LCD;

// 02C SAFE: USB output desactivado. La lectura USB se mantiene en read().
static int s_usb_connected = 0;
static int s_usb_fail_count = 0;
#define USB_FAIL_THRESHOLD 3

void my_console_bt_receive(char c)
{
    /*
     * 04C_FIX7:
     * La entrada de teclado USB puede llegar como LF ('\n').
     * Si se la pasamos directa a VTerm, la pantalla baja de linea pero
     * conserva la columna y los prompts "$" quedan escalonados.
     *
     * Para la terminal, ENTER debe entrar como CR ('\r').
     * my_console_read() ya convierte CR -> LF para que la shell ejecute.
     */
    if (c == '\n') {
        c = '\r';
    }

    vterm_input_feed(c);
}

int my_console_bt_active(void)
{
    return 0;
}

// ============ VFS Implementation ============

static ssize_t my_console_write(int fd, const void *data, size_t size)
{
    (void)fd;

    if (!data || size == 0) {
        return 0;
    }

    const char *str = (const char *)data;

    // En modo gráfico no se escribe al VTerm.
    // USB Serial/JTAG también queda desactivado en 02C SAFE.
    if (s_output_mode == CONSOLE_OUT_GFX) {
        return (ssize_t)size;
    }

    // 02C SAFE:
    // Aunque alguien ponga setcon usb/both, mantenemos salida LCD para no
    // quedarnos a ciegas ni reactivar la ruta recursiva de USB.
    int active = vterm_get_active();
    vterm_write(active, str, size);

    int col, row, visible;
    vterm_get_cursor(active, &col, &row, &visible);
    rgb_display_set_cursor(visible ? col : -1, row);

    return (ssize_t)size;
}

static ssize_t my_console_read(int fd, void *data, size_t size)
{
    char *buf = (char *)data;
    size_t count = 0;
    int idx = (fd >= 0 && fd < MAX_CONSOLE_FDS) ? fd : 0;
    int nonblock = (s_fd_flags[idx] & O_NONBLOCK);

    if (!buf || size == 0) {
        return 0;
    }

    while (count < size) {
        // 02C LCD_ONLY:
        // No leer USB Serial/JTAG aquí.
        // Si el driver USB Serial/JTAG no está instalado, usb_serial_jtag_read_bytes()
        // provoca LoadProhibited. La entrada queda por VTerm/BT keyboard.
#if 0
        char c;
        while (usb_serial_jtag_read_bytes(&c, 1, 0) > 0) {
            if (c == '\r') {
                c = '\n';
            }
            vterm_input_feed(c);
        }
#endif

        int active = vterm_get_active();

        // Primer char: espera breve; siguientes: sin espera.
        int timeout = (count == 0) ? (nonblock ? 0 : 50) : 0;
        int ch = vterm_getchar(active, timeout);

        if (ch >= 0) {
            if (ch == '\r') {
                ch = '\n';
            }

            buf[count++] = (char)ch;

            if (ch == '\n') {
                break;
            }
        } else {
            if (count > 0) {
                break;
            }

            if (nonblock) {
                errno = EAGAIN;
                return -1;
            }

            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    return (ssize_t)count;
}

static int my_console_open(const char *path, int flags, int mode)
{
    (void)path;
    (void)mode;

    int fd = s_next_local_fd;
    s_next_local_fd = (s_next_local_fd + 1) % MAX_CONSOLE_FDS;

    if (fd < MAX_CONSOLE_FDS) {
        s_fd_flags[fd] = flags & O_NONBLOCK;
    }

    return fd;
}

static int my_console_close(int fd)
{
    (void)fd;
    return 0;
}

static int my_console_fstat(int fd, struct stat *st)
{
    (void)fd;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR;
    return 0;
}

static int my_console_fsync(int fd)
{
    (void)fd;
    return 0;
}

#if CONFIG_VFS_SUPPORT_TERMIOS
static int my_console_tcsetattr(int fd, int optional_actions, const struct termios *p)
{
    (void)fd;
    (void)optional_actions;
    (void)p;
    return 0;
}

static int my_console_tcgetattr(int fd, struct termios *p)
{
    (void)fd;
    memset(p, 0, sizeof(*p));
    p->c_cflag = CS8;
    p->c_cc[VMIN] = 1;
    p->c_cc[VTIME] = 0;
    return 0;
}
#endif

static int my_console_fcntl(int fd, int cmd, int arg)
{
    int idx = (fd >= 0 && fd < MAX_CONSOLE_FDS) ? fd : 0;

    switch (cmd) {
    case F_GETFL:
        return s_fd_flags[idx];

    case F_SETFL:
        s_fd_flags[idx] = arg & O_NONBLOCK;
        return 0;

    default:
        return 0;
    }
}

// Callback cuando se cambia de VT.
// 02C SAFE: no escribe por USB; solo sincroniza cursor LCD.
static void on_vt_switch(int new_vt)
{
    int col, row, visible;
    vterm_get_cursor(new_vt, &col, &row, &visible);
    rgb_display_set_cursor(visible ? col : -1, row);
}

// 02C SAFE: no probar USB escribiendo en el arranque.
// El crash anterior entraba por probe_usb_connection() y usb_serial_jtag_write_bytes().
static void probe_usb_connection(void)
{
    s_usb_connected = 0;
    s_usb_fail_count = USB_FAIL_THRESHOLD;
}

// --- Display component callbacks ---
// Bridge display component back to VTerm + console I/O.

static void my_console_enter_graphics_mode_internal(void)
{
    s_saved_output_mode = s_output_mode;
    s_output_mode = CONSOLE_OUT_GFX;
}

static void my_console_exit_graphics_mode_internal(void)
{
    s_output_mode = s_saved_output_mode;

    int col, row, visible;
    vterm_get_cursor(vterm_get_active(), &col, &row, &visible);
    rgb_display_set_cursor(visible ? col : -1, row);
}

static const uint16_t *display_cb_get_text_palette(void)
{
    return vterm_get_palette();
}

static int display_cb_enter_graphics(void)
{
    vterm_enter_graphics_mode();
    my_console_enter_graphics_mode_internal();
    return 0;
}

static int display_cb_exit_graphics(void)
{
    vterm_exit_graphics_mode();
    my_console_exit_graphics_mode_internal();
    return 0;
}

static lcd_cell_t *display_cb_get_text_buffer(void)
{
    return (lcd_cell_t *)vterm_get_direct_buffer();
}

static void display_cb_flush_input(void)
{
    vterm_input_flush(vterm_get_active());
}

static const rgb_display_callbacks_t s_display_cbs = {
    .get_text_palette = display_cb_get_text_palette,
    .enter_graphics   = display_cb_enter_graphics,
    .exit_graphics    = display_cb_exit_graphics,
    .get_text_buffer  = display_cb_get_text_buffer,
    .flush_input      = display_cb_flush_input,
};

esp_err_t my_console_init(void)
{
    esp_err_t ret = vterm_init();
    if (ret != ESP_OK) {
        return ret;
    }

    // Link vterm buffer directly to display (zero-copy).
    // vterm_cell_t and lcd_cell_t have identical layout.
    vterm_cell_t *buf = vterm_get_direct_buffer();
    if (buf) {
        rgb_display_set_buffer((lcd_cell_t *)buf);
    }

    rgb_display_set_callbacks(&s_display_cbs);

    int col, row, visible;
    vterm_get_cursor(vterm_get_active(), &col, &row, &visible);
    rgb_display_set_cursor(visible ? col : -1, row);

    vterm_set_switch_callback(on_vt_switch);

    esp_vfs_t vfs = {
        .flags = ESP_VFS_FLAG_DEFAULT,
        .write = my_console_write,
        .read = my_console_read,
        .open = my_console_open,
        .close = my_console_close,
        .fstat = my_console_fstat,
        .fcntl = my_console_fcntl,
        .fsync = my_console_fsync,
#if CONFIG_VFS_SUPPORT_TERMIOS
        .tcsetattr = my_console_tcsetattr,
        .tcgetattr = my_console_tcgetattr,
#endif
    };

    ret = esp_vfs_register(CONSOLE_DEV_PATH, &vfs, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);

    int fd_in = open(CONSOLE_DEV_PATH, O_RDONLY);
    int fd_out = open(CONSOLE_DEV_PATH, O_WRONLY);

    if (fd_in != STDIN_FILENO || fd_out != STDOUT_FILENO) {
        if (fd_in >= 0) {
            close(fd_in);
        }
        if (fd_out >= 0) {
            close(fd_out);
        }
    }

    if (!freopen(CONSOLE_DEV_PATH, "r", stdin)) {
        return ESP_FAIL;
    }
    if (!freopen(CONSOLE_DEV_PATH, "w", stdout)) {
        return ESP_FAIL;
    }

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    // Mantener ESP_LOG en su salida por defecto. No usar esp_log_set_vprintf()
    // hacia my_console_write ni hacia USB para evitar recursión.
    probe_usb_connection();

    return ESP_OK;
}

void my_console_set_output_mode(console_output_mode_t mode)
{
    // 02C SAFE: aceptar el valor para compatibilidad con setcon, pero no
    // reactivar salida USB en my_console_write().
    if (mode == CONSOLE_OUT_GFX) {
        s_output_mode = CONSOLE_OUT_GFX;
    } else {
        s_output_mode = CONSOLE_OUT_LCD;
    }
}

console_output_mode_t my_console_get_output_mode(void)
{
    return s_output_mode;
}

int my_console_usb_connected(void)
{
    return s_usb_connected;
}

void my_console_usb_reconnect(void)
{
    // 02C SAFE: mantener salida USB deshabilitada.
    s_usb_connected = 0;
    s_usb_fail_count = USB_FAIL_THRESHOLD;
}

void my_console_enter_graphics_mode(void)
{
    my_console_enter_graphics_mode_internal();
}

void my_console_exit_graphics_mode(void)
{
    my_console_exit_graphics_mode_internal();
}
