/* glassine-shell-be.h — what glassine-shell's common part (glassine-shell.c: views, state,
 * gestures, requests) and its display backend share. The backend owns the
 * display connection: surfaces (the bar, the popup, the resize rail), the
 * window/workspace model and input: glassine-shell-wl.c (layer-shell + sway IPC). */
#ifndef GLASSINE_SHELL_BE_H
#define GLASSINE_SHELL_BE_H
#include "glassine-shell.h"

typedef unsigned long WinId;            /* a client window (sway: its container id) */

extern int verbose;
#define LOG(...) do { if (verbose) { fprintf(stderr, "glassine-shell: " __VA_ARGS__); fputc('\n', stderr); } } while (0)
double now_ms(void);

void epdd(const char *cmd);             /* a line to glassine-epdd-desktop's FIFO */
extern double S;                        /* device px per logical px (2) */
extern int SW, SH;                      /* the screen, device px */
#define BH 52.0                         /* bar height, logical */

/* ---- surfaces: an image in RAM shown by the backend -------------------------- */
typedef struct { double x, y, w, h; int act, arg; WinId win; } Hit;
#define MAXHIT 160
typedef struct {
    int x, y, wd, ht;                   /* device px */
    int dock, mapped, grabbed;
    cairo_surface_t *img;
    uint32_t *prev;
    Hit hit[MAXHIT]; int nhit;
    void *be;                           /* the backend's */
} Surf;
extern Surf bar, pop, rail;

/* at x,y, w x h (device px); resized: img is new (the old one freed) */
void be_surf_config(Surf *s, int x, int y, int w, int h, int resized);
/* rows y..y+h-1, columns x..x+w-1 of img changed */
void be_surf_put(Surf *s, int x, int y, int w, int h);
/* a popup's outline from img's alpha (may rewrite img's pixels) */
void be_surf_shape(Surf *s);
/* img's alpha made hard (< 128: cleared, else opaque, un-premultiplied); with
 * mask (w+7)/8 bytes a row: the opaque pixels' bits set */
void surf_harden(Surf *s, unsigned char *mask);
extern int be_align;                    /* surfaces at multiples of this (device px) */
void be_surf_map(Surf *s);              /* shown, on top */
void be_surf_unmap(Surf *s);
int be_grab_pointer(Surf *s);           /* all presses to s; 0: failed */
void be_ungrab_pointer(void);
void be_grab_keyboard(Surf *s);
void be_ungrab_keyboard(void);
/* the on-screen keyboard shown/hidden (search): the command to run */
const char *be_osk_cmd(int show);

/* ---- windows and workspaces ---------------------------------------------------- */
typedef struct {
    char id[96], name[96], exec[512], icon[160], wmclass[64], exe[64], comment[160];
    const char *cat;
    int terminal, single;               /* single: runs once (DBusActivatable / SingleMainWindow) */
} App;
/* the app of a window by its class and instance names ("" = none) */
App *app_match(const char *cls, const char *inst);
App *app_of(WinId w);                   /* the .desktop list refreshed, then be_app_of */

typedef struct { WinId w; long desk; int x, y, wd, ht, maxed; char name[96]; char icon[160]; } Client;   /* desk -1: minimized */
#define MAXCL 64
extern Client cl[MAXCL];                /* the app windows, as be_read_desktop found them */
extern int ncl;
/* a screen's tiling as the tree has it: its groups (the screen's children:
 * a window or a split half) in order, each with its windows in order; side: the
 * groups sit side by side (else stacked). Floating windows, launch
 * placeholders and Home are not in it; full: the window glassine-swayd shows
 * full screen over the others (`full`: floating, marked), 0: none */
#define MAXDESK 32
typedef struct { int side, ng, gn[4]; WinId w[4][4]; WinId full; } Layout;
extern Layout lay[MAXDESK];
extern long ndesk, curdesk;
extern WinId active;
extern int warea[4];

void be_read_desktop(void);             /* fills the above */
App *be_app_of(WinId w);
void be_title(WinId w, char *out, size_t len);
/* the overview's still of workspace index d (what it showed when last seen,
 * device px of the work area); NULL: none */
cairo_surface_t *be_snap(long d);
void be_snap_fresh(void);               /* before the overview opens: the visible screen's still, if old or none */
int be_sheet_window(WinId w);           /* can get the window sheet: an app window or a dialog */
void be_activate(WinId w);
void be_close(WinId w);
void be_restore(WinId w);               /* a minimized window (sway's scratchpad) back, tiled, here */
void be_goto_desk(long d);
void be_set_desk(WinId w, long d);      /* no switch */
void be_new_screen(WinId w, int follow); /* a new screen right after w's own */
void be_grab_holes(const int (*r)[4], int n);   /* device px: the catch-all lets these through (a divider under its buttons) */
void be_pop_input(const int (*r)[4], int n);    /* device px: only these of the popup take input; n 0: all of it */
void be_move_to_desk(WinId w, long d);  /* d < 0: a new first one; >= ndesk: a new last one; then go there */
void be_tile(const char *cmd);        /* a tiling command to glassine-swayd */
void be_theme_publish(const char *name);
/* a launch placeholder for a (started as pid, 0 = unknown): glassine-swayd hands its
 * tile to the app's window */
void be_placeholder(const App *a, int pid);

/* ---- the connection ----------------------------------------------------------------- */
int be_init(void);                      /* sets SW, SH; 0: no display */
int be_fd(void);
void be_dispatch(void);                 /* everything pending -> ev_* */
void be_flush(void);

/* ---- events, backend -> common (device px) --------------------------------------------- */
enum { SK_OTHER, SK_ESC, SK_BACKSPACE, SK_ENTER, SK_TAB, SK_BACKTAB, SK_LEFT, SK_RIGHT };
void ev_pointer(int down, int x, int y);    /* button 1 / a touch on the bar */
void ev_long_press(int x, int y);           /* a right click */
void ev_motion(int x, int y);               /* the pressed finger/button moved (no drags at present) */
void ev_raw(int down, int up);              /* the finger/button really went down/up (no grab sees it) */
void ev_key(int key, const char *utf8, int n);
void ev_desktop(void);                      /* workspaces / windows / the active one changed */
void ev_title(void);                        /* the active window's title changed */
void ev_placeholder_draw(cairo_t *cr, int w, int h, const App *a);  /* device px */

#endif
