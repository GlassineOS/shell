/* swayd-size.h — the size of the interface per output (spec 012): what glassine-swayd
 * works out for each output of the session and publishes. No sway, no Wayland here
 * (swayd-size.c): glassine-swayd feeds it sway's outputs and applies the result.
 *
 * An output's density (the pixels of its mode per millimetre) comes from its reported
 * physical size when that is plausible, else from the density kept for it
 * (~/.config/glassine/output-density), else the fallback; a built-in display that
 * reports no size takes the panel adapter's (epd.conf panel_width_mm, panel_height_mm)
 * as its reported one. Its factor is the density times the design pixel's length at
 * the output's interface size (~/.config/glassine/ui-size); the compositor keeps a
 * scale in 120ths. */
#ifndef SWAYD_SIZE_H
#define SWAYD_SIZE_H

#define SZ_MAXOUT 8
typedef struct {
    char name[64], key[256];            /* the connector's name; the output key (spec 011) */
    int w_px, h_px;                     /* the mode, not turned */
    double w_mm, h_mm;                  /* the reported size (0: none) */
    double scale;                       /* the scale it has now */
    int turned;                         /* the transform swaps width and height */
    int lx, ly, lw, lh;                 /* its place and size in the layout, logical px */
    /* sz_plan: */
    double density;
    int source;                         /* UI_DENSITY_* */
    int percent;                        /* its interface size */
    double want;                        /* its factor, in 120ths */
} SzOut;

int sz_builtin(const char *name);       /* a device's own display, by its connector's name */
int sz_main(const SzOut *o, int n);     /* the main output: the built-in one, else the first; -1: none */
double sz_quant(double f);              /* the nearest scale the compositor keeps (120ths) */
int sz_f120(double f);                  /* the same, as a count of 120ths */
int sz_differs(double cur, double want);
/* the panel adapter's size in mm for a mode of w x h px (either way up); 0: none */
int sz_adapter_mm(const char *conf, int w_px, int h_px, double *w_mm, double *h_mm);
/* density, source, percent and want of o; files NULL: the person's own */
void sz_plan(SzOut *o, const char *size_file, const char *density_file, const char *adapter_conf);
/* the runtime file's text for o[0..n) at their present scales, the main output first */
int sz_scale_text(const SzOut *o, int n, char *buf, size_t len);
/* writes $XDG_RUNTIME_DIR/glassine/output-scale (path NULL) unless its text is `last`
 * (what the last call wrote; kept there) and the file is still there; 1: written,
 * 0: the same, -1: failed */
int sz_publish(const char *path, const SzOut *o, int n, char *last, size_t len);
/* a logical length or edge in device px at a scale of f120 120ths (round half up) */
static inline int sz_px(int logical, int f120) { long v = (long)logical * f120; return (int)((v >= 0 ? v + 60 : v - 59) / 120); }

#endif
