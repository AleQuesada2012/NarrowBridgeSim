/*
 * gui.c — Bridge Simulation Visualizer (SDL2)
 *
 * Reads structured log lines from the simulation via stdin (pipe),
 * maintains a live state, and redraws an SDL2 window at ~15 fps.
 *
 * No external font library required: 5×7 pixel bitmap font baked in.
 *
 * Recognised structured log formats (all others go to the log panel):
 *   [SLOT      <id> <slot> <bridge_len> <EAST|WEST> <is_ambulance>]
 *   [QUEUE     <EAST|WEST> <total> <ambulances>]
 *   [DIRECTION <EAST|WEST|NONE>]
 *   [LIGHT     <EAST|WEST> <GREEN|RED|OFF>]
 *   [MODE      <CARNAGE|SEMAPHORE|OFFICER>]
 *
 * Compile (Linux):
 *   gcc gui.c -lSDL2 -lpthread -lm -o bridge_gui
 * Compile (macOS, Homebrew SDL2):
 *   gcc gui.c $(sdl2-config --cflags --libs) -lpthread -lm -o bridge_gui
 * Run:
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

#define BRIDGE_X         203
#define BRIDGE_Y         270
#define BRIDGE_W         960
#define BRIDGE_H          80
#define METER_TICK_H      10

#define QUEUE_ZONE_W     200
#define QUEUE_LANE_H      50
#define QUEUE_SLOT_W      36

#define CAR_W             28
#define CAR_H             22
#define AMB_W             32
#define AMB_H             24

#define LOG_X             10
#define LOG_Y            530
#define LOG_W           1346
#define LOG_H            228
#define LOG_MAX_LINES     13
#define LOG_LINE_H        17

#define MAX_VEHICLES     512
#define REFRESH_MS        66

/* ============================================================ */
/* =====================  Colour Helpers  ===================== */
/* ============================================================ */

typedef struct { Uint8 r, g, b, a; } Colour;

#define COL(r,g,b)     ((Colour){(r),(g),(b),255})

static void set_col(SDL_Renderer *ren, Colour c)
{
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
}

static const Colour
    C_BG          = COL( 18,  20,  28),
    C_ROAD        = COL( 60,  65,  80),
    C_RAIL        = COL(120, 130, 150),
    C_LIGHT_GREEN = COL( 60, 220,  60),   /* traffic light green  */
    C_LIGHT_RED   = COL(220,  50,  50),   /* traffic light red    */
    C_LIGHT_OFF   = COL( 40,  40,  40),   /* unlit bulb           */
    C_LIGHT_BODY  = COL( 30,  30,  30),   /* light housing        */
    C_CAR_EAST    = COL( 80, 160, 255),
    C_CAR_WEST    = COL(255, 160,  60),
    C_AMB         = COL(255,  60,  60),
    C_QUEUE_BG    = COL( 30,  34,  48),
    C_TEXT        = COL(220, 230, 255),
    C_TEXT_DIM    = COL(100, 115, 150),
    C_PANEL_BG    = COL( 22,  26,  38),
    C_GRID        = COL( 40,  46,  64),
    C_EAST_LABEL  = COL( 80, 160, 255),
    C_WEST_LABEL  = COL(255, 160,  60),
    C_WHITE       = COL(255, 255, 255),
    C_BLACK       = COL(  0,   0,   0),
    C_AMBER       = COL(255, 200,   0),
    C_ENTRY_GREEN = COL( 80, 200,  80);

/* ============================================================ */
/* ================  5×7 Bitmap Font  ======================== */
/* ============================================================ */

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

#define FONT_CHAR_W   5
#define FONT_CHAR_H   7
#define FONT_SCALE    2
#define FONT_GAP      2
#define FONT_ADVANCE  (FONT_CHAR_W * FONT_SCALE + FONT_GAP)

static int draw_char(SDL_Renderer *ren, int x, int y,
                     char ch, Colour col, int scale)
{
    int idx = (unsigned char)ch - 0x20;
    if (idx < 0 || idx >= (int)(sizeof(FONT5X7)/sizeof(FONT5X7[0]))) idx = 0;
    set_col(ren, col);
    for (int ci = 0; ci < FONT_CHAR_W; ci++) {
        Uint8 bits = FONT5X7[idx][ci];
        for (int row = 0; row < FONT_CHAR_H; row++) {
            if (bits & (1 << row)) {
                SDL_Rect px = { x + ci*scale, y + row*scale, scale, scale };
                SDL_RenderFillRect(ren, &px);
            }
        }
    }
    return x + FONT_CHAR_W * scale + FONT_GAP;
}

static int draw_str(SDL_Renderer *ren, int x, int y,
                    const char *s, Colour col, int scale)
{
    while (*s) x = draw_char(ren, x, y, *s++, col, scale);
    return x;
}

static void draw_text(SDL_Renderer *ren, int x, int y,
                      const char *s, Colour col)
{
    draw_str(ren, x, y, s, col, FONT_SCALE);
}

static void draw_text_bold(SDL_Renderer *ren, int x, int y,
                           const char *s, Colour col)
{
    draw_str(ren, x+1, y+1, s, C_BLACK, FONT_SCALE);
    draw_str(ren, x,   y,   s, col,     FONT_SCALE);
}

static int text_width(const char *s)
{
    return (int)strlen(s) * FONT_ADVANCE;
}

/* ============================================================ */
/* ===================  Drawing Helpers  ====================== */
/* ============================================================ */

static void fill_rect_r(SDL_Renderer *ren, int x, int y, int w, int h,
                         Colour col, int r)
{
    if (r < 1) r = 1;
    set_col(ren, col);
    SDL_Rect rh = {x,     y+r,   w,     h-2*r};
    SDL_Rect rv = {x+r,   y,     w-2*r, h    };
    SDL_RenderFillRect(ren, &rh);
    SDL_RenderFillRect(ren, &rv);
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                SDL_Rect px; px.w = px.h = 1;
                px.x = x+dx;     px.y = y+dy;     SDL_RenderFillRect(ren, &px);
                px.x = x+w-1-dx; px.y = y+dy;     SDL_RenderFillRect(ren, &px);
                px.x = x+dx;     px.y = y+h-1-dy; SDL_RenderFillRect(ren, &px);
                px.x = x+w-1-dx; px.y = y+h-1-dy; SDL_RenderFillRect(ren, &px);
            }
        }
    }
}

static void fill_circle(SDL_Renderer *ren, int cx, int cy, int r, Colour col)
{
    set_col(ren, col);
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx*dx + dy*dy <= r*r)
                SDL_RenderDrawPoint(ren, cx+dx, cy+dy);
}

/* ============================================================ */
/* =====================  State Types  ======================== */
/* ============================================================ */

typedef enum { DIR_EAST = 0, DIR_WEST = 1, DIR_NONE = 2 } GuiDir;
typedef enum { MODE_CARNAGE, MODE_SEMAPHORE, MODE_OFFICER }  GuiMode;
typedef enum { GLIGHT_GREEN, GLIGHT_RED, GLIGHT_OFF }        GuiLight;

typedef struct {
    int    id;
    int    slot;
    int    bridge_len;
    GuiDir direction;
    int    is_ambulance;
    int    active;
} VehicleState;

#define QUEUE_MAX_DISPLAY 128  /* max vehicles whose id/type we store for icon rendering */

typedef struct {
    int total;                           /* exact count from simulation (can exceed QUEUE_MAX_DISPLAY) */
    int stored;                          /* how many entries are actually in ids[] / is_ambulance[]   */
    int ids[QUEUE_MAX_DISPLAY];          /* vehicle id,  index 0 = FIFO head                          */
    int is_ambulance[QUEUE_MAX_DISPLAY]; /* ambulance flag, same indexing                             */
} QueueState;

/* ============================================================ */
/* =====================  Shared State  ======================= */
/* ============================================================ */

static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static VehicleState vehicles[MAX_VEHICLES];
static int          bridge_length     = 10;
static GuiDir       bridge_dir        = DIR_NONE;
static QueueState   queue_east;   /* zero-initialised by C static storage */
static QueueState   queue_west;
static int          total_east_passed = 0;
static int          total_west_passed = 0;
static int          cars_on_bridge    = 0;  /* authoritative: from entered/exited log lines */
static int          sim_done          = 0;

static GuiMode  sim_mode              = MODE_CARNAGE;
static GuiLight light[2]              = {GLIGHT_OFF, GLIGHT_OFF};
                                        /* light[0]=EAST, light[1]=WEST          */

/* Officer mode state — updated by [OFFICER] and [MODE OFFICER k k] lines */
static int officer_active_side        = 0;   /* 0=EAST, 1=WEST */
static int officer_k[2]               = {0, 0};  /* K quota per side from config */
static int officer_passed[2]          = {0, 0};  /* passed this turn per side    */

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
            vehicles[i].id = id; vehicles[i].active = 1;
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

        /* [SLOT id slot bridge_len EAST|WEST is_ambulance] */
        if (strncmp(line, "[SLOT ", 6) == 0) {
            int id, slot, blen, is_amb; char dir_str[8];
            if (sscanf(line, "[SLOT %d %d %d %7[A-Z] %d]",
                       &id, &slot, &blen, dir_str, &is_amb) == 5) {
                bridge_length = blen;
                GuiDir gdir = strcmp(dir_str,"EAST")==0 ? DIR_EAST : DIR_WEST;
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
                        v->slot = slot; v->bridge_len = blen;
                        v->direction = gdir; v->is_ambulance = is_amb;
                    }
                }
            }
        }

        /*
         * [QUEUE EAST|WEST id0:amb0 id1:amb1 ...]
         *
         * Tokens after the side name are  <id>:<is_ambulance>  in FIFO
         * head-to-tail order.  We count every token for an exact total,
         * but only store up to QUEUE_MAX_DISPLAY entries for icon rendering.
         */
        else if (strncmp(line, "[QUEUE ", 7) == 0) {
            char side[8];
            if (sscanf(line, "[QUEUE %7[A-Z]", side) == 1) {
                QueueState *qs = (strcmp(side,"EAST")==0) ? &queue_east : &queue_west;
                qs->total  = 0;
                qs->stored = 0;

                const char *p = line;
                p = strchr(p, ' '); if (p) p++;  /* skip past "[QUEUE" */
                p = strchr(p, ' ');               /* skip past side name */

                while (p) {
                    int id, amb;
                    if (sscanf(p, " %d:%d", &id, &amb) == 2) {
                        /* Always count for the exact total */
                        qs->total++;
                        /* Only store if within display capacity */
                        if (qs->stored < QUEUE_MAX_DISPLAY) {
                            qs->ids[qs->stored]          = id;
                            qs->is_ambulance[qs->stored] = amb;
                            qs->stored++;
                        }
                        p = strchr(p + 1, ' ');
                    } else {
                        break;
                    }
                }
            }
        }

        /* [DIRECTION EAST|WEST|NONE] */
        else if (strncmp(line, "[DIRECTION ", 11) == 0) {
            char d[8];
            if (sscanf(line, "[DIRECTION %7[A-Z]]", d) == 1) {
                if      (strcmp(d,"EAST")==0) bridge_dir = DIR_EAST;
                else if (strcmp(d,"WEST")==0) bridge_dir = DIR_WEST;
                else                          bridge_dir = DIR_NONE;
            }
        }

        /* [LIGHT EAST|WEST GREEN|RED|OFF] */
        else if (strncmp(line, "[LIGHT ", 7) == 0) {
            char side[8], state[8];
            if (sscanf(line, "[LIGHT %7[A-Z] %7[A-Z]]", side, state) == 2) {
                int idx = (strcmp(side,"EAST")==0) ? 0 : 1;
                if      (strcmp(state,"GREEN")==0) light[idx] = GLIGHT_GREEN;
                else if (strcmp(state,"RED"  )==0) light[idx] = GLIGHT_RED;
                else                               light[idx] = GLIGHT_OFF;
            }
        }

        /* [MODE CARNAGE|SEMAPHORE|OFFICER [east_k west_k]] */
        else if (strncmp(line, "[MODE ", 6) == 0) {
            char m[16]; int ek = 0, wk = 0;
            if (sscanf(line, "[MODE %15[A-Z] %d %d]", m, &ek, &wk) >= 1) {
                if      (strcmp(m,"SEMAPHORE")==0) sim_mode = MODE_SEMAPHORE;
                else if (strcmp(m,"OFFICER"  )==0) {
                    sim_mode = MODE_OFFICER;
                    officer_k[0] = ek;  /* EAST k */
                    officer_k[1] = wk;  /* WEST k */
                }
                else                               sim_mode = MODE_CARNAGE;
            }
        }

        /* Authoritative bridge occupancy tracking */
        else if (strstr(line, "entered from") && strstr(line, "[BRIDGE]")) {
            cars_on_bridge++;
        }
        else if (strstr(line, "exited going") && strstr(line, "[BRIDGE]")) {
            if (cars_on_bridge > 0) cars_on_bridge--;
        }

        /*
         * [OFFICER EAST|WEST k_value passed_this_turn]
         * Emitted by bridge_set_officer and on each K-slot entry.
         */
        else if (strncmp(line, "[OFFICER ", 9) == 0
                 && (strncmp(line, "[OFFICER EAST ", 14) == 0
                     || strncmp(line, "[OFFICER WEST ", 14) == 0)) {
            char side[8]; int k, passed;
            if (sscanf(line, "[OFFICER %7[A-Z] %d %d]", side, &k, &passed) == 3) {
                int idx = (strcmp(side,"EAST")==0) ? 0 : 1;
                officer_active_side  = idx;
                officer_k[idx]       = k;
                officer_passed[idx]  = passed;
                /* Reset the other side's passed counter on a new turn */
                officer_passed[1-idx] = 0;
            }
        }

        /* Simulation finished */
        else if (strstr(line, "Simulation complete")) {
            sim_done = 1;
            log_push(line);
        }

        /* Everything else → human-readable log panel */
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
    const char *mode_str =
        sim_mode == MODE_SEMAPHORE ? "SEMAPHORE" :
        sim_mode == MODE_OFFICER   ? "OFFICER"   : "CARNAGE";

    char title[180];
    snprintf(title, sizeof(title),
             "Narrow Bridge Simulation  -  %s  -  length: %d m%s",
             mode_str, bridge_length,
             done ? "  [SIMULATION COMPLETE]" : "");
    draw_text_bold(ren, WIN_W/2 - text_width(title)/2, 16, title, C_TEXT);
}

/* ---- Carnage mode: simple flow-direction indicator ---- */
static void draw_carnage_indicator(SDL_Renderer *ren)
{
    // int east_on = (bridge_dir == DIR_EAST);
    // int west_on = (bridge_dir == DIR_WEST);

    // fill_circle(ren, WIN_W/2 - 80, 52, 10,
    //             west_on ? C_FLOW_GREEN : C_NO_LIGHT);
    // draw_text(ren, WIN_W/2 - 93, 68, "<-W", C_TEXT_DIM);

    // fill_circle(ren, WIN_W/2 + 80, 52, 10,
    //             east_on ? C_FLOW_GREEN : C_NO_LIGHT);
    // draw_text(ren, WIN_W/2 + 70, 68, "E->", C_TEXT_DIM);

    const char *dstr =
        bridge_dir == DIR_EAST ? "EASTBOUND" :
        bridge_dir == DIR_WEST ? "WESTBOUND" : "IDLE";
    Colour dcol =
        bridge_dir == DIR_EAST ? C_EAST_LABEL :
        bridge_dir == DIR_WEST ? C_WEST_LABEL  : C_TEXT_DIM;
    draw_text_bold(ren, WIN_W/2 - text_width(dstr)/2, 46, dstr, dcol);
}

/*
 * Draw one traffic light housing at (cx, cy).
 * The housing has two bulbs (top = red, bottom = green).
 * `state` is GLIGHT_GREEN or GLIGHT_RED (or OFF = both dim).
 * `label` is drawn below the housing (e.g. "EAST" or "WEST").
 */
static void draw_traffic_light(SDL_Renderer *ren,
                                int cx, int cy,
                                GuiLight state,
                                const char *label,
                                Colour label_col)
{
    const int BOX_W = 26;
    const int BOX_H = 52;
    const int BULB_R = 9;
    const int BULB_GAP = 6; /* gap from edge to bulb centre */

    int bx = cx - BOX_W/2;
    int by = cy - BOX_H/2;

    /* Housing */
    fill_rect_r(ren, bx, by, BOX_W, BOX_H, C_LIGHT_BODY, 4);

    /* Red bulb — top */
    int red_cy = by + BULB_GAP + BULB_R;
    fill_circle(ren, cx, red_cy, BULB_R,
                state == GLIGHT_RED ? C_LIGHT_RED : C_LIGHT_OFF);

    /* Green bulb — bottom */
    int grn_cy = by + BOX_H - BULB_GAP - BULB_R;
    fill_circle(ren, cx, grn_cy, BULB_R,
                state == GLIGHT_GREEN ? C_LIGHT_GREEN : C_LIGHT_OFF);

    /* Label below housing */
    int lw = text_width(label);
    draw_text(ren, cx - lw/2, by + BOX_H + 6, label, label_col);
}

/* ---- Semaphore mode: two traffic lights with green/red state ---- */
static void draw_semaphore_indicator(SDL_Renderer *ren)
{
    /*
     * light[0] = EAST side light  (shown on the RIGHT — east entrance)
     * light[1] = WEST side light  (shown on the LEFT  — west entrance)
     *
     * The lights are positioned symmetrically around the window centre,
     * just above the bridge tick-mark area.
     */
    int cy = 60;

    /* EAST light — right side */
    draw_traffic_light(ren, WIN_W/2 + 100, cy,
                       light[0], "E->", C_EAST_LABEL);

    /* WEST light — left side */
    draw_traffic_light(ren, WIN_W/2 - 100, cy,
                       light[1], "<-W", C_WEST_LABEL);

    /* Centre label: which side is currently flowing */
    const char *dstr =
        bridge_dir == DIR_EAST ? "EASTBOUND" :
        bridge_dir == DIR_WEST ? "WESTBOUND" : "IDLE";
    Colour dcol =
        bridge_dir == DIR_EAST ? C_EAST_LABEL :
        bridge_dir == DIR_WEST ? C_WEST_LABEL  : C_TEXT_DIM;
    draw_text_bold(ren, WIN_W/2 - text_width(dstr)/2, 46, dstr, dcol);
}


/*
 * Officer mode indicator: two panels (one per side) showing the K quota
 * and how many vehicles have passed so far this turn.
 *
 *   Layout per panel (centred on zone_cx for each side):
 *
 *     [OFFICER]           ← label
 *     K: N                ← quota for this side
 *     Passed: M / N       ← progress bar + fraction
 *
 * The active side panel is highlighted; the blocked side is dimmed.
 */
static void draw_officer_panel(SDL_Renderer *ren, int cx, int cy,
                                int is_active, int k, int passed,
                                Colour label_col, const char *side_name)
{
    const int PW = 160, PH = 90, PR = 6;
    int px = cx - PW/2, py = cy - PH/2;

    Colour bg = is_active ? COL(30, 40, 60) : COL(22, 26, 38);
    fill_rect_r(ren, px, py, PW, PH, bg, PR);

    /* Border — bright for active, dim for blocked */
    set_col(ren, is_active ? label_col : C_GRID);
    SDL_Rect border = {px, py, PW, PH};
    SDL_RenderDrawRect(ren, &border);

    /* "OFFICER" label */
    char head[32]; snprintf(head, sizeof(head), "OFFICER %s", side_name);
    draw_text_bold(ren, cx - text_width(head)/2, py + 8, head,
                   is_active ? label_col : C_TEXT_DIM);

    if (k <= 0) {
        /* No quota info yet */
        const char *wait = is_active ? "ACTIVE" : "WAITING";
        draw_text(ren, cx - text_width(wait)/2, py + 32, wait,
                  is_active ? C_ENTRY_GREEN : C_TEXT_DIM);
        return;
    }

    /* "K: N" */
    char kbuf[32]; snprintf(kbuf, sizeof(kbuf), "K: %d", k);
    draw_text(ren, cx - text_width(kbuf)/2, py + 30, kbuf,
              is_active ? C_TEXT : C_TEXT_DIM);

    /* "Passed: M / N" */
    char pbuf[32]; snprintf(pbuf, sizeof(pbuf), "Passed: %d / %d", passed, k);
    draw_text(ren, cx - text_width(pbuf)/2, py + 48, pbuf,
              is_active ? C_AMBER : C_TEXT_DIM);

    /* Progress bar */
    int bar_x = px + 12, bar_y = py + 68;
    int bar_w = PW - 24, bar_h = 10;
    fill_rect_r(ren, bar_x, bar_y, bar_w, bar_h, C_GRID, 2);
    if (k > 0) {
        int fill = (passed * bar_w) / k;
        if (fill > bar_w) fill = bar_w;
        if (fill > 0)
            fill_rect_r(ren, bar_x, bar_y, fill, bar_h,
                        is_active ? C_ENTRY_GREEN : C_TEXT_DIM, 2);
    }
}

static void draw_officer_indicator(SDL_Renderer *ren)
{
    /*
     * Panels are placed at WIN_W/4 and 3*WIN_W/4 so they sit clearly
     * between the stats block (top-left, x≈30..200) and the bridge side
     * labels (x≈131..1175), and below the direction label (y≈46..60).
     * cy=155 keeps the panel (height 90) in the band y=110..200, well
     * above the bridge tick marks that start around y=240.
     */
    int east_cx = WIN_W / 4;
    int west_cx = (WIN_W * 3) / 4;
    int cy = 155;

    int east_active = (officer_active_side == 0);
    int west_active = (officer_active_side == 1);

    draw_officer_panel(ren, east_cx, cy,
                       east_active, officer_k[0], officer_passed[0],
                       C_EAST_LABEL, "EAST");
    draw_officer_panel(ren, west_cx, cy,
                       west_active, officer_k[1], officer_passed[1],
                       C_WEST_LABEL, "WEST");

    /* Centre: show which direction currently has the bridge */
    const char *dstr =
        bridge_dir == DIR_EAST ? "EASTBOUND" :
        bridge_dir == DIR_WEST ? "WESTBOUND" : "IDLE";
    Colour dcol =
        bridge_dir == DIR_EAST ? C_EAST_LABEL :
        bridge_dir == DIR_WEST ? C_WEST_LABEL  : C_TEXT_DIM;
    draw_text_bold(ren, WIN_W/2 - text_width(dstr)/2, 46, dstr, dcol);
}

static void draw_mode_indicator(SDL_Renderer *ren)
{
    if (sim_mode == MODE_SEMAPHORE)
        draw_semaphore_indicator(ren);
    else if (sim_mode == MODE_OFFICER)
        draw_officer_indicator(ren);
    else
        draw_carnage_indicator(ren);
}

static void draw_bridge(SDL_Renderer *ren)
{
    fill_rect_r(ren, BRIDGE_X, BRIDGE_Y, BRIDGE_W, BRIDGE_H, C_ROAD, 6);

    set_col(ren, C_GRID);
    int cy = BRIDGE_Y + BRIDGE_H/2;
    for (int x = BRIDGE_X+10; x < BRIDGE_X+BRIDGE_W-10; x += 20)
        SDL_RenderDrawLine(ren, x, cy, x+10, cy);

    set_col(ren, C_RAIL);
    SDL_Rect tr = {BRIDGE_X, BRIDGE_Y,            BRIDGE_W, 4};
    SDL_Rect br = {BRIDGE_X, BRIDGE_Y+BRIDGE_H-4, BRIDGE_W, 4};
    SDL_RenderFillRect(ren, &tr);
    SDL_RenderFillRect(ren, &br);

    int tick_step = bridge_length<=20 ? 1 : bridge_length<=50 ? 5 : 10;
    set_col(ren, C_TEXT_DIM);
    for (int m = 0; m <= bridge_length; m += tick_step) {
        int px = BRIDGE_X + (int)((double)m/bridge_length*BRIDGE_W);
        SDL_RenderDrawLine(ren, px, BRIDGE_Y-METER_TICK_H, px, BRIDGE_Y);
        if (m==0 || m==bridge_length || m%(tick_step*2)==0) {
            char lbl[16]; snprintf(lbl, sizeof(lbl), "%dm", m);
            draw_text(ren, px-text_width(lbl)/2,
                          BRIDGE_Y-METER_TICK_H-14, lbl, C_TEXT_DIM);
        }
    }

    draw_text_bold(ren, BRIDGE_X-72,             BRIDGE_Y+BRIDGE_H/2-6, "WEST", C_WEST_LABEL);
    draw_text_bold(ren, BRIDGE_X+BRIDGE_W+12,    BRIDGE_Y+BRIDGE_H/2-6, "EAST", C_EAST_LABEL);
}

static void draw_vehicle_on_bridge(SDL_Renderer *ren, const VehicleState *v)
{
    if (v->slot < 0 || v->bridge_len <= 0) return;

    double frac = (double)v->slot / v->bridge_len;
    if (v->direction == DIR_WEST) frac = 1.0 - frac;

    int px = BRIDGE_X + (int)(frac * BRIDGE_W);
    int w  = v->is_ambulance ? AMB_W : CAR_W;
    int h  = v->is_ambulance ? AMB_H : CAR_H;
    int x  = px - w/2;
    int y  = BRIDGE_Y + (BRIDGE_H - h)/2;

    Colour body = v->is_ambulance ? C_AMB :
                  v->direction == DIR_EAST ? C_CAR_EAST : C_CAR_WEST;

    fill_rect_r(ren, x, y, w, h, body, 4);

    char lbl[8]; snprintf(lbl, sizeof(lbl), "%d", v->id);
    draw_text(ren, x+w/2-text_width(lbl)/2,
                  y+h/2-(FONT_CHAR_H*FONT_SCALE)/2, lbl, C_BLACK);

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

    int zone_cx = is_east
        ? (BRIDGE_X / 2)
        : (BRIDGE_X + BRIDGE_W + (WIN_W - BRIDGE_X - BRIDGE_W) / 2);

    int show = (total > 0) ? total : 1;
    int bg_w = show * QUEUE_SLOT_W;
    if (bg_w > QUEUE_ZONE_W - 8) bg_w = QUEUE_ZONE_W - 8;
    int bg_x = zone_cx - bg_w / 2;

    fill_rect_r(ren, bg_x, base_y, bg_w, QUEUE_LANE_H, C_QUEUE_BG, 4);

    int max_icons = bg_w / QUEUE_SLOT_W;
    if (max_icons > 20) max_icons = 20;

    /*
     * Render icons in FIFO order using q->stored (capped) entries.
     * q->total is the exact count used for the label — it may exceed
     * QUEUE_MAX_DISPLAY when there are very many waiting vehicles.
     */
    int ambs = 0;
    for (int i = 0; i < q->stored && i < max_icons; i++) {
        int amb = q->is_ambulance[i];
        if (amb) ambs++;
        Colour c = amb ? C_AMB : (is_east ? C_CAR_EAST : C_CAR_WEST);

        int vx = is_east
            ? (bg_x + bg_w - (i+1)*QUEUE_SLOT_W + 4)  /* head = rightmost */
            : (bg_x + i*QUEUE_SLOT_W + 4);              /* head = leftmost  */
        int vy = base_y + (QUEUE_LANE_H - CAR_H)/2;

        fill_rect_r(ren, vx, vy, CAR_W, CAR_H, c, 3);

        /* Vehicle id label */
        char idlbl[8]; snprintf(idlbl, sizeof(idlbl), "%d", q->ids[i]);
        draw_text(ren, vx + CAR_W/2 - text_width(idlbl)/2,
                       vy + CAR_H/2 - (FONT_CHAR_H*FONT_SCALE)/2,
                       idlbl, C_BLACK);

        if (amb) {
            set_col(ren, C_WHITE);
            SDL_RenderDrawLine(ren, vx+CAR_W/2, vy+2,       vx+CAR_W/2, vy+CAR_H-2);
            SDL_RenderDrawLine(ren, vx+2,       vy+CAR_H/2, vx+CAR_W-2, vy+CAR_H/2);
        }
    }
    if (total > max_icons) {
        char more[16]; snprintf(more, sizeof(more), "+%d", total - max_icons);
        draw_text(ren, bg_x+4, base_y+QUEUE_LANE_H/2-6, more, C_TEXT_DIM);
    }

    char label[64];
    snprintf(label, sizeof(label),
             "%s queue: %d waiting (%d amb)",
             is_east ? "East" : "West", total, ambs);
    int lw      = text_width(label);
    int label_x = zone_cx - lw/2;
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
    draw_text(ren, sx, sy+20, buf, C_WEST_LABEL);

    snprintf(buf, sizeof(buf), "On bridge:   %d", cars_on_bridge);
    draw_text(ren, sx, sy+40, buf, C_TEXT);
}

static void draw_log_panel(SDL_Renderer *ren)
{
    fill_rect_r(ren, LOG_X, LOG_Y, LOG_W, LOG_H, C_PANEL_BG, 6);

    set_col(ren, C_GRID);
    SDL_Rect border = {LOG_X, LOG_Y, LOG_W, LOG_H};
    SDL_RenderDrawRect(ren, &border);

    draw_text_bold(ren, LOG_X+8, LOG_Y-18, "Console log", C_TEXT_DIM);

    char lines[LOG_MAX_LINES][256];
    int  n = log_recent(lines, LOG_MAX_LINES);

    for (int i = 0; i < n; i++) {
        int    y   = LOG_Y + 10 + i * LOG_LINE_H;
        Colour col = C_TEXT_DIM;

        if      (strstr(lines[i], "[AMBULANCE]") || strstr(lines[i], "PRIORITY"))
            col = C_AMB;
        else if (strstr(lines[i], "crossing on RED"))
            col = C_AMB;
        else if (strstr(lines[i], "entered"))
            col = C_ENTRY_GREEN;
        else if (strstr(lines[i], "exited"))
            col = C_TEXT;
        else if (strstr(lines[i], "Switching") || strstr(lines[i], "direction"))
            col = C_AMBER;
        else if (strstr(lines[i], "[SEMAPHORE]") || strstr(lines[i], "Light GREEN"))
            col = C_AMBER;
        else if (strstr(lines[i], "[OFFICER]") || strstr(lines[i], "BRIDGE OPEN"))
            col = C_AMBER;

        char trunc[108];
        strncpy(trunc, lines[i], 107);
        trunc[107] = '\0';

        draw_text(ren, LOG_X+8, y, trunc, col);
    }
}

static void redraw(SDL_Renderer *ren, int done)
{
    draw_background(ren);
    draw_header(ren, done);
    draw_mode_indicator(ren);
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
        SDL_Quit(); return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "[GUI] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win); SDL_Quit(); return 1;
    }

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
            if (ev.type == SDL_QUIT) { running = 0; }
            else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_q || k == SDLK_ESCAPE) running = 0;
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