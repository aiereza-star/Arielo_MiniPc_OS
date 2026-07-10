/*
 * snake.c - Juego clasico Snake para Arielo MiniPC OS (BreezyBox ELF app)
 *
 * Controles: flechas para moverse, ESC para salir.
 * Al perder: ENTER reinicia, ESC sale.
 *
 * Compilado como ELF de BreezyBox (ver buildelf.bat/sh):
 *   - Modo grafico SM_400X240 (400x240 @ 8bpp, encaje perfecto 2x en pantalla).
 *   - Teclado leido con bt_keyboard_is_pressed() (con fallback automatico a
 *     USB HID, aunque el nombre diga "bt" -- ver usb_hid_keyboard_02d.c).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "rgb_gfx.h"
#include "rgb_display.h"
#include "bt_keyboard.h"

// ---------------- Configuracion del tablero ----------------
#define CELL        10                  // tamano de celda en pixeles
#define GRID_W      40                  // 40 * 10 = 400
#define GRID_H      21                  // 21 * 10 = 210
#define HUD_H       30                  // franja superior para el marcador (30+210=240)
#define MAX_SNAKE   (GRID_W * GRID_H)

// ---------------- Colores (paleta 8bpp indexada) ----------------
#define COL_BG      0
#define COL_SNAKE   1
#define COL_HEAD    2
#define COL_FOOD    3
#define COL_TEXT    4
#define COL_WALL    5
#define COL_OVER    6

typedef struct { int x, y; } point_t;

// Declaracion adelantada: draw_text_small se define mas abajo (junto a la
// fuente 5x7), pero draw_hud_score la necesita antes.
static void draw_text_small(int x, int y, const char *s, uint8_t color, int scale);

static point_t s_snake[MAX_SNAKE];
static int     s_snake_len;
static int     s_dir_x, s_dir_y;
static int     s_next_dir_x, s_next_dir_y;
static point_t s_food;
static int     s_score;
static int     s_game_over;
static int     s_tick_speed;   // frames de vsync entre movimientos

static void setup_palette(void)
{
    static uint16_t pal[256];
    memset(pal, 0, sizeof(pal));
    // RGB565: (r<<11)|(g<<5)|b, con r,b de 5 bits y g de 6 bits
    pal[COL_BG]    = 0x0000;                    // negro
    pal[COL_SNAKE] = ((0)<<11)  | (40<<5) | 8;   // verde
    pal[COL_HEAD]  = ((0)<<11)  | (63<<5) | 20;  // verde claro
    pal[COL_FOOD]  = (31<<11)   | (8<<5)  | 8;   // rojo
    pal[COL_TEXT]  = (31<<11)   | (63<<5) | 31;  // blanco
    pal[COL_WALL]  = (10<<11)   | (20<<5) | 25;  // azul grisaceo
    pal[COL_OVER]  = (31<<11)   | (40<<5) | 0;   // naranja
    rgb_display_set_vga_palette(pal);
}

static void place_food(void)
{
    int ok;
    do {
        ok = 1;
        s_food.x = rand() % GRID_W;
        s_food.y = rand() % GRID_H;
        for (int i = 0; i < s_snake_len; i++) {
            if (s_snake[i].x == s_food.x && s_snake[i].y == s_food.y) {
                ok = 0;
                break;
            }
        }
    } while (!ok);
}

static void new_game(void)
{
    s_snake_len = 3;
    s_snake[0].x = GRID_W / 2;     s_snake[0].y = GRID_H / 2;
    s_snake[1].x = GRID_W / 2 - 1; s_snake[1].y = GRID_H / 2;
    s_snake[2].x = GRID_W / 2 - 2; s_snake[2].y = GRID_H / 2;
    s_dir_x = 1; s_dir_y = 0;
    s_next_dir_x = 1; s_next_dir_y = 0;
    s_score = 0;
    s_game_over = 0;
    s_tick_speed = 8;   // mas alto = mas lento
    place_food();
}

static void read_input(void)
{
    // Cambiar direccion, evitando invertir 180 grados sobre si misma.
    if (bt_keyboard_is_pressed(BT_KEY_UP) && s_dir_y == 0) {
        s_next_dir_x = 0; s_next_dir_y = -1;
    } else if (bt_keyboard_is_pressed(BT_KEY_DOWN) && s_dir_y == 0) {
        s_next_dir_x = 0; s_next_dir_y = 1;
    } else if (bt_keyboard_is_pressed(BT_KEY_LEFT) && s_dir_x == 0) {
        s_next_dir_x = -1; s_next_dir_y = 0;
    } else if (bt_keyboard_is_pressed(BT_KEY_RIGHT) && s_dir_x == 0) {
        s_next_dir_x = 1; s_next_dir_y = 0;
    }
}

static void draw_cell(int gx, int gy, uint8_t color)
{
    rgb_gfx_rectfill(gx * CELL, HUD_H + gy * CELL, CELL - 1, CELL - 1, color);
}

static void draw_hud_score(void)
{
    // Solo se llama cuando la puntuacion cambia de verdad (al comer), no
    // en cada vuelta del bucle. Redibujamos SOLO el area del texto (no toda
    // la franja de 400px), para minimizar el riesgo de "tearing" visible
    // sin doble buffer -- un area pequena se escribe casi instantaneamente.
    rgb_gfx_rectfill(4, 4, 110, 22, COL_WALL);
    char buf[32];
    snprintf(buf, sizeof(buf), "SCORE %d", s_score);
    draw_text_small(6, 11, buf, COL_TEXT, 1);
}

static void step_snake(void)
{
    s_dir_x = s_next_dir_x;
    s_dir_y = s_next_dir_y;

    point_t old_head = s_snake[0];
    point_t head = old_head;
    head.x += s_dir_x;
    head.y += s_dir_y;

    // Colision con pared
    if (head.x < 0 || head.x >= GRID_W || head.y < 0 || head.y >= GRID_H) {
        s_game_over = 1;
        return;
    }
    // Colision consigo misma
    for (int i = 0; i < s_snake_len; i++) {
        if (s_snake[i].x == head.x && s_snake[i].y == head.y) {
            s_game_over = 1;
            return;
        }
    }

    int grew = (head.x == s_food.x && head.y == s_food.y);
    point_t old_tail = s_snake[s_snake_len - 1];

    // Desplazar el cuerpo (insertar nueva cabeza al principio)
    int limit = grew ? s_snake_len : s_snake_len - 1;
    for (int i = limit; i > 0; i--) {
        s_snake[i] = s_snake[i - 1];
    }
    s_snake[0] = head;

    // --- Dibujar SOLO lo que cambio (sin borrar ni redibujar la pantalla
    // entera). Sin doble buffer, un borrado total en cada vuelta del bucle
    // provoca parpadeo y "tearing" visible (franjas negras cruzando texto).
    draw_cell(old_head.x, old_head.y, COL_SNAKE);   // la vieja cabeza pasa a ser cuerpo
    draw_cell(head.x, head.y, COL_HEAD);            // nueva cabeza
    if (!grew) {
        draw_cell(old_tail.x, old_tail.y, COL_BG);  // borrar la cola que desaparece
    }

    if (grew) {
        s_snake_len++;
        s_score += 10;
        if (s_tick_speed > 3) s_tick_speed--;   // acelera un poco
        place_food();
        draw_cell(s_food.x, s_food.y, COL_FOOD);
        draw_hud_score();
    }
}

// Fuente 5x7 compacta (column-major, bit0 = fila superior). Solo los
// caracteres necesarios para el HUD: digitos, espacio, guion y las letras
// de "SCORE" / "GAME OVER".
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
        case 'C': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x22}; return g[col]; }
        case 'E': { static const uint8_t g[5]={0x7F,0x49,0x49,0x49,0x41}; return g[col]; }
        case 'G': { static const uint8_t g[5]={0x3E,0x41,0x49,0x49,0x7A}; return g[col]; }
        case 'I': { static const uint8_t g[5]={0x00,0x41,0x7F,0x41,0x00}; return g[col]; }
        case 'M': { static const uint8_t g[5]={0x7F,0x02,0x0C,0x02,0x7F}; return g[col]; }
        case 'N': { static const uint8_t g[5]={0x7F,0x04,0x08,0x10,0x7F}; return g[col]; }
        case 'O': { static const uint8_t g[5]={0x3E,0x41,0x41,0x41,0x3E}; return g[col]; }
        case 'R': { static const uint8_t g[5]={0x7F,0x09,0x19,0x29,0x46}; return g[col]; }
        case 'S': { static const uint8_t g[5]={0x46,0x49,0x49,0x49,0x31}; return g[col]; }
        case 'T': { static const uint8_t g[5]={0x01,0x01,0x7F,0x01,0x01}; return g[col]; }
        case 'V': { static const uint8_t g[5]={0x1F,0x20,0x40,0x20,0x1F}; return g[col]; }
        case 'X': { static const uint8_t g[5]={0x63,0x14,0x08,0x14,0x63}; return g[col]; }
        case 'Y': { static const uint8_t g[5]={0x03,0x04,0x78,0x04,0x03}; return g[col]; }
        case '-': { static const uint8_t g[5]={0x08,0x08,0x08,0x08,0x08}; return g[col]; }
        default:  return 0x00;   // espacio y desconocidos: en blanco
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

// Redibuja TODO desde cero: solo se llama al arrancar o reiniciar partida,
// nunca en cada vuelta del bucle (eso es lo que causaba el parpadeo).
static void render_full(void)
{
    rgb_gfx_clear(COL_BG);
    // Fondo completo de la barra del HUD: solo se pinta aqui, una vez al
    // arrancar/reiniciar (evento puntual, no en cada vuelta del bucle).
    rgb_gfx_rectfill(0, 0, GRID_W * CELL, HUD_H, COL_WALL);
    draw_hud_score();
    for (int i = 0; i < s_snake_len; i++) {
        draw_cell(s_snake[i].x, s_snake[i].y, (i == 0) ? COL_HEAD : COL_SNAKE);
    }
    draw_cell(s_food.x, s_food.y, COL_FOOD);
}

// Se llama UNA vez, justo cuando la partida termina (no en cada vuelta).
static void render_gameover_overlay(void)
{
    rgb_gfx_rectfill(70, 90, 260, 50, COL_OVER);
    // Borde del recuadro: rgb_gfx_rect no esta exportada al ELF loader,
    // asi que dibujamos el marco con 4 tiras finas usando rectfill.
    rgb_gfx_rectfill(70, 90, 260, 2, COL_TEXT);         // arriba
    rgb_gfx_rectfill(70, 138, 260, 2, COL_TEXT);        // abajo
    rgb_gfx_rectfill(70, 90, 2, 50, COL_TEXT);          // izquierda
    rgb_gfx_rectfill(328, 90, 2, 50, COL_TEXT);         // derecha
    draw_text_small(84, 100, "GAME OVER", COL_TEXT, 2);
    draw_text_small(84, 120, "ENTER-RETRY ESC-EXIT", COL_TEXT, 1);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    // Semilla simple: no dependemos de rgb_display_get_fb_width (no esta
    // exportada al ELF loader). Usamos una mezcla de direccion de variable
    // local (varia por la pila) y un valor fijo, suficiente para un juego.
    int seed_var;
    srand((unsigned)(uintptr_t)&seed_var ^ 0x5A17u);

    if (rgb_display_set_mode(SM_400X240) != 0) {
        printf("snake: no se pudo entrar en modo grafico\n");
        return 1;
    }
    setup_palette();
    new_game();
    render_full();

    int frame = 0;
    int running = 1;

    while (running) {
        if (bt_keyboard_is_pressed(BT_KEY_ESC)) {
            running = 0;
            break;
        }

        if (!s_game_over) {
            read_input();
            frame++;
            if (frame >= s_tick_speed) {
                frame = 0;
                step_snake();   // dibuja su propio "delta" internamente
                if (s_game_over) {
                    render_gameover_overlay();
                }
            }
        } else {
            if (bt_keyboard_is_pressed(BT_KEY_ENTER)) {
                new_game();
                render_full();
            }
        }

        // Solo marca el ritmo de tiempo; no toca la pantalla. El dibujo
        // real ocurre unicamente cuando el estado del juego cambia de
        // verdad (arriba), evitando el parpadeo de antes.
        rgb_display_wait_vsync();
    }

    rgb_display_set_mode(SM_TEXT);
    printf("Snake: fin de la partida. Puntuacion: %d\n", s_score);
    return 0;
}
