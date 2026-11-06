/* swayd-handles.h — glassine-swayd's drag handles between tiles (swayd-handles.c):
 * layer-shell surfaces in sway's inner gaps. All geometry in device (panel) px; the surfaces are at the output scale. */
#ifndef SWAYD_HANDLES_H
#define SWAYD_HANDLES_H

typedef struct {
    int x, y, w, h;                     /* the gap between the tiles */
    int vert;                           /* a vertical bar: the tiles side by side */
    int level;                          /* 0: the main line (between the halves), 1: the inner line of
                                         * half 0, 2: the inner line of half 1 */
    long a, b;                          /* the con_ids on both sides (b: the first of the rest) */
    int lo, hi;                         /* the bar's allowed start positions (y, or x if vert) */
    int a0, alen;                       /* the split's range along the drag axis */
    int snap;                           /* one more snap position (the other inner line's bar start), -1: none */
    double pos;                         /* the line's position as a fraction of its range (r or ri) */
} HandleDef;

/* connect to $WAYLAND_DISPLAY: the fd to poll, -1 without layer-shell (no handles) */
int hd_init(int scale);
/* the handles to show now (n <= 4; 0 hides all) */
void hd_set(const HandleDef *h, int n);
/* 1 (once) if sway closed a handle surface since the last call: set the handles again */
int hd_lost(void);
/* read + dispatch after poll: 0 = the display is gone */
int hd_dispatch(void);
void hd_flush(void);
/* the shell's theme changed ($XDG_RUNTIME_DIR/glassine-shell.theme): redraw */
void hd_theme_changed(void);

/* where a drag to bar start p (device px) lands: kept in lo..hi, snapped to 1/3, 1/2,
 * 2/3 of the range and to d->snap */
int hd_snap(const HandleDef *d, int p);

/* from swayd-handles.c to glassine-swayd: a handle was dragged to pos (the bar's start,
 * device px) / tapped */
void hd_dragged(const HandleDef *h, int pos);
void hd_tapped(const HandleDef *h);
/* a text field got (1) or lost (0) the keyboard focus (terminals excluded) */
void hd_text_focus(int on);
void hd_dragging(int on);               /* a handle is held or let go: tells glassine-epdd-desktop */
void hd_text_changed(void);             /* the focused field reported again */

#endif
