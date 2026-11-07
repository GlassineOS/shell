"""The files Settings shares with others: the gestures file (the sway session reads it;
old action ids read as the new ones, never written) and the start page from its command
line (glassine-shell launch glassine-settings new PAGE). Exit 1 on a failed check.
   files.py REPO"""
import os
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

for page, want in (("updates", "pkg-updates"), ("packages", "packages"), ("power", "power"), ("power#2", "power#2"),
                   ("network", "wifi"), ("pkg-changes", "pkg-changes"), ("../x", None), ("nope", None), ("", None),
                   ("pkg-updates#1", None), ("app", None)):
    check("start page %r" % page, A.start_page(page), want)
sys.exit(fail)
