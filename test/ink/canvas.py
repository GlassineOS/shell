"""A GTK 3 drawing app for the module's test: a toolbar over a scrolled window with a
large canvas (GtkDrawingArea). It prints the canvas' visible rect (the viewport's
allocation in the window) and the window's place (sway's tree) after each phase:
  PHASE NAME WX WY X Y W H
phase "one": the canvas window alone; phase "two": a second window without a canvas
beside it (sway tiles both). Then DONE."""
import json
import subprocess
import sys

import gi
gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk


def say(*a):
    print(*a, flush=True)


def con_rect(title):
    tree = json.loads(subprocess.run(["swaymsg", "-t", "get_tree", "-r"], capture_output=True, text=True).stdout
                      or "{}")
    stack = [tree]
    while stack:
        n = stack.pop()
        if n.get("name") == title and n.get("type") == "con":
            return n["rect"]["x"], n["rect"]["y"]
        stack += n.get("nodes", []) + n.get("floating_nodes", [])
    return None


def swaymsg_ok():
    try:
        subprocess.run(["swaymsg", "-t", "get_version"], capture_output=True, check=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


win = Gtk.Window(title="canvas")
win.set_decorated(False)
box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
bar = Gtk.Box(spacing=4)
bar.set_size_request(-1, 80)
for name in ("Pen", "Eraser", "Undo"):
    bar.pack_start(Gtk.Button(label=name), False, False, 0)
sw = Gtk.ScrolledWindow()
area = Gtk.DrawingArea()
area.set_size_request(3000, 3000)
sw.add(area)                            # (a GtkViewport between them)
box.pack_start(bar, False, False, 0)
box.pack_start(sw, True, True, 0)
win.add(box)
win.connect("destroy", Gtk.main_quit)
win.show_all()
other = None


def report(phase):
    vp = area.get_parent()
    a = vp.get_allocation()
    x, y = vp.translate_coordinates(win, 0, 0)
    origin = con_rect("canvas") if swaymsg_ok() else (0, 0)
    say("PHASE", phase, *(origin or (-1, -1)), x, y, a.width, a.height)


def phase_two():
    global other
    report("one")
    other = Gtk.Window(title="plain")
    other.set_decorated(False)
    other.add(Gtk.Label(label="no canvas here"))
    other.show_all()
    GLib.timeout_add(4000, finish)
    return False


def finish():
    report("two")
    say("DONE")
    Gtk.main_quit()
    return False


GLib.timeout_add(4000, phase_two)
Gtk.main()
sys.exit(0)
