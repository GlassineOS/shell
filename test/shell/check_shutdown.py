"""check_shutdown.py PPM [TEXT|-]: a screenshot of the shutdown screen (spec 009,
SC-001; grim -t ppm of the whole output, rotation 0): white corners; the mark of the
boot splash (41 % of the narrow side wide, centred, its centre at 44 % of the height: the
leaf's middle is the highlight grey, the sheet's left edge is ink); the line beneath it
(ink in the band under the mark, about centred) unless TEXT is "-"; nothing else."""
import sys

path = sys.argv[1]
text = len(sys.argv) < 3 or sys.argv[2] != "-"
data = open(path, "rb").read()


def die(msg):
    sys.exit(f"{path}: {msg}")


f = data.split(maxsplit=4)
if len(f) < 5 or f[0] != b"P6" or f[3] != b"255":
    die(f"not a P6 of maxval 255: {data[:20]!r}")
W, H = int(f[1]), int(f[2])
rgb = f[4]
if len(rgb) < W * H * 3:
    die(f"{len(rgb)} payload bytes, want {W * H * 3}")
px = bytes((rgb[i] * 77 + rgb[i + 1] * 150 + rgb[i + 2] * 29) >> 8 for i in range(0, W * H * 3, 3))
rot = 0
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
# nothing else: no dark pixel above the mark's view box or in the side margins
stray = [(u, v) for v in range(0, int(oy) - 4, 4) for u in range(0, vw, 4) if at(u, v) < 128]
if stray:
    die(f"ink above the mark (e.g. {stray[0]}): something else is on screen")
if len(dark) < 50:
    die(f"no line under the mark ({len(dark)} dark samples, turn {rot})")
mid = (min(u for u, _ in dark) + max(u for u, _ in dark)) / 2
if abs(mid - vw / 2) > 8 * S:
    die(f"the line is {mid - vw / 2:.0f} px off centre (turn {rot})")
