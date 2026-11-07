/* vptr — a virtual pointer for the headless sway test (test.sh): stays
 * connected (so the seat keeps its pointer capability) and reads commands from
 * a FIFO, one per line, positions in layout px of the output (W x H, argv):
 *   move X Y | down | up | right (a right click) | tap X Y | hold X Y MS
 * vptr FIFO [W H] */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/input-event-codes.h>
#include <wayland-client.h>
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static struct zwlr_virtual_pointer_manager_v1 *mgr;
static struct zwlr_virtual_pointer_v1 *vp;
static struct wl_display *d;
static int W = 702, H = 936;

static void g(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    (void)data; (void)ver;
    if (!strcmp(iface, zwlr_virtual_pointer_manager_v1_interface.name))
        mgr = wl_registry_bind(r, name, &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void gr(void *data, struct wl_registry *r, uint32_t name) { (void)data; (void)r; (void)name; }
static const struct wl_registry_listener rl = { g, gr };

static uint32_t ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void move(int x, int y) { zwlr_virtual_pointer_v1_motion_absolute(vp, ms(), x, y, W, H); zwlr_virtual_pointer_v1_frame(vp); }
static void btn(int b, int down)
{
    zwlr_virtual_pointer_v1_button(vp, ms(), b, down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(vp);
}
static void sync_(void) { wl_display_roundtrip(d); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: vptr FIFO [W H]\n"); return 1; }
    if (argc > 3) { W = atoi(argv[2]); H = atoi(argv[3]); }
    d = wl_display_connect(NULL);
    if (!d) return 1;
    struct wl_registry *r = wl_display_get_registry(d);
    wl_registry_add_listener(r, &rl, NULL);
    wl_display_roundtrip(d);
    if (!mgr) { fprintf(stderr, "vptr: no virtual pointer manager\n"); return 1; }
    vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, NULL);
    sync_();
    fprintf(stderr, "vptr: ready\n");
    for (;;) {
        FILE *f = fopen(argv[1], "r");
        if (!f) return 1;
        char l[128];
        while (fgets(l, sizeof l, f)) {
            int x, y, t;
            if (sscanf(l, "move %d %d", &x, &y) == 2) move(x, y);
            else if (!strncmp(l, "down", 4)) btn(BTN_LEFT, 1);
            else if (!strncmp(l, "up", 2)) btn(BTN_LEFT, 0);
            else if (!strncmp(l, "right", 5)) { btn(BTN_RIGHT, 1); sync_(); btn(BTN_RIGHT, 0); }
            else if (sscanf(l, "tap %d %d", &x, &y) == 2) { move(x, y); sync_(); btn(BTN_LEFT, 1); sync_(); usleep(80000); btn(BTN_LEFT, 0); }
            else if (sscanf(l, "hold %d %d %d", &x, &y, &t) == 3) { move(x, y); sync_(); btn(BTN_LEFT, 1); sync_(); usleep(t * 1000); btn(BTN_LEFT, 0); }
            sync_();
        }
        fclose(f);
    }
}
