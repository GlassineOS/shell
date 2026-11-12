/* glassine-sleep-pill — the sleep overlay (spec 004): the last page stays on the
 * panel, a small rounded pill near the bottom edge says the device is asleep: a
 * moon, "Sleeping", "Press the power key to wake" and the time sleep started
 * ("Since 20:11").
 *
 * The device's power daemon runs this before it sends "sleep" to
 * glassine-epdd-desktop, which draws the file over the page instead of its plain
 * bar (the bar stays the fallback when the file is missing or stale). Drawn with
 * GlassineOS/ui's voices and drawing, as glassine-shell, so it matches the shell
 * in all three voices.
 *
 * Coordinates: glassine-epdd-desktop's logical screen (W x H, portrait, transform
 * 0). The pill sits at the bottom of what the person sees, so the screen's
 * transform (/run/glassine-rotation, sway's clockwise degrees) turns it with the
 * content.
 *
 * Output: PAM, GRAYSCALE_ALPHA (premultiplied: grey = value x alpha / 255),
 * cropped to the pill, with its place as a comment line "# at X Y" (logical
 * px). Written atomically (OUT.tmp, rename).
 *
 * usage: glassine-sleep-pill [-t VOICE] [-r DEG] [-s TEXT] [-W W -H H] [-p PNG] OUT
 *   -t   white | editorial | stationery (default: $HOME's, then the desktop user's
 *        ~/.config/glassine-shell/theme, then white)
 *   -r   transform 0/90/180/270 (default: /run/glassine-rotation, else 0)
 *   -s   the time text (default "Since HH:MM", now)
 *   -W/-H the logical screen (default: the panel adapter, /etc/glassine/epd.conf or
 *        $GLASSINE_EPD_CONF: panel_width x panel_height, swapped when rotation is
 *        cw or ccw; no size: exit 1)
 *   -p   also write the whole screen as PNG (pill on white; host tests)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <pwd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <cairo.h>
#include <glassine-ui.h>

static const UiVoice *T;

static int read_word(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char fmt[16];
    snprintf(fmt, sizeof fmt, "%%%zus", n - 1);
    int ok = fscanf(f, fmt, out) == 1;
    fclose(f);
    return ok ? 0 : -1;
}

/* the logical screen from the panel adapter (glassine-epdd's keys): 0 or -1 */
static int screen_size(int *W, int *H)
{
    const char *path = getenv("GLASSINE_EPD_CONF");
    FILE *f = fopen(path && *path ? path : "/etc/glassine/epd.conf", "r");
    if (!f) return -1;
    char l[512];
    int pw = 0, ph = 0, turned = 1;     /* rotation's default: ccw */
    while (fgets(l, sizeof l, f)) {
        char *v = strchr(l, '=');
        if (*l == '#' || !v) continue;
        *v++ = 0;
        v[strcspn(v, "\r\n#")] = 0;
        for (char *e = v + strlen(v); e > v && (e[-1] == ' ' || e[-1] == '\t');) *--e = 0;
        if (!strcmp(l, "panel_width")) pw = atoi(v);
        else if (!strcmp(l, "panel_height")) ph = atoi(v);
        else if (!strcmp(l, "rotation")) turned = strcmp(v, "none") != 0;
    }
    fclose(f);
    if (pw <= 0 || ph <= 0) return -1;
    *W = turned ? ph : pw;
    *H = turned ? pw : ph;
    return 0;
}

/* the Material "bedtime" crescent, outlined: a disc less a disc offset to the
 * upper right, centred on cx,cy in an s-px square */
static void moon(UiGc *g, double cx, double cy, double s, double lw, ui_col c)
{
    cairo_t *cr = g->cr;
    double r = s * 0.44, ox = cx - s * 0.03, oy = cy + s * 0.03;
    double r2 = r * 0.78, dx = r * 0.50, dy = -r * 0.42;
    cairo_set_line_width(cr, lw);
    cairo_set_source_rgba(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0, 1);
    /* the crescent's outline: the outer circle where it is outside the cut
     * disc, the cut circle where it is inside the outer disc */
    cairo_save(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_rectangle(cr, cx - s, cy - s, 2 * s, 2 * s);
    cairo_arc(cr, ox + dx, oy + dy, r2, 0, 2 * M_PI);
    cairo_clip(cr);
    cairo_new_path(cr);
    cairo_arc(cr, ox, oy, r, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_restore(cr);
    cairo_save(cr);
    cairo_arc(cr, ox, oy, r, 0, 2 * M_PI);
    cairo_clip(cr);
    cairo_new_path(cr);
    cairo_arc(cr, ox + dx, oy + dy, r2, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_restore(cr);
}

/* the pill in the person's view (vw x vh design px): its height */
static double pill(UiGc *g, double vw, double vh, const char *since)
{
    /* the design: overlay padding 0 24 40, max-width 420; pill padding 14 28 14 18,
     * gap 16; a 48 px ring with the moon; "Sleeping" headFont at 20 px (line
     * 1.15), the hint in dateFont, sub colour (line 1.35); the time 12 px, sub */
    UiFont head = ui_font_px(&T->headFont, 20), hint = T->dateFont, tf = ui_font_px(&T->dateFont, 12);
    double th, hh2, sh;
    ui_text_w(g, &head, "Sleeping", &th);
    ui_text_w(g, &hint, "Press the power key to wake", &hh2);
    double sw = ui_text_w(g, &tf, since, &sh);
    double w = fmin(420, vw - 48), blk = th + 3 + hh2, h = 14 + fmax(48, blk) + 14;
    double x = (vw - w) / 2, y = vh - 40 - h;
    if (h > 200) return h;              /* text this tall: no usable fonts */
    UiBox b = T->card;
    for (int i = 0; i < 4; i++) b.r[i] = UI_HALF;
    ui_box(g, x, y, w, h, &b);
    double cy = y + h / 2, x0 = x + 18;
    ui_stroke_rrect(g, x0 + 0.75, cy - 24 + 0.75, 46.5, 46.5, 23.25, 1.5, T->ink, 0);
    moon(g, x0 + 24, cy, 26, 1.6, T->ink);
    double tx = x0 + 48 + 16, tw = x + w - 28 - sw - 16 - tx;
    ui_text(g, &head, "Sleeping", tx, cy - blk / 2, 0, tw, T->ink, NULL);
    ui_text(g, &hint, "Press the power key to wake", tx, cy - blk / 2 + th + 3, 0, tw, T->sub, NULL);
    ui_text(g, &tf, since, x + w - 28, cy - sh / 2, 2, 0, T->sub, NULL);
    return h;
}

static void voice_name(char *tn, size_t n)
{
    char p[512];
    const char *home = getenv("HOME");
    snprintf(tn, n, "white");
    if (home) {
        snprintf(p, sizeof p, "%s/.config/glassine-shell/theme", home);
        if (!read_word(p, tn, n)) return;
    }
    /* as root (the power daemon): the desktop user's, the first account with a uid from 1000 */
    struct passwd *pw;
    setpwent();
    while ((pw = getpwent()))
        if (pw->pw_uid >= 1000 && pw->pw_uid < 65534) {
            snprintf(p, sizeof p, "%s/.config/glassine-shell/theme", pw->pw_dir);
            if (read_word(p, tn, n)) snprintf(tn, n, "white");
            break;
        }
    endpwent();
}

int main(int argc, char **argv)
{
    const char *tname = NULL, *since = NULL, *png = NULL;
    int rot = -1, W = 0, H = 0, o;
    while ((o = getopt(argc, argv, "t:r:s:W:H:p:")) != -1)
        switch (o) {
        case 't': tname = optarg; break;
        case 'r': rot = atoi(optarg); break;
        case 's': since = optarg; break;
        case 'W': W = atoi(optarg); break;
        case 'H': H = atoi(optarg); break;
        case 'p': png = optarg; break;
        default: goto usage;
        }
    if (optind != argc - 1) {
usage:
        fprintf(stderr, "usage: glassine-sleep-pill [-t VOICE] [-r DEG] [-s TEXT] [-W W -H H] [-p PNG] OUT\n");
        return 2;
    }
    const char *out = argv[optind];
    if ((W <= 0 || H <= 0) && screen_size(&W, &H)) {
        fprintf(stderr, "glassine-sleep-pill: no screen size (-W/-H or the panel adapter's panel_width, panel_height)\n");
        return 1;
    }

    char tn[32];
    if (tname) snprintf(tn, sizeof tn, "%s", tname);
    else voice_name(tn, sizeof tn);
    T = ui_voice_find(tn);
    if (rot < 0) {
        char r[8];
        rot = read_word("/run/glassine-rotation", r, sizeof r) ? 0 : atoi(r);
    }
    rot = ((rot % 360) + 360) % 360 / 90 * 90;
    char sb[32];
    if (!since) {
        time_t t = time(NULL);
        strftime(sb, sizeof sb, "Since %H:%M", localtime(&t));
        since = sb;
    }

    ui_draw_init();
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, W, H);
    cairo_t *cr = cairo_create(s);
    /* the view: transform N turns the content N degrees clockwise on the panel */
    int vw = rot % 180 ? H : W, vh = rot % 180 ? W : H;
    switch (rot) {
    case 90: cairo_translate(cr, W, 0); cairo_rotate(cr, M_PI / 2); break;
    case 180: cairo_translate(cr, W, H); cairo_rotate(cr, M_PI); break;
    case 270: cairo_translate(cr, 0, H); cairo_rotate(cr, -M_PI / 2); break;
    }
    double S = round((W < H ? W : H) / 702.0);   /* as glassine-shell: the design is drawn at 702 px */
    if (S < 1) S = 1;
    cairo_scale(cr, S, S);
    UiGc g = { cr, 1, T };
    double ph = pill(&g, vw / S, vh / S, since);
    cairo_destroy(cr);
    if (ph > 200) {
        fprintf(stderr, "glassine-sleep-pill: the text measures %.0f px high: no usable fonts\n", ph);
        cairo_surface_destroy(s);
        return 1;
    }
    cairo_surface_flush(s);

    /* the crop: every pixel the pill touched (outer rings included) */
    const uint8_t *d = cairo_image_surface_get_data(s);
    int st = cairo_image_surface_get_stride(s), x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int py = 0; py < H; py++)
        for (int px = 0; px < W; px++)
            if (((const uint32_t *)(d + (size_t)py * st))[px] >> 24) {
                if (px < x0) x0 = px;
                if (px > x1) x1 = px;
                if (py < y0) y0 = py;
                if (py > y1) y1 = py;
            }
    if (x1 < 0) { fprintf(stderr, "glassine-sleep-pill: nothing drawn\n"); return 1; }

    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s.tmp", out);
    FILE *f = fopen(tmp, "w");
    if (!f) { perror(tmp); return 1; }
    int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
    fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 2\nMAXVAL 255\nTUPLTYPE GRAYSCALE_ALPHA\n# at %d %d\nENDHDR\n", cw, ch, x0, y0);
    for (int py = y0; py <= y1; py++)
        for (int px = x0; px <= x1; px++) {
            uint32_t v = ((const uint32_t *)(d + (size_t)py * st))[px];
            /* premultiplied ARGB -> grey (luma), alpha */
            uint8_t gr = (uint8_t)((((v >> 16) & 255) * 77 + ((v >> 8) & 255) * 150 + (v & 255) * 29) >> 8);
            fputc(gr, f);
            fputc(v >> 24, f);
        }
    if (fclose(f) || rename(tmp, out)) { perror(out); unlink(tmp); return 1; }

    if (png) {                          /* the whole screen, the pill on white */
        cairo_surface_t *bg = cairo_image_surface_create(CAIRO_FORMAT_RGB24, W, H);
        cairo_t *c2 = cairo_create(bg);
        cairo_set_source_rgb(c2, 1, 1, 1);
        cairo_paint(c2);
        cairo_set_source_surface(c2, s, 0, 0);
        cairo_paint(c2);
        cairo_destroy(c2);
        cairo_surface_write_to_png(bg, png);
        cairo_surface_destroy(bg);
    }
    cairo_surface_destroy(s);
    printf("%s %d %d %d %d (%s, transform %d)\n", out, x0, y0, cw, ch, T->name, rot);
    return 0;
}
