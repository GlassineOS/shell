"""check_screen.py PGM W H ROT [TEXT|-]: the shutdown screen (-m screen, spec 009) as
glassine-epdd-desktop's "shutdown-screen" reads it: a binary 8-bit P5 of the whole W x H
logical screen; white corners; the mark of the boot splash in the view turned by ROT
(41 % of the view's narrow side wide, centred, its centre at 44 % of the height: the
leaf's middle is the highlight grey, the sheet's left edge is ink); the line beneath it
(ink in the band under the mark, about centred) unless TEXT is "-"."""
import sys

path, W, H, rot = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
text = len(sys.argv) < 6 or sys.argv[5] != "-"
data = open(path, "rb").read()


def die(msg):
    sys.exit(f"{path}: {msg}")


head = f"P5\n{W} {H}\n255\n".encode()
if not data.startswith(head):
    die(f"header {data[:20]!r}, want {head!r}")
px = data[len(head):]
if len(px) != W * H:
    die(f"{len(px)} payload bytes, want {W * H}")
vw, vh = (W, H) if rot in (0, 180) else (H, W)


def at(u, v):
    """the grey at view point (u, v): the view turned ROT clockwise onto the screen"""
    u, v = int(u), int(v)
    x, y = {0: (u, v), 90: (W - 1 - v, u), 180: (W - 1 - u, H - 1 - v), 270: (v, H - 1 - u)}[rot]
    return px[y * W + x]


for u, v in ((2, 2), (vw - 3, 2), (2, vh - 3), (vw - 3, vh - 3)):
    if at(u, v) != 255:
        die(f"view corner {u},{v} is {at(u, v)}, not white (turn {rot})")
unit = min(vw, vh) * 0.41 / 48
ox, oy = (vw - 48 * unit) / 2, vh * 0.44 - 24 * unit
leaf = at(ox + 29 * unit, oy + 20 * unit)
if abs(leaf - 208) > 6:
    die(f"the leaf's middle is {leaf}, not the highlight grey (turn {rot})")
edge = min(at(ox + 2 * unit + d, oy + 21 * unit) for d in range(-3, 4))
if edge > 60:
    die(f"no sheet outline at its left edge ({edge}, turn {rot})")
S = max(1, round(min(W, H) / 702))
y0, y1 = int(oy + 50 * unit), int(min(vh - 1, oy + 50 * unit + (40 + 60) * S))
dark = [(u, v) for v in range(y0, y1, 2) for u in range(0, vw, 2) if at(u, v) < 128]
if not text:
    if dark:
        die(f"ink under the mark without a line (turn {rot})")
    sys.exit(0)
if len(dark) < 50:
    die(f"no line under the mark ({len(dark)} dark samples, turn {rot})")
mid = (min(u for u, _ in dark) + max(u for u, _ in dark)) / 2
if abs(mid - vw / 2) > 8 * S:
    die(f"the line is {mid - vw / 2:.0f} px off centre (turn {rot})")
