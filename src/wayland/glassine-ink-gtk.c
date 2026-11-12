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
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <stdio.h>
#include <string.h>
#include "glassine-ink-v1-client-protocol.h"

static struct wl_compositor *comp;
static struct glassine_ink_manager_v1 *mgr;
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
		mgr = wl_registry_bind(r, name, &glassine_ink_manager_v1_interface, 1);
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

static gboolean on_map(GSignalInvocationHint *h, guint n, const GValue *v, gpointer data)
{
	GtkWidget *w = g_value_get_object(&v[0]);
	if (!GTK_IS_WINDOW(w) || !gtk_widget_is_toplevel(w)) {
		return TRUE;
	}
	Win *s = g_object_get_data(G_OBJECT(w), "glassine-ink");
	if (!s) {
		s = g_new0(Win, 1);
		s->win = w;
		g_object_set_data_full(G_OBJECT(w), "glassine-ink", s, win_free);
		/* any relayout inside (a canvas resized, a panel shown) reallocates down
		 * from the toplevel: one idle update after it */
		g_signal_connect_after(w, "size-allocate", G_CALLBACK(on_alloc), s);
		g_signal_connect(w, "unmap", G_CALLBACK(on_unmap), s);
	}
	schedule(s);
	return TRUE;
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
}
