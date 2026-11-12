"""check.py PAM W H ROT: the file as glassine-epdd-desktop's pill_load accepts it
(P7, DEPTH 2, "# at X Y" before ENDHDR, inside the W x H screen, w*h*2 bytes),
premultiplied (grey <= alpha), and the crop at the bottom edge of the view turned by
ROT (0: bottom, 90: left, 180: top, 270: right; centred along that edge)."""
import sys

path, W, H, rot = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
data = open(path, "rb").read()


def die(msg):
    sys.exit(f"{path}: {msg}")


lines, pos = [], 0
while True:
    end = data.index(b"\n", pos)
    lines.append(data[pos:end].decode())
    pos = end + 1
    if lines[-1].startswith("ENDHDR"):
        break
if lines[0] != "P7":
    die("not P7")
hdr = {}
x = y = -1
for line in lines[1:-1]:
    if line.startswith("# at "):
        x, y = (int(v) for v in line[5:].split())
    else:
        k, v = line.split(None, 1)
        hdr[k] = v
w, h = int(hdr.get("WIDTH", 0)), int(hdr.get("HEIGHT", 0))
if hdr.get("DEPTH") != "2" or hdr.get("MAXVAL") != "255" or hdr.get("TUPLTYPE") != "GRAYSCALE_ALPHA":
    die(f"header {hdr}")
if not (w > 0 and h > 0 and x >= 0 and y >= 0 and x + w <= W and y + h <= H):
    die(f"box {x},{y} {w}x{h} outside {W}x{H}")
px = data[pos:]
if len(px) != w * h * 2:
    die(f"{len(px)} payload bytes, want {w * h * 2}")
if any(px[i] > px[i + 1] for i in range(0, len(px), 2)):
    die("not premultiplied (grey > alpha)")
# the crop is tight: something drawn on each of its edges
a = px[1::2]
if not (any(a[:w]) and any(a[(h - 1) * w:]) and any(a[::w]) and any(a[w - 1::w])):
    die("the crop is not tight")
long_side, short_side = (w, h) if rot in (0, 180) else (h, w)
if short_side > long_side or short_side > 400:
    die(f"{w}x{h} is not a pill")
# its distance from the edge it sits at, and centred along that edge
gap = {0: H - (y + h), 90: x, 180: y, 270: W - (x + w)}[rot]
centre_off = abs((x + w / 2) - W / 2) if rot in (0, 180) else abs((y + h / 2) - H / 2)
scale = max(1, round(min(W, H) / 702))
if not (30 * scale <= gap <= 50 * scale):
    die(f"{gap} px from its edge (turn {rot})")
if centre_off > 2:
    die(f"{centre_off} px off centre (turn {rot})")
