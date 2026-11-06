/* glassine-shell — the e-ink desktop shell: the top bar and
 * its popups, drawn natively (cairo + Pango on layer-shell surfaces), after the "E-ink Shell"
 * design (three voices, one layout: the voices and the drawing are GlassineOS/ui's,
 * glassine-ui.h).
 *
 * bar Home (shows or hides Home, glassine-swayd's: pressed while it
 * is shown) | the screen button: "n/N" in a small screen frame,
 * then the screen's apps' icons in reading order; opens Overview
 * (spec 001 FR-001..004) | clock + notification counter, Wi-Fi,
 * battery, keyboard, refresh | quick settings. A layer-shell
 * surface with an exclusive zone.
 * overview the screens, four a page: each its preview (tap = go there),
 * its apps' icons in four slots around it, under it move it
 * earlier / close it with its apps / move it later. Tap an app:
 * it moves -- arrows beside the other apps place it, its own
 * actions (close, full screen, new screen) sit in a half-circle
 * around it (FR-010..019)
 * notices (tap on the clock) the notifications (glassine-shell-notify.c):
 * tap = open, x = dismiss, clear all
 * divider (tap on a glassine-swayd handle) Swap on every divider,
 * Direction on a two-window screen, the crossing toggle on a 2 x 2
 * one; they stay until a tap elsewhere or Escape (FR-022..024)
 * quick Wi-Fi, Bluetooth, mute, refresh (hold one: its settings app);
 * front light, warmth, volume (hold a light label: the light
 * settings); e-ink mode for other apps; rotation (auto or one of
 * four, /etc/glassine/rotation via glassine-settings-root, glassine-rotd turns
 * the screen). On a landscape screen two columns.
 * power (the button next to the settings gear) sleep, restart, power
 * off, restart into the other system or recovery: one tap (the
 * popup is the confirmation)
 * resizing while a glassine-swayd handle is dragged: only the bar moves (drawn
 * by glassine-swayd / glassine-epdd-desktop) and, for rows, the 1/3 1/2 2/3 snap
 * marks
 *
 * Why not GTK/Python (glassine-popupd): a tap has to become ink fast. Everything
 * here is one resident process; a popup is a cairo image in RAM that goes out
 * as a wl_shm buffer, damaged only where it changed (pressing a button repaints
 * that button only); glassine-epdd-desktop paints a new popup fast (pol_popup, sway's
 * "popup" hint). Touch feedback is inversion of the pressed target (e-ink: one
 * clean change, no animation).
 *
 * Theme: ~/.config/glassine-shell/theme (white | editorial | stationery), switched
 * live by "glassine-shell theme NAME" or a tap on the quick settings title. The name
 * is published (be_theme_publish): glassine-swayd draws its divider handles to match.
 *
 * Requests (datagrams on $XDG_RUNTIME_DIR/glassine-shell, NUL-separated words):
 * toggle VIEW overview | quick | notices | power ("workspaces" = overview;
 * home | launcher = the request home)
 * home show or hide Home: "home toggle" to glassine-swayd (the
 * bar's button, the Home gesture)
 * home VISIBLE HOLE (glassine-swayd, on every change) Home is shown (0|1);
 * the empty screen under it, its number (0: none)
 * divider-tap | dividers N,ORIENT0 x,y,w,h,vert,level,a,b,pos... (glassine-swayd;
 * level 0 the main line, 1/2 the inner line of half 0/1; pos a
 * fraction; the old 8-field form without pos is read too)
 * drag VERT POS A0 ALEN C0 CLEN | dragend (glassine-swayd)
 * theme NAME | close
 * launch ID [new|here] start the app (desktop id): first "next new|here" to
 * glassine-swayd (where its window goes), "next hole" while
 * Home sits on an empty screen; without where: as glassine-swayd
 * places it
 * open ID its window if it has one, else launch
 * light up|down|warmer|cooler the front light one step, as the quick settings'
 * sliders do (the keyboard's brightness keys, sway keys.conf)
 * verbose toggle the log (render times etc.)
 *
 * To glassine-swayd (spec 001): place M T DIR, newscreen M 0, full M, scrmove N D,
 * scrclose N, swaphalves W, swapin W, direction W, cross W, home toggle|hide,
 * next new|here|hole.
 *
 * glassine-shell [-v] run (session autostart)
 * glassine-shell theme NAME switch the theme
 * glassine-shell toggle VIEW | home | close | verbose | launch ID [new|here] | light STEP send a request
 * GLASSINE_SHELL_HITS=DIR (host tests) the targets of each render to DIR/bar, DIR/pop
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <glib.h>
#include "glassine-shell-be.h"

int verbose;

double S;                               /* device px per logical px (2) */
int SW, SH;
static double LW, LH;
static const Theme *T;
#define NGFD 16                         /* GLib poll fds at most */
#define GAP_DEV 40                      /* the divider handle's thickness (glassine-swayd), device px */

double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* ============================================================================
 * surfaces: an image in RAM, shown by the backend
 * ========================================================================== */
enum {
    A_NONE, A_BACKDROP,
    A_BAR_HOME, A_BAR_WS, A_BAR_NOTIF, A_BAR_REFRESH, A_BAR_KBD, A_BAR_QS,
    A_QS_TITLE, A_QS_TOGGLE, A_QS_SLABEL, A_QS_SLIDER, A_QS_FLUID, A_QS_SETTINGS,
    A_QS_PWR, A_QS_ROT, A_P_ACT, A_P_CANCEL,
    A_WS_GOTO, A_WS_RESTORE,
    A_BAR_OV, A_BAR_CHG, A_OV_X, A_OV_PICK, A_OV_PUT, A_OV_NEW, A_OV_CLOSEAPP, A_OV_FULL, A_OV_CANCEL,
    A_OV_PREV, A_OV_NEXT, A_SCR_PREV, A_SCR_CLOSE, A_SCR_NEXT,
    A_N_CLEAR, A_N_OPEN, A_N_DISMISS,
    A_DIV_SWAP, A_DIV_DIR, A_DIV_CROSS,
};
Surf bar, pop, rail;

static Surf *cur_s;
static Gc *cur_g;
static double bx0, by0, bx1, by1;       /* layout pass: what the view covers */

static void bound(double x, double y, double w, double h, double m)
{
    if (x - m < bx0) bx0 = x - m;
    if (y - m < by0) by0 = y - m;
    if (x + w + m > bx1) bx1 = x + w + m;
    if (y + h + m > by1) by1 = y + h + m;
}

static void hit(double x, double y, double w, double h, int act, int arg, WinId win)
{
    if (!cur_g->paint || cur_s->nhit >= MAXHIT) return;
    cur_s->hit[cur_s->nhit++] = (Hit){ x, y, w, h, act, arg, win };
}

/* the pressed target (inverted) */
/* down: a press arrived and its release not yet; a touch press can be held
 * (the workspace buttons) like a mouse press. */
static struct { Surf *s; int act, arg; WinId win; double x, y, w, h; int consumed, down; double t;
                } press;

static void hits_dump(const Surf *s, const char *file);
static void surf_hide(Surf *s)
{
    if (s->grabbed) { be_ungrab_pointer(); be_ungrab_keyboard(); s->grabbed = 0; }
    be_surf_unmap(s);
    s->mapped = 0;
    s->nhit = 0;
    hits_dump(s, s == &bar ? "bar" : s == &pop ? "pop" : "rail");
}

static void surf_alloc(Surf *s, int x, int y, int w, int h)
{
    int resized = s->wd != w || s->ht != h;
    if (resized) {
        if (s->img) cairo_surface_destroy(s->img);
        free(s->prev);
        s->img = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        s->prev = malloc((size_t)w * h * 4);
        memset(s->prev, 0x5a, (size_t)w * h * 4);          /* != any frame: first put is whole */
    }
    be_surf_config(s, x, y, w, h, resized);
    s->x = x; s->y = y; s->wd = w; s->ht = h;
}

/* send the part of the image that changed since the last put */
static void surf_put(Surf *s)
{
    uint32_t *p = (uint32_t *)cairo_image_surface_get_data(s->img);
    int stride = cairo_image_surface_get_stride(s->img) / 4, w = s->wd, h = s->ht;
    int y0 = -1, y1 = -1, x0 = w, x1 = -1;
    for (int y = 0; y < h; y++) {
        uint32_t *a = p + (size_t)y * stride, *b = s->prev + (size_t)y * w;
        if (!memcmp(a, b, w * 4)) continue;
        if (y0 < 0) y0 = y;
        y1 = y;
        int l = 0, r = w - 1;
        while (l < w && a[l] == b[l]) l++;
        while (r > l && a[r] == b[r]) r--;
        if (l < x0) x0 = l;
        if (r > x1) x1 = r;
        memcpy(b, a, w * 4);
    }
    if (y0 < 0) return;
    be_surf_put(s, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    LOG("put %dx%d+%d+%d", x1 - x0 + 1, y1 - y0 + 1, s->x + x0, s->y + y0);
}

/* premultiplied edge pixels made opaque: no grey fringe around a card */
void surf_harden(Surf *s, unsigned char *m)
{
    uint32_t *p = (uint32_t *)cairo_image_surface_get_data(s->img);
    int stride = cairo_image_surface_get_stride(s->img) / 4, w = s->wd, h = s->ht, bpr = (w + 7) / 8;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t v = p[(size_t)y * stride + x];
            unsigned a = v >> 24;
            if (a < 128) { p[(size_t)y * stride + x] = 0; continue; }
            if (m) m[y * bpr + x / 8] |= 1 << (x & 7);
            if (a < 255) {
                unsigned r = ((v >> 16) & 255) * 255 / a, g = ((v >> 8) & 255) * 255 / a, b = (v & 255) * 255 / a;
                p[(size_t)y * stride + x] = 0xff000000u | (r > 255 ? 255 : r) << 16 | (g > 255 ? 255 : g) << 8 | (b > 255 ? 255 : b);
            }
        }
}

typedef void (*View)(Gc *g);

/* GLASSINE_SHELL_HITS=DIR (host tests, test/shell): after each render, the
 * surface's targets to DIR/bar or DIR/pop, one per line: "NAME ARG WIN X Y W H"
 * (logical px, WIN in hex), so a test can tap a target by what it does */
static const char *act_name(int a)
{
    switch (a) {
    case A_BAR_HOME: return "home";
    case A_BAR_OV: return "screen";
    case A_OV_X: return "ov-close";
    case A_OV_CANCEL: return "ov-cancel";
    case A_OV_PICK: return "ov-pick";
    case A_OV_PUT: return "ov-put";
    case A_OV_CLOSEAPP: return "ov-closeapp";
    case A_OV_FULL: return "ov-full";
    case A_OV_NEW: return "ov-new";
    case A_OV_PREV: return "ov-prev";
    case A_OV_NEXT: return "ov-next";
    case A_WS_GOTO: return "ov-goto";
    case A_SCR_PREV: return "scr-prev";
    case A_SCR_CLOSE: return "scr-close";
    case A_SCR_NEXT: return "scr-next";
    case A_DIV_SWAP: return "div-swap";
    case A_DIV_DIR: return "div-dir";
    case A_DIV_CROSS: return "div-cross";
    default: return NULL;
    }
}

static void hits_dump(const Surf *s, const char *file)
{
    const char *dir = getenv("GLASSINE_SHELL_HITS");
    if (!dir || !*dir) return;
    char p[512], t[520];
    snprintf(p, sizeof p, "%s/%s", dir, file);
    snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w");
    if (!f) return;
    for (int i = 0; i < s->nhit; i++) {
        const Hit *h = &s->hit[i];
        const char *n = act_name(h->act);
        if (n) fprintf(f, "%s %d %lx %.1f %.1f %.1f %.1f\n", n, h->arg, h->win, h->x, h->y, h->w, h->h);
        else if (h->act != A_NONE) fprintf(f, "act%d %d %lx %.1f %.1f %.1f %.1f\n", h->act, h->arg, h->win, h->x, h->y, h->w, h->h);
    }
    fclose(f);
    rename(t, p);
}

static void surf_render(Surf *s, View v)
{
    double t_start = now_ms();
    static cairo_surface_t *meas;
    if (!meas) meas = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(meas);
    cairo_scale(cr, S, S);
    Gc g = { cr, 0, T };
    bx0 = by0 = 1e9; bx1 = by1 = -1e9;
    cur_s = s; cur_g = &g;
    v(&g);
    cairo_destroy(cr);
    if (bx1 <= bx0) { surf_hide(s); return; }
    int x0 = (int)floor(bx0 * S), y0 = (int)floor(by0 * S), x1 = (int)ceil(bx1 * S), y1 = (int)ceil(by1 * S);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SW) x1 = SW;
    if (y1 > SH) y1 = SH;
    if (be_align > 1) {
        x0 -= x0 % be_align; y0 -= y0 % be_align;
        x1 += (be_align - x1 % be_align) % be_align; y1 += (be_align - y1 % be_align) % be_align;
    }
    surf_alloc(s, x0, y0, x1 - x0, y1 - y0);

    cr = cairo_create(s->img);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_translate(cr, -x0, -y0);
    cairo_scale(cr, S, S);
    g.cr = cr; g.paint = 1;
    s->nhit = 0;
    v(&g);
    cairo_destroy(cr);
    cairo_surface_flush(s->img);
    if (!s->dock) be_surf_shape(s);
    if (press.s == s && press.act != A_QS_SLIDER) {   /* touch feedback: the target inverted (a slider shows its knob instead) */
        uint32_t *p = (uint32_t *)cairo_image_surface_get_data(s->img);
        int stride = cairo_image_surface_get_stride(s->img) / 4;
        int ax = (int)floor(press.x * S) - x0, ay = (int)floor(press.y * S) - y0;
        int bx = (int)ceil((press.x + press.w) * S) - x0, by = (int)ceil((press.y + press.h) * S) - y0;
        for (int y = ay < 0 ? 0 : ay; y < by && y < s->ht; y++)
            for (int x = ax < 0 ? 0 : ax; x < bx && x < s->wd; x++)
                if (p[(size_t)y * stride + x] >> 24) p[(size_t)y * stride + x] ^= 0x00ffffff;
    }
    cairo_surface_mark_dirty(s->img);
    hits_dump(s, s == &bar ? "bar" : s == &pop ? "pop" : "rail");
    double t_drawn = now_ms();
    surf_put(s);
    if (!s->mapped) {
        be_surf_map(s);
        s->mapped = 1;
    }
    LOG("render %dx%d: draw %.1f ms, put %.1f ms, t=%lld", s->wd, s->ht, t_drawn - t_start, now_ms() - t_drawn,
        (long long)now_ms() % 1000000);    /* t: the clock of glassine-epdd-desktop's verbose log */
}

/* ============================================================================
 * desktop state (the backend's window model)
 * ========================================================================== */
/* ---- apps (.desktop) ------------------------------------------------------- */
static App *apps;
static int napps, apps_dirty = 1;
#define NCATS 9

static const char *category(const char *cats)
{
    static const struct { const char *fd, *cat; } map[] = {
        { "Viewer", "Reading" }, { "Literature", "Reading" }, { "Dictionary", "Reading" }, { "Presentation", "Office" },
        { "Office", "Office" }, { "Education", "Education" }, { "Science", "Education" }, { "Game", "Games" },
        { "AudioVideo", "Multimedia" }, { "Audio", "Multimedia" }, { "Video", "Multimedia" },
        { "Graphics", "Office" }, { "Network", "Internet" }, { "WebBrowser", "Internet" },
        { "Settings", "System" }, { "System", "System" }, { "Development", "System" }, { "Utility", "System" },
    };
    for (unsigned i = 0; i < sizeof map / sizeof *map; i++) {
        const char *p = cats;
        size_t l = strlen(map[i].fd);
        while ((p = strstr(p, map[i].fd))) {
            if ((p == cats || p[-1] == ';') && (p[l] == ';' || !p[l])) return map[i].cat;
            p += l;
        }
    }
    return "System";
}

static int hidden_ids(const char *id)
{
    const char *files[2] = { "/etc/glassine/grid-hidden", NULL };
    char home[512];
    snprintf(home, sizeof home, "%s/.config/glassine/grid-hidden", getenv("HOME") ? getenv("HOME") : "");
    files[1] = home;
    for (int i = 0; i < 2; i++) {
        FILE *f = fopen(files[i], "r");
        if (!f) continue;
        char l[256];
        while (fgets(l, sizeof l, f)) {
            l[strcspn(l, "\r\n")] = 0;
            if (l[0] && l[0] != '#' && !strcmp(l, id)) { fclose(f); return 1; }
        }
        fclose(f);
    }
    return 0;
}

static void state_path(char *out, size_t n, const char *kind, const char *file)
{
    const char *h = getenv("HOME") ? getenv("HOME") : "/tmp";
    snprintf(out, n, "%s/%s/glassine-shell", h, kind);
    mkdir(out, 0755);
    size_t l = strlen(out);
    snprintf(out + l, n - l, "/%s", file);
}

static int in_list_file(const char *path, const char *id)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char l[256];
    int r = 0;
    while (fgets(l, sizeof l, f)) { l[strcspn(l, "\r\n")] = 0; if (!strcmp(l, id)) r = 1; }
    fclose(f);
    return r;
}

/* a desktop file's OnlyShowIn/NotShowIn list ("A;B;") names one of the desktops in
 * $XDG_CURRENT_DESKTOP ("sway", or several separated by ':') */
static int desktop_listed(const char *list)
{
    const char *cur = getenv("XDG_CURRENT_DESKTOP");
    char d[128];
    snprintf(d, sizeof d, "%s", cur && *cur ? cur : "sway");
    for (char *sv = NULL, *t = strtok_r(d, ":", &sv); t; t = strtok_r(NULL, ":", &sv)) {
        size_t n = strlen(t);
        for (const char *q = list; *q; ) {
            size_t m = strcspn(q, ";");
            if (m == n && !strncasecmp(q, t, n)) return 1;
            q += m + (q[m] == ';');
        }
    }
    return 0;
}

static void load_app(const char *dir, const char *fn)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, fn);
    FILE *f = fopen(p, "r");
    if (!f) return;
    App a = { 0 };
    snprintf(a.id, sizeof a.id, "%.*s", (int)(strlen(fn) - 8), fn);
    char l[1024], cats[256] = "";
    int in = 0, nodisp = 0, app = 0, notshown = 0;
    while (fgets(l, sizeof l, f)) {
        l[strcspn(l, "\r\n")] = 0;
        if (l[0] == '[') { in = !strcmp(l, "[Desktop Entry]"); continue; }
        if (!in) continue;
        char *v = strchr(l, '=');
        if (!v) continue;
        *v++ = 0;
        if (!strcmp(l, "Name")) snprintf(a.name, sizeof a.name, "%s", v);
        else if (!strcmp(l, "Comment")) snprintf(a.comment, sizeof a.comment, "%s", v);
        else if (!strcmp(l, "GenericName") && !a.comment[0]) snprintf(a.comment, sizeof a.comment, "%s", v);
        else if (!strcmp(l, "Exec")) snprintf(a.exec, sizeof a.exec, "%s", v);
        else if (!strcmp(l, "Icon")) snprintf(a.icon, sizeof a.icon, "%s", v);
        else if (!strcmp(l, "Categories")) snprintf(cats, sizeof cats, "%s", v);
        else if (!strcmp(l, "StartupWMClass")) snprintf(a.wmclass, sizeof a.wmclass, "%s", v);
        else if (!strcmp(l, "Terminal")) a.terminal = !strcmp(v, "true");
        else if (!strcmp(l, "NoDisplay") || !strcmp(l, "Hidden")) nodisp |= !strcmp(v, "true");
        else if (!strcmp(l, "Type")) app = !strcmp(v, "Application");
        else if (!strcmp(l, "OnlyShowIn")) notshown |= !desktop_listed(v);
        else if (!strcmp(l, "NotShowIn")) notshown |= desktop_listed(v);
    }
    fclose(f);
    if (!app || nodisp || notshown || !a.name[0] || !a.exec[0] || hidden_ids(a.id)) return;
    for (int i = 0; i < napps; i++) if (!strcmp(apps[i].id, a.id)) return;     /* ~/.local first */
    /* the program: the first word, past glassine-app-render's wrapper ("glassine-appenv PROFILE CMD") */
    const char *ex = a.exec;
    const char *sp = strchr(ex, ' ');
    if (sp && sp - ex >= 10 && !strncmp(sp - 10, "glassine-appenv", 10)) {
        const char *q = strchr(sp + 1, ' ');
        if (q) { ex = q + 1; sp = strchr(ex, ' '); }
    }
    snprintf(a.exe, sizeof a.exe, "%.*s", (int)(sp ? sp - ex : (long)strlen(ex)), ex);
    char *sl = strrchr(a.exe, '/');
    if (sl) memmove(a.exe, sl + 1, strlen(sl));
    a.cat = category(cats);
    apps = realloc(apps, (napps + 1) * sizeof *apps);
    apps[napps++] = a;
}

static int app_cmp(const void *a, const void *b) { return strcasecmp(((App *)a)->name, ((App *)b)->name); }

/* the XDG application dirs, most important first: $XDG_DATA_HOME, then each
 * of $XDG_DATA_DIRS (flatpak's exports are there: , Chromium and
 * NewsFlash were missing from the drawer) */
#define MAXAPPDIRS 12
static int app_dirs(char dirs[MAXAPPDIRS][512])
{
    int n = 0;
    const char *home = getenv("HOME") ? getenv("HOME") : "";
    const char *dh = getenv("XDG_DATA_HOME");
    if (dh && *dh) snprintf(dirs[n++], 512, "%s/applications", dh);
    else snprintf(dirs[n++], 512, "%s/.local/share/applications", home);
    const char *dd = getenv("XDG_DATA_DIRS");
    if (!dd || !*dd) dd = "/usr/local/share:/usr/share";
    while (*dd && n < MAXAPPDIRS) {
        const char *e = strchr(dd, ':');
        int len = e ? (int)(e - dd) : (int)strlen(dd);
        if (len > 0) snprintf(dirs[n++], 512, "%.*s/applications", len, dd);
        dd += len + (e ? 1 : 0);
    }
    return n;
}

/* the dirs' mtimes: an install or removal changes one, the list is re-read;
 * so does the favourites file (glassine-settings > Apps > an app > Favourite) */
static long long apps_sig(char dirs[MAXAPPDIRS][512], int n)
{
    long long sig = 0;
    struct stat st;
    char fp[512];
    for (int d = 0; d < n; d++)
        if (!stat(dirs[d], &st)) sig = sig * 31 + (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
    state_path(fp, sizeof fp, ".config", "favourites");
    if (!stat(fp, &st)) sig = sig * 31 + (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
    return sig;
}

static void load_apps(void)
{
    static long long sig;
    static time_t checked;
    char dirs[MAXAPPDIRS][512];
    int ndirs = app_dirs(dirs);
    if (!apps_dirty && time(NULL) != checked) {     /* at most one stat round a second */
        checked = time(NULL);
        if (apps_sig(dirs, ndirs) != sig) apps_dirty = 1;
    }
    if (!apps_dirty) return;
    apps_dirty = 0;
    sig = apps_sig(dirs, ndirs);
    free(apps); apps = NULL; napps = 0;
    for (int d = 0; d < ndirs; d++) {
        DIR *dd = opendir(dirs[d]);
        if (!dd) continue;
        struct dirent *e;
        while ((e = readdir(dd)))
            if (strlen(e->d_name) > 8 && !strcmp(e->d_name + strlen(e->d_name) - 8, ".desktop")) load_app(dirs[d], e->d_name);
        closedir(dd);
    }
    qsort(apps, napps, sizeof *apps, app_cmp);
    char fp[512];
    state_path(fp, sizeof fp, ".config", "favourites");
    /* without the file: the apps the image itself ships */
    static const char *const DEF_FAV[] = { "glassine-settings", "foot", NULL };
    for (int i = 0; i < napps; i++) {
        int r = in_list_file(fp, apps[i].id);
        if (r < 0) { r = 0; for (int k = 0; DEF_FAV[k]; k++) if (!strcmp(DEF_FAV[k], apps[i].id)) r = 1; }
        apps[i].fav = r;
    }
    LOG("%d apps", napps);
}

/* the app of a window: its app_id or class/instance (XWayland) against StartupWMClass,
 * the desktop id (or its last dotted part), the executable (as glassineui.Apps) */
App *app_match(const char *cls, const char *inst)
{
    const char *keys[2] = { cls, inst };
    App *r = NULL;
    for (int pass = 0; pass < 3 && !r; pass++)
        for (int k = 0; k < 2 && !r; k++) {
            if (!keys[k][0]) continue;
            for (int i = 0; i < napps && !r; i++) {
                const char *id = apps[i].id, *dot = strrchr(id, '.');
                if ((pass == 0 && apps[i].wmclass[0] && !strcasecmp(apps[i].wmclass, keys[k])) ||
                    (pass == 1 && (!strcasecmp(id, keys[k]) || (dot && !strcasecmp(dot + 1, keys[k])))) ||
                    (pass == 2 && !strcasecmp(apps[i].exe, keys[k])))
                    r = &apps[i];
            }
        }
    return r;
}

App *app_of(WinId w)
{
    load_apps();
    return be_app_of(w);
}

/* ---- windows ---------------------------------------------------------------- */
Client cl[MAXCL];
int ncl;
long ndesk = 1, curdesk;
WinId active;
int warea[4];

static Client *client(WinId w)
{
    for (int i = 0; i < ncl; i++) if (cl[i].w == w) return &cl[i];
    return NULL;
}

static void tile_cmd(const char *fmt, ...)
{
    char c[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c, sizeof c, fmt, ap);
    va_end(ap);
    be_tile(c);
    LOG("tile: %s", c);
}

/* ============================================================================
 * system: processes, settings
 * ========================================================================== */
typedef void (*JobDone)(const char *out);
static struct job { pid_t pid; int fd; char buf[1024]; int len; JobDone done; } jobs[8];

/* run `cmd` with sh; its output goes to done when it exits (NULL: detached) */
static void run(const char *cmd, JobDone done)
{
    LOG("run: %s", cmd);
    if (!done) {
        pid_t p = fork();
        if (p == 0) {
            setsid();
            if (fork() == 0) {
                int fd = open("/dev/null", O_RDWR);
                dup2(fd, 0); dup2(fd, 1);
                for (int i = 3; i < 256; i++) close(i);
                execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
                _exit(127);
            }
            _exit(0);
        }
        if (p > 0) waitpid(p, NULL, 0);
        return;
    }
    struct job *j = NULL;
    for (int i = 0; i < 8; i++) if (!jobs[i].pid) { j = &jobs[i]; break; }
    if (!j) return;
    int pf[2];
    if (pipe2(pf, O_CLOEXEC)) return;
    pid_t p = fork();
    if (p == 0) {
        dup2(pf[1], 1);
        int fd = open("/dev/null", O_RDWR);
        dup2(fd, 0); dup2(fd, 2);
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
    close(pf[1]);
    if (p < 0) { close(pf[0]); return; }
    *j = (struct job){ .pid = p, .fd = pf[0], .done = done };
}

static int read_int(const char *path, int dflt)
{
    FILE *f = fopen(path, "r");
    if (!f) return dflt;
    int v = dflt;
    if (fscanf(f, "%d", &v) != 1) v = dflt;
    fclose(f);
    return v;
}

static void write_int(const char *path, int v)
{
    FILE *f = fopen(path, "w");
    if (!f) { LOG("%s: %s", path, strerror(errno)); return; }
    fprintf(f, "%d\n", v);
    fclose(f);
}

/* key=value files edited in place (/etc/glassine is root's, the files group-writable) */
static void conf_set(const char *path, const char *key, const char *val)
{
    char buf[8192] = "", l[512];
    int found = 0;
    size_t kl = strlen(key), n = 0;
    FILE *f = fopen(path, "r");
    while (f && fgets(l, sizeof l, f)) {
        if (!strncmp(l, key, kl) && l[kl] == '=') { snprintf(l, sizeof l, "%s=%s\n", key, val); found = 1; }
        n += snprintf(buf + n, sizeof buf - n, "%s", l);
        if (n >= sizeof buf) return;
    }
    if (f) fclose(f);
    if (!found) n += snprintf(buf + n, sizeof buf - n, "%s=%s\n", key, val);
    f = fopen(path, "w");
    if (!f) { LOG("%s: %s", path, strerror(errno)); return; }
    fputs(buf, f);
    fclose(f);
}

static int conf_get(const char *path, const char *key, int dflt)
{
    FILE *f = fopen(path, "r");
    if (!f) return dflt;
    char l[512];
    size_t kl = strlen(key);
    int v = dflt;
    while (fgets(l, sizeof l, f)) if (!strncmp(l, key, kl) && l[kl] == '=') v = atoi(l + kl + 1);
    fclose(f);
    return v;
}

#define POWER_CONF "/etc/glassine/power.conf"
#define EINK_CONF "/etc/glassine/eink.conf"
#define EPDD_CTL "/run/glassine-epdd-desktop.ctl"
#define BL "/sys/class/backlight/onyx_bl_%s/%s"
#define FL "/sys/class/backlight/frontlight-%s/brightness"
#define LIGHT_RUN "/run/glassine-light"      /* glassine-powerd: the managed light's schedule (b=, w= in %) */

static struct {
    int view;
    int wifi, bt, bt_ok, mute, vol, vol_ok, wifi_busy, bt_busy;
    int light, warm, br_max, ct_max;    /* light/warm: 0..100 as shown */
    int managed, sched[2], off[2];      /* managed light: glassine-powerd's schedule now (%), the user's offsets */
    int glow;                           /* managed light: brightness >= 5 % ("Keep a night glow") */
    int rlo, rhi;                       /* e-ink fluidity: glassine-epdd-desktop's mmin/mmax (LV_* indices) */
    int rot_auto, rot_lock, rot_cur;    /* rotation: auto?, the locked turn, the turn now (glassine-rotd) */
    int opening; double opening_t;      /* a held toggle: "Opening ... settings" */
    WinId mv;                           /* the overview: the app being moved (0: none) */
    int ov_page;                        /* its page (-1: the current screen's, or the moving app's) */
    int focus;                          /* keyboard: the popup's n-th target has the ring (-1: none) */
    pid_t chg_pid;                      /* a system change runs (shell.change-running): who said so */
    char chg_label[16];                 /* its step count */
    int home_vis, hole;                 /* Home is shown; the empty screen under it (its number, 0: none) */
    /* divider: level 0 the main line, 1/2 the inner line of half 0/1; pos its place as a fraction */
    struct { int x, y, w, h, vert, level; WinId a, b; double pos; } dv[4];   /* the screen's dividers (device px) */
    int ndv, dv_n, dv_o;                /* how many; the screen's windows; its main split side by side */
    /* dragging */
    int drag, dr_vert, dr_pos, dr_a0, dr_alen, dr_c0, dr_clen;
    /* bar */
    int batt, charging, wifi_lvl;
    char clock[12];
} st = { .opening = -1, .ov_page = -1, .focus = -1 };
enum { V_IDLE, V_QS, V_WS, V_NOTIF, V_DIV, V_PWR };

#define EPDD_CTL "/run/glassine-epdd-desktop.ctl"

static void light_hw(int br, int ct)
{
    char p[128];
    snprintf(p, sizeof p, FL, "a");
    if (!access(p, F_OK)) {             /* this device's banks, until they come from per-device config */
        if (ct < 0) ct = 0;
        if (ct > 24) ct = 24;
        int total = br <= 0 ? 0 : br >= 28 ? 255 : 1 + 254 * br * br / (28 * 28);
        char q[128];
        snprintf(q, sizeof q, FL, "b");
        write_int(p, total * ct / 24);         /* a: warm */
        write_int(q, total * (24 - ct) / 24);  /* b: cool */
    } else {
        snprintf(p, sizeof p, BL, "ct", "brightness"); write_int(p, ct);
        snprintf(p, sizeof p, BL, "br", "brightness"); write_int(p, br);
    }
}

static void set_light(int br, int ct)
{
    light_hw(br, ct);
    char v[16];
    snprintf(v, sizeof v, "%d", br); conf_set(POWER_CONF, "light_br", v);
    snprintf(v, sizeof v, "%d", ct); conf_set(POWER_CONF, "light_ct", v);
}

/* managed: what is on = the schedule + the offset (as glassine-powerd's light) */
static void light_managed_now(void)
{
    int *dst[2] = { &st.light, &st.warm };
    for (int i = 0; i < 2; i++) {
        int v = st.sched[i] + st.off[i];
        *dst[i] = v < 0 ? 0 : v > 100 ? 100 : v;
        if (i == 0 && st.glow && *dst[i] < 5) *dst[i] = 5;
    }
}

/* e-ink fluidity ("nonthreatening choices of
 * fluidity": the frames per update a motion run gets), as
 * glassine-settings' E-ink page: four steps, each glassine-epdd-desktop's mmax (the run's
 * paint; mmin=gc16, curve=off, still=range: stills keep the sized choice).
 * Calm: no motion paint at all (every change a GC16) */
static const char *const LV_NAME[7] = { "gc16", "du4", "a2", "a2_8", "a2_6", "a2_4", "a2_2" };
static const struct { const char *name, *ic; int lv; } FLUID[4] = {
    { "Fluid", "power-profile-performance-symbolic", 6 }, { "Balanced", "power-profile-balanced-symbolic", 5 },
    { "Clear", "view-reveal-symbolic", 3 }, { "Calm", "weather-clear-night-symbolic", 0 } };
/* the step a run's fast end shows as (older eink.confs: any A2 step) */
static int fluid_of(int hi) { return hi >= 6 ? 0 : hi >= 4 ? 1 : hi >= 2 ? 2 : 3; }

static void eink_range(int *lo, int *hi)
{
    FILE *f = fopen(EINK_CONF, "r");
    char l[512];
    *lo = 0; *hi = 6;                   /* glassine-epdd-desktop's default: GC16 .. A2/2 (Fluid) */
    while (f && fgets(l, sizeof l, f)) {
        int mx = !strncmp(l, "@mmax=", 6);
        if (!mx && strncmp(l, "@mmin=", 6)) continue;
        size_t n = strcspn(l + 6, " \t\n");
        for (int i = 0; i < 7; i++)
            if (strlen(LV_NAME[i]) == n && !strncmp(l + 6, LV_NAME[i], n)) *(mx ? hi : lo) = i;
    }
    if (f) fclose(f);
    if (*lo > *hi) { int t = *lo; *lo = *hi; *hi = t; }
}

/* one write (glassine-epdd-desktop re-reads eink.conf when it changes). Like glassine-settings'
 * Conf.edit: an exclusive flock from the read to the write (every eink.conf
 * writer takes it), never truncated first (blocks reserved, new bytes over the
 * old, then the length cut), so a reader never sees it empty and a full disk
 * fails before anything changes; an unreadable or empty existing file is left
 * alone (a truncated eink.conf lost its globals). */
static void set_fluid(int k)
{
    const char *k5[5] = { "@motion", "@still", "@curve", "@mmin", "@mmax" },
               *v[5] = { "adaptive", "range", "off", "gc16", LV_NAME[FLUID[k].lv] };
    char old[16384], buf[16384], add[256] = "";
    int found[5] = { 0 };
    size_t n = 0, an = 0;
    ssize_t r, on = 0;
    int fd = open(EINK_CONF, O_RDWR | O_CLOEXEC);
    if (fd < 0) { LOG(EINK_CONF ": %s", strerror(errno)); return; }
    if (flock(fd, LOCK_EX) < 0) { LOG(EINK_CONF ": flock: %s", strerror(errno)); close(fd); return; }
    while ((r = read(fd, old + on, sizeof old - 1 - (size_t)on)) > 0) on += r;
    if (r < 0 || on == 0 || on >= (ssize_t)sizeof old - 1) {
        LOG(EINK_CONF ": %s, not editing it", r < 0 ? strerror(errno) : on ? "too long" : "empty");
        close(fd); return;
    }
    old[on] = 0;
    for (char *l = old, *e; *l; l = e) {
        e = strchr(l, '\n'); e = e ? e + 1 : l + strlen(l);
        char line[512];
        size_t ln = (size_t)(e - l);
        int rep = 0;
        for (int i = 0; i < 5; i++)
            if (!strncmp(l, k5[i], strlen(k5[i])) && l[strlen(k5[i])] == '=') {
                snprintf(line, sizeof line, "%s=%s\n", k5[i], v[i]); found[i] = rep = 1;
            }
        if (n + (rep ? strlen(line) : ln) + 1 >= sizeof buf - sizeof add) { close(fd); return; }
        if (rep) n += (size_t)snprintf(buf + n, sizeof buf - n, "%s", line);
        else { memcpy(buf + n, l, ln); n += ln; }
    }
    for (int i = 0; i < 5; i++) if (!found[i]) an += (size_t)snprintf(add + an, sizeof add - an, "%s=%s\n", k5[i], v[i]);
    /* the "@" lines go before the profiles */
    memmove(buf + an, buf, n); memcpy(buf, add, an); n += an;
    int e = n > (size_t)on ? posix_fallocate(fd, 0, (off_t)n) : 0;
    if (e) { LOG(EINK_CONF ": %s, not changed", strerror(e)); close(fd); return; }
    if (pwrite(fd, buf, n, 0) != (ssize_t)n || ftruncate(fd, (off_t)n) < 0)
        LOG(EINK_CONF ": write: %s", strerror(errno));
    close(fd);
    st.rlo = 0; st.rhi = FLUID[k].lv;
}

static struct { double x, w; } slider_geo[3];     /* the quick-settings sliders as drawn (view_qs) */

void epdd(const char *cmd)
{
    int fd = open(EPDD_CTL, O_WRONLY | O_NONBLOCK);
    if (fd < 0) { LOG(EPDD_CTL ": %s", strerror(errno)); return; }
    if (write(fd, cmd, strlen(cmd)) < 0) LOG(EPDD_CTL ": %s", strerror(errno));
    close(fd);
}

static char bt_path[128];

static int bt_on(void)
{
    if (!bt_path[0]) {
        DIR *d = opendir("/sys/class/rfkill");
        struct dirent *e;
        while (d && (e = readdir(d))) {
            char p[300], nm[64] = "";
            snprintf(p, sizeof p, "/sys/class/rfkill/%s/name", e->d_name);
            FILE *f = fopen(p, "r");
            if (!f) continue;
            if (fgets(nm, sizeof nm, f)) nm[strcspn(nm, "\n")] = 0;
            fclose(f);
            if (!strcmp(nm, "bt_power") || !strncmp(nm, "hci", 3))
                snprintf(bt_path, sizeof bt_path, "/sys/class/rfkill/%s/soft", e->d_name);
        }
        if (d) closedir(d);
    }
    st.bt_ok = bt_path[0] != 0;
    return st.bt_ok && read_int(bt_path, 1) == 0;
}

static void render_all(void);
static void render_pop(void);

/* the status answers repaint only what changed (opening quick settings
 * rendered the card three times, each a new round of damage for glassine-epdd-desktop) */
static void wifi_done(const char *o)
{
    int w = strstr(o, "enabled") != NULL, ch = w != st.wifi || st.wifi_busy;
    st.wifi = w;
    st.wifi_busy = 0;
    if (ch) render_pop();
}
static void vol_done(const char *o)
{
    int ok = st.vol_ok, vol = st.vol, mute = st.mute;
    const char *p = strchr(o, '%');
    st.vol_ok = p != NULL;
    if (p) { while (p > o && isdigit((unsigned char)p[-1])) p--; st.vol = atoi(p); }
    st.mute = strstr(o, "Mute: yes") != NULL;
    if (ok != st.vol_ok || vol != st.vol || mute != st.mute) render_pop();
}
static void query_wifi(void) { run("nmcli radio wifi", wifi_done); }
static void query_vol(void) { run("pactl get-sink-volume @DEFAULT_SINK@; pactl get-sink-mute @DEFAULT_SINK@", vol_done); }
static void bt_done(const char *o) { (void)o; st.bt_busy = 0; st.bt = bt_on(); render_pop(); }
static void wifi_set_done(const char *o) { (void)o; query_wifi(); }
static void vol_set_done(const char *o) { (void)o; query_vol(); }

static void tz_follow(void);

static void read_status(void)
{
    if (st.chg_pid && kill(st.chg_pid, 0) < 0 && errno == ESRCH) { st.chg_pid = 0; st.chg_label[0] = 0; }
    /* battery */
    DIR *d = opendir("/sys/class/power_supply");
    struct dirent *e;
    st.batt = -1;
    while (d && (e = readdir(d))) {
        char p[300], t[32] = "";
        snprintf(p, sizeof p, "/sys/class/power_supply/%s/type", e->d_name);
        FILE *f = fopen(p, "r");
        if (!f) continue;
        if (fgets(t, sizeof t, f)) t[strcspn(t, "\n")] = 0;
        fclose(f);
        if (strcmp(t, "Battery")) continue;
        snprintf(p, sizeof p, "/sys/class/power_supply/%s/capacity", e->d_name);
        st.batt = read_int(p, -1);
        snprintf(p, sizeof p, "/sys/class/power_supply/%s/status", e->d_name);
        f = fopen(p, "r");
        st.charging = 0;
        if (f) { if (fgets(t, sizeof t, f)) st.charging = !strncmp(t, "Charging", 8) || !strncmp(t, "Full", 4); fclose(f); }
        break;
    }
    if (d) closedir(d);
    /* Wi-Fi: link quality of the first wireless interface, -1 = none */
    st.wifi_lvl = -1;
    FILE *f = fopen("/proc/net/wireless", "r");
    char l[256];
    for (int i = 0; f && fgets(l, sizeof l, f); i++) {
        float q;
        char ifn[32];
        if (i < 2 || sscanf(l, " %31[^:]: %*x %f", ifn, &q) != 2) continue;
        char op[64] = "", p[128];
        snprintf(p, sizeof p, "/sys/class/net/%s/operstate", ifn);
        FILE *o = fopen(p, "r");
        if (o) { if (fgets(op, sizeof op, o)) op[strcspn(op, "\n")] = 0; fclose(o); }
        if (strcmp(op, "up")) continue;
        st.wifi_lvl = q >= 50 ? 3 : q >= 35 ? 2 : q >= 20 ? 1 : 0;
        break;
    }
    if (f) fclose(f);
    /* mainline has no /proc/net/wireless: an up wireless interface = full bars */
    if (st.wifi_lvl < 0 && (d = opendir("/sys/class/net"))) {
        while ((e = readdir(d))) {
            char p[300], op[16] = "";
            snprintf(p, sizeof p, "/sys/class/net/%s/phy80211", e->d_name);
            if (access(p, F_OK)) continue;
            snprintf(p, sizeof p, "/sys/class/net/%s/operstate", e->d_name);
            FILE *o = fopen(p, "r");
            if (o) { if (fgets(op, sizeof op, o)) op[strcspn(op, "\n")] = 0; fclose(o); }
            if (!strcmp(op, "up")) { st.wifi_lvl = 3; break; }
        }
        closedir(d);
    }
    tz_follow();
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char c12[512];
    snprintf(c12, sizeof c12, "%s/.config/glassine-shell/clock12", getenv("HOME") ? getenv("HOME") : "");
    if (access(c12, F_OK)) {                    /* 24 h, unless glassine-settings → Date & time says 12 */
        strftime(st.clock, sizeof st.clock, "%H:%M", tm);
    } else {                                    /* "8:11 pm", as the settings page shows it */
        snprintf(st.clock, sizeof st.clock, "%d:%02d %s", (tm->tm_hour + 11) % 12 + 1, tm->tm_min,
                 tm->tm_hour < 12 ? "am" : "pm");
    }
}

/* musl reads /etc/localtime once and keeps it while TZ stays the same string: a
 * time zone set later (a relinked /etc/localtime) reaches the clock only by
 * changing TZ -- ":/etc/localtime" and "/etc/localtime" name the same file.
 * Called with every status read (30 s, and at each minute) */
static void tz_follow(void)
{
    static struct stat seen;
    static int init, fixed, flip;
    struct stat a, l;
    if (!init) fixed = getenv("TZ") != NULL;                /* a TZ from the session: its choice */
    if (fixed) return;
    if (stat("/etc/localtime", &a)) memset(&a, 0, sizeof a);
    if (lstat("/etc/localtime", &l)) memset(&l, 0, sizeof l);
    a.st_ctime ^= l.st_ctime; a.st_mtime ^= l.st_mtime;     /* the link itself changes too */
    if (init && (a.st_ino != seen.st_ino || a.st_dev != seen.st_dev || a.st_mtime != seen.st_mtime || a.st_ctime != seen.st_ctime)) {
        flip = !flip;
        setenv("TZ", flip ? ":/etc/localtime" : "/etc/localtime", 1);
        tzset();
        LOG("time zone changed: re-read /etc/localtime");
    }
    seen = a;
    init = 1;
}

/* ---- theme ------------------------------------------------------------------- */
static void theme_publish(void)
{
    be_theme_publish(T->name);
}

static void theme_set(const char *name, int save)
{
    T = ui_voice_find(name);
    if (save) {
        char p[512];
        state_path(p, sizeof p, ".config", "theme");
        FILE *f = fopen(p, "w");
        if (f) { fprintf(f, "%s\n", T->name); fclose(f); }
    }
    theme_publish();
    LOG("theme %s", T->name);
    /* the desktop follows: GTK3/GTK4 styles, UI font (glassine-theme-apply) */
    char cmd[128];
    snprintf(cmd, sizeof cmd, "/usr/bin/glassine-theme-apply %s", T->name);
    run(cmd, NULL);
}

/* ============================================================================
 * views (design px, screen coordinates)
 * ========================================================================== */
/* the overview's icon (Material's filter_N): two
 * overlapping sheets, num (may be "") printed on the front one */
#define GLASSINE_ICON_SHEETS "glassine:sheets"
static void sheets_icon(Gc *g, double cx, double cy, double px, const char *num, col c)
{
    cairo_t *cr = g->cr;
    double u = px / 24, x0 = cx - 12 * u, y0 = cy - 12 * u;
    cairo_save(cr);
    cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
    cairo_set_line_width(cr, 2 * u);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    cairo_rectangle(cr, x0 + 8 * u, y0 + 3 * u, 13 * u, 13 * u);   /* the front sheet */
    cairo_move_to(cr, x0 + 4 * u, y0 + 7 * u);                      /* the back one, under it */
    cairo_line_to(cr, x0 + 4 * u, y0 + 20 * u);
    cairo_line_to(cr, x0 + 17 * u, y0 + 20 * u);
    cairo_stroke(cr);
    cairo_restore(cr);
    if (num && *num) {
        TFont nf = { "IBM Plex Mono", 600, 0, (float)((strlen(num) > 1 ? 8.5 : 10) * u) };
        double th;
        ui_text_w(g, &nf, num, &th);
        ui_text(g, &nf, num, x0 + 14.5 * u, y0 + 9.5 * u - th / 2, 1, 0, c, NULL);
    }
}

#define WS_MAX_APPS 4                   /* glassine-swayd's APPS_PER_WS */
#define IL_HL RGB(0xcccccc)             /* the scheme's highlight: the move's arrows */

/* ---- a screen's windows (spec 001): its groups from the tree (Layout) as
 * rectangles in fractions of the screen, every split even: what the bar's
 * reading order, the overview's slots and the move's arrows go by (not the
 * lines' positions) ---- */
typedef struct { const Client *c; WinId w; int gi; double x, y, wd, ht; } Rc;

static int lay_n(const Layout *L)
{
    int n = 0;
    for (int g = 0; g < L->ng; g++) n += L->gn[g];
    return n;
}

/* screen d's layout, windows the shell doesn't know left out */
static Layout lay_get(long d)
{
    Layout L = { 0 };
    if (d < 0 || d >= MAXDESK || d >= ndesk) return L;
    const Layout *s = &lay[d];
    L.side = s->side;
    for (int g = 0; g < s->ng; g++) {
        int k = 0;
        for (int j = 0; j < s->gn[g]; j++) if (client(s->w[g][j])) L.w[L.ng][k++] = s->w[g][j];
        if (k) L.gn[L.ng++] = k;
    }
    if (s->full && client(s->full)) L.full = s->full;
    return L;
}

static int lay_rects(const Layout *L, Rc *out)
{
    int n = 0;
    for (int g = 0; g < L->ng; g++)
        for (int p = 0; p < L->gn[g]; p++) {
            double a = (double)g / L->ng, b = 1.0 / L->ng, c = (double)p / L->gn[g], e = 1.0 / L->gn[g];
            Rc *r = &out[n++];
            r->w = L->w[g][p]; r->c = client(r->w); r->gi = g;
            if (L->side) { r->x = a; r->wd = b; r->y = c; r->ht = e; }
            else { r->x = c; r->wd = e; r->y = a; r->ht = b; }
        }
    if (L->full) {                      /* out of the tiling while full: the whole screen, gi -1 */
        Rc *r = &out[n++];
        r->w = L->full; r->c = client(r->w); r->gi = -1;
        r->x = r->y = 0; r->wd = r->ht = 1;
    }
    return n;
}

/* screen d's windows as rectangles (at most 16), in the tree's order or, with
 * reading, by rows top to bottom, then left to right */
static int scr_rects(long d, Rc *out, int reading)
{
    Layout L = lay_get(d);
    int n = lay_rects(&L, out);
    for (int a = 1; reading && a < n; a++)
        for (int b = a; b > 0 && (out[b].y < out[b - 1].y - 1e-6 ||
                                  (fabs(out[b].y - out[b - 1].y) < 1e-6 && out[b].x < out[b - 1].x)); b--) {
            Rc t = out[b]; out[b] = out[b - 1]; out[b - 1] = t;
        }
    return n;
}

/* the screen without window m (FR-018): a split half that loses a window
 * becomes whole again; a half left alone splits the other way */
static Layout lay_without(const Layout *L, WinId m)
{
    Layout o = { .side = L->side, .full = L->full == m ? 0 : L->full };
    for (int g = 0; g < L->ng; g++) {
        int k = 0;
        for (int j = 0; j < L->gn[g]; j++) if (L->w[g][j] != m) o.w[o.ng][k++] = L->w[g][j];
        if (k) o.gn[o.ng++] = k;
    }
    if (o.ng == 1 && o.gn[0] == 2) {
        Layout f = { .side = !o.side, .ng = 2, .gn = { 1, 1 }, .full = o.full };
        f.w[0][0] = o.w[0][0]; f.w[1][0] = o.w[0][1];
        return f;
    }
    return o;
}

static int lay_same(const Layout *a, const Layout *b)
{
    if (a->ng != b->ng || (lay_n(a) > 1 && a->side != b->side)) return 0;
    for (int g = 0; g < a->ng; g++) {
        if (a->gn[g] != b->gn[g]) return 0;
        for (int j = 0; j < a->gn[g]; j++) if (a->w[g][j] != b->w[g][j]) return 0;
    }
    return 1;
}

/* the window shown full screen on its screen over the others (FR-012), NULL: none */
static const Client *scr_full(const Rc *R, int n)
{
    for (int i = 0; n > 1 && i < n; i++) if (R[i].c && (R[i].c->maxed || R[i].gi < 0)) return R[i].c;
    return NULL;
}

static void app_icon(Gc *g, const Client *c, double px, double cx, double cy, col fg)
{
    if (!(c && c->icon[0] && ui_icon(g, c->icon, px, cx, cy, fg)))
        ui_icon(g, "application-x-executable-symbolic", px, cx, cy, fg);
}

/* the bar's small screen: a frame with a thick top edge (1.5 px sides, 5 px top, radius 4) */
static void screen_frame(Gc *g, double x, double y, double w, double h, col c)
{
    if (!g->paint) return;
    cairo_t *cr = g->cr;
    const double r = 4, sw = 1.5, top = 5, ir = r - sw;
    cairo_save(cr);
    cairo_new_path(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
    cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
    double ix = x + sw, iy = y + top, iw = w - 2 * sw, ih = h - top - sw;
    cairo_new_sub_path(cr);
    cairo_move_to(cr, ix, iy);
    cairo_line_to(cr, ix + iw, iy);
    cairo_arc(cr, ix + iw - ir, iy + ih - ir, ir, 0, M_PI / 2);
    cairo_arc(cr, ix + ir, iy + ih - ir, ir, M_PI / 2, M_PI);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, (c >> 24) / 255.0);
    cairo_fill(cr);
    cairo_restore(cr);
}

static const Box *plate(Gc *g, double x, double y, double w, double h, int on, col *fg)
{
    const Box *b = on ? &T->plateOn : &T->plateOff;
    ui_box(g, x, y, w, h, b);
    *fg = on ? T->plateOnFg : T->barFg;
    return b;
}

static const char *wifi_icon(void)
{
    static const char *const L[4] = { "network-wireless-signal-weak-symbolic", "network-wireless-signal-ok-symbolic",
                                      "network-wireless-signal-good-symbolic", "network-wireless-signal-excellent-symbolic" };
    return st.wifi_lvl < 0 ? "network-wireless-offline-symbolic" : L[st.wifi_lvl];
}

static void view_bar(Gc *g)
{
    double H = BH, cy = H / 2;
    bound(0, 0, LW, H + T->barShadowH, 0);
    ui_fill_rect(g, 0, 0, LW, H, T->barBg);
    if (T->barShadowH) ui_fill_rect(g, 0, H, LW, T->barShadowH, T->barShadowC);
    if (T->barDouble) { ui_hline(g, 0, H - 3, LW, 1, T->barBorderC, 0); ui_hline(g, 0, H - 1, LW, 1, T->barBorderC, 0); }
    else if (T->barBorderW) ui_hline(g, 0, H - T->barBorderW, LW, T->barBorderW, T->barBorderC, 0);
    if (T->stitch) {
        ui_hline(g, 6, 5 - .75, LW - 12, 1.5, T->stitchColor, 1);
        ui_hline(g, 6, H - 5 - .75, LW - 12, 1.5, T->stitchColor, 1);
    }
    col fg;
    double x = 10;
    /* Home (spec 001 FR-001): pressed while Home is shown */
    plate(g, x, cy - 18, 42, 36, st.home_vis, &fg);
    ui_icon(g, "go-home-symbolic", 24, x + 21, cy, fg);
    hit(x, 0, 42, H, A_BAR_HOME, 0, 0);
    x += 42 + 6;

    double xr = LW - 10;
    xr -= 42;
    plate(g, xr, cy - 18, 42, 36, st.view == V_QS, &fg);
    ui_icon(g, "preferences-system-symbolic", 22, xr + 21, cy, fg);
    hit(xr, 0, 42, H, A_BAR_QS, 0, 0);
    xr -= 6 + 38;
    ui_icon(g, "view-refresh-symbolic", 22, xr + 19, cy, T->barFg);
    hit(xr, 0, 38, H, A_BAR_REFRESH, 0, 0);
    xr -= 2 + 38;
    ui_icon(g, "input-keyboard-symbolic", 22, xr + 19, cy, T->barFg);
    hit(xr, 0, 38, H, A_BAR_KBD, 0, 0);
    xr -= 6;
    if (st.batt >= 0) {
        char pct[8];
        snprintf(pct, sizeof pct, "%d%%", st.batt);
        double th, tw = ui_text_w(g, &T->numFont, pct, &th);
        xr -= tw;
        ui_text(g, &T->numFont, pct, xr, cy - th / 2, 0, 0, T->barFg, NULL);
        xr -= 5 + 26;
        if (g->paint) {
            ui_stroke_rrect(g, xr, cy - 6, 24, 12, 2, 1.5, T->barFg, 0);
            ui_fill_rect(g, xr + 3, cy - 3, 18 * st.batt / 100.0, 6, T->barFg);
            ui_fill_rect(g, xr + 24, cy - 2.5, 2, 5, T->barFg);
            if (st.charging) {          /* a bolt over the cell, cut out of the fill */
                cairo_t *cr = g->cr;
                cairo_move_to(cr, xr + 13, cy - 7); cairo_line_to(cr, xr + 8, cy + 1); cairo_line_to(cr, xr + 12, cy + 1);
                cairo_line_to(cr, xr + 11, cy + 7); cairo_line_to(cr, xr + 16, cy - 1); cairo_line_to(cr, xr + 12, cy - 1);
                cairo_close_path(cr);
                cairo_set_source_rgb(cr, ((T->barBg >> 16) & 255) / 255.0, ((T->barBg >> 8) & 255) / 255.0, (T->barBg & 255) / 255.0);
                cairo_set_line_width(cr, 1.5);
                cairo_stroke_preserve(cr);
                cairo_set_source_rgb(cr, ((T->barFg >> 16) & 255) / 255.0, ((T->barFg >> 8) & 255) / 255.0, (T->barFg & 255) / 255.0);
                cairo_fill(cr);
            }
        }
        xr -= 6;
    }
    xr -= 24;
    ui_icon(g, wifi_icon(), 20, xr + 12, cy, T->barFg);
    xr -= 6;
    /* a system change runs (shell.change-running): its step count, in a dashed
     * outline; the tap opens Packages */
    if (st.chg_label[0]) {
        TFont cf = { "IBM Plex Mono", 500, 0, 12 };
        double ch, cw = ui_text_w(g, &cf, st.chg_label, &ch), w = 9 + 20 + 6 + cw + 9;
        xr -= w;
        ui_stroke_rrect(g, xr + .75, cy - 18 + .75, w - 1.5, 34.5, T->plateOff.r[0], 1.5, T->barFg, 1);
        ui_icon(g, "package-x-generic-symbolic", 20, xr + 9 + 10, cy, T->barFg);
        ui_text(g, &cf, st.chg_label, xr + 9 + 20 + 6, cy - ch / 2, 0, 0, T->barFg, NULL);
        hit(xr, 0, w, H, A_BAR_CHG, 0, 0);
        xr -= 6;
    }
    /* the clock + the notification counter: a plate, the notices open from it */
    double th, tw = ui_text_w(g, &T->numFont, st.clock, &th);
    char cnt[8] = "";
    TFont bf = { "IBM Plex Mono", 600, 0, 11 };   /* the design: the counter is Plex Mono in every theme */
    double bw = 0, bh;
    if (nnotes) {
        snprintf(cnt, sizeof cnt, nnotes > 99 ? "99+" : "%d", nnotes);
        bw = ui_text_w(g, &bf, cnt, &bh) + 8;
        if (bw < 18) bw = 18;
    }
    double nw = 8 + tw + (nnotes ? 6 + bw : 0) + 8;
    xr -= nw;
    plate(g, xr, cy - 18, nw, 36, st.view == V_NOTIF, &fg);
    ui_text(g, &T->numFont, st.clock, xr + 8, cy - th / 2, 0, 0, fg, NULL);
    if (nnotes) {
        /* inverted pill: the plate's ink, cut out in the bar's (or the on plate's) ground */
        int on = st.view == V_NOTIF;
        col cut = on ? (T->plateOn.bg ? T->plateOn.bg : T->screenBg) : T->barBg;
        double px = xr + 8 + tw + 6;
        ui_rrect(g, px, cy - 9, bw, 18, 9, fg);
        ui_text(g, &bf, cnt, px + bw / 2, cy - bh / 2, 1, 0, cut, NULL);
    }
    hit(xr, 0, nw, H, A_BAR_NOTIF, 0, 0);
    xr -= 6;

    /* the screen button (FR-002..004): "n/N" in a small screen frame with a thick
     * top edge, then its apps' icons in reading order; with an empty screen
     * under Home its number only. It names the screen, not the focused window,
     * and opens Overview (pressed while it is open). */
    double pw = xr - x;
    if (pw > 40) {
        plate(g, x, cy - 18, pw, 36, st.view == V_WS, &fg);
        int hole = st.home_vis && st.hole > 0;
        long num = hole ? st.hole : curdesk + 1, cnt = ndesk > num ? ndesk : num;
        Rc R[16];
        int n = hole ? 0 : scr_rects(curdesk, R, 1);
        char lab[24];
        snprintf(lab, sizeof lab, "%ld/%ld", num, cnt);
        TFont nf = ui_font_px(&T->numFont, 13);
        double nh, nw = ui_text_w(g, &nf, lab, &nh), fw = 6 + nw + 6 + 3, FH = 26;
        double tot = fw + (n ? 8 + n * 20 + (n - 1) * 5 : 0);
        double sx = x + (pw - tot) / 2;
        if (sx < x + 10) sx = x + 10;
        screen_frame(g, sx, cy - FH / 2, fw, FH, fg);
        ui_text(g, &nf, lab, sx + fw / 2, cy - FH / 2 + 5 + (FH - 5 - 1.5 - nh) / 2, 1, 0, fg, NULL);
        sx += fw + 8;
        for (int i = 0; i < n && sx + 20 <= x + pw - 10; i++) {
            app_icon(g, R[i].c, 20, sx + 10, cy, fg);
            sx += 25;
        }
        hit(x, 0, pw, H, A_BAR_OV, 0, 0);
    }
}

/* a label in the theme's label style */
static double lbl(Gc *g, const char *s, double x, double y, int align, col c, double *h)
{
    return ui_text(g, &T->lbl, s, x, y, align, 0, c, h);
}

/* an icon button (bw x bh) centred in a cw-wide cell, its label below */
static double cell(Gc *g, double x, double y, double cw, double bw, double bh, double gap, const Box *b, col fg,
                   const char *ic, double ipx, const char *label, int act, int arg, WinId win)
{
    double bx = x + (cw - bw) / 2;
    ui_box(g, bx, y, bw, bh, b);
    if (ic && !strcmp(ic, GLASSINE_ICON_SHEETS)) sheets_icon(g, bx + bw / 2, y + bh / 2, ipx, "", fg);
    else if (ic) ui_icon(g, ic, ipx, bx + bw / 2, y + bh / 2, fg);
    double lh;
    ui_text_w(g, &T->lbl, label, &lh);
    lbl(g, label, x + cw / 2, y + bh + gap, 1, act ? T->ink : T->sub, NULL);
    if (act) hit(x, y, cw, bh + gap + lh, act, arg, win);
    return bh + gap + lh;
}

static void card_frame(Gc *g, double x, double y, double w, double h)
{
    bound(x, y, w, h, 10);
    ui_box(g, x, y, w, h, &T->card);
    if (T->emboss) ui_stroke_rrect(g, x + 6, y + 6, w - 12, h - 12, 2, 1, IL_RULE, 0);
}

/* ---- quick settings ---------------------------------------------------------- */
/* the Power popup: A_P_ACT's arg indexes these */
static const struct { const char *what, *name, *sub, *ic; } PWR[5] = {
    { "sleep", "Sleep", "Keeps the page on screen, wakes in a second", "weather-clear-night-symbolic" },
    { "reboot", "Restart", NULL, "system-reboot-symbolic" },     /* sub: the apps it closes */
    { "poweroff", "Power off", "The last page stays on the panel", "system-shutdown-symbolic" },
    { "android", "Android", "Second system", "phone-symbolic" },
    { "recovery", "TWRP", "Recovery, for updates", "applications-engineering-symbolic" },
};
#define ROT_CONF "/etc/glassine/rotation"    /* auto | lock N (glassine-rotd) */
#define ROT_RUN "/run/glassine-rotation"     /* the turn now */
static const char *const ROT_NAME[4] = { "Upright", "Turned right", "Upside down", "Turned left" };
static void rot_read(void)
{
    char b[32] = "";
    FILE *f = fopen(ROT_CONF, "r");
    if (f) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); }
    int n = -1;
    st.rot_auto = strncmp(b, "lock", 4) != 0;
    st.rot_cur = 0;
    if ((f = fopen(ROT_RUN, "r"))) { if (fscanf(f, "%d", &st.rot_cur) != 1) st.rot_cur = 0; fclose(f); }
    if (st.rot_cur % 90 || st.rot_cur < 0 || st.rot_cur > 270) st.rot_cur = 0;
    st.rot_lock = !st.rot_auto && sscanf(b + 4, "%d", &n) == 1 && n >= 0 && n <= 270 && n % 90 == 0 ? n : st.rot_cur;
}
static void rot_set(const char *v)
{
    char c[96];
    snprintf(c, sizeof c, "doas /usr/sbin/glassine-settings-root rotation %s", v);
    run(c, NULL);
}
/* held toggles open these (0-3: Wi-Fi, Bluetooth, Mute, Refresh; 4: a held
 * front light / warmth label): glassine-settings on its page */
#define GLASSINE_SETTINGS(page) "/usr/bin/glassine-settings " page
static const struct { const char *name, *cmd; } SETTINGS[5] = {
    { "network", GLASSINE_SETTINGS("wifi") },
    { "Bluetooth", GLASSINE_SETTINGS("bt") },
    { "sound", GLASSINE_SETTINGS("sound") },
    { "display", GLASSINE_SETTINGS("eink") },
    { "light", GLASSINE_SETTINGS("light") },
};
#define HOLD_MS 550
#define OPENING_ALL 6                   /* the settings button: glassine-settings itself */
static void raise_settings(void);        /* glassine-settings forward (below held) */

/* the Rotation row: Auto + the four turns, each a device held
 * upright with a T where the top of the screen will be; paint 0: height only */
static double qs_rotation(Gc *g, double x0, double y, double CW, double rowh)
{
    char v[48];
    int cur = st.rot_auto ? st.rot_cur : st.rot_lock;
    snprintf(v, sizeof v, "%s · %s", st.rot_auto ? "Auto" : "Locked", ROT_NAME[cur / 90]);
    lbl(g, "Rotation", x0, y, 0, T->lblC, NULL);
    ui_text(g, &T->numFont, v, x0 + CW, y, 2, 0, T->ink, NULL);
    y += rowh + 8;
    const double GAP = 8, RH = 64, bw = (CW - 4 * GAP) / 5;
    TFont af = ui_font_px(&T->dateFont, 12);
    double ah;
    ui_text_w(g, &af, "Auto", &ah);
    /* toggle fills and shadows on the button's corners */
    Box ton = T->togOn, toff = T->tog;
    memcpy(ton.r, T->btn.r, sizeof ton.r);
    memcpy(toff.r, T->btn.r, sizeof toff.r);
    {   /* Auto */
        const Box *b = st.rot_auto ? &ton : &toff;
        col fg = st.rot_auto ? T->togOnFg : T->ink;
        ui_box(g, x0, y, bw, RH, b);
        double top = y + (RH - 24 - 4 - ah) / 2;
        ui_icon(g, "rotation-allowed-symbolic", 24, x0 + bw / 2, top + 12, fg);
        ui_text(g, &af, "Auto", x0 + bw / 2, top + 24 + 4, 1, bw, fg, NULL);
        hit(x0, y, bw, RH, A_QS_ROT, 4, 0);
    }
    for (int k = 0; k < 4; k++) {
        double bx = x0 + (k + 1) * (bw + GAP);
        int sel = !st.rot_auto && st.rot_lock == k * 90;
        col fg = sel ? T->togOnFg : T->ink;
        ui_box(g, bx, y, bw, RH, sel ? &ton : &toff);
        if (st.rot_auto && st.rot_cur == k * 90) {   /* what the sensor turned it to */
            /* concentric with the tile's corners (its radius less the inset) */
            double m = (bw < RH ? bw : RH) / 2, r = toff.r[0] == HALF || toff.r[0] > m ? m : toff.r[0];
            ui_stroke_rrect(g, bx + 5, y + 5, bw - 10, RH - 10, r > 5 ? r - 5 : 0, 1.5, T->ink, 1);
        }
        /* the device: 24 x 33, its button below; the T points to the screen's top */
        double gx = bx + bw / 2, gy = y + RH / 2 - 2.5;
        ui_stroke_rrect(g, gx - 12, gy - 16.5, 24, 33, 4, 1.5, fg, 0);
        ui_rrect(g, gx - 4.5, gy + 16.5 - 6, 9, 2.5, 1, fg);
        static const int UX[4] = { 0, 1, 0, -1 }, UY[4] = { -1, 0, 1, 0 };
        double ux = UX[k], uy = UY[k];
        if (uy) {   /* bar horizontal, stem vertical */
            ui_rrect(g, gx - 4.5, gy + uy * 4.5 - 1, 9, 2, 0, fg);
            ui_rrect(g, gx - 1, gy - 4.5, 2, 9, 0, fg);
        } else {
            ui_rrect(g, gx + ux * 4.5 - 1, gy - 4.5, 2, 9, 0, fg);
            ui_rrect(g, gx - 4.5, gy - 1, 9, 2, 0, fg);
        }
        hit(bx, y, bw, RH, A_QS_ROT, k, 0);
    }
    return rowh + 8 + RH;
}

/* e-ink fluidity: four tiles as the rotation's (see FLUID) */
static double qs_fluidity(Gc *g, double x0, double y, double CW, double rowh)
{
    int cur = fluid_of(st.rhi);
    lbl(g, "E-ink fluidity", x0, y, 0, T->lblC, NULL);
    ui_text(g, &T->numFont, FLUID[cur].name, x0 + CW, y, 2, 0, T->ink, NULL);
    y += rowh + 8;
    const double GAP = 8, RH = 64, bw = (CW - 3 * GAP) / 4;
    TFont af = ui_font_px(&T->dateFont, 12);
    double ah;
    ui_text_w(g, &af, "Balanced", &ah);
    for (int k = 0; k < 4; k++) {
        double bx = x0 + k * (bw + GAP);
        int sel = k == cur;
        col fg = sel ? T->togOnFg : T->ink;
        ui_box(g, bx, y, bw, RH, sel ? &T->togOn : &T->tog);
        double top = y + (RH - 24 - 4 - ah) / 2;
        ui_icon(g, FLUID[k].ic, 24, bx + bw / 2, top + 12, fg);
        ui_text(g, &af, FLUID[k].name, bx + bw / 2, top + 24 + 4, 1, bw, fg, NULL);
        hit(bx, y, bw, RH, A_QS_FLUID, k, 0);
    }
    return rowh + 8 + RH;
}

static void view_qs(Gc *g)
{
    /* landscape: 860 wide, two columns -- the title, toggles
     * and sliders left; responsiveness, rotation and the session right */
    const int LAND = LW > LH;
    const double W = LAND ? 860 : 440, X = LW - 12 - W, Y = BH + 12, PX = 26, COLGAP = 32,
                 CW = LAND ? (W - 2 * PX - COLGAP) / 2 : W - 2 * PX;
    double th, dh, y;
    /* heights first (layout), then the card, then the content */
    char date[64];
    time_t t = time(NULL);
    strftime(date, sizeof date, "%A, %-d %B", localtime(&t));
    ui_text_w(g, &T->headFont, T->qsTitle, &th);
    ui_text_w(g, &T->dateFont, date, &dh);
    if (st.opening == OPENING_ALL) snprintf(date, sizeof date, "Opening Settings…");
    ui_text_w(g, &T->dateFont, date, &dh);
    double hh = (th + 2 + dh > 48 ? th + 2 + dh : 48) + 10 + T->headRuleW, lh, nh;
    ui_text_w(g, &T->lbl, "Ag", &lh);
    ui_text_w(g, &T->numFont, "0", &nh);
    double rowh = lh > nh ? lh : nh;
    TFont hf = ui_font_px(&T->dateFont, 13);
    char hint[64];
    if (st.opening >= 0 && st.opening <= 4) snprintf(hint, sizeof hint, "Opening %s settings…", SETTINGS[st.opening].name);
    else snprintf(hint, sizeof hint, "Hold an icon for its settings");
    double hinth;
    ui_text_w(g, &hf, hint, &hinth);
    double h_tog = 62 + 8 + lh, h_sl = 3 * (rowh + 2 + 46) + 2 * 4, h_mode = rowh + 8 + 64,
           h_rot = rowh + 8 + 64;
    double left = hh + 18 + h_tog + 10 + hinth + 18 + h_sl, right = h_mode + 18 + h_rot;
    double H = 20 + (LAND ? (left > right ? left : right) : left + 18 + right) + 22;
    card_frame(g, X, Y, W, H);

    y = Y + 20;
    double x0 = X + PX;
    /* the title over the date; power and settings buttons right */
    double cy = y + (hh - 10 - T->headRuleW) / 2;
    ui_text(g, &T->headFont, T->qsTitle, x0, cy - (th + 2 + dh) / 2, 0, CW - 116, T->ink, NULL);
    hit(x0, y, CW / 2, hh, A_QS_TITLE, 0, 0);
    ui_text(g, &T->dateFont, date, x0, cy - (th + 2 + dh) / 2 + th + 2, 0, CW - 116, T->sub, NULL);
    ui_box(g, x0 + CW - 48 - 10 - 48, cy - 24, 48, 48, &T->btn);
    ui_icon(g, "system-shutdown-symbolic", 26, x0 + CW - 48 - 10 - 24, cy, T->ink);
    hit(x0 + CW - 48 - 10 - 52, y, 52, hh, A_QS_PWR, 0, 0);
    ui_box(g, x0 + CW - 48, cy - 24, 48, 48, &T->btn);
    if (!ui_icon(g, "preferences-system", 26, x0 + CW - 24, cy, T->ink)) ui_icon(g, "emblem-system-symbolic", 26, x0 + CW - 24, cy, T->ink);
    hit(x0 + CW - 52, y, 52, hh, A_QS_SETTINGS, 0, 0);
    ui_hline(g, x0, y + hh - T->headRuleW, CW, T->headRuleW, T->headRuleC, 0);
    y += hh + 18;

    /* toggles */
    const char *ti[4] = { "network-wireless-signal-excellent-symbolic", "bluetooth-symbolic", "audio-volume-muted-symbolic", "view-refresh-symbolic" };
    const char *tl[4] = { st.wifi_busy ? "Wi-Fi…" : "Wi-Fi", st.bt_busy ? "Bluetooth…" : "Bluetooth", "Mute", "Refresh" };
    int on[4] = { st.wifi, st.bt, st.mute, 0 }, ok[4] = { !st.wifi_busy, st.bt_ok && !st.bt_busy, st.vol_ok, 1 };
    double sp = (CW - 4 * 76) / 3;
    for (int i = 0; i < 4; i++)
        cell(g, x0 + i * (76 + sp), y, 76, 62, 62, 8, on[i] ? &T->togOn : &T->tog, on[i] ? T->togOnFg : ok[i] ? T->ink : T->sub,
             ti[i], 26, tl[i], ok[i] ? A_QS_TOGGLE : A_NONE, i, 0);
    y += h_tog + 10;
    ui_text(g, &hf, hint, x0 + CW / 2, y, 1, 0, T->sub, NULL);
    y += hinth + 18;

    /* sliders */
    /* managed light: the schedule's value plus the offset, which the
     * slider sets from its centre (-50..+50) */
    const char *sl[3] = { "Front light", "Warmth", "Volume" };
    int sv[3] = { st.light, st.warm, st.vol }, sok[3] = { 1, 1, st.vol_ok };
    for (int i = 0; i < 3; i++) {
        char v[24], lb[40];
        int man = i < 2 && st.managed, o = man ? st.off[i] : 0;
        snprintf(v, sizeof v, "%d", sv[i]);
        snprintf(lb, sizeof lb, "%s%s", sl[i], man ? " · managed" : "");
        if (man) snprintf(v, sizeof v, o ? "%d · %s%d" : "%d", sv[i], o > 0 ? "+" : "−", abs(o));
        lbl(g, lb, x0, y, 0, sok[i] ? T->ink : T->sub, NULL);
        ui_text(g, &T->numFont, sok[i] ? v : "–", x0 + CW, y, 2, 0, T->ink, NULL);
        if (i < 2) hit(x0, y - 6, CW, rowh + 6, A_QS_SLABEL, i, 0);     /* hold: the light settings */
        y += rowh + 2;
        double w = CW * (man ? 50 + o : sv[i]) / 100.0;
        Box gr = { .bg = T->grooveBg, .r = { T->grooveR, T->grooveR, T->grooveR, T->grooveR }, .nring = T->ngrooveRing };
        memcpy(gr.ring, T->grooveRing, sizeof T->grooveRing);
        ui_box(g, x0, y + T->grooveTop, CW, T->grooveH, &gr);
        if (man) ui_rrect(g, x0 + CW / 2 - 1, y + 8, 2, 30, 0, T->ink);         /* the schedule mark */
        if (sok[i]) {
            if (man) {
                if (o) ui_rrect(g, x0 + CW * (o < 0 ? 50 + o : 50) / 100.0, y + T->fillTop, CW * abs(o) / 100.0, T->fillH, T->grooveR, T->ink);
            } else
                ui_rrect(g, x0, y + T->fillTop, w, T->fillH, T->grooveR, T->ink);
            Box kn = { .bg = T->knobBg, .bw = T->knobBw, .bc = T->knobBc, .r = { T->knobR, T->knobR, T->knobR, T->knobR } };
            ui_box(g, x0 + w + T->knobML, y + T->knobTop, T->knobW, T->knobH, &kn);
            hit(x0 - 10, y, CW + 20, 46, A_QS_SLIDER, i, 0);
            slider_geo[i].x = x0; slider_geo[i].w = CW;
        }
        y += 46 + (i < 2 ? 4 : 0);
    }
    if (LAND) { x0 += CW + COLGAP; y = Y + 20; }
    else y += 18;

    y += qs_fluidity(g, x0, y, CW, rowh);
    y += 18;

    /* rotation */
    y += qs_rotation(g, x0, y, CW, rowh);
}

/* "Closes Firefox, Terminal and 3 more" (the Power popup's Restart) */
static void pwr_closes(char *out, size_t len)
{
    be_read_desktop();
    const char *nm[64];
    int n = 0;
    for (int i = 0; i < ncl && n < 64; i++) {
        App *a = app_of(cl[i].w);
        const char *x = a && a->name[0] ? a->name : cl[i].name;
        int dup = !x[0];
        for (int j = 0; j < n && !dup; j++) dup = !strcmp(nm[j], x);
        if (!dup) nm[n++] = x;
    }
    if (!n) snprintf(out, len, "No apps open");
    else if (n == 1) snprintf(out, len, "Closes %s", nm[0]);
    else if (n == 2) snprintf(out, len, "Closes %s and %s", nm[0], nm[1]);
    else snprintf(out, len, "Closes %s, %s and %d more", nm[0], nm[1], n - 2);
}

/* ---- power ------------------------------------------------- */
static void view_power(Gc *g)
{
    const double W = 440, X = (LW - W) / 2, Y = LW > LH ? BH + 24 : 110, PX = 26, CW = W - 2 * PX;
    double th, mh, lh;
    const char *msg = "Open apps will be closed for restart and power off";
    ui_text_w(g, &T->headFont, "Power", &th);
    double mw = ui_text_w(g, &T->dateFont, msg, &mh);
    (void)mw;
    ui_text_w(g, &T->lbl, "Ag", &lh);
    char closes[160];
    pwr_closes(closes, sizeof closes);
    TFont sf = ui_font_px(&T->dateFont, 12), nf = ui_font_px(&T->nameFont, 15);
    double rowH[3], h_rows = 0;
    for (int i = 0; i < 3; i++) {
        double nh, sh;
        ui_text_w(g, &T->nameFont, PWR[i].name, &nh);
        ui_text_w(g, &T->dateFont, PWR[i].sub ? PWR[i].sub : closes, &sh);
        double c = 6 + nh + 4 + sh + 6;
        rowH[i] = c > 68 ? c : 68;
        h_rows += rowH[i];
    }
    double hh = th + 2 + mh + 10 + T->headRuleW;
    double H = 20 + hh + 18 + h_rows + 18 + lh + 8 + 64 + 18 + 48 + 22;
    card_frame(g, X, Y, W, H);
    double x0 = X + PX, y = Y + 20;
    ui_text(g, &T->headFont, "Power", x0, y, 0, CW, T->ink, NULL);
    ui_text(g, &T->dateFont, msg, x0, y + th + 2, 0, CW, T->sub, NULL);
    ui_hline(g, x0, y + hh - T->headRuleW, CW, T->headRuleW, T->headRuleC, 0);
    y += hh + 18;
    for (int i = 0; i < 3; i++) {
        double rh = rowH[i], cy = y + rh / 2, nh, sh;
        const Box *b = i == 2 ? &T->pwr : &T->btn;
        col fg = i == 2 ? T->pwrFg : T->ink;
        ui_box(g, x0, cy - 24, 48, 48, b);
        ui_icon(g, PWR[i].ic, 24, x0 + 24, cy, fg);
        const char *sub = PWR[i].sub ? PWR[i].sub : closes;
        ui_text_w(g, &T->nameFont, PWR[i].name, &nh);
        ui_text_w(g, &T->dateFont, sub, &sh);
        double ty = cy - (nh + 4 + sh) / 2;
        ui_text(g, &T->nameFont, PWR[i].name, x0 + 64, ty, 0, CW - 64, T->ink, NULL);
        ui_text(g, &T->dateFont, sub, x0 + 64, ty + nh + 4, 0, CW - 64, T->sub, NULL);
        ui_hline(g, x0, y + rh - T->sepW, CW, T->sepW, T->sepC, T->sepDashed);
        hit(x0, y, CW, rh, A_P_ACT, i, 0);
        y += rh;
    }
    y += 18;
    lbl(g, "Restart into", x0, y, 0, T->lblC, NULL);
    y += lh + 8;
    double cw = (CW - 10) / 2;
    for (int k = 0; k < 2; k++) {
        const int i = 3 + k;
        double bx = x0 + k * (cw + 10), nh, sh;
        ui_box(g, bx, y, cw, 64, &T->btn);
        ui_icon(g, PWR[i].ic, 22, bx + 12 + 11, y + 32, T->ink);
        ui_text_w(g, &nf, PWR[i].name, &nh);
        ui_text_w(g, &sf, PWR[i].sub, &sh);
        double ty = y + (64 - nh - 2 - sh) / 2;
        ui_text(g, &nf, PWR[i].name, bx + 12 + 22 + 10, ty, 0, cw - 56, T->ink, NULL);
        ui_text(g, &sf, PWR[i].sub, bx + 12 + 22 + 10, ty + nh + 2, 0, cw - 56, T->sub, NULL);
        hit(bx, y, cw, 64, A_P_ACT, i, 0);
    }
    y += 64 + 18;
    ui_box(g, x0, y, CW, 48, &T->btn);
    double ch;
    ui_text_w(g, &T->lbl, "Cancel", &ch);
    lbl(g, "Cancel", x0 + CW / 2, y + (48 - ch) / 2, 1, T->ink, NULL);
    hit(x0, y, CW, 48, A_P_CANCEL, 0, 0);
}

/* ---- workspaces ------------------------------------------------------------ */
/* the minimized windows (a swipe set to Minimize, default three fingers
 * down; sway's scratchpad): a row of chips under the workspaces, a tap
 * brings one back to this workspace.
 * paint 0: the height only */
static double ws_minimized(Gc *g, double x, double y, double w, col fg, int paint)
{
    int n = 0;
    for (int k = 0; k < ncl; k++) n += cl[k].desk < 0;
    if (!n) return 0;
    const double CH = 44, GAP = 10;
    double lh = 0;
    if (paint) lbl(g, "Minimized · tap to bring back", x, y, 0, fg, &lh);
    else ui_text_w(g, &T->lbl, "Minimized · tap to bring back", &lh);
    double cx = x, cy = y + lh + 10;
    for (int k = 0; k < ncl; k++) {
        if (cl[k].desk >= 0) continue;
        double nh, nw = ui_text_w(g, &T->folderFont, cl[k].name, &nh);
        if (nw > w - 60) nw = w - 60;
        double cw = 12 + (cl[k].icon[0] ? 26 : 0) + nw + 14;
        if (cx > x && cx + cw > x + w) { cx = x; cy += CH + GAP; }
        if (paint) {
            ui_stroke_rrect(g, cx, cy, cw, CH, CH / 2, 1.5, fg, 0);
            double ix = 0;
            if (cl[k].icon[0] && ui_icon(g, cl[k].icon, 20, cx + 12 + 10, cy + CH / 2, fg)) ix = 26;
            ui_text(g, &T->folderFont, cl[k].name, cx + 12 + ix, cy + (CH - nh) / 2, 0, nw, fg, NULL);
            hit(cx, cy, cw, CH, A_WS_RESTORE, k, cl[k].w);
        }
        cx += cw + GAP;
    }
    return cy + CH - y;
}

/* ---- the overview (spec 001 FR-010..019) ----------------------------------------
 * Each screen is its live preview (be_snap; else its tiles in miniature), 2 x 2 a
 * page; tap = go there. App icons in four slots around it, two per side, for the
 * screen's quarters: a window's icon takes the first quarter it covers, so an app
 * alone sits top left. Under each preview: move it earlier, close it with its
 * apps, move it later. Tap an icon: the app is picked up, switching screens
 * pauses, the previews fade, every app's icon sits inside its screen, arrows beside
 * an app put the moving app on that side of it (ov_targets) and the moving app's
 * own actions sit in a half-circle around its icon (ov_actions). */
static void pg_btn(Gc *g, double x, double y, double sz, int next, int on, int act, int arg);

/* the screen's title: its apps in order; a full-screen one says so (FR-012) */
static void ov_title(const Rc *R, int n, char *out, size_t len)
{
    const Client *f = scr_full(R, n);
    size_t o = 0;
    out[0] = 0;
    if (f) o += snprintf(out, len, "%s full screen", f->name);
    int k = 0;
    for (int i = 0; i < n && o < len; i++) {
        if (!R[i].c || R[i].c == f) continue;
        o += snprintf(out + o, len - o, "%s%s", f ? " · " : k ? " · " : "", R[i].c->name);
        k++;
    }
    if (f && k && o < len) snprintf(out + o, len - o, " hidden");
    if (!n) snprintf(out, len, "Empty");
}

static int ov_pages(int *npages, int *page)
{
    int np = ((int)ndesk + 3) / 4, p;
    const Client *m = st.mv ? client(st.mv) : NULL;
    if (np < 1) np = 1;
    p = st.ov_page >= 0 ? st.ov_page : (int)((m ? m->desk : curdesk) >= 0 ? (m ? m->desk : curdesk) / 4 : 0);
    if (p > np - 1) p = np - 1;
    *npages = np; *page = p;
    return p;
}

static void ov_turn_page(int dir)
{
    int np, p;
    ov_pages(&np, &p);
    int q = p + dir < 0 ? 0 : p + dir > np - 1 ? np - 1 : p + dir;
    if (q == p) return;
    st.ov_page = q;
    render_pop();
}

/* the still: be_snap's picture of the screen, else its tiles in miniature (a
 * full-screen app: the whole preview) */
static void ov_thumb(Gc *g, long d, double x, double y, double w, double h)
{
    ui_fill_rect(g, x, y, w, h, T->ink);   /* the gaps between the tiles: ink (design) */
    cairo_surface_t *snap = g->paint ? be_snap(d) : NULL;
    if (snap) {                         /* an A8 mask: 255 = ink */
        cairo_t *cr = g->cr;
        int sw = cairo_image_surface_get_width(snap), sh = cairo_image_surface_get_height(snap);
        ui_fill_rect(g, x, y, w, h, RGB(0xffffff));
        cairo_save(cr);
        cairo_rectangle(cr, x, y, w, h);
        cairo_clip(cr);
        cairo_translate(cr, x, y);
        cairo_scale(cr, w / sw, h / sh);
        cairo_set_source_rgb(cr, ((T->ink >> 16) & 255) / 255.0, ((T->ink >> 8) & 255) / 255.0, (T->ink & 255) / 255.0);
        cairo_pattern_t *m = cairo_pattern_create_for_surface(snap);
        cairo_pattern_set_filter(m, CAIRO_FILTER_GOOD);
        cairo_mask(cr, m);
        cairo_pattern_destroy(m);
        cairo_restore(cr);
        return;
    }
    Rc R[16];
    int n = scr_rects(d, R, 0);
    if (!n || scr_full(R, n)) { ui_fill_rect(g, x, y, w, h, RGB(0xffffff)); return; }
    for (int i = 0; i < n; i++) {
        const Client *c = R[i].c;
        if (!c || warea[2] <= 0 || warea[3] <= 0) continue;
        double rx = x + (c->x - warea[0]) * w / warea[2], ry = y + (c->y - warea[1]) * h / warea[3];
        double rw = c->wd * w / warea[2], rh = c->ht * h / warea[3];
        if (rx < x) rx = x;
        if (ry < y) ry = y;
        if (rx + rw > x + w) rw = x + w - rx;
        if (ry + rh > y + h) rh = y + h - ry;
        if (rw < 4 || rh < 4) continue;
        ui_fill_rect(g, rx, ry, rw, rh, RGB(0xffffff));
    }
}

/* a 48 px button with an app's icon; ring: the heavy edge of a full-screen app */
static void ov_icon_btn(Gc *g, const Client *c, double x, double y, int hi, int ring, int act)
{
    ui_box(g, x, y, 48, 48, hi ? &T->sel : &T->btn);
    if (ring) ui_stroke_rrect(g, x + 1.25, y + 1.25, 45.5, 45.5, T->btn.r[0] > 1.25 ? T->btn.r[0] - 1.25 : 0, 2.5, T->ink, 0);
    app_icon(g, c, 24, x + 24, y + 24, hi ? T->selFg : T->ink);
    if (act) hit(x, y, 48, 48, act, 0, c->w);
}

/* an arrow of the move (FR-015): its centre in preview px, the side it puts the
 * moving app on, the window it sits beside */
enum { SIDE_UP, SIDE_DOWN, SIDE_LEFT, SIDE_RIGHT };
static const char *const SIDE_W[4] = { "up", "down", "left", "right" };
typedef struct { double x, y; int side; WinId k; } Tgt;

/* the arrows on screen d for moving m, worked out on the screen as it would be
 * without m (so m's own screen works too); an arrow that would change nothing
 * is left out. R, n: d's rectangles now; W, H: the preview */
static int ov_targets(long d, const Client *m, const Rc *R, int n, double W, double H, Tgt *out)
{
    static const double DX[4] = { 0, 0, -54, 54 }, DY[4] = { -54, 54, 0, 0 };
    int src = m->desk == d, nt = 0;
    Layout X = lay_get(d), B;
    if (src) { if (n < 2) return 0; B = lay_without(&X, m->w); }
    else { if (n >= WS_MAX_APPS) return 0; B = X; }
    int nb = lay_n(&B);
    if (!nb) return 0;
    Rc RB[16];
    int nrb = lay_rects(&B, RB);
    struct { WinId k; int side; Layout nx; } C[16];
    int nc = 0;
    if (nb == 1) {
        WinId a = B.w[0][0];
        for (int sd = 0; sd < 4; sd++) {
            Layout nx = { .side = sd >= SIDE_LEFT, .ng = 2, .gn = { 1, 1 } };
            int first = sd == SIDE_UP || sd == SIDE_LEFT;
            nx.w[0][0] = first ? m->w : a; nx.w[1][0] = first ? a : m->w;
            C[nc].k = a; C[nc].side = sd; C[nc].nx = nx; nc++;
        }
    } else
        for (int i = 0; i < nrb && nc < 15; i++) {
            int gi = RB[i].gi;
            if (gi < 0 || B.gn[gi] != 1) continue;
            for (int before = 1; before >= 0; before--) {
                Layout nx = B;
                nx.gn[gi] = 2;
                nx.w[gi][0] = before ? m->w : RB[i].w; nx.w[gi][1] = before ? RB[i].w : m->w;
                C[nc].k = RB[i].w; C[nc].nx = nx;
                C[nc].side = B.side ? (before ? SIDE_UP : SIDE_DOWN) : (before ? SIDE_LEFT : SIDE_RIGHT);
                nc++;
            }
        }
    for (int i = 0; i < nc; i++) {
        if (src && lay_same(&C[i].nx, &X)) continue;
        const Rc *r = NULL;
        for (int k = 0; k < n && !r; k++) if (R[k].w == C[i].k) r = &R[k];
        for (int k = 0; k < nrb && !r; k++) if (RB[k].w == C[i].k) r = &RB[k];
        if (!r) continue;
        double tx = (r->x + r->wd / 2) * W + DX[C[i].side], ty = (r->y + r->ht / 2) * H + DY[C[i].side];
        tx = tx < -30 ? -30 : tx > W + 30 ? W + 30 : tx;
        ty = ty < 18 ? 18 : ty > H - 18 ? H - 18 : ty;
        out[nt++] = (Tgt){ tx, ty, C[i].side, C[i].k };
    }
    return nt;
}

/* the moving app's actions (FR-016): Close app, and with others on its screen
 * Full screen (or Exit full screen) and New screen, in a half-circle around its
 * icon that bulges out of the screen: sideways for a window narrower than the
 * screen (Close toward the nearest edge, Full screen above, New screen below),
 * up or down for a full-width half (Close toward that edge, Full screen left,
 * New screen right). A spot within 46 px of another icon, arrow or action, or
 * off the preview (x: by more than the rail beside it, 30 px; y: by more than
 * 8 px) moves to the first free spot around the icon. Preview px. */
typedef struct { double x, y; int act; } OvAct;
static int ov_actions(const Client *m, const Rc *R, int n, const Tgt *tg, int nt, double W, double H, OvAct *out)
{
    const Rc *me = NULL;
    for (int i = 0; i < n && !me; i++) if (R[i].w == m->w) me = &R[i];
    if (!me) return 0;
    double mx = (me->x + me->wd / 2) * W, my = (me->y + me->ht / 2) * H, edge = mx < W / 2 ? -50 : 50;
    int vert = me->wd >= .999 && me->ht < .999;
    double outY = my < H / 2 ? -46 : 46;
    double P[3][2];
    if (vert) { P[0][0] = 0; P[0][1] = outY; P[1][0] = -50; P[1][1] = 0; P[2][0] = 50; P[2][1] = 0; }
    else { P[0][0] = edge; P[0][1] = 0; P[1][0] = 0; P[1][1] = -46; P[2][0] = 0; P[2][1] = 46; }
    const double OFF[10][2] = { { 0, -46 }, { 0, 46 }, { edge, 0 }, { -edge, 0 }, { edge, -50 }, { edge, 50 },
                                { -edge, -50 }, { -edge, 50 }, { 2 * edge, 0 }, { -2 * edge, 0 } };
    double taken[48][2];
    int nk = 0;
    for (int i = 0; i < n; i++)
        if (R[i].w != m->w) { taken[nk][0] = (R[i].x + R[i].wd / 2) * W; taken[nk][1] = (R[i].y + R[i].ht / 2) * H; nk++; }
    for (int i = 0; i < nt && nk < 40; i++) { taken[nk][0] = tg[i].x; taken[nk][1] = tg[i].y; nk++; }
    taken[nk][0] = mx; taken[nk][1] = my; nk++;
    static const int ACT[3] = { A_OV_CLOSEAPP, A_OV_FULL, A_OV_NEW };
    int na = 0, want = n > 1 ? 3 : 1;
    for (int a = 0; a < want; a++) {
        double sx = 0, sy = 0;
        int found = 0;
        for (int k = -1; k < 10 && !found; k++) {
            double x = mx + (k < 0 ? P[a][0] : OFF[k][0]), y = my + (k < 0 ? P[a][1] : OFF[k][1]);
            if (x < -30 || x > W + 30 || y < 14 || y > H - 14) continue;
            int ok = 1;
            for (int t = 0; t < nk && ok; t++) ok = hypot(taken[t][0] - x, taken[t][1] - y) >= 46;
            if (ok) { sx = x; sy = y; found = 1; }
        }
        if (!found) continue;
        taken[nk][0] = sx; taken[nk][1] = sy; nk++;
        out[na++] = (OvAct){ sx, sy, ACT[a] };
    }
    return na;
}

/* the glyph of New screen (the design's add_to_queue): a screen with a plus */
static void newscreen_glyph(Gc *g, double cx, double cy, col c)
{
    ui_stroke_rrect(g, cx - 10, cy - 8, 20, 14, 1.5, 1.75, c, 0);
    ui_fill_rect(g, cx - 5, cy + 9, 10, 1.75, c);
    ui_fill_rect(g, cx - 4, cy - 1.875, 8, 1.75, c);
    ui_fill_rect(g, cx - .875, cy - 5, 1.75, 8, c);
}

static void view_ov(Gc *g)
{
    const double X = 8, Y = BH + 8, W = LW - 16, H = LH - BH - 16, PX = 14, CW = W - 2 * PX;
    if (st.mv && !client(st.mv)) st.mv = 0;     /* it closed meanwhile */
    const Client *mc = st.mv ? client(st.mv) : NULL;
    int np, page;
    ov_pages(&np, &page);
    int nwin = 0;
    for (int k = 0; k < ncl; k++) nwin += cl[k].desk >= 0;
    char sub[200], hint[300], pl[16];
    if (mc) {
        snprintf(sub, sizeof sub, "Moving %s · switching screens is paused", mc->name);
        snprintf(hint, sizeof hint, "Tap an arrow to put %s on that side of an app · its own actions sit around it · "
                 "tap another app to pick it instead", mc->name);
    } else {
        snprintf(sub, sizeof sub, "%ld %s · %d %s", ndesk, ndesk == 1 ? "screen" : "screens", nwin, nwin == 1 ? "window" : "windows");
        snprintf(hint, sizeof hint, "Tap a screen to go to it · tap an app to move, full-screen or close it · arrows reorder screens");
    }
    snprintf(pl, sizeof pl, "%d/%d", page + 1, np);

    /* the header: the title, the line, the pager, close (undo while moving: FR-017) */
    double th, tw = ui_text_w(g, &T->headFont, T->oTitle, &th), dh;
    ui_text_w(g, &T->dateFont, sub, &dh);
    double rowh = 52;
    card_frame(g, X, Y, W, H);
    double x0 = X + PX, y = Y + 10, cy = y + rowh / 2;
    ui_text(g, &T->headFont, T->oTitle, x0, cy - th / 2, 0, 0, T->ink, NULL);
    double xr = x0 + CW - 48;
    ui_box(g, xr, cy - 24, 48, 48, &T->btn);
    ui_icon(g, mc ? "edit-undo-symbolic" : "window-close-symbolic", 24, xr + 24, cy, T->ink);
    hit(xr, cy - 24, 48, 48, mc ? A_OV_CANCEL : A_OV_X, 0, 0);
    if (np > 1) {
        double px = xr - 10 - 48;
        pg_btn(g, px, cy - 24, 48, 1, page < np - 1, A_OV_NEXT, 0);
        double ph;
        ui_text_w(g, &T->pageFont, pl, &ph);
        ui_text(g, &T->pageFont, pl, px - 4 - 15, cy - ph / 2, 1, 0, T->ink, NULL);
        pg_btn(g, px - 4 - 30 - 4 - 48, cy - 24, 48, 0, page > 0, A_OV_PREV, 0);
        xr = px - 4 - 30 - 4 - 48;
    }
    ui_text(g, &T->dateFont, sub, x0 + tw + 10, cy - dh / 2, 0, xr - 10 - (x0 + tw + 10), T->sub, NULL);
    y += rowh + 6;
    ui_hline(g, x0, y, CW, T->headRuleW, T->headRuleC, 0);
    y += T->headRuleW + 10;

    TFont hf = ui_font_px(&T->dateFont, 13);
    double hh;
    ui_text_w(g, &hf, hint, &hh);
    double mh = mc ? 0 : ws_minimized(g, 0, 0, CW, T->ink, 0);
    double gridY = y, gridH = Y + H - 10 - hh - 10 - (mh ? mh + 10 : 0) - gridY;
    hit(X, gridY, W, gridH, A_NONE, 0, 0);  /* the gaps: no backdrop close (a swipe starts there) */

    const double CG = 14, RG = 12, RAIL = 48, RGAP = 6, ACTS = 48;
    double cw = (CW - CG) / 2, rh = (gridH - RG) / 2, nh;
    ui_text_w(g, &T->appNameFont, "Ag", &nh);
    double titleH = nh > 20 ? nh : 20;
    double asp = warea[3] > 0 ? (double)warea[2] / warea[3] : .75;
    double pw = cw - 2 * (RAIL + RGAP), ph = pw / asp, phmax = rh - titleH - 6 - 6 - ACTS;
    if (ph > phmax) { ph = phmax; pw = ph * asp; }

    for (int i = 0; i < 4 && page * 4 + i < ndesk; i++) {
        long d = page * 4 + i;
        double cx = x0 + (i % 2) * (cw + CG), ry = gridY + (i / 2) * (rh + RG);
        Rc R[16];
        int n = scr_rects(d, R, 0), isCur = d == curdesk, isSrc = mc && mc->desk == d;
        int nofit = mc && !isSrc && n >= WS_MAX_APPS;
        const Client *full = scr_full(R, n);
        char title[300], num[16];
        ov_title(R, n, title, sizeof title);
        if (nofit) snprintf(title + strlen(title), sizeof title - strlen(title), " · no room");
        snprintf(num, sizeof num, "%ld", d + 1);
        TFont tf = T->appNameFont;
        if (isCur && !mc) tf.weight = 700;
        double nw = ui_text(g, &T->pageFont, num, cx + 2, ry + (titleH - nh) / 2, 0, 0, T->ink, NULL);
        ui_text(g, &tf, title, cx + 2 + nw + 6, ry + (titleH - nh) / 2, 0, cw - 4 - nw - 6, T->ink, NULL);
        double py = ry + titleH + 6, px = cx + (cw - pw) / 2;
        /* the preview */
        ov_thumb(g, d, px, py, pw, ph);
        if (mc) ui_fill_rect(g, px, py, pw, ph, 0xd1ffffff);    /* faded: design 0.18 */
        ui_stroke_rrect(g, px - .75, py - .75, pw + 1.5, ph + 1.5, 0, 1.5, T->ink, 0);
        if (isCur) ui_stroke_rrect(g, px - 5.25, py - 5.25, pw + 10.5, ph + 10.5, 0, 2.5, T->ink, 0);   /* the double frame */
        if (!mc) {
            hit(px, py, pw, ph, A_WS_GOTO, (int)d, 0);
            /* the four slots: TL, TR, BL, BR; a window takes the first quarter it covers */
            const Rc *slot[4] = { 0 };
            static const double QX[4] = { .25, .75, .25, .75 }, QY[4] = { .25, .25, .75, .75 };
            for (int k = 0; k < n; k++) {
                const Rc *r = &R[k];
                for (int q = 0; q < 4; q++)
                    if (QX[q] > r->x && QX[q] < r->x + r->wd && QY[q] > r->y && QY[q] < r->y + r->ht) {
                        if (!slot[q]) slot[q] = r;
                        else if (r->gi < 0) continue;   /* the full one, out of the tiling: the first free slot */
                        break;
                    }
            }
            for (int q = 0; q < 4; q++) {
                if (!slot[q] || !slot[q]->c) continue;
                double sx = q % 2 ? px + pw + RGAP : px - RGAP - RAIL;
                double sy = py + (q < 2 ? ph / 4 : 3 * ph / 4) - RAIL / 2;
                ov_icon_btn(g, slot[q]->c, sx, sy, 0, slot[q]->c == full, A_OV_PICK);
            }
            /* earlier, close the screen with its apps, later (FR-013): faint at the ends */
            double by = py + ph + 6, bx = cx + cw / 2 - (3 * 48 + 2 * 8) / 2;
            static const char *const SI[3] = { "go-previous-symbolic", "window-close-symbolic", "go-next-symbolic" };
            int on[3] = { d > 0, ndesk > 1, d < ndesk - 1 }, A[3] = { A_SCR_PREV, A_SCR_CLOSE, A_SCR_NEXT };
            for (int b = 0; b < 3; b++) {
                double bxx = bx + b * (48 + 8);
                ui_box(g, bxx, by, 48, 48, &T->btn);
                ui_icon(g, SI[b], 22, bxx + 24, by + 24, on[b] ? T->ink : IL_DIM);
                if (on[b]) hit(bxx, by, 48, 48, A[b], (int)d, 0);
            }
        } else {
            /* every app inside, at its window's centre (FR-014): another one picks
             * it instead, the moving one's own icon cancels */
            for (int k = 0; k < n; k++) {
                if (!R[k].c) continue;
                double ic = px + (R[k].x + R[k].wd / 2) * pw, jc = py + (R[k].y + R[k].ht / 2) * ph;
                int me = R[k].w == st.mv;
                ov_icon_btn(g, R[k].c, ic - 24, jc - 24, me, 0, me ? A_OV_CANCEL : A_OV_PICK);
            }
            Tgt tg[16];
            int nt = ov_targets(d, mc, R, n, pw, ph, tg);
            for (int t = 0; t < nt; t++) {
                static const char *const AI[4] = { "go-up-symbolic", "go-down-symbolic", "go-previous-symbolic", "go-next-symbolic" };
                double tx = px + tg[t].x - 24, ty = py + tg[t].y - 24;
                ui_rrect(g, tx, ty, 48, 48, T->btn.r[0], IL_HL);
                ui_stroke_rrect(g, tx + 1, ty + 1, 46, 46, T->btn.r[0] > 1 ? T->btn.r[0] - 1 : 0, 2, T->ink, 1);
                ui_icon(g, AI[tg[t].side], 26, tx + 24, ty + 24, T->ink);
                hit(tx, ty, 48, 48, A_OV_PUT, tg[t].side, tg[t].k);
            }
            if (isSrc) {
                OvAct oa[3];
                int na = ov_actions(mc, R, n, tg, nt, pw, ph, oa);
                Box b = T->btn;
                b.bw = 1.5; b.bc = T->ink;
                for (int a = 0; a < na; a++) {
                    double ax = px + oa[a].x - 22, ay = py + oa[a].y - 22;
                    ui_box(g, ax, ay, 44, 44, &b);
                    if (oa[a].act == A_OV_CLOSEAPP) ui_icon(g, "window-close-symbolic", 24, ax + 22, ay + 22, T->ink);
                    else if (oa[a].act == A_OV_FULL)
                        ui_icon(g, mc->maxed ? "view-restore-symbolic" : "view-fullscreen-symbolic", 24, ax + 22, ay + 22, T->ink);
                    else newscreen_glyph(g, ax + 22, ay + 22, T->ink);
                    hit(ax, ay, 44, 44, oa[a].act, 0, mc->w);
                }
            }
            if (nofit) ui_fill_rect(g, cx, ry, cw, rh, 0x8cffffff);   /* design: opacity .45 */
        }
    }
    double yb = gridY + gridH + 10;
    if (mh) { ws_minimized(g, x0, yb, CW, T->ink, 1); yb += mh + 10; }
    ui_text(g, &hf, hint, x0 + CW / 2, yb, 1, CW, T->sub, NULL);
}

static App *notice_app(const char *entry, const char *app)
{
    load_apps();
    char e[96];
    snprintf(e, sizeof e, "%s", entry);
    size_t l = strlen(e);
    if (l > 8 && !strcmp(e + l - 8, ".desktop")) e[l - 8] = 0;
    for (int i = 0; e[0] && i < napps; i++) if (!strcmp(apps[i].id, e)) return &apps[i];
    for (int i = 0; app[0] && i < napps; i++)
        if (!strcasecmp(apps[i].name, app) || !strcasecmp(apps[i].id, app) ||
            (apps[i].wmclass[0] && !strcasecmp(apps[i].wmclass, app)) || (apps[i].exe[0] && !strcasecmp(apps[i].exe, app)))
            return &apps[i];
    return NULL;
}

static const char *notice_icon(const Notice *n)
{
    if (n->icon[0]) return n->icon;
    App *a = notice_app(n->entry, n->app);
    return a ? a->icon : "dialog-information-symbolic";
}

/* "now", "12 min ago", "14:32" (today), "Yesterday 14:32", "Mon 14:32", "3 Sep" */
static void notice_when(time_t t, char *out, size_t n)
{
    time_t now = time(NULL);
    long d = (long)(now - t);
    if (d < 60) { snprintf(out, n, "now"); return; }
    if (d < 3600) { snprintf(out, n, "%ld min ago", d / 60); return; }
    struct tm a = *localtime(&t), b = *localtime(&now);
    struct tm a0 = a, b0 = b;
    a0.tm_hour = a0.tm_min = a0.tm_sec = 0; b0.tm_hour = b0.tm_min = b0.tm_sec = 0;
    a0.tm_isdst = b0.tm_isdst = -1;
    long days = lround(difftime(mktime(&b0), mktime(&a0)) / 86400);
    strftime(out, n, days <= 0 ? "%H:%M" : days == 1 ? "Yesterday %H:%M" : days < 7 ? "%a %H:%M" : "%-d %b", &a);
}

static void view_notif(Gc *g)
{
    const double X = 12, Y = BH + 12, W = LW - 24, PX = 22, CW = W - 2 * PX;
    double th, lh, ch;
    ui_text_w(g, &T->headFont, T->nTitle, &th);
    ui_text_w(g, &T->lbl, "Ag", &lh);
    double hh = (th > 44 ? th : 44) + 10 + T->headRuleW;
    TFont hf = ui_font_px(&T->dateFont, 13);
    double fh, eh;
    ui_text_w(g, &hf, "Tap a notice to open it", &fh);
    ui_text_w(g, &T->dateFont, "Nothing new", &eh);
    /* the notices that fit on the screen, newest first */
    double tx = PX + 44 + 14, tw = CW - 44 - 14 - 14 - 44, room = LH - Y - 12 - (18 + hh + 10 + fh + 14);
    double ih[MAXNOTE];
    int shown = 0;
    double list_h = 0;
    for (int i = 0; i < nnotes; i++) {
        double nh = notes[i].title[0] ? ui_text_wrap(g, &T->nTitleFont, notes[i].title, 0, 0, tw, 2, 0, 0) : 0;
        double bh = notes[i].body[0] ? ui_text_wrap(g, &T->nBodyFont, notes[i].body, 0, 0, tw, 4, T->nBodyLh, 0) : 0;
        double c = lh + 3 + nh + (bh ? 3 + bh : 0);
        ih[i] = 14 + (c > 44 ? c : 44) + 14;
        if (list_h + ih[i] > room) break;
        list_h += ih[i];
        shown++;
    }
    if (!nnotes) list_h = 34 + eh + 24;
    double H = 18 + hh + list_h + 10 + fh + 14;
    card_frame(g, X, Y, W, H);

    double x0 = X + PX, y = Y + 18;
    ui_text(g, &T->headFont, T->nTitle, x0, y + (hh - 10 - T->headRuleW - th) / 2, 0, 0, T->ink, NULL);
    /* clear all */
    col cf = nnotes ? T->ink : IL_DIM;
    double cw = lbl(g, "Clear all", 0, 0, 0, 0, &ch);
    double cx = x0 + CW - cw, cy = y + (hh - 10 - T->headRuleW) / 2;
    lbl(g, "Clear all", cx, cy - ch / 2, 0, cf, NULL);
    ui_icon(g, "edit-clear-all-symbolic", 20, cx - 6 - 10, cy, cf);
    if (nnotes) hit(cx - 6 - 20 - 8, y, cw + 34 + 8, hh - T->headRuleW, A_N_CLEAR, 0, 0);
    ui_hline(g, x0, y + hh - T->headRuleW, CW, T->headRuleW, T->headRuleC, 0);
    y += hh;
    for (int i = 0; i < shown; i++) {
        const Notice *n = &notes[i];
        double iy = y + 14;
        ui_box(g, x0, iy, 44, 44, &T->btn);
        if (!ui_icon(g, notice_icon(n), 22, x0 + 22, iy + 22, T->ink))
            ui_icon(g, "dialog-information-symbolic", 22, x0 + 22, iy + 22, T->ink);
        char when[32], meta[160];
        notice_when(n->t, when, sizeof when);
        App *na = notice_app(n->entry, n->app);
        snprintf(meta, sizeof meta, "%s · %s", na ? na->name : n->app[0] ? n->app : "Notice", when);
        lbl(g, meta, x0 + tx - PX, iy, 0, T->lblC, NULL);
        double ty = iy + lh + 3;
        if (n->title[0]) ty += ui_text_wrap(g, &T->nTitleFont, n->title, x0 + tx - PX, ty, tw, 2, 0, T->ink) + 3;
        if (n->body[0]) ui_text_wrap(g, &T->nBodyFont, n->body, x0 + tx - PX, ty, tw, 4, T->nBodyLh, T->ink);
        hit(x0, y, CW - 44 - 7, ih[i], A_N_OPEN, (int)n->id, 0);
        ui_icon(g, "window-close-symbolic", 22, x0 + CW - 22, iy + 22, T->ink);
        hit(x0 + CW - 44 - 7, y, 44 + 7, ih[i], A_N_DISMISS, (int)n->id, 0);
        y += ih[i];
        if (i < shown - 1) ui_hline(g, x0, y - T->sepW, CW, T->sepW, T->sepC, T->sepDashed);
    }
    if (!nnotes) {
        ui_text(g, &T->dateFont, "Nothing new", x0 + CW / 2, y + 34, 1, 0, T->sub, NULL);
        y += list_h;
    }
    char foot[64];
    if (shown < nnotes) snprintf(foot, sizeof foot, "%d more · tap a notice to open it", nnotes - shown);
    else snprintf(foot, sizeof foot, "Tap a notice to open it");
    ui_text(g, &hf, foot, x0 + CW / 2, y + 10, 1, 0, T->sub, NULL);
}

/* ---- apps ------------------------------------------------------------------- */
static const char *cat_icon(const char *cat)
{
    static const struct { const char *c, *i; } m[] = {
        { "Office", "x-office-document-symbolic" }, { "Reading", "accessories-dictionary-symbolic" },
        { "Multimedia", "applications-multimedia-symbolic" }, { "Games", "applications-games-symbolic" },
        { "Education", "applications-science-symbolic" }, { "Internet", "applications-internet-symbolic" },
        { "System", "applications-system-symbolic" },
    };
    for (unsigned i = 0; i < sizeof m / sizeof *m; i++) if (!strcmp(m[i].c, cat)) return m[i].i;
    return "application-x-executable-symbolic";
}

/* a ‹ › pager button */
static void pg_btn(Gc *g, double x, double y, double sz, int next, int on, int act, int arg)
{
    ui_box(g, x, y, sz, sz, &T->btn);
    ui_icon(g, next ? "go-next-symbolic" : "go-previous-symbolic", 24, x + sz / 2, y + sz / 2, on ? T->ink : IL_DIM);
    if (on) hit(x - 6, y - 6, sz + 12, sz + 12, act, arg, 0);
}

/* ---- divider mode ----------------------------------------------------------- */
static void pictogram(Gc *g, int kind, double cx, double cy, col c)
{
    double lw = 1.5;
    switch (kind) {
    case 0: /* rows */
        ui_stroke_rrect(g, cx - 12, cy - 13, 24, 12, 2, lw, c, 0);
        ui_stroke_rrect(g, cx - 12, cy + 1, 24, 12, 2, lw, c, 0);
        break;
    case 1: /* side */
        ui_stroke_rrect(g, cx - 13, cy - 12, 12, 24, 2, lw, c, 0);
        ui_stroke_rrect(g, cx + 1, cy - 12, 12, 24, 2, lw, c, 0);
        break;
    }
}

/* two arrows along the way Swap exchanges (the design's swap_vert, swap_horiz) */
static void swap_glyph(Gc *g, double cx, double cy, int horiz, col c)
{
    if (!g->paint) return;
    cairo_t *cr = g->cr;
    const double L = 9, O = 4.5, H = 4;
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    if (horiz) cairo_rotate(cr, M_PI / 2);
    cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
    cairo_set_line_width(cr, 2);
    cairo_move_to(cr, -O, L); cairo_line_to(cr, -O, -L);
    cairo_move_to(cr, -O - H, -L + H); cairo_line_to(cr, -O, -L); cairo_line_to(cr, -O + H, -L + H);
    cairo_move_to(cr, O, -L); cairo_line_to(cr, O, L);
    cairo_move_to(cr, O - H, L - H); cairo_line_to(cr, O, L); cairo_line_to(cr, O + H, L - H);
    cairo_stroke(cr);
    cairo_restore(cr);
}

/* the crossing toggle's glyph (26 px): the through-line solid, the other axis
 * as two short stubs off each other (they are separate lines); horiz: the
 * through-line runs left to right */
static void cross_glyph(Gc *g, double cx, double cy, int horiz, col c)
{
    double x = cx - 13, y = cy - 13;
    if (horiz) {
        ui_rrect(g, x, y + 11.5, 26, 3, 1.5, c);
        ui_rrect(g, x + 8, y + 1, 1.5, 8, .75, c);
        ui_rrect(g, x + 16.5, y + 17, 1.5, 8, .75, c);
    } else {
        ui_rrect(g, x + 11.5, y, 3, 26, 1.5, c);
        ui_rrect(g, x + 1, y + 8, 8, 1.5, .75, c);
        ui_rrect(g, x + 17, y + 16.5, 8, 1.5, .75, c);
    }
}

/* a divider button: btn with an ink edge; round: the crossing toggle */
static void div_btn(Gc *g, double x, double y, double sz, int round_, int act, int arg, WinId w)
{
    Box b = T->btn;
    b.bw = 1.5; b.bc = T->ink;
    if (round_) for (int k = 0; k < 4; k++) b.r[k] = HALF;
    bound(x, y, sz, sz, 4);
    ui_box(g, x, y, sz, sz, &b);
    hit(x, y, sz, sz, act, arg, w);
}

/* shell.divider: Swap on every divider at its middle (the main one a quarter
 * along, away from where the inner lines meet it); on a two-window screen
 * Direction next to it; on a 2 x 2 screen the round crossing toggle on the
 * through-line where the separate lines would meet. No menu; they stay until
 * a divider is tapped again, a tap anywhere else or Escape. */
static void view_div(Gc *g)
{
    const double BS = 52, BG = 8, CS = 44;
    int main_i = -1, ninner = 0;
    double cross = 0;                   /* where the inner lines meet the main one (along it, logical px) */
    for (int i = 0; i < st.ndv; i++) if (st.dv[i].level == 0 && main_i < 0) main_i = i;
    if (main_i >= 0)
        for (int i = 0; i < st.ndv; i++) {
            const typeof(st.dv[0]) *d = &st.dv[i];
            if (d->level == 0) continue;
            cross += st.dv[main_i].vert ? (d->y + d->h / 2.0) / S : (d->x + d->w / 2.0) / S;
            ninner++;
        }
    if (ninner) cross /= ninner;
    for (int i = 0; i < st.ndv; i++) {
        const typeof(st.dv[0]) *d = &st.dv[i];
        double x = d->x / S, y = d->y / S, w = d->w / S, h = d->h / S;
        double f = .5;
        if (d->level == 0 && ninner) {
            double c = d->vert ? (cross - y) / h : (cross - x) / w;
            f = c < .5 ? .75 : .25;
        }
        double cx = d->vert ? x + w / 2 : x + w * f, cy = d->vert ? y + h * f : y + h / 2;
        int two = st.dv_n == 2 && d->level == 0;
        double bw = two ? 2 * BS + BG : BS, bx = cx - bw / 2, by = cy - BS / 2;
        div_btn(g, bx, by, BS, 0, A_DIV_SWAP, i, d->a);
        swap_glyph(g, bx + BS / 2, by + BS / 2, d->vert, T->ink);
        if (two) {
            double ex = bx + BS + BG;
            div_btn(g, ex, by, BS, 0, A_DIV_DIR, i, d->a);
            pictogram(g, d->vert ? 0 : 1, ex + BS / 2, by + BS / 2, T->ink);   /* the arrangement it switches to */
        }
        if (d->level == 0 && st.dv_n == 4 && ninner == 2) {
            double tx = d->vert ? x + w / 2 : cross, ty = d->vert ? cross : y + h / 2;
            div_btn(g, tx - CS / 2, ty - CS / 2, CS, 1, A_DIV_CROSS, i, d->a);
            cross_glyph(g, tx, ty, !d->vert, T->ink);
        }
    }
}

/* the dividers under their buttons: only the buttons of the popup take input and the
 * catch-all leaves the gaps free, so a tap (closes) or a drag (resizes, the buttons
 * stay and follow) reaches the handle itself */
static void div_input(void)
{
    int gaps[4][4], btn[12][4], ng = 0, nb = 0;
    if (st.view == V_DIV) {
        for (int i = 0; i < st.ndv; i++) { gaps[ng][0] = st.dv[i].x; gaps[ng][1] = st.dv[i].y; gaps[ng][2] = st.dv[i].w; gaps[ng][3] = st.dv[i].h; ng++; }
        for (int i = 0; i < pop.nhit && nb < 12; i++) {
            const Hit *h = &pop.hit[i];
            if (h->act != A_DIV_SWAP && h->act != A_DIV_DIR && h->act != A_DIV_CROSS) continue;
            btn[nb][0] = (int)(h->x * S); btn[nb][1] = (int)(h->y * S); btn[nb][2] = (int)(h->w * S); btn[nb][3] = (int)(h->h * S);
            nb++;
        }
    }
    be_grab_holes(gaps, ng);
    be_pop_input(btn, nb);
}

static void view_pop(Gc *g)
{
    switch (st.view) {
    case V_QS: view_qs(g); break;
    case V_WS: view_ov(g); break;
    case V_NOTIF: view_notif(g); break;
    case V_DIV: view_div(g); break;
    case V_PWR: view_power(g); break;
    }
    if (st.focus >= 0 && g->paint) {    /* the keyboard's ring: 2 px, 2 px out */
        int k = 0;
        for (int i = 0; i < pop.nhit; i++) {
            const Hit *h = &pop.hit[i];
            if (h->act == A_NONE) continue;
            if (k++ == st.focus) { ui_stroke_rrect(g, h->x - 3, h->y - 3, h->w + 6, h->h + 6, 3, 2, T->ink, 0); break; }
        }
    }
}

/* ---- resizing: the snap rail (the dragged bar itself is all else: design
 * "Dragging the divider shows only the line") --------------------------------- */
static void view_rail(Gc *g)
{
    double top = st.dr_a0 / S, len = st.dr_alen / S, rx = LW - 10 - 6;
    const char *L[3] = { T->frac ? "⅓" : "1/3", T->frac ? "½" : "1/2", T->frac ? "⅔" : "2/3" };
    double f[3] = { 1 / 3.0, .5, 2 / 3.0 };
    bound(rx - 60, top + 16, 66, len - 32, 0);
    ui_rrect(g, rx, top + 16, 6, len - 32, 3, T->ink);
    for (int i = 0; i < 3; i++) {
        double y = top + f[i] * len, th, tw = ui_text_w(g, &T->snapFont, L[i], &th);
        ui_fill_rect(g, rx - 10 - tw - 6, y - th / 2, tw + 6, th, T->screenBg);
        ui_text(g, &T->snapFont, L[i], rx - 13, y - th / 2, 2, 0, T->ink, NULL);
        ui_fill_rect(g, rx, y - .75, 6, 1.5, T->screenBg);
    }
}

/* ============================================================================
 * input
 * ========================================================================== */
static void render_pop(void)
{
    if (st.view == V_IDLE) { surf_hide(&pop); return; }
    surf_render(&pop, view_pop);
    if (!pop.grabbed && pop.mapped) {
        /* everything to the popup: a tap outside closes it, the bar still works */
        if (be_grab_pointer(&pop))
            pop.grabbed = 1;
        else LOG("pointer grab failed");
    }
}

static void render_bar(void) { surf_render(&bar, view_bar); }
static void notices_changed(void) { render_bar(); if (st.view == V_NOTIF) render_pop(); }
static void render_all(void) { render_bar(); if (st.view != V_IDLE) render_pop(); }


static double refresh_due, press_long_due;

/* the bar's gestures. The backend reports when the finger really goes down and
 * up (ev_raw) besides the press and release: a raw press starts a gesture (seq), the bar
 * acts at most once per gesture, and a workspace button acts only at the raw
 * lift -- HOLD_MS after a raw press still down, a gesture that pressed a
 * workspace button is a hold: the workspaces open, no tap follows. */
static double ws_hold_due;
static struct {
    int raw, down, held, pending;   /* raw: raw events arrive; pending: a workspace tap waits for the lift */
    unsigned seq, acted, ws;        /* the gesture; the last one the bar acted in; the last to press a ws button */
    Hit tap;
    double tap_x;
} gest;

/* the front light as power.conf / glassine-powerd's schedule have it now (others
 * change it: glassine-settings, glassine-powerd's managed light, a brightness key) */
static void light_read(void)
{
    int br = conf_get(POWER_CONF, "light_br", 0), ct = conf_get(POWER_CONF, "light_ct", 12);
    char p[160];
    snprintf(p, sizeof p, BL, "br", "max_brightness");
    st.br_max = read_int(p, 28);
    snprintf(p, sizeof p, BL, "ct", "max_brightness");
    st.ct_max = read_int(p, 24);
    st.light = (int)lround(br * 10.0 / st.br_max) * 10;
    st.warm = (int)lround(ct * 10.0 / st.ct_max) * 10;
    st.sched[0] = conf_get(LIGHT_RUN, "b", -1);
    st.sched[1] = conf_get(LIGHT_RUN, "w", -1);
    st.managed = conf_get(POWER_CONF, "light_managed", 0) && st.sched[0] >= 0 && st.sched[1] >= 0;
    st.off[0] = conf_get(POWER_CONF, "light_b_off", 0);
    st.off[1] = conf_get(POWER_CONF, "light_w_off", 0);
    st.glow = conf_get(POWER_CONF, "light_glow", 1);
    if (st.managed) light_managed_now();
}

/* "light up|down|warmer|cooler" (the brightness keys): one 10 % step
 * of what the quick settings' slider shows, saved as a slider tap saves it (a
 * managed light: the user's offset from the schedule, which glassine-powerd follows) */
static void light_step(const char *how)
{
    int i = !strcmp(how, "warmer") || !strcmp(how, "cooler");
    int d = !strcmp(how, "up") || !strcmp(how, "warmer") ? 10 : !strcmp(how, "down") || !strcmp(how, "cooler") ? -10 : 0;
    if (!d) return;
    light_read();
    if (st.managed) {
        int o = st.off[i] + d;
        st.off[i] = o < -50 ? -50 : o > 50 ? 50 : o;
        char c[16];
        snprintf(c, sizeof c, "%d", st.off[i]);
        conf_set(POWER_CONF, i ? "light_w_off" : "light_b_off", c);
        light_managed_now();
        light_hw((int)lround(st.light * st.br_max / 100.0), (int)lround(st.warm * st.ct_max / 100.0));
    } else {
        int *v = i ? &st.warm : &st.light;
        *v = *v + d < 0 ? 0 : *v + d > 100 ? 100 : *v + d;
        set_light((int)lround(st.light * st.br_max / 100.0), (int)lround(st.warm * st.ct_max / 100.0));
    }
    LOG("light %s: %d %% warmth %d %%%s", how, st.light, st.warm, st.managed ? " (managed)" : "");
    if (st.view == V_QS) render_pop();
}

static void set_view(int v)
{
    if (v == st.view) v = V_IDLE;       /* the bar buttons toggle */
    if (v == V_WS && !pop.mapped) be_snap_fresh();  /* the visible screen's still, before the popup covers it */
    if (pop.grabbed) { be_ungrab_pointer(); pop.grabbed = 0; }
    st.view = v;
    st.opening = -1;
    if (v != V_WS) st.mv = 0, st.ov_page = -1;
    st.focus = -1;
    if (v == V_QS) {
        light_read();
        eink_range(&st.rlo, &st.rhi);
        rot_read();
        st.bt = bt_on();
        query_wifi();
        query_vol();
    }
    if (v == V_WS || v == V_DIV) be_read_desktop();
    LOG("view %d", v);
    render_bar();
    render_pop();
    div_input();
    if (v != V_IDLE) be_grab_keyboard(&pop);   /* Escape, Tab, Enter (a keyboard's; the panel has none) */
    else be_ungrab_keyboard();
}

/* run `cmd` detached (as run(cmd, NULL)); returns the pid of the sh that runs
 * it (it execs a single command: then that is the app's pid), 0 if unknown */
/* Launch boost (opt-in until measured and felt): /etc/glassine/launch-boost
 * holds "MIN MS", e.g. "1024 2500". The launched process starts with
 * uclamp.min MIN (inherited by its threads and children, so schedutil picks a
 * high OPP for the whole start instead of ramping up), and MS ms later a
 * helper resets it on every task of the process tree. Absent or unreadable:
 * no boost. The launch time to compare is the placeholder's "closed (N s)" in
 * the log (sway). */
#define LAUNCH_BOOST "/etc/glassine/launch-boost"
struct glassine_sched_attr {
    uint32_t size, sched_policy;
    uint64_t sched_flags;
    int32_t sched_nice;
    uint32_t sched_priority;
    uint64_t sched_runtime, sched_deadline, sched_period;
    uint32_t sched_util_min, sched_util_max;
};

static int set_umin(pid_t tid, int v)          /* policy and priority kept; -1 = reset */
{
    struct glassine_sched_attr a = { .size = sizeof a };
    a.sched_flags = 0x08 | 0x10 | 0x20;         /* KEEP_POLICY | KEEP_PARAMS | UTIL_CLAMP_MIN */
    a.sched_util_min = v < 0 ? (uint32_t)-1 : (uint32_t)(v > 1024 ? 1024 : v);
    return syscall(SYS_sched_setattr, tid, &a, 0);
}

static pid_t ppid_of(pid_t p)
{
    char path[48], b[512];
    snprintf(path, sizeof path, "/proc/%d/stat", (int)p);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    b[n] = 0;
    char *q = strrchr(b, ')');              /* comm may hold spaces */
    int pp = 0;
    return q && sscanf(q + 1, " %*c %d", &pp) == 1 ? pp : 0;
}

/* the boost off again on root and all its descendants' threads; twice, for a
 * child forked during the first walk. Returns the tasks reset. */
static int unboost_tree(pid_t root)
{
    int n = 0;
    for (int pass = 0; pass < 2; pass++) {
        DIR *d = opendir("/proc");
        struct dirent *e;
        while (d && (e = readdir(d))) {
            pid_t p = atoi(e->d_name), q = p;
            if (p <= 1) continue;
            for (int k = 0; q > 1 && q != root && k < 64; k++) q = ppid_of(q);
            if (q != root) continue;
            char tp[48];
            snprintf(tp, sizeof tp, "/proc/%d/task", (int)p);
            DIR *t = opendir(tp);
            struct dirent *te;
            while (t && (te = readdir(t)))
                if (atoi(te->d_name) > 0 && !set_umin(atoi(te->d_name), -1) && !pass) n++;
            if (t) closedir(t);
        }
        if (d) closedir(d);
    }
    return n;
}

static pid_t run_launch(const char *cmd)
{
    LOG("run: %s", cmd);
    int bmin = 0, bms = 0;
    FILE *bf = fopen(LAUNCH_BOOST, "r");
    if (bf) {
        if (fscanf(bf, "%d %d", &bmin, &bms) != 2 || bmin <= 0 || bms <= 0) bmin = 0;
        fclose(bf);
    }
    if (bmin) LOG("launch boost: uclamp.min %d for %d ms", bmin, bms);
    int pf[2];
    if (pipe2(pf, O_CLOEXEC)) { run(cmd, NULL); return 0; }
    pid_t p = fork(), g = 0;
    if (p == 0) {
        setsid();
        pid_t c = fork();
        if (c == 0) {
            int fd = open("/dev/null", O_RDWR);
            dup2(fd, 0); dup2(fd, 1);
            for (int i = 3; i < 256; i++) close(i);
            if (bmin && set_umin(0, bmin)) bmin = 0;
            execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
            _exit(127);
        }
        if (write(pf[1], &c, sizeof c) != sizeof c) {}
        if (bmin && c > 0 && fork() == 0) {     /* the helper: reparented when we exit */
            for (int i = 0; i < 256; i++) close(i);
            struct timespec ts = { bms / 1000, (bms % 1000) * 1000000L };
            nanosleep(&ts, NULL);
            unboost_tree(c);
            _exit(0);
        }
        _exit(0);
    }
    close(pf[1]);
    if (p > 0) {
        if (read(pf[0], &g, sizeof g) != sizeof g) g = 0;
        waitpid(p, NULL, 0);
    }
    close(pf[0]);
    return g > 0 ? g : 0;
}

/* a launch placeholder's tile (device px): the app's icon and name, at once
 * at the tap; the app takes the tile over when it maps (glassine-swayd) */
void ev_placeholder_draw(cairo_t *cr, int w, int h, const App *a)
{
    Gc g = { cr, 1, T };
    cairo_save(cr);
    cairo_scale(cr, S, S);
    double lw = w / S, lh = h / S, th = 0;
    ui_fill_rect(&g, 0, 0, lw, lh, T->screenBg);
    double cy = lh / 2 - 30;
    if (!ui_icon(&g, a->icon, 64, lw / 2, cy, T->ink)) ui_icon(&g, cat_icon(a->cat), 64, lw / 2, cy, T->ink);
    ui_text_w(&g, &T->headFont, a->name, &th);
    ui_text(&g, &T->headFont, a->name, lw / 2, cy + 48, 1, lw - 40, T->ink, NULL);
    ui_text(&g, &T->lbl, "starting …", lw / 2, cy + 48 + th + 8, 1, lw - 40, T->sub, NULL);
    cairo_restore(cr);
}

static void launch_app(App *a)
{
    char cmd[700] = "";
    size_t n = 0;
    for (const char *p = a->exec; *p && n < sizeof cmd - 2; p++) {
        if (*p == '%') { if (p[1] == '%') cmd[n++] = '%'; if (p[1]) p++; continue; }   /* field codes: no files */
        cmd[n++] = *p;
    }
    cmd[n] = 0;
    char full[800];
    if (a->terminal) snprintf(full, sizeof full, "foot %s", cmd);
    else snprintf(full, sizeof full, "%s", cmd);
    be_placeholder(a, run_launch(full));
    /* recent: this one first */
    char p[512], l[128], buf[4096];
    state_path(p, sizeof p, ".local/state", "recent");
    size_t k = snprintf(buf, sizeof buf, "%s\n", a->id);
    FILE *f = fopen(p, "r");
    int cnt = 1;
    while (f && fgets(l, sizeof l, f) && cnt < 16) {
        l[strcspn(l, "\n")] = 0;
        if (!strcmp(l, a->id) || !l[0]) continue;
        k += snprintf(buf + k, sizeof buf - k, "%s\n", l);
        cnt++;
    }
    if (f) fclose(f);
    f = fopen(p, "w");
    if (f) { fputs(buf, f); fclose(f); }
}

/* where the next new window goes, told to glassine-swayd before the app starts
 * (its launch placeholder takes it): the empty screen under Home if there is
 * one, else where (new: a screen of its own after this one, here: beside the
 * apps on this one; NULL: as glassine-swayd places it) */
static void launch_where(App *a, const char *where)
{
    if (st.home_vis && st.hole > 0) tile_cmd("next hole");
    else if (where && (!strcmp(where, "new") || !strcmp(where, "here"))) tile_cmd("next %s", where);
    launch_app(a);
}

/* Home shown or hidden (spec 001 FR-030, FR-031): glassine-swayd owns it */
static void home_toggle(void)
{
    if (st.view != V_IDLE) set_view(V_IDLE);
    tile_cmd("home toggle");
}

/* the screen button: Overview on or off (FR-003). Opening it leaves Home
 * (an empty screen under it goes); not while Home holds the only screen */
static void overview(void)
{
    if (st.view != V_WS) {
        be_read_desktop();
        if (st.home_vis && st.hole > 0 && ndesk <= 1) return;
        if (st.home_vis) tile_cmd("home hide");
    }
    st.mv = 0; st.ov_page = -1;
    set_view(V_WS);
}

/* an app that already has a window comes forward (minimized: back here)
 * instead of a second copy (KOReader ran twice on one book); else it starts */
static void open_app(App *a)
{
    be_read_desktop();
    /* several: one here first, then one on another workspace, a minimized one last */
    int best = -1, rank = 3;
    for (int k = 0; k < ncl; k++) {
        if (app_of(cl[k].w) != a) continue;
        int r = cl[k].desk == curdesk ? 0 : cl[k].desk >= 0 ? 1 : 2;
        if (r < rank) { rank = r; best = k; }
    }
    if (best >= 0) {
        LOG("open %s: its window %lu is there", a->id, (unsigned long)cl[best].w);
        if (cl[best].desk < 0) { be_restore(cl[best].w); return; }
        if (cl[best].desk != curdesk) be_goto_desk(cl[best].desk);
        be_activate(cl[best].w);
        return;
    }
    /* glassine-settings > Appearance > Windows: a workspace of its own (*; the default since design, the file
     * app-shared-workspace turns it off); an empty current one is used as it is */
    char shared[512];
    const char *home = getenv("HOME");
    snprintf(shared, sizeof shared, "%s/.config/glassine/app-shared-workspace", home ? home : "");
    int busy = 0;
    for (int k = 0; k < ncl; k++) if (cl[k].desk == curdesk) busy = 1;
    launch_where(a, access(shared, F_OK) && busy ? "new" : NULL);
}

/* a notice without a "default" action: bring its app forward (a window of
 * the app: its workspace + raise), or start it */
static void notice_raise(const char *entry, const char *app)
{
    App *a = notice_app(entry, app);
    be_read_desktop();
    for (int k = 0; k < ncl; k++) {
        App *ca = app_of(cl[k].w);
        if ((a && ca == a) || (app[0] && !strcasecmp(cl[k].name, app))) {
            if (cl[k].desk < 0) { be_restore(cl[k].w); return; }     /* minimized: back here */
            if (cl[k].desk != curdesk) be_goto_desk(cl[k].desk);
            be_activate(cl[k].w);
            return;
        }
    }
    if (a) launch_app(a);
}

static void act(const Hit *h, double rx)
{
    LOG("act %d/%d", h->act, h->arg);
    switch (h->act) {
    case A_BAR_HOME:
        if (st.view != V_IDLE) set_view(V_IDLE);
        tile_cmd("home toggle");        /* glassine-swayd shows or hides it (and keeps an empty screen's rule) */
        break;
    case A_BAR_WS:
        if (st.view != V_IDLE) set_view(V_IDLE);
        if (h->arg != curdesk) be_goto_desk(h->arg);
        break;
    case A_BAR_NOTIF: set_view(V_NOTIF); break;
    case A_BAR_CHG:
        if (st.view != V_IDLE) set_view(V_IDLE);
        run("/usr/bin/glassine-settings packages", NULL);
        raise_settings();
        break;
    case A_BAR_QS: set_view(V_QS); break;
    case A_BAR_KBD: run("/usr/bin/glassine-osk", NULL); break;
    case A_BAR_REFRESH:
        /* close first: the refresh should clean the screen without the popup */
        if (st.view != V_IDLE) set_view(V_IDLE);
        refresh_due = now_ms() + 250;
        break;
    case A_BACKDROP: set_view(V_IDLE); break;
    case A_QS_TITLE: {
        int i = 0;
        while (i < ui_nvoices && ui_voices[i] != T) i++;
        theme_set(ui_voices[(i + 1) % ui_nvoices]->name, 1);
        render_all();
        break;
    }
    case A_QS_TOGGLE:
        if (h->arg == 0) {
            st.wifi_busy = 1;
            run(st.wifi ? "nmcli radio wifi off" : "nmcli radio wifi on", wifi_set_done);
        } else if (h->arg == 1) {
            st.bt_busy = 1;
            char c[400];
            snprintf(c, sizeof c, "gdbus call --system --dest org.blueman.Mechanism --object-path /org/blueman/mechanism "
                     "--method org.blueman.Mechanism.SetRfkillState %s", bt_on() ? "false" : "true");
            run(c, bt_done);
        } else if (h->arg == 2) {
            st.mute = !st.mute;
            run("pactl set-sink-mute @DEFAULT_SINK@ toggle", vol_set_done);
        } else {
            set_view(V_IDLE);
            refresh_due = now_ms() + 250;
            return;
        }
        render_pop();
        break;
    case A_QS_SLIDER: {
        int v = (int)lround((rx - slider_geo[h->arg].x) / slider_geo[h->arg].w * 10) * 10;
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        if (h->arg == 2) {
            st.vol = v;
            char c[96];
            snprintf(c, sizeof c, "pactl set-sink-volume @DEFAULT_SINK@ %d%%", v);
            run(c, vol_set_done);
        } else if (st.managed) {        /* the offset from the schedule, steps of 5 */
            int o = (int)lround(((rx - slider_geo[h->arg].x) / slider_geo[h->arg].w * 100 - 50) / 5) * 5;
            st.off[h->arg] = o < -50 ? -50 : o > 50 ? 50 : o;
            char c[16];
            snprintf(c, sizeof c, "%d", st.off[h->arg]);
            conf_set(POWER_CONF, h->arg ? "light_w_off" : "light_b_off", c);   /* glassine-powerd follows */
            light_managed_now();
            light_hw((int)lround(st.light * st.br_max / 100.0), (int)lround(st.warm * st.ct_max / 100.0));
        } else {
            if (h->arg == 0) st.light = v; else st.warm = v;
            set_light((int)lround(st.light * st.br_max / 100.0), (int)lround(st.warm * st.ct_max / 100.0));
        }
        render_pop();
        break;
    }
    case A_QS_FLUID: if (h->arg != fluid_of(st.rhi)) set_fluid(h->arg); render_pop(); break;
    case A_QS_SETTINGS:
        st.opening = OPENING_ALL;
        st.opening_t = now_ms();
        run("/usr/bin/glassine-settings", NULL);
        raise_settings();
        render_pop();
        break;
    case A_QS_SLABEL: break;             /* hold only */
    case A_QS_PWR: set_view(V_PWR); break;
    case A_P_CANCEL: set_view(V_IDLE); break;
    case A_P_ACT: {                     /* one tap: the popup is the confirmation */
        char c[128];
        set_view(V_IDLE);               /* first: Sleep's banner must not show the popup */
        snprintf(c, sizeof c, "doas /usr/sbin/glassine-power-root %s", PWR[h->arg].what);
        run(c, NULL);
        break;
    }
    case A_QS_ROT:
        if (h->arg == 4) {              /* Auto: off locks the turn it is at now */
            char v[8];
            st.rot_auto = !st.rot_auto;
            st.rot_lock = st.rot_cur;
            snprintf(v, sizeof v, "%d", st.rot_cur);
            rot_set(st.rot_auto ? "auto" : v);
        } else {
            char v[8];
            st.rot_auto = 0;
            st.rot_lock = h->arg * 90;
            snprintf(v, sizeof v, "%d", st.rot_lock);
            rot_set(v);
        }
        render_pop();
        break;
    case A_WS_GOTO: be_goto_desk(h->arg); set_view(V_IDLE); break;
    case A_WS_RESTORE: be_restore(h->win); set_view(V_IDLE); break;
    case A_BAR_OV: overview(); break;
    case A_OV_X: set_view(V_IDLE); break;
    case A_OV_PICK: st.mv = h->win; st.ov_page = -1; render_pop(); break;
    case A_OV_CANCEL: st.mv = 0; st.ov_page = -1; render_pop(); break;
    case A_OV_PUT:
        tile_cmd("place %lx %lx %s", st.mv, h->win, SIDE_W[h->arg & 3]);
        st.mv = 0; st.ov_page = -1;
        render_pop();
        break;
    case A_OV_NEW: be_new_screen(st.mv, 0); st.mv = 0; st.ov_page = -1; render_pop(); break;
    case A_OV_CLOSEAPP: be_close(st.mv); st.mv = 0; st.ov_page = -1; render_pop(); break;
    case A_OV_FULL: tile_cmd("full %lx", st.mv); st.mv = 0; st.ov_page = -1; render_pop(); break;
    case A_SCR_PREV: case A_SCR_NEXT: {    /* the page follows the screen (FR-013) */
        int to = h->arg + (h->act == A_SCR_NEXT ? 1 : -1);
        tile_cmd("scrmove %d %d", h->arg + 1, h->act == A_SCR_NEXT ? 1 : -1);
        st.ov_page = to / 4;
        render_pop();
        break;
    }
    case A_SCR_CLOSE: tile_cmd("scrclose %d", h->arg + 1); break;
    case A_OV_PREV: case A_OV_NEXT: ov_turn_page(h->act == A_OV_NEXT ? 1 : -1); break;
    case A_N_CLEAR: notify_clear(); render_bar(); render_pop(); break;
    case A_N_DISMISS: notify_dismiss((unsigned)h->arg); render_bar(); render_pop(); break;
    case A_N_OPEN: {
        char entry[96] = "", app[64] = "";
        int def = 0;
        for (int i = 0; i < nnotes; i++)
            if (notes[i].id == (unsigned)h->arg) {
                def = notes[i].has_default;
                snprintf(entry, sizeof entry, "%s", notes[i].entry);
                snprintf(app, sizeof app, "%s", notes[i].app);
            }
        notify_invoke((unsigned)h->arg);
        set_view(V_IDLE);
        if (!def) notice_raise(entry, app);
        break;
    }
    case A_DIV_SWAP:                    /* the buttons stay: several swaps take one tap each */
        tile_cmd("%s %lx", st.dv[h->arg].level == 0 ? "swaphalves" : "swapin", h->win);
        break;
    case A_DIV_DIR: tile_cmd("direction %lx", h->win); break;
    case A_DIV_CROSS: tile_cmd("cross %lx", h->win); break;
    }
}

static const Hit *hit_at(Surf *s, double x, double y)
{
    if (!s->mapped) return NULL;
    for (int i = s->nhit - 1; i >= 0; i--) {
        const Hit *h = &s->hit[i];
        if (h->act != A_NONE && x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h) return h;
    }
    return NULL;
}

/* glassine-settings is one window (a GApplication): a second start only turns its
 * page, and on Wayland it can't bring itself forward (no activation token), so
 * the shell does: its workspace + focus (minimized: back here). Not running: the
 * new window opens here anyway (a held drawer app didn't
 * reach the open Settings) */
static void raise_settings(void)
{
    be_read_desktop();
    for (int k = 0; k < ncl; k++) {
        const App *ap = app_of(cl[k].w);
        if (!ap || strcmp(ap->id, "glassine-settings")) continue;
        if (cl[k].desk < 0) { be_restore(cl[k].w); return; }
        if (cl[k].desk != curdesk) be_goto_desk(cl[k].desk);
        be_activate(cl[k].w);
        return;
    }
}

/* a target held: a quick-settings toggle or
 * slider label (its settings app: the hint says so for a second, then the card
 * goes), a bar workspace button (the workspaces, no switch) */
static void held(int a, int arg)
{
    LOG("held %d/%d", a, arg);
    if (a == A_QS_TOGGLE || a == A_QS_SLABEL) {
        st.opening = a == A_QS_SLABEL ? 4 : arg;
        st.opening_t = now_ms();
        run(SETTINGS[st.opening].cmd, NULL);
        raise_settings();
        press.s = NULL;             /* no inverted target: the hint is the feedback */
        render_pop();
    } else if (a == A_BAR_WS) {
        if (press.s == &bar) { press.s = NULL; render_bar(); }
        if (st.view != V_WS) set_view(V_WS);
    }
}

/* a right click (a mouse's right button) = a long press */
void ev_long_press(int rx_dev, int ry_dev)
{
    double x = rx_dev / S, y = ry_dev / S;
    Surf *s = y < BH ? &bar : &pop;
    if (s == &pop && !pop.mapped) return;
    const Hit *h = hit_at(s, x, y);
    LOG("right click %.0f,%.0f %s: hit %d/%d", x, y, s == &bar ? "bar" : "pop", h ? h->act : -1, h ? h->arg : -1);
    if (!h) return;
    int a = h->act;
    if ((s == &pop && (a == A_QS_TOGGLE || a == A_QS_SLABEL)) || (s == &bar && a == A_BAR_WS)) held(a, h->arg);
}

static void bar_act(const Hit *c, double x)
{
    if (gest.raw && gest.acted == gest.seq) { LOG("bar: gesture %u already acted", gest.seq); return; }
    if (gest.raw && gest.down && c->act == A_BAR_WS) {     /* a tap or a hold: the lift decides */
        gest.tap = *c; gest.tap_x = x; gest.pending = 1;
        return;
    }
    gest.acted = gest.seq;
    act(c, x);
}

void ev_pointer(int down, int rx_dev, int ry_dev)
{
    double x = rx_dev / S, y = ry_dev / S;
    Surf *s = y < BH ? &bar : &pop;
    const Hit *h = hit_at(s, x, y);
    static double sw_x, sw_y;           /* the overview: where the press was (a sideways swipe turns its page) */
    static int sw_on;
    if (down) { sw_x = x; sw_y = y; sw_on = st.view == V_WS && s == &pop; }
    else if (sw_on && st.view == V_WS && fabs(x - sw_x) > 90 && fabs(x - sw_x) > 2 * fabs(y - sw_y)) {
        sw_on = 0;
        press.s = NULL; press.down = 0; press_long_due = 0;
        LOG("overview: swipe %.0f", x - sw_x);
        ov_turn_page(x < sw_x ? 1 : -1);
        return;
    }
    if (down) {
        LOG("press %.0f,%.0f %s: hit %d/%d t=%.0f", x, y, s == &bar ? "bar" : "pop", h ? h->act : -1, h ? h->arg : -1, now_ms());
        press.down = 1;
        if (!h && st.view != V_IDLE && s == &pop && !(x >= pop.x / S && x < (pop.x + pop.wd) / S &&
                                                      y >= pop.y / S && y < (pop.y + pop.ht) / S)) {
            press.s = NULL; press.act = A_BACKDROP; press.consumed = 0;
            return;
        }
        if (!h) { press.s = NULL; press.act = A_NONE; return; }
        press.s = s; press.act = h->act; press.arg = h->arg; press.win = h->win;
        press.x = h->x; press.y = h->y; press.w = h->w; press.h = h->h; press.consumed = 0;
        press.t = now_ms();
        press_long_due = h->act == A_QS_TOGGLE || h->act == A_QS_SLABEL ? press.t + HOLD_MS : 0;
        if (s == &bar && h->act == A_BAR_WS) gest.ws = gest.seq;
        if (h->act != A_QS_SLIDER && h->act != A_QS_SLABEL) {
            if (s == &bar) render_bar(); else render_pop();
        }
        return;
    }
    /* release */
    Surf *ps = press.s;
    int pact = press.act, parg = press.arg, consumed = press.consumed;
    WinId pwin = press.win;
    int wasdown = press.down;
    press.s = NULL;
    press.down = 0;
    press_long_due = 0;
    LOG("release %.0f,%.0f %s: hit %d/%d win %lx, pressed %d/%d win %lx%s", x, y, s == &bar ? "bar" : "pop",
        h ? h->act : -1, h ? h->arg : -1, h ? h->win : 0, pact, parg, pwin, consumed ? " consumed" : wasdown ? "" : " (no press)");
    if (ps) { if (ps == &bar) render_bar(); else if (ps->mapped) render_pop(); }
    if (pact == A_BACKDROP) {
        if (!h && s == &pop && st.view != V_IDLE) set_view(V_IDLE);
        return;
    }
    if (!h || consumed || h->act != pact || h->arg != parg || h->win != pwin) return;
    Hit c = *h;
    if (s == &bar) bar_act(&c, x); else act(&c, x);
}

/* (the e-ink range slider was the only drag; its fluidity tiles are taps) */
void ev_motion(int rx_dev, int ry_dev) { (void)rx_dev; (void)ry_dev; }

void ev_raw(int down, int up)
{
    gest.raw = 1;
    if (down && !gest.down) {
        gest.seq++; gest.down = 1; gest.held = 0; gest.pending = 0;
        ws_hold_due = now_ms() + HOLD_MS;
        LOG("gesture %u t=%.0f", gest.seq, now_ms());
    } else if (up && gest.down) {
        gest.down = 0;
        ws_hold_due = 0;
        LOG("gesture %u lift t=%.0f", gest.seq, now_ms());
        if (gest.pending && !gest.held) {
            gest.pending = 0;
            gest.acted = gest.seq;
            Hit c = gest.tap;
            act(&c, gest.tap_x);
        }
    }
}

static void ws_hold_check(void)
{
    ws_hold_due = 0;
    if (!gest.down || gest.ws != gest.seq) return;
    gest.held = 1; gest.pending = 0; gest.acted = gest.seq;
    held(A_BAR_WS, 0);
}

/* the popup's targets, for the keyboard */
static int focus_count(void)
{
    int k = 0;
    for (int i = 0; i < pop.nhit; i++) k += pop.hit[i].act != A_NONE;
    return k;
}
static const Hit *focus_hit(void)
{
    int k = 0;
    for (int i = 0; i < pop.nhit; i++)
        if (pop.hit[i].act != A_NONE && k++ == st.focus) return &pop.hit[i];
    return NULL;
}

void ev_key(int k, const char *b, int n)
{
    if (st.view == V_IDLE) return;
    if (k == SK_ESC) {                  /* the top one: a move, then the panel (FR-050) */
        if (st.mv) { st.mv = 0; st.ov_page = -1; st.focus = -1; render_pop(); }
        else set_view(V_IDLE);
    } else if (k == SK_TAB || k == SK_BACKTAB) {
        int c = focus_count();
        if (!c) return;
        st.focus = k == SK_TAB ? (st.focus + 1) % c : st.focus <= 0 ? c - 1 : st.focus - 1;
        render_pop();
    } else if (k == SK_ENTER || (n == 1 && b[0] == ' ')) {
        const Hit *h = focus_hit();
        if (h) { Hit c = *h; act(&c, c.x + c.w / 2); }
    } else if (k == SK_LEFT || k == SK_RIGHT) {     /* a slider moves by its step */
        const Hit *h = focus_hit();
        if (!h || h->act != A_QS_SLIDER) return;
        int sv[3] = { st.light, st.warm, st.vol }, i = h->arg, man = i < 2 && st.managed;
        double w = slider_geo[i].w, x = slider_geo[i].x;
        double now = man ? 50 + st.off[i] : sv[i], stp = man ? 5 : 10, to = now + (k == SK_LEFT ? -stp : stp);
        Hit c = *h;
        act(&c, x + w * (to < 0 ? 0 : to > 100 ? 100 : to) / 100);
    }
}

void ev_desktop(void)
{
    be_read_desktop();
    render_bar();
    if (st.view == V_WS) render_pop();
}

void ev_title(void) { render_bar(); }

/* ============================================================================
 * requests
 * ========================================================================== */
static void request(char *msg, int len)
{
    char *w[16];
    int n = 0;
    msg[len] = 0;
    for (char *p = msg; n < 16 && p < msg + len; p += strlen(p) + 1) w[n++] = p;
    if (!n) return;
    LOG("request %s", w[0]);
    if (!strcmp(w[0], "toggle") && n > 1) {
        /* "launcher": the sway session's gesture action of that name (Home replaced the launcher) */
        if (!strcmp(w[1], "home") || !strcmp(w[1], "launcher")) { home_toggle(); return; }
        if (!strcmp(w[1], "overview") || !strcmp(w[1], "workspaces")) { overview(); return; }
        int v = !strcmp(w[1], "quick") ? V_QS : !strcmp(w[1], "notices") ? V_NOTIF : !strcmp(w[1], "power") ? V_PWR : V_IDLE;
        set_view(v);
    } else if (!strcmp(w[0], "home")) {
        if (n == 1) { home_toggle(); return; }  /* the bar's button, the gesture */
        /* glassine-swayd: "home VISIBLE HOLE" on every change */
        st.home_vis = atoi(w[1]) != 0;
        st.hole = n > 2 ? atoi(w[2]) : 0;
        LOG("home %d hole %d", st.home_vis, st.hole);
        render_bar();
    } else if (!strcmp(w[0], "close")) {
        if (st.view != V_IDLE) set_view(V_IDLE);
    } else if (!strcmp(w[0], "theme") && n > 1) {
        theme_set(w[1], 1);
        render_all();
    } else if ((!strcmp(w[0], "divider-tap") || !strcmp(w[0], "dividers")) && n >= 2) {
        /* glassine-swayd: "N,ORIENT0" then "x,y,w,h,vert,level,a,b,pos" per divider
         * (level 0 main, 1/2 the inner line of half 0/1; pos: a fraction) */
        st.ndv = 0;
        sscanf(w[1], "%d,%d", &st.dv_n, &st.dv_o);
        for (int i = 2; i < n && st.ndv < 4; i++) {
            typeof(st.dv[0]) *d = &st.dv[st.ndv];
            d->pos = .5;
            if (sscanf(w[i], "%d,%d,%d,%d,%d,%d,%lx,%lx,%lf", &d->x, &d->y, &d->w, &d->h, &d->vert, &d->level, &d->a, &d->b, &d->pos) >= 8)
                st.ndv++;
        }
        if (w[0][7] == '-') { if (st.ndv) set_view(V_DIV); }    /* the tap: open, or (again) close */
        else if (st.view == V_DIV) { if (st.ndv) { render_pop(); div_input(); } else set_view(V_IDLE); }
    } else if (!strcmp(w[0], "change")) {    /* a system change: "change PID 3/12"; "change" alone: over */
        st.chg_pid = n >= 3 ? atoi(w[1]) : 0;
        snprintf(st.chg_label, sizeof st.chg_label, "%s", n >= 3 ? w[2] : "");
        render_bar();
    } else if (!strcmp(w[0], "drag") && n >= 7) {
        st.dr_vert = atoi(w[1]); st.dr_pos = atoi(w[2]); st.dr_a0 = atoi(w[3]); st.dr_alen = atoi(w[4]);
        st.dr_c0 = atoi(w[5]); st.dr_clen = atoi(w[6]);
        if (st.dr_alen <= 0) return;
        if (st.view != V_IDLE && st.view != V_DIV) set_view(V_IDLE);    /* (the divider's buttons stay: a drag resizes) */
        if (!st.drag && !st.dr_vert) surf_render(&rail, view_rail);
        st.drag = 1;
    } else if (!strcmp(w[0], "status")) {    /* glassine-settings changed the time zone or 12/24 h: show it now */
        read_status();
        render_bar();
    } else if (!strcmp(w[0], "launch") && n > 1) {    /* "launch ID [new|here]" (Home's taps) */
        App *a = notice_app(w[1], "");
        if (a) launch_where(a, n > 2 ? w[2] : NULL);
        else LOG("launch %s: no such app", w[1]);
    } else if (!strcmp(w[0], "open") && n > 1) {    /* as a drawer tap: its window if there is one */
        App *a = notice_app(w[1], "");
        if (a) open_app(a);
        else LOG("open %s: no such app", w[1]);
    } else if (!strcmp(w[0], "light") && n > 1) {
        light_step(w[1]);
    } else if (!strcmp(w[0], "verbose")) {
        verbose = !verbose;
        fprintf(stderr, "glassine-shell: verbose %s\n", verbose ? "on" : "off");
    } else if (!strcmp(w[0], "dragend")) {
        st.drag = 0;
        surf_hide(&rail);
    }
}

static int sock_path(char *out, size_t n)
{
    const char *d = getenv("XDG_RUNTIME_DIR");
    return snprintf(out, n, "%s/glassine-shell", d && *d ? d : "/tmp") < (int)n;
}

static int send_request(int argc, char **argv)
{
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    if (!sock_path(a.sun_path, sizeof a.sun_path)) return 1;
    char msg[1024];
    size_t n = 0;
    for (int i = 0; i < argc; i++) {
        size_t l = strlen(argv[i]) + 1;
        if (n + l > sizeof msg) return 1;
        memcpy(msg + n, argv[i], l);
        n += l;
    }
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    int r = sendto(fd, msg, n - 1, 0, (struct sockaddr *)&a, sizeof a) == (ssize_t)(n - 1) ? 0 : 1;
    if (r) fprintf(stderr, "glassine-shell: not running (%s)\n", a.sun_path);
    close(fd);
    return r;
}

/* ============================================================================
 * main
 * ========================================================================== */
static volatile sig_atomic_t quit;
static void on_term(int s) { (void)s; quit = 1; }

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "theme") && argc > 2) {
        char p[512];
        state_path(p, sizeof p, ".config", "theme");
        FILE *f = fopen(p, "w");
        if (f) { fprintf(f, "%s\n", ui_voice_find(argv[2])->name); fclose(f); }
        send_request(2, argv + 1);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "toggle") || !strcmp(argv[1], "close") || !strcmp(argv[1], "verbose") || !strcmp(argv[1], "home") ||
                     !strcmp(argv[1], "status") || !strcmp(argv[1], "launch") || !strcmp(argv[1], "open") ||
                     !strcmp(argv[1], "light")))
        return send_request(argc - 1, argv + 1);
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1;

    if (!be_init()) return 1;
    S = round((SW < SH ? SW : SH) / 702.0);   /* the panel's short side: the same in landscape */
    if (S < 1) S = 1;
    LW = SW / S; LH = SH / S;
    char tp[512], tn[32] = "white";
    state_path(tp, sizeof tp, ".config", "theme");
    FILE *tf = fopen(tp, "r");
    if (tf) { if (fscanf(tf, "%31s", tn) != 1) snprintf(tn, sizeof tn, "white"); fclose(tf); }
    theme_set(tn, 0);

    double t0 = now_ms();
    ui_draw_init();
    LOG("draw init %.0f ms", now_ms() - t0);

    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    sock_path(sa.sun_path, sizeof sa.sun_path);
    unlink(sa.sun_path);
    int sfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (bind(sfd, (struct sockaddr *)&sa, sizeof sa)) { perror("glassine-shell: bind"); return 1; }

    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGPIPE, SIG_IGN);
    bar.dock = 1;
    be_read_desktop();
    read_status();
    t0 = now_ms();
    render_bar();
    LOG("bar %.0f ms", now_ms() - t0);
    load_apps();
    notify_init(notices_changed);

    /* warm the caches (fonts of all popups, icons): the first tap is as fast as any */
    {
        cairo_surface_t *m = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 64, 64);
        cairo_t *cr = cairo_create(m);
        cairo_scale(cr, S, S);
        Gc g = { cr, 1, T };
        Surf dummy = { 0 };
        cur_s = &dummy; cur_g = &g;
        t0 = now_ms();
        for (int v = V_QS; v <= V_NOTIF; v++) { st.view = v; view_pop(&g); }
        st.view = V_IDLE;
        cairo_destroy(cr);
        cairo_surface_destroy(m);
        LOG("warm %.0f ms", now_ms() - t0);
    }

    GMainContext *gctx = g_main_context_default();
    g_main_context_acquire(gctx);
    double status_due = now_ms() + 30000;
    while (!quit) {
        be_dispatch();
        be_flush();
        /* timers */
        double t = now_ms();
        time_t wall = time(NULL);
        double clock_due = t + (60 - wall % 60) * 1000.0 + 50;
        double due = status_due < clock_due ? status_due : clock_due;
        if (refresh_due && refresh_due < due) due = refresh_due;
        if (press_long_due && press_long_due < due) due = press_long_due;
        if (st.opening >= 0 && st.opening_t + 1000 < due) due = st.opening_t + 1000;
        if (ws_hold_due && ws_hold_due < due) due = ws_hold_due;
        struct pollfd pf[10 + NGFD] = { { be_fd(), POLLIN, 0 }, { sfd, POLLIN, 0 } };
        int np = 2, jix[8];
        for (int i = 0; i < 8; i++) if (jobs[i].pid) { pf[np] = (struct pollfd){ jobs[i].fd, POLLIN, 0 }; jix[np - 2] = i; np++; }
        int to = (int)(due - t);
        /* GLib's default context (GDBus: the notification server) in the same poll */
        GPollFD gfd[NGFD];
        gint gprio, gto;
        g_main_context_prepare(gctx, &gprio);
        int ng = g_main_context_query(gctx, gprio, &gto, gfd, NGFD);
        if (ng > NGFD) ng = NGFD;
        for (int i = 0; i < ng; i++) pf[np + i] = (struct pollfd){ gfd[i].fd, gfd[i].events, 0 };
        if (gto >= 0 && (to < 0 || gto < to)) to = gto;
        if (poll(pf, np + ng, to < 0 ? 0 : to) < 0 && errno != EINTR) break;
        for (int i = 0; i < ng; i++) gfd[i].revents = pf[np + i].revents;
        if (g_main_context_check(gctx, gprio, gfd, ng)) g_main_context_dispatch(gctx);
        t = now_ms();
        if (pf[1].revents & POLLIN) {
            char m[1100];
            int l;
            while ((l = recv(sfd, m, sizeof m - 1, 0)) > 0) request(m, l);
        }
        for (int i = 2; i < np; i++) {
            if (!pf[i].revents) continue;
            struct job *j = &jobs[jix[i - 2]];
            int l = read(j->fd, j->buf + j->len, sizeof j->buf - 1 - j->len);
            if (l > 0) { j->len += l; if (j->len < (int)sizeof j->buf - 1) continue; }
            j->buf[j->len] = 0;
            close(j->fd);
            waitpid(j->pid, NULL, 0);
            JobDone d = j->done;
            char out[1024];
            memcpy(out, j->buf, j->len + 1);
            j->pid = 0;
            d(out);
        }
        while (waitpid(-1, NULL, WNOHANG) > 0) ;
        if (refresh_due && t >= refresh_due) { refresh_due = 0; epdd("full\n"); }
        if (press_long_due && t >= press_long_due && press.s == &pop &&
            (press.act == A_QS_TOGGLE || press.act == A_QS_SLABEL)) {
            press_long_due = 0;
            press.consumed = 1;
            held(press.act, press.arg);
        }
        if (st.opening >= 0 && t >= st.opening_t + 1000) set_view(V_IDLE);
        if (ws_hold_due && t >= ws_hold_due) ws_hold_check();
        if (t >= status_due || wall % 60 == 0 || strcmp(st.clock, "") == 0) {
            char old[sizeof st.clock];
            int ob = st.batt, oc = st.charging, ow = st.wifi_lvl;
            memcpy(old, st.clock, sizeof old);
            read_status();
            if (t >= status_due) status_due = t + 30000;
            if (strcmp(old, st.clock) || ob != st.batt || oc != st.charging || ow != st.wifi_lvl) render_bar();
        }
    }
    unlink(sa.sun_path);
    return 0;
}
