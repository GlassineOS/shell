/* glassine-swayd — the window model of the sway session. Runs as the user, started by the sway config.
 *
 * tiling a workspace's tiled app windows (floating ones -- dialogs --
 * and a fullscreen workspace aside), in their order (the newest
 * first; "swap" changes it):
 * 1: the whole workspace
 * 2: top half / bottom half
 * 3: one half whole, the other split across (the whole one
 * first or last: "split"; a new third window comes first)
 * 4: quarters: the first two on top, the other two below
 * Two levels: the first | the rest (4: the first two |
 * the other two); each level stacked or side by side ("orient",
 * remembered per workspace; default stacked, then side by side).
 * With 4 the inner splits are always across the outer one and
 * share one boundary (design one split each
 * way), so the quarters line up. Rebuilt whenever a workspace's
 * windows change, in one sway command list (one transaction:
 * one panel update).
 * workspaces at most APPS_PER_WS tiled app windows each: one beyond that
 * moves to the first workspace with room, or a new one, and the
 * view follows it. Sway drops an empty workspace it is not on;
 * the ones behind are renumbered down (numbered ones only): no
 * gaps.
 * glassine-epdd-desktop window hints on its FIFO: "win X Y W H" for each area where a window on the
 * visible workspace appeared, went or moved, in portrait px
 * (turned back by glassine-rotd's /run/glassine-rotation);
 * "win new ..." (settle as after a MapNotify) for a new window
 * when GLASSINE_SWAYD_SETTLE=1 (sway maps a window with its first
 * frame and applies a retile atomically, so the settle wait is
 * off by default). Also the app list for its app profiles
 * ("apps"/"app"/"active", see app_list).
 * launch a placeholder (app_id glassine-placeholder, from glassine-shell-wl at the
 * tap; title "glassine-ph PID ID...") holds a tile for an app that
 * is starting: tiled and counted like any window, so the layout
 * is final at the tap. The first new tiled window on its
 * workspace that matches (its pid has PID as an ancestor, or
 * its app id / class is one of the IDs) takes the placeholder's
 * place in the order; the placeholder is closed and left out
 * from then on (it closes asynchronously), so the app's map
 * moves nothing else.
 * handles a drag handle in each gap between the visible workspace's
 * tiles (swayd-handles.c; needs `gaps inner 20` + `gaps outer
 * -20` in the sway config): drag = the split moves there (kept
 * per workspace across rebuilds), tap = glassine-shell's divider sheet.
 * commands datagrams on $XDG_RUNTIME_DIR/glassine-swayd (glassine-shell's divider
 * sheet):
 * swap HEXID HEXID the two windows trade places
 * orient LEVEL 0|1 level 0 (the first | the rest) or 1
 * (the rest) stacked (0) / side by side (1)
 * swaphalves HEXID that window's screen: the halves trade places
 * place HEXID HEXID up|down|left|right the first window to that
 * side of the second, on the second's screen
 * turn HEXID the workspace of that window: both levels
 * the other way (the overview's Turn)
 * swapdir left|right|up|down the focused window trades places
 * with the tile next to it that way (the
 * keyboard's Super+Shift+arrows, keys.conf)
 * keyboard the on-screen keyboard comes up on a text field's focus
 * (hd_text_focus) unless a physical keyboard is attached (see
 * kbd_physical; re-checked on sway's input events).
 *
 * glassine-swayd [-v]
 * glassine-swayd COMMAND ARGS... send a command (above) to the running one
 * glassine-swayd --kbd-test INPUTS DEVICES (host test) the physical keyboards
 * of a get_inputs reply + /proc/bus/input/devices
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <sys/inotify.h>
#include "glassine-popupd.h"
#include "wayland/sway-ipc.h"
#include "wayland/swayd-handles.h"

#define APPS_PER_WS 4
#define GAP 40                          /* between tiles, device px: sway `gaps inner 20` at scale 2 */
#define MIN_TILE 120                    /* a drag leaves each tile at least this, device px */
#define MAXV 64
#define MAXWS 32
#define EPDD_CTL "/run/glassine-epdd-desktop.ctl"
#define SIPC_GET_INPUTS 100             /* (not in sway-ipc.h: only glassine-swayd asks) */
#define SIPC_EV_INPUT 0x80000015u

static int verbose, settle;
#define LOG(...) do { if (verbose) { fprintf(stderr, "glassine-swayd: " __VA_ARGS__); fputc('\n', stderr); } } while (0)
/* always (sway.log): what makes a handle do nothing, and the handles' use */
#define WARN(...) do { fprintf(stderr, "glassine-swayd: " __VA_ARGS__); fputc('\n', stderr); } while (0)

static int cmd_fd = -1, ev_fd = -1, scale = 2;

/* ---- the tree ------------------------------------------------------------------ */
typedef struct {
    long id; int r[4]; int floating, fullscreen, focused; char cls[128];
    int full;                           /* `full`: floating over the work area, still one of the layout's */
    long pid; long ph_pid; char ph_ids[192];    /* ph_pid: a launch placeholder for that pid */
} View;
#define PH_APP_ID "glassine-placeholder"
#define FULL_MARK "_glassine_full"      /* `full W`: W floats over the work area (the bar stays), kept in the order */

/* Home: an app kept in the scratchpad, shown floating over the work area (under the bar,
 * never sway's fullscreen: that covers the bar). Its app_id: $GLASSINE_HOME_APP_ID, else
 * /etc/glassine/home-app, else glassine-home (any app can be the home) */
static char home_app[128] = "glassine-home";
static long home_id;                    /* its con_id, 0: not running */
static char home_on[64];                /* the workspace it is shown on, "": hidden */
static int home_r[4];                   /* where it is (device px) */
static int home_tiled;                  /* it mapped tiled (no rule moved it yet) */

static int has_mark(json_object *n, const char *m)
{
    json_object *a;
    if (!json_object_object_get_ex(n, "marks", &a)) return 0;
    for (size_t i = 0; i < json_object_array_length(a); i++)
        if (!strcmp(json_object_get_string(json_object_array_get_idx(a, i)), m)) return 1;
    return 0;
}

static int is_home(json_object *n)
{
    const char *id = sj_str(n, "app_id");
    return id && !strcmp(id, home_app);
}
typedef struct {
    char name[64]; long num; int focused, visible;
    int r[4];                           /* the area windows get (without the bar) */
    View v[MAXV]; int nv;               /* tiled views, tree order */
    View f[MAXV]; int nf;               /* floating ones (dialogs) */
    int fullscreen;                     /* a view is fullscreen or `full`: no tiling */
    long full;                          /* the `full` one (FULL_MARK), 0: none */
    long unwrap[16]; int nun;           /* the only child of a single-child container (build) */
} Ws;
static Ws ws[MAXWS];
static int nws;
static View vis[MAXV];                  /* all views on visible workspaces (hints) */
static int nvis;
static int area[4];                     /* the visible workspace's area */
static int rot_cw, out_w, out_h;        /* glassine-rotd's turn (clockwise, as sway's `transform`) and the
                                         * output's logical size, device px: hints go out in portrait px */

/* the name glassine-epdd-desktop matches its app profiles against (eink.conf), lower case:
 * the app_id; XWayland windows: "class instance"; a Flatpak's ID is
 * appended (view_flatpak_id) */
static void view_class(json_object *n, char *c, size_t len)
{
    const char *id = sj_str(n, "app_id");
    json_object *wp;
    c[0] = 0;
    if (id && *id) snprintf(c, len, "%s", id);
    else if (json_object_object_get_ex(n, "window_properties", &wp)) {
        const char *cl = sj_str(wp, "class"), *in = sj_str(wp, "instance");
        snprintf(c, len, "%s %s", cl ? cl : "", in ? in : "");
    }
    for (char *p = c; *p; p++) {
        *p = tolower((unsigned char)*p);
        if (*p == '\n' || *p == '\r') *p = ' ';
    }
}

/* a Flatpak's ID as one more word of the class (lower case): its window's app_id
 * is often its own short name ("animatch"), while eink.conf lines from glassine-settings
 * name the desktop id (com.holypangolin.animatch). From the sandbox's
 * /.flatpak-info, cached by pid (the tree is read on every event). */
static void view_flatpak_id(long pid, char *c, size_t len)
{
    static struct { long pid; char id[96]; } cache[32];
    static int next;
    if (pid <= 0) return;
    int k = -1;
    for (int i = 0; i < 32; i++) if (cache[i].pid == pid) k = i;
    if (k < 0) {
        k = next; next = (next + 1) % 32;
        cache[k].pid = pid;
        cache[k].id[0] = 0;
        char path[64], line[160];
        snprintf(path, sizeof path, "/proc/%ld/root/.flatpak-info", pid);
        FILE *f = fopen(path, "r");
        if (f) {
            while (fgets(line, sizeof line, f))
                if (!strncmp(line, "name=", 5)) {
                    line[strcspn(line, "\r\n")] = 0;
                    snprintf(cache[k].id, sizeof cache[k].id, "%s", line + 5);
                    for (char *q = cache[k].id; *q; q++) *q = *q == ' ' ? '_' : tolower((unsigned char)*q);
                    break;
                }
            fclose(f);
        }
    }
    const char *id = cache[k].id;
    if (!*id || !strcmp(c, id)) return;
    size_t l = strlen(c);
    if (l + 1 + strlen(id) < len) snprintf(c + l, len - l, "%s%s", l ? " " : "", id);
}

/* a placeholder's title "glassine-ph PID ID..." */
static void ph_title(json_object *n, View *v)
{
    const char *t = sj_str(n, "name");
    int off = 0;
    if (t && sscanf(t, "glassine-ph %ld %n", &v->ph_pid, &off) >= 1 && off > 0) {
        snprintf(v->ph_ids, sizeof v->ph_ids, " %s ", t + off);
        for (char *q = v->ph_ids; *q; q++) *q = tolower((unsigned char)*q);
    }
    if (v->ph_pid <= 0) v->ph_pid = -1;     /* no pid: it matches by id only */
}

/* placeholders in the scratchpad: their app has just mapped (ph_arm's rule) */
static View scr_ph[8];
static int nscr_ph;

static void scratch_walk(json_object *n)
{
    json_object *a;
    if (!json_object_object_get_ex(n, "floating_nodes", &a)) return;
    for (size_t i = 0; i < json_object_array_length(a); i++) {
        json_object *e = json_object_array_get_idx(a, i);
        if (sj_str(e, "shell") && is_home(e)) { home_id = sj_int(e, "id", 0); continue; }
        if (!sj_str(e, "shell") || nscr_ph >= 8) continue;
        View v = { .id = sj_int(e, "id", 0), .pid = sj_int(e, "pid", 0) };
        view_class(e, v.cls, sizeof v.cls);
        if (strcmp(v.cls, PH_APP_ID)) continue;
        ph_title(e, &v);
        scr_ph[nscr_ph++] = v;
    }
}

static void walk(json_object *n, Ws *w, int floating)
{
    const char *type = sj_str(n, "type");
    if (type && !strcmp(type, "workspace")) {
        const char *nm = sj_str(n, "name");
        if (nm && !strcmp(nm, "__i3_scratch")) { scratch_walk(n); return; }
        if (!nm || !strncmp(nm, "__", 2) || nws >= MAXWS) return;
        w = &ws[nws++];
        memset(w, 0, sizeof *w);
        snprintf(w->name, sizeof w->name, "%s", nm);
        w->num = sj_int(n, "num", -1);
        sj_rect(n, "rect", scale, w->r);
    }
    if (w && sj_str(n, "shell") && is_home(n)) {    /* Home: none of the layout's */
        home_id = sj_int(n, "id", 0);
        if (!floating) { home_tiled = 1; return; }
        snprintf(home_on, sizeof home_on, "%s", w->name);
        sj_rect(n, "rect", scale, home_r);
        View v = { .id = home_id, .floating = 1 };
        sj_rect(n, "rect", scale, v.r);
        v.focused = sj_bool(n, "focused");
        view_class(n, v.cls, sizeof v.cls);
        if (w->nf < MAXV) w->f[w->nf++] = v;        /* (the hints and the app list see it) */
        return;
    }
    if (w && sj_str(n, "shell")) {      /* a view */
        View v = { .id = sj_int(n, "id", 0), .floating = floating, .fullscreen = sj_int(n, "fullscreen_mode", 0) != 0 };
        if (floating && has_mark(n, FULL_MARK)) { v.full = 1; v.floating = 0; floating = 0; w->full = v.id; w->fullscreen = 1; }
        sj_rect(n, "rect", scale, v.r);
        v.focused = sj_bool(n, "focused");
        view_class(n, v.cls, sizeof v.cls);
        v.pid = sj_int(n, "pid", 0);
        view_flatpak_id(v.pid, v.cls, sizeof v.cls);
        if (!strcmp(v.cls, PH_APP_ID)) ph_title(n, &v);
        if (v.fullscreen) w->fullscreen = 1;
        if (!floating && w->nv < MAXV) w->v[w->nv++] = v;
        if (floating && w->nf < MAXV) w->f[w->nf++] = v;
        return;
    }
    json_object *a;
    if (json_object_object_get_ex(n, "nodes", &a)) {
        if (w && !floating && type && !strcmp(type, "con") && json_object_array_length(a) == 1 && w->nun < 16)
            w->unwrap[w->nun++] = sj_int(json_object_array_get_idx(a, 0), "id", 0);
        for (size_t i = 0; i < json_object_array_length(a); i++) walk(json_object_array_get_idx(a, i), w, floating);
    }
    if (json_object_object_get_ex(n, "floating_nodes", &a))
        for (size_t i = 0; i < json_object_array_length(a); i++) walk(json_object_array_get_idx(a, i), w, 1);
}

/* which workspaces are visible / focused: GET_WORKSPACES (the tree has no
 * "visible" on workspaces) */
static struct { char name[64]; int visible, focused; } wsflags[MAXWS];
static int nwsflags;

/* placeholders already replaced: closed but maybe still in the tree */
static long ph_dead[16];
static int nph_dead;

static void ph_filter(void)
{
    int k = 0;
    for (int d = 0; d < nph_dead; d++) {
        int found = 0;
        for (int i = 0; i < nws; i++) {
            Ws *w = &ws[i];
            for (int a = 0; a < w->nv; a++)
                if (w->v[a].id == ph_dead[d]) { memmove(&w->v[a], &w->v[a + 1], (w->nv - a - 1) * sizeof *w->v); w->nv--; a--; found = 1; }
            for (int a = 0; a < w->nf; a++)
                if (w->f[a].id == ph_dead[d]) { memmove(&w->f[a], &w->f[a + 1], (w->nf - a - 1) * sizeof *w->f); w->nf--; a--; found = 1; }
        }
        for (int a = 0; a < nscr_ph; a++)
            if (scr_ph[a].id == ph_dead[d]) { memmove(&scr_ph[a], &scr_ph[a + 1], (nscr_ph - a - 1) * sizeof *scr_ph); nscr_ph--; a--; found = 1; }
        if (found) ph_dead[k++] = ph_dead[d];   /* gone from the tree: forgotten */
    }
    nph_dead = k;
}

static int read_tree(void)
{
    json_object *o = sipc_call(cmd_fd, SIPC_GET_OUTPUTS, NULL);
    for (size_t i = 0; o && i < json_object_array_length(o); i++) {
        json_object *e = json_object_array_get_idx(o, i), *sc;
        if (!sj_bool(e, "active")) continue;
        if (json_object_object_get_ex(e, "scale", &sc)) scale = (int)(json_object_get_double(sc) + 0.5);
        if (scale < 1) scale = 1;
        int r[4];
        sj_rect(e, "rect", scale, r);
        out_w = r[2]; out_h = r[3];
    }
    json_object_put(o);
    if (scale < 1) scale = 1;
    rot_cw = 0;
    FILE *rf = fopen("/run/glassine-rotation", "r");
    if (rf) { if (fscanf(rf, "%d", &rot_cw) != 1) rot_cw = 0; fclose(rf); }
    json_object *wj = sipc_call(cmd_fd, SIPC_GET_WORKSPACES, NULL);
    if (!wj) return 0;
    nwsflags = 0;
    for (size_t i = 0; i < json_object_array_length(wj) && nwsflags < MAXWS; i++) {
        json_object *e = json_object_array_get_idx(wj, i);
        snprintf(wsflags[nwsflags].name, sizeof wsflags[0].name, "%s", sj_str(e, "name") ? sj_str(e, "name") : "");
        wsflags[nwsflags].visible = sj_bool(e, "visible");
        wsflags[nwsflags].focused = sj_bool(e, "focused");
        nwsflags++;
    }
    json_object_put(wj);
    json_object *t = sipc_call(cmd_fd, SIPC_GET_TREE, NULL);
    static int tree_bad;
    if (!t) {
        if (!tree_bad++) WARN("sway's tree unreadable (lost, or not JSON): handles and tiling stall");
        return 0;
    }
    if (tree_bad) WARN("sway's tree readable again (after %d failed reads)", tree_bad);
    tree_bad = 0;
    nws = 0;
    nscr_ph = 0;
    home_id = 0; home_on[0] = 0; home_tiled = 0;
    walk(t, NULL, 0);
    for (int i = 0; i < nws; i++)
        for (int k = 0; k < nwsflags; k++)
            if (!strcmp(ws[i].name, wsflags[k].name)) { ws[i].visible = wsflags[k].visible; ws[i].focused = wsflags[k].focused; }
    ph_filter();
    nvis = 0;                           /* stacking order: tiled, floating, fullscreen */
    for (int i = 0; i < nws; i++) {
        if (!ws[i].visible) continue;
        memcpy(area, ws[i].r, sizeof area);
        for (int k = 0; k < ws[i].nv && nvis < MAXV; k++) if (!ws[i].v[k].fullscreen) vis[nvis++] = ws[i].v[k];
        for (int k = 0; k < ws[i].nf && nvis < MAXV; k++) if (!ws[i].f[k].fullscreen) vis[nvis++] = ws[i].f[k];
        for (int k = 0; k < ws[i].nv && nvis < MAXV; k++) if (ws[i].v[k].fullscreen) vis[nvis++] = ws[i].v[k];
        for (int k = 0; k < ws[i].nf && nvis < MAXV; k++) if (ws[i].f[k].fullscreen) vis[nvis++] = ws[i].f[k];
    }
    json_object_put(t);
    return 1;
}

static Ws *ws_of_view(long id, int *idx)
{
    for (int i = 0; i < nws; i++)
        for (int k = 0; k < ws[i].nv; k++)
            if (ws[i].v[k].id == id) { if (idx) *idx = k; return &ws[i]; }
    return NULL;
}

/* ---- commands ---------------------------------------------------------------------- */
static char cbuf[8192];
static size_t clen;

static void cadd(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (clen) clen += snprintf(cbuf + clen, sizeof cbuf - clen, "; ");
    if (clen < sizeof cbuf) clen += vsnprintf(cbuf + clen, sizeof cbuf - clen, fmt, ap);
    va_end(ap);
}

static void crun(void)
{
    if (!clen) return;
    if (clen >= sizeof cbuf) { LOG("command list too long, dropped"); clen = 0; return; }
    LOG("sway: %s", cbuf);
    json_object *j = sipc_call(cmd_fd, SIPC_COMMAND, cbuf);
    for (size_t i = 0; j && i < json_object_array_length(j); i++) {
        json_object *e = json_object_array_get_idx(j, i), *err;
        if (!sj_bool(e, "success") && json_object_object_get_ex(e, "error", &err)) LOG("sway: %s", json_object_get_string(err));
    }
    json_object_put(j);
    clen = 0;
}

/* ---- what we keep per workspace ----------------------------------------------------- */
typedef struct {
    char name[64]; long order[MAXV]; int n; int orient[2]; int built;
    double frac[3];                     /* the lines' positions, as dragged (0: the middle): [0] r, between
                                         * the halves (half 0's share); [1] ri[0], inside half 0; [2] ri[1],
                                         * inside half 1 (the first window's share of its half) */
    int split;                          /* 3 windows: 1 = v0 | v1 v2, 0 = v0 v1 | v2 */
} Keep;
static Keep keep[MAXWS];
static int nkeep;

static Keep *keep_of(const char *name)
{
    for (int i = 0; i < nkeep; i++) if (!strcmp(keep[i].name, name)) return &keep[i];
    if (nkeep >= MAXWS) return NULL;
    Keep *k = &keep[nkeep++];
    memset(k, 0, sizeof *k);
    snprintf(k->name, sizeof k->name, "%s", name);
    k->orient[1] = 1;                   /* the rest side by side */
    k->split = 1;
    return k;
}

/* what we keep of workspaces sway dropped goes: a later rename to the same name
 * would otherwise find the old one first (and its layout) */
static void keep_prune(void)
{
    int o = 0;
    for (int j = 0; j < nkeep; j++) {
        int live = 0;
        for (int i = 0; i < nws; i++) if (!strcmp(ws[i].name, keep[j].name)) live = 1;
        if (live) keep[o++] = keep[j];
    }
    nkeep = o;
}

/* the empty screen under Home (spec 001 FR-040), "": none; and where the next new
 * window goes (`next`): mode n(ew)/h(ere)/o (hole), 0: nowhere special, from the
 * screen next_ws, armed at next_ms */
static char hole[64];
static char next_mode, next_ws[64];
static long next_ms;

/* a workspace's name changes (sway's rename in the same command list): what we keep follows it */
static void ws_renamed(const char *from, const char *to)
{
    for (int j = 0; j < nkeep; j++) if (!strcmp(keep[j].name, from)) snprintf(keep[j].name, sizeof keep[j].name, "%s", to);
    if (!strcmp(hole, from)) snprintf(hole, sizeof hole, "%s", to);
    if (!strcmp(next_ws, from)) snprintf(next_ws, sizeof next_ws, "%s", to);
}

/* the halves (design shell.divider): g[half][i], gn[half] windows; returns the halves */
static int keep_groups(const Keep *k, long g[2][2], int gn[2])
{
    const long *v = k->order;
    gn[0] = gn[1] = 0;
    if (k->n <= 0) return 0;
    if (k->n == 1) { g[0][0] = v[0]; gn[0] = 1; return 1; }
    int first = k->n == 4 || (k->n == 3 && !k->split) ? 2 : 1;
    for (int i = 0; i < k->n && i < 4; i++) {
        int h = i >= first;
        g[h][gn[h]++] = v[i];
    }
    return 2;
}

static void keep_from_groups(Keep *k, long g[2][2], const int gn[2])
{
    int n = 0;
    for (int h = 0; h < 2; h++) for (int i = 0; i < gn[h]; i++) k->order[n++] = g[h][i];
    k->n = n;
    k->split = n == 3 ? gn[0] != 2 : 1;
}

/* window id leaves: the gap closes (a split half that loses a window is whole
 * again; a screen left with one half of two takes that half's direction) */
static void keep_remove(Keep *k, long id)
{
    if (k->n > 4) {                     /* (more than a layout holds: overflow's) */
        int o = 0;
        for (int j = 0; j < k->n; j++) if (k->order[j] != id) k->order[o++] = k->order[j];
        k->n = o;
        return;
    }
    long g[2][2];
    int gn[2];
    keep_groups(k, g, gn);
    for (int h = 0; h < 2; h++) {
        int o = 0;
        for (int i = 0; i < gn[h]; i++) if (g[h][i] != id) g[h][o++] = g[h][i];
        if (o < gn[h]) k->frac[1 + h] = 0;  /* a split half lost one: whole, no inner line */
        gn[h] = o;
    }
    if (!gn[0] && gn[1]) {              /* half 0 went: half 1 (and its line) is half 0 now */
        g[0][0] = g[1][0]; g[0][1] = g[1][1]; gn[0] = gn[1]; gn[1] = 0;
        k->frac[1] = k->frac[2];
        k->frac[2] = 0;
    }
    if (gn[0] == 2 && !gn[1]) {         /* one half left: its split is the screen's now */
        g[1][0] = g[0][1]; gn[0] = gn[1] = 1;
        k->orient[0] = k->orient[1];
        k->frac[0] = k->frac[1];
        k->frac[1] = k->frac[2] = 0;
        k->orient[1] = !k->orient[0];
    } else if (gn[0] + gn[1] <= 2) k->frac[1] = k->frac[2] = 0;
    keep_from_groups(k, g, gn);
}

/* the workspace's windows in our order: kept ones in place, new ones first
 * (the newest on top); 1 if the set changed */
static int order_update(Keep *k, const Ws *w)
{
    long o[MAXV];
    int n = 0, changed = 0;
    for (int j = k->n - 1; j >= 0; j--) {   /* the ones gone first: their gaps close by the halves */
        int present = 0;
        for (int i = 0; i < w->nv; i++) if (w->v[i].id == k->order[j]) present = 1;
        if (!present) { keep_remove(k, k->order[j]); changed = 1; }
    }
    for (int i = w->nv - 1; i >= 0; i--) {  /* new ones, newest (highest id) first */
        int known = 0;
        for (int j = 0; j < k->n; j++) if (k->order[j] == w->v[i].id) known = 1;
        if (!known) changed = 1;
    }
    /* new ones: by id descending */
    long nw[MAXV]; int nn = 0;
    for (int i = 0; i < w->nv; i++) {
        int known = 0;
        for (int j = 0; j < k->n; j++) if (k->order[j] == w->v[i].id) known = 1;
        if (!known) nw[nn++] = w->v[i].id;
    }
    for (int a = 0; a < nn; a++)
        for (int b = a + 1; b < nn; b++)
            if (nw[b] > nw[a]) { long t = nw[a]; nw[a] = nw[b]; nw[b] = t; }
    for (int a = 0; a < nn && n < MAXV; a++) o[n++] = nw[a];
    for (int j = 0; j < k->n && n < MAXV; j++) {
        int present = 0;
        for (int i = 0; i < w->nv; i++) if (w->v[i].id == k->order[j]) present = 1;
        if (present) o[n++] = k->order[j];
        else changed = 1;
    }
    int was = k->n, was_split = k->split;
    memcpy(k->order, o, n * sizeof *o);
    k->n = n;
    if (n == 3 && nn) k->split = 1;     /* a new one comes first: whole, the others split */
    if (nn && n >= 3 && !(was == 3 && was_split && n == 4 && nn == 1)) k->frac[2] = 0;
    if (nn && n >= 3) k->frac[1] = 0;   /* the halves are new (but half 1 of 3 -> 4 is the same pair) */
    if (n == 2 && was < 2 && nn) {      /* a new pair: rows in portrait, side by side in landscape */
        k->orient[0] = w->r[2] > w->r[3];
        k->orient[1] = !k->orient[0];
        k->frac[0] = k->frac[1] = k->frac[2] = 0;
    }
    return changed;
}

static const char *lay(int side) { return side ? "splith" : "splitv"; }

static Ws *ws_named(const char *name)
{
    for (int i = 0; i < nws; i++) if (!strcmp(ws[i].name, name)) return &ws[i];
    return NULL;
}

/* the dragged lines again (after a rebuild): the first window of each split to
 * its fraction of the split's range, as the drag left it. r: half 0's first
 * window along the outer direction; ri[h]: half h's first window across it
 * (the halves span the whole workspace that way) */
static double fr(double f) { return f > 0 ? f : 0.5; }

static void splits(Keep *k)
{
    Ws *w = ws_named(k->name);
    if (!w || k->n < 2 || k->n > 4) return;
    long g[2][2];
    int gn[2];
    keep_groups(k, g, gn);
    int A = k->orient[0] ? w->r[2] : w->r[3];
    if (k->frac[0] > 0)
        cadd("[con_id=%ld] resize set %s %d px", g[0][0], k->orient[0] ? "width" : "height", ((int)(k->frac[0] * A) - GAP / 2) / scale);
    int A1 = k->orient[1] ? w->r[2] : w->r[3];
    for (int h = 0; h < 2; h++)
        if (gn[h] == 2 && k->frac[1 + h] > 0)
            cadd("[con_id=%ld] resize set %s %d px", g[h][0], k->orient[1] ? "width" : "height", ((int)(k->frac[1 + h] * A1) - GAP / 2) / scale);
}

/* the layout of one workspace, as commands (see the top). No focus changes
 * (the workspace may be a hidden one): marks and "move container to mark",
 * which puts a container right after the marked one, in its parent. Nesting
 * left behind (single-child containers) takes no room. */
static void build(Keep *k)
{
    long *v = k->order;
    if (k->n < 2) return;
    Ws *fw = ws_named(k->name);
    if (fw && fw->full) { k->built = 0; return; }   /* `full`: laid out again when it ends */
    /* the single-child containers earlier builds left go first ("split none" on
     * the only child flattens its parent): "layout" on a workspace's child wraps
     * the workspace's children in a new container, so each rebuild nested the
     * tree 2 JSON levels deeper (34 after 12; json-c's default parse stops at
     * 32 and the handles then did nothing) */
    Ws *w = ws_named(k->name);
    for (int i = 0; w && i < w->nun; i++) cadd("[con_id=%ld] split none", w->unwrap[i]);
    if (k->n == 4) k->orient[1] = !k->orient[0];  /* across the outer split */
    if (k->n >= 3) k->orient[1] = !k->orient[0];  /* one split each way */
    cadd("[con_id=%ld] mark --add _glassinet", v[0]);
    if (k->n == 3 && !k->split) {       /* v0 v2, then [v0 v1] v2 */
        cadd("[con_id=%ld] move container to mark _glassinet", v[2]);
        cadd("unmark _glassinet");
        cadd("[con_id=%ld] layout %s", v[0], lay(k->orient[0]));
        cadd("[con_id=%ld] split %s", v[0], k->orient[1] ? "h" : "v");
        cadd("[con_id=%ld] mark --add _glassinet", v[0]);
        cadd("[con_id=%ld] move container to mark _glassinet", v[1]);
        cadd("unmark _glassinet");
        splits(k);
        k->built = 1;
        return;
    }
    if (k->n == 4) {                    /* v0 v2, then [v0 v1] [v2 v3] */
        cadd("[con_id=%ld] move container to mark _glassinet", v[2]);
        cadd("unmark _glassinet");
        cadd("[con_id=%ld] layout %s", v[0], lay(k->orient[0]));
        for (int j = 0; j <= 2; j += 2) {
            cadd("[con_id=%ld] split %s", v[j], k->orient[1] ? "h" : "v");
            cadd("[con_id=%ld] mark --add _glassinet", v[j]);
            cadd("[con_id=%ld] move container to mark _glassinet", v[j + 1]);
            cadd("unmark _glassinet");
        }
        splits(k);
        k->built = 1;
        return;
    }
    if (k->n == 3) cadd("[con_id=%ld] move container to mark _glassinet", v[2]);
    cadd("[con_id=%ld] move container to mark _glassinet", v[1]);       /* v0 v1 (v2) */
    cadd("unmark _glassinet");
    cadd("[con_id=%ld] layout %s", v[0], lay(k->orient[0]));
    if (k->n == 3) {                    /* v0 [v1 v2] */
        cadd("[con_id=%ld] split %s", v[1], k->orient[1] ? "h" : "v");
        cadd("[con_id=%ld] mark --add _glassinet", v[1]);
        cadd("[con_id=%ld] move container to mark _glassinet", v[2]);
        cadd("unmark _glassinet");
    }
    splits(k);
    k->built = 1;
}

/* ---- rules --------------------------------------------------------------------------- */
static long max_num(void)
{
    long m = 0;
    for (int i = 0; i < nws; i++) if (ws[i].num > m) m = ws[i].num;
    return m;
}

/* q is p or a descendant of p (/proc ppid chain) */
static int descends(long q, long p)
{
    for (int depth = 0; q > 1 && depth < 32; depth++) {
        if (q == p) return 1;
        char path[64], buf[512];
        snprintf(path, sizeof path, "/proc/%ld/stat", q);
        FILE *f = fopen(path, "r");
        if (!f) return 0;
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        char *e = strrchr(buf, ')');            /* comm may hold spaces */
        long pp;
        if (!e || sscanf(e + 2, "%*c %ld", &pp) != 1) return 0;
        q = pp;
    }
    return 0;
}

/* one of the placeholder's IDs is the view's app id, or a word of "class instance" */
static int ph_id_match(const View *ph, const View *v)
{
    char c[sizeof v->cls + 2], tok[130];
    snprintf(c, sizeof c, "%s", v->cls);
    for (char *save, *t = strtok_r(c, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
        snprintf(tok, sizeof tok, " %s ", t);
        if (strstr(ph->ph_ids, tok)) return 1;
    }
    return 0;
}

static int ph_matches(const View *ph, const View *v)
{
    return (ph->ph_pid > 0 && v->pid > 0 && descends(v->pid, ph->ph_pid)) || ph_id_match(ph, v);
}

static long launch_view, launch_ms;     /* the window that last took a placeholder's place */

static void launch_note(long id)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    launch_view = id;
    launch_ms = ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

/* Sway tells a new window its size when the map's transaction commits, after the
 * for_window rules ran. So a rule that moves the placeholder out of the tiles in
 * that step means the app never gets the half tile beside it (one resize less).
 * One rule per app (they stay until sway restarts, and do nothing without the
 * mark); the mark goes to the newest placeholder for that app. An app whose
 * app id isn't among the IDs takes the slower way below, by pid. */
static void ph_arm(void)
{
    static long armed[16];
    static int narmed;
    static char rules[32][64];
    static int nrules;
    for (int i = 0; i < nws; i++)
        for (int a = 0; a < ws[i].nv; a++) {
            const View *ph = &ws[i].v[a];
            if (!ph->ph_pid) continue;
            int seen = 0;
            for (int d = 0; d < 16; d++) if (armed[d] == ph->id) seen = 1;
            if (seen) continue;
            armed[narmed++ % 16] = ph->id;
            /* the IDs as a regex; "." for anything sway's parser or a regex could mind */
            char ids[sizeof ph->ph_ids], re[sizeof ph->ph_ids] = "", first[64] = "";
            snprintf(ids, sizeof ids, "%s", ph->ph_ids);
            size_t rl = 0;
            for (char *save, *t = strtok_r(ids, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
                for (char *q = t; *q; q++) if (!isalnum((unsigned char)*q) && *q != '_' && *q != '-') *q = '.';
                if (!first[0]) snprintf(first, sizeof first, "%s", t);
                rl += snprintf(re + rl, sizeof re - rl, "%s%s", rl ? "|" : "", t);
                if (rl >= sizeof re) break;
            }
            if (!first[0] || rl >= sizeof re) continue;
            int known = 0;
            for (int r = 0; r < nrules; r++) if (!strcmp(rules[r], first)) known = 1;
            if (!known) {
                if (nrules >= 32) continue;
                snprintf(rules[nrules++], sizeof rules[0], "%s", first);
                cadd("for_window [app_id=\"(?i)^(%s)$\"] [con_mark=\"^_glassine_ph_%s$\"] move scratchpad", re, first);
            }
            cadd("[con_id=%ld] mark --add _glassine_ph_%s", ph->id, first);
            crun();
        }
}

/* see "launch" at the top: a new window takes its placeholder's place */
static void ph_substitute(void)
{
    for (int s = 0; s < nscr_ph; s++) {     /* the rule moved it: its app is on a workspace */
        const View *ph = &scr_ph[s];
        for (int i = 0; i < nws; i++) {
            Keep *k = keep_of(ws[i].name);
            int slot = -1;
            for (int j = 0; k && j < k->n; j++) if (k->order[j] == ph->id) slot = j;
            if (slot < 0) continue;
            for (int b = 0; b < ws[i].nv; b++) {
                const View *v = &ws[i].v[b];
                int known = 0;
                for (int j = 0; j < k->n; j++) if (k->order[j] == v->id) known = 1;
                if (known || v->ph_pid || !ph_matches(ph, v)) continue;
                LOG("workspace %s: %s (%ld) takes the placeholder %ld's place", ws[i].name, v->cls, v->id, ph->id);
                k->order[slot] = v->id;
                launch_note(v->id);
                break;
            }
        }
        if (nph_dead < 16) ph_dead[nph_dead++] = ph->id;
        cadd("[con_id=%ld] kill", ph->id);
        crun();
    }
    nscr_ph = 0;
    for (int i = 0; i < nws; i++) {
        Ws *w = &ws[i];
        Keep *k = keep_of(w->name);
        if (!k) continue;
        for (int a = 0; a < w->nv; a++) {
            View *ph = &w->v[a];
            if (!ph->ph_pid) continue;
            int slot = -1;
            for (int j = 0; j < k->n; j++) if (k->order[j] == ph->id) slot = j;
            if (slot < 0) continue;                 /* not placed yet */
            for (int b = 0; b < w->nv; b++) {
                View *v = &w->v[b];
                if (v->ph_pid || v->id == ph->id) continue;
                int known = 0;
                for (int j = 0; j < k->n; j++) if (k->order[j] == v->id) known = 1;
                if (known) continue;
                if (!ph_matches(ph, v)) continue;
                LOG("workspace %s: %s (%ld) takes the placeholder %ld's place", w->name, v->cls, v->id, ph->id);
                k->order[slot] = v->id;
                launch_note(v->id);
                if (nph_dead < 16) ph_dead[nph_dead++] = ph->id;
                cadd("[con_id=%ld] kill", ph->id);
                crun();
                memmove(&w->v[a], &w->v[a + 1], (w->nv - a - 1) * sizeof *w->v);
                w->nv--;
                a--;
                break;
            }
        }
    }
}

/* a workspace with more than APPS_PER_WS: its newest (not yet kept) windows go on */
static int overflow(void)
{
    for (int i = 0; i < nws; i++) {
        Ws *w = &ws[i];
        if (w->nv <= APPS_PER_WS || w->fullscreen) continue;
        Keep *k = keep_of(w->name);
        /* the windows it doesn't know yet, newest first; else the newest */
        long mv = 0;
        for (int a = 0; a < w->nv; a++) {
            int known = 0;
            for (int j = 0; k && j < k->n; j++) if (k->order[j] == w->v[a].id) known = 1;
            if (!known && w->v[a].id > mv) mv = w->v[a].id;
        }
        if (!mv) for (int a = 0; a < w->nv; a++) if (w->v[a].id > mv) mv = w->v[a].id;
        char target[80] = "";
        for (int j = 0; j < nws && !target[0]; j++)
            if (j != i && ws[j].num >= 1 && ws[j].nv < APPS_PER_WS && !ws[j].fullscreen) snprintf(target, sizeof target, "\"%s\"", ws[j].name);
        if (!target[0]) snprintf(target, sizeof target, "number %ld", max_num() + 1);
        LOG("workspace %s full: window %ld to %s", w->name, mv, target);
        cadd("[con_id=%ld] move container to workspace %s", mv, target);
        cadd("workspace %s", target);
        cadd("[con_id=%ld] focus", mv);
        crun();
        return 1;
    }
    return 0;
}

/* numbered workspaces 1..n without gaps (the names are the numbers) */
static int renumber(void)
{
    long nums[MAXWS];
    int n = 0;
    for (int i = 0; i < nws; i++) {
        char b[32];
        snprintf(b, sizeof b, "%ld", ws[i].num);
        if (ws[i].num < 1 || strcmp(b, ws[i].name)) return 0;      /* a named one: leave them all */
        nums[n++] = ws[i].num;
    }
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (nums[b] < nums[a]) { long t = nums[a]; nums[a] = nums[b]; nums[b] = t; }
    int did = 0;
    for (int a = 0; a < n; a++)
        if (nums[a] != a + 1) {
            cadd("rename workspace \"%ld\" to \"%d\"", nums[a], a + 1);
            char o[32], t[32];
            snprintf(o, sizeof o, "%ld", nums[a]);
            snprintf(t, sizeof t, "%d", a + 1);
            ws_renamed(o, t);           /* what we keep follows the name */
            did = 1;
        }
    if (did) { LOG("renumbering"); crun(); }
    return did;
}

static void layout_all(void)
{
    for (int i = 0; i < nws; i++) {
        Ws *w = &ws[i];
        Keep *k = keep_of(w->name);
        if (!k) continue;
        int changed = order_update(k, w);
        if ((changed || !k->built) && !w->fullscreen && k->n >= 2 && k->n <= APPS_PER_WS) {
            LOG("workspace %s: layout of %d", w->name, k->n);
            build(k);
            crun();
        } else if (changed) k->built = 0;
    }
}

/* ---- hints to glassine-epdd-desktop ---------------------------------------------------------------- */
static View shown[MAXV];
static int nshown;
static long seen_max;                   /* the highest view id seen: a higher one is new */

/* a rect in the turned layout (device px) -> portrait px, what glassine-epdd-desktop's frame is
 * in (sway 0004 composes the turn with the grey output's own). 270 = the device
 * turned clockwise (glassine-rotd): its left edge is now the top */
static void portrait(const int b[4], int o[4])
{
    switch (rot_cw) {
    case 270: o[0] = b[1]; o[1] = out_w - b[0] - b[2]; o[2] = b[3]; o[3] = b[2]; break;
    case 90:  o[0] = out_h - b[1] - b[3]; o[1] = b[0]; o[2] = b[3]; o[3] = b[2]; break;
    case 180: o[0] = out_w - b[0] - b[2]; o[1] = out_h - b[1] - b[3]; o[2] = b[2]; o[3] = b[3]; break;
    default:  memcpy(o, b, 4 * sizeof *o);
    }
}

static void hint(const char *what, const int rl[4])
{
    char c[96];
    int r[4];
    portrait(rl, r);
    int n = snprintf(c, sizeof c, "%s %d %d %d %d\n", what, r[0], r[1], r[2], r[3]);
    int fd = open(EPDD_CTL, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { LOG("hint %s: %s", what, strerror(errno)); return; }
    if (write(fd, c, n) != n) LOG("hint: FIFO write failed");
    close(fd);
    LOG("hint %s %dx%d+%d+%d", what, r[2], r[3], r[0], r[1]);
}

/* the visible windows with their names, for glassine-epdd-desktop's app profiles: one write
 * (atomic, < PIPE_BUF), sent when it changes:
 * apps X Y W H the workspace area; starts a new list
 * app X Y W H NAME bottom to top
 * active NAME the focused window ("active" alone: none) */
static char app_list_sent[4096];
static void app_list(void)
{
    char *last = app_list_sent;
    char c[4096];
    int pa[4], pr[4];
    portrait(area, pa);
    int n = snprintf(c, sizeof c, "apps %d %d %d %d\n", pa[0], pa[1], pa[2], pa[3]);
    const char *act = "";
    for (int k = 0; k < nvis && n < (int)sizeof c - 200; k++) {
        portrait(vis[k].r, pr);
        n += snprintf(c + n, sizeof c - n, "app %d %d %d %d %s\n", pr[0], pr[1], pr[2], pr[3], vis[k].cls);
        if (vis[k].focused) act = vis[k].cls;
    }
    n += snprintf(c + n, sizeof c - n, *act ? "active %s\n" : "active\n", act);
    if (!strcmp(c, last)) return;
    int fd = open(EPDD_CTL, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { LOG("apps: %s", strerror(errno)); return; }
    if (write(fd, c, n) != n) LOG("apps: FIFO write failed");
    else snprintf(last, sizeof app_list_sent, "%s", c);
    close(fd);
    LOG("apps: %d, active %s", nvis, *act ? act : "-");
}

void hd_dragging(int on)
{
    char c[32];
    int l = snprintf(c, sizeof c, "debounce_drag=%d\n", on);
    int fd = open(EPDD_CTL, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    if (write(fd, c, l) != l) LOG("hint: FIFO write failed");
    close(fd);
}

static void hints(void)
{
    app_list();
    for (int i = 0; i < nshown; i++) {          /* gone or moved: the old area */
        int still = 0;
        for (int k = 0; k < nvis; k++)
            if (vis[k].id == shown[i].id && !memcmp(vis[k].r, shown[i].r, sizeof shown[i].r)) still = 1;
        if (!still) hint("win", shown[i].r);
    }
    for (int k = 0; k < nvis; k++) {            /* new or moved: the new area */
        int same = 0;
        for (int i = 0; i < nshown; i++)
            if (vis[k].id == shown[i].id && !memcmp(vis[k].r, shown[i].r, sizeof shown[i].r)) same = 1;
        if (same) continue;
        hint(settle && vis[k].id > seen_max ? "win new" : "win", vis[k].r);
    }
    memcpy(shown, vis, nvis * sizeof *vis);
    nshown = nvis;
    for (int k = 0; k < nvis; k++) if (vis[k].id > seen_max) seen_max = vis[k].id;
}

/* ---- drag handles (swayd-handles.c) ------------------------------------------------------ */
static int hd_fd = -1;

static const View *view_by_id(const Ws *w, long id)
{
    for (int i = 0; i < w->nv; i++) if (w->v[i].id == id) return &w->v[i];
    return NULL;
}

/* o = the smallest rect holding a and b */
static void rect_union(const int a[4], const int b[4], int o[4])
{
    int x0 = a[0] < b[0] ? a[0] : b[0], y0 = a[1] < b[1] ? a[1] : b[1];
    int x1 = a[0] + a[2] > b[0] + b[2] ? a[0] + a[2] : b[0] + b[2];
    int y1 = a[1] + a[3] > b[1] + b[3] ? a[1] + a[3] : b[1] + b[3];
    o[0] = x0; o[1] = y0; o[2] = x1 - x0; o[3] = y1 - y0;
}

/* the gap between tile a and the region b (x y w h), in the range lo0..len */
static int gap_def(HandleDef *d, const int a[4], const int b[4], const int range[4])
{
    int vert = b[0] >= a[0] + a[2];
    d->vert = vert;
    if (vert) {
        d->x = a[0] + a[2]; d->w = b[0] - d->x; d->y = range[1]; d->h = range[3];
        d->a0 = range[0]; d->alen = range[2];
    } else {
        d->y = a[1] + a[3]; d->h = b[1] - d->y; d->x = range[0]; d->w = range[2];
        d->a0 = range[1]; d->alen = range[3];
    }
    d->lo = d->a0 + MIN_TILE;
    d->hi = d->a0 + d->alen - MIN_TILE - GAP;
    if (d->hi < d->lo) d->hi = d->lo;
    return (vert ? d->w : d->h) >= GAP / 2;       /* sway's gaps are there */
}

/* the visible screen's dividers to glassine-shell: "<verb> N,ORIENT0 x,y,w,h,vert,level,a,b,pos ..."
 * (level 0 main, 1/2 the inner line of half 0/1; pos: r or ri, 3 decimals) */
static HandleDef div_now[3];
static int div_n, div_wins, div_orient;
static void dividers_send(const char *verb)
{
    char a[4][96];
    const char *av[5] = { verb, a[0], a[1], a[2], a[3] };
    snprintf(a[0], sizeof a[0], "%d,%d", div_wins, div_orient);
    for (int j = 0; j < div_n && j < 3; j++) {
        const HandleDef *h = &div_now[j];
        snprintf(a[j + 1], sizeof a[j + 1], "%d,%d,%d,%d,%d,%d,%lx,%lx,%.3f", h->x, h->y, h->w, h->h, h->vert, h->level, h->a, h->b, h->pos);
    }
    glassine_shell(av, 2 + div_n);
}

static void handles_update(int force)
{
    HandleDef d[3] = { 0 };
    int n = 0;
    char why[96] = "";                  /* why the focused workspace has none (logged on change) */
    for (int i = 0; i < nws && hd_fd >= 0; i++) {
        Ws *w = &ws[i];
        if (!w->focused) continue;
        if (home_on[0] && !strcmp(home_on, w->name)) { snprintf(why, sizeof why, "Home shown"); continue; }
        if (w->fullscreen || w->nv < 2 || w->nv > APPS_PER_WS) {
            if (w->fullscreen || w->nv > APPS_PER_WS)    /* (one window: no gap, nothing to say) */
                snprintf(why, sizeof why, "%s, %d tiled", w->fullscreen ? "fullscreen" : "more than 4 tiles", w->nv);
            continue;
        }
        Keep *k = keep_of(w->name);
        if (!k || k->n != w->nv) {
            snprintf(why, sizeof why, "the order knows %d windows, sway has %d", k ? k->n : -1, w->nv);
            continue;
        }
        long g[2][2];
        int gn[2];
        keep_groups(k, g, gn);
        const View *v[2][2] = { { 0 } };
        int half[2][4];                 /* each half's rect: its windows' union */
        for (int h = 0; h < 2; h++)
            for (int j = 0; j < gn[h]; j++) {
                if (!(v[h][j] = view_by_id(w, g[h][j]))) {
                    WARN("workspace %s: window %ld of the order not in sway's tree: handles left as they were", w->name, g[h][j]);
                    return;
                }
                if (j) rect_union(half[h], v[h][j]->r, half[h]);
                else memcpy(half[h], v[h][j]->r, sizeof half[h]);
            }
        memset(d, 0, sizeof d);
        if (gap_def(&d[n], half[0], half[1], w->r)) {
            d[n].level = 0; d[n].a = g[0][0]; d[n].b = g[1][0]; d[n].pos = fr(k->frac[0]); d[n].snap = -1; n++;
        }
        int inner[2] = { -1, -1 };      /* one line per split half, each on its own */
        for (int h = 0; h < 2; h++)
            if (gn[h] == 2 && gap_def(&d[n], v[h][0]->r, v[h][1]->r, half[h])) {
                d[n].level = 1 + h; d[n].a = g[h][0]; d[n].b = g[h][1]; d[n].pos = fr(k->frac[1 + h]); d[n].snap = -1;
                inner[h] = n++;
            }
        if (inner[0] >= 0 && inner[1] >= 0)     /* 2x2: each inner line snaps to the other */
            for (int h = 0; h < 2; h++) {
                const HandleDef *o = &d[inner[!h]];
                d[inner[h]].snap = o->vert ? o->x : o->y;
            }
        if (!n) LOG("workspace %s: no gaps between the tiles (sway config: gaps inner): no handles", w->name);
    }
    static HandleDef last[3];
    static int nlast = -1;
    static char lastwhy[96];
    if (strcmp(why, lastwhy)) {
        if (*why) WARN("no handles: %s", why);
        snprintf(lastwhy, sizeof lastwhy, "%s", why);
    }
    if (!force && n == nlast && !memcmp(last, d, n * sizeof *d)) return;   /* (pos included: a drag that
                                         * only snapped to where the line was changes nothing) */
    memcpy(last, d, sizeof last);
    nlast = n;
    memcpy(div_now, d, sizeof div_now);
    div_n = n; div_wins = 0; div_orient = 0;
    for (int i = 0; i < nws; i++)
        if (ws[i].focused) { Keep *k = keep_of(ws[i].name); if (k) { div_wins = k->n; div_orient = k->orient[0]; } }
    for (int j = 0; j < n; j++)
        LOG("handle %d: level %d %s %dx%d+%d+%d", j, d[j].level, d[j].vert ? "vertical" : "horizontal", d[j].w, d[j].h, d[j].x, d[j].y);
    hd_set(d, n);
    dividers_send("dividers");          /* a swap or Direction moved them under the shell's buttons */
}

static Keep *keep_focused(void)
{
    for (int i = 0; i < nws; i++) if (ws[i].focused) return keep_of(ws[i].name);
    return NULL;
}

void hd_dragged(const HandleDef *h, int pos)
{
    if (!read_tree()) { WARN("handle level %d dragged: sway's tree unreadable, nothing done", h->level); return; }
    Keep *k = keep_focused();
    int l = h->level;
    long g[2][2];
    int gn[2] = { 0, 0 };
    if (k && k->n >= 2 && k->n <= APPS_PER_WS) keep_groups(k, g, gn);
    if (!k || k->n < 2 || l < 0 || l > 2 || (l && gn[l - 1] != 2) || h->alen <= 0) {
        WARN("handle level %d dragged: the focused workspace's layout (%d windows) has no such line, nothing done",
             l, k ? k->n : -1);
        return;
    }
    int size = pos - h->a0;
    k->frac[l] = (size + GAP / 2.0) / h->alen;
    WARN("handle level %d dragged: %d px (%.2f)", l, size, k->frac[l]);
    /* the line's first window: half 0's for the main line, the half's own for an inner one */
    cadd("[con_id=%ld] resize set %s %d px", l ? g[l - 1][0] : g[0][0], h->vert ? "width" : "height", size / scale);
    crun();
    /* a resize sends no window event: move the handles into the new gaps now
     * (they are BOTTOM-layer, under the tiles: left in the old gap they'd be
     * hidden until the next event, P13 device run) */
    if (read_tree()) { hints(); handles_update(0); }
}

void hd_tapped(const HandleDef *h)
{
    if (!read_tree()) { WARN("handle level %d tapped: sway's tree unreadable, no divider sheet", h->level); return; }
    Keep *k = keep_focused();
    if (!k) { WARN("handle level %d tapped: no focused workspace, no divider sheet", h->level); return; }
    handles_update(0);                  /* the list as it is now */
    WARN("handle level %d tapped: the dividers' buttons", h->level);
    dividers_send("divider-tap");
}

/* the keyboard follows the text-input focus; it
 * hides again only if it came up this way (the bar's button shows it for good).
 * ~/.config/glassine/osk-no-auto (glassine-settings → Keyboard) turns the auto-show off. */
static int osk_auto(void)
{
    const char *h = getenv("HOME");
    char p[512];
    if (!h) return 1;
    snprintf(p, sizeof p, "%s/.config/glassine/osk-no-auto", h);
    return access(p, F_OK) != 0;
}

/* ---- a physical keyboard (a Bluetooth keyboard paired) ---------------------
 * With one attached, a text field's focus brings no on-screen keyboard (the bar's
 * button still does). A physical keyboard: an input sway lists as a keyboard
 * - that libinput drives (sway's get_inputs has its "libinput" object only
 * then: wvkbd's and wtype's virtual keyboards have none; their names:
 * "virtual-keyboard", on the device "wlr_virtual_keyboard_v1"),
 * - whose name is not one of the device's own keys (KBD_OWN: power key, the
 * cover's hall sensor, the headset jack's buttons, touch, pen; the
 * patterns in KBD_OWN, and wvkbd's wlr_virtual_keyboard_v1),
 * - and that has the letter keys: its /proc/bus/input/devices entry (same
 * name, vendor, product) has KEY_A..KEY_Z set. A Bluetooth mouse or a
 * keyboard's media-key node is also a "keyboard" to sway, without letters.
 * No /proc entry found: not counted (the auto-show stays: the safe side). */
static const char *const KBD_OWN[] = { "pwrkey", "resin", "qpnp_pon", "gpio-keys", "gpio_keys", "hall", "sf-keys",
                                       "hbtp", "headset", "jack", "cyttsp", "touch", "wacom", "stylus", "digitizer", "virtual", "wvkbd", NULL };

/* /proc/bus/input/devices names this device (Name, and Vendor/Product when sway
 * knows them; -1: any) with all 26 letter keys */
static int kbd_letters(const char *devices, const char *name, long vendor, long product)
{
    int match = 0, ok = 0;
    for (const char *l = devices; l && *l && !ok;) {
        const char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l) : strlen(l);
        unsigned vd = 0, pd = 0;
        if (n == 0) match = 0;                          /* a blank line ends an entry */
        else if (!strncmp(l, "I: ", 3) && sscanf(l, "I: Bus=%*x Vendor=%x Product=%x", &vd, &pd) == 2)
            match = (vendor < 0 || (long)vd == vendor) && (product < 0 || (long)pd == product) ? 1 : -1;
        else if (!strncmp(l, "N: Name=\"", 9) && match >= 0)
            match = n - 10 == strlen(name) && !strncmp(l + 9, name, n - 10) && l[n - 1] == '"';
        else if (!strncmp(l, "B: KEY=", 7) && match == 1) {
            /* hex words, the highest first; the last one holds keys 0..63 (a
             * long: 64 bits on the device's arm64 kernel and on the host) */
            const char *w = l + n;
            while (w > l + 7 && w[-1] != ' ' && w[-1] != '=') w--;
            unsigned long long bits = strtoull(w, NULL, 16);
            const unsigned long long letters = 0x3FFull << 16 | 0x1FFull << 30 | 0x7Full << 44;  /* Q..P, A..L, Z..M */
            ok = (bits & letters) == letters;
        }
        l = e ? e + 1 : l + n;
    }
    return ok;
}

/* one input of get_inputs: a physical keyboard (see above) */
static int kbd_physical(json_object *in, const char *devices)
{
    const char *type = sj_str(in, "type"), *name = sj_str(in, "name");
    json_object *li;
    if (!type || strcmp(type, "keyboard") || !name || !*name) return 0;
    if (!json_object_object_get_ex(in, "libinput", &li)) return 0;
    char low[256];
    snprintf(low, sizeof low, "%s", name);
    for (char *q = low; *q; q++) *q = tolower((unsigned char)*q);
    for (int i = 0; KBD_OWN[i]; i++) if (strstr(low, KBD_OWN[i])) return 0;
    return kbd_letters(devices, name, sj_int(in, "vendor", -1), sj_int(in, "product", -1));
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    size_t cap = 16384, n = 0;
    char *b = malloc(cap);
    for (size_t r; b && (r = fread(b + n, 1, cap - n - 1, f)) > 0;) {
        n += r;
        if (n + 1 >= cap) { char *nb = realloc(b, cap *= 2); if (!nb) { free(b); b = NULL; } else b = nb; }
    }
    fclose(f);
    if (b) b[n] = 0;
    return b;
}

/* the physical keyboards of a get_inputs reply, names into out ("" none); count */
static int kbd_scan(json_object *inputs, const char *devices, char *out, size_t len)
{
    int n = 0;
    size_t o = 0;
    out[0] = 0;
    for (size_t i = 0; inputs && i < json_object_array_length(inputs); i++) {
        json_object *in = json_object_array_get_idx(inputs, i);
        if (!kbd_physical(in, devices)) continue;
        if (o < len) o += snprintf(out + o, len - o, "%s%s", n ? ", " : "", sj_str(in, "name"));
        n++;
    }
    return n;
}

static int kbd_attached, osk_auto_shown;

static void osk_set(int on)
{
    osk_auto_shown = on;
    LOG("text field %s: keyboard %s", on ? "focused" : "left", on ? "shown" : "hidden");
    char *av[] = { "/usr/bin/glassine-osk", on ? "show" : "hide", NULL };
    pid_t pid;
    if (posix_spawn(&pid, av[0], NULL, NULL, av, environ)) LOG("glassine-osk: can't run");
}

static void kbd_update(void)
{
    json_object *in = sipc_call(cmd_fd, SIPC_GET_INPUTS, NULL);
    if (!in) return;
    const char *dp = getenv("GLASSINE_SWAYD_INPUT_DEVICES");     /* (tests) */
    char *dev = read_file(dp && *dp ? dp : "/proc/bus/input/devices");
    char names[512];
    int n = kbd_scan(in, dev, names, sizeof names);
    free(dev);
    json_object_put(in);
    LOG("inputs: %d physical keyboard%s%s%s", n, n == 1 ? "" : "s", n ? ": " : "", names);
    if (n != kbd_attached) WARN("physical keyboard%s: %s", n == 1 ? "" : "s", n ? names : "none (text fields show the on-screen one)");
    if (n && !kbd_attached && osk_auto_shown) osk_set(0);   /* it came up by itself: it goes */
    kbd_attached = n;
}

/* An app that focuses a text field as it opens (Chromium's address bar) doesn't
 * raise the keyboard: it would resize the new window again, for a field nobody
 * tapped. Only for a launch from the shell (a dialog that asks for text still
 * raises it). The tap that should raise it can't be seen from here: an app
 * reports nothing for a tap on a field it already has focused (Chromium's address
 * bar). So while held, the touch and pen devices are read (not grabbed): a short
 * tap with the field still focused OSK_TAP_SETTLE_MS later raises it; a tap
 * elsewhere in the app takes the focus away first. A tap on something that leaves
 * the focus alone (a toolbar button, the bar) raises it too. No device: no hold. */
#define OSK_LAUNCH_QUIET_MS 1000
#define OSK_HOLD_SETTLE_MS 300
#define OSK_TAP_MS 500
#define OSK_TAP_SETTLE_MS 250
#define OSK_TAP_DEVS 4
static int osk_held;
static long osk_held_ms;
static int tap_fd[OSK_TAP_DEVS], ntap;
static long tap_down[OSK_TAP_DEVS], tap_deadline;

static long mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000;
}

static int tap_open(void)
{
    DIR *d = opendir("/dev/input");
    struct dirent *e;
    ntap = 0;
    while (d && (e = readdir(d)) && ntap < OSK_TAP_DEVS) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[300];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 &&
            (keys[BTN_TOUCH / (8 * sizeof(long))] >> (BTN_TOUCH % (8 * sizeof(long)))) & 1) {
            tap_down[ntap] = 0;
            tap_fd[ntap++] = fd;
        } else close(fd);
    }
    if (d) closedir(d);
    return ntap;
}

static void osk_release(void)
{
    osk_held = 0;
    tap_deadline = 0;
    while (ntap) close(tap_fd[--ntap]);
}

static void tap_read(int i)
{
    struct input_event ev[32];
    ssize_t l;
    while ((l = read(tap_fd[i], ev, sizeof ev)) > 0)
        for (size_t k = 0; k < (size_t)l / sizeof *ev; k++) {
            if (ev[k].type != EV_KEY || ev[k].code != BTN_TOUCH) continue;
            if (ev[k].value) tap_down[i] = mono_ms();
            else if (tap_down[i] && mono_ms() - tap_down[i] < OSK_TAP_MS) tap_deadline = mono_ms() + OSK_TAP_SETTLE_MS;
        }
}

/* ms until a tap is due (-1: none); a due one raises the keyboard */
static int tap_wait(void)
{
    if (!tap_deadline) return -1;
    long left = tap_deadline - mono_ms();
    if (left > 0) return (int)left;
    osk_release();
    if (osk_auto() && !kbd_attached) osk_set(1);
    return -1;
}

void hd_text_focus(int on)
{
    if (on && (!osk_auto() || kbd_attached)) return;
    if (on && read_tree()) {
        ph_substitute();                /* it may have mapped just now */
        long fv = 0;
        for (int i = 0; i < nws; i++) {
            for (int a = 0; a < ws[i].nv; a++) if (ws[i].v[a].focused) fv = ws[i].v[a].id;
            for (int a = 0; a < ws[i].nf; a++) if (ws[i].f[a].focused) fv = ws[i].f[a].id;
        }
        if (fv && fv == launch_view && mono_ms() - launch_ms < OSK_LAUNCH_QUIET_MS && tap_open()) {
            LOG("text field focused by the app opening: keyboard held back (%d input devices)", ntap);
            osk_held = 1;
            osk_held_ms = mono_ms();
            return;
        }
    }
    osk_release();
    if (!on && !osk_auto_shown) return;
    osk_set(on);
}

void hd_text_changed(void)
{
    if (!osk_held || mono_ms() - osk_held_ms < OSK_HOLD_SETTLE_MS) return;
    osk_release();
    if (osk_auto() && !kbd_attached) osk_set(1);
}

/* ---- the shell's commands --------------------------------------------------------------- */
/* windows a and b (same workspace) trade places in the order */
static void swap_ids(long a, long b)
{
    int ia, ib;
    Ws *wa = ws_of_view(a, &ia), *wb = ws_of_view(b, &ib);
    if (!wa || wa != wb) return;
    Keep *k = keep_of(wa->name);
    int ka = -1, kb = -1;
    for (int j = 0; k && j < k->n; j++) { if (k->order[j] == a) ka = j; if (k->order[j] == b) kb = j; }
    if (ka < 0 || kb < 0) return;
    long t = k->order[ka]; k->order[ka] = k->order[kb]; k->order[kb] = t;
    build(k); crun();
}

/* the tile next to the focused one on its workspace, that way (dx, dy: -1/0/1):
 * the nearest whose rect lies beyond that edge and overlaps it across; 0: none */
static long neighbour(int dx, int dy)
{
    for (int i = 0; i < nws; i++) {
        Ws *w = &ws[i];
        if (!w->focused || w->fullscreen) continue;
        const View *f = NULL;
        for (int k = 0; k < w->nv; k++) if (w->v[k].focused) f = &w->v[k];
        if (!f) return 0;
        long best = 0;
        int bd = 1 << 30;
        for (int k = 0; k < w->nv; k++) {
            const View *c = &w->v[k];
            if (c == f) continue;
            const int *a = f->r, *b = c->r;
            int d, over;
            if (dx) {
                d = dx < 0 ? a[0] - (b[0] + b[2]) : b[0] - (a[0] + a[2]);
                over = (a[1] + a[3] < b[1] + b[3] ? a[1] + a[3] : b[1] + b[3]) - (a[1] > b[1] ? a[1] : b[1]);
            } else {
                d = dy < 0 ? a[1] - (b[1] + b[3]) : b[1] - (a[1] + a[3]);
                over = (a[0] + a[2] < b[0] + b[2] ? a[0] + a[2] : b[0] + b[2]) - (a[0] > b[0] ? a[0] : b[0]);
            }
            if (d < 0 || over <= 0 || d >= bd) continue;
            bd = d; best = c->id;
        }
        return best;
    }
    return 0;
}

/* the halves change places: 2 windows swap, 3 the whole one and the pair, 4 the
 * pairs; the main line mirrors, the inner lines go with their halves */
static void swap_halves(long a)
{
    int ia;
    Ws *w = ws_of_view(a, &ia);
    Keep *k = w ? keep_of(w->name) : NULL;
    if (!k || k->n < 2 || k->n > 4) return;
    long g[2][2], t[2][2];
    int gn[2], tn[2];
    keep_groups(k, g, gn);
    memcpy(t[0], g[1], sizeof t[0]); memcpy(t[1], g[0], sizeof t[1]);
    tn[0] = gn[1]; tn[1] = gn[0];
    keep_from_groups(k, t, tn);
    if (k->frac[0] > 0) k->frac[0] = 1 - k->frac[0];
    double f = k->frac[1]; k->frac[1] = k->frac[2]; k->frac[2] = f;
    build(k); crun();
}

/* the workspace of window a with its kept layout (2..4 windows); NULL: none */
static Keep *keep_of_view(long a, Ws **wp, const char *verb)
{
    int ia;
    Ws *w = ws_of_view(a, &ia);
    Keep *k = w ? keep_of(w->name) : NULL;
    if (!k || k->n != w->nv || k->n < 2 || k->n > APPS_PER_WS) {
        WARN("%s: window %lx has no layout of 2-4 windows", verb, (unsigned long)a);
        return NULL;
    }
    if (wp) *wp = w;
    return k;
}

/* swapin: the two windows of w's split half trade places; the half's line mirrors */
static void swap_in(long a)
{
    Keep *k = keep_of_view(a, NULL, "swapin");
    if (!k) return;
    long g[2][2];
    int gn[2];
    keep_groups(k, g, gn);
    for (int h = 0; h < 2; h++) {
        if (gn[h] != 2 || (g[h][0] != a && g[h][1] != a)) continue;
        long t = g[h][0]; g[h][0] = g[h][1]; g[h][1] = t;
        keep_from_groups(k, g, gn);
        if (k->frac[1 + h] > 0) k->frac[1 + h] = 1 - k->frac[1 + h];
        build(k); crun();
        return;
    }
    WARN("swapin: window %lx is not in a split half", (unsigned long)a);
}

/* cross (spec 001 FR-023): a 2x2 screen's other axis runs through; every window
 * keeps its quarter. The two inner lines meet at their mean, the old through-line
 * splits in two at its own position */
static void cross(long a)
{
    Keep *k = keep_of_view(a, NULL, "cross");
    if (!k) return;
    if (k->n != 4) { WARN("cross: %d windows, not 2x2", k->n); return; }
    long g[2][2];
    int gn[2];
    keep_groups(k, g, gn);
    double r = fr(k->frac[0]), ri0 = fr(k->frac[1]), ri1 = fr(k->frac[2]);
    long o[4] = { g[0][0], g[1][0], g[0][1], g[1][1] };
    memcpy(k->order, o, sizeof o);
    k->orient[0] = !k->orient[0];
    k->orient[1] = !k->orient[0];
    k->frac[0] = (ri0 + ri1) / 2;
    k->frac[1] = k->frac[2] = r;
    build(k); crun();
}

/* direction: a two-window screen, rows <-> side by side */
static void direction(long a)
{
    Keep *k = keep_of_view(a, NULL, "direction");
    if (!k) return;
    if (k->n != 2) { WARN("direction: %d windows, not 2", k->n); return; }
    k->orient[0] = !k->orient[0];
    k->orient[1] = !k->orient[0];
    build(k); crun();
}

/* `full` ends for window id (it leaves or is placed): back into the tiling */
static void unfull(long id)
{
    for (int i = 0; i < nws; i++)
        if (ws[i].full == id) {
            cadd("[con_id=%ld] unmark " FULL_MARK, id);
            cadd("[con_id=%ld] floating disable", id);
            ws[i].full = 0;
            ws[i].fullscreen = 0;
            for (int a = 0; a < ws[i].nv; a++) if (ws[i].v[a].fullscreen) ws[i].fullscreen = 1;
        }
}

/* a floating window over workspace w's area (device px; under the bar) */
static void cover(long id, const Ws *w)
{
    cadd("[con_id=%ld] border none", id);
    cadd("[con_id=%ld] resize set width %d px height %d px", id, w->r[2] / scale, w->r[3] / scale);
    cadd("[con_id=%ld] move absolute position %d px %d px", id, w->r[0] / scale, w->r[1] / scale);
}

/* full (spec 001 FR-019): window a over its whole screen, the bar still above it;
 * the others stay tiled underneath. Not sway's fullscreen (that covers the bar):
 * a floating window over the work area, marked, kept in the layout's order */
static void full(long a)
{
    int ia;
    Ws *w = ws_of_view(a, &ia);
    Keep *k = w ? keep_of(w->name) : NULL;
    if (!k) { WARN("full: no window %lx", (unsigned long)a); return; }
    if (w->full == a) {                 /* off: back to its tile */
        unfull(a);
        crun();
        if (read_tree() && (w = ws_named(k->name)) && !w->fullscreen) { build(k); crun(); }
        return;
    }
    if (w->full) unfull(w->full);
    for (int i = 0; i < w->nv; i++) if (w->v[i].fullscreen) cadd("[con_id=%ld] fullscreen disable", w->v[i].id);
    cadd("[con_id=%ld] mark --add " FULL_MARK, a);
    cadd("[con_id=%ld] floating enable", a);
    cover(a, w);
    cadd("[con_id=%ld] focus", a);
    crun();
}

/* the overview's arrows: window m to that side of window t (design
 * shell.overview.move). t alone: any side, the side sets the split; t a whole
 * half: the two sides that split it; else refused. On m's own screen the same,
 * on the screen as it is without m; a move that changes nothing is refused */
static void place(long m, long t, const char *dir)
{
    int im, it;
    Ws *wm = ws_of_view(m, &im), *wt = ws_of_view(t, &it);
    if (!wm || !wt || m == t) { WARN("place: no such target"); return; }
    Keep *kt = keep_of(wt->name), *km = keep_of(wm->name);
    int same = wm == wt;
    if (!kt || !km || kt->n != wt->nv) { WARN("place: workspace %s can't take it", wt->name); return; }
    Keep b = *kt;                       /* the target screen, as it would be */
    if (same) keep_remove(&b, m);
    if (b.n < 1 || b.n >= APPS_PER_WS) { WARN("place: workspace %s can't take it", wt->name); return; }
    int up = !strcmp(dir, "up"), down = !strcmp(dir, "down"), left = !strcmp(dir, "left"), right = !strcmp(dir, "right");
    if (!up && !down && !left && !right) return;
    int before = up || left, across_v = up || down;
    long g[2][2];
    int gn[2];
    if (b.n == 1) {
        g[0][0] = before ? m : t; g[1][0] = before ? t : m;
        gn[0] = gn[1] = 1;
        b.orient[0] = !across_v;
        b.orient[1] = across_v;
        b.frac[0] = b.frac[1] = b.frac[2] = 0;
    } else {
        keep_groups(&b, g, gn);
        int h = g[0][0] == t && gn[0] == 1 ? 0 : g[1][0] == t && gn[1] == 1 ? 1 : -1;
        /* stacked halves split left/right, side-by-side halves up/down */
        if (h < 0 || b.orient[0] != across_v) { WARN("place: side %s not offered on %s", dir, wt->name); return; }
        g[h][0] = before ? m : t; g[h][1] = before ? t : m;
        gn[h] = 2;
        b.orient[1] = !b.orient[0];
        b.frac[1 + h] = 0;
    }
    keep_from_groups(&b, g, gn);
    if (same && b.n == kt->n && !memcmp(b.order, kt->order, b.n * sizeof *b.order) &&
        b.orient[0] == kt->orient[0] && (b.n < 3 || b.split == kt->split)) {
        WARN("place: %s of the same window changes nothing", dir);
        return;
    }
    int gone = !same && km->n == 1, follow = gone && wm->focused;
    if (!same) keep_remove(km, m);
    *kt = b;
    for (int i = 0; i < wt->nv; i++) {  /* it shares now: a maximized one comes back to its tile */
        if (wt->v[i].fullscreen) cadd("[con_id=%ld] fullscreen disable", wt->v[i].id);
    }
    if (wt->full) unfull(wt->full);
    if (!same) {
        unfull(m);
        cadd("[con_id=%ld] move container to workspace \"%s\"", m, wt->name);
    }
    if (follow) cadd("workspace \"%s\"", wt->name);
    crun();
    if (!read_tree()) return;
    if ((kt = keep_of(b.name))) build(kt);
    if (!same && !gone && (km = keep_of(wm->name))) build(km);
    crun();
}

/* a numbered workspace after at: the later ones move up one (renames, top
 * down; what we keep follows). Returns its number */
static long insert_after(long at)
{
    long top = max_num();
    if (at < 1) at = top;               /* a named one: after the numbered ones */
    for (long n = top; n > at; n--)
        for (int i = 0; i < nws; i++) {
            char b[32], t[32];
            snprintf(b, sizeof b, "%ld", n);
            snprintf(t, sizeof t, "%ld", n + 1);
            if (ws[i].num == n && !strcmp(ws[i].name, b)) { cadd("rename workspace \"%s\" to \"%s\"", b, t); ws_renamed(b, t); }
        }
    return at + 1;
}

/* Full and the overview's New screen: window m on a new screen right after its
 * own (design shell.overview.move: splice(i + 1)); the later ones move up one.
 * follow: go there and focus it. */
static void new_screen(long m, int follow)
{
    int im;
    Ws *wm = ws_of_view(m, &im);
    if (!wm || wm->nv < 2) { WARN("newscreen: window %ld has no screen to leave", m); return; }
    char name[64];
    snprintf(name, sizeof name, "%s", wm->name);
    long at = insert_after(wm->num);
    Keep *km = keep_of(name);
    if (km) keep_remove(km, m);
    unfull(m);
    cadd("[con_id=%ld] move container to workspace \"%ld\"", m, at);
    if (follow) { cadd("workspace \"%ld\"", at); cadd("[con_id=%ld] focus", m); }
    crun();
    if (km && read_tree()) { build(km); crun(); }
}

/* the numbered workspaces: count, and the one numbered n */
static int ws_count(void)
{
    int c = 0;
    for (int i = 0; i < nws; i++) if (ws[i].num >= 1) c++;
    return c;
}

static Ws *ws_num(long n)
{
    for (int i = 0; i < nws; i++) if (ws[i].num == n) return &ws[i];
    return NULL;
}

static Ws *ws_focused(void)
{
    for (int i = 0; i < nws; i++) if (ws[i].focused) return &ws[i];
    return NULL;
}

/* scrmove: screen n trades places with the one before (d -1) or after (d 1) */
static void scr_move(long n, int d)
{
    Ws *a = ws_num(n), *b = ws_num(n + d);
    if ((d != 1 && d != -1) || !a || !b) { WARN("scrmove %ld %d: no screen there", n, d); return; }
    char an[64], bn[64];
    snprintf(an, sizeof an, "%s", a->name);
    snprintf(bn, sizeof bn, "%s", b->name);
    const char *tmp = "glassine-scrmove";
    cadd("rename workspace \"%s\" to \"%s\"", an, tmp); ws_renamed(an, tmp);
    cadd("rename workspace \"%s\" to \"%s\"", bn, an); ws_renamed(bn, an);
    cadd("rename workspace \"%s\" to \"%s\"", tmp, bn); ws_renamed(tmp, bn);
    crun();
}

/* ---- Home and the empty screen ------------------------------------------------------------ */
static void home_send(int force)
{
    static int lv = -1, lh = -1;
    Ws *h = hole[0] ? ws_named(hole) : NULL;
    int v = home_on[0] != 0, hn = h && h->num > 0 ? (int)h->num : 0;
    if (!force && v == lv && hn == lh) return;
    lv = v; lh = hn;
    char a[16], b[16];
    snprintf(a, sizeof a, "%d", v);
    snprintf(b, sizeof b, "%d", hn);
    const char *av[3] = { "home", a, b };
    LOG("home %d %d", v, hn);
    glassine_shell(av, 3);
}

static void home_show(void)
{
    Ws *f = ws_focused();
    if (!home_id) { WARN("home: no Home window (app_id %s) runs", home_app); return; }
    if (!f || !strcmp(home_on, f->name)) return;
    if (home_on[0]) cadd("[con_id=%ld] move scratchpad", home_id);
    cadd("[con_id=%ld] scratchpad show", home_id);
    cover(home_id, f);
    crun();
    snprintf(home_on, sizeof home_on, "%s", f->name);
}

/* Home goes back to the scratchpad. by_app: an app opened (it fills the hole);
 * else the hole goes too: Home hides while on it, then the view goes to `go`
 * (NULL: the screen before the hole, or after it for the first), and sway drops
 * the empty workspace it leaves. The only screen keeps Home: refused */
static void home_hide(int by_app, const char *go)
{
    if (!home_on[0]) return;
    Ws *hw = hole[0] ? ws_named(hole) : NULL;
    if (hw && !by_app) {
        char to[80] = "";
        if (go) snprintf(to, sizeof to, "%s", go);
        else {
            Ws *t = ws_num(hw->num - 1);
            if (!t) t = ws_num(hw->num + 1);
            if (!t) for (int i = 0; i < nws && !t; i++) if (&ws[i] != hw) t = &ws[i];
            if (!t) { WARN("home: the only screen is empty: Home stays until an app opens"); return; }
            snprintf(to, sizeof to, "%s", t->name);
        }
        if (!hw->focused) cadd("workspace \"%s\"", hw->name);
        cadd("[con_id=%ld] move scratchpad", home_id);
        cadd("workspace \"%s\"", to);
    } else cadd("[con_id=%ld] move scratchpad", home_id);
    hole[0] = 0;
    home_on[0] = 0;
    crun();
}

/* `next`: where the next new window goes */
static void next_set(const char *m)
{
    Ws *f = ws_focused();
    next_mode = 0;
    if (!f) return;
    if (!strcmp(m, "new")) next_mode = 'n';
    else if (!strcmp(m, "here")) {
        if (f->nv >= APPS_PER_WS) { WARN("next here: screen %s is full", f->name); return; }
        next_mode = 'h';
    } else if (!strcmp(m, "hole")) {
        if (!hole[0]) { WARN("next hole: no empty screen"); return; }
        next_mode = 'o';
    } else { WARN("next %s: new, here or hole", m); return; }
    snprintf(next_ws, sizeof next_ws, "%s", f->name);
    next_ms = mono_ms();
}

/* the views of the last pass: the focused workspace and its windows (for "the last
 * one closed"), and the highest tiled view id (a higher one is new) */
static char pf_name[64];
static long pf_ids[MAXV];
static int pf_n, seen_init;
static long seen_view;

static int view_exists(long id)
{
    for (int i = 0; i < nws; i++) {
        for (int a = 0; a < ws[i].nv; a++) if (ws[i].v[a].id == id) return 1;
        for (int a = 0; a < ws[i].nf; a++) if (ws[i].f[a].id == id) return 1;
    }
    return 0;
}

/* new tiled windows: `next` sends the first where it was asked to go; any hides
 * Home (filling the hole if it is there). 1: something moved (read again) */
static int fresh(void)
{
    long nid = 0, top = seen_view;
    Ws *nw = NULL;
    for (int i = 0; i < nws; i++)
        for (int a = 0; a < ws[i].nv; a++) {
            long id = ws[i].v[a].id;
            if (id > top) top = id;
            if (seen_init && id > seen_view && (!nid || id < nid)) { nid = id; nw = &ws[i]; }
        }
    seen_view = top;
    if (!seen_init) { seen_init = 1; return 0; }
    if (!nid) return 0;
    char where[64];
    snprintf(where, sizeof where, "%s", nw->name);
    int did = 0;
    if (next_mode && nid != launch_view) {
        char mode = next_mode;
        next_mode = 0;
        if (mono_ms() - next_ms > 10000) LOG("next: forgotten (over 10 s)");
        else if (mode == 'n') {
            Ws *from = ws_named(next_ws);
            long at = insert_after(from ? from->num : nw->num);
            cadd("[con_id=%ld] move container to workspace \"%ld\"", nid, at);
            cadd("workspace \"%ld\"", at);
            cadd("[con_id=%ld] focus", nid);
            snprintf(where, sizeof where, "%ld", at);
            LOG("next new: window %ld on a new screen %ld", nid, at);
            did = 1;
        } else {
            const char *to = mode == 'o' ? hole : next_ws;
            Ws *t = to[0] ? ws_named(to) : NULL;
            if (t && t != nw && t->nv < APPS_PER_WS) {
                cadd("[con_id=%ld] move container to workspace \"%s\"", nid, t->name);
                cadd("workspace \"%s\"", t->name);
                cadd("[con_id=%ld] focus", nid);
                snprintf(where, sizeof where, "%s", t->name);
                did = 1;
            }
            LOG("next %s: window %ld to %s", mode == 'o' ? "hole" : "here", nid, t ? t->name : "(gone)");
        }
        crun();
        if (did && !read_tree()) return 1;  /* (the focus moved: home_hide must see it) */
    }
    if (home_on[0]) {                   /* an app opened: Home hides */
        if (hole[0] && strcmp(hole, where)) home_hide(0, where);    /* not into the hole: it goes */
        else home_hide(1, NULL);
        did = 1;
    }
    return did;
}

/* the empty screen (FR-040): the last window on the focused screen closed -> it
 * stays, Home on it. Home left anywhere but on the focused screen hides (an empty
 * screen under it goes). Home mapped tiled goes to the scratchpad. 1: changed */
static int empty_screen(void)
{
    if (home_tiled && home_id) {
        cadd("[con_id=%ld] move scratchpad", home_id);
        crun();
        return 1;
    }
    Ws *f = ws_focused();
    if (home_on[0] && f && strcmp(home_on, f->name)) {
        LOG("home: the view left it (%s): hidden", home_on);
        if (hole[0] && !strcmp(hole, home_on)) home_hide(0, f->name);
        else home_hide(1, NULL);
        return 1;
    }
    if (hole[0] && !ws_named(hole)) hole[0] = 0;
    if (hole[0] && f && !strcmp(hole, f->name) && f->nv) hole[0] = 0;   /* (filled) */
    if (!f || !home_id || hole[0] || f->nv || strcmp(f->name, pf_name) || !pf_n) return 0;
    for (int i = 0; i < f->nf; i++) if (f->f[i].id != home_id) return 0;
    for (int j = 0; j < pf_n; j++) if (view_exists(pf_ids[j])) return 0;   /* moved away, not closed */
    LOG("workspace %s: its last window closed: empty, under Home", f->name);
    snprintf(hole, sizeof hole, "%s", f->name);
    if (!home_on[0]) home_show();
    return 1;
}

static void pf_note(void)
{
    Ws *f = ws_focused();
    pf_n = 0;
    pf_name[0] = 0;
    if (!f) return;
    snprintf(pf_name, sizeof pf_name, "%s", f->name);
    for (int a = 0; a < f->nv && pf_n < MAXV; a++) pf_ids[pf_n++] = f->v[a].id;
    for (int a = 0; a < f->nf && pf_n < MAXV; a++) if (f->f[a].id != home_id) pf_ids[pf_n++] = f->f[a].id;
}

/* scrclose: every app on screen n closes, the screen goes (the view first leaves it) */
static void scr_close(long n)
{
    Ws *w = ws_num(n);
    if (!w) { WARN("scrclose %ld: no such screen", n); return; }
    if (ws_count() < 2) { WARN("scrclose %ld: the last screen stays", n); return; }
    if (hole[0] && !strcmp(hole, w->name)) { home_hide(0, NULL); return; }
    if (w->focused) {
        Ws *t = ws_num(n - 1);
        if (!t) t = ws_num(n + 1);
        if (home_on[0]) { cadd("[con_id=%ld] move scratchpad", home_id); home_on[0] = 0; }
        if (t) cadd("workspace \"%s\"", t->name);
    }
    for (int a = 0; a < w->nv; a++) cadd("[con_id=%ld] kill", w->v[a].id);
    for (int a = 0; a < w->nf; a++) if (w->f[a].id != home_id) cadd("[con_id=%ld] kill", w->f[a].id);
    crun();
}

/* (tests, GLASSINE_SWAYD_TEST=1) the line of that level dragged to fraction f */
static void test_drag(int level, double f)
{
    handles_update(0);
    for (int j = 0; j < div_n; j++)
        if (div_now[j].level == level) {
            HandleDef d = div_now[j];
            hd_dragged(&d, hd_snap(&d, (int)lround(d.a0 + f * d.alen - GAP / 2.0)));
            return;
        }
    WARN("testdrag: no line of level %d", level);
}

static void command(char *m)
{
    LOG("command: %s", m);
    unsigned long a, b;
    int lv, side;
    long n;
    double f;
    char dir[8];
    if (sscanf(m, "place %lx %lx %7s", &a, &b, dir) == 3) { place((long)a, (long)b, dir); return; }
    if (sscanf(m, "newscreen %lx %d", &a, &lv) == 2) { new_screen((long)a, lv); return; }
    if (sscanf(m, "swaphalves %lx", &a) == 1) { swap_halves((long)a); return; }
    if (sscanf(m, "swapin %lx", &a) == 1) { swap_in((long)a); return; }
    if (sscanf(m, "cross %lx", &a) == 1) { cross((long)a); return; }
    if (sscanf(m, "direction %lx", &a) == 1) { direction((long)a); return; }
    if (sscanf(m, "full %lx", &a) == 1) { full((long)a); return; }
    if (sscanf(m, "scrmove %ld %d", &n, &lv) == 2) { scr_move(n, lv); return; }
    if (sscanf(m, "scrclose %ld", &n) == 1) { scr_close(n); return; }
    if (sscanf(m, "next %7s", dir) == 1) { next_set(dir); return; }
    if (sscanf(m, "home %7s", dir) == 1) {
        if (!strcmp(dir, "show") || (!strcmp(dir, "toggle") && !home_on[0])) home_show();
        else if (!strcmp(dir, "hide") || !strcmp(dir, "toggle")) home_hide(0, NULL);
        return;
    }
    if (sscanf(m, "testdrag %d %lf", &lv, &f) == 2) {
        if (getenv("GLASSINE_SWAYD_TEST")) test_drag(lv, f);
        return;
    }
    if (sscanf(m, "swap %lx %lx", &a, &b) == 2) {
        swap_ids((long)a, (long)b);
    } else if (sscanf(m, "swapdir %7s", dir) == 1) {
        int dx = !strcmp(dir, "left") ? -1 : !strcmp(dir, "right"), dy = !strcmp(dir, "up") ? -1 : !strcmp(dir, "down");
        long f = 0, n = (dx || dy) ? neighbour(dx, dy) : 0;
        for (int i = 0; i < nws && n; i++)
            for (int k = 0; ws[i].focused && k < ws[i].nv; k++) if (ws[i].v[k].focused) f = ws[i].v[k].id;
        if (n && f) swap_ids(f, n);
        else LOG("swapdir %s: no tile that way", dir);
    } else if (!strncmp(m, "apps?", 5)) {   /* glassine-epdd-desktop (re)started: the app list again */
        app_list_sent[0] = 0;
    } else if (sscanf(m, "turn %lx", &a) == 1) {       /* the overview's Turn: both levels the other way */
        int ia;
        Ws *wa = ws_of_view((long)a, &ia);
        Keep *k = wa ? keep_of(wa->name) : NULL;
        if (!k || k->n < 2) return;
        k->orient[0] = !k->orient[0];
        k->orient[1] = !k->orient[1];
        build(k); crun();
    } else if (sscanf(m, "orient %d %d", &lv, &side) == 2 && lv >= 0 && lv <= 1) {
        for (int i = 0; i < nws; i++) {
            if (!ws[i].focused) continue;
            Keep *k = keep_of(ws[i].name);
            if (!k) return;
            k->orient[lv] = side != 0;
            if (k->n == 4) k->orient[!lv] = !k->orient[lv];   /* the quarters: one split each way */
            build(k); crun();
        }
    }
}

/* ---- main ---------------------------------------------------------------------------------- */
static volatile sig_atomic_t quit;
static void on_term(int s) { (void)s; quit = 1; }

/* a `full` window or Home that no longer covers its workspace's area (the output
 * turned): over it again. 1: changed */
static int covers(void)
{
    int did = 0;
    for (int i = 0; i < nws; i++) {
        const View *v = ws[i].full ? view_by_id(&ws[i], ws[i].full) : NULL;
        if (v && memcmp(v->r, ws[i].r, sizeof v->r)) { cover(v->id, &ws[i]); did = 1; }
        if (home_on[0] && !strcmp(home_on, ws[i].name) && memcmp(home_r, ws[i].r, sizeof home_r)) { cover(home_id, &ws[i]); did = 1; }
    }
    if (did) crun();
    return did;
}

static void settle_state(void)
{
    /* the rules until nothing changes (each may move windows: re-read) */
    for (int round = 0; round < 8 && read_tree(); round++) {
        keep_prune();
        ph_arm();
        ph_substitute();
        if (fresh()) continue;
        if (empty_screen()) continue;
        if (overflow()) continue;
        if (renumber()) continue;
        layout_all();
        if (covers()) continue;
        break;
    }
    if (read_tree()) { hints(); handles_update(0); pf_note(); home_send(0); }
}

/* Home's app_id (see home_app) and the rule that keeps it in the scratchpad from its map */
static void home_init(void)
{
    const char *e = getenv("GLASSINE_HOME_APP_ID");
    char b[128] = "";
    if (e && *e) snprintf(b, sizeof b, "%s", e);
    else {
        FILE *f = fopen("/etc/glassine/home-app", "r");
        if (f) { if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f); }
        b[strcspn(b, " \t\r\n")] = 0;
    }
    if (*b) snprintf(home_app, sizeof home_app, "%s", b);
    char re[sizeof home_app];
    snprintf(re, sizeof re, "%s", home_app);
    for (char *q = re; *q; q++) if (!isalnum((unsigned char)*q) && *q != '_' && *q != '-') *q = '.';
    cadd("for_window [app_id=\"^%s$\"] move scratchpad", re);
    crun();
    LOG("home: app_id %s", home_app);
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--kbd-test")) {      /* (host test) */
        char *ij = read_file(argv[2]), *dev = read_file(argv[3]), names[512];
        json_object *in = ij ? sj_parse(ij) : NULL;
        int n = kbd_scan(in, dev, names, sizeof names);
        printf("%d%s%s\n", n, n ? " " : "", names);
        return 0;
    }
    if (argc > 1 && argv[1][0] != '-') {                    /* a command for the running one */
        char m[256];
        size_t o = 0;
        for (int i = 1; i < argc && o < sizeof m; i++) o += snprintf(m + o, sizeof m - o, "%s%s", i > 1 ? " " : "", argv[i]);
        const char *rt = getenv("XDG_RUNTIME_DIR");
        struct sockaddr_un sa = { .sun_family = AF_UNIX };
        snprintf(sa.sun_path, sizeof sa.sun_path, "%s/glassine-swayd", rt && *rt ? rt : "/tmp");
        int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0 || sendto(fd, m, strlen(m), 0, (struct sockaddr *)&sa, sizeof sa) < 0) {
            fprintf(stderr, "glassine-swayd: not running (%s)\n", sa.sun_path);
            return 1;
        }
        return 0;
    }
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) verbose = 1;
    settle = getenv("GLASSINE_SWAYD_SETTLE") && atoi(getenv("GLASSINE_SWAYD_SETTLE"));
    cmd_fd = sipc_open();
    ev_fd = sipc_open();
    if (cmd_fd < 0 || ev_fd < 0) { fprintf(stderr, "glassine-swayd: no sway IPC ($SWAYSOCK)\n"); return 1; }
    uint32_t t;
    char *ok;
    if (!sipc_send(ev_fd, SIPC_SUBSCRIBE, "[\"window\",\"workspace\",\"output\",\"input\"]") || !(ok = sipc_recv(ev_fd, &t))) {
        fprintf(stderr, "glassine-swayd: subscribe failed\n");
        return 1;
    }
    free(ok);
    const char *rt = getenv("XDG_RUNTIME_DIR");
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s/glassine-swayd", rt && *rt ? rt : "/tmp");
    unlink(sa.sun_path);
    int sfd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (bind(sfd, (struct sockaddr *)&sa, sizeof sa)) { perror("glassine-swayd: bind"); return 1; }
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);           /* glassine-osk runs (hd_text_focus): no zombies */
    read_tree();                        /* the output scale first */
    home_init();
    hd_fd = hd_init(scale);
    int ino = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);  /* the shell's theme: the handles' look */
    if (ino >= 0 && rt && *rt) inotify_add_watch(ino, rt, IN_MOVED_TO | IN_CLOSE_WRITE);
    settle_state();
    home_send(1);
    kbd_update();
    LOG("running%s", hd_fd >= 0 ? " (drag handles)" : "");
    while (!quit) {
        hd_flush();
        struct pollfd pf[4 + OSK_TAP_DEVS] = { { ev_fd, POLLIN, 0 }, { sfd, POLLIN, 0 }, { hd_fd, POLLIN, 0 }, { ino, POLLIN, 0 } };
        int nt = ntap;
        for (int i = 0; i < nt; i++) pf[4 + i] = (struct pollfd){ tap_fd[i], POLLIN, 0 };
        if (poll(pf, 4 + nt, tap_wait()) < 0) { if (errno == EINTR) continue; break; }
        for (int i = 0; i < nt; i++) if (pf[4 + i].revents & POLLIN) tap_read(i);
        tap_wait();
        if (pf[2].revents & (POLLIN | POLLHUP | POLLERR)) {
            if (!hd_dispatch()) { fprintf(stderr, "glassine-swayd: Wayland gone\n"); goto out; }
            if (hd_lost()) { WARN("a handle surface was closed: placing the handles again"); if (read_tree()) handles_update(1); }
        }
        if (pf[3].revents & POLLIN) {
            char b[4096];
            ssize_t l;
            int theme = 0;
            while ((l = read(ino, b, sizeof b)) > 0)
                for (char *q = b; q < b + l;) {
                    struct inotify_event *ie = (struct inotify_event *)q;
                    if (ie->len && !strcmp(ie->name, "glassine-shell.theme")) theme = 1;
                    q += sizeof *ie + ie->len;
                }
            if (theme) hd_theme_changed();
        }
        int dirty = 0;
        if (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            /* this event and whatever else is queued: one pass for all */
            do {
                char *m = sipc_recv(ev_fd, &t);
                if (!m) { fprintf(stderr, "glassine-swayd: sway gone\n"); goto out; }
                json_object *j = sj_parse(m);
                free(m);
                const char *ch = j ? sj_str(j, "change") : NULL;
                if (t == SIPC_EV_INPUT) {      /* a device came or went: no layout change */
                    if (ch && (!strcmp(ch, "added") || !strcmp(ch, "removed"))) kbd_update();
                } else if (ch && strcmp(ch, "title") && strcmp(ch, "mark") && strcmp(ch, "urgent")) dirty = 1;
                json_object_put(j);
                pf[0].revents = 0;
            } while (poll(pf, 1, 0) > 0 && (pf[0].revents & POLLIN));
        }
        if (pf[1].revents & POLLIN) {
            char m[256];
            ssize_t l;
            while ((l = recv(sfd, m, sizeof m - 1, 0)) > 0) {
                m[l] = 0;
                if (read_tree()) command(m);
                dirty = 1;
            }
        }
        if (dirty) settle_state();
    }
out:
    unlink(sa.sun_path);
    return 0;
}
