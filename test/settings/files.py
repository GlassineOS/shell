"""The files Settings shares with others: the gestures file (the sway session reads it;
old action ids read as the new ones, never written) and the start page from its command
line (glassine-shell launch glassine-settings new PAGE). Exit 1 on a failed check.
   files.py REPO"""
import os
import re
import sys
import tempfile

sys.path.insert(0, sys.argv[1] + "/rootfs/usr/lib/glassine")
from settings import app as A, backend as B  # noqa: E402

fail = 0


def check(what, got, want):
    global fail
    if got == want:
        print("ok   " + what)
    else:
        print("FAIL %s\n     got:  %r\n     want: %r" % (what, got, want))
        fail = 1


d = tempfile.mkdtemp()
g = os.path.join(d, "gestures")
check("gestures: no file: 4 up is Home, 3 left/right the screens",
      [B.read_gestures(g)[k] for k in ((4, "up"), (3, "left"), (3, "right"))], ["home", "screen-next", "screen-prev"])
with open(g, "w") as f:
    f.write("# old\n2 tap launcher\n3 up maximize\n3 left ws-prev\n3 right ws-next\n4 up launcher\n4 down overview\n4 left bogus\n")
r = B.read_gestures(g)
check("gestures: old ids read as the new ones",
      [r[k] for k in ((2, "tap"), (3, "up"), (3, "left"), (3, "right"), (4, "up"), (4, "down"), (4, "left"))],
      ["home", "fullscreen", "screen-prev", "screen-next", "home", "overview", "move-next"])
B.write_gestures(r, g)
words = [l.split()[2] for l in open(g) if not l.startswith("#")]
check("gestures: an old id is never written", sorted(set(words) & set(B.GESTURE_OLD)), [])
check("gestures: written as read back", B.read_gestures(g), r)
check("gestures: every action has a label", sorted(a for a in B.GESTURE_ACTIONS if a not in A.pages.GA_NAME), [])
check("gestures: the labels", [A.pages.GA_NAME[a] for a in ("home", "screen-next", "screen-prev")], ["Home", "Next screen", "Previous screen"])
check("gestures: immersive's labels (spec 001 FR-019, FR-061)",
      [A.pages.GA_NAME[a] for a in ("fullscreen", "unfullscreen", "show-bar")], ["Immersive", "Leave immersive", "Show bar"])
# the edges (FR-060): an old file without edge lines has the defaults, top = Show bar
check("gestures: a file without edges: top edge Show bar, the others nothing",
      [r[("edge", e)] for e in B.GESTURE_EDGES], ["show-bar", "nothing", "nothing", "nothing"])
r[("edge", "left")] = "screen-prev"
r[("edge", "top")] = "nothing"
B.write_gestures(r, g)
check("gestures: the edges written as the sway session reads them, after the swipes",
      [l.strip() for l in open(g) if l.startswith("edge ")],
      ["edge top nothing", "edge bottom nothing", "edge left screen-prev", "edge right nothing"])
check("gestures: the swipe lines kept their format", [l.split()[:2] for l in open(g) if not l.startswith(("#", "edge"))][:2],
      [["2", "tap"], ["3", "up"]])
check("gestures: edges read back", B.read_gestures(g), r)
with open(g, "a") as f:
    f.write("edge middle home\nedge top bogus\n")
check("gestures: an unknown edge or action is left out", B.read_gestures(g)[("edge", "top")], "nothing")
ui = A.UI(B.Demo(), lambda: None)
ui.go("gestures")
pg = A.pages.build(ui, ui.be)
rows = {getattr(b, "pick_key", None): b for b in pg.blocks}
check("Gestures page: an Edges group with the four edges", [k for k in ("gEdgeTop", "gEdgeBottom", "gEdgeLeft", "gEdgeRight") if k in rows],
      ["gEdgeTop", "gEdgeBottom", "gEdgeLeft", "gEdgeRight"])
check("Gestures page: the top edge shows Show bar", rows["gEdgeTop"].sum_txt, "Show bar")

# an app's preferred mode (FR-063): ~/.config/glassine/app-modes, "DESKTOP_ID immersive"
am = os.path.join(d, "app-modes")
check("app-modes: no file: every app tiled", B.read_app_modes(am), {})
with open(am, "w") as f:
    f.write("# kept\norg.other.App immersive\n")
B.write_app_mode("org.koreader.KOReader", "immersive", am)
check("app-modes: Immersive adds the app's line", B.read_app_modes(am), {"org.other.App": "immersive", "org.koreader.KOReader": "immersive"})
B.write_app_mode("org.koreader.KOReader", "immersive", am)
check("app-modes: written once", sum(1 for l in open(am) if l.startswith("org.koreader.KOReader ")), 1)
B.write_app_mode("org.koreader.KOReader", "tiled", am)
check("app-modes: Tiled removes it, the others and comments kept", open(am).read(), "# kept\norg.other.App immersive\n")
B.write_app_mode("org.other.App", "tiled", am)
check("app-modes: nothing left: the file goes", os.path.exists(am), False)
ui.go("app", app="KOReader")
seg = [b for b in A.pages.build(ui, ui.be).blocks if getattr(b, "label", None) == "Opens"]
check("app page: a Window head with Opens: Tiled | Immersive", [(o, seg[0].cur) for o in seg[0].opts] if seg else None,
      [(("tiled", "Tiled"), "immersive"), (("immersive", "Immersive"), "immersive")])
seg[0].set_fn("tiled")
check("app page: Tiled sets the app's mode", ui.be.app("KOReader")["mode"], "tiled")
check("app page: the mode counts as the app's own setting", ui.be.customised(dict(ui.be.app("KOReader"), mode="immersive")), True)

for page, want in (("updates", "pkg-updates"), ("packages", "packages"), ("power", "power"), ("power#2", "power#2"),
                   ("network", "wifi"), ("pkg-changes", "pkg-changes"), ("../x", None), ("nope", None), ("", None),
                   ("pkg-updates#1", None), ("app", None)):
    check("start page %r" % page, A.start_page(page), want)

# the shell checks the page name before it starts Settings (launch_arg_bad in src/glassine-shell.c): its lists
# are copies of these, kept the same here
src = open(sys.argv[1] + "/src/glassine-shell.c").read()


def c_strings(name, rows=False):
    body = src.split(name + "[]", 1)[1].split("};", 1)[0] if not rows else src.split(name + "[][2]", 1)[1].split("};", 1)[0]
    return re.findall(r'"([^"]+)"', body)


tabs, subs, alias = c_strings("SETTINGS_TABS"), c_strings("SETTINGS_SUBS"), c_strings("SETTINGS_ALIAS", True)
check("the shell's Settings pages: the tabs", tabs, A.TOPICS)
check("the shell's Settings pages: the sub-pages", subs, [p for p in A.START if p not in A.TOPICS])
check("the shell's Settings pages: the aliases", dict(zip(alias[::2], alias[1::2])), A.START_ALIAS)
sys.exit(fail)
