"""A self-drawn GTK 3 app for the module's content test: one window, one drawing area
that repaints when asked. It plays taps, a drag and a scroll as events of its own
(put on GDK's queue, so they pass the module as real ones do) and prints a line
before each, "STEP NAME"; the module's log (GLASSINE_INK_LOG=1, stderr) then shows
what it said for the repaint that followed.
  content.py asked     the app asks for it (GLASSINE_CONTENT=auto:PID, set after
                       Gtk is imported, as importing GlassineOS/ui's glassine_ui does)
  content.py parent    it inherits another process's mark (auto:1)
  content.py plain     it asks for nothing
Then DONE."""
import os
import sys

import gi
gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, GLib, Gtk  # noqa: E402

mode = sys.argv[1]
if mode == "asked":
    os.environ["GLASSINE_CONTENT"] = "auto:%d" % os.getpid()
elif mode == "parent":
    os.environ["GLASSINE_CONTENT"] = "auto:1"
else:
    os.environ.pop("GLASSINE_CONTENT", None)


def say(*a):
    print(*a, flush=True)
    sys.stderr.write("STEP " + " ".join(str(x) for x in a[1:]) + "\n")
    sys.stderr.flush()


win = Gtk.Window(title="pages")
win.set_decorated(False)
area = Gtk.DrawingArea()
area.add_events(Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK |
                Gdk.EventMask.POINTER_MOTION_MASK | Gdk.EventMask.SCROLL_MASK)
shade = [0]
repaint = {"release": True, "motion": True, "scroll": True}


def on_draw(w, cr):
    shade[0] = (shade[0] + 1) % 4
    cr.set_source_rgb(shade[0] / 4, shade[0] / 4, shade[0] / 4)
    cr.paint()


area.connect("draw", on_draw)
area.connect("button-release-event", lambda w, e: repaint["release"] and w.queue_draw())
area.connect("motion-notify-event", lambda w, e: repaint["motion"] and w.queue_draw())
area.connect("scroll-event", lambda w, e: repaint["scroll"] and w.queue_draw())
win.add(area)
win.connect("destroy", Gtk.main_quit)
win.show_all()


def ev(kind, x, y):
    e = Gdk.Event.new(kind)
    e.any.window = area.get_window()
    e.any.send_event = True
    seat = Gdk.Display.get_default().get_default_seat()
    e.set_device(seat.get_pointer())
    if kind == Gdk.EventType.MOTION_NOTIFY:
        e.motion.x, e.motion.y, e.motion.x_root, e.motion.y_root = x, y, x, y
        e.motion.state = Gdk.ModifierType.BUTTON1_MASK
    elif kind == Gdk.EventType.SCROLL:
        e.scroll.x, e.scroll.y, e.scroll.x_root, e.scroll.y_root = x, y, x, y
        e.scroll.direction = Gdk.ScrollDirection.DOWN
    else:
        e.button.x, e.button.y, e.button.x_root, e.button.y_root = x, y, x, y
        e.button.button = 1
    e.put()
    # GDK holds motion back until the frame clock's next event flush; real input asks for it
    area.get_window().get_frame_clock().request_phase(Gdk.FrameClockPhase.FLUSH_EVENTS)


P, R, M, S = (Gdk.EventType.BUTTON_PRESS, Gdk.EventType.BUTTON_RELEASE, Gdk.EventType.MOTION_NOTIFY,
              Gdk.EventType.SCROLL)


def tap():
    ev(P, 50, 50)
    ev(R, 50, 50)


def style(kind):
    win.get_style_context().add_class("glassine-content-" + kind)


def set_repaint(k, v):
    repaint[k] = v


# (name, what happens); 400 ms apart, so each repaint is over before the next step
steps = [
    ("idle-repaint", lambda: area.queue_draw()),            # nothing happened: says nothing
    ("tap", tap),                                           # a tap that repaints: a page
    ("repaint-after-tap", lambda: area.queue_draw()),       # ... its first repaint only
    ("tap-without-repaint", lambda: (set_repaint("release", False), tap())),
    ("late-repaint", lambda: (set_repaint("release", True), area.queue_draw())),    # 400 ms after that tap: no page
    ("press", lambda: ev(P, 50, 50)),
    ("wiggle", lambda: ev(M, 53, 52)),                      # inside the slop: no drag yet
    ("drag-1", lambda: ev(M, 50, 90)),                      # a step: motion
    ("drag-2", lambda: ev(M, 50, 130)),
    ("drop", lambda: ev(R, 50, 130)),                       # the result: nothing, and no tap
    ("scroll", lambda: ev(S, 50, 50)),                      # a wheel's scroll: nothing (no drag)
    ("own-page", lambda: (style("page"), area.queue_draw())),       # the app's own word
    ("own-none", lambda: (style("none"), tap())),           # ... also against the rule
    ("tap-again", tap),                                     # the word held for one repaint
]


def run(i):
    if i == len(steps):
        say("DONE", "done")
        Gtk.main_quit()
        return False
    say("STEP", steps[i][0])
    steps[i][1]()
    GLib.timeout_add(400, run, i + 1)
    return False


GLib.timeout_add(1500, run, 0)
Gtk.main()
sys.exit(0)
