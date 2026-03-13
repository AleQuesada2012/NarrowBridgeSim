/*
 * gui.c — Bridge Simulation Visualizer
 *
 * Reads structured log lines from the simulation via stdin (pipe),
 * maintains a live state, and redraws an X11 window at ~15 fps.
 *
 * Recognised log formats (all others are displayed in the log panel):
 *   [SLOT <id> <slot> <bridge_len> <EAST|WEST> <is_ambulance>]
 *   [QUEUE <EAST|WEST> <total> <ambulances>]
 *   [DIRECTION <EAST|WEST|NONE>]
 *
 * Compile:
 *   gcc gui.c -lX11 -lpthread -o gui
 *
 * Run (piped from simulation):
 *   ./bridge_sim | ./gui
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include <time.h>

/* ============================================================ */
/* =====================  Constants  ========================== */
/* ============================================================ */

#define WIN_W          1100
#define WIN_H           700

/* Bridge rendering */
#define BRIDGE_X        120          /* left edge of bridge lane */
#define BRIDGE_Y        260          /* top of bridge lane */
#define BRIDGE_W        860          /* rendered width (pixels) */
#define BRIDGE_H         80          /* lane height */
#define METER_TICK_H     10          /* tick mark height */

/* Queue lanes (west queue is left of bridge, east queue right of it) */
#define QUEUE_LANE_H     50
#define QUEUE_SLOT_W     36

/* Vehicle drawing */
#define CAR_W            28
#define CAR_H            22
#define AMB_W            32
#define AMB_H            24

/* Log panel */
#define LOG_X            10
#define LOG_Y           470
#define LOG_W          1080
#define LOG_H           220
#define LOG_MAX_LINES    12
#define LOG_LINE_H       17

/* Max vehicles tracked simultaneously */
#define MAX_VEHICLES    512

/* Refresh interval in microseconds (~15 fps) */
#define REFRESH_US      66000

/* ============================================================ */
/* =====================  State Types  ======================== */
/* ============================================================ */

typedef enum { DIR_EAST = 0, DIR_WEST = 1, DIR_NONE = 2 } GuiDir;

typedef struct {
    int  id;
    int  slot;          /* -1 = gone, 0..bridge_len-1 = on bridge */
    int  bridge_len;
    GuiDir direction;
    int  is_ambulance;
    int  active;        /* 1 if this slot is in use */
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
static int          bridge_length = 10;   /* updated from first SLOT line */
static GuiDir       bridge_dir    = DIR_NONE;
static QueueState   queue_east    = {0, 0};
static QueueState   queue_west    = {0, 0};
static int          total_east_passed = 0;
static int          total_west_passed = 0;
static int          sim_done      = 0;

/* Circular log buffer */
#define LOG_BUF_LINES 200
static char log_buf[LOG_BUF_LINES][256];
static int  log_head = 0;  /* next write position */
static int  log_count = 0;

/* ============================================================ */
/* =====================  Log Buffer  ========================= */
/* ============================================================ */

static void log_push(const char *line)
{
    /* strip newline */
    strncpy(log_buf[log_head], line, 255);
    log_buf[log_head][255] = '\0';
    char *nl = strchr(log_buf[log_head], '\n');
    if (nl) *nl = '\0';

    log_head = (log_head + 1) % LOG_BUF_LINES;
    if (log_count < LOG_BUF_LINES) log_count++;
}

/* Fill `out` with the `n` most recent log lines (oldest first). */
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
    /* Reuse existing or grab a free slot */
    for (int i = 0; i < MAX_VEHICLES; i++)
        if (!vehicles[i].active) {
            vehicles[i].id     = id;
            vehicles[i].active = 1;
            return &vehicles[i];
        }
    return NULL; /* table full — shouldn't happen with MAX_VEHICLES=512 */
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

        /* --- [SLOT id slot bridge_len direction is_ambulance] --- */
        if (line[0] == '[' && strncmp(line, "[SLOT ", 6) == 0) {
            int id, slot, blen, is_amb;
            char dir_str[8];
            if (sscanf(line, "[SLOT %d %d %d %7s %d]",
                       &id, &slot, &blen, dir_str, &is_amb) == 5)
            {
                bridge_length = blen;
                GuiDir gdir = (strcmp(dir_str, "EAST") == 0) ? DIR_EAST : DIR_WEST;

                if (slot == -1) {
                    /* Vehicle exited completely */
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
                        v->slot       = slot;
                        v->bridge_len = blen;
                        v->direction  = gdir;
                        v->is_ambulance = is_amb;
                    }
                }
            }
            /* SLOT lines are not pushed to the human log */
        }

        /* --- [QUEUE EAST|WEST total ambulances] --- */
        else if (strncmp(line, "[QUEUE ", 7) == 0) {
            char side[8];
            int total, ambs;
            if (sscanf(line, "[QUEUE %7s %d %d]", side, &total, &ambs) == 3) {
                if (strcmp(side, "EAST") == 0) {
                    queue_east.total      = total;
                    queue_east.ambulances = ambs;
                } else {
                    queue_west.total      = total;
                    queue_west.ambulances = ambs;
                }
            }
            /* Queue lines also not pushed to human log */
        }

        /* --- [DIRECTION EAST|WEST|NONE] --- */
        else if (strncmp(line, "[DIRECTION ", 11) == 0) {
            char d[8];
            if (sscanf(line, "[DIRECTION %7s]", d) == 1) {
                if      (strcmp(d, "EAST") == 0) bridge_dir = DIR_EAST;
                else if (strcmp(d, "WEST") == 0) bridge_dir = DIR_WEST;
                else                              bridge_dir = DIR_NONE;
            }
            /* Direction changes are structural, not pushed to log */
        }

        /* --- Simulation complete marker --- */
        else if (strstr(line, "Simulation complete")) {
            sim_done = 1;
            log_push(line);
        }

        /* --- All other lines go to the human-readable log --- */
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
/* =====================  Drawing Helpers  ==================== */
/* ============================================================ */

/* Colour table — allocated in init */
static unsigned long
    col_bg, col_bridge_road, col_bridge_rail,
    col_east_light, col_west_light, col_no_light,
    col_car_east, col_car_west,
    col_amb_east, col_amb_west,
    col_queue_bg, col_text,
    col_text_dim, col_panel_bg, col_grid,
    col_label_east, col_label_west,
    col_white, col_black, col_amber;

static Display *dpy;
static Window   win;
static GC       gc;
static int      screen;
static XFontStruct *font_normal;
static XFontStruct *font_bold;

static unsigned long alloc_rgb(int r, int g, int b)
{
    XColor c;
    c.red   = (unsigned short)(r * 257);
    c.green = (unsigned short)(g * 257);
    c.blue  = (unsigned short)(b * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    XAllocColor(dpy, DefaultColormap(dpy, screen), &c);
    return c.pixel;
}

static void init_colors(void)
{
    col_bg          = alloc_rgb(18,  20,  28);   /* dark navy */
    col_bridge_road = alloc_rgb(60,  65,  80);   /* asphalt */
    col_bridge_rail = alloc_rgb(120,130,150);    /* guardrail */
    col_east_light  = alloc_rgb(80, 200,  80);   /* green */
    col_west_light  = alloc_rgb(200, 80,  80);   /* red */
    col_no_light    = alloc_rgb(60,  60,  60);
    col_car_east    = alloc_rgb(80, 160, 255);   /* blue */
    col_car_west    = alloc_rgb(255,160,  60);   /* orange */
    col_amb_east    = alloc_rgb(255, 60,  60);   /* red */
    col_amb_west    = alloc_rgb(255, 60,  60);
    col_queue_bg    = alloc_rgb(30,  34,  48);
    col_text        = alloc_rgb(220,230,255);
    col_text_dim    = alloc_rgb(100,115,150);
    col_panel_bg    = alloc_rgb(22,  26,  38);
    col_grid        = alloc_rgb(40,  46,  64);
    col_label_east  = alloc_rgb(80, 160, 255);
    col_label_west  = alloc_rgb(255,160,  60);
    col_white       = alloc_rgb(255,255,255);
    col_black       = alloc_rgb(0,   0,   0);
    col_amber       = alloc_rgb(255,200,  0);
}

/* Draw filled rounded rectangle (approximated with Xlib arcs) */
static void fill_rect_r(int x, int y, int w, int h, unsigned long col, int r)
{
    XSetForeground(dpy, gc, col);
    /* main body minus corners */
    XFillRectangle(dpy, win, gc, x + r, y,     w - 2*r, h);
    XFillRectangle(dpy, win, gc, x,     y + r, w,       h - 2*r);
    /* four arc corners */
    XFillArc(dpy, win, gc, x,         y,         2*r, 2*r, 90*64,  90*64);
    XFillArc(dpy, win, gc, x+w-2*r,   y,         2*r, 2*r,  0,     90*64);
    XFillArc(dpy, win, gc, x,         y+h-2*r,   2*r, 2*r, 180*64, 90*64);
    XFillArc(dpy, win, gc, x+w-2*r,   y+h-2*r,   2*r, 2*r, 270*64, 90*64);
}

static void draw_text(int x, int y, const char *s, unsigned long col)
{
    XSetForeground(dpy, gc, col);
    XDrawString(dpy, win, gc, x, y, s, (int)strlen(s));
}

static void draw_text_bold(int x, int y, const char *s, unsigned long col)
{
    if (font_bold)
        XSetFont(dpy, gc, font_bold->fid);
    XSetForeground(dpy, gc, col);
    XDrawString(dpy, win, gc, x, y, s, (int)strlen(s));
    if (font_normal)
        XSetFont(dpy, gc, font_normal->fid);
}

/* ============================================================ */
/* =====================  Scene Drawing  ====================== */
/* ============================================================ */

static void draw_background(void)
{
    XSetForeground(dpy, gc, col_bg);
    XFillRectangle(dpy, win, gc, 0, 0, WIN_W, WIN_H);
}

static void draw_header(int done)
{
    char title[128];
    snprintf(title, sizeof(title),
             "Narrow Bridge Simulation  —  length: %d m%s",
             bridge_length, done ? "  [COMPLETE]" : "");
    draw_text_bold(WIN_W/2 - (int)strlen(title)*4, 28, title, col_text);
}

/* Traffic light indicator (small circle) */
static void draw_light(int cx, int cy, int radius,
                        unsigned long on_col, int is_on)
{
    /* outer ring */
    XSetForeground(dpy, gc, col_grid);
    XFillArc(dpy, win, gc, cx-radius-2, cy-radius-2,
             2*(radius+2), 2*(radius+2), 0, 360*64);
    /* bulb */
    XSetForeground(dpy, gc, is_on ? on_col : col_no_light);
    XFillArc(dpy, win, gc, cx-radius, cy-radius,
             2*radius, 2*radius, 0, 360*64);
}

static void draw_direction_indicator(void)
{
    /* EAST indicator — right side of title bar */
    int east_on = (bridge_dir == DIR_EAST);
    int west_on = (bridge_dir == DIR_WEST);

    /* Left lamp (WEST going) */
    draw_light(WIN_W/2 - 80, 50, 10, col_east_light, west_on);
    draw_text(WIN_W/2 - 95, 75, "W→", col_text_dim);

    /* Right lamp (EAST going) */
    draw_light(WIN_W/2 + 80, 50, 10, col_east_light, east_on);
    draw_text(WIN_W/2 + 68, 75, "→E", col_text_dim);

    /* Centre label */
    const char *dstr =
        (bridge_dir == DIR_EAST) ? "EASTBOUND" :
        (bridge_dir == DIR_WEST) ? "WESTBOUND" : "IDLE";
    unsigned long dcol =
        (bridge_dir == DIR_EAST) ? col_label_east :
        (bridge_dir == DIR_WEST) ? col_label_west  : col_text_dim;
    draw_text_bold(WIN_W/2 - (int)strlen(dstr)*4, 58, dstr, dcol);
}

static void draw_bridge(void)
{
    /* Road surface */
    fill_rect_r(BRIDGE_X, BRIDGE_Y, BRIDGE_W, BRIDGE_H, col_bridge_road, 6);

    /* Centre dashed line */
    XSetForeground(dpy, gc, col_grid);
    int cy = BRIDGE_Y + BRIDGE_H / 2;
    for (int x = BRIDGE_X + 10; x < BRIDGE_X + BRIDGE_W - 10; x += 20) {
        XDrawLine(dpy, win, gc, x, cy, x + 10, cy);
    }

    /* Guardrails */
    XSetForeground(dpy, gc, col_bridge_rail);
    XFillRectangle(dpy, win, gc, BRIDGE_X, BRIDGE_Y,         BRIDGE_W, 4);
    XFillRectangle(dpy, win, gc, BRIDGE_X, BRIDGE_Y+BRIDGE_H-4, BRIDGE_W, 4);

    /* Meter tick marks every 10 m (or every m if bridge is short) */
    int tick_step = (bridge_length <= 20) ? 1 :
                    (bridge_length <= 50) ? 5 : 10;
    XSetForeground(dpy, gc, col_text_dim);
    for (int m = 0; m <= bridge_length; m += tick_step) {
        int px = BRIDGE_X + (int)((double)m / bridge_length * BRIDGE_W);
        XDrawLine(dpy, win, gc,
                  px, BRIDGE_Y - METER_TICK_H,
                  px, BRIDGE_Y);
        if (m == 0 || m == bridge_length || m % (tick_step * 2) == 0) {
            char label[16];
            snprintf(label, sizeof(label), "%dm", m);
            draw_text(px - 8, BRIDGE_Y - METER_TICK_H - 3, label, col_text_dim);
        }
    }

    /* Side labels */
    draw_text_bold(BRIDGE_X - 90, BRIDGE_Y + BRIDGE_H/2 + 5, "WEST", col_label_west);
    draw_text_bold(BRIDGE_X + BRIDGE_W + 12, BRIDGE_Y + BRIDGE_H/2 + 5, "EAST", col_label_east);
}

/* Draw a single vehicle rectangle on the bridge */
static void draw_vehicle_on_bridge(const VehicleState *v)
{
    if (v->slot < 0 || v->bridge_len <= 0) return;

    double frac = (double)v->slot / v->bridge_len;
    /* EAST vehicles: slot 0 = left (WEST entrance), slot N = right (EAST exit)
       WEST vehicles: slot 0 = right (EAST entrance), slot N = left (WEST exit) */
    if (v->direction == DIR_WEST)
        frac = 1.0 - frac;

    int px = BRIDGE_X + (int)(frac * BRIDGE_W);
    int w  = v->is_ambulance ? AMB_W : CAR_W;
    int h  = v->is_ambulance ? AMB_H : CAR_H;
    int x  = px - w/2;
    int y  = BRIDGE_Y + (BRIDGE_H - h) / 2;

    unsigned long body_col =
        v->is_ambulance ? col_amb_east :
        (v->direction == DIR_EAST) ? col_car_east : col_car_west;

    fill_rect_r(x, y, w, h, body_col, 4);

    /* ID label */
    char label[8];
    snprintf(label, sizeof(label), "%d", v->id);
    int tw = (int)strlen(label) * 6;
    XSetForeground(dpy, gc, col_black);
    XDrawString(dpy, win, gc, x + w/2 - tw/2, y + h/2 + 4,
                label, (int)strlen(label));

    /* Ambulance cross */
    if (v->is_ambulance) {
        XSetForeground(dpy, gc, col_white);
        XDrawLine(dpy, win, gc, x+w/2, y+2, x+w/2, y+h-2);
        XDrawLine(dpy, win, gc, x+2,   y+h/2, x+w-2, y+h/2);
    }
}

/* Draw a queue of vehicles waiting on one side */
static void draw_queue(const QueueState *q, int is_east)
{
    int base_y = BRIDGE_Y + BRIDGE_H + 20;
    int total  = q->total;
    int ambs   = q->ambulances;

    /* Background lane */
    int lx = is_east
        ? (BRIDGE_X + BRIDGE_W + 4)
        : (BRIDGE_X - 4 - (total > 0 ? total * QUEUE_SLOT_W : QUEUE_SLOT_W));
    /* Always draw at least one slot wide for aesthetics */
    int lw = (total > 0 ? total : 1) * QUEUE_SLOT_W;
    /* Clamp to window */
    if (!is_east && lx < 0) lx = 0;

    fill_rect_r(lx, base_y, lw, QUEUE_LANE_H, col_queue_bg, 4);

    /* Draw vehicle icons */
    for (int i = 0; i < total && i < 20; i++) {
        int amb = (i < ambs);   /* first `ambs` slots are ambulances */
        unsigned long c = amb
            ? col_amb_east
            : (is_east ? col_car_east : col_car_west);

        int vx, vy;
        if (is_east) {
            vx = lx + i * QUEUE_SLOT_W + 4;
        } else {
            /* West queue: rightmost slot is closest to bridge */
            vx = lx + (total - 1 - i) * QUEUE_SLOT_W + 4;
        }
        vy = base_y + (QUEUE_LANE_H - CAR_H) / 2;

        fill_rect_r(vx, vy, CAR_W, CAR_H, c, 3);
        if (amb) {
            XSetForeground(dpy, gc, col_white);
            XDrawLine(dpy, win, gc, vx+CAR_W/2, vy+2, vx+CAR_W/2, vy+CAR_H-2);
            XDrawLine(dpy, win, gc, vx+2, vy+CAR_H/2, vx+CAR_W-2, vy+CAR_H/2);
        }
    }
    if (total > 20) {
        char more[16];
        snprintf(more, sizeof(more), "+%d", total - 20);
        draw_text(lx + lw - 30, base_y + QUEUE_LANE_H/2 + 5, more, col_text_dim);
    }

    /* Label */
    char label[64];
    snprintf(label, sizeof(label),
             "%s queue: %d waiting (%d amb)",
             is_east ? "East" : "West",
             total, ambs);
    draw_text(lx, base_y + QUEUE_LANE_H + 16, label,
              is_east ? col_label_east : col_label_west);
}

static void draw_stats(void)
{
    int sx = 30, sy = 100;
    char buf[128];

    snprintf(buf, sizeof(buf), "Passed EAST: %d", total_east_passed);
    draw_text(sx, sy, buf, col_label_east);

    snprintf(buf, sizeof(buf), "Passed WEST: %d", total_west_passed);
    draw_text(sx, sy + 20, buf, col_label_west);

    /* Count vehicles currently on bridge */
    int on = 0;
    for (int i = 0; i < MAX_VEHICLES; i++)
        if (vehicles[i].active && vehicles[i].slot >= 0)
            on++;

    snprintf(buf, sizeof(buf), "On bridge:   %d", on);
    draw_text(sx, sy + 40, buf, col_text);
}

static void draw_log_panel(void)
{
    fill_rect_r(LOG_X, LOG_Y, LOG_W, LOG_H, col_panel_bg, 6);

    XSetForeground(dpy, gc, col_grid);
    XDrawRectangle(dpy, win, gc, LOG_X, LOG_Y, LOG_W, LOG_H);

    draw_text_bold(LOG_X + 8, LOG_Y - 6, "Console log", col_text_dim);

    char lines[LOG_MAX_LINES][256];
    int  n = log_recent(lines, LOG_MAX_LINES);

    for (int i = 0; i < n; i++) {
        int y = LOG_Y + 16 + i * LOG_LINE_H;
        unsigned long col = col_text_dim;

        /* Colour-code interesting lines */
        if      (strstr(lines[i], "[AMBULANCE]") || strstr(lines[i], "PRIORITY"))
            col = col_amb_east;
        else if (strstr(lines[i], "entered"))
            col = col_east_light;
        else if (strstr(lines[i], "exited"))
            col = col_text;
        else if (strstr(lines[i], "Switching") || strstr(lines[i], "direction"))
            col = col_amber;
        else if (strstr(lines[i], "Waiting"))
            col = col_text_dim;

        /* Truncate to fit panel */
        char trunc[128];
        strncpy(trunc, lines[i], 127);
        trunc[127] = '\0';

        draw_text(LOG_X + 8, y, trunc, col);
    }
}

static void redraw(int done)
{
    draw_background();
    draw_header(done);
    draw_direction_indicator();
    draw_bridge();
    draw_stats();

    /* Vehicles on the bridge */
    for (int i = 0; i < MAX_VEHICLES; i++)
        if (vehicles[i].active && vehicles[i].slot >= 0)
            draw_vehicle_on_bridge(&vehicles[i]);

    draw_queue(&queue_east, 1);
    draw_queue(&queue_west, 0);
    draw_log_panel();

    XFlush(dpy);
}

/* ============================================================ */
/* ========================  Main  ============================ */
/* ============================================================ */

int main(void)
{
    /* Pipe mode: stdin is the simulation output.
     * Make it line-buffered so we read as soon as lines arrive. */
    setvbuf(stdin,  NULL, _IOLBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Initialise vehicle table */
    memset(vehicles, 0, sizeof(vehicles));

    /* Open X display */
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "[GUI] Cannot open X display. "
                        "Is DISPLAY set? (try: export DISPLAY=:0)\n");
        return 1;
    }
    screen = DefaultScreen(dpy);

    /* Create window */
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen),
                              100, 100, WIN_W, WIN_H, 2,
                              BlackPixel(dpy, screen),
                              BlackPixel(dpy, screen));

    XStoreName(dpy, win, "Bridge Simulation — Live View");
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(dpy, win);

    /* GC */
    gc = XCreateGC(dpy, win, 0, NULL);

    /* Colours */
    init_colors();
    XSetBackground(dpy, gc, col_bg);

    /* Fonts — try a few common X11 fixed fonts in order of preference */
    const char *font_names[] = {
        "-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1",
        "-misc-fixed-medium-r-normal--13-120-75-75-c-70-iso8859-1",
        "fixed",
        NULL
    };
    font_normal = NULL;
    for (int i = 0; font_names[i] && !font_normal; i++)
        font_normal = XLoadQueryFont(dpy, font_names[i]);
    if (!font_normal)
        font_normal = XLoadQueryFont(dpy, "fixed");

    const char *bold_names[] = {
        "-misc-fixed-bold-r-normal--14-130-75-75-c-70-iso8859-1",
        "-misc-fixed-bold-r-normal--13-120-75-75-c-70-iso8859-1",
        NULL
    };
    font_bold = NULL;
    for (int i = 0; bold_names[i] && !font_bold; i++)
        font_bold = XLoadQueryFont(dpy, bold_names[i]);

    if (font_normal) XSetFont(dpy, gc, font_normal->fid);

    /* Start parser thread */
    pthread_t ptid;
    pthread_create(&ptid, NULL, parser_thread, NULL);

    /* Event + redraw loop */
    XEvent ev;
    struct timespec ts_last = {0, 0};

    while (1) {
        /* Handle X events */
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);
            if (ev.type == KeyPress) {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_q || ks == XK_Escape) goto quit;
            }
            if (ev.type == Expose || ev.type == MapNotify) {
                pthread_mutex_lock(&state_lock);
                redraw(sim_done);
                pthread_mutex_unlock(&state_lock);
            }
        }

        /* Throttle redraws to ~15 fps */
        struct timespec ts_now;
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        long elapsed_us =
            (ts_now.tv_sec  - ts_last.tv_sec)  * 1000000L +
            (ts_now.tv_nsec - ts_last.tv_nsec) / 1000L;

        if (elapsed_us >= REFRESH_US) {
            ts_last = ts_now;
            pthread_mutex_lock(&state_lock);
            redraw(sim_done);
            pthread_mutex_unlock(&state_lock);
        }

        usleep(5000); /* 5 ms yield */

        pthread_mutex_lock(&state_lock);
        int done = sim_done;
        pthread_mutex_unlock(&state_lock);

        if (done) {
            /* Redraw one final time with "COMPLETE" banner, then wait for key */
            pthread_mutex_lock(&state_lock);
            redraw(1);
            pthread_mutex_unlock(&state_lock);

            /* Wait for a keypress or window close */
            while (1) {
                XNextEvent(dpy, &ev);
                if (ev.type == KeyPress) goto quit;
                if (ev.type == Expose)  { redraw(1); XFlush(dpy); }
            }
        }
    }

quit:
    pthread_cancel(ptid);
    pthread_join(ptid, NULL);

    if (font_normal) XFreeFont(dpy, font_normal);
    if (font_bold)   XFreeFont(dpy, font_bold);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}