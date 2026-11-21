"""check.py DIR SCALE...: the renders of each interface size step (run.sh, SIDE=steps).
Per orientation and factor, from the shell's log (its factor, logical size and millimetres
per design px) and the hit dumps (every target of the bar and the popups, logical px):
- the factor the shell draws at is the output's scale, its layout the output's logical size;
- every target lies inside that logical size, and the bar's reach its right end (FR-020:
  no layout of a fixed width);
- every target takes taps on at least 7 mm both ways on the glass (FR-022; the .tap dumps);
  the controls drawn smaller than that (the design's size, their tap area grown) are listed
  (the dumps' 0.1 px: 0.01 mm of tolerance);
- text (FR-022, FR-024b): the smallest styles at least 2.0 mm, running text and rows 2.8 mm.
exit 1 on a failed check."""
import glob
import os
import re
import sys

d, scales = sys.argv[1], sys.argv[2:]
fail = 0


def ok(m):
    print("ok   " + m)


def bad(m):
    global fail
    print("FAIL " + m)
    fail = 1


small = {}
tiny = set()
for o in ("portrait", "landscape"):
    for f in scales:
        log = os.path.join(d, f"shell-{o}-{f}.log")
        if not os.path.exists(log):
            continue
        t = open(log, encoding="utf-8", errors="replace").read()
        m = re.search(r"logical (\d+)x(\d+), factor ([0-9.]+)", t)
        mm = re.search(r"size: factor [0-9.]+, ([0-9.]+) mm per design px", t)
        if not m or not mm:
            bad(f"{o} {f}: no factor or size in the shell's log")
            continue
        lw, lh, fac, mpp = int(m[1]), int(m[2]), float(m[3]), float(mm[1])
        if abs(fac - float(f)) < 1 / 240:
            ok(f"{o} {f}: the shell draws at the output's scale ({fac:.4f}), logical {lw}x{lh}, {mpp:.4f} mm per design px")
        else:
            bad(f"{o} {f}: factor {fac}, the output's {f}")
        tm = re.search(r"size: text mm: 11 px ([0-9.]+), 13 px ([0-9.]+), body \d+ px ([0-9.]+), rows \d+ px ([0-9.]+)", t)
        if not tm:
            bad(f"{o} {f}: no text sizes in the shell's log")
        else:
            a11, a13, body, rows = map(float, tm.groups())
            if min(a11, a13) >= 2.0 - 0.005 and min(body, rows) >= 2.8 - 0.005:
                ok(f"{o} {f}: text at least 2.0 mm, rows 2.8 mm (11 px {a11:.2f}, 13 px {a13:.2f}, body {body:.2f}, rows {rows:.2f} mm)")
            else:
                bad(f"{o} {f}: text under its minimum: 11 px {a11}, 13 px {a13}, body {body}, rows {rows} mm")
        outside, reach = [], 0
        for p in sorted(glob.glob(os.path.join(d, "hits", f"{o}-{f}", "*.bar")) + glob.glob(os.path.join(d, "hits", f"{o}-{f}", "*.pop"))):
            what = os.path.basename(p)
            for ln in open(p + ".tap"):
                a = ln.split()
                if len(a) == 7 and a[0] != "act1" and min(map(float, a[5:7])) * mpp < 7 - 0.01:
                    tiny.add(f"{o} {f} {what}:{a[0]} taps on {float(a[5]):.1f}x{float(a[6]):.1f} px")
            for ln in open(p):
                a = ln.split()
                if len(a) != 7:
                    continue
                x, y, w, h = map(float, a[3:])
                if x < -0.5 or y < -0.5 or x + w > lw + 0.5 or y + h > lh + 0.5:
                    outside.append(f"{what}:{a[0]}")
                if what.endswith(".bar"):
                    reach = max(reach, x + w)
                if min(w, h) * mpp < 7 - 1e-6 and a[0] not in ("act1",):
                    small.setdefault((o, f), set()).add(f"{a[0]} {w:.0f}x{h:.0f} px = {w * mpp:.1f}x{h * mpp:.1f} mm")
        if outside:
            bad(f"{o} {f}: targets outside the logical size: {', '.join(sorted(set(outside))[:6])}")
        else:
            ok(f"{o} {f}: every target inside {lw}x{lh}")
        if reach >= lw - 60:
            ok(f"{o} {f}: the bar reaches the right end ({reach:.0f} of {lw})")
        else:
            bad(f"{o} {f}: the bar ends at {reach:.0f}, the output is {lw} wide")
if tiny:
    bad("targets taking taps on less than 7 mm: " + "; ".join(sorted(tiny)[:8]))
else:
    ok("every target takes taps on at least 7 mm both ways, at every step")
for (o, f), s in sorted(small.items()):
    print(f"note {o} {f}: drawn under 7 mm one way (taps on 7 mm): " + "; ".join(sorted(s)))
sys.exit(fail)
