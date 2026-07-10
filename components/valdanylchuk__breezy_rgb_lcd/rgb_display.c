/*
* rgb_display.c - Tuned Text Renderer + 05C Top Bar Viewport + Mouse Pointer + FIX2 HSHIFT
*
* Reads directly from interleaved lcd_cell_t buffer (IRAM).
* Optimized for 32-bit reads with 2-byte aligned cells.
* Uses configurable 16-color palette via callbacks.
*/

#include "rgb_display.h"
#include "rgb_gfx.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "display";

#define SCREEN_WIDTH    800
#define SCREEN_HEIGHT   480
#define BOUNCE_HEIGHT_PX 12  // 12 lines = 24KB bounce buffer (used by both text and graphics modes)
#define FONT_WIDTH      8
#define FONT_HEIGHT     16
#define TEXT_COLS       100
#define TEXT_ROWS       29

// Graphics mode constants adapted to Waveshare 800x480
// VGA13H and 150P are kept for compatibility, but centered safely.
// 03D uses SM_400X240: perfect 2x scale to 800x480.
#define GFX_VGA_WIDTH       320
#define GFX_VGA_HEIGHT      200
#define GFX_VGA_SIZE        (GFX_VGA_WIDTH * GFX_VGA_HEIGHT)      // 64000 bytes
#define GFX_VGA_SCALE       2
#define GFX_VGA_MARGIN_X    ((SCREEN_WIDTH  - (GFX_VGA_WIDTH  * GFX_VGA_SCALE)) / 2)   // 80
#define GFX_VGA_MARGIN_Y    ((SCREEN_HEIGHT - (GFX_VGA_HEIGHT * GFX_VGA_SCALE)) / 2)   // 40

#define GFX_150P_WIDTH      256
#define GFX_150P_HEIGHT     150
#define GFX_150P_SIZE       (GFX_150P_WIDTH * GFX_150P_HEIGHT)    // 38400 bytes
#define GFX_150P_SCALE      3
#define GFX_150P_MARGIN_X   ((SCREEN_WIDTH  - (GFX_150P_WIDTH  * GFX_150P_SCALE)) / 2) // 16
#define GFX_150P_MARGIN_Y   ((SCREEN_HEIGHT - (GFX_150P_HEIGHT * GFX_150P_SCALE)) / 2) // 15

#define GFX_400_WIDTH       400
#define GFX_400_HEIGHT      240
#define GFX_400_SIZE        (GFX_400_WIDTH * GFX_400_HEIGHT)      // 96000 bytes
#define GFX_400_SCALE       2
#define GFX_400_MARGIN_X    0
#define GFX_400_MARGIN_Y    0

// Current mode dimensions (set during mode switch)
static int s_gfx_width = 0;
static int s_gfx_height = 0;
static int s_gfx_scale = 0;
static int s_gfx_margin_x = 0;
static int s_gfx_margin_y = 0;

static inline bool is_graphics_mode(screen_mode_t mode)
{
    return mode == SM_VGA13H || mode == SM_150P || mode == SM_400X240;
}

// Pointer to external buffer (managed by caller, e.g. vterm)
static lcd_cell_t *s_display_buffer = NULL;

static esp_lcd_panel_handle_t panel_handle = NULL;

// Screen mode state
static screen_mode_t s_screen_mode = SM_TEXT;
static uint8_t *s_graphics_framebuffer = NULL;

// VSYNC synchronization
static SemaphoreHandle_t s_vsync_sem = NULL;
static volatile bool s_waiting_for_vsync = false;

// Cursor state (volatile for IRAM callback access)
static volatile int s_cursor_col = -1;  // -1 = hidden
static volatile int s_cursor_row = -1;
static uint32_t s_frame_count = 0;

// 05A GUI base: mouse pointer overlay.
// Pixel coordinates in physical LCD space: 0..799 / 0..479.
// Kept very small and ISR-safe: only a 12x18 arrow mask is blended into
// the current bounce line after text/graphics rendering.
#define MOUSE_PTR_W 12
#define MOUSE_PTR_H 18
static volatile int s_mouse_ptr_x = SCREEN_WIDTH / 2;
static volatile int s_mouse_ptr_y = SCREEN_HEIGHT / 2;
static volatile uint8_t s_mouse_ptr_buttons = 0;
static volatile int s_mouse_ptr_visible = 0;
// 10AN: cursor ocupado/reloj de arena mientras el navegador carga paginas.
static volatile int s_mouse_ptr_busy = 0;

// 05B GUI base: top status bar.
// This is a text-mode top viewport bar; it does not modify VTerm,
// its 100x30 geometry, or the display buffer. It is intentionally static and
// lightweight inside the RGB bounce callback.
#define TOPBAR_H 16
#define TOPBAR_TEXT_Y 0
static volatile int s_topbar_visible = 1;
static volatile int s_topbar_wifi = 1;
static volatile int s_topbar_sd = 1;
static volatile int s_topbar_usb = 0;

static const uint16_t s_mouse_ptr_mask[MOUSE_PTR_H] = {
    0x800, 0xC00, 0xA00, 0x900, 0x880, 0x840,
    0x820, 0x810, 0x808, 0x804, 0x87C, 0x890,
    0xA10, 0xC08, 0x808, 0x004, 0x004, 0x000
};

// 10AN: mascara simple de reloj de arena. Misma caja que el puntero (12x18),
// para que el ISR solo cambie de mascara sin memoria extra ni dibujo pesado.
static const uint16_t s_mouse_busy_mask[MOUSE_PTR_H] = {
    0xFFF, 0x801, 0x402, 0x204, 0x108, 0x090,
    0x060, 0x060, 0x090, 0x108, 0x204, 0x402,
    0x801, 0xFFF, 0x000, 0x000, 0x000, 0x000
};

// LUTs
static uint8_t font_ram[256][16];
static uint32_t BYTE_MASKS[256][4];
static const uint32_t MASK_LUT[4] = { 0x00000000, 0xFFFF0000, 0x0000FFFF, 0xFFFFFFFF };

// ATTR_LUT: precomputed bg32 and xor32 for each attribute byte
// ATTR_LUT[attr][0] = bg32, ATTR_LUT[attr][1] = xor32
static uint32_t ATTR_LUT[256][2];

// VGA 256-color palette (RGB565)
static uint16_t s_vga_palette[256];

// External font data
extern const uint8_t terminus16_glyph_bitmap[];

// Callbacks for terminal/console integration (optional)
static const rgb_display_callbacks_t *s_callbacks = NULL;

// Standard 16 CGA colors (RGB565)
static const uint16_t s_cga_colors[16] = {
    0x0000, // 0: Black
    0x0015, // 1: Blue
    0x0540, // 2: Green
    0x0555, // 3: Cyan
    0xA800, // 4: Red
    0xA815, // 5: Magenta
    0xA520, // 6: Brown (dark yellow)
    0xAD55, // 7: Light Gray
    0x52AA, // 8: Dark Gray
    0x52BF, // 9: Light Blue
    0x57EA, // 10: Light Green
    0x57FF, // 11: Light Cyan
    0xFAAA, // 12: Light Red
    0xFABF, // 13: Light Magenta
    0xFFE0, // 14: Yellow
    0xFFFF, // 15: White
};

static void init_vga_palette(void)
{
    // Copy CGA colors to first 16 entries
    memcpy(s_vga_palette, s_cga_colors, sizeof(s_cga_colors));

    // Generate 6x6x6 color cube for indices 16-231
    int idx = 16;
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                // Convert 0-5 levels to RGB565
                uint16_t r5 = (r * 51 * 31) / 255;  // 0-5 -> 0-31
                uint16_t g6 = (g * 51 * 63) / 255;  // 0-5 -> 0-63
                uint16_t b5 = (b * 51 * 31) / 255;  // 0-5 -> 0-31
                s_vga_palette[idx++] = (r5 << 11) | (g6 << 5) | b5;
            }
        }
    }

    // Generate 24-step grayscale for indices 232-255
    for (int i = 0; i < 24; i++) {
        int gray = 8 + i * 10;  // 8, 18, 28, ... 238
        uint16_t g5 = (gray * 31) / 255;
        uint16_t g6 = (gray * 63) / 255;
        s_vga_palette[232 + i] = (g5 << 11) | (g6 << 5) | g5;
    }
}

static int allocate_graphics_framebuffer(screen_mode_t mode)
{
    if (s_graphics_framebuffer != NULL) {
        return 0;  // Already allocated
    }

    // Determine size based on mode
    int fb_size;
    if (mode == SM_VGA13H) {
        fb_size = GFX_VGA_SIZE;
        s_gfx_width = GFX_VGA_WIDTH;
        s_gfx_height = GFX_VGA_HEIGHT;
        s_gfx_scale = GFX_VGA_SCALE;
        s_gfx_margin_x = GFX_VGA_MARGIN_X;
        s_gfx_margin_y = GFX_VGA_MARGIN_Y;
    } else if (mode == SM_150P) {
        fb_size = GFX_150P_SIZE;
        s_gfx_width = GFX_150P_WIDTH;
        s_gfx_height = GFX_150P_HEIGHT;
        s_gfx_scale = GFX_150P_SCALE;
        s_gfx_margin_x = GFX_150P_MARGIN_X;
        s_gfx_margin_y = GFX_150P_MARGIN_Y;
    } else if (mode == SM_400X240) {
        fb_size = GFX_400_SIZE;
        s_gfx_width = GFX_400_WIDTH;
        s_gfx_height = GFX_400_HEIGHT;
        s_gfx_scale = GFX_400_SCALE;
        s_gfx_margin_x = GFX_400_MARGIN_X;
        s_gfx_margin_y = GFX_400_MARGIN_Y;
    } else {
        ESP_LOGE(TAG, "Unknown graphics mode for allocation: %d", mode);
        return -1;
    }

    // Try internal RAM first (faster for DMA)
    s_graphics_framebuffer = heap_caps_malloc(fb_size,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

#ifdef CONFIG_SPIRAM
    if (!s_graphics_framebuffer) {
        // Fallback to PSRAM if internal RAM is tight
        ESP_LOGW(TAG, "Internal RAM tight, using PSRAM for framebuffer");
        s_graphics_framebuffer = heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
    }
#endif

    if (!s_graphics_framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate graphics framebuffer (%d bytes)", fb_size);
        return -1;
    }

    // Diagnostic: confirm memory region
    if (esp_ptr_internal(s_graphics_framebuffer)) {
        ESP_LOGI(TAG, "Framebuffer in INTERNAL RAM at %p (%d bytes)", s_graphics_framebuffer, fb_size);
    } else if (esp_ptr_external_ram(s_graphics_framebuffer)) {
        ESP_LOGW(TAG, "Framebuffer in PSRAM at %p (%d bytes) - vsync timing may be tight", s_graphics_framebuffer, fb_size);
    }

    // Clear to black (palette index 0)
    memset(s_graphics_framebuffer, 0, fb_size);
    return 0;
}

static void free_graphics_framebuffer(void)
{
    if (s_graphics_framebuffer) {
        heap_caps_free(s_graphics_framebuffer);
        s_graphics_framebuffer = NULL;
        ESP_LOGI(TAG, "Freed graphics framebuffer");
    }

    s_gfx_width = 0;
    s_gfx_height = 0;
    s_gfx_scale = 0;
    s_gfx_margin_x = 0;
    s_gfx_margin_y = 0;
}

static void rebuild_attr_lut(void)
{
    const uint16_t *palette = (s_callbacks && s_callbacks->get_text_palette)
        ? s_callbacks->get_text_palette()
        : s_cga_colors;

    for (int attr = 0; attr < 256; attr++) {
        uint8_t fg_idx = LCD_ATTR_FG(attr);
        uint8_t bg_idx = LCD_ATTR_BG(attr);

        uint16_t fg_color = palette[fg_idx];
        uint16_t bg_color = palette[bg_idx];

        uint32_t bg32 = (bg_color << 16) | bg_color;
        uint32_t fg32 = (fg_color << 16) | fg_color;

        ATTR_LUT[attr][0] = bg32;
        ATTR_LUT[attr][1] = fg32 ^ bg32;  // xor32
    }
}

static void precompute_tables(void)
{
    // Build ATTR_LUT from palette
    rebuild_attr_lut();

    // Pre-compute glyph byte to pixel masks
    for (int i = 0; i < 256; i++) {
        BYTE_MASKS[i][0] = MASK_LUT[(i >> 6) & 0x03];
        BYTE_MASKS[i][1] = MASK_LUT[(i >> 4) & 0x03];
        BYTE_MASKS[i][2] = MASK_LUT[(i >> 2) & 0x03];
        BYTE_MASKS[i][3] = MASK_LUT[i & 0x03];
    }
}



static inline void draw_text8_line(uint16_t *linebuf, int x, int y_in_glyph,
                                   const char *text, uint16_t fg, uint16_t bg)
{
    if (!linebuf || !text || y_in_glyph < 0 || y_in_glyph >= FONT_HEIGHT) return;

    int px = x;
    for (int i = 0; text[i] != '\0' && i < 80; i++) {
        unsigned char ch = (unsigned char)text[i];
        uint8_t glyph = font_ram[ch][y_in_glyph];

        for (int bit = 0; bit < 8; bit++) {
            int sx = px + bit;
            if (sx >= 0 && sx < SCREEN_WIDTH) {
                linebuf[sx] = (glyph & (0x80 >> bit)) ? fg : bg;
            }
        }
        px += FONT_WIDTH;
        if (px >= SCREEN_WIDTH) break;
    }
}

static inline void draw_topbar_line(uint16_t *linebuf, int lcd_y)
{
    if (!s_topbar_visible || !linebuf) return;
    if (lcd_y < 0 || lcd_y >= TOPBAR_H) return;

    // Calm dark header, subtle bottom separator.
    const uint16_t bg      = 0x0841;  // very dark blue/gray
    const uint16_t bg2     = 0x1082;  // slightly lighter strip
    const uint16_t sep     = 0x39E7;  // gray separator
    const uint16_t fg      = 0xFFFF;  // white
    const uint16_t muted   = 0xBDF7;  // light gray
    const uint16_t ok      = 0x07E0;  // green
    const uint16_t off     = 0x8410;  // gray
    const uint16_t warn    = 0xFFE0;  // yellow

    uint16_t fill = bg;
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        linebuf[x] = fill;
    }

    // Very thin separator. The last font line is normally empty in Terminus16,
    // so this keeps the bar compact and still clean.
    if (lcd_y == TOPBAR_H - 1) {
        for (int x = 0; x < SCREEN_WIDTH; x++) linebuf[x] = sep;
        return;
    }

    int gy = lcd_y - TOPBAR_TEXT_Y;
    if (gy >= 0 && gy < FONT_HEIGHT) {
        draw_text8_line(linebuf, 8, gy, "Arielo MiniPC OS", fg, fill);
        draw_text8_line(linebuf, 184, gy, "TEXT", muted, fill);

        // Right-side status, fixed positions to avoid strlen in callback.
        draw_text8_line(linebuf, 568, gy, "WiFi", s_topbar_wifi ? ok : off, fill);
        draw_text8_line(linebuf, 632, gy, "SD",   s_topbar_sd   ? ok : off, fill);
        draw_text8_line(linebuf, 680, gy, "USB",  s_topbar_usb  ? warn : off, fill);
        draw_text8_line(linebuf, 728, gy, "100x29", muted, fill);
    }
}

static inline bool mouse_ptr_mask_at(int x, int y, int busy)
{
    if (x < 0 || x >= MOUSE_PTR_W || y < 0 || y >= MOUSE_PTR_H) return false;
    const uint16_t *mask = busy ? s_mouse_busy_mask : s_mouse_ptr_mask;
    return (mask[y] & (1u << (MOUSE_PTR_W - 1 - x))) != 0;
}

static inline void draw_mouse_pointer_line(uint16_t *linebuf, int lcd_y)
{
    if (!s_mouse_ptr_visible || !linebuf) return;

    int px = s_mouse_ptr_x;
    int py = s_mouse_ptr_y;
    uint8_t buttons = s_mouse_ptr_buttons;
    int busy = s_mouse_ptr_busy;

    int ry = lcd_y - py;
    if (ry < -1 || ry > MOUSE_PTR_H) return;

    // 10AN: blanco normal, amarillo si se pulsa o si esta en modo ocupado.
    const uint16_t fill = (busy || (buttons & 0x01)) ? 0xFFE0 : 0xFFFF;
    const uint16_t outline = 0x0000;

    for (int rx = -1; rx <= MOUSE_PTR_W; rx++) {
        int sx = px + rx;
        if (sx < 0 || sx >= SCREEN_WIDTH) continue;

        bool body = mouse_ptr_mask_at(rx, ry, busy);
        bool edge = false;

        if (!body) {
            for (int oy = -1; oy <= 1 && !edge; oy++) {
                for (int ox = -1; ox <= 1; ox++) {
                    if (mouse_ptr_mask_at(rx + ox, ry + oy, busy)) {
                        edge = true;
                        break;
                    }
                }
            }
        }

        if (body) {
            linebuf[sx] = fill;
        } else if (edge) {
            linebuf[sx] = outline;
        }
    }
}

static IRAM_ATTR bool on_bounce_empty(esp_lcd_panel_handle_t panel, void *buf,
                                    int pos_px, int len_bytes, void *user_ctx)
{
    // Clear to black - also serves as fallback if nothing is ready
    memset(buf, 0, len_bytes);

    int y_start = pos_px / SCREEN_WIDTH;
    int num_lines = (len_bytes / 2) / SCREEN_WIDTH;

    // Frame counter for cursor blink (increment at start of each frame)
    if (y_start == 0) s_frame_count++;

    // === GRAPHICS MODE ===
    // Safe generic scaler for Waveshare 800x480.
    // Previous 1024x600 assumptions could write past the 800-pixel bounce line.
    uint8_t *graphics_fb_snapshot = s_graphics_framebuffer;
    if (is_graphics_mode(s_screen_mode) && graphics_fb_snapshot) {
        uint16_t *dest_base = (uint16_t *)buf;
        int gfx_width = s_gfx_width;
        int gfx_height = s_gfx_height;
        int gfx_scale = s_gfx_scale;
        int margin_x = s_gfx_margin_x;
        int margin_y = s_gfx_margin_y;

        if (gfx_width <= 0 || gfx_height <= 0 || gfx_scale <= 0) {
            return false;
        }

        for (int line = 0; line < num_lines; line++) {
            int lcd_y = y_start + line;
            uint16_t *linebuf = dest_base + (line * SCREEN_WIDTH);

            if (lcd_y < margin_y) {
                draw_mouse_pointer_line(linebuf, lcd_y);
                continue;
            }
            int src_y = (lcd_y - margin_y) / gfx_scale;
            if (src_y < 0 || src_y >= gfx_height) {
                draw_mouse_pointer_line(linebuf, lcd_y);
                continue;
            }

            if (margin_x >= SCREEN_WIDTH) {
                draw_mouse_pointer_line(linebuf, lcd_y);
                continue;
            }

            uint16_t *dest = linebuf + margin_x;
            int pixels_left = SCREEN_WIDTH - margin_x;

            const uint8_t *src_row = &graphics_fb_snapshot[src_y * gfx_width];

            /*
             * 03D_FIX6:
             * Fast path para SM_400X240: scale=2, margin_x=0.
             * En vez de 800 stores de 16 bits por linea LCD,
             * hacemos 400 stores de 32 bits (dos pixeles iguales).
             * Esto aligera mucho el callback del bounce buffer y reduce
             * underflow/tembleque/rayas.
             */
            if (gfx_scale == 2 && margin_x == 0 && pixels_left >= gfx_width * 2) {
                uint32_t *d32 = (uint32_t *)dest;
                for (int x = 0; x < gfx_width; x++) {
                    uint16_t color = s_vga_palette[src_row[x]];
                    d32[x] = ((uint32_t)color << 16) | color;
                }
            } else {
                for (int x = 0; x < gfx_width && pixels_left > 0; x++) {
                    uint16_t color = s_vga_palette[src_row[x]];
                    for (int rep = 0; rep < gfx_scale && pixels_left > 0; rep++) {
                        *dest++ = color;
                        pixels_left--;
                    }
                }
            }
            // Remaining right margin stays black from memset().
            draw_mouse_pointer_line(linebuf, lcd_y);
        }
        return false;
    }

    // === TEXT MODE (SM_TEXT) ===
    if (!s_display_buffer) return false;

    const lcd_cell_t *src_buf = s_display_buffer;

    // Cursor state: check once per callback
    int cursor_col = s_cursor_col;
    int cursor_row = s_cursor_row;
    // Blink at ~2Hz: frame_count >> 4 toggles every 16 frames (~0.5s at 30fps)
    int cursor_blink_on = (s_frame_count >> 4) & 1;

    for (int line = 0; line < num_lines; line++) {
        int y = y_start + line;
        uint16_t *linebuf = (uint16_t *)((uint8_t *)buf + (line * SCREEN_WIDTH * 2));

        /*
         * 05C_TEXT_VIEWPORT:
         * La barra superior deja de ser un simple overlay. Ahora consume TOPBAR_H
         * pixels reales y el terminal se renderiza debajo, alineado a 16 px.
         * VTerm sigue intacto en 100x30; aqui solo cambiamos la ventana visible.
         */
        int topbar_h = s_topbar_visible ? TOPBAR_H : 0;
        if (topbar_h && y < topbar_h) {
            draw_topbar_line(linebuf, y);
            draw_mouse_pointer_line(linebuf, y);
            continue;
        }

        int text_y = y - topbar_h;
        if (text_y < 0) {
            draw_mouse_pointer_line(linebuf, y);
            continue;
        }

        int text_row = text_y / FONT_HEIGHT;
        if (text_row >= TEXT_ROWS) {
            draw_mouse_pointer_line(linebuf, y);
            continue;
        }

        int glyph_y = text_y % FONT_HEIGHT;
        uint32_t *dest = (uint32_t *)linebuf;

        // Check if cursor should be drawn on this scanline (last 2 rows of glyph)
        int draw_cursor = (cursor_row >= 0 && text_row == cursor_row &&
                          glyph_y >= FONT_HEIGHT - 2 && cursor_blink_on);

        // Get pointer to the start of the row in the cell buffer
        const lcd_cell_t *cell_row_ptr = &src_buf[text_row * TEXT_COLS];

        // Process 2 cells at a time using 32-bit aligned reads
        // With 2-byte cells, reading 4 bytes gives us 2 cells
        const uint32_t *cell_pairs = (const uint32_t *)cell_row_ptr;

        for (int pair = 0; pair < TEXT_COLS / 2; pair++) {
            uint32_t cell_data = cell_pairs[pair];

            // Extract cell 0 (low 16 bits): ch in bits 0-7, attr in bits 8-15
            uint8_t ch0 = cell_data & 0xFF;
            uint8_t attr0 = (cell_data >> 8) & 0xFF;

            // Extract cell 1 (high 16 bits): ch in bits 16-23, attr in bits 24-31
            uint8_t ch1 = (cell_data >> 16) & 0xFF;
            uint8_t attr1 = (cell_data >> 24) & 0xFF;

            // --- Render cell 0 ---
            uint32_t bg32_0 = ATTR_LUT[attr0][0];
            uint32_t xor32_0 = ATTR_LUT[attr0][1];
            uint8_t glyph0 = font_ram[ch0][glyph_y];

            if (glyph0 == 0) {
                *dest++ = bg32_0; *dest++ = bg32_0; *dest++ = bg32_0; *dest++ = bg32_0;
            } else {
                const uint32_t *m = BYTE_MASKS[glyph0];
                *dest++ = (xor32_0 & m[0]) ^ bg32_0;
                *dest++ = (xor32_0 & m[1]) ^ bg32_0;
                *dest++ = (xor32_0 & m[2]) ^ bg32_0;
                *dest++ = (xor32_0 & m[3]) ^ bg32_0;
            }

            // Cursor underscore for cell 0
            if (draw_cursor && pair * 2 == cursor_col) {
                uint32_t fg32 = bg32_0 ^ xor32_0;
                dest[-4] = fg32; dest[-3] = fg32; dest[-2] = fg32; dest[-1] = fg32;
            }

            // --- Render cell 1 ---
            uint32_t bg32_1 = ATTR_LUT[attr1][0];
            uint32_t xor32_1 = ATTR_LUT[attr1][1];
            uint8_t glyph1 = font_ram[ch1][glyph_y];

            if (glyph1 == 0) {
                *dest++ = bg32_1; *dest++ = bg32_1; *dest++ = bg32_1; *dest++ = bg32_1;
            } else {
                const uint32_t *m = BYTE_MASKS[glyph1];
                *dest++ = (xor32_1 & m[0]) ^ bg32_1;
                *dest++ = (xor32_1 & m[1]) ^ bg32_1;
                *dest++ = (xor32_1 & m[2]) ^ bg32_1;
                *dest++ = (xor32_1 & m[3]) ^ bg32_1;
            }

            // Cursor underscore for cell 1
            if (draw_cursor && pair * 2 + 1 == cursor_col) {
                uint32_t fg32 = bg32_1 ^ xor32_1;
                dest[-4] = fg32; dest[-3] = fg32; dest[-2] = fg32; dest[-1] = fg32;
            }
        }

        draw_mouse_pointer_line(linebuf, y);
    }
    return false;
}

static IRAM_ATTR bool on_vsync(esp_lcd_panel_handle_t panel,
                                const esp_lcd_rgb_panel_event_data_t *edata,
                                void *user_ctx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (s_waiting_for_vsync && s_vsync_sem) {
        xSemaphoreGiveFromISR(s_vsync_sem, &xHigherPriorityTaskWoken);
        s_waiting_for_vsync = false;
    }
    return xHigherPriorityTaskWoken;
}

void rgb_display_init(void)
{
    ESP_LOGI(TAG, "Initializing RGB LCD (Bounce Buffer Text Mode - Zero Copy)");

    volatile const void *exports[] = { // for ELF binaries
        // Display API
        (void *)rgb_display_refresh_palette,
        (void *)rgb_display_set_mode,
        (void *)rgb_display_get_mode,
        (void *)rgb_display_get_framebuffer,
        (void *)rgb_display_get_fb_width,
        (void *)rgb_display_get_fb_height,
        (void *)rgb_display_set_vga_palette,
        (void *)rgb_display_set_vga_palette_entry,
        (void *)rgb_display_get_vga_palette_entry,
        (void *)rgb_display_wait_vsync,
        // Graphics primitives
        (void *)rgb_gfx_clear,
        (void *)rgb_gfx_pixel,
        (void *)rgb_gfx_hline,
        (void *)rgb_gfx_vline,
        (void *)rgb_gfx_rect,
        (void *)rgb_gfx_rectfill,
        (void *)rgb_gfx_blit,
        (void *)rgb_gfx_blit_flip,
    };
    (void)exports; // suppress unused warning

    // Initialize VGA palette before precomputing tables
    init_vga_palette();
    precompute_tables();

    // Load font to RAM
    memset(font_ram, 0, sizeof(font_ram));
    for (int i = 0x20; i < 0x100; i++)
        memcpy(font_ram[i], &terminus16_glyph_bitmap[(i - 0x20) * 16], 16);

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = 16 * 1000 * 1000,
            .h_res = SCREEN_WIDTH,
            .v_res = SCREEN_HEIGHT,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 4,
            .hsync_front_porch = 249,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 12,
            .vsync_front_porch = 22,
            .flags.pclk_active_neg = 1,
        },
        .data_width = 16,
        .bits_per_pixel = 16,
        .num_fbs = 0,
        .flags.no_fb = 1,
        .bounce_buffer_size_px = SCREEN_WIDTH * BOUNCE_HEIGHT_PX,
        .hsync_gpio_num = 46,
        .vsync_gpio_num = 3,
        .de_gpio_num = 5,
        .pclk_gpio_num = 7,
        .disp_gpio_num = -1,
        .data_gpio_nums = {14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40},
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    // Create vsync semaphore for graphics mode synchronization
    s_vsync_sem = xSemaphoreCreateBinary();

    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_bounce_empty = on_bounce_empty,
        .on_vsync = on_vsync,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    ESP_LOGI(TAG, "Display ready: %dx%d pixels, %dx%d chars",
            SCREEN_WIDTH, SCREEN_HEIGHT, TEXT_COLS, TEXT_ROWS);
}

void rgb_display_set_buffer(lcd_cell_t *cells)
{
    s_display_buffer = cells;
}

void rgb_display_set_callbacks(const rgb_display_callbacks_t *cb)
{
    s_callbacks = cb;
}

// Rebuild ATTR_LUT when palette changes
void rgb_display_refresh_palette(void)
{
    rebuild_attr_lut();
}

// Set cursor position for blinking underscore (-1 to hide)
void rgb_display_set_cursor(int col, int row)
{
    s_cursor_col = col;
    s_cursor_row = row;
}

void rgb_display_set_mouse_pointer(int x, int y, uint8_t buttons, int visible)
{
    if (x < 0) x = 0;
    if (x >= SCREEN_WIDTH) x = SCREEN_WIDTH - 1;
    if (y < 0) y = 0;
    if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;

    s_mouse_ptr_x = x;
    s_mouse_ptr_y = y;
    s_mouse_ptr_buttons = buttons;
    s_mouse_ptr_visible = visible ? 1 : 0;
}

void rgb_display_hide_mouse_pointer(void)
{
    s_mouse_ptr_visible = 0;
}

void rgb_display_set_mouse_busy(int busy)
{
    s_mouse_ptr_busy = busy ? 1 : 0;
}

void rgb_display_topbar_set_visible(int visible)
{
    s_topbar_visible = visible ? 1 : 0;
}

void rgb_display_topbar_set_usb(int present)
{
    s_topbar_usb = present ? 1 : 0;
}

void rgb_display_topbar_set_wifi(int connected)
{
    s_topbar_wifi = connected ? 1 : 0;
}

void rgb_display_topbar_set_sd(int mounted)
{
    s_topbar_sd = mounted ? 1 : 0;
}

// --- Screen Mode API ---

screen_mode_t rgb_display_get_mode(void)
{
    return s_screen_mode;
}

int rgb_display_set_mode(screen_mode_t mode)
{
    if (mode == s_screen_mode) {
        return 0;  // Already in this mode
    }

    if (is_graphics_mode(mode)) {
        // Notify external system to save text state and redirect console
        if (s_callbacks && s_callbacks->enter_graphics) {
            if (s_callbacks->enter_graphics() != 0) return -1;
        }

        // Switch to graphics mode
        if (allocate_graphics_framebuffer(mode) != 0) {
            // Rollback
            if (s_callbacks && s_callbacks->exit_graphics)
                s_callbacks->exit_graphics();
            return -1;
        }
        s_screen_mode = mode;
        s_display_buffer = NULL;  // Disable text buffer pointer
        ESP_LOGI(TAG, "Switched to graphics mode %d (%dx%d scale=%d margin=%d,%d)",
                mode, s_gfx_width, s_gfx_height, s_gfx_scale,
                s_gfx_margin_x, s_gfx_margin_y);
    }
    else if (mode == SM_TEXT) {
        // Switch back to text mode
        // 10AH_FIX2: primero impedir nuevas lecturas graficas desde el ISR,
        // luego dar un margen corto antes de liberar el framebuffer.
        // Evita carrera: ISR comprueba puntero no NULL y otro hilo lo libera.
        s_screen_mode = SM_TEXT;
        vTaskDelay(pdMS_TO_TICKS(20));
        free_graphics_framebuffer();

        // Notify external system to restore text state and console routing
        if (s_callbacks && s_callbacks->exit_graphics)
            s_callbacks->exit_graphics();

        // Re-link display buffer from external system
        if (s_callbacks && s_callbacks->get_text_buffer)
            s_display_buffer = s_callbacks->get_text_buffer();

        // Flush stale input accumulated during graphics mode
        if (s_callbacks && s_callbacks->flush_input)
            s_callbacks->flush_input();

        ESP_LOGI(TAG, "Switched to text mode");
    }
    else {
        ESP_LOGE(TAG, "Unknown screen mode: %d", mode);
        return -1;  // Unknown mode
    }

    return 0;
}

uint8_t *rgb_display_get_framebuffer(void)
{
    return s_graphics_framebuffer;
}

// --- VGA Palette API ---

void rgb_display_set_vga_palette(const uint16_t palette[256])
{
    memcpy(s_vga_palette, palette, sizeof(s_vga_palette));
}

void rgb_display_set_vga_palette_entry(int index, uint16_t rgb565)
{
    if (index >= 0 && index < 256) {
        s_vga_palette[index] = rgb565;
    }
}

uint16_t rgb_display_get_vga_palette_entry(int index)
{
    if (index >= 0 && index < 256) {
        return s_vga_palette[index];
    }
    return 0;
}

// --- VSYNC Synchronization ---

void rgb_display_wait_vsync(void)
{
    if (!is_graphics_mode(s_screen_mode) || !s_vsync_sem) return;
    s_waiting_for_vsync = true;
    xSemaphoreTake(s_vsync_sem, pdMS_TO_TICKS(100));  // Timeout ~2 frames
}

// --- Framebuffer Dimension Getters ---

int rgb_display_get_fb_width(void)
{
    return s_gfx_width;
}

int rgb_display_get_fb_height(void)
{
    return s_gfx_height;
}

