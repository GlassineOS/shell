"""Settings and setup are not scaled twice (spec 012 FR-031, FR-032): the session gives
whole-scale-only GTK apps the remainder of a fractional factor as GDK_DPI_SCALE; our own
programs draw at the factor themselves, so they take the variable out before GTK starts.
Run under a display server at GTK scale 2 with GDK_DPI_SCALE set:
    GDK_SCALE=2 GDK_DPI_SCALE=1.5 xvfb-run -a python3 dpi.py REPO
Each window is drawn by GTK (its real draw handler, at 80 and 120 % on a panel of 8.943 px
per mm, text size 150 %) and compared byte for byte with the same page rendered without a
display and without the variable. Exit 1 on a failed check."""
import os
import sys

R = sys.argv[1]
sys.path.insert(0, R + "/rootfs/usr/lib/glassine")
SET = os.environ.get("GDK_DPI_SCALE")
import settings  # noqa: E402
import setup  # noqa: E402
from settings import app as A, backend  # noqa: E402
from setup import app as SA, backend as SB  # noqa: E402
import cairo  # noqa: E402
from gi.repository import GLib, Gtk  # noqa: E402
from glassine_ui import size as S  # noqa: E402

PANEL = 8.943
fail = 0


def check(ok, what):
    global fail
    print(("ok   " if ok else "FAIL ") + what)
    fail |= not ok


check(SET is not None and "GDK_DPI_SCALE" not in os.environ and settings.GDK_DPI_SCALE == SET,
      "GDK_DPI_SCALE (%s) taken out of the environment before GTK starts" % SET)
check(settings.child_env().get("GDK_DPI_SCALE") == SET, "... and given back to the programs Settings starts")
Gtk.init([])


def settle(win, w, h):
    win.set_default_size(w, h)
    win.show_all()
    for _ in range(200):
        while Gtk.events_pending():
            Gtk.main_iteration()
        a = win.area.get_allocation()
        if a.width == w and a.height == h and win.area.get_scale_factor() == 2:
            return True
        GLib.usleep(10000)
    return False


def gtk_draw(win, px):
    """the window's area drawn by GTK into an image of px device pixels (its scale 2)"""
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, *px)
    surf.set_device_scale(2, 2)
    cr = cairo.Context(surf)
    win.area.draw(cr)
    surf.flush()
    return bytes(surf.get_data())


for pct in (80, 120):
    fac = S.factor(PANEL, pct)
    for page, px in (("appearance", (1404, 1768)), ("wifi", (1872, 1300))):
        be = backend.Demo()
        be.text_size = 150
        d = be.displays[0]
        d.update(percent=pct, factor=fac, applied=True)
        ui = A.UI(be, lambda: None)
        win = A.Window(None, ui)
        ok = settle(win, px[0] // 2, px[1] // 2)
        check(ok, "Settings: a %dx%d window at GTK scale 2" % (px[0] // 2, px[1] // 2))
        ui.go(page)
        got = gtk_draw(win, px)
        r = A.render(["--demo", "--render", "-", "--page", page, "--px", "%dx%d" % px, "--ui-size", str(pct),
                      "--density", repr(PANEL), "--text", "150"], backend.Demo())
        r.surf.flush()
        check(abs(win.z - fac / 2) < 1e-9, "Settings %d %%: drawn at F / N = %.4f" % (pct, win.z))
        check(got == bytes(r.surf.get_data()), "Settings %s at %d %%, text 150 %%, GDK_DPI_SCALE=%s: the render without it" % (page, pct, SET))
        win.destroy()

    sbe = SB.Demo()
    app = SA.Setup(sbe, lambda: None)
    app.disp = sbe.display()
    app.disp.update(percent=pct, factor=fac, applied=True)
    app.text = 150
    app.s.step = "look"
    win = SA.Window(app)
    win.disconnect_by_func(Gtk.main_quit)
    win.replace = lambda: False                       # (the demo's display and text size stay as set)
    px = (1404, 1872)
    check(settle(win, px[0] // 2, px[1] // 2), "setup: a 702x936 window at GTK scale 2")
    got = gtk_draw(win, px)
    r = SA.render(["--demo", "--render", "-", "--keep", "--step", "look", "--px", "%dx%d" % px, "--ui-size", str(pct),
                   "--density", repr(PANEL), "--text", "150"], SB.Demo())
    r.surf.flush()
    check(got == bytes(r.surf.get_data()), "setup look at %d %%, text 150 %%, GDK_DPI_SCALE=%s: the render without it" % (pct, SET))
    win.destroy()
sys.exit(fail)
