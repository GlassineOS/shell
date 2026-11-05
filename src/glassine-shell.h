/* glassine-shell.h — shared types of glassine-shell (the e-ink desktop shell: top bar +
 * popups, design "E-ink Shell" with three themes). See glassine-shell.c. */
#ifndef GLASSINE_SHELL_H
#define GLASSINE_SHELL_H
#include <stdint.h>
#include <time.h>
#include <cairo.h>
#include <pango/pangocairo.h>

/* ---- theme ------------------------------------------------------------------
 * Every value is a design value in logical px (the design is drawn at 702x936,
 * the panel is 1404x1872: everything is scaled by 2). Colours 0xAARRGGBB,
 * 0 = transparent. Box shadows of the design are all hard (no blur): outer
 * rings/offsets and inset rings/lines, drawn as flat fills. */
typedef uint32_t col;
#define RGB(x) (0xff000000u | (x))
#define HALF (-1.0f)                    /* radius 50% */
/* E-ink scheme tokens shared by all three voices */
#define IL_RULE RGB(0xbbbbbb)
#define IL_DIM RGB(0xb4b4b4)            /* disabled: a pager at its end, nothing to clear */

typedef struct { float x, y, spread; col c; int inset; } Ring;
#define NRING 8
typedef struct {
    col bg;
    float bw; col bc; int dashed;       /* border, inside the box */
    float r[4];                         /* tl tr br bl; HALF = 50% */
    Ring ring[NRING]; int nring;        /* CSS order: the first is on top */
} Box;

enum { CASE_NONE, CASE_UPPER, CASE_LOWER };
typedef struct {
    const char *family;
    int weight, italic;
    float px;
    int smallcaps;
    float ls;                           /* letter spacing, em */
    int tcase;
} TFont;

typedef struct {
    const char *name;
    col screenBg, ink, sub;
    TFont lbl; col lblC;                /* section labels (ink-soft / ink / gold) */
    /* bar */
    col barBg, barFg;
    float barBorderW; col barBorderC; int barDouble;   /* bottom edge, inside */
    float barShadowH; col barShadowC;                  /* below the bar */
    int stitch; col stitchColor;
    Box plateOn, plateOff; col plateOnFg;
    TFont wsFont, titleFont, numFont;
    /* divider (glassine-winfit draws it; glassine-shell only passes the theme name) */
    /* cards */
    Box card; int emboss;
    float sepW; col sepC; int sepDashed;  /* 1 dashed, 2 dotted */
    float sepSoftW; col sepSoftC;
    float headRuleW; col headRuleC;
    TFont nameFont, headFont, dateFont;
    Box btn, sel; col selFg;
    Box tog, togOn; col togOnFg;        /* tog: off (= btn with togR) */
    float grooveTop, grooveH, grooveR; col grooveBg; Ring grooveRing[3]; int ngrooveRing;
    float fillTop, fillH;
    float knobTop, knobML, knobW, knobH, knobR; col knobBg; float knobBw; col knobBc;
    Box tray, segOn; float segR; TFont segFont; col segOnFg;
    Box pwr; col pwrFg;
    Box pad; col padFg; float padStitchInset;
    Box folder, folderTab; TFont folderFont;
    Box app; TFont appNameFont, appCatFont;
    TFont nTitleFont, nBodyFont; float nBodyLh;       /* notifications */
    TFont searchFont, pageFont;
    Box tabOn, tabOff; TFont tabFont;
    TFont ratioFont, snapFont;
    int frac, roman;
    const char *qsTitle, *lTitle, *nTitle;
    const char *oTitle, *curTag;        /* the overview: its title, the current screen's tag */
} Theme;

extern const Theme *themes[];
extern const int nthemes;
const Theme *theme_find(const char *name);

/* ---- drawing (logical px; the cairo context carries the x2) --------------- */
typedef struct {
    cairo_t *cr;
    int paint;                          /* 0: layout pass (measure only) */
    const Theme *t;
} Gc;

void draw_init(void);
void box(Gc *g, double x, double y, double w, double h, const Box *b);
void box_bg(Gc *g, double x, double y, double w, double h, const Box *b, col bg);
void fill_rect(Gc *g, double x, double y, double w, double h, col c);
void rrect(Gc *g, double x, double y, double w, double h, double r, col c);
void stroke_rrect(Gc *g, double x, double y, double w, double h, double r, double lw, col c, int dashed);
void hline(Gc *g, double x, double y, double w, double lw, col c, int dashed);
/* text with its top-left at x,y (align 0 left, 1 centre, 2 right: x is that
 * edge/centre); maxw > 0 ellipsizes. Returns the width; *h the line height. */
double text(Gc *g, const TFont *f, const char *s, double x, double y, int align, double maxw, col c, double *h);
double text_w(Gc *g, const TFont *f, const char *s, double *h);
/* wrapped text in a w-wide column, at most maxlines lines (the last one
 * ellipsized), line height lh x the font size (0: the font's). Returns the height. */
double text_wrap(Gc *g, const TFont *f, const char *s, double x, double y, double w, int maxlines, double lh, col c);
/* an icon of the installed themes (glassine-mono -> Adwaita -> hicolor), px square,
 * centred on cx,cy. Line-art (glassine-mono) and -symbolic icons are drawn in c;
 * others in their own colours. Returns 0 if there is no such icon. */
int icon(Gc *g, const char *name, double px, double cx, double cy, col c);
TFont font_with(const TFont *f, int weight);
TFont font_px(const TFont *f, float px);

/* ---- notifications (org.freedesktop.Notifications, glassine-shell-notify.c) ----- */
typedef struct {
    unsigned id;
    char app[64], icon[256], title[160], body[512], entry[96];
    time_t t;
    int has_default;
} Notice;
#define MAXNOTE 32
extern Notice notes[MAXNOTE];           /* newest first */
extern int nnotes;
/* own the name on the session bus; changed runs whenever the list changes */
int notify_init(void (*changed)(void));
void notify_dismiss(unsigned id);       /* closed by (reason 2) */
void notify_invoke(unsigned id);        /* "default" action, then dismissed */
void notify_clear(void);
void notify_local(const char *app, const char *icon, const char *title, const char *body);

#endif
