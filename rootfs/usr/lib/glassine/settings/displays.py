"""The session's displays and their interface size (spec 012): each output's name, key,
density and factor, for Appearance > Size and text and for the scale Settings draws at.

The files (README "Runtime interfaces"; glassine_ui.size reads and writes them):
  ~/.config/glassine/ui-size              PERCENT OUTPUT-KEY: a display's interface size (written here)
  ~/.config/glassine/text-size            PERCENT: the text size, 100 to 150 (written here)
  $XDG_RUNTIME_DIR/glassine/output-scale  FACTOR DENSITY SOURCE OUTPUT-KEY: what the session
                                          applied to each display (read here)
A display without a line in output-scale gets its density from the size the display server
reports for it (when plausible), else the density kept for it (spec 011), else the fallback.
"""
from glassine_ui import size as S

from . import outputs

BUILTIN = ("eDP", "LVDS", "DSI", "EPD")           # connectors of a display that is part of the device
STEPS = (("Smaller", 80), ("Small", 90), ("Default", 100), ("Large", 120))
TEXT_LO, TEXT_HI, TEXT_STEP = 100, 150, 5


def builtin(o):
    return (o.get("name") or "").startswith(BUILTIN)


def name(o):
    """what a display is called: the built-in one by what it is, another by its make and model"""
    if builtin(o):
        return "Built-in panel"
    make, model = (o.get("make") or ""), (o.get("model") or "")
    return " ".join(p for p in (make, model) if p not in outputs.UNKNOWN) or o.get("name") or outputs.key(o)


def mode_px(o):
    """the pixels of the output's mode, not turned, not scaled"""
    m = o.get("current_mode") or {}
    w, h = m.get("width") or 0, m.get("height") or 0
    if w > 0 and h > 0:
        return int(w), int(h)
    r, s = o.get("rect") or {}, float(o.get("scale") or 1) or 1.0
    w, h = (r.get("width") or 0) * s, (r.get("height") or 0) * s
    if str(o.get("transform") or "normal").endswith(("90", "270")):
        w, h = h, w
    return int(round(w)), int(round(h))


def mode_mm(px, w_mm, h_mm):
    """the display server's millimetres the way up the mode is (it may give them turned
    with the output)"""
    if w_mm > 0 and h_mm > 0 and (px[0] >= px[1]) != (w_mm >= h_mm):
        return h_mm, w_mm
    return w_mm, h_mm


def listing(outs, mons, kept, applied, sizes, here=None):
    """the active outputs of swaymsg -t get_outputs as displays [{key, name, conn, at, here,
    percent, density, source, factor, applied, unknown}]. mons: outputs.gdk_monitors() (None:
    no sizes); kept: the kept densities; applied: the session's output-scale; sizes: ui-size;
    here: the logical (x, y) of the display Settings is on. A display with a line in
    output-scale has that line's factor and density (applied); another has the density of
    spec 012 FR-005 and the factor its interface size gives. The display Settings is on
    comes first."""
    r = []
    for o in outs or []:
        if not o.get("active", True):
            continue
        k = outputs.key(o)
        rect = o.get("rect") or {}
        at = (rect.get("x", 0), rect.get("y", 0))
        pct = sizes.get(k, 100)
        px = mode_px(o)
        m = next((m for m in mons or [] if (m["x"], m["y"]) == at), None)
        w_mm, h_mm = mode_mm(px, m["w_mm"], m["h_mm"]) if m else (0, 0)
        dens, src = S.density(px[0], px[1], w_mm, h_mm, kept.get(k))
        if k in applied:
            fac, dens, src = applied[k]
        else:
            fac = S.factor(dens, pct)
        r.append(dict(key=k, name=name(o), conn=o.get("name") or "", at=at, here=here is not None and at == tuple(here),
                      percent=pct, density=dens, source=src, factor=fac, applied=k in applied,
                      unknown=m is not None and not S.plausible(px[0], px[1], w_mm, h_mm)))
    return sorted(r, key=lambda d: not d["here"])


def dens_text(d, cap=False):
    """"8.94 px per mm, reported" / "4.25 px per mm, measured" / "not measured" """
    if d["source"] == "fallback":
        return "Not measured" if cap else "not measured"
    return "%s px per mm, %s" % (outputs.fmt(d["density"]), d["source"])


def row_text(d):
    """a display's line in the Display row"""
    parts = ["Settings is here"] if d["here"] else []
    if d["source"] == "fallback":
        parts.append(("not measured" if parts else "Not measured") + " · drawn at one pixel per design pixel")
    else:
        parts.append(dens_text(d))
    return " · ".join(parts)


def step_mm(d, percent, voice, text):
    """the sizes of a step on a display, in millimetres on its glass: (the voice's row text,
    a standard button), at the text size text (percent). voice: glassine_ui.voices.voice()"""
    fac = S.factor(d["density"], percent)
    mm = S.mm_per_px(fac, d["density"])
    row, body = S.row_px(voice), S.body_px(voice)
    tp = S.text_px_at(row, text / 100.0, body, row, mm)
    btn = max(48 + 1.3 * (tp - row), S.TAP_MM / mm if mm > 0 else 0)
    return tp * mm, btn * mm


def text_pos(percent):
    """the text size as the slider's 0..100"""
    return (percent - TEXT_LO) * 100 // (TEXT_HI - TEXT_LO)


def text_at(pos):
    """the slider's 0..100 as a text size: 100 to 150 in steps of 5"""
    p = TEXT_LO + round(pos * (TEXT_HI - TEXT_LO) / 100 / TEXT_STEP) * TEXT_STEP
    return max(TEXT_LO, min(TEXT_HI, p))
