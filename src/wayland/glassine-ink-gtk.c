/* glassine-ink-gtk.c — a GTK 3 module (GTK_MODULES=glassine-ink, libglassine-ink.so in
 * gtk-3.0/modules): unmodified GTK apps register their canvas as the pen's ink
 * area (glassine_ink_v1, src/wayland/protocols/glassine-ink-v1.xml; spec 003). It
 * looks for known canvas widgets by type name (Xournal++'s GtkXournal;
 * GLASSINE_INK_WIDGETS=Type,Type replaces the list) in every toplevel and sends
 * their visible part (clipped by every ancestor: the scrolled window's viewport)
 * as the window's areas. Once an app showed a canvas, its other windows (dialogs)
 * get an empty area: no ink there. A window of an app without a known canvas is
 * left alone (its per-app setting decides). Nothing happens off Wayland or
 * without a compositor that offers glassine_ink_manager_v1.
 * GLASSINE_INK_LOG=1: every region sent (and why nothing is) on stderr.
 *
 * It also says what a repaint is (set_content, the protocol's version 4), for an app
 * that asks for it: GLASSINE_CONTENT=auto:PID with the app's own pid (importing
 * GlassineOS/ui's Python package sets that), or =auto for every app that inherits it.
 * Such an app draws itself, answers a tap with a new picture and never animates, so:
 * the first repaint of a window within CONTENT_TAP_MS of a tap in it is a page; a
 * repaint after the finger, the pen or the pointer moved while down, or after a scroll
 * event, is motion; everything else says nothing. An app can name a repaint itself,
 * asked for or not, with a style class on the toplevel, read and taken off at the
 * repaint: glassine-content-page, -motion, or -none (nothing is said for it). Apps
 * that do neither are left exactly as they are. Logged as "content KIND".
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "glassine-ink-v1-client-protocol.h"

static struct wl_compositor *comp;
static struct glassine_ink_manager_v1 *mgr;
static uint32_t mgr_ver;        /* the version bound */
static char **types;            /* canvas widget type names */
static gboolean seen_canvas;    /* this app showed a canvas */
static gboolean logging;

typedef struct {
	GtkWidget *win;
	struct glassine_ink_surface_v1 *ink;
	guint idle;
	cairo_region_t *last;       /* the areas sent (NULL: none yet) */
} Win;

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t v)
{
	if (!strcmp(iface, wl_compositor_interface.name)) {
		comp = wl_registry_bind(r, name, &wl_compositor_interface, 1);
	} else if (!strcmp(iface, glassine_ink_manager_v1_interface.name)) {
		mgr_ver = v < 4 ? v : 4;        /* 1: areas; 4: what a repaint is */
		mgr = wl_registry_bind(r, name, &glassine_ink_manager_v1_interface, mgr_ver);
	}
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener reg_l = { reg_global, reg_remove };

static gboolean is_canvas(GtkWidget *w)
{
	const char *t = G_OBJECT_TYPE_NAME(w);
	for (char **p = types; *p; p++) {
		if (**p && !strcmp(t, *p)) {
			return TRUE;
		}
	}
	return FALSE;
}

/* w's allocation in the toplevel's coordinates (= surface coordinates) */
static gboolean rect_in(GtkWidget *w, GtkWidget *top, cairo_rectangle_int_t *r)
{
	GtkAllocation a;
	int x, y;
	gtk_widget_get_allocation(w, &a);
	if (!gtk_widget_translate_coordinates(w, top, 0, 0, &x, &y)) {
		return FALSE;
	}
	*r = (cairo_rectangle_int_t){ x, y, a.width, a.height };
	return a.width > 0 && a.height > 0;
}

static void find(GtkWidget *w, gpointer data)
{
	void **a = data;            /* toplevel, region */
	if (!gtk_widget_is_drawable(w)) {
		return;
	}
	if (is_canvas(w)) {
		cairo_rectangle_int_t r;
		if (!rect_in(w, a[0], &r)) {
			return;
		}
		cairo_region_t *vis = cairo_region_create_rectangle(&r);
		for (GtkWidget *p = gtk_widget_get_parent(w); p && p != a[0]; p = gtk_widget_get_parent(p)) {
			cairo_rectangle_int_t pr;
			if (rect_in(p, a[0], &pr)) {
				cairo_region_intersect_rectangle(vis, &pr);
			}
		}
		cairo_region_union(a[1], vis);
		cairo_region_destroy(vis);
		return;
	}
	if (GTK_IS_CONTAINER(w)) {
		gtk_container_forall(GTK_CONTAINER(w), find, data);
	}
}

static void log_areas(Win *s, const cairo_region_t *areas)
{
	const char *title = gtk_window_get_title(GTK_WINDOW(s->win));
	fprintf(stderr, "glassine-ink: areas \"%s\"", title ? title : "");
	int n = cairo_region_num_rectangles(areas);
	if (!n) {
		fprintf(stderr, " none");
	}
	for (int i = 0; i < n; i++) {
		cairo_rectangle_int_t b;
		cairo_region_get_rectangle(areas, i, &b);
		fprintf(stderr, " %d,%d,%d,%d", b.x, b.y, b.width, b.height);
	}
	fprintf(stderr, "\n");
}

static gboolean update(gpointer data)
{
	Win *s = data;
	s->idle = 0;
	GdkWindow *gw = gtk_widget_get_window(s->win);
	if (!gw || !GDK_IS_WAYLAND_WINDOW(gw)) {
		return G_SOURCE_REMOVE;
	}
	cairo_region_t *areas = cairo_region_create();
	void *a[2] = { s->win, areas };
	gtk_container_forall(GTK_CONTAINER(s->win), find, a);
	gboolean canvas = !cairo_region_is_empty(areas);
	if (canvas) {
		seen_canvas = TRUE;
	} else if (!seen_canvas) {
		cairo_region_destroy(areas);    /* not a canvas app (yet): its setting decides */
		return G_SOURCE_REMOVE;
	}
	if (s->last && cairo_region_equal(s->last, areas)) {
		cairo_region_destroy(areas);
		return G_SOURCE_REMOVE;
	}
	if (!s->ink) {
		struct wl_surface *ws = gdk_wayland_window_get_wl_surface(gw);
		if (!ws) {
			cairo_region_destroy(areas);
			return G_SOURCE_REMOVE;
		}
		s->ink = glassine_ink_manager_v1_get_ink_surface(mgr, ws);
	}
	struct wl_region *r = wl_compositor_create_region(comp);
	for (int i = 0; i < cairo_region_num_rectangles(areas); i++) {
		cairo_rectangle_int_t b;
		cairo_region_get_rectangle(areas, i, &b);
		wl_region_add(r, b.x, b.y, b.width, b.height);
	}
	glassine_ink_surface_v1_set_areas(s->ink, r);
	wl_region_destroy(r);
	wl_display_flush(gdk_wayland_display_get_wl_display(gdk_window_get_display(gw)));
	if (logging) {
		log_areas(s, areas);
	}
	if (s->last) {
		cairo_region_destroy(s->last);
	}
	s->last = areas;
	return G_SOURCE_REMOVE;
}

static void schedule(Win *s)
{
	if (!s->idle) {
		s->idle = g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, update, s, NULL);
	}
}

static void on_alloc(GtkWidget *w, GdkRectangle *a, gpointer data) { schedule(data); }

static void forget(Win *s)
{
	if (s->idle) {
		g_source_remove(s->idle);
		s->idle = 0;
	}
	if (s->ink) {
		glassine_ink_surface_v1_destroy(s->ink);     /* (the wl_surface goes with the unmap) */
		s->ink = NULL;
	}
	if (s->last) {
		cairo_region_destroy(s->last);
		s->last = NULL;
	}
}

static void on_unmap(GtkWidget *w, gpointer data) { forget(data); }

static void win_free(gpointer data)
{
	forget(data);
	g_free(data);
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data);

static Win *win_of(GtkWidget *w)
{
	Win *s = g_object_get_data(G_OBJECT(w), "glassine-ink");
	if (!s) {
		s = g_new0(Win, 1);
		s->win = w;
		g_object_set_data_full(G_OBJECT(w), "glassine-ink", s, win_free);
		/* any relayout inside (a canvas resized, a panel shown) reallocates down
		 * from the toplevel: one idle update after it */
		g_signal_connect_after(w, "size-allocate", G_CALLBACK(on_alloc), s);
		g_signal_connect(w, "unmap", G_CALLBACK(on_unmap), s);
		if (mgr_ver >= GLASSINE_INK_SURFACE_V1_SET_CONTENT_SINCE_VERSION) {
			/* (a handler of its own: GTK emits "draw" only for widgets that have one) */
			g_signal_connect(w, "draw", G_CALLBACK(on_draw), NULL);
		}
	}
	return s;
}

static gboolean on_map(GSignalInvocationHint *h, guint n, const GValue *v, gpointer data)
{
	GtkWidget *w = g_value_get_object(&v[0]);
	if (!GTK_IS_WINDOW(w) || !gtk_widget_is_toplevel(w)) {
		return TRUE;
	}
	schedule(win_of(w));
	return TRUE;
}

/* ---- what a repaint is (set_content) ---- */
#define CONTENT_TAP_MS 150      /* a tap's answer is painted within this */
#define CONTENT_SLOP 8          /* px the pointer moves before it drags */
enum { C_NONE, C_PAGE, C_MOTION };
static const char *const c_class[] = { "glassine-content-none", "glassine-content-page", "glassine-content-motion" };
static const char *const c_name[] = { "none", "page", "motion" };
static struct {
	gboolean asked;             /* GLASSINE_CONTENT names this process (kept once seen) */
	gboolean down, moved;       /* the finger, pen or button; it moved past the slop */
	double x0, y0;
	GtkWidget *tap_top;         /* the window tapped (not owned, only compared) */
	gint64 tap_us;
	GtkWidget *step_top;        /* the window a drag or a scroll moved over since its last repaint */
} ct;

static gboolean content_asked(void)
{
	if (!ct.asked) {
		const char *e = getenv("GLASSINE_CONTENT");     /* (set by the app after gtk_init: read late) */
		if (e && !strncmp(e, "auto", 4)) {
			ct.asked = !e[4] || (e[4] == ':' && atol(e + 5) == (long)getpid());
		}
	}
	return ct.asked;
}

static GtkWidget *event_top(GdkEvent *e)
{
	GtkWidget *w = gtk_get_event_widget(e);
	GtkWidget *t = w ? gtk_widget_get_toplevel(w) : NULL;
	return t && GTK_IS_WINDOW(t) ? t : NULL;
}

/* every event, before GTK handles it: where a tap, a drag or a scroll happens */
static void content_event(GdkEvent *e, gpointer data)
{
	double x, y;
	switch (e->type) {
	case GDK_BUTTON_PRESS:
	case GDK_TOUCH_BEGIN:
		ct.down = TRUE, ct.moved = FALSE;
		gdk_event_get_root_coords(e, &ct.x0, &ct.y0);
		break;
	case GDK_MOTION_NOTIFY:
	case GDK_TOUCH_UPDATE:
		if (ct.down && gdk_event_get_root_coords(e, &x, &y)) {
			if (!ct.moved && (ABS(x - ct.x0) >= CONTENT_SLOP || ABS(y - ct.y0) >= CONTENT_SLOP)) {
				ct.moved = TRUE;
			}
			if (ct.moved) {
				ct.step_top = event_top(e);
			}
		}
		break;
	case GDK_BUTTON_RELEASE:
	case GDK_TOUCH_END:
		if (ct.down && !ct.moved) {
			ct.tap_top = event_top(e);
			ct.tap_us = g_get_monotonic_time();
		}
		ct.down = FALSE;
		ct.step_top = NULL;     /* what is painted now is the drag's result, no step */
		break;
	case GDK_TOUCH_CANCEL:
		ct.down = FALSE;
		ct.step_top = NULL;
		break;
	case GDK_SCROLL:
		ct.step_top = event_top(e);
		break;
	default:
		break;
	}
	gtk_main_do_event(e);
}

static gboolean class_off(gpointer data)
{
	GtkStyleContext *sc = gtk_widget_get_style_context(data);
	for (int k = 0; k < 3; k++) {
		if (gtk_style_context_has_class(sc, c_class[k])) {
			gtk_style_context_remove_class(sc, c_class[k]);
		}
	}
	g_object_unref(data);
	return G_SOURCE_REMOVE;
}

/* a toplevel is painted (its commit follows): what this repaint is */
static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data)
{
	GdkWindow *gw = gtk_widget_get_window(w);
	if (!gw || !GDK_IS_WAYLAND_WINDOW(gw) || !cr || !gtk_cairo_should_draw_window(cr, gw)) {
		return FALSE;           /* drawn somewhere else (a picture of it): no commit follows */
	}
	int kind = C_NONE, said = -1;
	GtkStyleContext *sc = gtk_widget_get_style_context(w);
	for (int k = 0; k < 3; k++) {
		if (gtk_style_context_has_class(sc, c_class[k])) {
			said = k;
		}
	}
	if (said >= 0) {
		kind = said;            /* the app's own word, once (taken off outside the paint) */
		g_idle_add_full(G_PRIORITY_HIGH, class_off, g_object_ref(w), NULL);
	} else if (content_asked()) {
		if (ct.tap_top == w && g_get_monotonic_time() - ct.tap_us < CONTENT_TAP_MS * 1000) {
			kind = C_PAGE;
		} else if (ct.step_top == w) {
			kind = C_MOTION;
		}
	}
	if (ct.tap_top == w) {
		ct.tap_top = NULL;      /* the first repaint after a tap only */
	}
	if (ct.step_top == w) {
		ct.step_top = NULL;     /* one repaint per step */
	}
	if (kind == C_NONE) {
		if (logging && said >= 0) {
			fprintf(stderr, "glassine-ink: content none (the app's word)\n");
		}
		return FALSE;
	}
	Win *s = win_of(w);
	if (!s->ink) {
		struct wl_surface *ws = gdk_wayland_window_get_wl_surface(gw);
		if (!ws) {
			return FALSE;
		}
		/* (an ink object without areas changes nothing about the pen's ink) */
		s->ink = glassine_ink_manager_v1_get_ink_surface(mgr, ws);
	}
	glassine_ink_surface_v1_set_content(s->ink, (uint32_t)kind);
	if (logging) {
		const char *title = gtk_window_get_title(GTK_WINDOW(w));
		fprintf(stderr, "glassine-ink: content %s \"%s\"%s\n", c_name[kind], title ? title : "", said >= 0 ? " (the app's word)" : "");
	}
	return FALSE;
}

G_MODULE_EXPORT void gtk_module_init(gint *argc, gchar ***argv)
{
	logging = g_strcmp0(g_getenv("GLASSINE_INK_LOG"), "1") == 0;
	GdkDisplay *d = gdk_display_get_default();
	if (!d || !GDK_IS_WAYLAND_DISPLAY(d)) {
		if (logging) {
			fprintf(stderr, "glassine-ink: not on Wayland: off\n");
		}
		return;
	}
	struct wl_display *wd = gdk_wayland_display_get_wl_display(d);
	struct wl_event_queue *q = wl_display_create_queue(wd);
	struct wl_display *wrap = wl_proxy_create_wrapper(wd);
	wl_proxy_set_queue((struct wl_proxy *)wrap, q);
	struct wl_registry *reg = wl_display_get_registry(wrap);
	wl_registry_add_listener(reg, &reg_l, NULL);
	wl_display_roundtrip_queue(wd, q);
	wl_registry_destroy(reg);
	wl_proxy_wrapper_destroy(wrap);
	if (!mgr || !comp) {            /* no ink protocol here (no events to dispatch: the queue can go) */
		if (logging) {
			fprintf(stderr, "glassine-ink: no glassine_ink_manager_v1: off\n");
		}
		wl_event_queue_destroy(q);
		return;
	}
	/* the bound objects stay on the private queue: they get no events */
	const char *e = g_getenv("GLASSINE_INK_WIDGETS");
	types = g_strsplit(e ? e : "GtkXournal", ",", -1);
	if (logging) {
		fprintf(stderr, "glassine-ink: on (canvas types %s)\n", e ? e : "GtkXournal");
	}
	g_type_class_ref(GTK_TYPE_WIDGET);
	g_signal_add_emission_hook(g_signal_lookup("map", GTK_TYPE_WIDGET), 0, on_map, NULL, NULL);
	if (mgr_ver >= GLASSINE_INK_SURFACE_V1_SET_CONTENT_SINCE_VERSION) {
		/* (gtk_init set gtk_main_do_event as the handler before it loaded the modules) */
		gdk_event_handler_set(content_event, NULL, NULL);
	} else if (logging) {
		fprintf(stderr, "glassine-ink: content: the compositor's protocol is version %u: nothing said\n", mgr_ver);
	}
}
