/* glassine-shell.h — shared types of glassine-shell (the e-ink desktop shell: top bar +
 * popups, design "E-ink Shell" with three voices). See glassine-shell.c. */
#ifndef GLASSINE_SHELL_H
#define GLASSINE_SHELL_H
#include <stdint.h>
#include <time.h>
#include <cairo.h>
#include <pango/pangocairo.h>

/* ---- theme and drawing: GlassineOS/ui (glassine-ui.h, pkg-config glassine-ui) ----
 * The voices (White, Editorial, Stationery) and the drawing in them. Every value is
 * a design value in logical px (the design is drawn at 702x936, the panel is
 * 1404x1872: everything is scaled by 2). The shell's short names for ui's types: */
#include <glassine-ui.h>
typedef ui_col col;
typedef UiRing Ring;
typedef UiBox Box;
typedef UiFont TFont;
typedef UiVoice Theme;
typedef UiGc Gc;
#define RGB(x) UI_RGB(x)
#define HALF UI_HALF
#define IL_RULE UI_RULE
#define IL_DIM UI_DIM
#define NRING UI_NRING
#define CASE_NONE UI_CASE_NONE
#define CASE_UPPER UI_CASE_UPPER
#define CASE_LOWER UI_CASE_LOWER

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
