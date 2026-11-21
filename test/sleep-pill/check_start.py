"""check_start.py SCREEN TURN PAM START: START (glassine-sleep-pill -b, spec 010's start
picture) is SCREEN (the upright picture, P5/P6) turned back by TURN (clockwise, as the
panel daemon's frame and the sleep pill turn it), of the same kind (P5/P6), exactly,
everywhere outside PAM's box (the resume pill drawn alone with -W/-H of that frame and
-r TURN), and the notice changed pixels inside it, at the bottom of the person's view."""
import sys


def die(msg):
    sys.exit(f"check_start: {msg}")


def pnm(path):
    d = open(path, "rb").read()
    toks, pos = [], 0
    while len(toks) < 4:
        while d[pos:pos + 1].isspace():
            pos += 1
        if d[pos:pos + 1] == b"#":
            pos = d.index(b"\n", pos)
            continue
        e = pos
        while not d[e:e + 1].isspace():
            e += 1
        toks.append(d[pos:e].decode())
        pos = e
    pos += 1
    m, w, h = toks[0], int(toks[1]), int(toks[2])
    if m not in ("P5", "P6") or toks[3] != "255":
        die(f"{path}: {toks}")
    n = 1 if m == "P5" else 3
    px = d[pos:]
    if len(px) != w * h * n:
        die(f"{path}: {len(px)} bytes, want {w * h * n}")
    return m, w, h, n, px


def pam_box(path):
    d = open(path, "rb").read()
    hdr = d[:d.index(b"ENDHDR")].decode().split("\n")
    kv = dict(l.split(None, 1) for l in hdr[1:] if l and not l.startswith("#"))
    x, y = (int(v) for v in next(l for l in hdr if l.startswith("# at "))[5:].split())
    return x, y, int(kv["WIDTH"]), int(kv["HEIGHT"])


scr, turn, pam, start = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
m, LW, LH, n, sp = pnm(scr)
m2, W, H, n2, tp = pnm(start)
want = (LH, LW) if turn % 180 else (LW, LH)
if m2 != m or (W, H) != want:
    die(f"start {m2} {W}x{H}, want {m} {want[0]}x{want[1]}")
bx, by, bw, bh = pam_box(pam)
# the upright pixel each frame pixel shows
def src(rx, ry):
    if turn == 90:
        return ry, LH - 1 - rx
    if turn == 180:
        return LW - 1 - rx, LH - 1 - ry
    if turn == 270:
        return LW - 1 - ry, rx
    return rx, ry
changed = 0
for ry in range(H):
    inb_y = by <= ry < by + bh
    for rx in range(W):
        lx, ly = src(rx, ry)
        a = tp[(ry * W + rx) * n:(ry * W + rx + 1) * n]
        b = sp[(ly * LW + lx) * n:(ly * LW + lx + 1) * n]
        if inb_y and bx <= rx < bx + bw:
            changed += a != b
        elif a != b:
            die(f"frame px {rx},{ry} (upright {lx},{ly}) is {list(a)}, the screen's {list(b)}")
if changed < bw * bh // 20:
    die(f"only {changed} pixels changed in the notice's box {bw}x{bh}")
# the box at the bottom of the person's view: the upright place of its centre
cx, cy = src(bx + bw // 2, by + bh // 2)
if not (cy > LH * 3 // 4 and abs(cx - LW // 2) <= 2):
    die(f"the notice's centre is at {cx},{cy} upright in {LW}x{LH}: not bottom centre")
print(f"start picture {m} {W}x{H}, turn {turn}: the screen exact outside the notice ({changed} px changed in it)")
