/*
 * arielo_tetris - classic Tetris-style game for Arielo MiniPC OS
 * Target: ESP32-S3 / BreezyBox external ELF app
 * Video: SM_150P 256x150, intended to be scaled by Arielo SM_150P x3 patch
 * Input: bt_keyboard_* bridge already connected to USB HID in Arielo MiniPC OS 09A+
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

extern int rgb_display_set_mode(int mode);
extern void rgb_display_set_vga_palette(const uint16_t palette[256]);
extern void rgb_display_wait_vsync(void);
extern void rgb_gfx_clear(uint8_t color);
extern void rgb_gfx_rectfill(int x, int y, int w, int h, uint8_t color);
extern int bt_keyboard_is_pressed(unsigned char keycode);
extern void vTaskDelay(unsigned int ticks);

#define SM_TEXT 3
#define SM_150P 0x80

#define HID_KEY_A       0x04
#define HID_KEY_C       0x06
#define HID_KEY_D       0x07
#define HID_KEY_P       0x13
#define HID_KEY_Q       0x14
#define HID_KEY_R       0x15
#define HID_KEY_S       0x16
#define HID_KEY_V       0x19
#define HID_KEY_W       0x1A
#define HID_KEY_X       0x1B
#define HID_KEY_Z       0x1D
#define HID_KEY_ESC     0x29
#define HID_KEY_SPACE   0x2C
#define HID_KEY_RIGHT   0x4F
#define HID_KEY_LEFT    0x50
#define HID_KEY_DOWN    0x51
#define HID_KEY_UP      0x52

#define SW 256
#define SH 150
#define BW 10
#define BH 20
#define CELL 6
#define BX 42
#define BY 15

#define COL_BG     0
#define COL_PANEL  1
#define COL_GRID   2
#define COL_TEXT   3
#define COL_WHITE  4
#define COL_RED    5
#define COL_CYAN   6
#define COL_YELLOW 7
#define COL_GREEN  8
#define COL_BLUE   9
#define COL_ORANGE 10
#define COL_PURPLE 11
#define COL_PINK   12

static const uint16_t pal[256] = {
    [COL_BG]     = 0x0000,
    [COL_PANEL]  = 0x1082,
    [COL_GRID]   = 0x3186,
    [COL_TEXT]   = 0xC618,
    [COL_WHITE]  = 0xFFFF,
    [COL_RED]    = 0xF800,
    [COL_CYAN]   = 0x07FF,
    [COL_YELLOW] = 0xFFE0,
    [COL_GREEN]  = 0x07E0,
    [COL_BLUE]   = 0x001F,
    [COL_ORANGE] = 0xFD20,
    [COL_PURPLE] = 0x781F,
    [COL_PINK]   = 0xF81F,
};

static const uint8_t piece_color[7] = { COL_CYAN, COL_YELLOW, COL_PURPLE, COL_GREEN, COL_RED, COL_BLUE, COL_ORANGE };

static const int8_t shapes[7][4][4][2] = {
    // I
    {{{0,1},{1,1},{2,1},{3,1}}, {{2,0},{2,1},{2,2},{2,3}}, {{0,2},{1,2},{2,2},{3,2}}, {{1,0},{1,1},{1,2},{1,3}}},
    // O
    {{{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{2,1}}},
    // T
    {{{1,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{2,1},{1,2}}, {{1,0},{0,1},{1,1},{1,2}}},
    // S
    {{{1,0},{2,0},{0,1},{1,1}}, {{1,0},{1,1},{2,1},{2,2}}, {{1,1},{2,1},{0,2},{1,2}}, {{0,0},{0,1},{1,1},{1,2}}},
    // Z
    {{{0,0},{1,0},{1,1},{2,1}}, {{2,0},{1,1},{2,1},{1,2}}, {{0,1},{1,1},{1,2},{2,2}}, {{1,0},{0,1},{1,1},{0,2}}},
    // J
    {{{0,0},{0,1},{1,1},{2,1}}, {{1,0},{2,0},{1,1},{1,2}}, {{0,1},{1,1},{2,1},{2,2}}, {{1,0},{1,1},{0,2},{1,2}}},
    // L
    {{{2,0},{0,1},{1,1},{2,1}}, {{1,0},{1,1},{1,2},{2,2}}, {{0,1},{1,1},{2,1},{0,2}}, {{0,0},{1,0},{1,1},{1,2}}}
};

static uint8_t board[BH][BW];
static int cur, nextp, rot, px, py;
static int score, lines, level, game_over, paused;
static uint32_t rng_state = 0x1234abcdU;
static uint8_t prev_keys[256/8];

static uint32_t rnd32(void) { rng_state = rng_state * 1664525U + 1013904223U; return rng_state; }
static int rnd_piece(void) { return (int)((rnd32() >> 8) % 7); }

static int key_down(uint8_t k) { return bt_keyboard_is_pressed(k) ? 1 : 0; }
static int key_edge(uint8_t k) {
    int d = key_down(k);
    uint8_t m = (uint8_t)(1U << (k & 7));
    uint8_t *b = &prev_keys[k >> 3];
    int was = (*b & m) != 0;
    if (d) *b |= m; else *b &= (uint8_t)~m;
    return d && !was;
}

static uint8_t glyph3(char c, int row) {
    static const uint8_t num[10][5]={{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
    static const uint8_t let[26][5]={{2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,7,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},{7,1,2,4,7}};
    if (row < 0 || row >= 5) return 0;
    if (c >= '0' && c <= '9') return num[c-'0'][row];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return let[c-'A'][row];
    if (c == ':') return (uint8_t[]){0,2,0,2,0}[row];
    if (c == '-') return (uint8_t[]){0,0,7,0,0}[row];
    if (c == '.') return (uint8_t[]){0,0,0,0,2}[row];
    return 0;
}

static void draw_text(int x, int y, const char *t, uint8_t c, int scale) {
    while (*t) {
        char ch = *t++;
        if (ch == ' ') { x += 4*scale; continue; }
        for (int r=0;r<5;r++) {
            uint8_t bits = glyph3(ch, r);
            for (int col=0; col<3; col++) if (bits & (1 << (2-col))) {
                rgb_gfx_rectfill(x + col*scale, y + r*scale, scale, scale, c);
            }
        }
        x += 4*scale;
    }
}

static void draw_num(int x, int y, int v, uint8_t c) {
    char buf[16]; snprintf(buf, sizeof(buf), "%d", v); draw_text(x,y,buf,c,1);
}

static int collide(int npiece, int nrot, int nx, int ny) {
    for (int i=0;i<4;i++) {
        int x = nx + shapes[npiece][nrot][i][0];
        int y = ny + shapes[npiece][nrot][i][1];
        if (x < 0 || x >= BW || y >= BH) return 1;
        if (y >= 0 && board[y][x]) return 1;
    }
    return 0;
}

static void lock_piece(void) {
    for (int i=0;i<4;i++) {
        int x = px + shapes[cur][rot][i][0];
        int y = py + shapes[cur][rot][i][1];
        if (x>=0 && x<BW && y>=0 && y<BH) board[y][x] = piece_color[cur];
    }
}

static void clear_lines(void) {
    int cleared=0;
    for (int y=BH-1; y>=0; y--) {
        int full=1;
        for (int x=0; x<BW; x++) if (!board[y][x]) { full=0; break; }
        if (full) {
            for (int yy=y; yy>0; yy--) memcpy(board[yy], board[yy-1], BW);
            memset(board[0], 0, BW);
            cleared++;
            y++;
        }
    }
    if (cleared) {
        static const int pts[5] = {0,40,100,300,1200};
        score += pts[cleared] * level;
        lines += cleared;
        level = 1 + lines / 10;
    }
}

static void spawn_piece(void) {
    cur = nextp;
    nextp = rnd_piece();
    rot = 0; px = 3; py = 0;
    if (collide(cur, rot, px, py)) game_over = 1;
}

static void reset_game(void) {
    memset(board,0,sizeof(board));
    memset(prev_keys,0,sizeof(prev_keys));
    score=0; lines=0; level=1; game_over=0; paused=0;
    nextp = rnd_piece();
    spawn_piece();
}

static void draw_cell(int gx, int gy, uint8_t color) {
    int x = BX + gx*CELL;
    int y = BY + gy*CELL;
    rgb_gfx_rectfill(x, y, CELL-1, CELL-1, color ? color : COL_BG);
}

static void draw_piece_at(int piece, int rr, int gx, int gy, int ox, int oy, int cell) {
    for (int i=0;i<4;i++) {
        int x = ox + (gx + shapes[piece][rr][i][0])*cell;
        int y = oy + (gy + shapes[piece][rr][i][1])*cell;
        rgb_gfx_rectfill(x, y, cell-1, cell-1, piece_color[piece]);
    }
}

static void render(void) {
    rgb_gfx_clear(COL_BG);
    rgb_gfx_rectfill(BX-2, BY-2, BW*CELL+4, BH*CELL+4, COL_GRID);
    rgb_gfx_rectfill(BX, BY, BW*CELL, BH*CELL, COL_BG);
    for (int y=0;y<BH;y++) for (int x=0;x<BW;x++) draw_cell(x,y,board[y][x]);
    if (!game_over) draw_piece_at(cur, rot, px, py, BX, BY, CELL);

    draw_text(8, 4, "ARIELO TETRIS", COL_YELLOW, 1);
    draw_text(128, 18, "NEXT", COL_TEXT, 1);
    rgb_gfx_rectfill(126, 28, 38, 30, COL_PANEL);
    draw_piece_at(nextp, 0, 0, 0, 132, 32, 6);
    draw_text(128, 66, "SCORE", COL_TEXT, 1); draw_num(128, 75, score, COL_WHITE);
    draw_text(128, 90, "LEVEL", COL_TEXT, 1); draw_num(128, 99, level, COL_WHITE);
    draw_text(128, 114, "LINES", COL_TEXT, 1); draw_num(128, 123, lines, COL_WHITE);
    draw_text(190, 24, "Z ROT", COL_TEXT, 1);
    draw_text(190, 36, "X DROP", COL_TEXT, 1);
    draw_text(190, 48, "P PAUSE", COL_TEXT, 1);
    draw_text(190, 60, "Q EXIT", COL_TEXT, 1);

    if (paused) {
        rgb_gfx_rectfill(74, 62, 60, 18, COL_PANEL);
        draw_text(84, 69, "PAUSE", COL_YELLOW, 1);
    }
    if (game_over) {
        rgb_gfx_rectfill(62, 52, 86, 34, COL_PANEL);
        draw_text(75, 60, "GAME OVER", COL_RED, 1);
        draw_text(72, 74, "R RESTART", COL_WHITE, 1);
    }
}

static void rotate_piece(void) {
    int nr = (rot + 1) & 3;
    if (!collide(cur,nr,px,py)) { rot=nr; return; }
    if (!collide(cur,nr,px-1,py)) { px--; rot=nr; return; }
    if (!collide(cur,nr,px+1,py)) { px++; rot=nr; return; }
}

static void hard_drop(void) {
    while (!collide(cur, rot, px, py+1)) { py++; score += 2; }
    lock_piece(); clear_lines(); spawn_piece();
}

void app_main(void) {
    if (rgb_display_set_mode(SM_150P) != 0) {
        puts("arielo_tetris: no se pudo entrar en modo grafico");
        return;
    }
    rgb_display_set_vga_palette(pal);
    reset_game();

    int frame=0;
    int quit=0;
    while (!quit) {
        if (key_edge(HID_KEY_Q) || key_edge(HID_KEY_ESC)) quit=1;
        if (key_edge(HID_KEY_R)) reset_game();
        if (key_edge(HID_KEY_P)) paused = !paused;

        if (!paused && !game_over) {
            if (key_edge(HID_KEY_LEFT) || key_edge(HID_KEY_A))  { if (!collide(cur,rot,px-1,py)) px--; }
            if (key_edge(HID_KEY_RIGHT)|| key_edge(HID_KEY_D))  { if (!collide(cur,rot,px+1,py)) px++; }
            if (key_edge(HID_KEY_UP) || key_edge(HID_KEY_W) || key_edge(HID_KEY_Z) || key_edge(HID_KEY_C)) rotate_piece();
            if (key_edge(HID_KEY_X) || key_edge(HID_KEY_V) || key_edge(HID_KEY_SPACE)) hard_drop();

            int speed = 34 - (level-1)*3; if (speed < 6) speed = 6;
            if (key_down(HID_KEY_DOWN) || key_down(HID_KEY_S)) speed = 3;
            if (++frame >= speed) {
                frame=0;
                if (!collide(cur,rot,px,py+1)) py++;
                else { lock_piece(); clear_lines(); spawn_piece(); }
            }
        }

        render();
        rgb_display_wait_vsync();
        vTaskDelay(1);
    }
    rgb_display_set_mode(SM_TEXT);
    printf("Tetris: score=%d lines=%d level=%d\n", score, lines, level);
}
