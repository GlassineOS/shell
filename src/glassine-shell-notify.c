/* glassine-shell-notify.c — glassine-shell is the desktop's notification server
 * (org.freedesktop.Notifications on the session bus). E-ink: a notice never pops up over the apps -- it goes into
 * the list behind the bar's clock, whose counter shows how many are waiting;
 * the list opens with a tap on the clock. Notices stay until dismissed (a tap
 * opens one: its "default" action; the x or "Clear all" dismisses), their
 * expire timeouts are ignored, transient ones (volume, brightness OSDs) are
 * dropped at once.
 *
 * GDBus dispatches on the default GMainContext; glassine-shell's poll loop runs
 * that context (see main_context_* in glassine-shell.c). */
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include "glassine-shell.h"

Notice notes[MAXNOTE];
int nnotes;

static GDBusConnection *bus;
static void (*on_change)(void);
static unsigned next_id = 1;

static const char XML[] =
    "<node><interface name='org.freedesktop.Notifications'>"
    "<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
    "<method name='Notify'><arg type='s' name='app_name' direction='in'/><arg type='u' name='replaces_id' direction='in'/>"
    "<arg type='s' name='app_icon' direction='in'/><arg type='s' name='summary' direction='in'/>"
    "<arg type='s' name='body' direction='in'/><arg type='as' name='actions' direction='in'/>"
    "<arg type='a{sv}' name='hints' direction='in'/><arg type='i' name='expire_timeout' direction='in'/>"
    "<arg type='u' direction='out'/></method>"
    "<method name='CloseNotification'><arg type='u' name='id' direction='in'/></method>"
    "<method name='GetServerInformation'><arg type='s' direction='out'/><arg type='s' direction='out'/>"
    "<arg type='s' direction='out'/><arg type='s' direction='out'/></method>"
    "<signal name='NotificationClosed'><arg type='u'/><arg type='u'/></signal>"
    "<signal name='ActionInvoked'><arg type='u'/><arg type='s'/></signal>"
    "</interface></node>";

/* body markup (b i u a img, entities) to plain text */
static void plain(const char *in, char *out, size_t n)
{
    size_t k = 0;
    for (const char *p = in; *p && k + 4 < n; p++) {
        if (*p == '<') {
            const char *e = strchr(p, '>');
            if (!e) break;
            if (!strncmp(p, "<br", 3)) out[k++] = ' ';
            p = e;
            continue;
        }
        if (*p == '&') {
            static const struct { const char *e, *c; } ent[] = {
                { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" }, { "&#39;", "'" } };
            unsigned i;
            for (i = 0; i < sizeof ent / sizeof *ent; i++)
                if (!strncmp(p, ent[i].e, strlen(ent[i].e))) break;
            if (i < sizeof ent / sizeof *ent) { out[k++] = ent[i].c[0]; p += strlen(ent[i].e) - 1; continue; }
        }
        out[k++] = *p == '\n' || *p == '\t' ? ' ' : *p;
    }
    out[k] = 0;
}

static int find(unsigned id)
{
    for (int i = 0; i < nnotes; i++) if (notes[i].id == id) return i;
    return -1;
}

static void closed(unsigned id, unsigned reason)
{
    if (bus) g_dbus_connection_emit_signal(bus, NULL, "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                                           "NotificationClosed", g_variant_new("(uu)", id, reason), NULL);
}

static void drop(int i)
{
    memmove(&notes[i], &notes[i + 1], (nnotes - i - 1) * sizeof *notes);
    nnotes--;
}

static void notify(GVariant *p, GDBusMethodInvocation *inv)
{
    const char *app, *icon, *summary, *body;
    guint32 rid;
    gint32 timeout;
    GVariantIter *acts, *hints;
    g_variant_get(p, "(&su&s&s&sasa{sv}i)", &app, &rid, &icon, &summary, &body, &acts, &hints, &timeout);
    Notice n = { 0 };
    snprintf(n.app, sizeof n.app, "%s", app);
    snprintf(n.icon, sizeof n.icon, "%s", icon);
    plain(summary, n.title, sizeof n.title);
    plain(body, n.body, sizeof n.body);
    n.t = time(NULL);
    const char *a;
    int k = 0;
    while (g_variant_iter_next(acts, "&s", &a)) if (k++ % 2 == 0 && !strcmp(a, "default")) n.has_default = 1;
    int transient = 0;
    const char *key;
    GVariant *v;
    while (g_variant_iter_next(hints, "{&sv}", &key, &v)) {
        if (!strcmp(key, "transient") && g_variant_is_of_type(v, G_VARIANT_TYPE_BOOLEAN)) transient = g_variant_get_boolean(v);
        else if (!strcmp(key, "desktop-entry") && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING))
            snprintf(n.entry, sizeof n.entry, "%s", g_variant_get_string(v, NULL));
        else if ((!strcmp(key, "image-path") || !strcmp(key, "image_path")) && !n.icon[0] &&
                 g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) {
            const char *s = g_variant_get_string(v, NULL);
            snprintf(n.icon, sizeof n.icon, "%s", strncmp(s, "file://", 7) ? s : s + 7);
        }
        g_variant_unref(v);
    }
    g_variant_iter_free(acts);
    g_variant_iter_free(hints);

    int old = rid ? find(rid) : -1;
    n.id = old >= 0 ? rid : next_id++;
    if (!next_id) next_id = 1;
    g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", n.id));
    if (old >= 0) drop(old);
    if (transient || (!n.title[0] && !n.body[0])) {
        closed(n.id, 1);
        if (old >= 0 && on_change) on_change();
        return;
    }
    if (nnotes == MAXNOTE) { closed(notes[MAXNOTE - 1].id, 1); nnotes--; }
    memmove(&notes[1], &notes[0], nnotes * sizeof *notes);
    notes[0] = n;
    nnotes++;
    if (on_change) on_change();
}

/* a notice of the shell itself (no D-Bus client) */
void notify_local(const char *app, const char *icon, const char *title, const char *body)
{
    Notice n = { .t = time(NULL) };
    snprintf(n.app, sizeof n.app, "%s", app);
    snprintf(n.icon, sizeof n.icon, "%s", icon);
    snprintf(n.title, sizeof n.title, "%s", title);
    snprintf(n.body, sizeof n.body, "%s", body);
    n.id = next_id++;
    if (!next_id) next_id = 1;
    if (nnotes == MAXNOTE) { closed(notes[MAXNOTE - 1].id, 1); nnotes--; }
    memmove(&notes[1], &notes[0], nnotes * sizeof *notes);
    notes[0] = n;
    nnotes++;
    if (on_change) on_change();
}

static void method(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *name,
                   GVariant *p, GDBusMethodInvocation *inv, gpointer ud)
{
    (void)c; (void)sender; (void)path; (void)iface; (void)ud;
    if (!strcmp(name, "Notify")) notify(p, inv);
    else if (!strcmp(name, "GetCapabilities")) {
        const char *caps[] = { "body", "actions", "persistence", NULL };
        g_dbus_method_invocation_return_value(inv, g_variant_new("(^as)", caps));
    } else if (!strcmp(name, "GetServerInformation")) {
        g_dbus_method_invocation_return_value(inv, g_variant_new("(ssss)", "glassine-shell", "glassine", "1", "1.2"));
    } else if (!strcmp(name, "CloseNotification")) {
        guint32 id;
        g_variant_get(p, "(u)", &id);
        g_dbus_method_invocation_return_value(inv, NULL);
        int i = find(id);
        if (i >= 0) { drop(i); closed(id, 3); if (on_change) on_change(); }
    } else g_dbus_method_invocation_return_dbus_error(inv, "org.freedesktop.DBus.Error.UnknownMethod", name);
}

static const GDBusInterfaceVTable vt = { method, NULL, NULL, { 0 } };

static void acquired(GDBusConnection *c, const char *name, gpointer ud) { (void)c; (void)ud; fprintf(stderr, "glassine-shell: %s owned\n", name); }
static void lost(GDBusConnection *c, const char *name, gpointer ud)
{
    (void)c; (void)ud;
    fprintf(stderr, "glassine-shell: %s not owned (another notification server?)\n", name);
}

int notify_init(void (*changed)(void))
{
    on_change = changed;
    GError *err = NULL;
    bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &err);
    if (!bus) { fprintf(stderr, "glassine-shell: session bus: %s\n", err ? err->message : "?"); g_clear_error(&err); return -1; }
    GDBusNodeInfo *ni = g_dbus_node_info_new_for_xml(XML, NULL);
    if (!g_dbus_connection_register_object(bus, "/org/freedesktop/Notifications", ni->interfaces[0], &vt, NULL, NULL, &err)) {
        fprintf(stderr, "glassine-shell: register: %s\n", err ? err->message : "?");
        g_clear_error(&err);
        return -1;
    }
    /* REPLACE: take the name from another server if it lets go; nobody takes it from us */
    g_bus_own_name_on_connection(bus, "org.freedesktop.Notifications",
                                 G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                 acquired, lost, NULL, NULL);
    return 0;
}

void notify_dismiss(unsigned id)
{
    int i = find(id);
    if (i < 0) return;
    drop(i);
    closed(id, 2);
}

void notify_invoke(unsigned id)
{
    int i = find(id);
    if (i < 0) return;
    if (notes[i].has_default && bus)
        g_dbus_connection_emit_signal(bus, NULL, "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                                      "ActionInvoked", g_variant_new("(us)", id, "default"), NULL);
    notify_dismiss(id);
}

void notify_clear(void)
{
    while (nnotes) notify_dismiss(notes[0].id);
}
