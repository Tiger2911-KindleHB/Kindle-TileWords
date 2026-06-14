// TileWords KUAL - native Kindle word-tile game
// Dependency-free framebuffer + evdev implementation for jailbroken Kindle devices.

#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BOARD_N 15
#define RACK_N 7
#define MAX_BAG 100
#define MAX_WORDS 300000
#define MAX_WORD_LEN 32
#define BINGO_BONUS 50
#define SAVE_VERSION 1

#define ABS_MT_POSITION_X 0x35
#define ABS_MT_POSITION_Y 0x36
#define ABS_MT_TRACKING_ID 0x39

// ---------- Utility ----------
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int mini(int a, int b) { return a < b ? a : b; }
static int maxi(int a, int b) { return a > b ? a : b; }

static void safe_strcpy(char *dst, size_t n, const char *src) {
    if (!dst || n == 0) return;
    if (!src) src = "";
    snprintf(dst, n, "%s", src);
}

static void trim_upper_word(char *s) {
    size_t len = strlen(s);
    while (len && (s[len - 1] == '\n' || s[len - 1] == '\r' || isspace((unsigned char)s[len - 1]))) s[--len] = 0;
    char out[MAX_WORD_LEN];
    int j = 0;
    for (size_t i = 0; s[i] && j < MAX_WORD_LEN - 1; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isalpha(c)) out[j++] = (char)toupper(c);
    }
    out[j] = 0;
    safe_strcpy(s, MAX_WORD_LEN, out);
}

// ---------- Framebuffer drawing ----------
typedef struct {
    int fd;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    uint8_t *mem;
    long screensize;
    int w, h, bpp, stride;
} FB;

static FB g_fb;

static int fb_open(FB *fb) {
    memset(fb, 0, sizeof(*fb));
    fb->fd = open("/dev/fb0", O_RDWR);
    if (fb->fd < 0) return -1;
    if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &fb->finfo) < 0) return -1;
    if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &fb->vinfo) < 0) return -1;
    fb->w = (int)fb->vinfo.xres;
    fb->h = (int)fb->vinfo.yres;
    fb->bpp = (int)fb->vinfo.bits_per_pixel;
    fb->stride = (int)fb->finfo.line_length;
    fb->screensize = fb->stride * fb->h;
    fb->mem = mmap(0, fb->screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb->fd, 0);
    if (fb->mem == MAP_FAILED) return -1;
    return 0;
}

static void fb_close(FB *fb) {
    if (fb->mem && fb->mem != MAP_FAILED) munmap(fb->mem, fb->screensize);
    if (fb->fd >= 0) close(fb->fd);
}

static inline uint32_t gray_to_pixel(FB *fb, uint8_t g) {
    if (fb->bpp == 8) return g;
    if (fb->bpp == 16) {
        uint16_t r = (g >> 3) & 0x1F;
        uint16_t gr = (g >> 2) & 0x3F;
        uint16_t b = (g >> 3) & 0x1F;
        return (r << 11) | (gr << 5) | b;
    }
    return (g << 16) | (g << 8) | g;
}

static void put_px(FB *fb, int x, int y, uint8_t g) {
    if ((unsigned)x >= (unsigned)fb->w || (unsigned)y >= (unsigned)fb->h) return;
    uint8_t *p = fb->mem + y * fb->stride + x * (fb->bpp / 8);
    uint32_t pix = gray_to_pixel(fb, g);
    if (fb->bpp == 8) p[0] = (uint8_t)pix;
    else if (fb->bpp == 16) *((uint16_t *)p) = (uint16_t)pix;
    else if (fb->bpp == 32) *((uint32_t *)p) = pix;
}

static void rect_fill(int x, int y, int w, int h, uint8_t g) {
    if (w <= 0 || h <= 0) return;
    int x0 = clampi(x, 0, g_fb.w), y0 = clampi(y, 0, g_fb.h);
    int x1 = clampi(x + w, 0, g_fb.w), y1 = clampi(y + h, 0, g_fb.h);
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) put_px(&g_fb, xx, yy, g);
}

static void rect_outline(int x, int y, int w, int h, uint8_t g, int t) {
    rect_fill(x, y, w, t, g);
    rect_fill(x, y + h - t, w, t, g);
    rect_fill(x, y, t, h, g);
    rect_fill(x + w - t, y, t, h, g);
}

static void line_h(int x, int y, int w, uint8_t g) { rect_fill(x, y, w, 1, g); }

static void hatch(int x, int y, int w, int h, int step, bool diag) {
    step = maxi(step, 4);
    if (diag) {
        for (int k = -h; k < w + h; k += step) {
            for (int i = 0; i < h; i++) {
                int xx = x + k + i;
                int yy = y + i;
                if (xx >= x && xx < x + w) put_px(&g_fb, xx, yy, 0);
            }
        }
    } else {
        for (int yy = y + step / 2; yy < y + h; yy += step) line_h(x, yy, w, 0);
    }
}

// 5x7 uppercase font. Each row is 5 bits stored as text for readability.
static const char *glyph_rows(char c, int row) {
    static const char *blank[7] = {"00000","00000","00000","00000","00000","00000","00000"};
    static const char *g[128][7];
    static bool init = false;
    if (!init) {
#define SET(ch,a,b,c,d,e,f,h) do { g[(int)(ch)][0]=a; g[(int)(ch)][1]=b; g[(int)(ch)][2]=c; g[(int)(ch)][3]=d; g[(int)(ch)][4]=e; g[(int)(ch)][5]=f; g[(int)(ch)][6]=h; } while(0)
        SET('0',"01110","10001","10011","10101","11001","10001","01110");
        SET('1',"00100","01100","00100","00100","00100","00100","01110");
        SET('2',"01110","10001","00001","00010","00100","01000","11111");
        SET('3',"11110","00001","00001","01110","00001","00001","11110");
        SET('4',"00010","00110","01010","10010","11111","00010","00010");
        SET('5',"11111","10000","10000","11110","00001","00001","11110");
        SET('6',"01110","10000","10000","11110","10001","10001","01110");
        SET('7',"11111","00001","00010","00100","01000","01000","01000");
        SET('8',"01110","10001","10001","01110","10001","10001","01110");
        SET('9',"01110","10001","10001","01111","00001","00001","01110");
        SET('A',"01110","10001","10001","11111","10001","10001","10001");
        SET('B',"11110","10001","10001","11110","10001","10001","11110");
        SET('C',"01110","10001","10000","10000","10000","10001","01110");
        SET('D',"11110","10001","10001","10001","10001","10001","11110");
        SET('E',"11111","10000","10000","11110","10000","10000","11111");
        SET('F',"11111","10000","10000","11110","10000","10000","10000");
        SET('G',"01110","10001","10000","10111","10001","10001","01110");
        SET('H',"10001","10001","10001","11111","10001","10001","10001");
        SET('I',"11111","00100","00100","00100","00100","00100","11111");
        SET('J',"00111","00010","00010","00010","10010","10010","01100");
        SET('K',"10001","10010","10100","11000","10100","10010","10001");
        SET('L',"10000","10000","10000","10000","10000","10000","11111");
        SET('M',"10001","11011","10101","10101","10001","10001","10001");
        SET('N',"10001","11001","10101","10011","10001","10001","10001");
        SET('O',"01110","10001","10001","10001","10001","10001","01110");
        SET('P',"11110","10001","10001","11110","10000","10000","10000");
        SET('Q',"01110","10001","10001","10001","10101","10010","01101");
        SET('R',"11110","10001","10001","11110","10100","10010","10001");
        SET('S',"01111","10000","10000","01110","00001","00001","11110");
        SET('T',"11111","00100","00100","00100","00100","00100","00100");
        SET('U',"10001","10001","10001","10001","10001","10001","01110");
        SET('V',"10001","10001","10001","10001","10001","01010","00100");
        SET('W',"10001","10001","10001","10101","10101","10101","01010");
        SET('X',"10001","10001","01010","00100","01010","10001","10001");
        SET('Y',"10001","10001","01010","00100","00100","00100","00100");
        SET('Z',"11111","00001","00010","00100","01000","10000","11111");
        SET(' ',"00000","00000","00000","00000","00000","00000","00000");
        SET('.',"00000","00000","00000","00000","00000","01100","01100");
        SET(',',"00000","00000","00000","00000","01100","01100","01000");
        SET(':',"00000","01100","01100","00000","01100","01100","00000");
        SET('-',"00000","00000","00000","11111","00000","00000","00000");
        SET('+',"00000","00100","00100","11111","00100","00100","00000");
        SET('!',"00100","00100","00100","00100","00100","00000","00100");
        SET('?',"01110","10001","00001","00010","00100","00000","00100");
        SET('/',"00001","00010","00010","00100","01000","01000","10000");
        SET('*',"00100","10101","01110","11111","01110","10101","00100");
        SET('(',"00010","00100","01000","01000","01000","00100","00010");
        SET(')',"01000","00100","00010","00010","00010","00100","01000");
        SET('[',"01110","01000","01000","01000","01000","01000","01110");
        SET(']',"01110","00010","00010","00010","00010","00010","01110");
        init = true;
#undef SET
    }
    unsigned char uc = (unsigned char)c;
    if (islower(uc)) c = (char)toupper(uc);
    if ((unsigned char)c >= 128 || !g[(int)c][0]) return blank[row];
    return g[(int)c][row];
}

static int text_width(const char *s, int scale) {
    return (int)strlen(s) * 6 * scale;
}

static void draw_char(int x, int y, char c, int scale, uint8_t fg) {
    if (scale <= 0) return;
    for (int r = 0; r < 7; r++) {
        const char *row = glyph_rows(c, r);
        for (int col = 0; col < 5; col++) if (row[col] == '1') rect_fill(x + col * scale, y + r * scale, scale, scale, fg);
    }
}

static void draw_text(int x, int y, const char *s, int scale, uint8_t fg) {
    for (int i = 0; s && s[i]; i++) draw_char(x + i * 6 * scale, y, s[i], scale, fg);
}

static void draw_text_center(int x, int y, int w, int h, const char *s, int scale, uint8_t fg) {
    int tw = text_width(s, scale);
    int th = 7 * scale;
    draw_text(x + (w - tw) / 2, y + (h - th) / 2, s, scale, fg);
}

// ---------- Game model ----------
typedef enum { PREM_NONE, PREM_DL, PREM_TL, PREM_DW, PREM_TW, PREM_STAR } Premium;
typedef struct { char ch; bool locked; bool blank; } Cell;
typedef struct { char ch; bool blank; } Tile;

typedef struct {
    char **words;
    int count;
} Dictionary;

typedef enum {
    MODE_NORMAL,
    MODE_EXCHANGE,
    MODE_BLANK,
    MODE_CONFIRM_NEW,
    MODE_INVALID,
    MODE_GAME_OVER,
    MODE_HANDOFF
} UIMode;

typedef struct {
    Cell board[BOARD_N][BOARD_N];
    Tile racks[2][RACK_N];
    int rack_count[2];
    char bag[MAX_BAG];
    int bag_count;
    int scores[2];
    int current_player;
    int selected_rack;
    bool exchange_selected[RACK_N];
    int pass_count;
    bool game_over;
    UIMode mode;
    int blank_r, blank_c, blank_rack;
    char invalid_msg[160];
    Dictionary dict;
    char home[512];
} Game;

static Game G;

static int letter_score(char ch) {
    switch (toupper((unsigned char)ch)) {
        case 'A': case 'E': case 'I': case 'O': case 'U': case 'L': case 'N': case 'S': case 'T': case 'R': return 1;
        case 'D': case 'G': return 2;
        case 'B': case 'C': case 'M': case 'P': return 3;
        case 'F': case 'H': case 'V': case 'W': case 'Y': return 4;
        case 'K': return 5;
        case 'J': case 'X': return 8;
        case 'Q': case 'Z': return 10;
    }
    return 0;
}

static Premium premium_at(int r, int c) {
    static const int tw[][2] = {{0,0},{0,7},{0,14},{7,0},{7,14},{14,0},{14,7},{14,14},{-1,-1}};
    static const int dw[][2] = {{1,1},{2,2},{3,3},{4,4},{10,10},{11,11},{12,12},{13,13},{1,13},{2,12},{3,11},{4,10},{10,4},{11,3},{12,2},{13,1},{-1,-1}};
    static const int tl[][2] = {{1,5},{1,9},{5,1},{5,5},{5,9},{5,13},{9,1},{9,5},{9,9},{9,13},{13,5},{13,9},{-1,-1}};
    static const int dl[][2] = {{0,3},{0,11},{2,6},{2,8},{3,0},{3,7},{3,14},{6,2},{6,6},{6,8},{6,12},{7,3},{7,11},{8,2},{8,6},{8,8},{8,12},{11,0},{11,7},{11,14},{12,6},{12,8},{14,3},{14,11},{-1,-1}};
    if (r == 7 && c == 7) return PREM_STAR;
    for (int i = 0; tw[i][0] >= 0; i++) if (tw[i][0] == r && tw[i][1] == c) return PREM_TW;
    for (int i = 0; dw[i][0] >= 0; i++) if (dw[i][0] == r && dw[i][1] == c) return PREM_DW;
    for (int i = 0; tl[i][0] >= 0; i++) if (tl[i][0] == r && tl[i][1] == c) return PREM_TL;
    for (int i = 0; dl[i][0] >= 0; i++) if (dl[i][0] == r && dl[i][1] == c) return PREM_DL;
    return PREM_NONE;
}

static int dict_cmp(const void *a, const void *b) {
    const char * const *sa = (const char * const *)a;
    const char * const *sb = (const char * const *)b;
    return strcmp(*sa, *sb);
}

static const char *fallback_words[] = {
    "AA","AB","AD","AE","AG","AH","AI","AL","AM","AN","AR","AS","AT","AW","AX","AY",
    "BA","BE","BI","BO","BY","DA","DE","DO","ED","EF","EH","EL","EM","EN","ER","ES","ET","EX",
    "FA","FE","GO","HA","HE","HI","HM","HO","ID","IF","IN","IS","IT","JO","KA","KI","LA","LI","LO",
    "MA","ME","MI","MM","MO","MU","MY","NA","NE","NO","NU","OD","OE","OF","OH","OI","OM","ON","OP","OR","OS","OW","OX","OY",
    "PA","PE","PI","QI","RE","SH","SI","SO","TA","TE","TI","TO","UH","UM","UN","UP","US","UT","WE","WO","XI","XU","YA","YE","YO","ZA",
    "ABOUT","ABOVE","ACTION","ACTOR","AFTER","AGAIN","AIR","ALERT","ALONE","APPLE","AREA","ARROW","AUDIO","BAKER","BASIC","BEACH","BEARD","BEAST","BEGIN","BIRD","BLACK","BLANK","BOARD","BOAT","BOOK","BRAIN","BRAVE","BREAD","BRICK","BROWN","BUILD","CABLE","CAKE","CARD","CARE","CAT","CHESS","CLEAN","CLOCK","CLOUD","CODE","COIN","COLD","CORE","CROWN","DARK","DATA","DEAL","DECK","DEEP","DOG","DOOR","DRAW","DRIVE","EARTH","EASY","ECHO","EDGE","EIGHT","ENGINE","ENTER","EXIT","FAIR","FARM","FAST","FIRE","FISH","FIVE","FLAG","FOCUS","FONT","FOOD","FORGE","FOUR","GAME","GARDEN","GHOST","GLASS","GOOD","GREAT","GREEN","GRID","HAND","HARD","HAT","HEART","HIGH","HOME","HUMAN","ICON","IDEA","INPUT","IRON","KIND","KING","KITE","LAKE","LARGE","LAST","LETTER","LIGHT","LOCAL","LOGIC","LONG","MAIN","MAP","MATCH","MATE","MOVE","NATIVE","NEW","NINE","NORMAL","NOTE","OPEN","ORANGE","PAGE","PASS","PATH","PAWN","PHONE","PINE","PLACE","PLAIN","PLAY","POINT","POWER","QUEEN","QUICK","RACK","READ","RED","ROAD","ROUND","SAVE","SCORE","SCREEN","SEVEN","SHARE","SHIFT","SHORT","SIX","SKY","SMALL","SMART","SOLVE","SOUND","SPACE","STAR","START","STONE","TABLE","TAP","TILE","TIME","TOUCH","TREE","TURN","TWO","VALID","VALUE","WORD","WORDS","WORLD","WRITE","YELLOW","ZERO",
    NULL
};

static void dict_free(Dictionary *d) {
    if (!d) return;
    for (int i = 0; i < d->count; i++) free(d->words[i]);
    free(d->words); d->words = NULL; d->count = 0;
}

static void dict_add(Dictionary *d, const char *w) {
    if (!w || strlen(w) < 2 || strlen(w) > BOARD_N) return;
    if (d->count >= MAX_WORDS) return;
    d->words = realloc(d->words, sizeof(char *) * (d->count + 1));
    d->words[d->count++] = strdup(w);
}

static void dict_load(Game *g) {
    char path[768];
    snprintf(path, sizeof(path), "%s/data/dictionary.txt", g->home);
    FILE *f = fopen(path, "r");
    char line[256];
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == '#') continue;
            trim_upper_word(line);
            dict_add(&g->dict, line);
        }
        fclose(f);
    }
    if (g->dict.count == 0) {
        for (int i = 0; fallback_words[i]; i++) dict_add(&g->dict, fallback_words[i]);
    }
    qsort(g->dict.words, g->dict.count, sizeof(char *), dict_cmp);
    // De-duplicate in place.
    int out = 0;
    for (int i = 0; i < g->dict.count; i++) {
        if (out == 0 || strcmp(g->dict.words[i], g->dict.words[out - 1]) != 0) g->dict.words[out++] = g->dict.words[i];
        else free(g->dict.words[i]);
    }
    g->dict.count = out;
}

static bool dict_has(Dictionary *d, const char *word) {
    if (!word || strlen(word) < 2) return true;
    char key[MAX_WORD_LEN]; safe_strcpy(key, sizeof(key), word); trim_upper_word(key);
    char *k = key;
    return bsearch(&k, d->words, d->count, sizeof(char *), dict_cmp) != NULL;
}

static void bag_add(Game *g, char ch, int n) { while (n-- > 0 && g->bag_count < MAX_BAG) g->bag[g->bag_count++] = ch; }
static void bag_init(Game *g) {
    g->bag_count = 0;
    bag_add(g,'A',9); bag_add(g,'B',2); bag_add(g,'C',2); bag_add(g,'D',4); bag_add(g,'E',12); bag_add(g,'F',2);
    bag_add(g,'G',3); bag_add(g,'H',2); bag_add(g,'I',9); bag_add(g,'J',1); bag_add(g,'K',1); bag_add(g,'L',4);
    bag_add(g,'M',2); bag_add(g,'N',6); bag_add(g,'O',8); bag_add(g,'P',2); bag_add(g,'Q',1); bag_add(g,'R',6);
    bag_add(g,'S',4); bag_add(g,'T',6); bag_add(g,'U',4); bag_add(g,'V',2); bag_add(g,'W',2); bag_add(g,'X',1);
    bag_add(g,'Y',2); bag_add(g,'Z',1); bag_add(g,'?',2);
}

static char bag_draw(Game *g) {
    if (g->bag_count <= 0) return 0;
    int i = rand() % g->bag_count;
    char ch = g->bag[i];
    g->bag[i] = g->bag[g->bag_count - 1];
    g->bag_count--;
    return ch;
}

static void rack_add(Game *g, int p, char ch) {
    if (g->rack_count[p] >= RACK_N || !ch) return;
    int i = g->rack_count[p]++;
    g->racks[p][i].ch = ch;
    g->racks[p][i].blank = (ch == '?');
}

static void refill_rack(Game *g, int p) { while (g->rack_count[p] < RACK_N && g->bag_count > 0) rack_add(g, p, bag_draw(g)); }

static void rack_remove(Game *g, int p, int idx) {
    if (idx < 0 || idx >= g->rack_count[p]) return;
    for (int i = idx; i < g->rack_count[p] - 1; i++) g->racks[p][i] = g->racks[p][i + 1];
    g->rack_count[p]--;
    if (g->selected_rack == idx) g->selected_rack = -1;
    else if (g->selected_rack > idx) g->selected_rack--;
}

static void clear_board(Game *g) { memset(g->board, 0, sizeof(g->board)); }

static void new_game(Game *g) {
    clear_board(g);
    g->rack_count[0] = g->rack_count[1] = 0;
    g->scores[0] = g->scores[1] = 0;
    g->current_player = 0;
    g->selected_rack = -1;
    g->pass_count = 0;
    g->game_over = false;
    g->mode = MODE_NORMAL;
    memset(g->exchange_selected, 0, sizeof(g->exchange_selected));
    bag_init(g);
    refill_rack(g, 0);
    refill_rack(g, 1);
}

static bool board_has_locked(Game *g) {
    for (int r = 0; r < BOARD_N; r++) for (int c = 0; c < BOARD_N; c++) if (g->board[r][c].locked) return true;
    return false;
}

static bool is_new_tile(Game *g, int r, int c) { return g->board[r][c].ch && !g->board[r][c].locked; }
static bool is_filled(Game *g, int r, int c) { return r >= 0 && r < BOARD_N && c >= 0 && c < BOARD_N && g->board[r][c].ch; }

static int collect_new_tiles(Game *g, int rr[], int cc[]) {
    int n = 0;
    for (int r = 0; r < BOARD_N; r++) for (int c = 0; c < BOARD_N; c++) if (is_new_tile(g, r, c)) { rr[n] = r; cc[n] = c; n++; }
    return n;
}

static int word_score(Game *g, int sr, int sc, int dr, int dc, char *out_word, int out_cap) {
    int wr = 1;
    int total = 0;
    int len = 0;
    int r = sr, c = sc;
    while (r >= 0 && r < BOARD_N && c >= 0 && c < BOARD_N && g->board[r][c].ch) {
        Cell *cell = &g->board[r][c];
        int ls = cell->blank ? 0 : letter_score(cell->ch);
        if (!cell->locked) {
            Premium p = premium_at(r, c);
            if (p == PREM_DL) ls *= 2;
            else if (p == PREM_TL) ls *= 3;
            else if (p == PREM_DW || p == PREM_STAR) wr *= 2;
            else if (p == PREM_TW) wr *= 3;
        }
        total += ls;
        if (out_word && len < out_cap - 1) out_word[len] = cell->ch;
        len++;
        r += dr; c += dc;
    }
    if (out_word) out_word[mini(len, out_cap - 1)] = 0;
    return total * wr;
}

static int word_start(Game *g, int *r, int *c, int dr, int dc) {
    int len = 0;
    while (*r - dr >= 0 && *r - dr < BOARD_N && *c - dc >= 0 && *c - dc < BOARD_N && g->board[*r - dr][*c - dc].ch) { *r -= dr; *c -= dc; }
    int rr = *r, cc = *c;
    while (rr >= 0 && rr < BOARD_N && cc >= 0 && cc < BOARD_N && g->board[rr][cc].ch) { len++; rr += dr; cc += dc; }
    return len;
}

static bool adjacent_locked(Game *g, int r, int c) {
    const int d[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
    for (int i = 0; i < 4; i++) {
        int nr = r + d[i][0], nc = c + d[i][1];
        if (nr >= 0 && nr < BOARD_N && nc >= 0 && nc < BOARD_N && g->board[nr][nc].locked) return true;
    }
    return false;
}

static bool validate_move(Game *g, int *score_out, char *msg, size_t msgn) {
    int rr[RACK_N], cc[RACK_N];
    int n = collect_new_tiles(g, rr, cc);
    if (n <= 0) { safe_strcpy(msg, msgn, "PLACE AT LEAST ONE TILE"); return false; }
    bool first = !board_has_locked(g);
    bool same_row = true, same_col = true;
    for (int i = 1; i < n; i++) {
        if (rr[i] != rr[0]) same_row = false;
        if (cc[i] != cc[0]) same_col = false;
    }
    if (!same_row && !same_col) { safe_strcpy(msg, msgn, "TILES MUST BE IN ONE LINE"); return false; }
    if (first) {
        bool covers_center = false;
        for (int i = 0; i < n; i++) if (rr[i] == 7 && cc[i] == 7) covers_center = true;
        if (!covers_center) { safe_strcpy(msg, msgn, "FIRST MOVE MUST COVER CENTER"); return false; }
    } else {
        bool connected = false;
        for (int i = 0; i < n; i++) if (adjacent_locked(g, rr[i], cc[i])) connected = true;
        if (!connected) { safe_strcpy(msg, msgn, "MOVE MUST CONNECT TO BOARD"); return false; }
    }

    int dr = 0, dc = 0;
    if (n > 1) { dr = same_col ? 1 : 0; dc = same_row ? 1 : 0; }
    else {
        int r = rr[0], c = cc[0];
        if (is_filled(g, r, c - 1) || is_filled(g, r, c + 1)) { dr = 0; dc = 1; }
        else if (is_filled(g, r - 1, c) || is_filled(g, r + 1, c)) { dr = 1; dc = 0; }
        else { safe_strcpy(msg, msgn, "SINGLE TILE MUST FORM A WORD"); return false; }
    }

    int sr = rr[0], sc = cc[0];
    int main_len = word_start(g, &sr, &sc, dr, dc);
    if (main_len < 2) { safe_strcpy(msg, msgn, "MOVE MUST FORM A WORD"); return false; }
    // No gaps in the declared main run: every newly placed tile must lie inside the contiguous main word.
    for (int i = 0; i < n; i++) {
        bool inside = false;
        int r = sr, c = sc;
        for (int k = 0; k < main_len; k++, r += dr, c += dc) if (r == rr[i] && c == cc[i]) inside = true;
        if (!inside) { safe_strcpy(msg, msgn, "TILES CANNOT HAVE GAPS"); return false; }
    }

    int total_score = 0;
    char word[MAX_WORD_LEN];
    int main_score = word_score(g, sr, sc, dr, dc, word, sizeof(word));
    if (!dict_has(&g->dict, word)) { snprintf(msg, msgn, "WORD NOT FOUND: %.40s", word); return false; }
    total_score += main_score;

    int pdr = dc, pdc = dr; // perpendicular vector
    for (int i = 0; i < n; i++) {
        int pr = rr[i], pc = cc[i];
        int wlen = word_start(g, &pr, &pc, pdr, pdc);
        if (wlen > 1) {
            int s = word_score(g, pr, pc, pdr, pdc, word, sizeof(word));
            if (!dict_has(&g->dict, word)) { snprintf(msg, msgn, "WORD NOT FOUND: %.40s", word); return false; }
            total_score += s;
        }
    }
    if (n == 7) total_score += BINGO_BONUS;
    *score_out = total_score;
    safe_strcpy(msg, msgn, "OK");
    return true;
}

static void maybe_game_over(Game *g) {
    if (g->bag_count == 0 && (g->rack_count[0] == 0 || g->rack_count[1] == 0)) {
        int empty = g->rack_count[0] == 0 ? 0 : 1;
        int other = 1 - empty;
        int penalty = 0;
        for (int i = 0; i < g->rack_count[other]; i++) penalty += g->racks[other][i].blank ? 0 : letter_score(g->racks[other][i].ch);
        g->scores[other] -= penalty;
        g->scores[empty] += penalty;
        g->game_over = true;
        g->mode = MODE_GAME_OVER;
    } else if (g->pass_count >= 6) {
        g->game_over = true;
        g->mode = MODE_GAME_OVER;
    }
}

// ---------- Save/load ----------
static void ensure_data_dir(Game *g) {
    char path[768];
    snprintf(path, sizeof(path), "%s/data", g->home);
    mkdir(path, 0755);
}

static void save_game(Game *g) {
    ensure_data_dir(g);
    char path[768], tmp[1024];
    snprintf(path, sizeof(path), "%s/data/save.json", g->home);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "{\n");
    fprintf(f, "  \"version\": %d,\n", SAVE_VERSION);
    fprintf(f, "  \"current_player\": %d,\n", g->current_player);
    fprintf(f, "  \"scores\": [%d, %d],\n", g->scores[0], g->scores[1]);
    fprintf(f, "  \"bag\": \"");
    for (int i = 0; i < g->bag_count; i++) fputc(g->bag[i], f);
    fprintf(f, "\",\n");
    fprintf(f, "  \"racks\": [\"");
    for (int i = 0; i < g->rack_count[0]; i++) fputc(g->racks[0][i].blank ? '?' : g->racks[0][i].ch, f);
    fprintf(f, "\", \"");
    for (int i = 0; i < g->rack_count[1]; i++) fputc(g->racks[1][i].blank ? '?' : g->racks[1][i].ch, f);
    fprintf(f, "\"],\n");
    fprintf(f, "  \"board\": [\n");
    for (int r = 0; r < BOARD_N; r++) {
        fprintf(f, "    \"");
        for (int c = 0; c < BOARD_N; c++) fputc(g->board[r][c].ch ? g->board[r][c].ch : '.', f);
        fprintf(f, r == BOARD_N - 1 ? "\"\n" : "\",\n");
    }
    fprintf(f, "  ],\n  \"locked\": [\n");
    for (int r = 0; r < BOARD_N; r++) {
        fprintf(f, "    \"");
        for (int c = 0; c < BOARD_N; c++) fputc(g->board[r][c].locked ? '1' : '0', f);
        fprintf(f, r == BOARD_N - 1 ? "\"\n" : "\",\n");
    }
    fprintf(f, "  ],\n  \"blank\": [\n");
    for (int r = 0; r < BOARD_N; r++) {
        fprintf(f, "    \"");
        for (int c = 0; c < BOARD_N; c++) fputc(g->board[r][c].blank ? '1' : '0', f);
        fprintf(f, r == BOARD_N - 1 ? "\"\n" : "\",\n");
    }
    fprintf(f, "  ],\n");
    fprintf(f, "  \"pass_count\": %d,\n", g->pass_count);
    fprintf(f, "  \"handoff_pending\": %d,\n", g->mode == MODE_HANDOFF ? 1 : 0);
    fprintf(f, "  \"game_over\": %d\n", g->game_over ? 1 : 0);
    fprintf(f, "}\n");
    fclose(f);
    rename(tmp, path);
}

static char *read_file_all(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f);
    char *buf = malloc(n + 1); if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, n, f); buf[n] = 0; fclose(f); return buf;
}

static int json_int(const char *json, const char *key, int def) {
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    char *p = strstr((char *)json, pat); if (!p) return def;
    p = strchr(p, ':'); if (!p) return def;
    return atoi(p + 1);
}

static bool json_string_after(const char *json, const char *start, int index, char *out, int outn) {
    const char *p = strstr(json, start); if (!p) return false;
    p = strchr(p, '['); if (!p) return false;
    for (int i = 0; i <= index; i++) {
        p = strchr(p, '"'); if (!p) return false;
        p++;
        if (i == index) {
            int j = 0;
            while (*p && *p != '"' && j < outn - 1) out[j++] = *p++;
            out[j] = 0;
            return true;
        }
        p = strchr(p, '"'); if (!p) return false;
        p++;
    }
    return false;
}

static bool json_board_array(const char *json, const char *key, char rows[BOARD_N][BOARD_N + 1]) {
    char pat[64]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat); if (!p) return false;
    p = strchr(p, '['); if (!p) return false;
    for (int r = 0; r < BOARD_N; r++) {
        p = strchr(p, '"'); if (!p) return false;
        p++;
        for (int c = 0; c < BOARD_N; c++) rows[r][c] = p[c] ? p[c] : '.';
        rows[r][BOARD_N] = 0;
        p = strchr(p, '"'); if (!p) return false;
        p++;
    }
    return true;
}

static bool load_game(Game *g) {
    char path[768]; snprintf(path, sizeof(path), "%s/data/save.json", g->home);
    char *json = read_file_all(path); if (!json) return false;
    if (json_int(json, "version", 0) != SAVE_VERSION) { free(json); return false; }
    clear_board(g);
    g->current_player = clampi(json_int(json, "current_player", 0), 0, 1);
    char *scores = strstr(json, "\"scores\"");
    if (scores) { char *b = strchr(scores, '['); if (b) sscanf(b, "[%d , %d]", &g->scores[0], &g->scores[1]); }
    g->bag_count = 0;
    char *bp = strstr(json, "\"bag\"");
    if (bp && (bp = strchr(bp, ':')) && (bp = strchr(bp, '"'))) {
        bp++;
        while (*bp && *bp != '"' && g->bag_count < MAX_BAG) g->bag[g->bag_count++] = *bp++;
    }
    char rack0[32] = {0}, rack1[32] = {0};
    json_string_after(json, "\"racks\"", 0, rack0, sizeof(rack0));
    json_string_after(json, "\"racks\"", 1, rack1, sizeof(rack1));
    g->rack_count[0] = g->rack_count[1] = 0;
    for (int i = 0; rack0[i] && i < RACK_N; i++) rack_add(g, 0, rack0[i]);
    for (int i = 0; rack1[i] && i < RACK_N; i++) rack_add(g, 1, rack1[i]);
    char bro[BOARD_N][BOARD_N + 1], lro[BOARD_N][BOARD_N + 1], xro[BOARD_N][BOARD_N + 1];
    if (!json_board_array(json, "board", bro) || !json_board_array(json, "locked", lro) || !json_board_array(json, "blank", xro)) { free(json); return false; }
    for (int r = 0; r < BOARD_N; r++) for (int c = 0; c < BOARD_N; c++) {
        char ch = bro[r][c];
        g->board[r][c].ch = (ch == '.' ? 0 : ch);
        g->board[r][c].locked = (lro[r][c] == '1');
        g->board[r][c].blank = (xro[r][c] == '1');
    }
    g->pass_count = json_int(json, "pass_count", 0);
    g->game_over = json_int(json, "game_over", 0) != 0;
    g->selected_rack = -1;
    if (g->game_over) g->mode = MODE_GAME_OVER;
    else if (json_int(json, "handoff_pending", 0)) g->mode = MODE_HANDOFF;
    else g->mode = MODE_NORMAL;
    memset(g->exchange_selected, 0, sizeof(g->exchange_selected));
    free(json);
    return true;
}

// ---------- Layout/UI ----------
typedef enum { BTN_SUBMIT, BTN_PASS, BTN_EXCHANGE, BTN_SHUFFLE, BTN_NEW, BTN_EXIT, BTN_COUNT } ButtonId;
typedef struct { int x,y,w,h; const char *label; ButtonId id; } Button;
typedef struct {
    int top_h, board_x, board_y, cell, board_size;
    int rack_y, rack_tile, rack_gap;
    Button buttons[BTN_COUNT];
} Layout;

static Layout L;

static void compute_layout(Layout *l) {
    memset(l, 0, sizeof(*l));
    int w = g_fb.w, h = g_fb.h;
    l->top_h = clampi(h / 13, 56, 96);
    int bottom_h = clampi(h / 5, 170, 310);
    int available = h - l->top_h - bottom_h - 8;
    l->cell = mini(w / BOARD_N, available / BOARD_N);
    l->cell = maxi(l->cell, 28);
    l->board_size = l->cell * BOARD_N;
    l->board_x = (w - l->board_size) / 2;
    l->board_y = l->top_h + 4;
    l->rack_y = l->board_y + l->board_size + 8;
    l->rack_gap = maxi(3, w / 180);
    l->rack_tile = mini((w - 20 - (RACK_N - 1) * l->rack_gap) / RACK_N, clampi(h / 14, 44, 78));
    int btn_y = l->rack_y + l->rack_tile + 10;
    int gap = maxi(4, w / 120);
    int bw = (w - 16 - gap * (BTN_COUNT - 1)) / BTN_COUNT;
    int bh = clampi(h - btn_y - 8, 44, 70);
    const char *labels[BTN_COUNT] = {"SUBMIT","PASS","EXCH","SHUF","NEW","EXIT"};
    for (int i = 0; i < BTN_COUNT; i++) {
        l->buttons[i].x = 8 + i * (bw + gap);
        l->buttons[i].y = btn_y;
        l->buttons[i].w = bw;
        l->buttons[i].h = bh;
        l->buttons[i].label = labels[i];
        l->buttons[i].id = (ButtonId)i;
    }
}

static int scale_for_box_text(const char *s, int w, int h, int maxs) {
    int smax = maxi(1, maxs);
    for (int sc = smax; sc >= 1; sc--) if (text_width(s, sc) <= w - 4 && 7 * sc <= h - 4) return sc;
    return 1;
}

static void draw_button(Button *b, const char *label, bool selected) {
    rect_fill(b->x, b->y, b->w, b->h, selected ? 0 : 255);
    rect_outline(b->x, b->y, b->w, b->h, selected ? 255 : 0, 2);
    int sc = scale_for_box_text(label, b->w, b->h, 3);
    draw_text_center(b->x, b->y, b->w, b->h, label, sc, selected ? 255 : 0);
}

static void draw_tile_box(int x, int y, int s, char ch, bool blank, bool selected, bool locked) {
    uint8_t bg = selected ? 0 : 255;
    uint8_t fg = selected ? 255 : 0;
    rect_fill(x, y, s, s, bg);
    rect_outline(x, y, s, s, fg, locked ? 3 : 2);
    char letter[2] = { ch ? ch : ' ', 0 };
    int sc = clampi(s / 13, 2, 5);
    draw_text_center(x, y + s / 10, s, s * 7 / 10, letter, sc, fg);
    int pts = blank ? 0 : letter_score(ch);
    char pbuf[4]; snprintf(pbuf, sizeof(pbuf), "%d", pts);
    draw_text(x + s - text_width(pbuf, 1) - 4, y + s - 11, pbuf, 1, fg);
}

static void draw_premium(int x, int y, int s, Premium p) {
    rect_fill(x, y, s, s, 255);
    if (p == PREM_NONE) return;
    const char *label = "";
    if (p == PREM_DL) { label = "2L"; hatch(x + 2, y + 2, s - 4, s - 4, maxi(6, s / 5), false); }
    else if (p == PREM_TL) { label = "3L"; hatch(x + 2, y + 2, s - 4, s - 4, maxi(5, s / 6), true); }
    else if (p == PREM_DW) { label = "2W"; rect_outline(x + s/5, y + s/5, s - 2*s/5, s - 2*s/5, 0, 1); }
    else if (p == PREM_TW) { label = "3W"; rect_outline(x + s/7, y + s/7, s - 2*s/7, s - 2*s/7, 0, 1); rect_outline(x + s/3, y + s/3, s - 2*s/3, s - 2*s/3, 0, 1); }
    else if (p == PREM_STAR) label = "*";
    int sc = scale_for_box_text(label, s - 2, s - 2, maxi(1, s / 18));
    draw_text_center(x, y, s, s, label, sc, 0);
}

static void draw_status(Game *g) {
    rect_fill(0, 0, g_fb.w, L.top_h, 255);
    rect_fill(0, L.top_h - 2, g_fb.w, 2, 0);
    char line1[128];
    snprintf(line1, sizeof(line1), "P%d TURN   P1 %d   P2 %d", g->current_player + 1, g->scores[0], g->scores[1]);
    int sc = scale_for_box_text(line1, g_fb.w - 16, L.top_h / 2, 4);
    draw_text(8, 8, line1, sc, 0);
    char line2[128];
    snprintf(line2, sizeof(line2), "TILES LEFT %d   DICT %d", g->bag_count, g->dict.count);
    int sc2 = scale_for_box_text(line2, g_fb.w - 16, L.top_h / 2, 3);
    draw_text(8, L.top_h / 2 + 4, line2, sc2, 0);
}

static void draw_board(Game *g) {
    for (int r = 0; r < BOARD_N; r++) for (int c = 0; c < BOARD_N; c++) {
        int x = L.board_x + c * L.cell;
        int y = L.board_y + r * L.cell;
        Cell *cell = &g->board[r][c];
        if (cell->ch) draw_tile_box(x + 1, y + 1, L.cell - 2, cell->ch, cell->blank, !cell->locked, cell->locked);
        else draw_premium(x + 1, y + 1, L.cell - 2, premium_at(r, c));
        rect_outline(x, y, L.cell, L.cell, 0, 1);
    }
}

static void draw_rack(Game *g) {
    int p = g->current_player;
    int start_x = (g_fb.w - (RACK_N * L.rack_tile + (RACK_N - 1) * L.rack_gap)) / 2;
    for (int i = 0; i < RACK_N; i++) {
        int x = start_x + i * (L.rack_tile + L.rack_gap);
        bool sel = (g->selected_rack == i) || (g->mode == MODE_EXCHANGE && g->exchange_selected[i]);
        if (i < g->rack_count[p]) draw_tile_box(x, L.rack_y, L.rack_tile, g->racks[p][i].ch, g->racks[p][i].blank, sel, false);
        else { rect_fill(x, L.rack_y, L.rack_tile, L.rack_tile, 255); rect_outline(x, L.rack_y, L.rack_tile, L.rack_tile, 0, 1); }
    }
}

static void draw_popup_box(int x, int y, int w, int h) {
    rect_fill(x, y, w, h, 255);
    rect_outline(x, y, w, h, 0, 4);
}

static void draw_popup(Game *g) {
    if (g->mode == MODE_NORMAL || g->mode == MODE_EXCHANGE) return;
    int w = g_fb.w * 82 / 100;
    int h = g_fb.h * 34 / 100;
    int x = (g_fb.w - w) / 2, y = (g_fb.h - h) / 2;
    draw_popup_box(x, y, w, h);
    if (g->mode == MODE_INVALID) {
        draw_text_center(x + 10, y + 20, w - 20, 40, "INVALID MOVE", 4, 0);
        int sc = scale_for_box_text(g->invalid_msg, w - 30, 40, 3);
        draw_text_center(x + 15, y + h / 2 - 20, w - 30, 40, g->invalid_msg, sc, 0);
        draw_text_center(x + 10, y + h - 58, w - 20, 40, "TAP TO CLOSE", 3, 0);
    } else if (g->mode == MODE_CONFIRM_NEW) {
        draw_text_center(x + 10, y + 20, w - 20, 40, "NEW GAME?", 4, 0);
        draw_text_center(x + 10, y + h / 2 - 20, w - 20, 40, "CURRENT GAME WILL BE LOST", 2, 0);
        rect_outline(x + 20, y + h - 70, w/2 - 35, 50, 0, 2);
        rect_outline(x + w/2 + 15, y + h - 70, w/2 - 35, 50, 0, 2);
        draw_text_center(x + 20, y + h - 70, w/2 - 35, 50, "YES", 3, 0);
        draw_text_center(x + w/2 + 15, y + h - 70, w/2 - 35, 50, "NO", 3, 0);
    } else if (g->mode == MODE_BLANK) {
        draw_text_center(x + 10, y + 12, w - 20, 32, "CHOOSE BLANK LETTER", 3, 0);
        int grid_x = x + 22, grid_y = y + 54;
        int bw = (w - 44) / 7, bh = (h - 82) / 4;
        for (int i = 0; i < 26; i++) {
            int cx = grid_x + (i % 7) * bw;
            int cy = grid_y + (i / 7) * bh;
            rect_outline(cx + 2, cy + 2, bw - 4, bh - 4, 0, 1);
            char s[2] = { (char)('A' + i), 0 };
            draw_text_center(cx + 2, cy + 2, bw - 4, bh - 4, s, scale_for_box_text(s, bw - 4, bh - 4, 4), 0);
        }
    } else if (g->mode == MODE_GAME_OVER) {
        draw_text_center(x + 10, y + 20, w - 20, 40, "GAME OVER", 4, 0);
        char msg[96];
        if (g->scores[0] > g->scores[1]) snprintf(msg, sizeof(msg), "PLAYER 1 WINS  %d-%d", g->scores[0], g->scores[1]);
        else if (g->scores[1] > g->scores[0]) snprintf(msg, sizeof(msg), "PLAYER 2 WINS  %d-%d", g->scores[1], g->scores[0]);
        else snprintf(msg, sizeof(msg), "DRAW  %d-%d", g->scores[0], g->scores[1]);
        draw_text_center(x + 10, y + h/2 - 20, w - 20, 40, msg, scale_for_box_text(msg, w - 20, 40, 3), 0);
        draw_text_center(x + 10, y + h - 58, w - 20, 40, "NEW OR EXIT", 3, 0);
    }
}

static void draw_exchange_hint(Game *g) {
    if (g->mode != MODE_EXCHANGE) return;
    int y = L.rack_y - 32;
    rect_fill(0, y, g_fb.w, 28, 255);
    char msg[128]; snprintf(msg, sizeof(msg), "EXCHANGE: TAP RACK TILES, SUBMIT TO CONFIRM, EXCH TO CANCEL");
    draw_text_center(4, y, g_fb.w - 8, 28, msg, scale_for_box_text(msg, g_fb.w - 8, 28, 2), 0);
}

static void handoff_button_rect(int *x, int *y, int *w, int *h) {
    *w = g_fb.w * 56 / 100;
    *h = maxi(70, g_fb.h * 9 / 100);
    *x = (g_fb.w - *w) / 2;
    *y = g_fb.h * 66 / 100;
}

static void draw_handoff_screen(Game *g) {
    rect_fill(0, 0, g_fb.w, g_fb.h, 255);
    int box_w = g_fb.w * 84 / 100;
    int box_h = g_fb.h * 46 / 100;
    int box_x = (g_fb.w - box_w) / 2;
    int box_y = (g_fb.h - box_h) / 2;
    draw_popup_box(box_x, box_y, box_w, box_h);

    draw_text_center(box_x + 12, box_y + 30, box_w - 24, 56, "PASS KINDLE", 5, 0);
    char msg[64];
    snprintf(msg, sizeof(msg), "PLAYER %d'S TURN", g->current_player + 1);
    draw_text_center(box_x + 12, box_y + box_h / 2 - 28, box_w - 24, 56, msg, scale_for_box_text(msg, box_w - 24, 56, 4), 0);
    draw_text_center(box_x + 12, box_y + box_h / 2 + 26, box_w - 24, 34, "RACK HIDDEN UNTIL CONFIRM", 2, 0);

    int bx, by, bw, bh;
    handoff_button_rect(&bx, &by, &bw, &bh);
    rect_outline(bx, by, bw, bh, 0, 4);
    draw_text_center(bx, by, bw, bh, "CONFIRM", scale_for_box_text("CONFIRM", bw - 20, bh - 12, 5), 0);
}

static void draw_all(Game *g) {
    compute_layout(&L);
    if (g->mode == MODE_HANDOFF) {
        draw_handoff_screen(g);
        ioctl(g_fb.fd, FBIOPAN_DISPLAY, &g_fb.vinfo);
        return;
    }
    rect_fill(0, 0, g_fb.w, g_fb.h, 255);
    draw_status(g);
    draw_board(g);
    draw_exchange_hint(g);
    draw_rack(g);
    for (int i = 0; i < BTN_COUNT; i++) {
        const char *label = L.buttons[i].label;
        if (g->mode == MODE_EXCHANGE && i == BTN_SUBMIT) label = "DONE";
        if (g->mode == MODE_EXCHANGE && i == BTN_EXCHANGE) label = "CANCEL";
        draw_button(&L.buttons[i], label, false);
    }
    draw_popup(g);
    ioctl(g_fb.fd, FBIOPAN_DISPLAY, &g_fb.vinfo);
}

// ---------- Input handling ----------
static void set_invalid(Game *g, const char *msg) {
    safe_strcpy(g->invalid_msg, sizeof(g->invalid_msg), msg);
    g->mode = MODE_INVALID;
}

static void enter_handoff(Game *g) {
    g->selected_rack = -1;
    memset(g->exchange_selected, 0, sizeof(g->exchange_selected));
    if (!g->game_over) g->mode = MODE_HANDOFF;
}

static void submit_move(Game *g) {
    int score = 0; char msg[160];
    if (!validate_move(g, &score, msg, sizeof(msg))) { set_invalid(g, msg); return; }
    for (int r = 0; r < BOARD_N; r++) for (int c = 0; c < BOARD_N; c++) if (is_new_tile(g, r, c)) g->board[r][c].locked = true;
    g->scores[g->current_player] += score;
    refill_rack(g, g->current_player);
    g->current_player = 1 - g->current_player;
    g->selected_rack = -1;
    g->pass_count = 0;
    maybe_game_over(g);
    enter_handoff(g);
    save_game(g);
}

static void cancel_unsubmitted(Game *g) {
    int p = g->current_player;
    for (int r = 0; r < BOARD_N; r++) for (int c = 0; c < BOARD_N; c++) if (is_new_tile(g, r, c)) {
        rack_add(g, p, g->board[r][c].blank ? '?' : g->board[r][c].ch);
        memset(&g->board[r][c], 0, sizeof(Cell));
    }
    g->selected_rack = -1;
}

static void pass_turn(Game *g) {
    cancel_unsubmitted(g);
    g->current_player = 1 - g->current_player;
    g->pass_count++;
    maybe_game_over(g);
    enter_handoff(g);
    save_game(g);
}

static void do_exchange(Game *g) {
    int p = g->current_player;
    int selected = 0;
    for (int i = 0; i < g->rack_count[p]; i++) if (g->exchange_selected[i]) selected++;
    if (selected == 0) { g->mode = MODE_NORMAL; return; }
    if (g->bag_count < selected) { set_invalid(g, "NOT ENOUGH TILES IN BAG"); memset(g->exchange_selected,0,sizeof(g->exchange_selected)); return; }
    for (int i = g->rack_count[p] - 1; i >= 0; i--) if (g->exchange_selected[i]) {
        if (g->bag_count < MAX_BAG) g->bag[g->bag_count++] = g->racks[p][i].blank ? '?' : g->racks[p][i].ch;
        rack_remove(g, p, i);
    }
    refill_rack(g, p);
    memset(g->exchange_selected,0,sizeof(g->exchange_selected));
    g->current_player = 1 - g->current_player;
    g->pass_count++;
    maybe_game_over(g);
    enter_handoff(g);
    save_game(g);
}

static void shuffle_rack(Game *g) {
    int p = g->current_player;
    for (int i = g->rack_count[p] - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        Tile t = g->racks[p][i]; g->racks[p][i] = g->racks[p][j]; g->racks[p][j] = t;
    }
    g->selected_rack = -1;
    save_game(g);
}

static int rack_index_at(int x, int y) {
    int start_x = (g_fb.w - (RACK_N * L.rack_tile + (RACK_N - 1) * L.rack_gap)) / 2;
    if (y < L.rack_y || y > L.rack_y + L.rack_tile) return -1;
    for (int i = 0; i < RACK_N; i++) {
        int rx = start_x + i * (L.rack_tile + L.rack_gap);
        if (x >= rx && x < rx + L.rack_tile) return i;
    }
    return -1;
}

static bool board_pos_at(int x, int y, int *r, int *c) {
    if (x < L.board_x || y < L.board_y || x >= L.board_x + L.board_size || y >= L.board_y + L.board_size) return false;
    *c = (x - L.board_x) / L.cell;
    *r = (y - L.board_y) / L.cell;
    return *r >= 0 && *r < BOARD_N && *c >= 0 && *c < BOARD_N;
}

static int button_at(int x, int y) {
    for (int i = 0; i < BTN_COUNT; i++) if (x >= L.buttons[i].x && x < L.buttons[i].x + L.buttons[i].w && y >= L.buttons[i].y && y < L.buttons[i].y + L.buttons[i].h) return i;
    return -1;
}

static void handle_blank_popup(Game *g, int x, int y) {
    int w = g_fb.w * 82 / 100;
    int h = g_fb.h * 34 / 100;
    int bx = (g_fb.w - w) / 2, by = (g_fb.h - h) / 2;
    int grid_x = bx + 22, grid_y = by + 54;
    int bw = (w - 44) / 7, bh = (h - 82) / 4;
    if (x < grid_x || y < grid_y) return;
    int col = (x - grid_x) / bw, row = (y - grid_y) / bh;
    int idx = row * 7 + col;
    if (idx < 0 || idx >= 26) return;
    char ch = (char)('A' + idx);
    int p = g->current_player;
    int ri = g->blank_rack;
    if (ri >= 0 && ri < g->rack_count[p]) {
        g->board[g->blank_r][g->blank_c].ch = ch;
        g->board[g->blank_r][g->blank_c].blank = true;
        g->board[g->blank_r][g->blank_c].locked = false;
        rack_remove(g, p, ri);
    }
    g->selected_rack = -1;
    g->mode = MODE_NORMAL;
    save_game(g);
}

static void handle_click(Game *g, int x, int y) {
    compute_layout(&L);
    if (g->mode == MODE_HANDOFF) {
        int bx, by, bw, bh;
        handoff_button_rect(&bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            g->mode = MODE_NORMAL;
            save_game(g);
        }
        return;
    }
    if (g->mode == MODE_INVALID) { g->mode = MODE_NORMAL; return; }
    if (g->mode == MODE_CONFIRM_NEW) {
        int w = g_fb.w * 82 / 100, h = g_fb.h * 34 / 100;
        int bx = (g_fb.w - w) / 2, by = (g_fb.h - h) / 2;
        if (y >= by + h - 70 && y <= by + h - 20) {
            if (x >= bx + 20 && x <= bx + w/2 - 15) { new_game(g); save_game(g); }
            else g->mode = MODE_NORMAL;
        } else g->mode = MODE_NORMAL;
        return;
    }
    if (g->mode == MODE_BLANK) { handle_blank_popup(g, x, y); return; }

    int b = button_at(x, y);
    if (b >= 0) {
        if (b == BTN_EXIT) { save_game(g); fb_close(&g_fb); exit(0); }
        if (b == BTN_NEW) { g->mode = MODE_CONFIRM_NEW; return; }
        if (g->game_over) return;
        if (g->mode == MODE_EXCHANGE) {
            if (b == BTN_SUBMIT) { do_exchange(g); return; }
            if (b == BTN_EXCHANGE) { memset(g->exchange_selected,0,sizeof(g->exchange_selected)); g->mode = MODE_NORMAL; return; }
            return;
        }
        if (b == BTN_SUBMIT) { submit_move(g); return; }
        if (b == BTN_PASS) { pass_turn(g); return; }
        if (b == BTN_EXCHANGE) { cancel_unsubmitted(g); memset(g->exchange_selected,0,sizeof(g->exchange_selected)); g->mode = MODE_EXCHANGE; return; }
        if (b == BTN_SHUFFLE) { shuffle_rack(g); return; }
    }

    if (g->game_over) return;
    int ri = rack_index_at(x, y);
    int p = g->current_player;
    if (ri >= 0 && ri < g->rack_count[p]) {
        if (g->mode == MODE_EXCHANGE) g->exchange_selected[ri] = !g->exchange_selected[ri];
        else g->selected_rack = (g->selected_rack == ri) ? -1 : ri;
        return;
    }

    int r, c;
    if (board_pos_at(x, y, &r, &c)) {
        if (g->board[r][c].ch && !g->board[r][c].locked) {
            rack_add(g, p, g->board[r][c].blank ? '?' : g->board[r][c].ch);
            memset(&g->board[r][c], 0, sizeof(Cell));
            save_game(g);
            return;
        }
        if (!g->board[r][c].ch && g->selected_rack >= 0 && g->selected_rack < g->rack_count[p]) {
            int sel = g->selected_rack;
            Tile t = g->racks[p][sel];
            if (t.blank || t.ch == '?') {
                g->blank_r = r; g->blank_c = c; g->blank_rack = sel; g->mode = MODE_BLANK;
            } else {
                g->board[r][c].ch = t.ch;
                g->board[r][c].locked = false;
                g->board[r][c].blank = false;
                rack_remove(g, p, sel);
                save_game(g);
            }
        }
    }
}

static int test_bit(const unsigned long *bits, int bit) { return (bits[bit / (8 * sizeof(unsigned long))] >> (bit % (8 * sizeof(unsigned long)))) & 1; }

static int open_touch_device(int *x_min, int *x_max, int *y_min, int *y_max, bool *mt) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *de;
    char path[512];
    int best = -1;
    while ((de = readdir(d))) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof(path), "/dev/input/%s", de->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        unsigned long evbits[(EV_MAX + 8 * sizeof(long)) / (8 * sizeof(long))];
        unsigned long absbits[(ABS_MAX + 8 * sizeof(long)) / (8 * sizeof(long))];
        memset(evbits,0,sizeof(evbits)); memset(absbits,0,sizeof(absbits));
        ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
        bool has_mt = test_bit(absbits, ABS_MT_POSITION_X) && test_bit(absbits, ABS_MT_POSITION_Y);
        bool has_abs = test_bit(absbits, ABS_X) && test_bit(absbits, ABS_Y);
        if (test_bit(evbits, EV_ABS) && (has_mt || has_abs)) {
            struct input_absinfo ax, ay;
            int xcode = has_mt ? ABS_MT_POSITION_X : ABS_X;
            int ycode = has_mt ? ABS_MT_POSITION_Y : ABS_Y;
            if (ioctl(fd, EVIOCGABS(xcode), &ax) == 0 && ioctl(fd, EVIOCGABS(ycode), &ay) == 0) {
                *x_min = ax.minimum; *x_max = ax.maximum; *y_min = ay.minimum; *y_max = ay.maximum; *mt = has_mt; best = fd; break;
            }
        }
        close(fd);
    }
    closedir(d);
    return best;
}

static int map_coord(int v, int minv, int maxv, int outmax) {
    if (maxv == minv) return v;
    long n = (long)(v - minv) * (outmax - 1) / (maxv - minv);
    return clampi((int)n, 0, outmax - 1);
}

static void event_loop(Game *g) {
    int xmin=0,xmax=g_fb.w-1,ymin=0,ymax=g_fb.h-1; bool mt=false;
    int fd = open_touch_device(&xmin,&xmax,&ymin,&ymax,&mt);
    if (fd < 0) {
        set_invalid(g, "NO TOUCH DEVICE FOUND");
        while (1) { draw_all(g); sleep(1); }
    }
    struct input_event ev;
    int rawx = xmin, rawy = ymin, sx = 0, sy = 0;
    bool down = false, had_down = false;
    while (1) {
        ssize_t n = read(fd, &ev, sizeof(ev));
        if (n != sizeof(ev)) continue;
        if (ev.type == EV_ABS) {
            if (ev.code == ABS_MT_TRACKING_ID) {
                if (ev.value < 0) {
                    if (down || had_down) { handle_click(g, sx, sy); draw_all(g); }
                    down = false; had_down = false;
                } else {
                    down = true; had_down = true;
                }
                continue;
            }
            if ((!mt && ev.code == ABS_X) || (mt && ev.code == ABS_MT_POSITION_X)) rawx = ev.value;
            if ((!mt && ev.code == ABS_Y) || (mt && ev.code == ABS_MT_POSITION_Y)) rawy = ev.value;
            sx = map_coord(rawx, xmin, xmax, g_fb.w);
            sy = map_coord(rawy, ymin, ymax, g_fb.h);
        } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            if (ev.value) { down = true; had_down = true; }
            else if (down || had_down) { down = false; had_down = false; handle_click(g, sx, sy); draw_all(g); }
        }
    }
}

static void init_home(Game *g, const char *argv0) {
    const char *env = getenv("TILEWORDS_HOME");
    if (env && *env) { safe_strcpy(g->home, sizeof(g->home), env); return; }
    // Fallback: assume launched from extension directory.
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd))) safe_strcpy(g->home, sizeof(g->home), cwd);
    else safe_strcpy(g->home, sizeof(g->home), "/mnt/us/extensions/tilewords");
    (void)argv0;
}

int main(int argc, char **argv) {
    srand((unsigned)time(NULL) ^ (unsigned)getpid());
    memset(&G, 0, sizeof(G));
    init_home(&G, argc > 0 ? argv[0] : NULL);
    dict_load(&G);
    if (!load_game(&G)) new_game(&G);
    if (fb_open(&g_fb) != 0) {
        fprintf(stderr, "TileWords: cannot open /dev/fb0: %s\n", strerror(errno));
        return 1;
    }
    draw_all(&G);
    event_loop(&G);
    fb_close(&g_fb);
    dict_free(&G.dict);
    return 0;
}
