/* swayd-handles.h — glassine-swayd's drag handles between tiles (swayd-handles.c):
 * layer-shell surfaces in sway's inner gaps. All geometry in device (panel) px; the surfaces are at the output scale. */
#ifndef SWAYD_HANDLES_H
#define SWAYD_HANDLES_H

typedef struct {
    int x, y, w, h;                     /* the gap between the tiles */
    int vert;                           /* a vertical bar: the tiles side by side */
    int level;                          /* 0: the newest | the rest, 1: the rest split */
    long a, b;                          /* the con_ids on both sides (b: the first of the rest) */
    int lo, hi;                         /* the bar's allowed start positions (y, or x if vert) */
    int a0, alen;                       /* the split's range along the drag axis */
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

/* from swayd-handles.c to glassine-swayd: a handle was dragged to pos (the bar's start,
 * device px) / tapped */
void hd_dragged(const HandleDef *h, int pos);
void hd_tapped(const HandleDef *h);
/* a text field got (1) or lost (0) the keyboard focus (terminals excluded) */
void hd_text_focus(int on);
void hd_dragging(int on);               /* a handle is held or let go: tells glassine-epdd-desktop */
void hd_text_changed(void);             /* the focused field reported again */

#endif
