"""Appearance > Screen size (spec 011): the pixels per millimetre of a display whose physical
size the compositor doesn't know, measured once with a ruler and kept per display.

The file (README "Runtime interfaces"; the compositor reads it for the pen interface):
  ~/.config/glassine/output-density   one line per display: DENSITY OUTPUT-KEY
DENSITY: the pixels of the display's mode (not turned, not scaled) per millimetre, a decimal
number with a dot, 1 to 100; OUTPUT-KEY: the compositor's identifier, "MAKE MODEL SERIAL",
or the connector name when all three are unknown; to the end of the line. "#" lines are
comments; of two lines for one key the later counts. Points per millimetre (what the pen
interface uses) = DENSITY / the display's scale.
"""
import math
import os

PATH = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.join(os.path.expanduser("~"), ".config"),
                    "glassine", "output-density")
LO, HI = 1.0, 100.0
START = 96 / 25.4                       # the ruler before the first measurement: 96 pixels per inch
CARD_MM = 85.6                          # a bank card's long edge (ISO/IEC 7810 ID-1)
STEP = 0.01                             # longer / shorter: 1 %
UNKNOWN = ("", "Unknown")


def key(o):
    """an output of swaymsg -t get_outputs -> its key (sway's identifier, else its name)"""
    parts = [(o.get(k) or "Unknown").strip() or "Unknown" for k in ("make", "model", "serial")]
    if all(p in UNKNOWN for p in parts):
        return o.get("name") or ""
    return " ".join(parts)


def fmt(v):
    """3.7795 -> "3.78"; 12.5 -> "12.5" (at most three decimals in the file, two shown)"""
    return ("%.2f" % v).rstrip("0").rstrip(".")


def parse(s):
    """a typed density -> float in LO..HI, or None (digits and one "." or ",")"""
    s = (s or "").strip().replace(",", ".")
    if not s or s.count(".") > 1 or not s.replace(".", "").isdigit() or s == ".":
        return None
    v = float(s)
    return v if LO <= v <= HI else None


def read(path=PATH):
    """{key: density}; bad and out-of-range lines are skipped"""
    r = {}
    try:
        with open(path) as f:
            lines = f.read().splitlines()
    except OSError:
        return r
    for l in lines:
        if not l.strip() or l.lstrip().startswith("#"):
            continue
        v, _, k = l.strip().partition(" ")
        k = k.strip()
        try:
            d = float(v)
        except ValueError:
            continue
        if k and math.isfinite(d) and LO <= d <= HI:
            r[k] = d
    return r


def write(k, v, path=PATH):
    """the key's line set to v (None: removed); every other line kept; the file replaced whole"""
    try:
        with open(path) as f:
            lines = f.read().splitlines()
    except OSError:
        lines = []
    keep = [l for l in lines if l.lstrip().startswith("#") or not l.strip() or l.strip().partition(" ")[2].strip() != k]
    if v is not None:
        keep.append("%s %s" % (("%.3f" % v).rstrip("0").rstrip("."), k))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write("".join(l + "\n" for l in keep))
    os.replace(tmp, path)


def gdk_monitors():
    """the display server's monitors [{x, y, w_mm, h_mm}] (logical position), None: no display"""
    try:
        import gi
        gi.require_version("Gdk", "3.0")
        from gi.repository import Gdk
    except (ImportError, ValueError):
        return None
    d = Gdk.Display.get_default()
    if d is None:
        return None
    r = []
    for i in range(d.get_n_monitors()):
        m = d.get_monitor(i)
        g = m.get_geometry()
        r.append(dict(x=g.x, y=g.y, w_mm=m.get_width_mm(), h_mm=m.get_height_mm()))
    return r


def listing(outs, mons, density, here=None):
    """the displays Appearance lists: size unknown, or with a kept density (FR-001).
    outs: swaymsg -t get_outputs; mons: gdk_monitors() (None: sizes can't be told, none is
    unknown); here: the logical (x, y) of the display Settings is on"""
    r = []
    for o in outs or []:
        if not o.get("active", True):
            continue
        k = key(o)
        rect = o.get("rect") or {}
        at = (rect.get("x", 0), rect.get("y", 0))
        m = next((m for m in mons or [] if (m["x"], m["y"]) == at), None)
        unknown = m is not None and (m["w_mm"] <= 0 or m["h_mm"] <= 0)
        if not unknown and k not in density:
            continue
        make, model = (o.get("make") or ""), (o.get("model") or "")
        name = " ".join(p for p in (make, model) if p not in UNKNOWN) or o.get("name") or k
        r.append(dict(key=k, name=name, conn=o.get("name") or "", scale=float(o.get("scale") or 1) or 1.0,
                      here=here is not None and at == tuple(here), density=density.get(k)))
    return r


def ruler(density, scale, avail):
    """the ruler for a page avail logical px wide -> (mm, logical px per mm, is the card).
    A bank card when it fits, else the most whole centimetres that fit, at least 2 (FR-004)"""
    per = density / (scale or 1)
    if CARD_MM * per <= avail:
        return CARD_MM, per, True
    return max(2, int(avail / per // 10)) * 10, per, False


def step(v, d):
    """longer (+1) / shorter (-1) by 1 %, in LO..HI, three decimals"""
    return round(min(HI, max(LO, v * (1 + STEP * d))), 3)
