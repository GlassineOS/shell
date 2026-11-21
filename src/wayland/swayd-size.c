/* swayd-size.c — see swayd-size.h */
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <glassine-ui.h>
#include "swayd-size.h"

/* eDP, LVDS, DSI: a panel wired inside the device; EPD: the panel daemon's output */
int sz_builtin(const char *name)
{
    static const char *const pre[] = { "eDP", "LVDS", "DSI", "EPD" };
    for (size_t i = 0; name && i < sizeof pre / sizeof *pre; i++)
        if (!strncmp(name, pre[i], strlen(pre[i]))) return 1;
    return 0;
}

int sz_main(const SzOut *o, int n)
{
    for (int i = 0; i < n; i++) if (sz_builtin(o[i].name)) return i;
    return n > 0 ? 0 : -1;
}

int sz_f120(double f) { int k = (int)lround(f * 120); return k < 1 ? 1 : k; }
double sz_quant(double f) { return sz_f120(f) / 120.0; }
int sz_differs(double cur, double want) { return sz_f120(cur) != sz_f120(want); }

int sz_adapter_mm(const char *conf, int w_px, int h_px, double *w_mm, double *h_mm)
{
    const char *e = getenv("GLASSINE_EPD_CONF");
    FILE *f = fopen(conf ? conf : e && *e ? e : "/etc/glassine/epd.conf", "r");
    if (!f) return 0;
    char line[256];
    double pw = 0, ph = 0, mw = 0, mh = 0;
    while (fgets(line, sizeof line, f)) {
        char k[64];
        double v;
        if (sscanf(line, " %63[a-z_] = %lf", k, &v) != 2) continue;
        if (!strcmp(k, "panel_width")) pw = v;
        else if (!strcmp(k, "panel_height")) ph = v;
        else if (!strcmp(k, "panel_width_mm")) mw = v;
        else if (!strcmp(k, "panel_height_mm")) mh = v;
    }
    fclose(f);
    if (!(mw > 0) || !(mh > 0) || !(pw > 0) || !(ph > 0)) return 0;
    if (w_px == (int)pw && h_px == (int)ph) { *w_mm = mw; *h_mm = mh; return 1; }
    if (w_px == (int)ph && h_px == (int)pw) { *w_mm = mh; *h_mm = mw; return 1; }
    return 0;
}

void sz_plan(SzOut *o, const char *size_file, const char *density_file, const char *adapter_conf)
{
    double wm = o->w_mm, hm = o->h_mm, am, ah;
    if (!ui_size_plausible(o->w_px, o->h_px, wm, hm) && sz_builtin(o->name) &&
        sz_adapter_mm(adapter_conf, o->w_px, o->h_px, &am, &ah)) { wm = am; hm = ah; }
    o->density = ui_density(o->w_px, o->h_px, wm, hm, ui_output_density_read(density_file, o->key), &o->source);
    o->percent = ui_size_read(size_file, o->key);
    o->want = sz_quant(ui_factor(o->density, o->percent));
}

static int fill(const SzOut *o, int n, UiOutputScale *v)
{
    int m = sz_main(o, n), k = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            if ((i == m) != (pass == 0)) continue;
            v[k].factor = sz_quant(o[i].scale); v[k].density = o[i].density; v[k].source = o[i].source;
            snprintf(v[k].key, sizeof v[k].key, "%s", o[i].key);
            k++;
        }
    return k;
}

static int text_of(const UiOutputScale *v, int n, char *buf, size_t len)
{
    size_t l = 0;
    if (len) *buf = 0;
    for (int i = 0; i < n && l < len; i++)
        l += snprintf(buf + l, len - l, "%.4f %.4f %s %s\n", v[i].factor, v[i].density, ui_density_source_name(v[i].source), v[i].key);
    return l < len;
}

int sz_scale_text(const SzOut *o, int n, char *buf, size_t len)
{
    UiOutputScale v[SZ_MAXOUT];
    if (n > SZ_MAXOUT) n = SZ_MAXOUT;
    return text_of(v, fill(o, n, v), buf, len);
}

int sz_publish(const char *path, const SzOut *o, int n, char *last, size_t len)
{
    char p[512], want[SZ_MAXOUT * 320];
    UiOutputScale v[SZ_MAXOUT];
    if (!path) { if (!ui_output_scale_file(p, sizeof p)) return -1; path = p; }
    if (n > SZ_MAXOUT) n = SZ_MAXOUT;
    int k = fill(o, n, v);
    text_of(v, k, want, sizeof want);
    struct stat st;
    if (last && !strcmp(want, last) && !stat(path, &st)) return 0;
    if (ui_output_scale_write(path, v, k)) return -1;
    if (last) snprintf(last, len, "%s", want);
    return 1;
}
