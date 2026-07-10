/*
 * pong.c - Pong clasico para Arielo MiniPC OS (BreezyBox ELF app)
 *
 * Un jugador contra la maquina (estructura preparada para anadir un modo
 * 2 jugadores mas adelante: solo haria falta leer BT_KEY_W/BT_KEY_S para
 * el segundo jugador en vez de mover la pala derecha por IA).
 *
 * Controles: flecha arriba / flecha abajo mueven tu pala (izquierda).
 * ESC sale.
 *
 * Mismo patron que snake.c: modo grafico SM_400X240, teclado via
 * bt_keyboard_is_pressed() (con fallback automatico a USB HID), y dibujo
 * "delta" (solo se repinta lo que cambia) para evitar parpadeo, ya que la
 * pantalla no tiene doble buffer.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rgb_gfx.h"
#include "rgb_display.h"
#include "bt_keyboard.h"

// ---------------- Configuracion del campo ----------------
#define FIELD_W     400
#define FIELD_H     240
#define HUD_H       26              // franja superior para el marcador

#define PAD_W       6
#define PAD_H       36
#define PAD_MARGIN  14              // distancia al borde lateral
#define PAD_SPEED   6               //3 px por tick

#define BALL_SIZE   6
#define WIN_SCORE   21              // partida hasta 21 puntos

#define COL_BG      0
#define COL_WALL    1
#define COL_TEXT    2
#define COL_PAD_L   3
#define COL_PAD_R   4
#define COL_BALL    5
#define COL_MID     6

typedef struct { int x, y; } point_t;

static void draw_text_small(int x, int y, const char *s, uint8_t color, int scale);

// ---------------- Estado del juego ----------------
static int s_pad_l_y, s_pad_r_y;          // Y de la esquina superior de cada pala
static int s_prev_pad_l_y, s_prev_pad_r_y;
static int s_ball_x, s_ball_y;
static int s_prev_ball_x, s_prev_ball_y;
static int s_ball_dx, s_ball_dy;
static int s_score_l, s_score_r;
static int s_ai_reaction;                 // "torpeza" de la IA (0=perfecta)
static int s_game_over;                   // 1 cuando alguien llega a WIN_SCORE
static int s_winner;                      // 1=jugador, 2=CPU

static void setup_palette(void)
{
    static uint16_t pal[256];
    memset(pal, 0, sizeof(pal));
    pal[COL_BG]    = 0x0000;                   // negro
    pal[COL_WALL]  = (10<<11) | (20<<5) | 25;  // azul grisaceo (HUD)
    pal[COL_TEXT]  = (31<<11) | (63<<5) | 31;  // blanco
    pal[COL_PAD_L] = (0<<11)  | (55<<5) | 31;  // cian
    pal[COL_PAD_R] = (31<<11) | (20<<5) | 8;   // naranja
    pal[COL_BALL]  = (31<<11) | (63<<5) | 31;  // blanco
    pal[COL_MID]   = (12<<11) | (24<<5) | 12;  // gris tenue (linea central)
    rgb_display_set_vga_palette(pal);
}

// -------- Fuente 5x7 compacta (column-major, bit0 = fila superior) --------
static uint8_t font_glyph_col(char c, int col)
{
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
        case 'D': { static const uint8_t g[5]={0x7F,0x41,0x41,0x22,0x1C}; return g[col]; }
        case 'G': { static const uint8_t g[5]={0x3E,0x41,0x49,0x49,0x7A}; return g[col]; }
        case 'I': { static const uint8_t g[5]={0x00,0x41,0x7F,0x41,0x00}; return g[col]; }
        case 'L': { static const uint8_t g[5]={0x7F,0x40,0x40,0x40,0x40}; return g[col]; }
        case 'N': { static const uint8_t g[5]={0x7F,0x04,0x08,0x10,0x7F}; return g[col]; }
        case 'C': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x22}; return g[col]; }
        case 'E': { static const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x41}; return g[col]; }
        case 'M': { static const uint8_t g[5]={0x7F,0x02,0x0C,0x02,0x7F}; return g[col]; }
        case 'O': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x3E}; return g[col]; }
        case 'P': { static const uint8_t g[5]={0x7F,0x09,0x09,0x09,0x06}; return g[col]; }
        case 'S': { static const uint8_t g[5]={0x46,0x49,0x49,0x49,0x31}; return g[col]; }
        case 'T': { static const uint8_t g[5]={0x01,0x01,0x7F,0x01,0x01}; return g[col]; }
        case 'U': { static const uint8_t g[5]={0x3F,0x40,0x40,0x40,0x3F}; return g[col]; }
        case 'W': { static const uint8_t g[5]={0x3F,0x40,0x38,0x40,0x3F}; return g[col]; }
        case 'Y': { static const uint8_t g[5]={0x07,0x08,0x70,0x08,0x07}; return g[col]; }
        case 'X': { static const uint8_t g[5]={0x63,0x14,0x08,0x14,0x63}; return g[col]; }
        case ':': { static const uint8_t g[5]={0x00,0x36,0x36,0x00,0x00}; return g[col]; }
        case '-': { static const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; return g[col]; }
        default:  return 0x00;
    }
}

static void draw_text_small(int x, int y, const char *s, uint8_t color, int scale)
{
    int px = x;
    while (*s) {
        char c = *s++;
        for (int col = 0; col < 5; col++) {
            uint8_t bits = font_glyph_col(c, col);
            for (int row = 0; row < 7; row++) {
                if (bits & (1 << row)) {
                    if (scale == 1) {
                        rgb_gfx_pixel(px + col, y + row, color);
                    } else {
                        rgb_gfx_rectfill(px + col * scale, y + row * scale, scale, scale, color);
                    }
                }
            }
        }
        px += 6 * scale;
    }
}

// ---------------- HUD ----------------
static void draw_hud_score(void)
{
    // Solo repintamos el area pequena del marcador, no toda la franja
    // (evita el parpadeo por "tearing" sin doble buffer, igual que Snake).
    rgb_gfx_rectfill(FIELD_W / 2 - 60, 4, 120, 20, COL_WALL);
    char buf[24];
    snprintf(buf, sizeof(buf), "%d - %d", s_score_l, s_score_r);
    int w = (int)strlen(buf) * 6 * 2;
    draw_text_small(FIELD_W / 2 - w / 2, 8, buf, COL_TEXT, 2);
}

static void new_ball(int dir)
{
    s_ball_x = FIELD_W / 2 - BALL_SIZE / 2;
    s_ball_y = HUD_H + (FIELD_H - HUD_H) / 2 - BALL_SIZE / 2;
    s_ball_dx = dir * 8;                      // antes: dir (±1) -> ahora ±3
    s_ball_dy = (rand() % 2) ? 6 : -6;         // antes: ±1 -> ahora ±2
}

static void new_game(void)
{
    s_pad_l_y = s_pad_r_y = HUD_H + (FIELD_H - HUD_H) / 2 - PAD_H / 2;
    s_prev_pad_l_y = s_pad_l_y;
    s_prev_pad_r_y = s_pad_r_y;
    s_score_l = s_score_r = 0;
    s_ai_reaction = 4;   // 0..~4: cuanto "tarda" en reaccionar la IA
    s_game_over = 0;
    s_winner = 0;
    new_ball(1);
}

static void draw_pad(int x, int y, uint8_t color)
{
    rgb_gfx_rectfill(x, y, PAD_W, PAD_H, color);
}

static void erase_pad(int x, int y)
{
    rgb_gfx_rectfill(x, y, PAD_W, PAD_H, COL_BG);
}

static void draw_mid_line(void)
{
    // Linea central discontinua, se pinta una sola vez en render_full.
    for (int y = HUD_H + 4; y < FIELD_H; y += 12) {
        rgb_gfx_rectfill(FIELD_W / 2 - 1, y, 2, 6, COL_MID);
    }
}

static void render_full(void)
{
    rgb_gfx_clear(COL_BG);
    rgb_gfx_rectfill(0, 0, FIELD_W, HUD_H, COL_WALL);
    draw_hud_score();
    draw_mid_line();
    draw_pad(PAD_MARGIN, s_pad_l_y, COL_PAD_L);
    draw_pad(FIELD_W - PAD_MARGIN - PAD_W, s_pad_r_y, COL_PAD_R);
    rgb_gfx_rectfill(s_ball_x, s_ball_y, BALL_SIZE, BALL_SIZE, COL_BALL);
    s_prev_pad_l_y = s_pad_l_y;
    s_prev_pad_r_y = s_pad_r_y;
    s_prev_ball_x = s_ball_x;
    s_prev_ball_y = s_ball_y;
}

static void draw_game_over_overlay(void)
{
    // Ventana final. No se limpia todo el campo; solo una caja central.
    rgb_gfx_rectfill(106, 84, 188, 72, COL_WALL);

    if (s_winner == 1) {
        draw_text_small(152, 98, "YOU WIN", COL_TEXT, 1);
    } else if (s_winner == 2) {
        draw_text_small(156, 98, "CPU WIN", COL_TEXT, 1);
    } else {
        draw_text_small(158, 98, "GAME OVER", COL_TEXT, 1);
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d - %d", s_score_l, s_score_r);
    int w = (int)strlen(buf) * 6 * 2;
    draw_text_small(FIELD_W / 2 - w / 2, 114, buf, COL_TEXT, 2);

    draw_text_small(136, 140, "R RESET  ESC EXIT", COL_TEXT, 1);
}

static int check_match_point(void)
{
    if (s_score_l >= WIN_SCORE) {
        s_score_l = WIN_SCORE;
        s_game_over = 1;
        s_winner = 1;
        return 1;
    }

    if (s_score_r >= WIN_SCORE) {
        s_score_r = WIN_SCORE;
        s_game_over = 1;
        s_winner = 2;
        return 1;
    }

    return 0;
}

static void read_input(void)
{
    if (bt_keyboard_is_pressed(BT_KEY_UP)) {
        s_pad_l_y -= PAD_SPEED;
    } else if (bt_keyboard_is_pressed(BT_KEY_DOWN)) {
        s_pad_l_y += PAD_SPEED;
    }
    if (s_pad_l_y < HUD_H) s_pad_l_y = HUD_H;
    if (s_pad_l_y > FIELD_H - PAD_H) s_pad_l_y = FIELD_H - PAD_H;
}

static void move_ai(void)
{
    // IA sencilla: persigue el centro de la pelota, con un margen de
    // "torpeza" (s_ai_reaction) para que no sea perfecta ni imbatible.
    int pad_center = s_pad_r_y + PAD_H / 2;
    int ball_center = s_ball_y + BALL_SIZE / 2;

    if (pad_center < ball_center - s_ai_reaction) {
        s_pad_r_y += PAD_SPEED - 1;
    } else if (pad_center > ball_center + s_ai_reaction) {
        s_pad_r_y -= PAD_SPEED - 1;
    }
    if (s_pad_r_y < HUD_H) s_pad_r_y = HUD_H;
    if (s_pad_r_y > FIELD_H - PAD_H) s_pad_r_y = FIELD_H - PAD_H;
}

// Devuelve 1 si hubo punto (y ya se relanzo la pelota)
static int step_ball(void)
{
    s_ball_x += s_ball_dx;
    s_ball_y += s_ball_dy;

    // Rebote arriba / abajo
    if (s_ball_y <= HUD_H) {
        s_ball_y = HUD_H;
        s_ball_dy = -s_ball_dy;
    } else if (s_ball_y >= FIELD_H - BALL_SIZE) {
        s_ball_y = FIELD_H - BALL_SIZE;
        s_ball_dy = -s_ball_dy;
    }

    // Rebote en pala izquierda
    int lx = PAD_MARGIN;
    if (s_ball_dx < 0 &&
        s_ball_x <= lx + PAD_W && s_ball_x + BALL_SIZE >= lx &&
        s_ball_y + BALL_SIZE >= s_pad_l_y && s_ball_y <= s_pad_l_y + PAD_H) {
        s_ball_x = lx + PAD_W;
        s_ball_dx = -s_ball_dx;
        // el punto de impacto en la pala afecta al angulo vertical
        int offset = (s_ball_y + BALL_SIZE / 2) - (s_pad_l_y + PAD_H / 2);
        s_ball_dy = (offset < -6) ? -2 : (offset > 6) ? 2 : (offset < 0 ? -1 : 1);
    }

    // Rebote en pala derecha
    int rx = FIELD_W - PAD_MARGIN - PAD_W;
    if (s_ball_dx > 0 &&
        s_ball_x + BALL_SIZE >= rx && s_ball_x <= rx + PAD_W &&
        s_ball_y + BALL_SIZE >= s_pad_r_y && s_ball_y <= s_pad_r_y + PAD_H) {
        s_ball_x = rx - BALL_SIZE;
        s_ball_dx = -s_ball_dx;
        int offset = (s_ball_y + BALL_SIZE / 2) - (s_pad_r_y + PAD_H / 2);
        s_ball_dy = (offset < -6) ? -2 : (offset > 6) ? 2 : (offset < 0 ? -1 : 1);
    }

    // Punto
    if (s_ball_x < 0) {
        s_score_r++;

        // Si llega al tope, NO relanzar la pelota.
        // Este era el detalle clave: antes siempre se hacia new_ball()
        // despues de puntuar, por eso la partida seguia viva.
        if (!check_match_point()) {
            new_ball(1);
        }
        return 1;
    } else if (s_ball_x > FIELD_W) {
        s_score_l++;

        if (!check_match_point()) {
            new_ball(-1);
        }
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    int seed_var;
    srand((unsigned)(uintptr_t)&seed_var ^ 0x9E17u);

    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("pong: no se pudo entrar en modo grafico\n");
        return 1;
    }
    setup_palette();
    new_game();
    render_full();

    int running = 1;
    while (running) {
        if (bt_keyboard_is_pressed(BT_KEY_ESC) || bt_keyboard_is_pressed(BT_KEY_Q)) {
            running = 0;
            break;
        }

        if (s_game_over) {
            // Partida terminada: congelar pelota/IA y esperar R o ESC/Q.
            if (bt_keyboard_is_pressed(BT_KEY_R)) {
                new_game();
                render_full();
            } else {
                draw_game_over_overlay();
            }
            rgb_display_wait_vsync();
            continue;
        }

        read_input();
        move_ai();
        int scored = step_ball();

        // --- Dibujo "delta": solo lo que cambio ---
        if (s_pad_l_y != s_prev_pad_l_y) {
            erase_pad(PAD_MARGIN, s_prev_pad_l_y);
            draw_pad(PAD_MARGIN, s_pad_l_y, COL_PAD_L);
            s_prev_pad_l_y = s_pad_l_y;
        }
        if (s_pad_r_y != s_prev_pad_r_y) {
            erase_pad(FIELD_W - PAD_MARGIN - PAD_W, s_prev_pad_r_y);
            draw_pad(FIELD_W - PAD_MARGIN - PAD_W, s_pad_r_y, COL_PAD_R);
            s_prev_pad_r_y = s_pad_r_y;
        }

        if (scored) {
            // La pelota "salto" de sitio (o la partida termino):
            // redibujar una vez y, si hay ganador, mostrar overlay.
            draw_hud_score();
            render_full();
            if (s_game_over) {
                draw_game_over_overlay();
            }
        } else {
            rgb_gfx_rectfill(s_prev_ball_x, s_prev_ball_y, BALL_SIZE, BALL_SIZE, COL_BG);
            rgb_gfx_rectfill(s_ball_x, s_ball_y, BALL_SIZE, BALL_SIZE, COL_BALL);
            s_prev_ball_x = s_ball_x;
            s_prev_ball_y = s_ball_y;
        }

        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    printf("Pong: fin de la partida. %d - %d\n", s_score_l, s_score_r);
    return 0;
}
