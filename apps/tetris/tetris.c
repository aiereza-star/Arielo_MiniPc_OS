/*
 * tetris.c - Tetris clasico para Arielo MiniPC OS
 *
 * 10B_FIX:
 *   - Cambiado a SM_400X240, mismo modo estable usado por Snake/Pong.
 *   - Dibujo por zonas, sin limpiar toda la pantalla en cada frame.
 *   - Bucle ligero: solo redibuja cuando cambia el estado.
 *   - main() igual que Snake/Pong; buildelf.bat usa -Dmain=app_main.
 *
 * Controles:
 *   Flechas/A-D: mover
 *   Arriba/W/Z/C: rotar
 *   Abajo/S: caida rapida
 *   X/V/Espacio: caida instantanea
 *   P: pausa
 *   R: reiniciar
 *   Q/ESC: salir
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rgb_gfx.h"
#include "rgb_display.h"
#include "bt_keyboard.h"

#define SW 400
#define SH 240

#define BW 10
#define BH 20
#define CELL 10
#define BX 78
#define BY 22

#define COL_BG       0
#define COL_PANEL    1
#define COL_GRID     2
#define COL_TEXT     3
#define COL_WHITE    4
#define COL_RED      5
#define COL_CYAN     6
#define COL_YELLOW   7
#define COL_GREEN    8
#define COL_BLUE     9
#define COL_ORANGE   10
#define COL_PURPLE   11
#define COL_PINK     12

static const uint8_t piece_col[7] = {
    COL_CYAN, COL_YELLOW, COL_PURPLE, COL_GREEN, COL_RED, COL_BLUE, COL_ORANGE
};

static const int8_t shapes[7][4][4][2] = {
    {{{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}}, {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}}},
    {{{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}},
    {{{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}}},
    {{{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}}},
    {{{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}}},
    {{{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}}, {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}}},
    {{{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}}}
};

static uint8_t board[BH][BW];
static int cur_piece, next_piece, rot, px, py;
static int score, lines, level, paused, game_over;
static uint32_t rng_state = 0xA17E105U;
static uint8_t prev_keys[32];

static void setup_palette(void)
{
    static uint16_t pal[256];
    memset(pal, 0, sizeof(pal));
    pal[COL_BG]     = 0x0000;
    pal[COL_PANEL]  = 0x1082;
    pal[COL_GRID]   = 0x3186;
    pal[COL_TEXT]   = 0xC618;
    pal[COL_WHITE]  = 0xFFFF;
    pal[COL_RED]    = 0xF800;
    pal[COL_CYAN]   = 0x07FF;
    pal[COL_YELLOW] = 0xFFE0;
    pal[COL_GREEN]  = 0x07E0;
    pal[COL_BLUE]   = 0x001F;
    pal[COL_ORANGE] = 0xFD20;
    pal[COL_PURPLE] = 0x781F;
    pal[COL_PINK]   = 0xF81F;
    rgb_display_set_vga_palette(pal);
}

static uint32_t rnd32(void)
{
    rng_state = rng_state * 1664525U + 1013904223U;
    return rng_state;
}

static int rnd_piece(void)
{
    return (int)((rnd32() >> 16) % 7);
}

static int key_down(uint8_t key)
{
    return bt_keyboard_is_pressed(key) ? 1 : 0;
}

static int key_edge(uint8_t key)
{
    uint8_t mask = (uint8_t)(1U << (key & 7));
    uint8_t *byte = &prev_keys[key >> 3];
    int now = key_down(key);
    int was = ((*byte) & mask) != 0;
    if (now) *byte |= mask;
    else     *byte &= (uint8_t)~mask;
    return now && !was;
}

/* Fuente 5x7 basica, column-major. */
static uint8_t font_col(char c, int col)
{
    if (col < 0 || col >= 5) return 0;
    switch (c) {
        case '0': { static const uint8_t g[5]={0x3E,0x51,0x49,0x45,0x3E}; return g[col]; }
        case '1': { static const uint8_t g[5]={0x00,0x42,0x7F,0x40,0x00}; return g[col]; }
        case '2': { static const uint8_t g[5]={0x42,0x61,0x51,0x49,0x46}; return g[col]; }
        case '3': { static const uint8_t g[5]={0x21,0x41,0x45,0x4B,0x31}; return g[col]; }
        case '4': { static const uint8_t g[5]={0x18,0x14,0x12,0x7F,0x10}; return g[col]; }
        case '5': { static const uint8_t g[5]={0x27,0x45,0x45,0x45,0x39}; return g[col]; }
        case '6': { static const uint8_t g[5]={0x3C,0x4A,0x49,0x49,0x30}; return g[col]; }
        case '7': { static const uint8_t g[5]={0x01,0x71,0x09,0x05,0x03}; return g[col]; }
        case '8': { static const uint8_t g[5]={0x36,0x49,0x49,0x49,0x36}; return g[col]; }
        case '9': { static const uint8_t g[5]={0x06,0x49,0x49,0x29,0x1E}; return g[col]; }
        case 'A': { static const uint8_t g[5]={0x7E,0x11,0x11,0x11,0x7E}; return g[col]; }
        case 'B': { static const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x36}; return g[col]; }
        case 'C': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x22}; return g[col]; }
        case 'D': { static const uint8_t g[5]={0x7F,0x41,0x41,0x22,0x1C}; return g[col]; }
        case 'E': { static const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x41}; return g[col]; }
        case 'G': { static const uint8_t g[5]={0x3E,0x41,0x49,0x49,0x7A}; return g[col]; }
        case 'I': { static const uint8_t g[5]={0x00,0x41,0x7F,0x41,0x00}; return g[col]; }
        case 'L': { static const uint8_t g[5]={0x7F,0x40,0x40,0x40,0x40}; return g[col]; }
        case 'M': { static const uint8_t g[5]={0x7F,0x02,0x0C,0x02,0x7F}; return g[col]; }
        case 'N': { static const uint8_t g[5]={0x7F,0x04,0x08,0x10,0x7F}; return g[col]; }
        case 'O': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x3E}; return g[col]; }
        case 'P': { static const uint8_t g[5]={0x7F,0x09,0x09,0x09,0x06}; return g[col]; }
        case 'R': { static const uint8_t g[5]={0x7F,0x09,0x19,0x29,0x46}; return g[col]; }
        case 'S': { static const uint8_t g[5]={0x46,0x49,0x49,0x49,0x31}; return g[col]; }
        case 'T': { static const uint8_t g[5]={0x01,0x01,0x7F,0x01,0x01}; return g[col]; }
        case 'U': { static const uint8_t g[5]={0x3F,0x40,0x40,0x40,0x3F}; return g[col]; }
        case 'V': { static const uint8_t g[5]={0x1F,0x20,0x40,0x20,0x1F}; return g[col]; }
        case 'X': { static const uint8_t g[5]={0x63,0x14,0x08,0x14,0x63}; return g[col]; }
        case ':': { static const uint8_t g[5]={0x00,0x36,0x36,0x00,0x00}; return g[col]; }
        case '-': { static const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; return g[col]; }
        default: return 0;
    }
}

static void draw_text(int x, int y, const char *s, uint8_t color, int scale)
{
    int px0 = x;
    while (*s) {
        char c = *s++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c == '\n') { y += 9 * scale; px0 = x; continue; }
        if (c == ' ') { px0 += 4 * scale; continue; }
        for (int col = 0; col < 5; col++) {
            uint8_t bits = font_col(c, col);
            for (int row = 0; row < 7; row++) {
                if (bits & (1U << row)) {
                    rgb_gfx_rectfill(px0 + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        px0 += 6 * scale;
    }
}

static void draw_num(int x, int y, int n, uint8_t color)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", n);
    draw_text(x, y, buf, color, 1);
}

static int collide(int piece, int rr, int nx, int ny)
{
    for (int i = 0; i < 4; i++) {
        int x = nx + shapes[piece][rr][i][0];
        int y = ny + shapes[piece][rr][i][1];

        if (x < 0 || x >= BW || y >= BH) return 1;
        if (y >= 0 && board[y][x]) return 1;
    }
    return 0;
}

static void lock_piece(void)
{
    for (int i = 0; i < 4; i++) {
        int x = px + shapes[cur_piece][rot][i][0];
        int y = py + shapes[cur_piece][rot][i][1];
        if (x >= 0 && x < BW && y >= 0 && y < BH) {
            board[y][x] = piece_col[cur_piece];
        }
    }
}

static void clear_lines(void)
{
    int cleared = 0;
    for (int y = BH - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < BW; x++) {
            if (!board[y][x]) { full = 0; break; }
        }
        if (full) {
            for (int yy = y; yy > 0; yy--) {
                memcpy(board[yy], board[yy - 1], BW);
            }
            memset(board[0], 0, BW);
            cleared++;
            y++;
        }
    }

    if (cleared) {
        static const int points[5] = {0, 40, 100, 300, 1200};
        score += points[cleared] * level;
        lines += cleared;
        level = 1 + lines / 10;
    }
}

static void spawn_piece(void)
{
    cur_piece = next_piece;
    next_piece = rnd_piece();
    rot = 0;
    px = 3;
    py = 0;
    if (collide(cur_piece, rot, px, py)) game_over = 1;
}

static void new_game(void)
{
    memset(board, 0, sizeof(board));
    memset(prev_keys, 0, sizeof(prev_keys));
    score = 0;
    lines = 0;
    level = 1;
    paused = 0;
    game_over = 0;
    next_piece = rnd_piece();
    spawn_piece();
}

static void draw_cell_at(int gx, int gy, uint8_t color)
{
    int x = BX + gx * CELL;
    int y = BY + gy * CELL;
    rgb_gfx_rectfill(x, y, CELL - 1, CELL - 1, color ? color : COL_BG);
}

static void draw_piece_pixels(int piece, int rr, int gx, int gy, int ox, int oy, int cell)
{
    for (int i = 0; i < 4; i++) {
        int x = ox + (gx + shapes[piece][rr][i][0]) * cell;
        int y = oy + (gy + shapes[piece][rr][i][1]) * cell;
        rgb_gfx_rectfill(x, y, cell - 1, cell - 1, piece_col[piece]);
    }
}

static void draw_static_frame(void)
{
    rgb_gfx_clear(COL_BG);

    rgb_gfx_rectfill(0, 0, SW, 18, COL_PANEL);
    draw_text(8, 6, "ARIELO TETRIS 10B", COL_YELLOW, 1);

    rgb_gfx_rectfill(BX - 3, BY - 3, BW * CELL + 6, BH * CELL + 6, COL_GRID);
    rgb_gfx_rectfill(BX, BY, BW * CELL, BH * CELL, COL_BG);

    draw_text(210, 32, "NEXT", COL_TEXT, 1);
    rgb_gfx_rectfill(208, 46, 62, 48, COL_PANEL);

    draw_text(210, 108, "SCORE", COL_TEXT, 1);
    draw_text(210, 134, "LEVEL", COL_TEXT, 1);
    draw_text(210, 160, "LINES", COL_TEXT, 1);

    draw_text(300, 48, "Z ROT", COL_TEXT, 1);
    draw_text(300, 64, "X DROP", COL_TEXT, 1);
    draw_text(300, 80, "P PAUSE", COL_TEXT, 1);
    draw_text(300, 96, "Q EXIT", COL_TEXT, 1);
}

static void draw_board_and_piece(void)
{
    rgb_gfx_rectfill(BX, BY, BW * CELL, BH * CELL, COL_BG);

    for (int y = 0; y < BH; y++) {
        for (int x = 0; x < BW; x++) {
            if (board[y][x]) draw_cell_at(x, y, board[y][x]);
        }
    }

    if (!game_over) {
        draw_piece_pixels(cur_piece, rot, px, py, BX, BY, CELL);
    }
}

static void draw_side_info(void)
{
    rgb_gfx_rectfill(208, 46, 62, 48, COL_PANEL);
    draw_piece_pixels(next_piece, 0, 0, 0, 220, 54, 8);

    rgb_gfx_rectfill(210, 118, 70, 10, COL_BG);
    rgb_gfx_rectfill(210, 144, 70, 10, COL_BG);
    rgb_gfx_rectfill(210, 170, 70, 10, COL_BG);
    draw_num(210, 118, score, COL_WHITE);
    draw_num(210, 144, level, COL_WHITE);
    draw_num(210, 170, lines, COL_WHITE);
}

static void render_game(void)
{
    draw_board_and_piece();
    draw_side_info();

    rgb_gfx_rectfill(70, 224, 260, 12, COL_BG);
    if (paused) {
        draw_text(160, 226, "PAUSA", COL_YELLOW, 1);
    }
    if (game_over) {
        rgb_gfx_rectfill(120, 92, 160, 48, COL_PANEL);
        draw_text(150, 104, "GAME OVER", COL_RED, 1);
        draw_text(142, 122, "R RESTART", COL_WHITE, 1);
    }
}

static void rotate_piece(void)
{
    int nr = (rot + 1) & 3;
    if (!collide(cur_piece, nr, px, py)) { rot = nr; return; }
    if (!collide(cur_piece, nr, px - 1, py)) { px--; rot = nr; return; }
    if (!collide(cur_piece, nr, px + 1, py)) { px++; rot = nr; return; }
}

static void hard_drop(void)
{
    if (game_over) return;
    while (!collide(cur_piece, rot, px, py + 1)) {
        py++;
        score += 2;
    }
    lock_piece();
    clear_lines();
    spawn_piece();
}

int main(void)
{
    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("Tetris: no pudo entrar en SM_400X240\n");
        return 1;
    }

    setup_palette();
    new_game();
    draw_static_frame();
    render_game();

    int frame = 0;
    int running = 1;

    while (running) {
        int dirty = 0;

        if (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q)) {
            running = 0;
            break;
        }

        if (key_edge(BT_KEY_R)) {
            new_game();
            draw_static_frame();
            dirty = 1;
        }

        if (key_edge(BT_KEY_P)) {
            paused = !paused;
            dirty = 1;
        }

        if (!paused && !game_over) {
            if (key_edge(BT_KEY_LEFT) || key_edge(BT_KEY_A)) {
                if (!collide(cur_piece, rot, px - 1, py)) { px--; dirty = 1; }
            }
            if (key_edge(BT_KEY_RIGHT) || key_edge(BT_KEY_D)) {
                if (!collide(cur_piece, rot, px + 1, py)) { px++; dirty = 1; }
            }
            if (key_edge(BT_KEY_UP) || key_edge(BT_KEY_W) || key_edge(BT_KEY_Z) || key_edge(BT_KEY_C)) {
                rotate_piece();
                dirty = 1;
            }
            if (key_edge(BT_KEY_X) || key_edge(BT_KEY_V) || key_edge(BT_KEY_SPACE)) {
                hard_drop();
                dirty = 1;
            }

            int speed = 32 - (level - 1) * 3;
            if (speed < 6) speed = 6;
            if (key_down(BT_KEY_DOWN) || key_down(BT_KEY_S)) speed = 3;

            frame++;
            if (frame >= speed) {
                frame = 0;
                if (!collide(cur_piece, rot, px, py + 1)) {
                    py++;
                } else {
                    lock_piece();
                    clear_lines();
                    spawn_piece();
                }
                dirty = 1;
            }
        }

        if (dirty) {
            render_game();
        }

        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    printf("Tetris: score=%d lines=%d level=%d\n", score, lines, level);
    return 0;
}
