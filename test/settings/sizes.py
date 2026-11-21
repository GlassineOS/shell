"""The interface size and the text size in Settings' pages (spec 012 FR-020 to FR-024b):
every page drawn in one process, no files but the lists.   sizes.py REPO OUT

- Default: a page drawn with nothing said is, byte for byte, the page drawn at factor 2 on
  a display of 8.943 px per mm at a text size of 100 % (what a tablet's panel gets), in
  every voice and window size; nothing reaches a minimum there;
- every step (80, 90, 100, 120 %) on a portrait and a landscape panel in every voice: the
  layout is the one for the window's size in design pixels, a paged page shows no fewer
  rows at a smaller step (among the steps with the same header: the compact one holds
  the pager), the header's rule, the card's edge and every row's rule have
  whole device pixels of their own colour (none thinner than one, none blurred);
- the text size at 125 and 150 %: every text style is drawn at the size of FR-024a, rows
  are no lower than at 100 %;
- the minimums at each step and at densities from 3 to 25 px per mm: no tap target under
  7 mm either way, running text and row labels at least 2.8 mm, other text 2.0 mm.
OUT/size-minimums.txt lists what reached a minimum (not kept in the repository).
Exit 1 on a failed check."""
import os
import sys

sys.path.insert(0, sys.argv[1] + "/rootfs/usr/lib/glassine")
from glassine_ui import paint as P  # noqa: E402
from glassine_ui import size as S  # noqa: E402
from glassine_ui import voices  # noqa: E402
from settings import app as A, backend  # noqa: E402

OUT = sys.argv[2] if len(sys.argv) > 2 else "."
THEMES = (os.environ.get("THEMES") or "white editorial stationery").split()
PAGES = ("wifi wifi-net wifi-new bt bt-dev bt-add sound light light-range eink render-apps apps origins app power "
         "keyboard kb-langs kb-words gestures appearance time-zone time-city storage about admin-auth packages "
         "eink#1 keyboard#3 power#3 screen-size:unknown screen-size:set screen-size:elsewhere appearance:unknown").split()
WINS = {"full": (702, 884), "column": (350, 884), "row": (702, 441), "quarter": (350, 441)}
PANEL = 8.943                           # a tablet's panel: 1404 x 1872 px on 157 x 209 mm
PORTRAIT, LAND = (1404, 1768), (1872, 1300)        # the panel below the bar at Default, in device px
RULED = ("tog", "item", "seg", "slider", "kv", "radio", "section-head", "step")
fail = 0
fonts = []                              # (shorthand, weight) of every text style a draw used
mins = {}                               # what reached a minimum -> where first


def check(ok, what):
    global fail
    if not ok:
        print("FAIL " + what)
        fail += 1
    return ok


_font = P.font


def font(s, weight=None, grow=True):
    if grow:
        fonts.append(s)
    return _font(s, weight, grow)


P.font = font


def draw(page, theme, size=None, px=None, fac=2.0, dens=0.0, text=100, markers=False):
    """-> the Renderer (with .surf) of one page"""
    page, _, scr = page.partition(":")
    os.environ.pop("GLASSINE_DEMO_SCREEN", None)
    if scr:
        os.environ["GLASSINE_DEMO_SCREEN"] = scr
    argv = ["--demo", "--render", "-", "--page", page, "--theme", theme, "--factor", repr(fac), "--density", repr(dens),
            "--text", str(text)]
    argv += ["--px", "%dx%d" % px] if px else ["--size", "%dx%d" % (size or WINS["full"])]
    if page == "wifi-new":
        argv += ["--pw", "kranich"]
    if page == "admin-auth":
        argv += ["--pw", "secret", "--auth-err"]
    del fonts[:]
    return A.render(argv, backend.Demo())


def data(r):
    r.surf.flush()
    return bytes(r.surf.get_data())


class Img:
    def __init__(self, surf):
        surf.flush()
        self.w, self.h, self.st, self.d = surf.get_width(), surf.get_height(), surf.get_stride(), bytes(surf.get_data())

    def px(self, x, y):
        o = y * self.st + x * 4
        return (self.d[o + 2], self.d[o + 1], self.d[o])


def rgb(col):
    return tuple(int(round(v * 255)) for v in P.color(col)[:3])


def has_rule(im, fac, x0, x1, y, bd, what):
    """a rule (CSS border bd) across x0..x1 with its lower edge at y (design px): a device
    pixel row of its colour (solid: nearly the whole row; dotted or dashed: some of it)"""
    b = P.border(bd)
    if not b:
        return True
    col = tuple(int(round(v * 255)) for v in b[2][:3])
    a, z = int(x0 * fac) + 2, int(x1 * fac) - 2
    best = 0
    for yy in range(max(0, int(y * fac) - 4), min(im.h, int(y * fac) + 3)):
        n = sum(1 for xx in range(a, min(z, im.w)) if im.px(xx, yy) == col)
        best = max(best, n)
    need = 0.9 * (z - a) if b[1] == "solid" else 0.15 * (z - a)
    return check(best >= need, "%s: no device pixel row of the rule's colour (%d of %d px)" % (what, best, z - a))


def minimums(r, mm, what):
    """the tap targets and the text styles against FR-022; -> notes what reached one"""
    m = S.min_tap()
    for (x, y, w, h), _ in r.hits:
        check(w >= m - 1e-6 and h >= m - 1e-6, "%s: a tap target of %.1f x %.1f mm" % (what, w * mm, h * mm))
    for w, h in getattr(r, "small", []):
        mins.setdefault("tap target %g x %g design px" % (round(w, 1), round(h, 1)), what)
    _, tf, body, row = S.metrics()
    for f in set(fonts):
        px, got = S.font_px(f), S.text_px(f)
        want = (S.TEXT_MM_ROW if px >= row else S.TEXT_MM_MIN)
        check(got * mm >= want - 1e-6, "%s: text %r is %.2f mm" % (what, f, got * mm))
        check(abs(got - max(S.text_px_at(px, tf, body, row, 0), want / mm)) < 1e-6, "%s: text %r drawn at %g px" % (what, f, got))
        if got > S.text_px_at(px, tf, body, row, 0) + 1e-6:
            mins.setdefault("text %s (%g px)" % (f, px), what)


# -- Default: nothing said = factor 2 on the panel, text 100 %
n, f0 = 0, fail
for t in THEMES:
    for wn, size in WINS.items():
        for pg in PAGES:
            a = data(draw(pg, t, size=size))
            r = draw(pg, t, size=size, fac=2.0, dens=PANEL, text=100)
            n += 1
            check(a == data(r), "Default on the panel differs from the plain render: %s %s %s" % (t, wn, pg))
            check(not r.small, "Default on the panel: a tap target under the minimum: %s %s %s %r" % (t, wn, pg, r.small))
            minimums(r, 2.0 / PANEL, "Default %s %s %s" % (t, wn, pg))
check(not mins, "Default on the panel: reached a minimum: %r" % (mins,))
print("ok   Default on a panel of %g px per mm: %d pages byte for byte the plain render, no minimum reached" % (PANEL, n) if fail == f0 else
      "FAIL Default")

# -- every step, portrait and landscape, every voice
n, f0 = 0, fail
for t in THEMES:
    th = A.THEMES[t]
    for on, px in (("portrait", PORTRAIT), ("landscape", LAND)):
        shown = {}
        for pct in S.STEPS:
            fac = S.factor(PANEL, pct)
            mm = S.mm_per_px(fac, PANEL)
            for pg in PAGES:
                what = "%d %% %s %s %s" % (pct, on, t, pg)
                r = draw(pg, t, px=px, fac=fac, dens=PANEL)
                n += 1
                L, mk = r.ui.L, r.markers
                check(abs(L.W - px[0] / fac) < 1e-6 and abs(L.H - px[1] / fac) < 1e-6, what + ": the layout is not for the window's design size")
                cx, cy, cw, ch = r.card
                check(abs(cx + cw + L.card_r - L.W) < 1e-6 and abs(cy + ch + L.m + 8 - L.H) < 1e-6, what + ": the card does not fill the window")
                for w in mk["widgets"]:
                    check(w["x"] >= cx and w["x"] + w["w"] <= cx + cw + 1e-6, what + ": a row outside the card: %r" % (w,))
                    if w["kind"] != "pager":
                        check(w["y"] + w["h"] <= cy + ch + 1, what + ": a row below the card: %r" % (w,))
                if "paged" in mk["fit"]:
                    shown.setdefault((pg, L.compact), {})[pct] = len([w for w in mk["widgets"] if w["kind"] != "pager"])
                minimums(r, mm, what)
                if fac != int(fac):
                    im = Img(r.surf)
                    has_rule(im, fac, cx + 2, cx + cw - 2, cy + L.head_h, th["headRule"], what + " header")
                    ink = rgb(th["ink"])
                    xs = [x for x in range(int(cx * fac) - 2, int(cx * fac) + 4) if im.px(x, int((cy + ch / 2) * fac)) == ink]
                    check(len(xs) >= 1, what + ": the card's edge has no device pixel of its colour")
                    for w in mk["widgets"]:
                        if w["kind"] in RULED and not r.ui.sheet and w["y"] + w["h"] < cy + ch - 2:
                            has_rule(im, fac, w["x"], w["x"] + w["w"], w["y"] + w["h"],
                                     th["sepSoft" if w["kind"] == "section-head" else "sep"], what + " " + w["kind"] + " " + w["label"])
        for (pg, cmp), by in shown.items():         # (the compact header holds the pager: compared among its own)
            got = [by.get(p) for p in S.STEPS if p in by]
            check(got == sorted(got, reverse=True), "%s %s %s: a smaller step shows fewer rows: %r" % (on, t, pg, by))
print("ok   %d pages at 80, 90, 100 and 120 %%: laid out for the design size, rules on whole device pixels" % n if fail == f0 else "FAIL steps")

# -- the text size
n, f0 = 0, fail
for t in THEMES:
    v = voices.voice(t)
    for pg in PAGES:
        base = draw(pg, t, dens=PANEL)
        hs = {(w["kind"], w["label"]): w["h"] for w in base.markers["widgets"]}
        for text in (125, 150):
            for pct in ((100, 80) if t == "white" else (100,)):
                fac = S.factor(PANEL, pct)
                r = draw(pg, t, px=PORTRAIT, fac=fac, dens=PANEL, text=text)
                n += 1
                what = "text %d %% at %d %% %s %s" % (text, pct, t, pg)
                minimums(r, S.mm_per_px(fac, PANEL), what)
                check(S.metrics()[1] == text / 100.0, what + ": the text size was not set")
                if pct == 100:
                    for w in r.markers["widgets"]:
                        h0 = hs.get((w["kind"], w["label"]))
                        if h0 is not None and w["kind"] not in ("pager", "themes", "curve"):
                            check(w["h"] >= h0 - 1e-6, what + ": a row lower than at 100 %%: %r (%g)" % (w, h0))
    b = S.body_px(v)
    for f in sorted({x for x in A.THEMES[t].values() if isinstance(x, str) and "px" in x and "'" in x}):
        try:
            px = S.font_px(f)
        except ValueError:
            continue
        S.set_voice_metrics(v, 0, 1.5)
        check(abs(S.text_px(f) - px * (1 + 0.5 * min(1, b / px))) < 1e-9, "text 150 %%: %s %r" % (t, f))
print("ok   %d pages at a text size of 125 and 150 %%" % n if fail == f0 else "FAIL text size")

# -- the minimums at every step, densities 3 to 25 px per mm
n, f0 = 0, fail
for dens in (3.0, S.FALLBACK_DENSITY, 4.6, 6.0, 8.7, PANEL, 13.0, 13.8, 25.0):
    for pct in S.STEPS:
        fac = S.factor(dens, pct)
        mm = S.mm_per_px(fac, dens)
        for pg in PAGES:
            for wn in ("full", "quarter"):
                r = draw(pg, "white", size=WINS[wn], fac=fac, dens=dens)
                n += 1
                minimums(r, mm, "%g px/mm %d %% %s %s" % (dens, pct, wn, pg))
print("ok   %d pages at 3 to 25 px per mm: no tap target under %g mm, no text under %g / %g mm" % (n, S.TAP_MM, S.TEXT_MM_ROW, S.TEXT_MM_MIN)
      if fail == f0 else "FAIL minimums")
os.makedirs(OUT, exist_ok=True)
with open(os.path.join(OUT, "size-minimums.txt"), "w") as fh:
    fh.write("What reaches a minimum of spec 012 FR-022 in Settings, and where first:\n")
    for k in sorted(mins):
        fh.write("%s    (%s)\n" % (k, mins[k]))
print("     %d elements reach a minimum somewhere: %s/size-minimums.txt" % (len(mins), OUT))
sys.exit(1 if fail else 0)
