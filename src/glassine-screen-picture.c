// SPDX-License-Identifier: GPL-2.0-or-later
/* glassine-screen-picture OUT: the picture of the screen as the person sees it (spec 010),
 * for the next start's preview. A wlr-screencopy client of the running compositor: the
 * first output, whole, turned by the output's transform so that the picture stands as
 * shown, in device pixels. OUT gets a binary PNM: P5 (grey) when the output is grey (an
 * R8 buffer, or one whose colour sits in one channel with the others empty), else P6;
 * written as OUT.tmp and renamed.
 *
 * The compositor: WAYLAND_DISPLAY (with XDG_RUNTIME_DIR), else the first wayland-N
 * socket under GLASSINE_USER_RUN (default /run/user/<any>/), so a caller outside the
 * session (the hibernate step, as root) finds it.
 *
 * Exit: 0 written; 3 nothing to take (no compositor, no screencopy, no output, a
 * failed copy, an unknown pixel format, past the time limit); 2 usage; 1 OUT not
 * written. Never waits longer than PICTURE_MAX_S. */
#define _GNU_SOURCE
#include <errno.h>
#include <glob.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"

#define PICTURE_MAX_S 3

static struct wl_shm *shm;
static struct wl_output *out;
static struct zwlr_screencopy_manager_v1 *scm;
static int out_tr;
static struct {
    uint32_t fmt; int w, h, stride;
    struct wl_buffer *buf; uint8_t *px; size_t size;
    int yinv, done, failed;
} cap;

static void on_alarm(int s) { (void)s; _exit(3); }

static void geom(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                 int32_t sub, const char *make, const char *model, int32_t tr)
{
    (void)d; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)sub; (void)make; (void)model;
    out_tr = tr;
}
static void omode(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r)
{ (void)d; (void)o; (void)f; (void)w; (void)h; (void)r; }
static void odone(void *d, struct wl_output *o) { (void)d; (void)o; }
static void oscale(void *d, struct wl_output *o, int32_t s) { (void)d; (void)o; (void)s; }
static const struct wl_output_listener out_l = { geom, omode, odone, oscale };

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    (void)d; (void)ver;
    if (!strcmp(iface, wl_shm_interface.name)) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && !out) {
        out = wl_registry_bind(r, name, &wl_output_interface, 1);
        wl_output_add_listener(out, &out_l, NULL);
    } else if (!strcmp(iface, zwlr_screencopy_manager_v1_interface.name))
        scm = wl_registry_bind(r, name, &zwlr_screencopy_manager_v1_interface, 1);
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n) { (void)d; (void)r; (void)n; }
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

static void f_buffer(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride)
{
    (void)d;
    if (cap.buf) return;                /* the first shm format offered */
    cap.fmt = fmt; cap.w = (int)w; cap.h = (int)h; cap.stride = (int)stride;
    cap.size = (size_t)stride * h;
    int fd = memfd_create("glassine-screen-picture", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)cap.size) < 0) { cap.failed = 1; return; }
    cap.px = mmap(NULL, cap.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (cap.px == MAP_FAILED) { cap.px = NULL; close(fd); cap.failed = 1; return; }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)cap.size);
    cap.buf = wl_shm_pool_create_buffer(pool, 0, (int32_t)w, (int32_t)h, (int32_t)stride, fmt);
    wl_shm_pool_destroy(pool);
    close(fd);
    zwlr_screencopy_frame_v1_copy(f, cap.buf);
}
static void f_flags(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t fl)
{ (void)d; (void)f; cap.yinv = !!(fl & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT); }
static void f_ready(void *d, struct zwlr_screencopy_frame_v1 *f, uint32_t a, uint32_t b, uint32_t c)
{ (void)d; (void)f; (void)a; (void)b; (void)c; cap.done = 1; }
static void f_failed(void *d, struct zwlr_screencopy_frame_v1 *f) { (void)d; (void)f; cap.failed = 1; }
static const struct zwlr_screencopy_frame_v1_listener frame_l = { f_buffer, f_flags, f_ready, f_failed };

/* a buffer pixel's R, G, B (bpp: bytes a pixel), or 0 for an unknown format */
static int fmt_bpp(uint32_t fmt, int *ri, int *gi, int *bi)
{
    switch (fmt) {                      /* little endian: the bytes of the 32-bit word */
    case WL_SHM_FORMAT_R8: *ri = *gi = *bi = 0; return 1;
    case WL_SHM_FORMAT_XRGB8888: case WL_SHM_FORMAT_ARGB8888: *bi = 0; *gi = 1; *ri = 2; return 4;
    case WL_SHM_FORMAT_XBGR8888: case WL_SHM_FORMAT_ABGR8888: *ri = 0; *gi = 1; *bi = 2; return 4;
    default: return 0;
    }
}

/* the picture as shown, (lx, ly) -> the buffer (the output's mode, untransformed) */
static inline void map(int lx, int ly, int *bx, int *by)
{
    int W = cap.w, H = cap.h;
    switch (out_tr & 3) {
    case 1:  *bx = ly; *by = H - 1 - lx; break;
    case 2:  *bx = W - 1 - lx; *by = H - 1 - ly; break;
    case 3:  *bx = W - 1 - ly; *by = lx; break;
    default: *bx = lx; *by = ly;
    }
    if (cap.yinv) *by = H - 1 - *by;
}

static struct wl_display *connect_any(void)
{
    if (getenv("WAYLAND_DISPLAY")) return wl_display_connect(NULL);
    const char *users = getenv("GLASSINE_USER_RUN");
    char pat[512];
    snprintf(pat, sizeof pat, "%s/*/wayland-[0-9]*", users && *users ? users : "/run/user");
    glob_t g;
    struct wl_display *d = NULL;
    if (glob(pat, 0, NULL, &g) == 0) {
        for (size_t i = 0; i < g.gl_pathc && !d; i++) {
            size_t n = strlen(g.gl_pathv[i]);
            if (n > 5 && !strcmp(g.gl_pathv[i] + n - 5, ".lock")) continue;
            d = wl_display_connect(g.gl_pathv[i]);   /* an absolute name: that socket */
        }
        globfree(&g);
    }
    return d;
}

int main(int argc, char **argv)
{
    if (argc != 2 || argv[1][0] == '-') {
        fprintf(stderr, "usage: glassine-screen-picture OUT\n");
        return 2;
    }
    signal(SIGALRM, on_alarm);
    alarm(PICTURE_MAX_S);
    struct wl_display *dpy = connect_any();
    if (!dpy) { fprintf(stderr, "glassine-screen-picture: no compositor\n"); return 3; }
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg, &reg_l, NULL);
    wl_display_roundtrip(dpy);
    wl_display_roundtrip(dpy);          /* the output's geometry (its transform) */
    if (!shm || !out || !scm) { fprintf(stderr, "glassine-screen-picture: no screencopy or no output\n"); return 3; }
    struct zwlr_screencopy_frame_v1 *f = zwlr_screencopy_manager_v1_capture_output(scm, 0, out);
    zwlr_screencopy_frame_v1_add_listener(f, &frame_l, NULL);
    while (!cap.done && !cap.failed)
        if (wl_display_dispatch(dpy) < 0) { cap.failed = 1; break; }
    int ri, gi, bi, bpp = cap.done ? fmt_bpp(cap.fmt, &ri, &gi, &bi) : 0;
    if (cap.failed || !cap.px || !bpp || cap.w <= 0 || cap.h <= 0) {
        fprintf(stderr, "glassine-screen-picture: the copy failed (format 0x%x)\n", cap.fmt);
        return 3;
    }
    /* grey: R8, or colour in one channel only (a grey output's 32-bit copy) */
    int grey = bpp == 1, used = 0;
    if (!grey) {
        for (int y = 0; y < cap.h; y++) {
            const uint8_t *p = cap.px + (size_t)y * cap.stride;
            for (int x = 0; x < cap.w; x++, p += 4)
                used |= (p[ri] ? 1 : 0) | (p[gi] ? 2 : 0) | (p[bi] ? 4 : 0);
        }
        grey = used == 1 || used == 2 || used == 4;
    }
    int LW = (out_tr & 1) ? cap.h : cap.w, LH = (out_tr & 1) ? cap.w : cap.h;
    size_t opx = grey ? 1 : 3, row = (size_t)LW * opx;
    uint8_t *o = malloc(row * LH);
    if (!o) return 3;
    for (int ly = 0; ly < LH; ly++)
        for (int lx = 0; lx < LW; lx++) {
            int bx, by;
            map(lx, ly, &bx, &by);
            const uint8_t *p = cap.px + (size_t)by * cap.stride + (size_t)bx * bpp;
            uint8_t *q = o + (size_t)ly * row + (size_t)lx * opx;
            if (bpp == 1) q[0] = p[0];
            else if (grey) { int m = p[ri] > p[gi] ? p[ri] : p[gi]; q[0] = (uint8_t)(m > p[bi] ? m : p[bi]); }
            else { q[0] = p[ri]; q[1] = p[gi]; q[2] = p[bi]; }
        }
    char tmp[4096];
    snprintf(tmp, sizeof tmp, "%s.tmp", argv[1]);
    FILE *fp = fopen(tmp, "wb");
    if (!fp) { fprintf(stderr, "glassine-screen-picture: %s: %s\n", tmp, strerror(errno)); return 1; }
    int ok = fprintf(fp, "%s\n%d %d\n255\n", grey ? "P5" : "P6", LW, LH) > 0 &&
             fwrite(o, 1, row * LH, fp) == row * LH;
    ok = (fclose(fp) == 0) && ok;
    if (!ok || rename(tmp, argv[1]) != 0) {
        unlink(tmp);
        fprintf(stderr, "glassine-screen-picture: %s not written\n", argv[1]);
        return 1;
    }
    return 0;
}
