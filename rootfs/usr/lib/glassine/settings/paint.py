"""Drawing for glassine-settings: the design's CSS values on cairo.

Everything the design uses is drawn with hard edges (no blur), so the CSS
values are exact recipes:
  box(cr, x, y, w, h, radius, bg, shadow, border)   an element with its
      background, box-shadows (outer below, inset above the background) and
      border, in the CSS order
  rule(cr, x, y, w, border)                          a border-bottom line
  text(...)                                          a CSS font shorthand on Pango
  icon(...)                                          an icon-theme icon in one colour
Units are logical px (the panel is 2 device px per logical px).
"""
import math
import re

import cairo
import gi
gi.require_version("Pango", "1.0")
gi.require_version("PangoCairo", "1.0")
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk, Pango, PangoCairo  # noqa: E402

# ---- values ---------------------------------------------------------------------


def color(s):
    """'#rrggbb' | '#rgb' | 'rgba(r,g,b,a)' | 'transparent' -> (r, g, b, a) or None"""
    s = (s or "").strip()
    if not s or s in ("none", "transparent"):
        return None
    if s.startswith("#"):
        h = s[1:]
        if len(h) == 3:
            h = "".join(c * 2 for c in h)
        a = int(h[6:8], 16) / 255 if len(h) == 8 else 1.0
        return (int(h[0:2], 16) / 255, int(h[2:4], 16) / 255, int(h[4:6], 16) / 255, a)
    m = re.match(r"rgba?\(([^)]*)\)", s)
    if m:
        p = [float(v) for v in m.group(1).split(",")]
        return (p[0] / 255, p[1] / 255, p[2] / 255, p[3] if len(p) > 3 else 1.0)
    return None


def px(s, ref=0.0):
    """'12px' | '50%' (of ref) | '0' -> float"""
    s = str(s).strip()
    if s.endswith("%"):
        return float(s[:-1]) / 100 * ref
    return float(s[:-2]) if s.endswith("px") else float(s or 0)


def radii(s, w, h):
    """CSS border-radius (1 or 4 values) -> [tl, tr, br, bl], clamped"""
    v = [px(p, min(w, h)) for p in str(s or "0").split()]
    v = (v * 4)[:4] if len(v) == 1 else (v + v[:2])[:4] if len(v) == 2 else (v + [v[1]])[:4] if len(v) == 3 else v[:4]
    lim = min(w, h) / 2
    return [min(r, lim) for r in v]


_SPLIT = re.compile(r",(?![^(]*\))")


def shadows(s):
    """box-shadow list -> [(inset, x, y, spread, rgba)] in CSS order (first on top)"""
    out = []
    for part in _SPLIT.split(s or ""):
        part = part.strip()
        if not part or part == "none":
            continue
        inset = part.startswith("inset")
        if inset:
            part = part[5:].strip()
        m = re.match(r"(.*?)\s*((?:#|rgba?\().*)$", part)
        nums, col = (m.group(1), m.group(2)) if m else (part, "#000")
        n = [px(v) for v in nums.split()]
        n += [0] * (4 - len(n))
        out.append((inset, n[0], n[1], n[3], color(col)))
    return out


def border(s):
    """'1.5px solid #111' -> (width, style, rgba) or None"""
    s = (s or "").strip()
    if not s or s == "none":
        return None
    w, style, col = 1.0, "solid", "#000"
    for p in re.split(r"\s+(?![^(]*\))", s):
        if p.endswith("px") or re.match(r"^[\d.]+$", p):
            w = px(p)
        elif p in ("solid", "dashed", "dotted", "double"):
            style = p
        else:
            col = p
    return (w, style, color(col))


_FONT = re.compile(r"^(?:(italic)\s+)?(?:(\d{3})\s+)?([\d.]+)px(?:/([\d.]+(?:px)?))?\s+(.+)$")
_fonts = {}


def font(s, weight=None):
    """CSS font shorthand ("italic 500 22px/1.1 'EB Garamond',serif") ->
    (Pango.FontDescription, line-height factor or None)"""
    key = (s, weight)
    if key in _fonts:
        return _fonts[key]
    m = _FONT.match(s.strip())
    italic, w, size, lh, fam = m.groups() if m else (None, None, "15", None, "sans-serif")
    fam = fam.split(",")[0].strip().strip("'\"")
    d = Pango.FontDescription()
    d.set_family(fam)
    d.set_absolute_size(float(size) * Pango.SCALE)
    d.set_weight(int(weight or w or 400))
    if italic:
        d.set_style(Pango.Style.ITALIC)
    if lh and lh.endswith("px"):                  # a line-height in px ("13px/21px") -> a factor
        lh = float(lh[:-2]) / float(size)
    _fonts[key] = (d, float(lh) if lh else None)
    return _fonts[key]


# ---- shapes -----------------------------------------------------------------------


def rrect(cr, x, y, w, h, r):
    """a rounded rectangle path; r = [tl, tr, br, bl] or a number"""
    if not isinstance(r, (list, tuple)):
        r = [r] * 4
    tl, tr, br, bl = [max(0.0, min(v, w / 2, h / 2)) if w > 0 and h > 0 else 0 for v in r]
    cr.new_sub_path()
    cr.move_to(x + tl, y)
    cr.line_to(x + w - tr, y)
    if tr: cr.arc(x + w - tr, y + tr, tr, -math.pi / 2, 0)
    cr.line_to(x + w, y + h - br)
    if br: cr.arc(x + w - br, y + h - br, br, 0, math.pi / 2)
    cr.line_to(x + bl, y + h)
    if bl: cr.arc(x + bl, y + h - bl, bl, math.pi / 2, math.pi)
    cr.line_to(x, y + tl)
    if tl: cr.arc(x + tl, y + tl, tl, math.pi, 1.5 * math.pi)
    cr.close_path()


def _grow(r, d):
    return [max(0.0, v + d) if v > 0 else 0.0 for v in r]


def box(cr, x, y, w, h, radius="0", bg=None, shadow=None, bd=None):
    """a CSS element: outer shadows, background, inset shadows, border"""
    r = radii(radius, w, h) if not isinstance(radius, list) else radius
    sh = shadows(shadow)
    for inset, dx, dy, sp, col in reversed(sh):          # outer: below, last first
        if inset or not col:
            continue
        cr.set_source_rgba(*col)
        rrect(cr, x + dx - sp, y + dy - sp, w + 2 * sp, h + 2 * sp, _grow(r, sp))
        cr.fill()
    c = color(bg) if isinstance(bg, str) else bg
    if c:
        cr.set_source_rgba(*c)
        rrect(cr, x, y, w, h, r)
        cr.fill()
    ins = [s for s in sh if s[0] and s[4]]
    if ins:
        cr.save()
        rrect(cr, x, y, w, h, r)
        cr.clip()
        for _, dx, dy, sp, col in reversed(ins):
            cr.set_source_rgba(*col)
            cr.set_fill_rule(cairo.FILL_RULE_EVEN_ODD)
            rrect(cr, x - 50, y - 50, w + 100, h + 100, 0)
            rrect(cr, x + dx + sp, y + dy + sp, w - 2 * sp, h - 2 * sp, _grow(r, -sp))
            cr.fill()
        cr.restore()
        cr.set_fill_rule(cairo.FILL_RULE_WINDING)
    b = border(bd) if isinstance(bd, str) else bd
    if b and b[2]:
        bw, style, col = b
        cr.set_source_rgba(*col)
        if style == "double":
            t = max(1.0, round(bw / 3))
            for inset in (0, bw - t):
                cr.set_line_width(t)
                rrect(cr, x + inset + t / 2, y + inset + t / 2, w - 2 * inset - t, h - 2 * inset - t, _grow(r, -inset - t / 2))
                cr.stroke()
        else:
            cr.set_line_width(bw)
            _dash(cr, style, bw)
            rrect(cr, x + bw / 2, y + bw / 2, w - bw, h - bw, _grow(r, -bw / 2))
            cr.stroke()
            cr.set_dash([])


def _dash(cr, style, w):
    if style == "dashed":
        cr.set_dash([3 * w, 3 * w])
    elif style == "dotted":
        cr.set_line_cap(cairo.LINE_CAP_ROUND)
        cr.set_dash([0, 2 * w + 1])
    else:
        cr.set_line_cap(cairo.LINE_CAP_BUTT)
        cr.set_dash([])


def rule(cr, x, y, w, bd):
    """a border-bottom whose lower edge is at y (the element's bottom)"""
    b = border(bd) if isinstance(bd, str) else bd
    if not b or not b[2]:
        return
    bw, style, col = b
    cr.set_source_rgba(*col)
    if style == "double":
        t = max(1.0, round(bw / 3))
        cr.rectangle(x, y - bw, w, t)
        cr.rectangle(x, y - t, w, t)
        cr.fill()
        return
    if style == "solid":
        cr.rectangle(x, y - bw, w, bw)
        cr.fill()
        return
    cr.set_line_width(bw)
    _dash(cr, style, bw)
    cr.move_to(x + (bw / 2 if style == "dotted" else 0), y - bw / 2)
    cr.line_to(x + w, y - bw / 2)
    cr.stroke()
    cr.set_dash([])
    cr.set_line_cap(cairo.LINE_CAP_BUTT)


def fill(cr, x, y, w, h, col):
    c = color(col) if isinstance(col, str) else col
    if c:
        cr.set_source_rgba(*c)
        cr.rectangle(x, y, w, h)
        cr.fill()


# ---- text -------------------------------------------------------------------------


def layout(cr, s, f, weight=None, caps=None, maxw=None, wrap=False):
    """a Pango layout for s in CSS font f; caps = (variant, case) of the design
    ('small-caps', 'lowercase') -> lower-cased small capitals; 'all-small-caps' turns
    capitals into small capitals too"""
    desc, lh = font(f, weight)
    lo = PangoCairo.create_layout(cr)
    lo.set_font_description(desc)
    variant, case = caps or ("normal", "none")
    if case == "uppercase":
        s = s.upper()
    elif case == "lowercase":
        s = s.lower()
    lo.set_text(s, -1)
    # lining figures: the web fonts of the design have them by default,
    # Alpine's EB Garamond has old-style ones ("K38o")
    al = Pango.AttrList()
    feat = {"small-caps": "lnum, smcp", "all-small-caps": "lnum, smcp, c2sc"}.get(variant, "lnum")
    al.insert(Pango.attr_font_features_new(feat))
    lo.set_attributes(al)
    if maxw is not None:
        lo.set_width(int(max(1, maxw) * Pango.SCALE))
        if wrap:
            lo.set_wrap(Pango.WrapMode.WORD_CHAR)
        else:
            lo.set_ellipsize(Pango.EllipsizeMode.END)
    return lo


def size(lo):
    _, log = lo.get_pixel_extents()
    return log.width, log.height


def baseline(lo):
    return lo.get_baseline() / Pango.SCALE


def _lh_lines(lo, f, weight, lh):
    """a CSS line-height on a layout: [(line, x offset, width, top of its baseline
    from the box top)], box height. Each line box is lh x the font size, the glyphs
    centred in it (half-leading), as a browser lays out the design's text."""
    desc, _ = font(f, weight)
    pitch = lh * desc.get_size() / Pango.SCALE
    out, it, i = [], lo.get_iter(), 0
    while True:
        _, log = it.get_line_extents()
        top, h = log.y / Pango.SCALE, log.height / Pango.SCALE
        base = it.get_baseline() / Pango.SCALE
        out.append((it.get_line_readonly(), log.x / Pango.SCALE, log.width / Pango.SCALE,
                    i * pitch + (pitch - h) / 2 + (base - top)))
        i += 1
        if not it.next_line():
            break
    return out, i * pitch


def text(cr, s, f, x, y, col, weight=None, caps=None, align=0, maxw=None, wrap=False, ls=0.0, lh=None):
    """draws s with its top at y; align 0 left of x, 1 centred on x, 2 right of x
    (wrapped lines each on their own). ls: letter spacing in em; lh: a CSS
    line-height factor (None: the font's own). -> (width, height)"""
    lo = layout(cr, s, f, weight, caps, maxw, wrap)
    if ls:
        desc, _ = font(f, weight)
        al = lo.get_attributes() or Pango.AttrList()
        al.insert(Pango.attr_letter_spacing_new(int(ls * desc.get_size())))
        lo.set_attributes(al)
    c = color(col) if isinstance(col, str) else col
    cr.set_source_rgba(*(c or (0, 0, 0, 1)))
    lh = lh or font(f, weight)[1]
    if lh:
        lines, h = _lh_lines(lo, f, weight, lh)
        w = max(lw for _, _, lw, _ in lines)
        for line, lx, lw, base in lines:
            cr.move_to(x - (lw / 2 if align == 1 else lw if align == 2 else 0), y + base)
            PangoCairo.show_layout_line(cr, line)
        return w, h
    w, h = size(lo)
    if maxw is not None and not wrap:
        w = min(w, maxw)
    ox = x - (w / 2 if align == 1 else w if align == 2 else 0)
    cr.move_to(ox, y)
    PangoCairo.show_layout(cr, lo)
    return w, h


def measure(cr, s, f, weight=None, caps=None, maxw=None, wrap=False, lh=None):
    lo = layout(cr, s, f, weight, caps, maxw, wrap)
    lh = lh or font(f, weight)[1]
    if lh:
        lines, h = _lh_lines(lo, f, weight, lh)
        w = max(lw for _, _, lw, _ in lines)
        return (min(w, maxw) if maxw is not None and not wrap else w), h
    return size(lo)


def ls_em(v):
    """'.08em' -> 0.08"""
    v = str(v or "0").strip()
    return float(v[:-2]) if v.endswith("em") else 0.0


# ---- icons ------------------------------------------------------------------------
# the design's Material Symbols names -> icon-theme names (glassine-mono first, then
# Adwaita's symbolic set), as the shell draws its icons
ICONS = {
    "wifi": ["network-wireless-signal-excellent-symbolic"],
    "wifi_off": ["network-wireless-offline-symbolic", "network-wireless-disabled-symbolic"],
    "network_wifi_1_bar": ["network-wireless-signal-weak-symbolic"],
    "network_wifi_2_bar": ["network-wireless-signal-ok-symbolic"],
    "network_wifi_3_bar": ["network-wireless-signal-good-symbolic"],
    "signal_wifi_4_bar": ["network-wireless-signal-excellent-symbolic"],
    "bluetooth": ["bluetooth-symbolic", "bluetooth-active-symbolic"],
    "volume_up": ["audio-volume-high-symbolic"],
    "volume_off": ["audio-volume-muted-symbolic"],
    "light_mode": ["display-brightness-symbolic", "weather-clear-symbolic"],
    "contrast": ["preferences-desktop-display-symbolic", "video-display-symbolic", "display-brightness-symbolic"],
    "battery_5_bar": ["battery-good-symbolic", "battery-level-70-symbolic"],
    "keyboard": ["input-keyboard-symbolic"],
    "palette": ["applications-graphics-symbolic", "preferences-desktop-appearance-symbolic", "color-select-symbolic"],
    "schedule": ["preferences-system-time-symbolic", "alarm-symbolic"],
    "hard_drive": ["drive-harddisk-symbolic"],
    "info": ["help-about-symbolic", "dialog-information-symbolic"],
    "chevron_left": ["go-previous-symbolic", "pan-start-symbolic"],
    "chevron_right": ["go-next-symbolic", "pan-end-symbolic"],
    "refresh": ["view-refresh-symbolic"],
    "add": ["list-add-symbolic"],
    "remove": ["list-remove-symbolic"],
    "lock": ["changes-prevent-symbolic", "system-lock-screen-symbolic"],
    "delete": ["user-trash-symbolic", "edit-delete-symbolic"],
    "headphones": ["audio-headphones-symbolic"],
    "headset": ["audio-headset-symbolic", "audio-headphones-symbolic"],
    "speaker": ["audio-speakers-symbolic"],
    "smartphone": ["phone-symbolic", "phone-apple-iphone-symbolic"],
    "draw": ["input-tablet-symbolic"],
    "mouse": ["input-mouse-symbolic"],
    "gamepad": ["input-gaming-symbolic"],
    "computer": ["computer-symbolic"],
    "devices": ["bluetooth-symbolic"],
    "close": ["window-close-symbolic"],
    "expand_more": ["pan-down-symbolic", "go-down-symbolic"],
    "expand_less": ["pan-up-symbolic", "go-up-symbolic"],
    "unfold_more": ["view-more-symbolic", "pan-down-symbolic"],
    # the More tab, Packages (t14-t18)
    "more_vert": ["view-more-symbolic", "open-menu-symbolic"],
    "deployed_code": ["package-x-generic-symbolic", "system-software-install-symbolic"],
    "search": ["system-search-symbolic", "edit-find-symbolic"],
    "check": ["object-select-symbolic", "emblem-ok-symbolic"],
    "check_circle": ["object-select-symbolic", "emblem-ok-symbolic"],
    "pending": ["view-more-horizontal-symbolic", "content-loading-symbolic"],
    "error": ["dialog-error-symbolic"],
    "warning": ["dialog-warning-symbolic"],
    "download": ["folder-download-symbolic", "document-save-symbolic"],
    "restart_alt": ["view-refresh-symbolic", "system-reboot-symbolic"],
    "update": ["software-update-available-symbolic", "view-refresh-symbolic"],
    "history": ["document-open-recent-symbolic"],
    "backspace": ["edit-clear-symbolic"],
    "keyboard_hide": ["input-keyboard-symbolic"],
    # Apps: the app rows' category icons, the Responsiveness zones
    "apps": ["view-app-grid-symbolic", "application-x-executable-symbolic"],
    "public": ["web-browser-symbolic", "applications-internet-symbolic"],
    "terminal": ["utilities-terminal-symbolic"],
    "mail": ["mail-unread-symbolic", "mail-send-symbolic"],
    "calendar_month": ["x-office-calendar-symbolic"],
    "folder": ["folder-symbolic"],
    "table_chart": ["x-office-spreadsheet-symbolic"],
    "description": ["x-office-document-symbolic"],
    "nights_stay": ["weather-clear-night-symbolic"],
    "language": ["globe-symbolic", "applications-internet-symbolic", "web-browser-symbolic"],
    "map": ["find-location-symbolic", "mark-location-symbolic"],
    "terrain": ["find-location-symbolic", "mark-location-symbolic"],
    "chess": ["applications-games-symbolic", "input-gaming-symbolic"],
    "sports_esports": ["applications-games-symbolic", "input-gaming-symbolic"],
    "school": ["accessories-dictionary-symbolic", "applications-science-symbolic"],
    "science": ["applications-science-symbolic"],
    "menu_book": ["accessories-dictionary-symbolic", "x-office-document-symbolic"],
    "music_note": ["audio-x-generic-symbolic"],
    "movie": ["video-x-generic-symbolic"],
    "edit_note": ["accessories-text-editor-symbolic", "document-edit-symbolic"],
    "code": ["applications-engineering-symbolic", "text-x-script-symbolic"],
    "storefront": ["system-software-install-symbolic"],
    "settings": ["emblem-system-symbolic", "preferences-system-symbolic"],
    "build": ["applications-utilities-symbolic", "preferences-other-symbolic"],
    "quiz": ["dialog-question-symbolic", "help-browser-symbolic"],
    "texture": ["image-x-generic-symbolic"],
    "bolt": ["weather-storm-symbolic", "media-seek-forward-symbolic"],
    # Gestures
    "swipe": ["input-touchpad-symbolic", "input-tablet-symbolic"],
    "swipe_up": ["go-up-symbolic", "pan-up-symbolic"],
    "swipe_down": ["go-down-symbolic", "pan-down-symbolic"],
    "swipe_left": ["go-previous-symbolic", "pan-start-symbolic"],
    "swipe_right": ["go-next-symbolic", "pan-end-symbolic"],
    # the first-run setup
    "arrow_forward": ["go-next-symbolic", "pan-end-symbolic"],
    "arrow_back": ["go-previous-symbolic", "pan-start-symbolic"],
    "accessibility_new": ["preferences-desktop-accessibility-symbolic", "org.gnome.Settings-accessibility-symbolic"],
    "location_on": ["mark-location-symbolic", "find-location-symbolic"],
    "wifi_find": ["network-wireless-acquiring-symbolic", "network-wireless-symbolic"],
    "network_wifi": ["network-wireless-signal-excellent-symbolic"],
    "translate": ["preferences-desktop-locale-symbolic", "globe-symbolic"],
    "person": ["avatar-default-symbolic", "system-users-symbolic"],
    "touch_app": ["input-touchpad-symbolic", "input-tablet-symbolic"],
    "stylus_note": ["input-tablet-symbolic", "document-edit-symbolic"],
    "edit_document": ["document-edit-symbolic", "accessories-text-editor-symbolic"],
    "sync": ["emblem-synchronizing-symbolic", "view-refresh-symbolic"],
    "android": ["phone-symbolic", "computer-symbolic"],
    "inventory_2": ["package-x-generic-symbolic"],
    "package_2": ["application-x-addon-symbolic", "package-x-generic-symbolic"],
    "help": ["help-about-symbolic", "dialog-question-symbolic"],
    "key": ["dialog-password-symbolic", "changes-prevent-symbolic"],
    "visibility": ["view-reveal-symbolic"],
    "visibility_off": ["view-conceal-symbolic"],
}
# symbolic icons fill ~7/8 of their box, Material glyphs less: scale to the
# design's visual size (chevrons are the smallest there)
ISCALE = {"chevron_left": .62, "chevron_right": .62, "expand_more": .62, "expand_less": .62, "add": .72, "remove": .72, "refresh": .8}
_theme = None
_icons = {}


def _icon_theme():
    global _theme
    if _theme is None:
        try:
            from gi.repository import Gdk
            _theme = Gtk.IconTheme.get_default() if Gdk.Display.get_default() else None
        except Exception:
            _theme = None
        if _theme is None:                   # no display (offscreen renders)
            _theme = Gtk.IconTheme.new()
            _theme.set_custom_theme("glassine-mono")
    return _theme


def _icon_surface(name, sz, scale):
    """an icon's image at sz logical px (device scale applied), from its SVG
    (librsvg) or else its pixbuf; no display needed"""
    key = (name, sz, scale)
    if key in _icons:
        return _icons[key]
    surf = None
    th = _icon_theme()
    n = int(round(sz * scale))
    for cand in ICONS.get(name, [name]):
        info = th.lookup_icon(cand, n, Gtk.IconLookupFlags.FORCE_SIZE)
        fn = info.get_filename() if info else None
        if not fn:
            continue
        surf = _svg(fn, n) if fn.endswith(".svg") else None
        if surf is None:
            try:
                surf = _pixbuf(info.load_icon(), n)
            except Exception:
                surf = None
        if surf is not None:
            surf.set_device_scale(scale, scale)
            break
    _icons[key] = surf
    return surf


def _svg(fn, n):
    try:
        gi.require_version("Rsvg", "2.0")
        from gi.repository import Rsvg
        h = Rsvg.Handle.new_from_file(fn)
    except Exception:
        return None
    s = cairo.ImageSurface(cairo.FORMAT_ARGB32, n, n)
    cr = cairo.Context(s)
    vp = Rsvg.Rectangle()
    vp.x, vp.y, vp.width, vp.height = 0, 0, n, n
    try:
        h.render_document(cr, vp)
    except Exception:
        return None
    return s


def _pixbuf(pb, n):
    """a GdkPixbuf -> a cairo surface holding its alpha (icons are drawn as masks)"""
    pb = pb.scale_simple(n, n, 2) if pb.get_width() != n else pb
    if not pb.get_has_alpha():
        pb = pb.add_alpha(False, 0, 0, 0)
    w, h, rs = pb.get_width(), pb.get_height(), pb.get_rowstride()
    px_ = pb.get_pixels()
    s = cairo.ImageSurface(cairo.FORMAT_A8, w, h)
    stride = s.get_stride()
    buf = bytearray(stride * h)
    for y in range(h):
        row = y * rs
        for x in range(w):
            buf[y * stride + x] = px_[row + x * 4 + 3]
    s2 = cairo.ImageSurface.create_for_data(buf, cairo.FORMAT_A8, w, h, stride)
    s.flush()
    cr = cairo.Context(s)
    cr.set_source_surface(s2, 0, 0)
    cr.set_operator(cairo.OPERATOR_SOURCE)
    cr.paint()
    return s


RINGED = ("check_circle", "pending", "error", "radio_button_unchecked")          # Material's circled glyphs: a ring + the glyph at 60 %


def icon(cr, name, sz, cx, cy, col, scale=2):
    """a one-colour icon (its alpha as a mask) centred on cx,cy; -> drawn?"""
    if name in ("arrow_forward", "arrow_back"):     # Material's arrows (the themes only have chevrons)
        c = color(col) if isinstance(col, str) else col
        cr.set_source_rgba(*(c or (0, 0, 0, 1)))
        d = 1 if name == "arrow_forward" else -1
        h = sz * 0.31
        cr.set_line_width(max(1.5, sz / 11))
        cr.set_line_cap(cairo.LINE_CAP_SQUARE)
        cr.set_line_join(cairo.LINE_JOIN_MITER)
        cr.move_to(cx - d * h, cy)
        cr.line_to(cx + d * h, cy)
        cr.move_to(cx + d * (h - sz * 0.27), cy - sz * 0.27)
        cr.line_to(cx + d * h, cy)
        cr.line_to(cx + d * (h - sz * 0.27), cy + sz * 0.27)
        cr.stroke()
        cr.set_line_cap(cairo.LINE_CAP_BUTT)
        return True
    if name in RINGED:
        c = color(col) if isinstance(col, str) else col
        cr.set_source_rgba(*(c or (0, 0, 0, 1)))
        cr.set_line_width(max(1.5, sz / 12))
        cr.new_sub_path()
        cr.arc(cx, cy, sz * 0.40, 0, 6.2832)
        cr.stroke()
        if name == "radio_button_unchecked":
            return True
        if name == "error":                      # Material's error: a ring with "!"
            w = max(1.5, sz / 11)
            cr.rectangle(cx - w / 2, cy - sz * 0.22, w, sz * 0.27)
            cr.rectangle(cx - w / 2, cy + sz * 0.12, w, w)
            cr.fill()
            return True
        sz, name = sz * 0.6, ("check" if name == "check_circle" else name)
    sz = round(sz * ISCALE.get(name, .86))
    s = _icon_surface(name, sz, scale)
    if not s:
        return False
    c = color(col) if isinstance(col, str) else col
    cr.set_source_rgba(*(c or (0, 0, 0, 1)))
    cr.mask_surface(s, cx - sz / 2, cy - sz / 2)
    return True
