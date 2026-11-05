/* glassine-shell-draw.c — drawing for glassine-shell: boxes with the design's hard-edged
 * rings, Pango text, icons from the installed icon themes.
 *
 * Every call works in two passes (Gc.paint): the layout pass only measures
 * (text sizes) so a card knows its height before its background is drawn; the
 * paint pass draws. Coordinates are logical px; the cairo context carries the
 * device scale (x2 on the device).
 *
 * Icons: an index of /usr/share/icons/{glassine-mono,Adwaita,hicolor}, the Flatpak
 * exports' hicolor (system + user) and /usr/share/pixmaps built once (name -> best file: theme order first, then SVG
 * over the largest PNG). Each (icon, pixel size) is rasterized once with
 * librsvg and kept: an e-ink popup redraws often but with the same few icons.
 * Line art (glassine-mono) and -symbolic icons are used as a mask and painted in
 * the requested colour, so pressed (inverted) buttons invert their icons too. */
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <glib.h>
#include <librsvg/rsvg.h>
#include "glassine-shell.h"

static PangoContext *pctx;
static GHashTable *icon_index;          /* name -> struct icon_file */
static GHashTable *icon_cache;          /* "name@devpx#mono" -> cairo_surface_t (or NULL: none) */

struct icon_file { char *path; int rank; int mono; };

static const char *const ICON_THEMES[] = { "glassine-mono", "Adwaita", "hicolor" };

/* rank: lower is better. theme * 10000, then SVG (0) before PNG (1000 - size) */
static void index_dir(const char *dir, int theme)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[1024];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        struct stat s;
        if (stat(p, &s)) continue;
        if (S_ISDIR(s.st_mode)) { index_dir(p, theme); continue; }
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || (strcmp(dot, ".svg") && strcmp(dot, ".png"))) continue;
        int size = 48, a, b;            /* .../NNxNN/... or .../NNxNN@2/... */
        for (const char *t = p; (t = strchr(t, '/')); t++)
            if (sscanf(t + 1, "%dx%d", &a, &b) == 2) size = a;
        int rank = theme * 10000 + (!strcmp(dot, ".svg") ? 0 : 1000 - (size > 999 ? 999 : size));
        char *name = g_strndup(e->d_name, dot - e->d_name);
        struct icon_file *old = g_hash_table_lookup(icon_index, name);
        if (old && old->rank <= rank) { g_free(name); continue; }
        struct icon_file *f = g_new(struct icon_file, 1);
        f->path = g_strdup(p);
        f->rank = rank;
        f->mono = theme == 0 || g_str_has_suffix(name, "-symbolic");
        g_hash_table_replace(icon_index, name, f);
    }
    closedir(d);
}

void draw_init(void)
{
    PangoFontMap *fm = pango_cairo_font_map_get_default();
    pctx = pango_font_map_create_context(fm);
    cairo_font_options_t *o = cairo_font_options_create();
    cairo_font_options_set_antialias(o, CAIRO_ANTIALIAS_GRAY);      /* e-ink: no subpixel colour */
    cairo_font_options_set_hint_style(o, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(o, CAIRO_HINT_METRICS_OFF);
    pango_cairo_context_set_font_options(pctx, o);
    cairo_font_options_destroy(o);

    icon_index = g_hash_table_new(g_str_hash, g_str_equal);
    icon_cache = g_hash_table_new(g_str_hash, g_str_equal);
    for (unsigned i = 0; i < sizeof ICON_THEMES / sizeof *ICON_THEMES; i++) {
        char p[256];
        snprintf(p, sizeof p, "/usr/share/icons/%s", ICON_THEMES[i]);
        index_dir(p, i);
    }
    /* Flatpak apps export their icons beside the system themes, not in them
     * (Chromium, LocalSend: PNG only, no glassine-mono icon -> none in the drawer,
     * ): hicolor's rank, after the system's own */
    index_dir("/var/lib/flatpak/exports/share/icons/hicolor", 2);
    const char *home = g_get_home_dir();
    if (home) {
        char p[512];
        snprintf(p, sizeof p, "%s/.local/share/flatpak/exports/share/icons/hicolor", home);
        index_dir(p, 2);
    }
    index_dir("/usr/share/pixmaps", 3);
}

static void src(cairo_t *cr, col c)
{
    cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, (c >> 24) / 255.0);
}

static double rad(float r, double w, double h)
{
    double m = (w < h ? w : h) / 2;
    if (r == HALF) return m;
    return r > m ? m : r < 0 ? 0 : r;
}

static void path_rr4(cairo_t *cr, double x, double y, double w, double h, const float r4[4], double grow)
{
    if (w <= 0 || h <= 0) return;
    double r[4];
    for (int i = 0; i < 4; i++) {
        double v = r4[i] > 0 ? r4[i] + grow : 0;
        r[i] = r4[i] == HALF ? (w < h ? w : h) / 2 : rad(v < 0 ? 0 : v, w, h);   /* (v = -1 is not HALF) */
    }
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r[1], y + r[1], r[1], -M_PI / 2, 0);
    cairo_arc(cr, x + w - r[2], y + h - r[2], r[2], 0, M_PI / 2);
    cairo_arc(cr, x + r[3], y + h - r[3], r[3], M_PI / 2, M_PI);
    cairo_arc(cr, x + r[0], y + r[0], r[0], M_PI, 3 * M_PI / 2);
    cairo_close_path(cr);
}

void box_bg(Gc *g, double x, double y, double w, double h, const Box *b, col bg)
{
    if (!g->paint || w <= 0 || h <= 0) return;
    cairo_t *cr = g->cr;
    /* outer rings, bottom-most (last) first, never under the box itself */
    for (int i = b->nring - 1; i >= 0; i--) {
        const Ring *r = &b->ring[i];
        if (r->inset || !r->c) continue;
        cairo_save(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        cairo_rectangle(cr, x - 100, y - 100, w + 200, h + 200);
        path_rr4(cr, x, y, w, h, b->r, 0);
        cairo_clip(cr);
        path_rr4(cr, x + r->x - r->spread, y + r->y - r->spread, w + 2 * r->spread, h + 2 * r->spread, b->r, r->spread);
        src(cr, r->c);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    if (bg) {
        path_rr4(cr, x, y, w, h, b->r, 0);
        src(cr, bg);
        cairo_fill(cr);
    }
    for (int i = b->nring - 1; i >= 0; i--) {
        const Ring *r = &b->ring[i];
        if (!r->inset || !r->c) continue;
        cairo_save(cr);
        path_rr4(cr, x, y, w, h, b->r, 0);
        cairo_clip(cr);
        cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
        path_rr4(cr, x, y, w, h, b->r, 0);
        path_rr4(cr, x + r->x + r->spread, y + r->y + r->spread, w - 2 * r->spread, h - 2 * r->spread, b->r, -r->spread);
        src(cr, r->c);
        cairo_fill(cr);
        cairo_restore(cr);
    }
    if (b->bw > 0 && b->bc) {
        if (b->dashed) {
            path_rr4(cr, x + b->bw / 2, y + b->bw / 2, w - b->bw, h - b->bw, b->r, -b->bw / 2);
            double d[2] = { 3 * b->bw, 2 * b->bw };
            cairo_set_dash(cr, d, 2, 0);
            cairo_set_line_width(cr, b->bw);
            src(cr, b->bc);
            cairo_stroke(cr);
            cairo_set_dash(cr, NULL, 0, 0);
        } else {
            cairo_save(cr);
            cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
            path_rr4(cr, x, y, w, h, b->r, 0);
            path_rr4(cr, x + b->bw, y + b->bw, w - 2 * b->bw, h - 2 * b->bw, b->r, -b->bw);
            src(cr, b->bc);
            cairo_fill(cr);
            cairo_restore(cr);
        }
    }
}

void box(Gc *g, double x, double y, double w, double h, const Box *b) { box_bg(g, x, y, w, h, b, b->bg); }

void fill_rect(Gc *g, double x, double y, double w, double h, col c)
{
    if (!g->paint || !c) return;
    cairo_rectangle(g->cr, x, y, w, h);
    src(g->cr, c);
    cairo_fill(g->cr);
}

void rrect(Gc *g, double x, double y, double w, double h, double r, col c)
{
    if (!g->paint || !c) return;
    float r4[4] = { r, r, r, r };
    path_rr4(g->cr, x, y, w, h, r4, 0);
    src(g->cr, c);
    cairo_fill(g->cr);
}

void stroke_rrect(Gc *g, double x, double y, double w, double h, double r, double lw, col c, int dashed)
{
    if (!g->paint || !c) return;
    float r4[4] = { r, r, r, r };
    path_rr4(g->cr, x + lw / 2, y + lw / 2, w - lw, h - lw, r4, -lw / 2);
    double d[2] = { 3 * lw, 2 * lw };
    cairo_set_dash(g->cr, d, dashed ? 2 : 0, 0);
    cairo_set_line_width(g->cr, lw);
    src(g->cr, c);
    cairo_stroke(g->cr);
    cairo_set_dash(g->cr, NULL, 0, 0);
}

/* dashed: 1 dashes (3:2), 2 dots (1:1, CSS "dotted" for thin rules) */
void hline(Gc *g, double x, double y, double w, double lw, col c, int dashed)
{
    if (!g->paint || !c || lw <= 0) return;
    if (!dashed) { fill_rect(g, x, y, w, lw, c); return; }
    double d[2] = { dashed == 2 ? lw : 3 * lw, dashed == 2 ? lw : 2 * lw };
    cairo_set_dash(g->cr, d, 2, 0);
    cairo_set_line_width(g->cr, lw);
    cairo_move_to(g->cr, x, y + lw / 2);
    cairo_line_to(g->cr, x + w, y + lw / 2);
    src(g->cr, c);
    cairo_stroke(g->cr);
    cairo_set_dash(g->cr, NULL, 0, 0);
}

TFont font_with(const TFont *f, int weight) { TFont n = *f; n.weight = weight; return n; }
TFont font_px(const TFont *f, float px) { TFont n = *f; n.px = px; return n; }

static PangoLayout *layout(Gc *g, const TFont *f, const char *s, double maxw)
{
    pango_cairo_update_context(g->cr, pctx);
    PangoLayout *l = pango_layout_new(pctx);
    PangoFontDescription *d = pango_font_description_new();
    pango_font_description_set_family(d, f->family);
    pango_font_description_set_weight(d, f->weight ? f->weight : 400);
    pango_font_description_set_style(d, f->italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
    pango_font_description_set_absolute_size(d, f->px * PANGO_SCALE);
    if (f->smallcaps) pango_font_description_set_variant(d, PANGO_VARIANT_SMALL_CAPS);
    pango_layout_set_font_description(l, d);
    pango_font_description_free(d);
    PangoAttrList *al = pango_attr_list_new();
    if (f->ls) pango_attr_list_insert(al, pango_attr_letter_spacing_new((int)(f->ls * f->px * PANGO_SCALE)));
    if (f->tcase == CASE_UPPER) pango_attr_list_insert(al, pango_attr_text_transform_new(PANGO_TEXT_TRANSFORM_UPPERCASE));
    if (f->tcase == CASE_LOWER) pango_attr_list_insert(al, pango_attr_text_transform_new(PANGO_TEXT_TRANSFORM_LOWERCASE));
    pango_layout_set_attributes(l, al);
    pango_attr_list_unref(al);
    pango_layout_set_text(l, s, -1);
    if (maxw > 0) {
        pango_layout_set_width(l, (int)(maxw * PANGO_SCALE));
        pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    }
    return l;
}

double text_w(Gc *g, const TFont *f, const char *s, double *h)
{
    PangoLayout *l = layout(g, f, s, 0);
    PangoRectangle lr;
    pango_layout_get_extents(l, NULL, &lr);
    g_object_unref(l);
    if (h) *h = lr.height / (double)PANGO_SCALE;
    return lr.width / (double)PANGO_SCALE;
}

double text(Gc *g, const TFont *f, const char *s, double x, double y, int align, double maxw, col c, double *h)
{
    PangoLayout *l = layout(g, f, s, maxw);
    PangoRectangle lr;
    pango_layout_get_extents(l, NULL, &lr);
    double w = lr.width / (double)PANGO_SCALE;
    if (h) *h = lr.height / (double)PANGO_SCALE;
    if (g->paint && c) {
        double ox = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
        cairo_move_to(g->cr, ox - lr.x / (double)PANGO_SCALE, y);
        src(g->cr, c);
        pango_cairo_show_layout(g->cr, l);
    }
    g_object_unref(l);
    return w;
}

double text_wrap(Gc *g, const TFont *f, const char *s, double x, double y, double w, int maxlines, double lh, col c)
{
    PangoLayout *l = layout(g, f, s, 0);
    pango_layout_set_width(l, (int)(w * PANGO_SCALE));
    pango_layout_set_wrap(l, PANGO_WRAP_WORD_CHAR);
    pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    pango_layout_set_height(l, -maxlines);
    if (lh > 0) pango_layout_set_line_spacing(l, lh);
    PangoRectangle lr;
    pango_layout_get_extents(l, NULL, &lr);
    double h = lr.height / (double)PANGO_SCALE;
    if (g->paint && c) {
        cairo_move_to(g->cr, x, y);
        src(g->cr, c);
        pango_cairo_show_layout(g->cr, l);
    }
    g_object_unref(l);
    return h;
}

/* rasterize an icon file at dev x dev device pixels */
static cairo_surface_t *raster(const char *path, int dev)
{
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dev, dev);
    cairo_t *cr = cairo_create(s);
    int ok = 0;
    if (g_str_has_suffix(path, ".svg")) {
        RsvgHandle *h = rsvg_handle_new_from_file(path, NULL);
        if (h) {
            RsvgRectangle vp = { 0, 0, dev, dev };
            ok = rsvg_handle_render_document(h, cr, &vp, NULL);
            g_object_unref(h);
        }
    } else {
        cairo_surface_t *png = cairo_image_surface_create_from_png(path);
        if (cairo_surface_status(png) == CAIRO_STATUS_SUCCESS) {
            int pw = cairo_image_surface_get_width(png), ph = cairo_image_surface_get_height(png);
            double sc = (double)dev / (pw > ph ? pw : ph);
            cairo_scale(cr, sc, sc);
            cairo_set_source_surface(cr, png, 0, 0);
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
            cairo_paint(cr);
            ok = 1;
        }
        cairo_surface_destroy(png);
    }
    cairo_destroy(cr);
    if (!ok) { cairo_surface_destroy(s); return NULL; }
    return s;
}

int icon(Gc *g, const char *name, double px, double cx, double cy, col c)
{
    if (!name || !*name) return 0;
    double sx = 1, sy = 0;
    cairo_user_to_device_distance(g->cr, &sx, &sy);   /* device px per logical px */
    int dev = (int)lround(px * sx);
    const char *path = NULL;
    int mono = 0;
    if (name[0] == '/') {
        path = name;
        mono = strstr(name, "/glassine-mono/") || strstr(name, "-symbolic");
    } else {
        struct icon_file *f = g_hash_table_lookup(icon_index, name);
        if (!f) return 0;
        path = f->path;
        mono = f->mono;
    }
    if (!g->paint) return 1;
    char key[1100];
    snprintf(key, sizeof key, "%s@%d", path, dev);
    cairo_surface_t *s;
    if (!g_hash_table_lookup_extended(icon_cache, key, NULL, (gpointer *)&s)) {
        s = raster(path, dev);
        g_hash_table_insert(icon_cache, g_strdup(key), s);
    }
    if (!s) return 0;
    cairo_t *cr = g->cr;
    cairo_save(cr);
    /* snap to whole device pixels: crisp line art */
    double dx = cx - px / 2, dy = cy - px / 2;
    cairo_user_to_device(cr, &dx, &dy);
    cairo_identity_matrix(cr);
    dx = round(dx); dy = round(dy);
    if (mono) {
        src(cr, c);
        cairo_mask_surface(cr, s, dx, dy);
    } else {
        cairo_set_source_surface(cr, s, dx, dy);
        cairo_paint(cr);
    }
    cairo_restore(cr);
    return 1;
}
