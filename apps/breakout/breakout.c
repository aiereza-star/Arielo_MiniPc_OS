/*
 * breakout.c - Breakout clasico para Arielo MiniPC OS
 *
 * 10B_FIX:
 *   - Cambiado a SM_400X240.
 *   - Sin rgb_gfx_clear() por frame.
 *   - Dibujo delta: solo borra/redibuja pelota, pala y ladrillo roto.
 *   - Mucho menos parpadeo que la version 10A.
 *
 * Controles:
 *   Flechas/A-D: mover pala
 *   P/Espacio: pausa
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

#define COL_BG       0
#define COL_PANEL    1
#define COL_TEXT     2
#define COL_WHITE    3
#define COL_RED      4
#define COL_YELLOW   5
#define COL_GREEN    6
#define COL_BLUE     7
#define COL_CYAN     8
#define COL_ORANGE   9
#define COL_PURPLE   10

#define BR_ROWS 6
#define BR_COLS 12
#define BR_W    26
#define BR_H    10
#define BR_GAP  3
#define BR_X    26
#define BR_Y    34

#define PADDLE_Y 218
#define BALL_SZ  6

static uint8_t bricks[BR_ROWS][BR_COLS];
static int paddle_x, paddle_w, old_paddle_x;
static int ball_x, ball_y, old_ball_x, old_ball_y;
static int ball_dx, ball_dy;
static int score, lives, paused, game_over, win;
static uint8_t prev_keys[32];

static void setup_palette(void)
{
    static uint16_t pal[256];
    memset(pal, 0, sizeof(pal));
    pal[COL_BG]     = 0x0000;
    pal[COL_PANEL]  = 0x1082;
    pal[COL_TEXT]   = 0xC618;
    pal[COL_WHITE]  = 0xFFFF;
    pal[COL_RED]    = 0xF800;
    pal[COL_YELLOW] = 0xFFE0;
    pal[COL_GREEN]  = 0x07E0;
    pal[COL_BLUE]   = 0x001F;
    pal[COL_CYAN]   = 0x07FF;
    pal[COL_ORANGE] = 0xFD20;
    pal[COL_PURPLE] = 0x781F;
    rgb_display_set_vga_palette(pal);
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
        case 'K': { static const uint8_t g[5]={0x7F,0x08,0x14,0x22,0x41}; return g[col]; }
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
        case 'W': { static const uint8_t g[5]={0x3F,0x40,0x38,0x40,0x3F}; return g[col]; }
        case 'X': { static const uint8_t g[5]={0x63,0x14,0x08,0x14,0x63}; return g[col]; }
        case ':': { static const uint8_t g[5]={0x00,0x36,0x36,0x00,0x00}; return g[col]; }
        case '-': { static const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; return g[col]; }
        default: return 0;
    }
}

static void draw_text(int x, int y, const char *s, uint8_t color, int scale)
{
    int px = x;
    while (*s) {
        char c = *s++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c == ' ') { px += 4 * scale; continue; }
        for (int col = 0; col < 5; col++) {
            uint8_t bits = font_col(c, col);
            for (int row = 0; row < 7; row++) {
                if (bits & (1U << row)) {
                    rgb_gfx_rectfill(px + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        px += 6 * scale;
    }
}

static void draw_num(int x, int y, int n, uint8_t color)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", n);
    draw_text(x, y, buf, color, 1);
}

static uint8_t brick_color(int row)
{
    static const uint8_t c[BR_ROWS] = {
        COL_RED, COL_ORANGE, COL_YELLOW, COL_GREEN, COL_CYAN, COL_PURPLE
    };
    return c[row];
}

static void draw_hud(void)
{
    rgb_gfx_rectfill(0, 0, SW, 22, COL_PANEL);
    draw_text(8, 8, "ARIELO BREAKOUT 10B", COL_YELLOW, 1);
    draw_text(210, 8, "SCORE", COL_TEXT, 1);
    rgb_gfx_rectfill(246, 6, 54, 12, COL_PANEL);
    draw_num(246, 8, score, COL_WHITE);
    draw_text(318, 8, "LIVES", COL_TEXT, 1);
    rgb_gfx_rectfill(354, 6, 24, 12, COL_PANEL);
    draw_num(354, 8, lives, COL_WHITE);
}

static void draw_brick(int r, int c)
{
    int x = BR_X + c * (BR_W + BR_GAP);
    int y = BR_Y + r * (BR_H + BR_GAP);
    if (bricks[r][c]) {
        rgb_gfx_rectfill(x, y, BR_W, BR_H, brick_color(r));
        rgb_gfx_rectfill(x + 1, y + 1, BR_W - 2, 2, COL_WHITE);
    } else {
        rgb_gfx_rectfill(x, y, BR_W, BR_H, COL_BG);
    }
}

static void draw_all_bricks(void)
{
    for (int r = 0; r < BR_ROWS; r++) {
        for (int c = 0; c < BR_COLS; c++) {
            draw_brick(r, c);
        }
    }
}

static void draw_paddle(int x)
{
    rgb_gfx_rectfill(x, PADDLE_Y, paddle_w, 7, COL_WHITE);
    rgb_gfx_rectfill(x + 2, PADDLE_Y + 2, paddle_w - 4, 3, COL_CYAN);
}

static void erase_paddle(int x)
{
    rgb_gfx_rectfill(x, PADDLE_Y, paddle_w, 7, COL_BG);
}

static void draw_ball(int x, int y)
{
    rgb_gfx_rectfill(x, y, BALL_SZ, BALL_SZ, COL_YELLOW);
}

static void erase_ball(int x, int y)
{
    rgb_gfx_rectfill(x, y, BALL_SZ, BALL_SZ, COL_BG);
}

static int bricks_left(void)
{
    int n = 0;
    for (int r = 0; r < BR_ROWS; r++) {
        for (int c = 0; c < BR_COLS; c++) {
            if (bricks[r][c]) n++;
        }
    }
    return n;
}

static void reset_ball(void)
{
    ball_x = paddle_x + paddle_w / 2 - BALL_SZ / 2;
    ball_y = PADDLE_Y - 12;
    old_ball_x = ball_x;
    old_ball_y = ball_y;
    ball_dx = 3;
    ball_dy = -3;
}

static void new_game(void)
{
    memset(prev_keys, 0, sizeof(prev_keys));
    for (int r = 0; r < BR_ROWS; r++) {
        for (int c = 0; c < BR_COLS; c++) {
            bricks[r][c] = 1;
        }
    }

    paddle_w = 58;
    paddle_x = (SW - paddle_w) / 2;
    old_paddle_x = paddle_x;
    score = 0;
    lives = 3;
    paused = 0;
    game_over = 0;
    win = 0;
    reset_ball();
}

static void render_full(void)
{
    rgb_gfx_clear(COL_BG);
    draw_hud();
    draw_all_bricks();
    draw_paddle(paddle_x);
    draw_ball(ball_x, ball_y);
    draw_text(8, 230, "A/D OR ARROWS  P PAUSE  R RESET  Q EXIT", COL_TEXT, 1);
}

static void render_overlay(void)
{
    rgb_gfx_rectfill(118, 104, 168, 36, COL_PANEL);
    if (paused) {
        draw_text(164, 118, "PAUSA", COL_YELLOW, 1);
    } else if (game_over) {
        if (win) draw_text(160, 112, "YOU WIN", COL_GREEN, 1);
        else     draw_text(146, 112, "GAME OVER", COL_RED, 1);
        draw_text(144, 126, "R RESTART", COL_WHITE, 1);
    }
}

static void update_game(void)
{
    old_paddle_x = paddle_x;
    old_ball_x = ball_x;
    old_ball_y = ball_y;

    if (key_down(BT_KEY_LEFT) || key_down(BT_KEY_A))  paddle_x -= 6;
    if (key_down(BT_KEY_RIGHT) || key_down(BT_KEY_D)) paddle_x += 6;

    if (paddle_x < 6) paddle_x = 6;
    if (paddle_x + paddle_w > SW - 6) paddle_x = SW - 6 - paddle_w;

    ball_x += ball_dx;
    ball_y += ball_dy;

    if (ball_x < 4) { ball_x = 4; ball_dx = -ball_dx; }
    if (ball_x > SW - 4 - BALL_SZ) { ball_x = SW - 4 - BALL_SZ; ball_dx = -ball_dx; }
    if (ball_y < 24) { ball_y = 24; ball_dy = -ball_dy; }

    if (ball_dy > 0 &&
        ball_y + BALL_SZ >= PADDLE_Y &&
        ball_y <= PADDLE_Y + 7 &&
        ball_x + BALL_SZ >= paddle_x &&
        ball_x <= paddle_x + paddle_w) {

        ball_y = PADDLE_Y - BALL_SZ - 1;
        ball_dy = -ball_dy;

        int rel = (ball_x + BALL_SZ / 2) - (paddle_x + paddle_w / 2);
        ball_dx = rel / 8;
        if (ball_dx == 0) ball_dx = rel < 0 ? -1 : 1;
        if (ball_dx < -5) ball_dx = -5;
        if (ball_dx > 5) ball_dx = 5;
    }

    int hit_r = -1, hit_c = -1;
    for (int r = 0; r < BR_ROWS && hit_r < 0; r++) {
        for (int c = 0; c < BR_COLS; c++) {
            if (!bricks[r][c]) continue;
            int x = BR_X + c * (BR_W + BR_GAP);
            int y = BR_Y + r * (BR_H + BR_GAP);
            if (ball_x + BALL_SZ > x && ball_x < x + BR_W &&
                ball_y + BALL_SZ > y && ball_y < y + BR_H) {
                hit_r = r;
                hit_c = c;
                break;
            }
        }
    }

    if (hit_r >= 0) {
        bricks[hit_r][hit_c] = 0;
        score += (BR_ROWS - hit_r) * 10;
        ball_dy = -ball_dy;
        draw_brick(hit_r, hit_c);
        draw_hud();

        if (bricks_left() == 0) {
            win = 1;
            game_over = 1;
        }
    }

    if (ball_y > SH) {
        lives--;
        draw_hud();
        if (lives <= 0) {
            game_over = 1;
        } else {
            erase_ball(old_ball_x, old_ball_y);
            reset_ball();
            old_paddle_x = paddle_x;
            old_ball_x = ball_x;
            old_ball_y = ball_y;
            draw_ball(ball_x, ball_y);
        }
    }
}

static void render_delta(void)
{
    if (old_paddle_x != paddle_x) {
        erase_paddle(old_paddle_x);
        draw_paddle(paddle_x);
    }

    if (old_ball_x != ball_x || old_ball_y != ball_y) {
        erase_ball(old_ball_x, old_ball_y);
        draw_ball(ball_x, ball_y);
    }
}

int main(void)
{
    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("Breakout: no pudo entrar en SM_400X240\n");
        return 1;
    }

    setup_palette();
    new_game();
    render_full();

    int running = 1;
    int overlay_active = 0;

    while (running) {
        if (key_edge(BT_KEY_ESC) || key_edge(BT_KEY_Q)) {
            running = 0;
            break;
        }

        if (key_edge(BT_KEY_R)) {
            new_game();
            render_full();
            overlay_active = 0;
        }

        if (key_edge(BT_KEY_P) || key_edge(BT_KEY_SPACE)) {
            paused = !paused;
            render_full();
            overlay_active = 0;
        }

        if (!paused && !game_over) {
            update_game();
            render_delta();
        }

        if ((paused || game_over) && !overlay_active) {
            render_overlay();
            overlay_active = 1;
        }
        if (!paused && !game_over) {
            overlay_active = 0;
        }

        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    printf("Breakout: score=%d lives=%d\n", score, lives);
    return 0;
}
