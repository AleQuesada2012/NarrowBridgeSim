/*
 * gui.c — Bridge Simulation Visualizer (SDL2 port)
 *
 * Reads structured log lines from the simulation via stdin (pipe),
 * maintains a live state, and redraws an SDL2 window at ~15 fps.
 *
 * No external font library is required: text is drawn with a compact
 * 5×7 pixel bitmap font baked directly into this file.
 *
 * Recognised log formats (all others are displayed in the log panel):
 *   [SLOT <id> <slot> <bridge_len> <EAST|WEST> <is_ambulance>]
 *   [QUEUE <EAST|WEST> <total> <ambulances>]
 *   [DIRECTION <EAST|WEST|NONE>]
 *
 * Compile (Linux):
 *   gcc gui.c -lSDL2 -lpthread -lm -o bridge_gui
 *
 * Compile (macOS, Homebrew SDL2):
 *   gcc gui.c $(sdl2-config --cflags --libs) -lpthread -lm -o bridge_gui
 *
 * Run (piped from simulation):
 *   ./bridge_sim bridge.config | ./bridge_gui
 */

#include <SDL2/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

/* ============================================================ */
/* =====================  Constants  ========================== */
/* ============================================================ */

#define WIN_W           1366
#define WIN_H            768

/* Bridge rendering — centred in the window */
#define BRIDGE_X         203
#define BRIDGE_Y         270
#define BRIDGE_W         960
#define BRIDGE_H          80
#define METER_TICK_H      10

/*
 * Each side has a fixed-width zone for its queue.
 * EAST-bound vehicles (→) wait on the LEFT  (west entrance of bridge).
 * WEST-bound vehicles (←) wait on the RIGHT (east entrance of bridge).
 *
 * Zone layout:
 *   [EAST queue zone 0..BRIDGE_X-1] [bridge] [WEST queue zone BRIDGE_X+BRIDGE_W..WIN_W-1]
 */
#define QUEUE_ZONE_W     200   /* pixels reserved on each side */
#define QUEUE_LANE_H      50
#define QUEUE_SLOT_W      36

/* Vehicle drawing */
#define CAR_W             28
#define CAR_H             22
#define AMB_W             32
#define AMB_H             24

/* Log panel */
#define LOG_X             10
#define LOG_Y            530
#define LOG_W           1346
#define LOG_H            228
#define LOG_MAX_LINES     13
#define LOG_LINE_H        17

/* Max vehicles tracked simultaneously */
#define MAX_VEHICLES     512

/* Refresh interval ms (~15 fps) */
#define REFRESH_MS        66

/* ============================================================ */
/* =====================  Colour Helpers  ===================== */
/* ============================================================ */

typedef struct { Uint8 r, g, b, a; } Colour;

#define COL(r,g,b)      ((Colour){(r),(g),(b),255})
#define COL_A(r,g,b,a)  ((Colour){(r),(g),(b),(a)})

static void set_col(SDL_Renderer *ren, Colour c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
}

/* Colour palette */
static const Colour
    C_BG           = COL( 18,  20,  28),
    C_ROAD         = COL( 60,  65,  80),
    C_RAIL         = COL(120, 130, 150),
    C_GREEN        = COL( 80, 200,  80),
    C_RED_LIGHT    = COL(200,  80,  80),
    C_NO_LIGHT     = COL( 60,  60,  60),
    C_CAR_EAST     = COL( 80, 160, 255),
    C_CAR_WEST     = COL(255, 160,  60),
    C_AMB          = COL(255,  60,  60),
    C_QUEUE_BG     = COL( 30,  34,  48),
    C_TEXT         = COL(220, 230, 255),
    C_TEXT_DIM     = COL(100, 115, 150),
    C_PANEL_BG     = COL( 22,  26,  38),
    C_GRID         = COL( 40,  46,  64),
    C_EAST_LABEL   = COL( 80, 160, 255),
    C_WEST_LABEL   = COL(255, 160,  60),
    C_WHITE        = COL(255, 255, 255),
    C_BLACK        = COL(  0,   0,   0),
    C_AMBER        = COL(255, 200,   0),
    C_ENTRY_GREEN  = COL( 80, 200,  80);

/* ============================================================ */
/* ================  5×7 Bitmap Font  ======================== */
/* ============================================================ */

/*
 * Each character is encoded as 5 bytes, one per column (left→right).
 * Within each byte, bit 0 = top row, bit 6 = bottom row (7 rows used).
 * Printable ASCII starting at 0x20 (space).
 */
static const Uint8 FONT5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x00,0x00,0x5F,0x00,0x00}, /* '!' */
    {0x00,0x07,0x00,0x07,0x00}, /* '"' */
    {0x14,0x7F,0x14,0x7F,0x14}, /* '#' */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$' */
    {0x23,0x13,0x08,0x64,0x62}, /* '%' */
    {0x36,0x49,0x55,0x22,0x50}, /* '&' */
    {0x00,0x05,0x03,0x00,0x00}, /* ''' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '(' */
    {0x00,0x41,0x22,0x1C,0x00}, /* ')' */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*' */
    {0x08,0x08,0x3E,0x08,0x08}, /* '+' */
    {0x00,0x50,0x30,0x00,0x00}, /* ',' */
    {0x08,0x08,0x08,0x08,0x08}, /* '-' */
    {0x00,0x60,0x60,0x00,0x00}, /* '.' */
    {0x20,0x10,0x08,0x04,0x02}, /* '/' */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0' */
    {0x00,0x42,0x7F,0x40,0x00}, /* '1' */
    {0x42,0x61,0x51,0x49,0x46}, /* '2' */
    {0x21,0x41,0x45,0x4B,0x31}, /* '3' */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4' */
    {0x27,0x45,0x45,0x45,0x39}, /* '5' */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6' */
    {0x01,0x71,0x09,0x05,0x03}, /* '7' */
    {0x36,0x49,0x49,0x49,0x36}, /* '8' */
    {0x06,0x49,0x49,0x29,0x1E}, /* '9' */
    {0x00,0x36,0x36,0x00,0x00}, /* ':' */
    {0x00,0x56,0x36,0x00,0x00}, /* ';' */
    {0x08,0x14,0x22,0x41,0x00}, /* '<' */
    {0x14,0x14,0x14,0x14,0x14}, /* '=' */
    {0x00,0x41,0x22,0x14,0x08}, /* '>' */
    {0x02,0x01,0x51,0x09,0x06}, /* '?' */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@' */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 'A' */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B' */
    {0x3E,0x41,0x41,0x41,0x22}, /* 'C' */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D' */
    {0x7F,0x49,0x49,0x49,0x41}, /* 'E' */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F' */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 'G' */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H' */
    {0x00,0x41,0x7F,0x41,0x00}, /* 'I' */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J' */
    {0x7F,0x08,0x14,0x22,0x41}, /* 'K' */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L' */
    {0x7F,0x02,0x0C,0x02,0x7F}, /* 'M' */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N' */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 'O' */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P' */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q' */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R' */
    {0x46,0x49,0x49,0x49,0x31}, /* 'S' */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T' */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 'U' */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V' */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 'W' */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X' */
    {0x07,0x08,0x70,0x08,0x07}, /* 'Y' */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z' */
    {0x00,0x7F,0x41,0x41,0x00}, /* '[' */
    {0x02,0x04,0x08,0x10,0x20}, /* '\' */
    {0x00,0x41,0x41,0x7F,0x00}, /* ']' */
    {0x04,0x02,0x01,0x02,0x04}, /* '^' */
    {0x40,0x40,0x40,0x40,0x40}, /* '_' */
    {0x00,0x01,0x02,0x04,0x00}, /* '`' */
    {0x20,0x54,0x54,0x54,0x78}, /* 'a' */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b' */
    {0x38,0x44,0x44,0x44,0x20}, /* 'c' */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd' */
    {0x38,0x54,0x54,0x54,0x18}, /* 'e' */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f' */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 'g' */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h' */
    {0x00,0x44,0x7D,0x40,0x00}, /* 'i' */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j' */
    {0x7F,0x10,0x28,0x44,0x00}, /* 'k' */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l' */
    {0x7C,0x04,0x18,0x04,0x78}, /* 'm' */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n' */
    {0x38,0x44,0x44,0x44,0x38}, /* 'o' */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p' */
    {0x08,0x14,0x14,0x18,0x7C}, /* 'q' */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r' */
    {0x48,0x54,0x54,0x54,0x20}, /* 's' */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't' */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 'u' */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v' */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 'w' */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x' */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 'y' */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z' */
    {0x00,0x08,0x36,0x41,0x00}, /* '{' */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|' */
    {0x00,0x41,0x36,0x08,0x00}, /* '}' */
    {0x10,0x08,0x08,0x10,0x08}, /* '~' */
};

#define FONT_CHAR_W   5    /* pixels per column */
#define FONT_CHAR_H   7    /* pixel rows used   */
#define FONT_SCALE    2    /* render at 2× → 10×14 per glyph */
#define FONT_GAP      2    /* pixels between characters */
#define FONT_ADVANCE  (FONT_CHAR_W * FONT_SCALE + FONT_GAP)

/*
 * Draw a single character at (x,y) with the given colour and scale.
 * Returns the x position after the character.
 */
static int draw_char(SDL_Renderer *ren, int x, int y,
                     char ch, Colour col, int scale)
{
    int idx = (unsigned char)ch - 0x20;
    if (idx < 0 || idx >= (int)(sizeof(FONT5X7)/sizeof(FONT5X7[0])))
        idx = 0;

    set_col(ren, col);
    for (int col_i = 0; col_i < FONT_CHAR_W; col_i++) {
        Uint8 bits = FONT5X7[idx][col_i];
        for (int row = 0; row < FONT_CHAR_H; row++) {
            if (bits & (1 << row)) {
                SDL_Rect px = {
                    x + col_i * scale,
                    y + row   * scale,
                    scale, scale
                };
                SDL_RenderFillRect(ren, &px);
            }
        }
    }
    return x + FONT_CHAR_W * scale + FONT_GAP;
}

/*
 * Draw a null-terminated string. Returns final x position.
 */
static int draw_str(SDL_Renderer *ren, int x, int y,
                    const char *s, Colour col, int scale)
{
    while (*s)
        x = draw_char(ren, x, y, *s++, col, scale);
    return x;
}

/* Convenience wrappers matching the old X11 call sites */
static void draw_text(SDL_Renderer *ren, int x, int y,
                      const char *s, Colour col)
{
    draw_str(ren, x, y, s, col, FONT_SCALE);
}

static void draw_text_bold(SDL_Renderer *ren, int x, int y,
                           const char *s, Colour col)
{
    /* "Bold" = draw at 2× scale with a 1-pixel shadow for depth */
    draw_str(ren, x+1, y+1, s, C_BLACK, FONT_SCALE);
    draw_str(ren, x,   y,   s, col,     FONT_SCALE);
}

/* Width of a string in pixels at FONT_SCALE */
static int text_width(const char *s)
{
    return (int)strlen(s) * FONT_ADVANCE;
}

/* ============================================================ */
/* ===================  Drawing Helpers  ====================== */
/* ============================================================ */

/* Filled rounded rectangle — approximated with three overlapping rects
 * (horizontal slab + vertical slab) and four filled circles at corners. */
static void fill_rect_r(SDL_Renderer *ren, int x, int y, int w, int h,
                         Colour col, int r)
{
    if (r < 1) r = 1;
    set_col(ren, col);
    SDL_Rect rh = {x,     y + r, w,     h - 2*r};
    SDL_Rect rv = {x + r, y,     w-2*r, h      };
    SDL_RenderFillRect(ren, &rh);
    SDL_RenderFillRect(ren, &rv);
    /* corners (filled circles via filled squares — good enough at this scale) */
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                SDL_Rect px;
                px.w = px.h = 1;
                px.x = x     + dx; px.y = y     + dy; SDL_RenderFillRect(ren, &px);
                px.x = x+w-1 - dx; px.y = y     + dy; SDL_RenderFillRect(ren, &px);
                px.x = x     + dx; px.y = y+h-1 - dy; SDL_RenderFillRect(ren, &px);
                px.x = x+w-1 - dx; px.y = y+h-1 - dy; SDL_RenderFillRect(ren, &px);
            }
        }
    }
}

/* Filled circle */
static void fill_circle(SDL_Renderer *ren, int cx, int cy, int r, Colour col)
{
    set_col(ren, col);
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                SDL_RenderDrawPoint(ren, cx + dx, cy + dy);
            }
        }
    }
}

/* ============================================================ */
/* =====================  State Types  ======================== */
/* ============================================================ */

typedef enum { DIR_EAST = 0, DIR_WEST = 1, DIR_NONE = 2 } GuiDir;

typedef struct {
    int    id;
    int    slot;
    int    bridge_len;
    GuiDir direction;
    int    is_ambulance;
    int    active;
} VehicleState;

typedef struct {
    int total;
    int ambulances;
} QueueState;

/* ============================================================ */
/* =====================  Shared State  ======================= */
/* ============================================================ */

static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static VehicleState vehicles[MAX_VEHICLES];
static int          bridge_length      = 10;
static GuiDir       bridge_dir         = DIR_NONE;
static QueueState   queue_east         = {0, 0};
static QueueState   queue_west         = {0, 0};
static int          total_east_passed  = 0;
static int          total_west_passed  = 0;
static int          sim_done           = 0;

#define LOG_BUF_LINES 200
static char log_buf[LOG_BUF_LINES][256];
static int  log_head  = 0;
static int  log_count = 0;

/* ============================================================ */
/* =====================  Log Buffer  ========================= */
/* ============================================================ */

static void log_push(const char *line)
{
    strncpy(log_buf[log_head], line, 255);
    log_buf[log_head][255] = '\0';
    char *nl = strchr(log_buf[log_head], '\n');
    if (nl) *nl = '\0';
    log_head = (log_head + 1) % LOG_BUF_LINES;
    if (log_count < LOG_BUF_LINES) log_count++;
}

static int log_recent(char out[][256], int n)
{
    int avail = (log_count < n) ? log_count : n;
    int start = (log_head - avail + LOG_BUF_LINES) % LOG_BUF_LINES;
    for (int i = 0; i < avail; i++)
        strncpy(out[i], log_buf[(start + i) % LOG_BUF_LINES], 255);
    return avail;
}

/* ============================================================ */
/* =====================  Vehicle Lookup  ===================== */
/* ============================================================ */

static VehicleState *find_vehicle(int id)
{
    for (int i = 0; i < MAX_VEHICLES; i++)
        if (vehicles[i].active && vehicles[i].id == id)
            return &vehicles[i];
    return NULL;
}

static VehicleState *alloc_vehicle(int id)
{
    for (int i = 0; i < MAX_VEHICLES; i++)
        if (!vehicles[i].active) {
            vehicles[i].id     = id;
            vehicles[i].active = 1;
            return &vehicles[i];
        }
    return NULL;
}

/* ============================================================ */
/* =====================  Parser Thread  ====================== */
/* ============================================================ */

static void *parser_thread(void *arg)
{
    (void)arg;
    char line[512];

    while (fgets(line, sizeof(line), stdin)) {

        pthread_mutex_lock(&state_lock);

        if (line[0] == '[' && strncmp(line, "[SLOT ", 6) == 0) {
            int id, slot, blen, is_amb;
            char dir_str[8];
            if (sscanf(line, "[SLOT %d %d %d %7s %d]",
                       &id, &slot, &blen, dir_str, &is_amb) == 5) {
                bridge_length = blen;
                GuiDir gdir = (strcmp(dir_str, "EAST") == 0) ? DIR_EAST : DIR_WEST;
                if (slot == -1) {
                    VehicleState *v = find_vehicle(id);
                    if (v) {
                        v->active = 0;
                        if (gdir == DIR_EAST) total_east_passed++;
                        else                  total_west_passed++;
                    }
                } else {
                    VehicleState *v = find_vehicle(id);
                    if (!v) v = alloc_vehicle(id);
                    if (v) {
                        v->slot         = slot;
                        v->bridge_len   = blen;
                        v->direction    = gdir;
                        v->is_ambulance = is_amb;
                    }
                }
            }
        }
        else if (strncmp(line, "[QUEUE ", 7) == 0) {
            char side[8]; int total, ambs;
            if (sscanf(line, "[QUEUE %7s %d %d]", side, &total, &ambs) == 3) {
                if (strcmp(side, "EAST") == 0) { queue_east.total = total; queue_east.ambulances = ambs; }
                else                           { queue_west.total = total; queue_west.ambulances = ambs; }
            }
        }
        else if (strncmp(line, "[DIRECTION ", 11) == 0) {
            char d[8];
            if (sscanf(line, "[DIRECTION %7s]", d) == 1) {
                if      (strcmp(d, "EAST") == 0) bridge_dir = DIR_EAST;
                else if (strcmp(d, "WEST") == 0) bridge_dir = DIR_WEST;
                else                             bridge_dir = DIR_NONE;
            }
        }
        else if (strstr(line, "Simulation complete")) {
            sim_done = 1;
            log_push(line);
        }
        else {
            log_push(line);
        }

        pthread_mutex_unlock(&state_lock);
    }

    pthread_mutex_lock(&state_lock);
    sim_done = 1;
    pthread_mutex_unlock(&state_lock);
    return NULL;
}

/* ============================================================ */
/* =====================  Scene Drawing  ====================== */
/* ============================================================ */

static void draw_background(SDL_Renderer *ren)
{
    set_col(ren, C_BG);
    SDL_RenderClear(ren);
}

static void draw_header(SDL_Renderer *ren, int done)
{
    char title[160];
    snprintf(title, sizeof(title),
             "Narrow Bridge Simulation  -  length: %d m%s",
             bridge_length, done ? "  [SIMULATION COMPLETE]" : "");
    int tx = WIN_W/2 - text_width(title)/2;
    draw_text_bold(ren, tx, 16, title, C_TEXT);
}

static void draw_direction_indicator(SDL_Renderer *ren)
{
    int east_on = (bridge_dir == DIR_EAST);
    int west_on = (bridge_dir == DIR_WEST);

    /* Left lamp — WEST going */
    fill_circle(ren, WIN_W/2 - 80, 52, 10,
                west_on ? C_GREEN : C_NO_LIGHT);
    draw_text(ren, WIN_W/2 - 93, 68, "W->", C_TEXT_DIM);

    /* Right lamp — EAST going */
    fill_circle(ren, WIN_W/2 + 80, 52, 10,
                east_on ? C_GREEN : C_NO_LIGHT);
    draw_text(ren, WIN_W/2 + 70, 68, "->E", C_TEXT_DIM);

    /* Centre direction label */
    const char *dstr =
        (bridge_dir == DIR_EAST) ? "EASTBOUND" :
        (bridge_dir == DIR_WEST) ? "WESTBOUND" : "IDLE";
    Colour dcol =
        (bridge_dir == DIR_EAST) ? C_EAST_LABEL :
        (bridge_dir == DIR_WEST) ? C_WEST_LABEL  : C_TEXT_DIM;
    draw_text_bold(ren, WIN_W/2 - text_width(dstr)/2, 46, dstr, dcol);
}

static void draw_bridge(SDL_Renderer *ren)
{
    /* Road surface */
    fill_rect_r(ren, BRIDGE_X, BRIDGE_Y, BRIDGE_W, BRIDGE_H, C_ROAD, 6);

    /* Centre dashed line */
    set_col(ren, C_GRID);
    int cy = BRIDGE_Y + BRIDGE_H / 2;
    for (int x = BRIDGE_X + 10; x < BRIDGE_X + BRIDGE_W - 10; x += 20) {
        SDL_RenderDrawLine(ren, x, cy, x + 10, cy);
    }

    /* Guardrails */
    set_col(ren, C_RAIL);
    SDL_Rect top_rail = {BRIDGE_X, BRIDGE_Y,            BRIDGE_W, 4};
    SDL_Rect bot_rail = {BRIDGE_X, BRIDGE_Y+BRIDGE_H-4, BRIDGE_W, 4};
    SDL_RenderFillRect(ren, &top_rail);
    SDL_RenderFillRect(ren, &bot_rail);

    /* Meter tick marks */
    int tick_step = (bridge_length <= 20) ? 1 :
                    (bridge_length <= 50) ? 5 : 10;
    set_col(ren, C_TEXT_DIM);
    for (int m = 0; m <= bridge_length; m += tick_step) {
        int px = BRIDGE_X + (int)((double)m / bridge_length * BRIDGE_W);
        SDL_RenderDrawLine(ren, px, BRIDGE_Y - METER_TICK_H, px, BRIDGE_Y);

        if (m == 0 || m == bridge_length || m % (tick_step * 2) == 0) {
            char label[16];
            snprintf(label, sizeof(label), "%dm", m);
            draw_text(ren, px - text_width(label)/2,
                          BRIDGE_Y - METER_TICK_H - 14,
                          label, C_TEXT_DIM);
        }
    }

    /* Side labels */
    draw_text_bold(ren, BRIDGE_X - 72, BRIDGE_Y + BRIDGE_H/2 - 6, "WEST", C_WEST_LABEL);
    draw_text_bold(ren, BRIDGE_X + BRIDGE_W + 12, BRIDGE_Y + BRIDGE_H/2 - 6, "EAST", C_EAST_LABEL);
}

static void draw_vehicle_on_bridge(SDL_Renderer *ren, const VehicleState *v)
{
    if (v->slot < 0 || v->bridge_len <= 0) return;

    double frac = (double)v->slot / v->bridge_len;
    if (v->direction == DIR_WEST)
        frac = 1.0 - frac;

    int px = BRIDGE_X + (int)(frac * BRIDGE_W);
    int w  = v->is_ambulance ? AMB_W : CAR_W;
    int h  = v->is_ambulance ? AMB_H : CAR_H;
    int x  = px - w/2;
    int y  = BRIDGE_Y + (BRIDGE_H - h) / 2;

    Colour body =
        v->is_ambulance ? C_AMB :
        (v->direction == DIR_EAST) ? C_CAR_EAST : C_CAR_WEST;

    fill_rect_r(ren, x, y, w, h, body, 4);

    /* ID label */
    char label[8];
    snprintf(label, sizeof(label), "%d", v->id);
    int lw = text_width(label);
    draw_text(ren, x + w/2 - lw/2, y + h/2 - (FONT_CHAR_H*FONT_SCALE)/2,
              label, C_BLACK);

    /* Ambulance cross */
    if (v->is_ambulance) {
        set_col(ren, C_WHITE);
        SDL_RenderDrawLine(ren, x+w/2, y+3,   x+w/2,   y+h-3);
        SDL_RenderDrawLine(ren, x+3,   y+h/2, x+w-3,   y+h/2);
    }
}

static void draw_queue(SDL_Renderer *ren, const QueueState *q, int is_east)
{
    int base_y = BRIDGE_Y + BRIDGE_H + 20;
    int total  = q->total;
    int ambs   = q->ambulances;

    /*
     * Side assignment (matches physical reality):
     *   EAST-bound (is_east=1): vehicles travel →, queue on the LEFT side
     *     (west entrance of bridge).  Zone: x = 0 .. BRIDGE_X-1
     *   WEST-bound (is_east=0): vehicles travel ←, queue on the RIGHT side
     *     (east entrance of bridge).  Zone: x = BRIDGE_X+BRIDGE_W .. WIN_W-1
     *
     * zone_cx is the horizontal centre of the reserved zone — all geometry
     * and the label are anchored here so nothing clips regardless of count.
     */
    int zone_cx = is_east
        ? (BRIDGE_X / 2)
        : (BRIDGE_X + BRIDGE_W + (WIN_W - BRIDGE_X - BRIDGE_W) / 2);

    /* Background rect — capped to zone width */
    int show = (total > 0) ? total : 1;
    int bg_w = show * QUEUE_SLOT_W;
    if (bg_w > QUEUE_ZONE_W - 8) bg_w = QUEUE_ZONE_W - 8;
    int bg_x = zone_cx - bg_w / 2;

    fill_rect_r(ren, bg_x, base_y, bg_w, QUEUE_LANE_H, C_QUEUE_BG, 4);

    /*
     * Vehicle icons — slot 0 is always the vehicle closest to the bridge
     * entrance, so it appears nearest the bridge edge in both queues:
     *   EAST queue (left zone):  slot 0 at rightmost position, grows left
     *   WEST queue (right zone): slot 0 at leftmost position,  grows right
     */
    int max_icons = bg_w / QUEUE_SLOT_W;
    if (max_icons > 20) max_icons = 20;

    for (int i = 0; i < total && i < max_icons; i++) {
        int amb = (i < ambs);
        Colour c = amb ? C_AMB : (is_east ? C_CAR_EAST : C_CAR_WEST);

        int vx;
        if (is_east)
            vx = bg_x + bg_w - (i + 1) * QUEUE_SLOT_W + 4; /* slot 0 rightmost */
        else
            vx = bg_x + i * QUEUE_SLOT_W + 4;               /* slot 0 leftmost  */
        int vy = base_y + (QUEUE_LANE_H - CAR_H) / 2;

        fill_rect_r(ren, vx, vy, CAR_W, CAR_H, c, 3);
        if (amb) {
            set_col(ren, C_WHITE);
            SDL_RenderDrawLine(ren, vx+CAR_W/2, vy+2,       vx+CAR_W/2, vy+CAR_H-2);
            SDL_RenderDrawLine(ren, vx+2,       vy+CAR_H/2, vx+CAR_W-2, vy+CAR_H/2);
        }
    }
    if (total > max_icons) {
        char more[16];
        snprintf(more, sizeof(more), "+%d", total - max_icons);
        draw_text(ren, bg_x + 4, base_y + QUEUE_LANE_H/2 - 6, more, C_TEXT_DIM);
    }

    /* Label centred on the zone, clamped so it never clips the screen edges */
    char label[64];
    snprintf(label, sizeof(label),
             "%s queue: %d waiting (%d amb)",
             is_east ? "East" : "West", total, ambs);
    int lw      = text_width(label);
    int label_x = zone_cx - lw / 2;
    if (label_x < 4)              label_x = 4;
    if (label_x + lw > WIN_W - 4) label_x = WIN_W - 4 - lw;
    draw_text(ren, label_x, base_y + QUEUE_LANE_H + 8, label,
              is_east ? C_EAST_LABEL : C_WEST_LABEL);
}

static void draw_stats(SDL_Renderer *ren)
{
    int sx = 30, sy = 100;
    char buf[128];

    snprintf(buf, sizeof(buf), "Passed EAST: %d", total_east_passed);
    draw_text(ren, sx, sy, buf, C_EAST_LABEL);

    snprintf(buf, sizeof(buf), "Passed WEST: %d", total_west_passed);
    draw_text(ren, sx, sy + 20, buf, C_WEST_LABEL);

    int on = 0;
    for (int i = 0; i < MAX_VEHICLES; i++)
        if (vehicles[i].active && vehicles[i].slot >= 0)
            on++;

    snprintf(buf, sizeof(buf), "On bridge:   %d", on);
    draw_text(ren, sx, sy + 40, buf, C_TEXT);
}

static void draw_log_panel(SDL_Renderer *ren)
{
    fill_rect_r(ren, LOG_X, LOG_Y, LOG_W, LOG_H, C_PANEL_BG, 6);

    set_col(ren, C_GRID);
    SDL_Rect border = {LOG_X, LOG_Y, LOG_W, LOG_H};
    SDL_RenderDrawRect(ren, &border);

    draw_text_bold(ren, LOG_X + 8, LOG_Y - 18, "Console log", C_TEXT_DIM);

    char lines[LOG_MAX_LINES][256];
    int  n = log_recent(lines, LOG_MAX_LINES);

    for (int i = 0; i < n; i++) {
        int y = LOG_Y + 10 + i * LOG_LINE_H;
        Colour col = C_TEXT_DIM;

        if      (strstr(lines[i], "[AMBULANCE]") || strstr(lines[i], "PRIORITY"))
            col = C_AMB;
        else if (strstr(lines[i], "entered"))
            col = C_ENTRY_GREEN;
        else if (strstr(lines[i], "exited"))
            col = C_TEXT;
        else if (strstr(lines[i], "Switching") || strstr(lines[i], "direction"))
            col = C_AMBER;

        /* Truncate to fit panel width */
        char trunc[108];
        strncpy(trunc, lines[i], 107);
        trunc[107] = '\0';

        draw_text(ren, LOG_X + 8, y, trunc, col);
    }
}

static void redraw(SDL_Renderer *ren, int done)
{
    draw_background(ren);
    draw_header(ren, done);
    draw_direction_indicator(ren);
    draw_bridge(ren);
    draw_stats(ren);

    for (int i = 0; i < MAX_VEHICLES; i++)
        if (vehicles[i].active && vehicles[i].slot >= 0)
            draw_vehicle_on_bridge(ren, &vehicles[i]);

    draw_queue(ren, &queue_east, 1);
    draw_queue(ren, &queue_west, 0);
    draw_log_panel(ren);

    SDL_RenderPresent(ren);
}

/* ============================================================ */
/* ========================  Main  ============================ */
/* ============================================================ */

int main(void)
{
    setvbuf(stdin,  NULL, _IOLBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    memset(vehicles, 0, sizeof(vehicles));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[GUI] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "Bridge Simulation - Live View",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WIN_W, WIN_H,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!win) {
        fprintf(stderr, "[GUI] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!ren) {
        /* Fall back to software renderer (e.g. SSH session without GPU) */
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        fprintf(stderr, "[GUI] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    /* High-DPI: scale logical size to match the physical window */
    {
        int pw, ph;
        SDL_GetRendererOutputSize(ren, &pw, &ph);
        if (pw != WIN_W || ph != WIN_H)
            SDL_RenderSetScale(ren, (float)pw/WIN_W, (float)ph/WIN_H);
    }

    pthread_t ptid;
    pthread_create(&ptid, NULL, parser_thread, NULL);

    Uint32 last_draw = 0;
    int    running   = 1;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_q || k == SDLK_ESCAPE)
                    running = 0;
            }
        }

        Uint32 now = SDL_GetTicks();
        if (now - last_draw >= REFRESH_MS) {
            last_draw = now;

            pthread_mutex_lock(&state_lock);
            int done = sim_done;
            redraw(ren, done);
            pthread_mutex_unlock(&state_lock);

            if (done) {
                /* Keep the window alive until the user closes it */
                SDL_Event e2;
                while (SDL_WaitEvent(&e2)) {
                    if (e2.type == SDL_QUIT) break;
                    if (e2.type == SDL_KEYDOWN) {
                        SDL_Keycode k = e2.key.keysym.sym;
                        if (k == SDLK_q || k == SDLK_ESCAPE) break;
                    }
                    if (e2.type == SDL_WINDOWEVENT &&
                        e2.window.event == SDL_WINDOWEVENT_EXPOSED) {
                        pthread_mutex_lock(&state_lock);
                        redraw(ren, 1);
                        pthread_mutex_unlock(&state_lock);
                    }
                }
                running = 0;
            }
        }

        SDL_Delay(5);
    }

    pthread_cancel(ptid);
    pthread_join(ptid, NULL);

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}