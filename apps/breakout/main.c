/*
 * arielo_breakout - classic Breakout/Arkanoid-style game for Arielo MiniPC OS
 * Target: ESP32-S3 / BreezyBox external ELF app
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
#define SW 256
#define SH 150

#define HID_KEY_A       0x04
#define HID_KEY_D       0x07
#define HID_KEY_P       0x13
#define HID_KEY_Q       0x14
#define HID_KEY_R       0x15
#define HID_KEY_ESC     0x29
#define HID_KEY_SPACE   0x2C
#define HID_KEY_RIGHT   0x4F
#define HID_KEY_LEFT    0x50

#define COL_BG 0
#define COL_PANEL 1
#define COL_TEXT 2
#define COL_WHITE 3
#define COL_RED 4
#define COL_YELLOW 5
#define COL_GREEN 6
#define COL_BLUE 7
#define COL_CYAN 8
#define COL_ORANGE 9
#define COL_PURPLE 10

static const uint16_t pal[256] = {
    [COL_BG] = 0x0000, [COL_PANEL] = 0x1082, [COL_TEXT] = 0xC618,
    [COL_WHITE] = 0xFFFF, [COL_RED] = 0xF800, [COL_YELLOW] = 0xFFE0,
    [COL_GREEN] = 0x07E0, [COL_BLUE] = 0x001F, [COL_CYAN] = 0x07FF,
    [COL_ORANGE] = 0xFD20, [COL_PURPLE] = 0x781F,
};

#define BR_COLS 10
#define BR_ROWS 5
#define BR_W 20
#define BR_H 7
#define BR_X 18
#define BR_Y 22

static uint8_t bricks[BR_ROWS][BR_COLS];
static int paddle_x, paddle_w;
static int ball_x, ball_y, ball_dx, ball_dy;
static int score, lives, paused, game_over, win;
static uint8_t prev_keys[256/8];

static int key_down(uint8_t k) { return bt_keyboard_is_pressed(k) ? 1 : 0; }
static int key_edge(uint8_t k) {
    int d = key_down(k);
    uint8_t m=(uint8_t)(1U<<(k&7)); uint8_t *b=&prev_keys[k>>3]; int was=(*b&m)!=0;
    if(d)*b|=m; else *b&=(uint8_t)~m;
    return d && !was;
}

static uint8_t glyph3(char c, int row) {
    static const uint8_t num[10][5]={{7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},{7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7}};
    static const uint8_t let[26][5]={{2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4},{3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{5,7,7,7,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,7,3},{6,5,6,5,5},{3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},{7,1,2,4,7}};
    if(row<0||row>=5)return 0; if(c>='0'&&c<='9')return num[c-'0'][row]; if(c>='a'&&c<='z')c=(char)(c-32); if(c>='A'&&c<='Z')return let[c-'A'][row]; if(c==':')return (uint8_t[]){0,2,0,2,0}[row]; if(c=='-')return (uint8_t[]){0,0,7,0,0}[row]; return 0;
}
static void draw_text(int x,int y,const char*t,uint8_t c,int sc){ while(*t){ char ch=*t++; if(ch==' '){x+=4*sc;continue;} for(int r=0;r<5;r++){uint8_t bits=glyph3(ch,r); for(int col=0;col<3;col++) if(bits&(1<<(2-col))) rgb_gfx_rectfill(x+col*sc,y+r*sc,sc,sc,c);} x+=4*sc; } }
static void draw_num(int x,int y,int v,uint8_t c){ char b[16]; snprintf(b,sizeof(b),"%d",v); draw_text(x,y,b,c,1); }

static void reset_ball(void) {
    ball_x = paddle_x + paddle_w/2; ball_y = 124;
    ball_dx = 2; ball_dy = -2;
}
static void reset_game(void) {
    memset(prev_keys,0,sizeof(prev_keys));
    for(int r=0;r<BR_ROWS;r++) for(int c=0;c<BR_COLS;c++) bricks[r][c]=(uint8_t)(1+r);
    paddle_w=36; paddle_x=(SW-paddle_w)/2; score=0; lives=3; paused=0; game_over=0; win=0;
    reset_ball();
}

static int bricks_left(void){ int n=0; for(int r=0;r<BR_ROWS;r++) for(int c=0;c<BR_COLS;c++) if(bricks[r][c]) n++; return n; }
static uint8_t brick_color(int r){ static const uint8_t col[BR_ROWS]={COL_RED,COL_ORANGE,COL_YELLOW,COL_GREEN,COL_CYAN}; return col[r]; }

static void update_game(void) {
    if(key_down(HID_KEY_LEFT)||key_down(HID_KEY_A)) paddle_x-=4;
    if(key_down(HID_KEY_RIGHT)||key_down(HID_KEY_D)) paddle_x+=4;
    if(paddle_x<4)paddle_x=4; if(paddle_x+paddle_w>SW-4)paddle_x=SW-4-paddle_w;

    ball_x += ball_dx; ball_y += ball_dy;
    if(ball_x < 4) { ball_x=4; ball_dx=-ball_dx; }
    if(ball_x > SW-5) { ball_x=SW-5; ball_dx=-ball_dx; }
    if(ball_y < 14) { ball_y=14; ball_dy=-ball_dy; }

    // paddle collision
    if(ball_y >= 130 && ball_y <= 136 && ball_x >= paddle_x-2 && ball_x <= paddle_x+paddle_w+2 && ball_dy>0) {
        ball_y=129; ball_dy=-ball_dy;
        int rel = ball_x - (paddle_x + paddle_w/2);
        ball_dx = rel / 6; if(ball_dx==0) ball_dx = (rel<0)?-1:1; if(ball_dx<-4)ball_dx=-4; if(ball_dx>4)ball_dx=4;
    }

    // brick collision
    for(int r=0;r<BR_ROWS;r++) for(int c=0;c<BR_COLS;c++) if(bricks[r][c]) {
        int x=BR_X+c*(BR_W+2), y=BR_Y+r*(BR_H+2);
        if(ball_x>=x && ball_x<x+BR_W && ball_y>=y && ball_y<y+BR_H) {
            bricks[r][c]=0; score += (BR_ROWS-r)*10; ball_dy=-ball_dy;
            if(bricks_left()==0){ win=1; game_over=1; }
            return;
        }
    }

    if(ball_y > SH) {
        lives--;
        if(lives<=0) game_over=1; else reset_ball();
    }
}

static void render(void) {
    rgb_gfx_clear(COL_BG);
    rgb_gfx_rectfill(0,0,SW,12,COL_PANEL);
    draw_text(6,4,"ARIELO BREAKOUT",COL_YELLOW,1);
    draw_text(154,4,"SCORE",COL_TEXT,1); draw_num(180,4,score,COL_WHITE);
    draw_text(214,4,"L",COL_TEXT,1); draw_num(222,4,lives,COL_WHITE);

    for(int r=0;r<BR_ROWS;r++) for(int c=0;c<BR_COLS;c++) if(bricks[r][c]) {
        int x=BR_X+c*(BR_W+2), y=BR_Y+r*(BR_H+2);
        rgb_gfx_rectfill(x,y,BR_W,BR_H,brick_color(r));
        rgb_gfx_rectfill(x+1,y+1,BR_W-2,1,COL_WHITE);
    }
    rgb_gfx_rectfill(paddle_x,134,paddle_w,5,COL_WHITE);
    rgb_gfx_rectfill(paddle_x+2,135,paddle_w-4,3,COL_CYAN);
    rgb_gfx_rectfill(ball_x-2,ball_y-2,4,4,COL_YELLOW);
    draw_text(6,142,"A/D OR ARROWS  P PAUSE  Q EXIT",COL_TEXT,1);

    if(paused){ rgb_gfx_rectfill(96,68,62,18,COL_PANEL); draw_text(112,75,"PAUSE",COL_YELLOW,1); }
    if(game_over){ rgb_gfx_rectfill(72,62,112,30,COL_PANEL); draw_text(94,69,win?"YOU WIN":"GAME OVER",win?COL_GREEN:COL_RED,1); draw_text(88,82,"R RESTART",COL_WHITE,1); }
}

void app_main(void) {
    if(rgb_display_set_mode(SM_150P)!=0){ puts("arielo_breakout: no se pudo entrar en modo grafico"); return; }
    rgb_display_set_vga_palette(pal);
    reset_game();
    int quit=0, slow=0;
    while(!quit){
        if(key_edge(HID_KEY_Q)||key_edge(HID_KEY_ESC)) quit=1;
        if(key_edge(HID_KEY_R)) reset_game();
        if(key_edge(HID_KEY_P)||key_edge(HID_KEY_SPACE)) paused=!paused;
        if(!paused && !game_over){ if(++slow>=2){slow=0; update_game();} }
        render(); rgb_display_wait_vsync(); vTaskDelay(1);
    }
    rgb_display_set_mode(SM_TEXT);
    printf("Breakout: score=%d lives=%d\n",score,lives);
}
