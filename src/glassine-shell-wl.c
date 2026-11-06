/* glassine-shell-wl.c — glassine-shell's Wayland backend (the sway session).
 *
 * Surfaces: layer-shell surfaces (wlr-layer-shell-unstable-v1). The bar is on
 * the top layer, anchored to the top edge with an exclusive zone (sway's
 * windows start below it); popups and the resize rail are on the overlay
 * layer at an absolute position (exclusive zone -1). Pixels go out as wl_shm
 * buffers at the output's scale (device px); a surface is a multiple of the
 * scale in size and place (be_align). The popup's pointer grab of the X
 * backend is a transparent full-screen surface on the top layer ("backdrop"):
 * a tap outside the popup lands there (the bar region included: the common
 * part picks the bar by y), as with the grab.
 * Input: wl_pointer / wl_touch (the first finger) straight from sway, no
 * window manager grab in between: a press is also the "raw" press (ev_raw),
 * so holds are timed from the real finger. The search field takes the
 * keyboard (keyboard interactivity exclusive, xkbcommon for the keys).
 * Windows and workspaces: sway's IPC (GET_WORKSPACES, GET_TREE, commands),
 * events on a second connection subscribed to workspace + window. A workspace
 * index is its place among the existing workspaces in number order (sway
 * removes an empty one it is not on, as glassine-winfit does on X); a window is
 * its container id. glassine-winfit requests go to glassine-swayd
 * ($XDG_RUNTIME_DIR/glassine-swayd, a datagram, when it runs); the theme name to
 * $XDG_RUNTIME_DIR/glassine-shell.theme. glassine-epdd-desktop learns about the popups from
 * here ("popup X Y W H" / "popup gone ..." on its FIFO; on X it sees them map).
 * A close asks glassine-bgprio (-sway) to thaw the app first (/run/glassine-bgprio.sock,
 * "thaw CON_ID").
 * Launch placeholders (be_placeholder): an xdg toplevel (app_id
 * glassine-placeholder, title "glassine-ph PID ID...") with the app's icon and name,
 * mapped at the tap; glassine-swayd gives its tile to the app's first matching
 * window and closes it. Gone by itself after PH_MAX_S, or PH_GONE_S after the
 * started process exited without a window. Left out of the window list. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <glib.h>
#include <signal.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"
#include "glassine-shell-be.h"
#include "wayland/sway-ipc.h"

int be_align = 1;
int be_impostors = 0;                   /* glassine-epdd-desktop has no impostors on the wl source */
static struct wl_display *wd;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *lsh;
static struct zwlr_screencopy_manager_v1 *scm;   /* the overview's stills (be_snap) */
static void snap_later(void);
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct wl_output *out;
static struct wl_pointer *ptr;
static struct wp_cursor_shape_manager_v1 *csm;   /* the pointer's image over the shell (sway draws it) */
static struct wp_cursor_shape_device_v1 *csd;
static struct wl_touch *tch;
static struct wl_keyboard *kbd;
static struct xkb_context *xkb;
static struct xkb_keymap *keymap;
static struct xkb_state *xst;
static int out_w, out_h, out_tr, osc = 1;      /* out_tr: the output transform (mode is untransformed) */
static int epfd = -1, ipc_cmd = -1, ipc_ev = -1;
/* the layer surfaces' configures and the buffer releases: a queue of their own,
 * so waiting for them (inside a render) never runs an input handler */
static struct wl_event_queue *sq;

/* ============================================================================
 * surfaces
 * ========================================================================== */
typedef struct { struct wl_buffer *b; uint32_t *px; size_t size; int busy; } Buf;
#define NBUF 3
typedef struct {
    struct wl_surface *s;
    struct zwlr_layer_surface_v1 *ls;
    Surf *owner;                        /* NULL: the backdrop */
    int configured, mapped, kbd;
    int stale;                          /* unmapping: configures sent before it are not acked */
    int w, h;                           /* buffer size, device px */
    Buf buf[NBUF]; int cur;             /* cur: the buffer with the latest image (-1: none) */
} WSurf;
static WSurf backdrop;

static void buf_release(void *data, struct wl_buffer *b) { (void)b; ((Buf *)data)->busy = 0; }
static const struct wl_buffer_listener buf_lis = { buf_release };

static void buf_free(Buf *b)
{
    if (b->b) wl_buffer_destroy(b->b);
    if (b->px) munmap(b->px, b->size);
    memset(b, 0, sizeof *b);
}

static int buf_new(Buf *b, int w, int h)
{
    size_t size = (size_t)w * h * 4;
    int fd = memfd_create("glassine-shell", MFD_CLOEXEC);
    if (fd < 0) return 0;
    if (ftruncate(fd, size)) { close(fd); return 0; }
    void *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) { close(fd); return 0; }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    b->b = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_proxy_set_queue((struct wl_proxy *)b->b, sq);
    b->px = px; b->size = size; b->busy = 0;
    wl_buffer_add_listener(b->b, &buf_lis, b);
    return 1;
}

static void ls_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h)
{
    (void)w; (void)h;
    if (((WSurf *)data)->stale) return;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    ((WSurf *)data)->configured = 1;
}

static void ls_closed(void *data, struct zwlr_layer_surface_v1 *ls)
{
    (void)ls;
    WSurf *w = data;
    w->configured = w->mapped = 0;
}
static const struct zwlr_layer_surface_v1_listener ls_lis = { ls_configure, ls_closed };

static void wsurf_create(WSurf *w, uint32_t layer)
{
    w->s = wl_compositor_create_surface(comp);
    wl_surface_set_user_data(w->s, w);
    wl_surface_set_buffer_scale(w->s, osc);
    w->ls = zwlr_layer_shell_v1_get_layer_surface(lsh, w->s, out, layer, "glassine-shell");
    wl_proxy_set_queue((struct wl_proxy *)w->ls, sq);
    zwlr_layer_surface_v1_add_listener(w->ls, &ls_lis, w);
    w->cur = -1;
}

static WSurf *ws_of(Surf *s)
{
    if (!s->be) {
        WSurf *w = calloc(1, sizeof *w);
        w->owner = s;
        wsurf_create(w, s->dock ? ZWLR_LAYER_SHELL_V1_LAYER_TOP : ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
        s->be = w;
    }
    return s->be;
}

/* the initial (or, after an unmap, the next) configure before a buffer */
static void wsurf_configure(WSurf *w)
{
    if (w->configured) return;
    wl_surface_attach(w->s, NULL, 0, 0);
    wl_surface_commit(w->s);
    for (int i = 0; i < 20 && !w->configured; i++)
        if (wl_display_roundtrip_queue(wd, sq) < 0) break;
}

static Buf *wsurf_free_buf(WSurf *w)
{
    for (int tries = 0; tries < 50; tries++) {
        for (int i = 0; i < NBUF; i++)
            if (i != w->cur && w->buf[i].b && !w->buf[i].busy) return &w->buf[i];
        for (int i = 0; i < NBUF; i++)
            if (!w->buf[i].b) return buf_new(&w->buf[i], w->w, w->h) ? &w->buf[i] : NULL;
        if (wl_display_roundtrip_queue(wd, sq) < 0) break;     /* all in use: until sway lets one go */
    }
    return NULL;
}

static void wsurf_show(WSurf *w, int x, int y, int ww, int hh)
{
    wsurf_configure(w);
    wl_surface_attach(w->s, w->buf[w->cur].b, 0, 0);
    wl_surface_damage_buffer(w->s, x, y, ww, hh);
    wl_surface_commit(w->s);
    w->buf[w->cur].busy = 1;
    w->mapped = 1;
}

static void wsurf_hide(WSurf *w)
{
    if (!w->mapped) return;
    wl_surface_attach(w->s, NULL, 0, 0);
    wl_surface_commit(w->s);
    w->mapped = 0;
    w->configured = 0;                  /* a null buffer unmaps: the next map is configured anew */
    /* a configure sway sent while it was mapped (e.g. wvkbd appearing changes
     * the layout) is void after the unmap; acking it on the next map is a
     * protocol error ("wrong configure serial", seen in the harness). Sway
     * sends an unmapped layer surface none, so everything that comes before
     * this roundtrip's reply is stale. */
    w->stale = 1;
    wl_display_roundtrip_queue(wd, sq);
    w->stale = 0;
}

/* a surface's rect in glassine-epdd-desktop's frame: portrait px (glassine-swayd's portrait:
 * the layout turned back by glassine-rotd's turn, clockwise as sway's transform) */
static void epdd_rect(const Surf *s, int o[4])
{
    int rot = 0;
    FILE *rf = fopen("/run/glassine-rotation", "r");
    if (rf) { if (fscanf(rf, "%d", &rot) != 1) rot = 0; fclose(rf); }
    switch (rot) {
    case 270: o[0] = s->y; o[1] = SW - s->x - s->wd; o[2] = s->ht; o[3] = s->wd; break;
    case 90:  o[0] = SH - s->y - s->ht; o[1] = s->x; o[2] = s->ht; o[3] = s->wd; break;
    case 180: o[0] = SW - s->x - s->wd; o[1] = SH - s->y - s->ht; o[2] = s->wd; o[3] = s->ht; break;
    default:  o[0] = s->x; o[1] = s->y; o[2] = s->wd; o[3] = s->ht;
    }
}

/* "shell ID ..." by the surface (its address: fixed for the process) */
static unsigned surf_id(const Surf *s) { return (unsigned)((uintptr_t)s >> 3); }
static void shell_hint(const Surf *s, const Surf *at, int gone)
{
    char c[80];
    int r[4];
    epdd_rect(at, r);
    if (gone) snprintf(c, sizeof c, "shell gone %x\n", surf_id(s));
    else snprintf(c, sizeof c, "shell %x %d %d %d %d\n", surf_id(s), r[0], r[1], r[2], r[3]);
    epdd(c);
}

void be_surf_config(Surf *s, int x, int y, int w, int h, int resized)
{
    WSurf *b = ws_of(s);
    int moved = s->x != x || s->y != y;
    if (b->mapped && (moved || resized)) {      /* a mapped surface changes: glassine-epdd-desktop's calm box with it */
        Surf n = *s;
        n.x = x; n.y = y; n.wd = w; n.ht = h;
        shell_hint(s, &n, 0);
    }
    if (resized) {
        for (int i = 0; i < NBUF; i++) buf_free(&b->buf[i]);
        b->cur = -1;
        b->w = w; b->h = h;
    }
    zwlr_layer_surface_v1_set_size(b->ls, w / osc, h / osc);
    if (s->dock) {
        zwlr_layer_surface_v1_set_anchor(b->ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(b->ls, h / osc);
    } else {
        zwlr_layer_surface_v1_set_anchor(b->ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
        zwlr_layer_surface_v1_set_margin(b->ls, y / osc, 0, 0, x / osc);
        zwlr_layer_surface_v1_set_exclusive_zone(b->ls, -1);
    }
    if (b->mapped && moved && !resized) wl_surface_commit(b->s);    /* a put follows a resize, not a move */
}

void be_surf_put(Surf *s, int x, int y, int w, int h)
{
    WSurf *b = ws_of(s);
    Buf *nb = wsurf_free_buf(b);
    if (!nb) { LOG("wl: no free buffer"); return; }
    const uint32_t *p = (const uint32_t *)cairo_image_surface_get_data(s->img);
    int stride = cairo_image_surface_get_stride(s->img) / 4;
    for (int r = 0; r < b->h; r++) memcpy(nb->px + (size_t)r * b->w, p + (size_t)r * stride, (size_t)b->w * 4);
    b->cur = (int)(nb - b->buf);
    if (b->mapped) wsurf_show(b, x, y, w, h);
}

void be_surf_shape(Surf *s) { surf_harden(s, NULL); }   /* the alpha is the outline */

/* a popup's area to glassine-epdd-desktop: a fast first paint / one partial of what it
 * uncovered; and every shell surface (the bar too) as "shell": calm paints
 * there while it is mapped (no press/motion:) */
static void popup_hint(Surf *s, int gone)
{
    char c[160];
    int r[4];
    epdd_rect(s, r);
    if (!s->dock) {
        snprintf(c, sizeof c, "popup %s%d %d %d %d\n", gone ? "gone " : "", r[0], r[1], r[2], r[3]);
        epdd(c);
    }
    shell_hint(s, s, gone);
}

void be_surf_map(Surf *s)
{
    WSurf *b = ws_of(s);
    if (b->cur < 0) return;
    popup_hint(s, 0);                   /* first: the policy knows before the frame */
    wsurf_show(b, 0, 0, b->w, b->h);
}

void be_surf_unmap(Surf *s)
{
    if (!s->be || !((WSurf *)s->be)->mapped) return;
    wsurf_hide(s->be);
    popup_hint(s, 1);
    if (s == &pop) snap_later();        /* the screen without the popup: a still of it */
}

int be_grab_pointer(Surf *s)
{
    (void)s;
    if (!backdrop.s) {
        wsurf_create(&backdrop, ZWLR_LAYER_SHELL_V1_LAYER_TOP);
        zwlr_layer_surface_v1_set_anchor(backdrop.ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                      ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(backdrop.ls, -1);
        backdrop.w = SW; backdrop.h = SH;
        if (!buf_new(&backdrop.buf[0], SW, SH)) return 0;     /* all zero: transparent */
        backdrop.cur = 0;
    }
    if (backdrop.buf[0].busy) return 1;
    wsurf_show(&backdrop, 0, 0, SW, SH);
    return 1;
}

void be_ungrab_pointer(void) { wsurf_hide(&backdrop); }

/* the input region (surface coordinates): `all` minus the rects, or just the rects */
static void wsurf_input(WSurf *w, int all, const int (*r)[4], int n, int ox, int oy)
{
    if (!w || !w->s) return;
    if (!all && !n) wl_surface_set_input_region(w->s, NULL);    /* everywhere */
    else {
        struct wl_region *g = wl_compositor_create_region(comp);
        if (all) wl_region_add(g, 0, 0, INT32_MAX, INT32_MAX);
        for (int i = 0; i < n; i++) {
            int x = (r[i][0] - ox) / osc, y = (r[i][1] - oy) / osc, ww = r[i][2] / osc, hh = r[i][3] / osc;
            if (all) wl_region_subtract(g, x, y, ww, hh);
            else wl_region_add(g, x, y, ww, hh);
        }
        wl_surface_set_input_region(w->s, g);
        wl_region_destroy(g);
    }
    if (w->mapped) wl_surface_commit(w->s);     /* (else it waits for the map) */
}

void be_grab_holes(const int (*r)[4], int n) { wsurf_input(&backdrop, n > 0, r, n, 0, 0); }
void be_pop_input(const int (*r)[4], int n) { wsurf_input(pop.be, 0, r, n, pop.x, pop.y); }

static void set_kbd(Surf *s, int on)
{
    WSurf *b = ws_of(s);
    if (b->kbd == on) return;
    b->kbd = on;
    zwlr_layer_surface_v1_set_keyboard_interactivity(b->ls, on ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE :
                                                                 ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    if (b->mapped) wl_surface_commit(b->s);
}

void be_grab_keyboard(Surf *s) { set_kbd(s, 1); }
void be_ungrab_keyboard(void) { set_kbd(&pop, 0); }

/* wvkbd (started hidden by the sway config), through glassine-osk */
const char *be_osk_cmd(int show) { return show ? "/usr/bin/glassine-osk show" : "/usr/bin/glassine-osk hide"; }

/* ============================================================================
 * input
 * ========================================================================== */
static void origin(struct wl_surface *s, int *x, int *y)
{
    WSurf *w = s ? wl_surface_get_user_data(s) : NULL;
    *x = w && w->owner ? w->owner->x : 0;
    *y = w && w->owner ? w->owner->y : 0;
}

static struct { struct wl_surface *s; double x, y; } pt;        /* the pointer: its surface, logical px there */

static void pt_dev(int *x, int *y)
{
    origin(pt.s, x, y);
    *x += (int)(pt.x * osc); *y += (int)(pt.y * osc);
}

static void p_enter(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)serial;
    /* the default arrow: the pointer shows on the panel since (glassine-epdd-desktop
     * paints it). Before, NULL hid it over the bar and the popups: it seemed not to
     * reach the bar. (sway-glassine 0009 keeps it hidden after the pen and touch) */
    if (csm && !csd) csd = wp_cursor_shape_manager_v1_get_pointer(csm, p);
    if (csd) wp_cursor_shape_device_v1_set_shape(csd, serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
    else wl_pointer_set_cursor(p, serial, NULL, 0, 0);
    pt.s = s; pt.x = wl_fixed_to_double(x); pt.y = wl_fixed_to_double(y);
}
static void p_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) { (void)d; (void)p; (void)serial; (void)s; pt.s = NULL; }
static int pt_down;                     /* BTN_LEFT held: motion is a drag */
static void p_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)p; (void)t;
    pt.x = wl_fixed_to_double(x); pt.y = wl_fixed_to_double(y);
    if (pt_down) { int dx, dy; pt_dev(&dx, &dy); ev_motion(dx, dy); }
}
static void p_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t t, uint32_t button, uint32_t state)
{
    (void)d; (void)p; (void)serial; (void)t;
    int x, y;
    pt_dev(&x, &y);
    int down = state == WL_POINTER_BUTTON_STATE_PRESSED;
    if (button == BTN_LEFT) {
        pt_down = down;
        if (down) { ev_raw(1, 0); ev_pointer(1, x, y); }
        else { ev_pointer(0, x, y); ev_raw(0, 1); }
    } else if (button == BTN_RIGHT && down) ev_long_press(x, y);
}
static void p_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) { (void)d; (void)p; (void)t; (void)a; (void)v; }
static void p_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; }
static void p_axis_source(void *d, struct wl_pointer *p, uint32_t s) { (void)d; (void)p; (void)s; }
static void p_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; }
static void p_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t v) { (void)d; (void)p; (void)a; (void)v; }
static const struct wl_pointer_listener ptr_lis = { p_enter, p_leave, p_motion, p_button, p_axis, p_frame, p_axis_source,
                                                    p_axis_stop, p_axis_discrete };

/* the first finger is the pointer; the others are ignored (sway's three-finger
 * swipe cancels the touch) */
static struct { int id; int x, y; } tp = { -1, 0, 0 };
static struct wl_surface *tp_s;         /* the surface the finger went down on (t_motion is relative to it) */

static void t_down(void *d, struct wl_touch *t, uint32_t serial, uint32_t time, struct wl_surface *s, int32_t id,
                   wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)t; (void)serial; (void)time;
    if (tp.id >= 0) return;
    int ox, oy;
    origin(s, &ox, &oy);
    tp.id = id;
    tp_s = s;
    tp.x = ox + (int)(wl_fixed_to_double(x) * osc);
    tp.y = oy + (int)(wl_fixed_to_double(y) * osc);
    LOG("touch %d begin t=%.0f", id, now_ms());
    ev_raw(1, 0);
    ev_pointer(1, tp.x, tp.y);
}
static void t_up(void *d, struct wl_touch *t, uint32_t serial, uint32_t time, int32_t id)
{
    (void)d; (void)t; (void)serial; (void)time;
    if (id != tp.id) return;
    tp.id = -1;
    LOG("touch %d end t=%.0f", id, now_ms());
    ev_pointer(0, tp.x, tp.y);
    ev_raw(0, 1);
}
/* a drag (the range slider) sees the finger move; the lift stays at the press
 * point (taps as before: a finger drifting off a button still taps it) */
static void t_motion(void *d, struct wl_touch *t, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    (void)d; (void)t; (void)time;
    if (id != tp.id || !tp_s) return;
    int ox, oy;
    origin(tp_s, &ox, &oy);
    ev_motion(ox + (int)(wl_fixed_to_double(x) * osc), oy + (int)(wl_fixed_to_double(y) * osc));
}
static void t_frame(void *d, struct wl_touch *t) { (void)d; (void)t; }
static void t_cancel(void *d, struct wl_touch *t)
{
    (void)d; (void)t;
    if (tp.id < 0) return;
    tp.id = -1;
    LOG("touch cancelled");
    ev_pointer(0, -1, -1);              /* a release on nothing: the press ends without an action */
    ev_raw(0, 1);
}
static void t_shape(void *d, struct wl_touch *t, int32_t id, wl_fixed_t a, wl_fixed_t b) { (void)d; (void)t; (void)id; (void)a; (void)b; }
static void t_orient(void *d, struct wl_touch *t, int32_t id, wl_fixed_t o) { (void)d; (void)t; (void)id; (void)o; }
static const struct wl_touch_listener tch_lis = { t_down, t_up, t_motion, t_frame, t_cancel, t_shape, t_orient };

static void k_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int32_t fd, uint32_t size)
{
    (void)d; (void)k;
    if (fmt != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    char *m = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return;
    if (xst) xkb_state_unref(xst);
    if (keymap) xkb_keymap_unref(keymap);
    keymap = xkb_keymap_new_from_string(xkb, m, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(m, size);
    xst = keymap ? xkb_state_new(keymap) : NULL;
}
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f, struct wl_array *a) { (void)d; (void)k; (void)s; (void)f; (void)a; }
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f) { (void)d; (void)k; (void)s; (void)f; }
static void k_key(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t t, uint32_t key, uint32_t state)
{
    (void)d; (void)k; (void)serial; (void)t;
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !xst) return;
    xkb_keysym_t ks = xkb_state_key_get_one_sym(xst, key + 8);
    char b[16];
    int n = xkb_state_key_get_utf8(xst, key + 8, b, sizeof b);
    if (n >= (int)sizeof b) n = 0;
    ev_key(ks == XKB_KEY_Escape ? SK_ESC : ks == XKB_KEY_BackSpace ? SK_BACKSPACE :
           ks == XKB_KEY_Return || ks == XKB_KEY_KP_Enter ? SK_ENTER : ks == XKB_KEY_Tab ? SK_TAB : ks == XKB_KEY_ISO_Left_Tab ? SK_BACKTAB :
           ks == XKB_KEY_Left ? SK_LEFT : ks == XKB_KEY_Right ? SK_RIGHT : SK_OTHER, b, n);
}
static void k_mods(void *d, struct wl_keyboard *k, uint32_t serial, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp)
{
    (void)d; (void)k; (void)serial;
    if (xst) xkb_state_update_mask(xst, dep, lat, lock, 0, 0, grp);
}
static void k_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t del) { (void)d; (void)k; (void)r; (void)del; }
static const struct wl_keyboard_listener kbd_lis = { k_keymap, k_enter, k_leave, k_key, k_mods, k_repeat };

static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
    (void)d;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !ptr) { ptr = wl_seat_get_pointer(s); wl_pointer_add_listener(ptr, &ptr_lis, NULL); }
    if (!(caps & WL_SEAT_CAPABILITY_POINTER) && ptr) {
        if (csd) { wp_cursor_shape_device_v1_destroy(csd); csd = NULL; }
        wl_pointer_release(ptr); ptr = NULL;
    }
    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !tch) { tch = wl_seat_get_touch(s); wl_touch_add_listener(tch, &tch_lis, NULL); }
    if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && tch) { wl_touch_release(tch); tch = NULL; }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !kbd) { kbd = wl_seat_get_keyboard(s); wl_keyboard_add_listener(kbd, &kbd_lis, NULL); }
    if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && kbd) { wl_keyboard_release(kbd); kbd = NULL; }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_lis = { seat_caps, seat_name };

/* ============================================================================
 * the output, the registry
 * ========================================================================== */
static void o_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph, int32_t sp,
                       const char *mk, const char *md, int32_t tr)
{ (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sp; (void)mk; (void)md; out_tr = tr; }
static void o_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t r)
{
    (void)d; (void)o; (void)r;
    if (flags & WL_OUTPUT_MODE_CURRENT) { out_w = w; out_h = h; }
}
/* the output turned (glassine-rotd: sway's output transform): the layout is made for
 * one size, so exit with RESIZE_EXIT; glassine-shell-wl-run starts it again at once,
 * without counting a death */
#define RESIZE_EXIT 75
static int be_ready;
static void o_done(void *d, struct wl_output *o)
{
    (void)d; (void)o;
    if (!be_ready) return;
    int w = out_tr & 1 ? out_h : out_w, h = out_tr & 1 ? out_w : out_h;
    if (w == SW && h == SH) return;
    fprintf(stderr, "glassine-shell: output now %dx%d (was %dx%d): restart\n", w, h, SW, SH);
    exit(RESIZE_EXIT);
}
static void o_scale(void *d, struct wl_output *o, int32_t f) { (void)d; (void)o; osc = f < 1 ? 1 : f; }
static const struct wl_output_listener out_lis = { o_geometry, o_mode, o_done, o_scale };

/* the output to use: $GLASSINE_SHELL_OUTPUT's name if given (wl_output v4), else the first */
/* ============================================================================
 * launch placeholders (see the top)
 * ========================================================================== */
#define PH_MAX_S 30
#define PH_GONE_S 5
#define NPH 4
static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial) { (void)d; xdg_wm_base_pong(b, serial); }
static const struct xdg_wm_base_listener wm_lis = { wm_ping };

typedef struct {
    struct wl_surface *s;
    struct xdg_surface *xs;
    struct xdg_toplevel *top;
    App app;                            /* a copy: the list may be re-read */
    int pid, w, h;                      /* configured size, device px (0: not yet) */
    double t0, gone_t;                  /* opened; the started process seen gone */
    Buf buf;
} Ph;
static Ph ph[NPH];
static guint ph_timer;

static void ph_close(Ph *p)
{
    if (!p->s) return;
    LOG("placeholder %s closed (%.1f s)", p->app.name, (now_ms() - p->t0) / 1000);
    xdg_toplevel_destroy(p->top);
    xdg_surface_destroy(p->xs);
    wl_surface_destroy(p->s);
    buf_free(&p->buf);
    memset(p, 0, sizeof *p);
    wl_display_flush(wd);
}

static void ph_draw(Ph *p)
{
    if (!p->w || !p->h) return;
    if (p->buf.b && (p->buf.size != (size_t)p->w * p->h * 4 || p->buf.busy)) buf_free(&p->buf);
    if (!p->buf.b && !buf_new(&p->buf, p->w, p->h)) return;
    cairo_surface_t *cs = cairo_image_surface_create_for_data((unsigned char *)p->buf.px, CAIRO_FORMAT_ARGB32, p->w, p->h, p->w * 4);
    cairo_t *cr = cairo_create(cs);
    ev_placeholder_draw(cr, p->w, p->h, &p->app);
    cairo_destroy(cr);
    cairo_surface_destroy(cs);
    wl_surface_set_buffer_scale(p->s, osc);
    wl_surface_attach(p->s, p->buf.b, 0, 0);
    wl_surface_damage_buffer(p->s, 0, 0, p->w, p->h);
    p->buf.busy = 1;
    wl_surface_commit(p->s);
    wl_display_flush(wd);
}

static void ph_xs_configure(void *d, struct xdg_surface *xs, uint32_t serial)
{
    Ph *p = d;
    xdg_surface_ack_configure(xs, serial);
    ph_draw(p);
}
static const struct xdg_surface_listener ph_xs_lis = { ph_xs_configure };

static void ph_top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
    (void)t; (void)st;
    Ph *p = d;
    p->w = (w > 0 ? w : 300) * osc;     /* logical -> device px */
    p->h = (h > 0 ? h : 300) * osc;
}
static void ph_top_close(void *d, struct xdg_toplevel *t) { (void)t; ph_close(d); }   /* glassine-swayd's kill */
static const struct xdg_toplevel_listener ph_top_lis = { ph_top_configure, ph_top_close };

static gboolean ph_tick(gpointer u)
{
    (void)u;
    double t = now_ms();
    int live = 0;
    for (int i = 0; i < NPH; i++) {
        Ph *p = &ph[i];
        if (!p->s) continue;
        if (p->pid > 0 && !p->gone_t && kill(p->pid, 0) && errno == ESRCH) p->gone_t = t;
        if (t - p->t0 > PH_MAX_S * 1000 || (p->gone_t && t - p->gone_t > PH_GONE_S * 1000)) ph_close(p);
        else live = 1;
    }
    if (!live) ph_timer = 0;
    return live;
}

void be_placeholder(const App *a, int pid)
{
    if (!wm) return;
    Ph *p = NULL;
    for (int i = 0; i < NPH && !p; i++) if (!ph[i].s) p = &ph[i];
    if (!p) { ph_close(&ph[0]); p = &ph[0]; }
    *p = (Ph){ .app = *a, .pid = pid, .t0 = now_ms() };
    p->s = wl_compositor_create_surface(comp);
    p->xs = xdg_wm_base_get_xdg_surface(wm, p->s);
    xdg_surface_add_listener(p->xs, &ph_xs_lis, p);
    p->top = xdg_surface_get_toplevel(p->xs);
    xdg_toplevel_add_listener(p->top, &ph_top_lis, p);
    /* what glassine-swayd matches the app's window by: the pid, then the ids
     * (desktop id, its last dotted part, StartupWMClass, the executable) */
    char t[400];
    const char *dot = strrchr(a->id, '.');
    snprintf(t, sizeof t, "glassine-ph %d %s %s %s %s", pid, a->id, dot ? dot + 1 : "", a->wmclass, a->exe);
    xdg_toplevel_set_title(p->top, t);
    xdg_toplevel_set_app_id(p->top, "glassine-placeholder");
    wl_surface_commit(p->s);            /* no buffer: asks for the first configure */
    wl_display_flush(wd);
    if (!ph_timer) ph_timer = g_timeout_add_seconds(1, ph_tick, NULL);
    LOG("placeholder for %s (pid %d)", a->name, pid);
}

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    (void)d;
    if (!strcmp(iface, wl_compositor_interface.name)) comp = wl_registry_bind(r, name, &wl_compositor_interface, ver < 4 ? ver : 4);
    else if (!strcmp(iface, wl_shm_interface.name)) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, xdg_wm_base_interface.name)) {
        wm = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm, &wm_lis, NULL);
    } else if (!strcmp(iface, wp_cursor_shape_manager_v1_interface.name))
        csm = wl_registry_bind(r, name, &wp_cursor_shape_manager_v1_interface, 1);
    else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
        lsh = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, ver < 4 ? ver : 4);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat) {
        seat = wl_registry_bind(r, name, &wl_seat_interface, ver < 5 ? ver : 5);
        wl_seat_add_listener(seat, &seat_lis, NULL);
    } else if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name)) {
        scm = wl_registry_bind(r, name, &zwlr_screencopy_manager_v1_interface, 1);
    } else if (!strcmp(iface, wl_output_interface.name) && !out) {
        out = wl_registry_bind(r, name, &wl_output_interface, ver < 2 ? ver : 2);
        wl_output_add_listener(out, &out_lis, NULL);
    }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) { (void)d; (void)r; (void)name; }
static const struct wl_registry_listener reg_lis = { reg_global, reg_remove };

/* ============================================================================
 * sway IPC (wayland/sway-ipc.c)
 * ========================================================================== */
static json_object *ipc_call(uint32_t type, const char *payload)
{
    json_object *j = sipc_call(ipc_cmd, type, payload);
    if (!j) { fprintf(stderr, "glassine-shell: sway IPC lost\n"); exit(1); }
    return j;
}

static void swaycmd(const char *fmt, ...)
{
    char c[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c, sizeof c, fmt, ap);
    va_end(ap);
    LOG("sway: %s", c);
    json_object *j = ipc_call(SIPC_COMMAND, c);
    for (size_t i = 0; i < json_object_array_length(j); i++) {
        json_object *e = json_object_array_get_idx(j, i), *err;
        if (!sj_bool(e, "success") && json_object_object_get_ex(e, "error", &err)) LOG("sway: %s", json_object_get_string(err));
    }
    json_object_put(j);
}

/* ============================================================================
 * windows and workspaces
 * ========================================================================== */
#define MAXDESK 32
static struct { char name[64]; long num; } desk[MAXDESK];
static struct { WinId id; char cls[96], inst[96], title[160]; } info[MAXCL];
static int ninfo;
static char active_title[160];

static int desk_cmp(const void *a, const void *b)
{
    long x = ((const typeof(desk[0]) *)a)->num, y = ((const typeof(desk[0]) *)b)->num;
    if (x < 0) x = 1L << 30;
    if (y < 0) y = 1L << 30;
    return x < y ? -1 : x > y;
}

static long desk_max(void)
{
    long m = 0;
    for (int i = 0; i < ndesk; i++) if (desk[i].num > m) m = desk[i].num;
    return m;
}

/* the views under n (tiled and floating) on workspace index d (D_SCRATCH:
 * sway's scratchpad = the minimized ones, Client.desk -1) */
#define D_SCRATCH (-2)
static void walk(json_object *n, long d)
{
    const char *type = sj_str(n, "type");
    if (type && !strcmp(type, "workspace")) {
        const char *nm = sj_str(n, "name");
        d = nm && !strcmp(nm, "__i3_scratch") ? D_SCRATCH : -1;
        for (int i = 0; nm && i < ndesk; i++) if (!strcmp(desk[i].name, nm)) d = i;
        if (d == -1) return;
    }
    if (sj_str(n, "shell") && d != -1) {   /* a view */
        WinId id = sj_int(n, "id", 0);
        if (sj_str(n, "app_id") && !strcmp(sj_str(n, "app_id"), "glassine-placeholder")) return;   /* a launch placeholder */
        if (sj_bool(n, "focused")) { active = id; snprintf(active_title, sizeof active_title, "%s", sj_str(n, "name") ? sj_str(n, "name") : ""); }
        if (ncl >= MAXCL) return;
        const char *app = sj_str(n, "app_id"), *cls = NULL, *inst = NULL;
        json_object *wp;
        if (json_object_object_get_ex(n, "window_properties", &wp)) { cls = sj_str(wp, "class"); inst = sj_str(wp, "instance"); }
        typeof(info[0]) *f = &info[ninfo++];
        f->id = id;
        snprintf(f->cls, sizeof f->cls, "%s", app ? app : cls ? cls : "");
        snprintf(f->inst, sizeof f->inst, "%s", app ? app : inst ? inst : "");
        snprintf(f->title, sizeof f->title, "%s", sj_str(n, "name") ? sj_str(n, "name") : "");
        Client *k = &cl[ncl++];
        int r[4];
        sj_rect(n, "rect", osc, r);
        k->w = id; k->desk = d == D_SCRATCH ? -1 : d;
        k->x = r[0]; k->y = r[1]; k->wd = r[2]; k->ht = r[3];
        k->maxed = sj_int(n, "fullscreen_mode", 0) != 0;
        App *ap = app_of(id);
        if (ap) { snprintf(k->name, sizeof k->name, "%s", ap->name); snprintf(k->icon, sizeof k->icon, "%s", ap->icon); }
        else { snprintf(k->name, sizeof k->name, "%s", f->title); k->icon[0] = 0; }
        return;
    }
    const char *const kids[2] = { "nodes", "floating_nodes" };
    for (int k = 0; k < 2; k++) {
        json_object *a;
        if (!json_object_object_get_ex(n, kids[k], &a)) continue;
        for (size_t i = 0; i < json_object_array_length(a); i++) walk(json_object_array_get_idx(a, i), d);
    }
}

void be_read_desktop(void)
{
    json_object *ws = ipc_call(SIPC_GET_WORKSPACES, NULL);
    ndesk = 0;
    for (size_t i = 0; ws && i < json_object_array_length(ws) && ndesk < MAXDESK; i++) {
        json_object *w = json_object_array_get_idx(ws, i);
        snprintf(desk[ndesk].name, sizeof desk[ndesk].name, "%s", sj_str(w, "name") ? sj_str(w, "name") : "");
        desk[ndesk].num = sj_int(w, "num", -1);
        ndesk++;
    }
    qsort(desk, ndesk, sizeof *desk, desk_cmp);
    curdesk = 0;
    warea[0] = 0; warea[1] = (int)(BH * S); warea[2] = SW; warea[3] = SH - warea[1];
    for (size_t i = 0; ws && i < json_object_array_length(ws); i++) {
        json_object *w = json_object_array_get_idx(ws, i);
        if (!sj_bool(w, "focused")) continue;
        for (int d = 0; d < ndesk; d++) if (sj_str(w, "name") && !strcmp(desk[d].name, sj_str(w, "name"))) curdesk = d;
        sj_rect(w, "rect", osc, warea);
    }
    json_object_put(ws);
    if (ndesk < 1) { ndesk = 1; snprintf(desk[0].name, sizeof desk[0].name, "1"); desk[0].num = 1; }
    json_object *tree = ipc_call(SIPC_GET_TREE, NULL);
    ncl = 0; ninfo = 0; active = 0; active_title[0] = 0;
    if (tree) walk(tree, -1);
    json_object_put(tree);
}

App *be_app_of(WinId w)
{
    for (int i = 0; i < ninfo; i++) if (info[i].id == w) return app_match(info[i].cls, info[i].inst);
    return NULL;
}

void be_title(WinId w, char *out, size_t len)
{
    out[0] = 0;
    for (int i = 0; i < ninfo; i++) if (info[i].id == w) { snprintf(out, len, "%s", info[i].title); return; }
    if (w == active) snprintf(out, len, "%s", active_title);
}

int be_sheet_window(WinId w) { return w != 0; }        /* active is a view or nothing */
void be_activate(WinId w) { swaycmd("[con_id=%lu] focus", w); }
/* a frozen app (glassine-bgprio freezes long-hidden ones) sees a close only once
 * thawed: ask bgprio first (a datagram, as _GLASSINE_THAW on X) */
void be_close(WinId w)
{
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "/run/glassine-bgprio.sock");
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    char m[32];
    int n = snprintf(m, sizeof m, "thaw %lu", w);
    if (fd >= 0 && sendto(fd, m, n, MSG_DONTWAIT, (struct sockaddr *)&a, sizeof a) == n) {
        struct timespec ts = { 0, 50 * 1000000L };
        nanosleep(&ts, NULL);
    }
    if (fd >= 0) close(fd);
    swaycmd("[con_id=%lu] kill", w);
}
void be_toggle_max(WinId w) { swaycmd("[con_id=%lu] fullscreen toggle", w); }
/* minimized = sway's scratchpad (the three-finger swipe down): shown here,
 * then tiled (floating disable takes it out of the scratchpad) */
void be_restore(WinId w) { swaycmd("[con_id=%lu] scratchpad show, floating disable, focus", w); }

/* workspace index d as a sway workspace argument; past the last: new ones */
static void desk_arg(long d, char *out, size_t n)
{
    if (d >= 0 && d < ndesk) snprintf(out, n, "\"%s\"", desk[d].name);
    else snprintf(out, n, "number %ld", desk_max() + 1 + (d > ndesk ? d - ndesk : 0));
}

void be_goto_desk(long d)
{
    char a[80];
    desk_arg(d, a, sizeof a);
    swaycmd("workspace %s", a);
}

void be_set_desk(WinId w, long d)
{
    char a[80];
    desk_arg(d, a, sizeof a);
    swaycmd("[con_id=%lu] move container to workspace %s", w, a);
}

void be_new_screen(WinId w, int follow)
{
    char c[64];
    snprintf(c, sizeof c, "newscreen %lx %d", (unsigned long)w, follow);
    be_winfit(c);
}

void be_move_to_desk(WinId w, long d)
{
    char a[80];
    if (d < 0) {                        /* a new first one: the numbered ones move up one */
        for (int i = ndesk - 1; i >= 0; i--)
            if (desk[i].num >= 1) swaycmd("rename workspace \"%s\" to \"%ld\"", desk[i].name, desk[i].num + 1);
        snprintf(a, sizeof a, "number 1");
    } else desk_arg(d, a, sizeof a);
    swaycmd("[con_id=%lu] move container to workspace %s; workspace %s; [con_id=%lu] focus", w, a, a, w);
}

void be_winfit(const char *c)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s/glassine-swayd", rt ? rt : "/tmp");
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd >= 0 && sendto(fd, c, strlen(c), 0, (struct sockaddr *)&a, sizeof a) < 0) LOG("glassine-swayd: %s", strerror(errno));
    if (fd >= 0) close(fd);
}

void be_theme_publish(const char *name)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char p[256], t[270];
    snprintf(p, sizeof p, "%s/glassine-shell.theme", rt ? rt : "/tmp");
    snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "w");
    if (!f) return;
    fprintf(f, "%s\n", name);
    fclose(f);
    rename(t, p);
}

/* ============================================================================
 * the connection
 * ========================================================================== */
static void ipc_event(void)
{
    uint32_t type;
    char *m = sipc_recv(ipc_ev, &type);
    if (!m) { fprintf(stderr, "glassine-shell: sway IPC lost\n"); exit(1); }
    json_object *j = sj_parse(m);
    free(m);
    if (!j) return;
    const char *change = sj_str(j, "change");
    if (type == SIPC_EV_WORKSPACE || (change && strcmp(change, "title"))) snap_later();   /* (a busy title: no stills) */
    if (type == SIPC_EV_WORKSPACE) ev_desktop();
    else if (type == SIPC_EV_WINDOW && change) {
        json_object *c;
        WinId id = json_object_object_get_ex(j, "container", &c) ? (WinId)sj_int(c, "id", 0) : 0;
        if (!strcmp(change, "title")) {
            if (id == active) { be_read_desktop(); ev_title(); }
        } else if (strcmp(change, "mark") && strcmp(change, "urgent")) ev_desktop();
    }
    json_object_put(j);
}

static int readable(int fd)
{
    struct pollfd p = { fd, POLLIN, 0 };
    return poll(&p, 1, 0) > 0 && (p.revents & (POLLIN | POLLHUP | POLLERR));
}

void be_dispatch(void)
{
    while (wl_display_prepare_read(wd) != 0)
        if (wl_display_dispatch_pending(wd) < 0) goto lost;
    wl_display_flush(wd);
    if (readable(wl_display_get_fd(wd))) { if (wl_display_read_events(wd) < 0) goto lost; }
    else wl_display_cancel_read(wd);
    if (wl_display_dispatch_pending(wd) < 0 || wl_display_dispatch_queue_pending(wd, sq) < 0) goto lost;
    while (ipc_ev >= 0 && readable(ipc_ev)) ipc_event();
    return;
lost:
    fprintf(stderr, "glassine-shell: Wayland connection lost\n");
    exit(1);
}

void be_flush(void) { wl_display_flush(wd); }
int be_fd(void) { return epfd; }

int be_init(void)
{
    wd = wl_display_connect(NULL);
    if (!wd) { fprintf(stderr, "glassine-shell: cannot open display\n"); return 0; }
    xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    sq = wl_display_create_queue(wd);
    struct wl_registry *r = wl_display_get_registry(wd);
    wl_registry_add_listener(r, &reg_lis, NULL);
    wl_display_roundtrip(wd);
    wl_display_roundtrip(wd);           /* the output's mode and scale, the seat's capabilities */
    if (!comp || !shm || !lsh || !out || !out_w) { fprintf(stderr, "glassine-shell: no layer shell / output\n"); return 0; }
    /* the mode is the output's own pixels: a 90/270 transform (sway's grey output,
     * mode 1872x1404, transform 270) swaps them for the desktop */
    SW = out_tr & 1 ? out_h : out_w;
    SH = out_tr & 1 ? out_w : out_h;
    be_align = osc;
    ipc_cmd = sipc_open();
    ipc_ev = sipc_open();
    if (ipc_cmd < 0 || ipc_ev < 0) { fprintf(stderr, "glassine-shell: no sway IPC ($SWAYSOCK)\n"); return 0; }
    uint32_t t;
    char *ok = NULL;
    if (!sipc_send(ipc_ev, SIPC_SUBSCRIBE, "[\"workspace\",\"window\"]") || !(ok = sipc_recv(ipc_ev, &t))) {
        fprintf(stderr, "glassine-shell: sway IPC subscribe failed\n");
        return 0;
    }
    free(ok);
    epfd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event e = { .events = EPOLLIN };
    e.data.fd = wl_display_get_fd(wd);
    epoll_ctl(epfd, EPOLL_CTL_ADD, e.data.fd, &e);
    e.data.fd = ipc_ev;
    epoll_ctl(epfd, EPOLL_CTL_ADD, ipc_ev, &e);
    LOG("wayland: output %dx%d scale %d", SW, SH, osc);
    epdd("shell reset\n");             /* glassine-epdd-desktop: the last shell's calm boxes are gone (a restart, a turn) */
    be_ready = 1;
    return 1;
}

/* ============================================================================
 * the overview's stills (chose real snapshots,
 * ): wlr-screencopy of the output, in-process. A still is taken of
 * the visible screen SNAP_IDLE_MS after its windows last changed (a sway
 * event) or a popup closed, never while a popup is up (it would be in it), and
 * right before the overview opens when the visible screen has none or an old
 * one (be_snap_fresh, waits at most SNAP_WAIT_MS). Kept as an A8 coverage
 * mask (255 = ink) of the work area at 1/SNAP_DS, keyed by the screen's
 * windows and their places: a screen whose windows changed since shows its
 * tiles instead (ov_thumb). Hidden apps are frozen (glassine-bgprio), so a still is
 * the screen as it was when last seen; one switched away by a gesture keeps
 * the last still taken while it showed.
 * GLASSINE_SNAP_DUMP=DIR writes each still there as PNG (the harness).
 * ========================================================================== */
#define SNAP_DS 3
#define SNAP_MAX 12
#define SNAP_IDLE_MS 1500
#define SNAP_WAIT_MS 600
#define SNAP_OLD_MS 30000
#define SNAP_WIN 16
typedef struct { int n; WinId id[SNAP_WIN]; int r[SNAP_WIN][4]; } SnapKey;
typedef struct { SnapKey k; cairo_surface_t *img; double t; } Snap;
static Snap snaps[SNAP_MAX];
static struct {
    struct zwlr_screencopy_frame_v1 *f;
    struct wl_buffer *buf;
    uint8_t *px; size_t size;
    uint32_t fmt; int w, h, stride, yinv;
    int busy, done;
    SnapKey k;
} cap;
static guint snap_timer;

/* screen index d's windows, sorted by id (their places from be_read_desktop) */
static void snap_key(long d, SnapKey *k)
{
    k->n = 0;
    for (int i = 0; i < ncl && k->n < SNAP_WIN; i++) {
        if (cl[i].desk != d) continue;
        int j = k->n++;
        while (j > 0 && k->id[j - 1] > cl[i].w) { k->id[j] = k->id[j - 1]; memcpy(k->r[j], k->r[j - 1], sizeof k->r[j]); j--; }
        k->id[j] = cl[i].w;
        k->r[j][0] = cl[i].x; k->r[j][1] = cl[i].y; k->r[j][2] = cl[i].wd; k->r[j][3] = cl[i].ht;
    }
}

static int snap_same(const SnapKey *a, const SnapKey *b)
{
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; i++) if (a->id[i] != b->id[i] || memcmp(a->r[i], b->r[i], sizeof a->r[i])) return 0;
    return 1;
}

static Snap *snap_find(const SnapKey *k)
{
    for (int i = 0; i < SNAP_MAX; i++) if (snaps[i].img && snap_same(&snaps[i].k, k)) return &snaps[i];
    return NULL;
}

/* logical device px (the layout as shown) -> the buffer (the output's mode, untransformed) */
static inline void snap_map(int lx, int ly, int *bx, int *by)
{
    int W = cap.w, H = cap.h;
    switch (out_tr & 3) {
    case 1:  *bx = ly; *by = H - 1 - lx; break;          /* (checked on the harness's headless output) */
    case 2:  *bx = W - 1 - lx; *by = H - 1 - ly; break;
    case 3:  *bx = W - 1 - ly; *by = lx; break;
    default: *bx = lx; *by = ly;
    }
    if (cap.yinv) *by = H - 1 - *by;
}

static inline int snap_luma(const uint8_t *p)
{
    switch (cap.fmt) {
    case WL_SHM_FORMAT_R8: return p[0];
    /* the grey output (sway 0004) has its grey in one channel, the others 0
     * (grim's PNG of it is red): the brightest channel, = the grey for R=G=B */
    default: { int m = p[0] > p[1] ? p[0] : p[1]; return m > p[2] ? m : p[2]; }
    }
}

static void cap_end(void)
{
    if (cap.f) zwlr_screencopy_frame_v1_destroy(cap.f);
    if (cap.buf) wl_buffer_destroy(cap.buf);
    if (cap.px) munmap(cap.px, cap.size);
    cap.f = NULL; cap.buf = NULL; cap.px = NULL;
    cap.busy = 0; cap.done = 1;
}

static void sc_buffer(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride)
{
    (void)d;
    if (cap.buf) return;                /* the first format offered */
    int bpp = fmt == WL_SHM_FORMAT_R8 ? 1 : 4;
    if (fmt != WL_SHM_FORMAT_R8 && fmt != WL_SHM_FORMAT_XRGB8888 && fmt != WL_SHM_FORMAT_ARGB8888 &&
        fmt != WL_SHM_FORMAT_XBGR8888 && fmt != WL_SHM_FORMAT_ABGR8888) {
        LOG("snap: buffer format 0x%x not handled", fmt);
        cap_end();
        return;
    }
    (void)bpp;
    cap.fmt = fmt; cap.w = w; cap.h = h; cap.stride = stride;
    cap.size = (size_t)stride * h;
    int fd = memfd_create("glassine-snap", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, cap.size)) { if (fd >= 0) close(fd); cap_end(); return; }
    cap.px = mmap(NULL, cap.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (cap.px == MAP_FAILED) { cap.px = NULL; close(fd); cap_end(); return; }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, cap.size);
    cap.buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, fmt);
    wl_shm_pool_destroy(pool);
    close(fd);
    zwlr_screencopy_frame_v1_copy(f, cap.buf);
}

static void sc_flags(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t flags)
{ (void)d; (void)f; cap.yinv = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0; }

static void sc_ready(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t sh, uint32_t sl, uint32_t ns)
{
    (void)d; (void)f; (void)sh; (void)sl; (void)ns;
    double t0 = now_ms();
    int L = out_tr & 1, bw = L ? cap.h : cap.w, bh = L ? cap.w : cap.h;   /* the buffer in the layout's orientation */
    int x0 = warea[0], y0 = warea[1], ww = warea[2], wh = warea[3];
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 + ww > bw) ww = bw - x0;
    if (y0 + wh > bh) wh = bh - y0;
    int tw = ww / SNAP_DS, th = wh / SNAP_DS, bpp = cap.fmt == WL_SHM_FORMAT_R8 ? 1 : 4;
    if (tw < 8 || th < 8) { cap_end(); return; }
    cairo_surface_t *img = cairo_image_surface_create(CAIRO_FORMAT_A8, tw, th);
    uint8_t *o = cairo_image_surface_get_data(img);
    int os = cairo_image_surface_get_stride(img);
    for (int ty = 0; ty < th; ty++)
        for (int tx = 0; tx < tw; tx++) {
            int sum = 0;
            for (int j = 0; j < SNAP_DS; j++)
                for (int i = 0; i < SNAP_DS; i++) {
                    int bx, by;
                    snap_map(x0 + tx * SNAP_DS + i, y0 + ty * SNAP_DS + j, &bx, &by);
                    sum += snap_luma(cap.px + (size_t)by * cap.stride + (size_t)bx * bpp);
                }
            o[ty * os + tx] = 255 - sum / (SNAP_DS * SNAP_DS);
        }
    cairo_surface_mark_dirty(img);
    /* the slot: the same windows, else the oldest */
    Snap *s = snap_find(&cap.k);
    if (!s) {
        s = &snaps[0];
        for (int i = 0; i < SNAP_MAX; i++) {
            if (!snaps[i].img) { s = &snaps[i]; break; }
            if (snaps[i].t < s->t) s = &snaps[i];
        }
    }
    if (s->img) cairo_surface_destroy(s->img);
    s->img = img; s->k = cap.k; s->t = now_ms();
    LOG("snap: %d windows, %dx%d from %dx%d fmt 0x%x tr %d, %.0f ms", cap.k.n, tw, th, cap.w, cap.h, cap.fmt, out_tr, now_ms() - t0);
    const char *dump = getenv("GLASSINE_SNAP_DUMP");
    if (dump) {
        static int seq;
        char p[512];
        snprintf(p, sizeof p, "%s/snap-%02d.png", dump, seq++);
        cairo_surface_write_to_png(img, p);
    }
    cap_end();
}

static void sc_failed(void *d, struct zwlr_screencopy_frame_v1 *f)
{ (void)d; (void)f; LOG("snap: copy failed"); cap_end(); }

static const struct zwlr_screencopy_frame_v1_listener sc_lis = { sc_buffer, sc_flags, sc_ready, sc_failed };

/* a still of the visible screen now (not with a popup up, nor of an empty one) */
static int snap_take(void)
{
    if (!scm || !out || cap.busy || pop.mapped) return 0;
    be_read_desktop();
    snap_key(curdesk, &cap.k);
    if (!cap.k.n) return 0;
    for (int i = 0; i < cap.k.n; i++) {     /* a fullscreen window covers the bar: no still of the work area */
        Client *c = NULL;
        for (int k = 0; k < ncl; k++) if (cl[k].w == cap.k.id[i]) c = &cl[k];
        if (c && c->maxed) return 0;
    }
    cap.busy = 1; cap.done = 0; cap.yinv = 0;
    cap.f = zwlr_screencopy_manager_v1_capture_output(scm, 0, out);
    wl_proxy_set_queue((struct wl_proxy *)cap.f, sq);   /* be_snap_fresh waits on this queue only (no input in between) */
    zwlr_screencopy_frame_v1_add_listener(cap.f, &sc_lis, NULL);
    wl_display_flush(wd);
    return 1;
}

static gboolean snap_tick(gpointer u)
{
    (void)u;
    snap_timer = 0;
    if (pop.mapped) return G_SOURCE_REMOVE;  /* the popup's close asks again */
    snap_take();
    return G_SOURCE_REMOVE;
}

static void snap_later(void)
{
    if (!scm) return;
    if (snap_timer) g_source_remove(snap_timer);
    snap_timer = g_timeout_add(SNAP_IDLE_MS, snap_tick, NULL);
}

static gboolean snap_idle_dispatch(gpointer u) { (void)u; be_dispatch(); return G_SOURCE_REMOVE; }

/* before the overview opens: the visible screen's still, if it has none that
 * fits or only an old one (waits for the copy, at most SNAP_WAIT_MS) */
void be_snap_fresh(void)
{
    if (!scm || pop.mapped) return;
    be_read_desktop();
    SnapKey k;
    snap_key(curdesk, &k);
    Snap *s = snap_find(&k);
    if (s && now_ms() - s->t < SNAP_OLD_MS) return;
    if (!cap.busy && !snap_take()) return;
    double end = now_ms() + SNAP_WAIT_MS, t0 = now_ms();
    int fd = wl_display_get_fd(wd);
    while (cap.busy && now_ms() < end) {
        while (wl_display_prepare_read_queue(wd, sq) != 0) wl_display_dispatch_queue_pending(wd, sq);
        wl_display_flush(wd);
        struct pollfd p = { fd, POLLIN, 0 };
        int ms = (int)(end - now_ms());
        if (poll(&p, 1, ms > 0 ? ms : 0) > 0) wl_display_read_events(wd);
        else wl_display_cancel_read(wd);
        wl_display_dispatch_queue_pending(wd, sq);
    }
    g_idle_add(snap_idle_dispatch, NULL);   /* what came for the default queue meanwhile (input): from the loop */
    LOG("snap: fresh still %s after %.0f ms", cap.busy ? "not ready" : "taken", now_ms() - t0);
}

cairo_surface_t *be_snap(long d)
{
    SnapKey k;
    snap_key(d, &k);
    if (!k.n) return NULL;
    Snap *s = snap_find(&k);
    return s ? s->img : NULL;
}
