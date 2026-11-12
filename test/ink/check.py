"""check.py APP_OUT FIFO_LOG APP_ERR: sway's ink reports (ink begin ... ink end blocks,
lines "ink S X Y W H APP" or "ink r K S X Y W H APP") against the canvas rects the app
printed (PHASE NAME WX WY X Y W H). For each phase there must be a report whose lines
are all the app's own areas (S 1) and whose union is exactly the canvas rect in output
coordinates (the window's place + the viewport's rect): in phase two the plain window,
whose empty area leaves it out of the report, adds nothing.
The module's own log must show an empty area for the plain window."""
import sys

out, fifo, err = (open(p).read() for p in sys.argv[1:4])
phases = {}
for line in out.splitlines():
    t = line.split()
    if t and t[0] == "PHASE":
        wx, wy, x, y, w, h = map(int, t[2:])
        phases[t[1]] = (wx + x, wy + y, w, h)

blocks, cur = [], None
for line in fifo.splitlines():
    t = line.split()
    if line == "ink begin":
        cur = []
    elif line == "ink end" and cur is not None:
        blocks.append(cur)
        cur = None
    elif cur is not None and t[:1] == ["ink"] and t[1] != "brush":
        if t[1] == "r":
            t = t[2:]           # "r K": a brush; the rest as without one
        else:
            t = t[1:]
        cur.append((int(t[0]), tuple(map(int, t[1:5]))))

fail = 0


def check(name, ok, detail=""):
    global fail
    print(("ok  " if ok else "FAIL"), "glassine:", name, detail)
    fail |= not ok


def area(rects):
    return sum(w * h for _, (x, y, w, h) in rects)


for name in ("one", "two"):
    want = phases.get(name)
    if not want:
        check(f"phase {name}: the app reported its canvas", False)
        continue
    x, y, w, h = want
    match = [b for b in blocks
             if b and all(s == 1 for s, _ in b) and area(b) == w * h
             and all(x <= r[0] and y <= r[1] and r[0] + r[2] <= x + w and r[1] + r[3] <= y + h for _, r in b)]
    check(f"phase {name}: sway reports the visible canvas {want} as the app's own area", bool(match),
          "" if match else f"(last reports: {blocks[-3:]})")
check("the canvas narrowed between the phases", phases.get("one") != phases.get("two"),
      f"{phases.get('one')} -> {phases.get('two')}")
check("the plain window: an empty area", 'areas "plain" none' in err)
sys.exit(fail)
