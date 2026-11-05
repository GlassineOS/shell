"""t13: Tab moves the focus over the drawn controls, Enter presses
the focused one, the ring draws (glassine-settings, demo backend, no display)."""
import sys

import cairo
import gi

gi.require_version("Gdk", "3.0")
from gi.repository import Gdk  # noqa: E402

sys.path.insert(0, sys.argv[1] + "/rootfs/usr/lib/glassine")
from settings import app as A, backend  # noqa: E402

ui = A.UI(backend.Demo(), lambda: None)
ui.go("about")
r = A.Renderer(ui)


class Win:                                         # the parts of Window.on_key uses
    def __init__(self):
        self.ui, self.r = ui, r

    def redraw(self):
        pass


def draw():
    s = cairo.ImageSurface(cairo.FORMAT_RGB24, 702, 884)
    r.draw(cairo.Context(s), 702, 884, 1)


def key(name):
    ev = Gdk.Event.new(Gdk.EventType.KEY_PRESS)
    ev.keyval = Gdk.keyval_from_name(name)
    ev.state = 0
    return A.Window.on_key(Win(), None, ev)


draw()
n = len(r.hits)
key("Tab")
draw()
ok = ui.kfocus == 0 and n > 0
key("ISO_Left_Tab")
ok = ok and ui.kfocus == n - 1
# Enter presses the focused target (a recorder in its place)
draw()
pressed = []
r.hits[1] = (r.hits[1][0], lambda: pressed.append(1))
ui.kfocus = 1
key("Return")
ok = ok and pressed == [1]
draw()                                             # the ring draws on the focused target
print(("ok   " if ok else "FAIL ") + "Tab focus: %d targets, Shift-Tab wraps, Enter presses" % n)
fails = 0 if ok else 1


def check(what, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    fails += 0 if cond else 1


def size(W, H, page):
    """the page drawn in a window -> its markers"""
    s_ = cairo.ImageSurface(cairo.FORMAT_RGB24, W, H)
    ui.go(page)
    r.draw(cairo.Context(s_), W, H, 1)
    return r.markers


def keyed(name, shift=False):
    ev = Gdk.Event.new(Gdk.EventType.KEY_PRESS)
    ev.keyval = Gdk.keyval_from_name(name)
    ev.state = Gdk.ModifierType.SHIFT_MASK if shift else 0
    return A.Window.on_key(Win(), None, ev)


def focus_on(pred):
    """the Tab focus on the first hit whose callable satisfies pred"""
    i = next(i for i, h in enumerate(r.hits) if pred(h[1]))
    ui.kfocus = i


# the header: compact below 560 wide or 700 high, the pager in it
check("header roomy at 702x884", size(702, 884, "eink")["header"] == "roomy")
check("header compact at 345x884", size(345, 884, "eink")["header"] == "compact")
check("header compact at 936x650", size(936, 650, "eink")["header"] == "compact")
# fit: a list pages and never folds; with three or more sections all may fold
m = size(702, 884, "apps")
check("apps: the lists page, not fold (%s)" % m["fit"], m["fit"] == "paged" and not any(x["kind"] == "section-link" for x in m["widgets"]))
m = size(345, 436, "eink")
check("eink in a quarter is an index of rows (%s)" % m["fit"], "collapsed" in m["fit"] and not any(x["kind"] == "seg" for x in m["widgets"]))
ui.app = "Firefox ESR"
m = size(702, 884, "app")
check("app: E-ink is always a row (%s)" % m["fit"],
      any(x["kind"] == "section-link" and x["section"] == "E-ink" for x in m["widgets"]))
be = ui.be
# sliders move with the arrows
size(702, 884, "sound")
hit = next((h for h in r.hits if hasattr(h[1], "arrow")), None)
check("a slider has arrow keys", hit is not None)
# a text field asks the system's keyboard; Settings draws none
calls = []
be.osk_show = lambda on: calls.append(on)
ui.pk.kb = False
size(702, 884, "packages")
next(h for h in r.hits if getattr(h[1], "__self__", None) is ui.pk and h[1].__name__ == "focus")[1]()
check("search field asks for the keyboard", calls == [True] and ui.typing())
keyed("r")
keyed("BackSpace")
keyed("v")
check("search field takes keys", ui.pk.q == "v")
keyed("Escape")
check("Escape closes the keyboard first", calls == [True, False] and ui.page == "packages")
keyed("Escape")
check("then steps back", ui.page == "apps")
ui.go("wifi-new", net="x")
check("the password page asks too, leaving hides", calls[-1] is True)
ui.go("wifi")
check("  and hides on leaving", calls[-1] is False)
# Remove runs the change flow; its settings go with it
ui.pk.d.ack()
ui.go("app", app="Calendar")
ui.pk.ask("uninstall", "Calendar")()
check("Remove: the plan's sheet", ui.sheet is not None and ui.sheet.title == "Remove Calendar?")
keyed("Escape")
check("Escape closes the sheet, the page stays", ui.sheet is None and ui.page == "app")
be.set_app_fluid("Calendar", "clear")
be.set_fav("Calendar", True)
ui.pk._ask("uninstall", "Calendar")
ui.pk.d.result = {"kind": "ok", "plan": ui.sheet.plan}
ui.pk.show_result()
a = be.app("Calendar")
check("Remove done: settings cleared, marked removed", a["removed"] and a["fluid"] is None and not a["fav"])
ui.go("apps")
core = next(x for x in be.apps if be.origin(x)[0] == "core")
ui.go("app", app=core["id"])
s_ = cairo.ImageSurface(cairo.FORMAT_RGB24, 702, 884)
r.draw(cairo.Context(s_), 702, 884, 1)
# the real backend's parts that need no system (the rest: a device run)
import types  # noqa: E402

R = backend.Real
wrote = []
stub = types.SimpleNamespace(fluid="fluid", _try=lambda fn: fn(),
                             eink=types.SimpleNamespace(set_many=lambda kv: wrote.append(dict(kv)), set=lambda k, v: wrote.append({k: v}),
                                                        set_kv=lambda m, k, v: wrote.append((m, k, v))),
                             app=lambda aid: app_a)
app_a = dict(id="x", match="x")
R.set_fluid(stub, "balanced")
check("real: Fluidity writes eink.conf's keys", wrote[-1] == dict(motion="adaptive", still="range", curve="off", mmin="gc16", mmax="a2_4"))
R.set_app_fluid(stub, "x", "clear")
check("real: an app's Fluidity is paint=", ("x", "paint", "gc16-a2_8") in wrote)
R.set_app_fluid(stub, "x", None)
check("real: clearing removes it", wrote[-2] == ("x", "paint", None))
backend.PACKS_CONF = sys.argv[1] + "/rootfs/usr/share/glassine/setup/packs.conf"
backend.read, real_read = (lambda f: "packs = web, dev\n" if f == backend.SETUP_CONF else real_read(f)), backend.read
stub = types.SimpleNamespace(_owners=lambda: dict(ff="firefox", foot="foot", mine="mine", base="basepkg"), added_fn=lambda: ["mine"])
stub._pack_of = lambda p: None
org = lambda a: R.origin(stub, dict(dict(fp=""), **a))
check("real: origins", org(dict(id="ff"))[0] == "system" and org(dict(id="foot"))[0] == "core" and org(dict(id="mine"))[0] == "package" and
      org(dict(id="overlay-kit"))[0] == "core" and org(dict(id="org.x", fp="user"))[0] == "flatpak" and org(dict(id="waydroid.com.a"))[:2] == ("android", "com.a"))
sp = types.SimpleNamespace()
packs_txt = real_read(backend.PACKS_CONF)
first = next(l for l in packs_txt.splitlines() if l.strip().startswith("apk"))
pkg = first.split("=", 1)[1].split()[0]
print("     packs.conf sample:", pkg, "->", R._pack_of(sp, pkg))
backend.read = real_read
sys.exit(1 if fails else 0)
