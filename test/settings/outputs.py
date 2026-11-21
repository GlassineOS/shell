"""Screen size (spec 011): the output-density file (the compositor reads it), the output
key, which displays are listed, the ruler's length, the number field. Exit 1 on a failed
check.   outputs.py REPO"""
import os
import sys
import tempfile

sys.path.insert(0, sys.argv[1] + "/rootfs/usr/lib/glassine")
from settings import app as A, backend, outputs as O, pages  # noqa: E402

fail = 0


def check(what, got, want):
    global fail
    if got == want:
        print("ok   " + what)
    else:
        print("FAIL %s\n     got:  %r\n     want: %r" % (what, got, want))
        fail = 1


d = tempfile.mkdtemp()
f = os.path.join(d, "glassine", "output-density")
check("density: no file", O.read(f), {})
os.makedirs(os.path.dirname(f))
with open(f, "w") as fh:
    fh.write("# kept by hand\n\n4.25 Optoma HD146X Unknown\nabc Bad Line\n0.5 Too Small\n120 Too Big\n"
             "nan Not A Number\n3 HDMI-A-2\n3.5 HDMI-A-2\n7\n")
check("density: comments, a key with spaces, bad and out-of-range lines skipped, the later line counts",
      O.read(f), {"Optoma HD146X Unknown": 4.25, "HDMI-A-2": 3.5})
O.write("Optoma HD146X Unknown", 4.31234, f)
lines = open(f).read().splitlines()
check("density: a write replaces the display's line (three decimals) and keeps the others",
      lines, ["# kept by hand", "", "abc Bad Line", "0.5 Too Small", "120 Too Big", "nan Not A Number",
              "3 HDMI-A-2", "3.5 HDMI-A-2", "7", "4.312 Optoma HD146X Unknown"])
check("density: no temporary file left", sorted(os.listdir(os.path.dirname(f))), ["output-density"])
O.write("DP-1", 5.0, f)
check("density: a new display's line at the end", open(f).read().splitlines()[-1], "5 DP-1")
O.write("HDMI-A-2", None, f)
check("density: Forget removes every line of the display",
      O.read(f), {"Optoma HD146X Unknown": 4.312, "DP-1": 5.0})
check("density: the file ends with a newline", open(f).read().endswith("\n"), True)
O.write("New One", 2, os.path.join(d, "fresh", "glassine", "output-density"))
check("density: a write makes the folder", O.read(os.path.join(d, "fresh", "glassine", "output-density")), {"New One": 2.0})

# the output key: sway's identifier, else the connector name
check("key: make model serial", O.key(dict(name="HDMI-A-1", make="Optoma", model="HD146X", serial="Unknown")),
      "Optoma HD146X Unknown")
check("key: all unknown -> the connector name", O.key(dict(name="HDMI-A-1", make="Unknown", model="Unknown", serial="Unknown")),
      "HDMI-A-1")
check("key: an empty part reads Unknown, the others as they are (sway's key)",
      O.key(dict(name="DP-3", make="Acme", model="", serial=None)), "Acme Unknown Unknown")
check("key: missing fields count as unknown", O.key(dict(name="VIRTUAL-1")), "VIRTUAL-1")

# the listing: size known (gone), unknown (listed), known with a kept line (listed)
outs = [dict(name="A-1", make="Unknown", model="Unknown", serial="Unknown", scale=1, rect=dict(x=0, y=0), active=True),
        dict(name="B-1", make="Make", model="Known", serial="S1", scale=2, rect=dict(x=1000, y=0), active=True),
        dict(name="C-1", make="Make", model="Kept", serial="S2", scale=1.5, rect=dict(x=2000, y=0), active=True),
        dict(name="D-1", make="Unknown", model="Unknown", serial="Unknown", scale=1, rect=dict(x=0, y=0), active=False)]
mons = [dict(x=0, y=0, w_mm=0, h_mm=0), dict(x=1000, y=0, w_mm=300, h_mm=200), dict(x=2000, y=0, w_mm=300, h_mm=200)]
ls = O.listing(outs, mons, {"Make Kept S2": 6.0}, here=(0, 0))
check("listing: unknown size and kept lines; not known sizes nor inactive outputs",
      [(x["key"], x["name"], x["here"], x["density"], x["scale"]) for x in ls],
      [("A-1", "A-1", True, None, 1.0), ("Make Kept S2", "Make Kept", False, 6.0, 1.5)])
check("listing: no display server -> nothing unknown", [x["key"] for x in O.listing(outs, None, {})], [])
check("listing: a tablet's panel (size known) -> none", O.listing(outs[1:2], mons, {}), [])

# the ruler: the card when it fits, else whole centimetres, at least 2; logical px per mm
check("ruler: the card fits", O.ruler(3.78, 1, 451), (85.6, 3.78, True))
check("ruler: at scale 2 the card is half as many logical px", O.ruler(8, 2, 451)[1:], (4.0, True))
check("ruler: too narrow -> whole centimetres", O.ruler(3.78, 1, 240)[0::2], (60, False))
check("ruler: very narrow -> 2 cm", O.ruler(20, 1, 100)[0::2], (20, False))
check("step: 1 % longer / shorter, clamped", (O.step(4, 1), O.step(4, -1), O.step(100, 1), O.step(1, -1)),
      (4.04, 3.96, 100.0, 1.0))

# the number field
check("parse: dot, comma, range", [O.parse(s) for s in ("3.78", "3,78", "12", "0.9", "101", "1.2.3", ".", "", "a1")],
      [3.78, 3.78, 12.0, None, None, None, None, None, None])

os.environ["GLASSINE_DEMO_SCREEN"] = "unknown"
be = backend.Demo()
ui = A.UI(be, lambda: None)
ui.go("appearance")
ui.L = A.Layout(702, 884)
rows = [b for b in pages.build(ui, be).blocks if getattr(b, "label", "") == "Screen size"]
check("appearance: a display of unknown size -> the Screen size section", len(rows), 2)
ui.go("screen-size", screen=be.screens[0]["key"])
ui.focus_ppm()
check("field: typing opens the keyboard", ui.typing(), True)
for ch in "4,2.5x":
    ui.type(ch)
check("field: digits and one separator", ui.ppm_txt, "4,25")
ui.key("done")
check("field: confirmed -> kept, typing ends", (be.screens[0]["density"], ui.typing()), (4.25, False))
ui.focus_ppm()
ui.ppm_txt = "150"
ui.key("done")
check("field: out of range -> refused, nothing kept", (bool(ui.ppm_err), be.screens[0]["density"]), (True, 4.25))
blocks = pages.build(ui, be).blocks
check("page: a Ruler on the display Settings is on", any(isinstance(b, A.B.Ruler) for b in blocks), True)
r = next(b for b in blocks if isinstance(b, A.B.Ruler))
r.fn(1)
check("ruler: Longer keeps 1 % more", be.screens[0]["density"], 4.293)
be.set_density(be.screens[0]["key"], None)
check("Forget: not measured again", be.screens[0]["density"], None)

os.environ["GLASSINE_DEMO_SCREEN"] = "elsewhere"
be = backend.Demo()
ui = A.UI(be, lambda: None)
ui.L = A.Layout(702, 884)
ui.go("screen-size", screen=be.screens[0]["key"])
check("page: on another display no ruler", any(isinstance(b, A.B.Ruler) for b in pages.build(ui, be).blocks), False)

del os.environ["GLASSINE_DEMO_SCREEN"]
be = backend.Demo()
ui = A.UI(be, lambda: None)
ui.L = A.Layout(702, 884)
ui.go("appearance")
check("appearance: every size known -> no Screen size section",
      [b for b in pages.build(ui, be).blocks if getattr(b, "label", "") == "Screen size"], [])
sys.exit(fail)
