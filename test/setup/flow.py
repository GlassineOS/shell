"""glassine-setup's flow on the demo backend, without a display: tap Continue through
every step, a pack card, the eye, a language row; Continue waits for a valid account."""
import sys

import cairo

P = sys.argv[1]
sys.path[:0] = [P + "/rootfs/usr/lib/glassine", P + "/rootfs/usr/lib/glassine"]
from setup import app as A, backend  # noqa: E402

fail = 0


def check(ok, what):
    global fail
    print(("ok   " if ok else "FAIL ") + what)
    fail |= not ok


be = backend.Demo()
be.apply = lambda s, packs, redraw: None          # no GLib timer here
app = A.Setup(be, lambda: None)
r = A.Renderer(app)


def draw():
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, A.W0, A.H0)
    r.draw(cairo.Context(surf), A.W0, A.H0, 1)


def tap(x, y):
    draw()
    _, fn = r.at(x, y)
    fn and fn()
    return bool(fn)


PRIMARY = (A.W0 - 60, A.H0 - 42)                  # the footer's right button
seen = []
for _ in range(9):
    seen.append(app.s.step)
    if app.s.step == "installing":
        break
    tap(*PRIMARY)
check(seen == A.STEPS + ["installing"], "Continue walks every step: " + " ".join(seen))

app.go("software")
draw()
n = len(app.s.packs)
tap(60, 400)                                      # the first card row (left column)
check(len(app.s.packs) != n, "a pack card toggles (%d -> %d)" % (n, len(app.s.packs)))

app.go("account")
app.s.pw2 = "x"
tap(*PRIMARY)
check(app.s.step == "account", "Continue waits while the passwords differ")
app.s.pw2 = app.s.pw
draw()
eye = [rect for rect, fn in r.hits if rect[2] == 48 and rect[3] == 44]
check(bool(eye), "the password field has its eye")
if eye:
    x, y, w, h = eye[0]
    tap(x + 10, y + 10)
    check(app.s.show_pw, "the eye shows the password")
tap(*PRIMARY)
check(app.s.step == "software", "a valid account continues")

# the admin password (Account step): default glassine passes, short or with ':' does not
app.s.apw = "short"
check(not app.s.account_ok(), "a short admin password blocks the account")
app.s.apw = "abc:defgh"
check(not app.s.account_ok(), "an admin password with ':' blocks the account")
app.s.apw = "glassine"
check(app.s.account_ok(), "the default admin password passes")

# a reset asks only for what it removed: needs -> steps, Continue walks only those
import os  # noqa: E402
for v, want in (("system,network", ["welcome", "location", "keyboard", "network", "review"]),
                ("network,software", ["welcome", "network", "software", "review"]),
                ("account,look", ["welcome", "look", "account", "review"])):
    os.environ["GLASSINE_SETUP_DEMO_NEEDS"] = v
    b2 = backend.Demo()
    b2.apply = lambda s, packs, redraw: None
    a2 = A.Setup(b2, lambda: None)
    check(a2.steps == want, "needs %s -> steps %s" % (v, " ".join(a2.steps)))
    r2 = A.Renderer(a2)
    walked = []
    for _ in range(8):
        walked.append(a2.s.step)
        if a2.s.step == "installing":
            break
        surf = cairo.ImageSurface(cairo.FORMAT_RGB24, A.W0, A.H0)
        r2.draw(cairo.Context(surf), A.W0, A.H0, 1)
        _, fn = r2.at(*PRIMARY)
        fn and fn()
    check(walked == want + ["installing"], "  Continue walks %s" % " ".join(walked))
del os.environ["GLASSINE_SETUP_DEMO_NEEDS"]

app.go("welcome")
draw()
tap(100, 330)
check(app.s.lang != "en-GB", "a language row selects (%s)" % app.s.lang)
app.s.focus = "query"
app.go("location")
app.s.focus = "query"
app.key("x"); app.key(back=True); app.key(back=True)
check(app.s.query == "Dre", "typing edits the focused field (%r)" % app.s.query)

# t13: Tab reaches every control, Enter presses it, the ring draws
app.go("welcome")
draw()
n = len(r.hits)
app.s.kfocus = n - 1                               # the last target: the footer's Begin
draw()
r.hits[app.s.kfocus][1]()
check(app.s.step == "look", "Tab focus + Enter presses the focused control (%d targets)" % n)

# --- the real backend (run.sh installed packs.conf, the apply and a doas shim)
import os  # noqa: E402
import time  # noqa: E402

rb = backend.Real()
rb._idle = staticmethod(lambda fn: fn())          # no main loop: run at once
rapp = A.Setup(rb, lambda: None)
s = rapp.s
check(len(rapp.packs) == 5 and all(p["status"] == "offered" for p in rapp.packs), "real: the 5 offered packs (%d)" % len(rapp.packs))
check(s.packs == ["web"], "real: the suggested pack preselected (%s)" % s.packs)
pl = rb.places("berl")
check(bool(pl) and pl[0][1] == "Europe/Berlin" and ":" in pl[0][3], "real: places('berl') -> %s" % (pl[:1],))
s.query, s.city = "berl", 0
s.name, s.user, s.host, s.pw, s.pw2 = "Bea Test", "bea", "bea-note", "Kiefernwald#8", "Kiefernwald#8"
s.packs = ["web", "dev"]
check(s.account_ok(), "real: the account validates")
rapp.go("installing")
for _ in range(100):
    if any(ln.startswith(("done", "failed")) for ln in rb.lines):
        break
    time.sleep(0.1)
print("     | " + "\n     | ".join(rb.lines))
st = rb.progress(rapp)
check(rb.lines[-1:] == ["done"], "real: the apply ran to done")
check(rapp.s.step == "done", "real: Installing moves on to Done")
check(all(state == "done" for _, state, _ in st["steps"]), "real: every step done")
check(rb.installed(rapp) == 2, "real: 2 packs installed")
check(os.path.exists("/home/bea/.config/glassine-shell/theme"), "real: bea's profile written")
draw_r = A.Renderer(rapp)
surf = cairo.ImageSurface(cairo.FORMAT_RGB24, A.W0, A.H0)
for step in ("location", "software", "review", "done"):
    rapp.s.step = step
    draw_r.draw(cairo.Context(surf), A.W0, A.H0, 1)
check(True, "real: location/software/review/done draw")
# Wi-Fi not skipped (no saved connection here): only Wi-Fi left; a partial plan keeps
# the zone (no Location step), sends no user, records network=skipped
from setup import needs as N  # noqa: E402
check(N.needs() == [], "real: the full run skipped Wi-Fi and recorded it: nothing asked (%s)" % N.needs())
conf = open("/etc/glassine/setup.conf").read()
check("network=skipped" in conf, "real: setup.conf says network=skipped")
open("/etc/glassine/setup.conf", "w").write(conf.replace("network=skipped\n", ""))
rb2 = backend.Real()
rb2._idle = staticmethod(lambda fn: fn())
rapp2 = A.Setup(rb2, lambda: None)
check(rapp2.steps == ["welcome", "network", "review"], "real: afterwards only Wi-Fi (%s)" % " ".join(rapp2.steps))
pl2 = rb2.plan(rapp2.s, rapp2)
check("user" not in pl2 and "packs" not in pl2 and "voice" not in pl2 and pl2.get("network") == "skipped",
      "real: the partial plan (%s)" % sorted(pl2))
check(pl2["zone"] == "Europe/Berlin", "real: the zone kept (%s)" % pl2["zone"])
check(rapp2.s.packs == ["web", "dev"], "real: the recorded packs prefilled (%s)" % rapp2.s.packs)
rapp2.go("installing")
for _ in range(100):
    if any(ln.startswith(("done", "failed")) for ln in rb2.lines):
        break
    time.sleep(0.1)
check(rb2.lines[-1:] == ["done"], "real: the partial apply ran to done (%s)" % rb2.lines[-1:])
check(N.needs() == [], "real: nothing left to ask (%s)" % N.needs())

rapp.s.user = "root"
rapp.go("installing")
for _ in range(50):
    if rb.lines and rb.lines[-1].startswith("failed"):
        break
    time.sleep(0.1)
st = rb.progress(rapp)
check("a system account" in st.get("failed", ""), "real: a refused plan shows why (%s)" % st.get("failed"))
sys.exit(fail)
