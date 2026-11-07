/* swayd-handles.c — the drag handles between tiles on sway (glassine-swayd). sway
 * leaves GAP (40 device px) between tiles (`gaps inner 20` + `gaps outer -20` in the session config); every gap
 * of the visible workspace's layout gets a BOTTOM-layer surface: under every
 * window, so dialogs and fullscreen apps cover it, and above the wallpaper.
 *
 * Drag it (finger, pen or pointer): a TOP-layer "ghost" the size of the bar (no
 * input) shows it where it would go, snapped to 1/3, 1/2, 2/3 (an inner line of a
 * 2x2 screen also to the other inner line: hd_snap). It moves by its
 * margins, with the one buffer: only the old and new place count as damage (until
 * a buffer of the split's whole range per step, all of it damaged); the windows are resized once, on release (every resize is an
 * e-ink repaint). The handle itself never moves during a drag: touch and
 * pointer positions stay relative to the surface they started on. The shell's
 * snap rail comes with it (glassine-shell `drag ...` / `dragend`).
 * A tap (no 16 px move) opens the shell's divider sheet (glassine-swayd). The look
 * follows glassine-shell's theme ($XDG_RUNTIME_DIR/glassine-shell.theme): the voice's
 * drag-handle values of GlassineOS/ui (UiVoice div*, grip*).
 *
 * The same connection watches the text-input focus for the on-screen keyboard:
 * an input method (zwp_input_method_v2, no keyboard
 * grab, it never types) that sway activates when a text field (text-input-v3:
 * GTK, Firefox, Qt) gets the focus; hd_text_focus tells glassine-swayd. Terminals
 * (content purpose terminal: foot has it while focused) don't count. */
#define _GNU_SOURCE
#include <cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <glassine-ui.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "input-method-unstable-v2-client-protocol.h"
#include "swayd-handles.h"
#include "../glassine-popupd.h"

#define GAP 40
#define MARGIN 8
#define MAXHD 4

static struct wl_display *dpy;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *lsh;
static struct wl_seat *seat;
static struct wl_touch *touch;
static struct wl_pointer *pointer;
static int scale = 2;
static struct zwp_input_method_manager_v2 *imm;
static struct zwp_input_method_v2 *im;

typedef struct {
    struct wl_surface *s;
    struct zwlr_layer_surface_v1 *ls;
    int configured, x, y, w, h;         /* device px */
    int ghost;                          /* the drag ghost (TOP, no input) */
    int dw, dh;                         /* the size of the buffer it has */
    HandleDef d;
} Surf;
static Surf hs[MAXHD], gh;
static int nhs;

/* ---- the look: glassine-shell's theme ----------------------------------------- */
#define DPX 2                           /* device px per logical px of the voice values (the panel's scale) */
static const UiVoice *hst;              /* NULL: the old look (white margins, grey bar, three lines) */

static void theme_read(void)
{
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char p[256], name[64] = "";
    snprintf(p, sizeof p, "%s/glassine-shell.theme", rt && *rt ? rt : "/tmp");
    FILE *f = fopen(p, "r");
    if (f) {
        if (!fgets(name, sizeof name, f)) name[0] = 0;
        fclose(f);
    }
    name[strcspn(name, "\n")] = 0;
    const UiVoice *v = ui_voice_find(name);   /* unknown names give White: not a voice here */
    hst = !strcmp(name, v->name) ? v : NULL;
}

static void rgb(cairo_t *c, unsigned v)
{
    cairo_set_source_rgb(c, (v >> 16 & 255) / 255.0, (v >> 8 & 255) / 255.0, (v & 255) / 255.0);
}

/* one bar at (x,y), GAP thick, len long, in device px */
static void bar(cairo_t *c, int x, int y, int vert, int len, int dragged)
{
    int w = vert ? GAP : len, ht = vert ? len : GAP;
    cairo_save(c);
    cairo_translate(c, x, y);
    if (!hst) {
        rgb(c, 0xffffff);
        cairo_rectangle(c, 0, 0, w, ht);
        cairo_fill(c);
        rgb(c, dragged ? 0x111111 : 0x888888);
        if (vert) cairo_rectangle(c, MARGIN, 0, GAP - 2 * MARGIN, len);
        else cairo_rectangle(c, 0, MARGIN, len, GAP - 2 * MARGIN);
        cairo_fill(c);
        rgb(c, dragged ? 0xffffff : 0x000000);
        for (int k = -1; k <= 1; k++) {
            if (vert) cairo_rectangle(c, GAP / 2 - 1 + k * 7, len / 2 - 40, 3, 80);
            else cairo_rectangle(c, len / 2 - 40, GAP / 2 - 1 + k * 7, 80, 3);
        }
        cairo_fill(c);
        cairo_restore(c);
        return;
    }
    const UiVoice *s = hst;
    rgb(c, dragged ? s->divDrag : s->divBg);
    cairo_rectangle(c, 0, 0, w, ht);
    cairo_fill(c);
    if (s->divEdgeW > 0) {
        double e = s->divEdgeW * DPX;
        rgb(c, s->divEdgeC);
        cairo_rectangle(c, 0, 0, w, e); cairo_rectangle(c, 0, ht - e, w, e);
        cairo_rectangle(c, 0, 0, e, ht); cairo_rectangle(c, w - e, 0, e, ht);
        cairo_fill(c);
    }
    if (s->divStitch && !vert) {        /* stitched strap: a horizontal divider only */
        rgb(c, s->divStitchC);
        double dash[2] = { 9, 6 };
        cairo_set_dash(c, dash, 2, 0);
        cairo_set_line_width(c, 3);
        cairo_move_to(c, 16, 7.5); cairo_line_to(c, w - 16, 7.5);
        cairo_move_to(c, 16, ht - 7.5); cairo_line_to(c, w - 16, ht - 7.5);
        cairo_stroke(c);
        cairo_set_dash(c, NULL, 0, 0);
    }
    int sgw = (int)lround(s->gripW * DPX), sgh = (int)lround(s->gripH * DPX), gap = (int)lround(s->gripGap * DPX);
    int gw = vert ? sgh : sgw, ghh = vert ? sgw : sgh;
    int tot = s->grips * (vert ? ghh : gw) + (s->grips - 1) * gap;
    for (int i = 0; i < s->grips; i++) {
        int gx = vert ? (w - gw) / 2 : (w - tot) / 2 + i * (gw + gap);
        int gy = vert ? (ht - tot) / 2 + i * (ghh + gap) : (ht - ghh) / 2;
        rgb(c, dragged ? s->gripDrag : s->gripBg);
        if (!s->gripRound) { cairo_rectangle(c, gx, gy, gw, ghh); cairo_fill(c); continue; }
        cairo_save(c);
        cairo_translate(c, gx + gw / 2.0, gy + ghh / 2.0);
        cairo_scale(c, gw / 2.0, ghh / 2.0);
        cairo_arc(c, 0, 0, 1, 0, 2 * M_PI);
        cairo_restore(c);
        cairo_fill(c);
        if (s->gripLip) {               /* a grey crescent at the grip's foot: inside the
                                         * grip, outside the grip shifted 4 px up */
            cairo_save(c);
            cairo_save(c);
            cairo_translate(c, gx + gw / 2.0, gy + ghh / 2.0);
            cairo_scale(c, gw / 2.0, ghh / 2.0);
            cairo_arc(c, 0, 0, 1, 0, 2 * M_PI);
            cairo_restore(c);
            cairo_clip(c);
            cairo_rectangle(c, gx, gy, gw, ghh);
            cairo_save(c);
            cairo_translate(c, gx + gw / 2.0, gy - 4 + ghh / 2.0);
            cairo_scale(c, gw / 2.0, ghh / 2.0);
            cairo_new_sub_path(c);
            cairo_arc(c, 0, 0, 1, 0, 2 * M_PI);
            cairo_restore(c);
            cairo_set_fill_rule(c, CAIRO_FILL_RULE_EVEN_ODD);
            rgb(c, s->gripLip);
            cairo_fill(c);
            cairo_restore(c);           /* the clip and the fill rule */
        }
    }
    cairo_restore(c);
}

/* ---- buffers: one shm buffer per draw (handles rarely change) ---------------- */
static void buf_release(void *data, struct wl_buffer *b) { wl_buffer_destroy(b); }
static const struct wl_buffer_listener buf_l = { buf_release };

static struct {
    int on, moved, pos, px, py;         /* px,py: the press, device px */
    Surf *s;
    int32_t touch_id;
    int via_touch;
} dr;

static void draw(Surf *s)
{
    if (!s->configured || s->w <= 0 || s->h <= 0) return;
    int stride = s->w * 4, sz = stride * s->h;
    int fd = memfd_create("glassine-swayd", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, sz)) { if (fd >= 0) close(fd); return; }
    void *m = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (m == MAP_FAILED) { close(fd); return; }
    memset(m, 0, sz);
    cairo_surface_t *cs = cairo_image_surface_create_for_data(m, CAIRO_FORMAT_ARGB32, s->w, s->h, stride);
    cairo_t *c = cairo_create(cs);
    const HandleDef *d = &s->d;
    if (s->ghost) {                     /* the bar where the drag would put it */
        bar(c, 0, 0, d->vert, d->vert ? s->h : s->w, 1);
    } else if (dr.on && dr.moved && dr.s == s) {
        rgb(c, UI_PAPER);               /* it's being moved: an empty gap (the paper) */
        cairo_paint(c);
    } else bar(c, 0, 0, d->vert, d->vert ? s->h : s->w, 0);
    cairo_destroy(c);
    cairo_surface_destroy(cs);
    munmap(m, sz);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, sz);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, s->w, s->h, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(b, &buf_l, NULL);
    wl_surface_set_buffer_scale(s->s, scale);
    wl_surface_attach(s->s, b, 0, 0);
    wl_surface_damage_buffer(s->s, 0, 0, s->w, s->h);
    wl_surface_commit(s->s);
    s->dw = s->w; s->dh = s->h;
}

/* ---- layer surfaces ------------------------------------------------------------ */
static void ls_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h)
{
    Surf *s = data;
    zwlr_layer_surface_v1_ack_configure(ls, serial);
    s->configured = 1;
    if (!s->ghost || s->dw != s->w || s->dh != s->h) draw(s);   /* the ghost: a move needs no new pixels */
    else wl_surface_commit(s->s);       /* (the ack) */
}
static void surf_drop(Surf *s);
/* sway closes a layer surface when its output goes (or is re-created): the
 * handle is gone although the layout is the same, so glassine-swayd must place it
 * again (hd_lost) -- before it stayed gone until the layout changed */
static int lost;
static void ls_closed(void *data, struct zwlr_layer_surface_v1 *ls) { surf_drop(data); lost = 1; }
int hd_lost(void) { int l = lost; lost = 0; return l; }
static const struct zwlr_layer_surface_v1_listener ls_l = { ls_configure, ls_closed };

static void surf_drop(Surf *s)
{
    if (s->ls) zwlr_layer_surface_v1_destroy(s->ls);
    if (s->s) wl_surface_destroy(s->s);
    s->ls = NULL; s->s = NULL; s->configured = 0; s->dw = s->dh = 0;
}

static void surf_place(Surf *s, int x, int y, int w, int h, int ghost)
{
    if (s->s && s->x == x && s->y == y && s->w == w && s->h == h) { if (!ghost) draw(s); return; }
    s->x = x; s->y = y; s->w = w; s->h = h; s->ghost = ghost;
    if (!s->s) {
        s->s = wl_compositor_create_surface(comp);
        if (ghost) {                    /* no input: the drag stays with the handle */
            struct wl_region *r = wl_compositor_create_region(comp);
            wl_surface_set_input_region(s->s, r);
            wl_region_destroy(r);
        }
        s->ls = zwlr_layer_shell_v1_get_layer_surface(lsh, s->s, NULL,
                    ghost ? ZWLR_LAYER_SHELL_V1_LAYER_TOP : ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
                    ghost ? "glassine-swayd-drag" : "glassine-swayd-handle");
        zwlr_layer_surface_v1_add_listener(s->ls, &ls_l, s);
        zwlr_layer_surface_v1_set_anchor(s->ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
        zwlr_layer_surface_v1_set_exclusive_zone(s->ls, -1);   /* output coordinates, as sway's rects */
        zwlr_layer_surface_v1_set_keyboard_interactivity(s->ls, 0);
        s->configured = 0;
    }
    zwlr_layer_surface_v1_set_size(s->ls, w / scale, h / scale);
    zwlr_layer_surface_v1_set_margin(s->ls, y / scale, 0, 0, x / scale);
    wl_surface_commit(s->s);            /* a new configure comes, then the draw */
}

void hd_set(const HandleDef *h, int n)
{
    if (!dpy) return;
    if (n > MAXHD) n = MAXHD;
    if (dr.on) {                        /* the layout changed under a drag: drop it */
        dr.on = 0;
        hd_dragging(0);
        surf_drop(&gh);
        const char *m[1] = { "dragend" };
        glassine_shell(m, 1);
    }
    theme_read();
    for (int i = 0; i < n; i++) {
        hs[i].d = h[i];
        surf_place(&hs[i], h[i].x, h[i].y, h[i].w, h[i].h, 0);
    }
    for (int i = n; i < MAXHD; i++) surf_drop(&hs[i]);
    nhs = n;
    wl_display_flush(dpy);
}

void hd_theme_changed(void)
{
    if (!dpy) return;
    theme_read();
    for (int i = 0; i < nhs; i++) draw(&hs[i]);
    wl_display_flush(dpy);
}

/* ---- dragging ----------------------------------------------------------------- */
static Surf *surf_of(struct wl_surface *s)
{
    for (int i = 0; i < nhs; i++) if (hs[i].s == s) return &hs[i];
    return NULL;
}

/* a drag ends without effect (a gesture took the touch, the pointer left, or a new
 * press came first): the ghost and the shell's rail go, the handle is drawn again */
static void cancel(void)
{
    if (!dr.on) return;
    dr.on = 0;
    hd_dragging(0);
    surf_drop(&gh);
    const char *m[1] = { "dragend" };
    glassine_shell(m, 1);
    draw(dr.s);
}

static void press(Surf *s, double lx, double ly)
{
    cancel();                           /* a stale drag whose up/release never came: it
                                         * must not block every later press (they were
                                         * all ignored until glassine-swayd restarted) */
    dr.on = 1; dr.moved = 0; dr.s = s;
    hd_dragging(1);                     /* its bar must not wait for the debounce */
    dr.px = s->x + (int)(lx * scale); dr.py = s->y + (int)(ly * scale);
    dr.pos = s->d.vert ? s->x : s->y;
}

int hd_snap(const HandleDef *d, int p)
{
    if (p < d->lo) p = d->lo;
    if (p > d->hi) p = d->hi;
    static const double snaps[3] = { 1 / 3.0, 0.5, 2 / 3.0 };
    for (int k = 0; k < 3; k++) {
        double sn = d->a0 + d->alen * snaps[k] - GAP / 2.0;
        if (fabs(p - sn) < 0.02 * d->alen && sn >= d->lo && sn <= d->hi) p = (int)lround(sn);
    }
    /* an inner line: the other one, so the two can line up again */
    if (d->snap >= d->lo && d->snap <= d->hi && abs(p - d->snap) < 0.02 * d->alen) p = d->snap;
    return p;
}

static void move(double lx, double ly)
{
    if (!dr.on) return;
    Surf *s = dr.s;
    const HandleDef *d = &s->d;
    int ax = s->x + (int)(lx * scale), ay = s->y + (int)(ly * scale);
    int p = hd_snap(d, (d->vert ? ax : ay) - GAP / 2);
    if (!dr.moved && (abs(ax - dr.px) >= 16 || abs(ay - dr.py) >= 16)) {
        dr.moved = 1;
        draw(s);                        /* the gap empties; the ghost shows the bar */
    }
    if (!dr.moved || (gh.s && abs(p - dr.pos) < 8)) return;
    dr.pos = p;
    gh.d = *d;
    if (d->vert) surf_place(&gh, p, s->y, GAP, s->h, 1);
    else surf_place(&gh, s->x, p, s->w, GAP, 1);
    char a[6][16];
    snprintf(a[0], 16, "%d", d->vert); snprintf(a[1], 16, "%d", p);
    snprintf(a[2], 16, "%d", d->a0); snprintf(a[3], 16, "%d", d->alen);
    snprintf(a[4], 16, "%d", d->vert ? s->y : s->x); snprintf(a[5], 16, "%d", d->vert ? s->h : s->w);
    const char *m[7] = { "drag", a[0], a[1], a[2], a[3], a[4], a[5] };
    glassine_shell(m, 7);
}

static void release(void)
{
    if (!dr.on) return;
    dr.on = 0;
    hd_dragging(0);
    Surf *s = dr.s;
    HandleDef d = s->d;
    if (!dr.moved) { hd_tapped(&d); return; }
    surf_drop(&gh);
    const char *m[1] = { "dragend" };
    glassine_shell(m, 1);
    draw(s);                            /* back in place until the relayout moves it */
    hd_dragged(&d, dr.pos);
}

static void t_down(void *data, struct wl_touch *t, uint32_t serial, uint32_t time, struct wl_surface *sf,
                   int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    Surf *s = surf_of(sf);
    if (!s) return;
    press(s, wl_fixed_to_double(x), wl_fixed_to_double(y));
    dr.touch_id = id; dr.via_touch = 1;
}
static void t_up(void *data, struct wl_touch *t, uint32_t serial, uint32_t time, int32_t id)
{
    if (dr.on && dr.via_touch && id == dr.touch_id) release();
}
static void t_motion(void *data, struct wl_touch *t, uint32_t time, int32_t id, wl_fixed_t x, wl_fixed_t y)
{
    if (dr.on && dr.via_touch && id == dr.touch_id) move(wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void t_frame(void *data, struct wl_touch *t) {}
static void t_cancel(void *data, struct wl_touch *t)
{
    if (dr.on && dr.via_touch) cancel();   /* a gesture took it: nothing happens */
}
static const struct wl_touch_listener touch_l = { t_down, t_up, t_motion, t_frame, t_cancel };

static Surf *p_on;
static double p_x, p_y;
static void p_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
    p_on = surf_of(sf);
    p_x = wl_fixed_to_double(x); p_y = wl_fixed_to_double(y);
}
static void p_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *sf)
{
    p_on = NULL;
    if (dr.on && !dr.via_touch) cancel();  /* left mid-drag (the pen out of range before
                                            * its tip lifted): the release won't come */
}
static void p_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    p_x = wl_fixed_to_double(x); p_y = wl_fixed_to_double(y);
    if (dr.on && !dr.via_touch) move(p_x, p_y);
}
static void p_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
    if (button != 0x110) return;        /* BTN_LEFT (the pen's tip too) */
    if (state == WL_POINTER_BUTTON_STATE_PRESSED && p_on) {
        press(p_on, p_x, p_y);
        dr.via_touch = 0;
    } else if (state == WL_POINTER_BUTTON_STATE_RELEASED && dr.on && !dr.via_touch) release();
}
static void p_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t v) {}
static const struct wl_pointer_listener pointer_l = { p_enter, p_leave, p_motion, p_button, p_axis };

/* a capability can go and come back (the touchscreen's input device replaced,
 * e.g. a clone device of the touchscreen starting or stopping): the old object
 * then gets no more events, so it is released and a new one taken */
static void seat_caps(void *data, struct wl_seat *st, uint32_t caps)
{
    if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !touch) {
        touch = wl_seat_get_touch(st);
        wl_touch_add_listener(touch, &touch_l, NULL);
        fprintf(stderr, "glassine-swayd: the seat has touch\n");
    } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && touch) {
        if (dr.on && dr.via_touch) cancel();
        wl_touch_release(touch);
        touch = NULL;
        fprintf(stderr, "glassine-swayd: the seat lost touch\n");
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
        pointer = wl_seat_get_pointer(st);
        wl_pointer_add_listener(pointer, &pointer_l, NULL);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && pointer) {
        if (dr.on && !dr.via_touch) cancel();
        wl_pointer_release(pointer);
        pointer = NULL;
        p_on = NULL;
    }
}
static void seat_name(void *data, struct wl_seat *st, const char *name) {}
static const struct wl_seat_listener seat_l = { seat_caps, seat_name };

/* ---- the text-input focus (the keyboard's auto-show) ----------------------- */
#define PURPOSE_TERMINAL 13             /* zwp_text_input_v3.content_purpose.terminal */
static struct { int active, purpose; } im_pend;
static int im_on;
static void im_activate(void *d, struct zwp_input_method_v2 *m) { im_pend.active = 1; im_pend.purpose = 0; }
static void im_deactivate(void *d, struct zwp_input_method_v2 *m) { im_pend.active = 0; }
static void im_surrounding(void *d, struct zwp_input_method_v2 *m, const char *t, uint32_t c, uint32_t a) {}
static void im_cause(void *d, struct zwp_input_method_v2 *m, uint32_t c) {}
static void im_content(void *d, struct zwp_input_method_v2 *m, uint32_t hint, uint32_t purpose) { im_pend.purpose = purpose; }
static void im_done(void *d, struct zwp_input_method_v2 *m)
{
    int on = im_pend.active && im_pend.purpose != PURPOSE_TERMINAL;
    if (on == im_on) {
        if (on) hd_text_changed();
        return;
    }
    im_on = on;
    hd_text_focus(on);
}
static void im_unavailable(void *d, struct zwp_input_method_v2 *m)
{
    fprintf(stderr, "glassine-swayd: another input method runs: no keyboard auto-show\n");
    zwp_input_method_v2_destroy(im);
    im = NULL;
}
static const struct zwp_input_method_v2_listener im_l = {
    im_activate, im_deactivate, im_surrounding, im_cause, im_content, im_done, im_unavailable
};

static void reg_global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v)
{
    if (!strcmp(iface, wl_compositor_interface.name)) comp = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name)) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name)) lsh = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
    else if (!strcmp(iface, zwp_input_method_manager_v2_interface.name))
        imm = wl_registry_bind(r, name, &zwp_input_method_manager_v2_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !seat) {
        seat = wl_registry_bind(r, name, &wl_seat_interface, v < 3 ? v : 3);   /* 3: .release (no pointer frames: v5) */
        wl_seat_add_listener(seat, &seat_l, NULL);
    }
}
static void reg_remove(void *data, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

int hd_init(int sc)
{
    scale = sc > 0 ? sc : 1;
    if (!getenv("WAYLAND_DISPLAY") || !(dpy = wl_display_connect(NULL))) return -1;
    struct wl_registry *r = wl_display_get_registry(dpy);
    wl_registry_add_listener(r, &reg_l, NULL);
    wl_display_roundtrip(dpy);
    wl_display_roundtrip(dpy);          /* the seat's capabilities */
    if (!comp || !shm || !lsh) {
        fprintf(stderr, "glassine-swayd: no layer-shell: no drag handles\n");
        wl_display_disconnect(dpy);
        dpy = NULL;
        return -1;
    }
    if (imm && seat && !getenv("GLASSINE_SWAYD_NO_OSK")) {
        im = zwp_input_method_manager_v2_get_input_method(imm, seat);
        zwp_input_method_v2_add_listener(im, &im_l, NULL);
    }
    return wl_display_get_fd(dpy);
}

int hd_dispatch(void)
{
    if (!dpy) return 1;
    if (wl_display_dispatch(dpy) < 0) return 0;
    wl_display_flush(dpy);
    return 1;
}

void hd_flush(void)
{
    if (!dpy) return;
    wl_display_dispatch_pending(dpy);   /* what a roundtrip left queued */
    wl_display_flush(dpy);
}
