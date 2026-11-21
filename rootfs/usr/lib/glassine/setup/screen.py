"""The display the setup runs on and its interface size (shell spec 012 FR-016).

The setup draws in design pixels at that display's factor: the one the session applied
($XDG_RUNTIME_DIR/glassine/output-scale), else the one its density and interface size give
(glassine_ui.size). Its Look step sets the interface size of that display
(~/.config/glassine/ui-size of the setup's account, which the session applies at once; the
apply copies the choice into the new account's file)."""
import json
import subprocess

from glassine_ui import size as S

STEPS = (("Smaller", 80), ("Small", 90), ("Default", 100), ("Large", 120))
BUILTIN = ("eDP", "LVDS", "DSI", "EPD")       # connectors of a display that is part of the device


def key(o):
    """an output of swaymsg -t get_outputs -> its key (spec 011)"""
    return S.output_key(*[o.get(k) if o.get(k) not in (None, "", "Unknown") else "" for k in ("make", "model", "serial")],
                        name=o.get("name") or "")


def name(o):
    if (o.get("name") or "").startswith(BUILTIN):
        return "Built-in panel"
    p = [o.get(k) for k in ("make", "model") if o.get(k) not in (None, "", "Unknown")]
    return " ".join(p) or o.get("name") or key(o)


def mode_px(o):
    """the pixels of the output's mode, not turned, not scaled"""
    m = o.get("current_mode") or {}
    if (m.get("width") or 0) > 0 and (m.get("height") or 0) > 0:
        return int(m["width"]), int(m["height"])
    r, s = o.get("rect") or {}, float(o.get("scale") or 1) or 1.0
    w, h = (r.get("width") or 0) * s, (r.get("height") or 0) * s
    if str(o.get("transform") or "normal").endswith(("90", "270")):
        w, h = h, w
    return int(round(w)), int(round(h))


def outputs():
    """sway's active outputs ([] without sway)"""
    try:
        p = subprocess.run(("swaymsg", "-r", "-t", "get_outputs"), capture_output=True, text=True, timeout=3)
        outs = json.loads(p.stdout) if p.returncode == 0 else []
    except (OSError, subprocess.SubprocessError, ValueError):
        outs = []
    return [o for o in outs if isinstance(o, dict) and o.get("active", True)]


def display(outs, mons, at=None, applied=None, sizes=None, kept=None):
    """the display at the logical position at (else the focused one, else the first) as
    {key, name, at, percent, density, source, factor, applied}; None without outputs.
    mons: [{x, y, w_mm, h_mm}] of GDK; applied: read_output_scale(); sizes: read_ui_size();
    kept: read_output_density()"""
    if not outs:
        return None
    pos = lambda o: ((o.get("rect") or {}).get("x", 0), (o.get("rect") or {}).get("y", 0))
    o = next((o for o in outs if at is not None and pos(o) == tuple(at)), None) or \
        next((o for o in outs if o.get("focused")), outs[0])
    k = key(o)
    applied = applied if applied is not None else S.read_output_scale()
    sizes = sizes if sizes is not None else S.read_ui_size()
    kept = kept if kept is not None else S.read_output_density()
    px = mode_px(o)
    m = next((m for m in mons or [] if (m["x"], m["y"]) == pos(o)), None)
    w_mm, h_mm = (m["w_mm"], m["h_mm"]) if m else (0, 0)
    if w_mm > 0 and h_mm > 0 and (px[0] >= px[1]) != (w_mm >= h_mm):
        w_mm, h_mm = h_mm, w_mm                     # (the display server may give them turned)
    pct = sizes.get(k, 100)
    dens, src = S.density(px[0], px[1], w_mm, h_mm, kept.get(k))
    if k in applied:
        fac, dens, src = applied[k]
    else:
        fac = S.factor(dens, pct)
    return dict(key=k, name=name(o), at=pos(o), percent=pct, density=dens, source=src, factor=fac, applied=k in applied)


def gdk_monitors():
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


def step_mm(d, percent, voice, text=100):
    """a step's sizes on the display d, in millimetres on its glass: (the voice's row text,
    a standard button). voice: glassine_ui.voices.voice()"""
    fac = S.factor(d["density"], percent)
    mm = S.mm_per_px(fac, d["density"])
    row, body = S.row_px(voice), S.body_px(voice)
    tp = S.text_px_at(row, text / 100.0, body, row, mm)
    btn = max(48 + 1.3 * (tp - row), S.TAP_MM / mm if mm > 0 else 0)
    return tp * mm, btn * mm
