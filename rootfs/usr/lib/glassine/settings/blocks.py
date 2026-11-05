"""The rows of a settings page (the design's blocks: H TG ST SL MT SG RD IT KV BT
field PG NOTE themes: curve offset; CSS line heights with
wrapping labels and values, compact theme rows). Each block lays itself out for a width and draws
itself at x, y. Its height is its whole box, the rule under it included (what the
design's data-h marks, which adds 1 px). Taps are collected as hits (rect, callback).
KIND is the design's data-widget; app.fit adds the other markers (section, index)
and records them for --markers. Sizes are the design's CSS values in logical px;
narrow/short windows (Ctx) drop labels and shrink as the design's L values do.
"""
import math
import re

import cairo

from . import paint as P
from .theme import OFF

LH_LBL = LH_SUB = None                  # the fonts carry their CSS line-heights (KS row 1.3, sub 1.4)


class Ctx:
    """what blocks draw with: the cairo context, the theme, the hit list"""

    def __init__(self, cr, t, scale=2):
        self.cr, self.t, self.scale = cr, t, scale
        self.hits = []
        self.narrow = self.short = False    # the window's layout (app.Layout)

    def hit(self, x, y, w, h, fn):
        if fn:
            self.hits.append(((x, y, w, h), fn))

    def lbl_caps(self):
        return (self.t["lblVar"], self.t["lblCase"])

    def txt(self, s, f, x, y, col=None, **kw):
        return P.text(self.cr, s, f, x, y, col or self.t["ink"], **kw)

    def m(self, s, f, **kw):
        return P.measure(self.cr, s, f, **kw)

    def icon(self, name, sz, cx, cy, col=None):
        return P.icon(self.cr, name, sz, cx, cy, col or self.t["ink"], self.scale)



def _bw(t, key):
    """the width of the theme's border key (a rule under a block is part of its box)"""
    b = P.border(t[key])
    return b[0] if b else 0


def _fsz(f, px):
    """the CSS font f at another size (the design's font: t.x; font-size: N)"""
    return re.sub(r"[\d.]+px", "%gpx" % px, f, count=1)


def _fpx(f, weight=None):
    return P.font(f, weight)[0].get_size() / 1024


def _base(c, s, f, lh=None, weight=None, caps=None):
    """the first line's baseline below the text's top (with a CSS line-height: in its line box)"""
    lo = P.layout(c.cr, s or "x", f, weight, caps)
    b = P.baseline(lo)
    if lh:
        b += (lh * _fpx(f, weight) - P.size(lo)[1]) / 2
    return b


def _brow(*items):
    """a flex row with align-items: baseline; items [(baseline, height)] ->
    ([top offset of each], the row's height)"""
    m = max(b for b, _ in items)
    offs = [m - b for b, _ in items]
    return offs, max(o + h for o, (_, h) in zip(offs, items))


def _col_h(c, label, sub, w, gap=6, weight=None, llh=LH_LBL, sub_w=None):
    """the design's label-over-sub column, both wrapping in w (the sub in sub_w)"""
    t = c.t
    h = c.m(label, t["rowFont"], weight=weight, maxw=w, wrap=True, lh=llh)[1] if label else 0
    if sub:
        h += gap + c.m(sub, t["dateFont"], maxw=sub_w or w, wrap=True, lh=LH_SUB)[1]
    return h


def _col(c, label, sub, x, y, w, gap=6, weight=None, llh=LH_LBL, sub_w=None, col=None, sub_col=None, sub_weight=None):
    """draws _col_h's column with its top at y -> the label's width"""
    t = c.t
    lw, lh = c.txt(label, t["rowFont"], x, y, col, weight=weight, maxw=w, wrap=True, lh=llh) if label else (0, 0)
    if sub:
        c.txt(sub, t["dateFont"], x, y + lh + gap, sub_col or t["sub"], weight=sub_weight, maxw=sub_w or w, wrap=True, lh=LH_SUB)
    return lw


def _btn(c, x, y, w, h, bg=None, sh=None, r=None):
    t = c.t
    P.box(c.cr, x, y, w, h, r or t["btnR"], bg if bg is not None else t["btnBg"], sh if sh is not None else t["btnSh"])


class Block:
    KIND = "block"                      # the design's data-widget

    def height(self, c, w):
        return 0

    def draw(self, c, x, y, w):
        pass

    def summ(self):
        """what a collapsed section's row shows of this widget (None: nothing)"""
        return None


class Head(Block):
    """a section head: the label (KS head, ink, its own caps and spacing)
    with an aside on the right (KS sub, sub colour; hidden when the page is narrow,
    the design's cw > 380), padding 20 / 9, the KS rule under it. inline: the
    section's widget draws its own title (Today's curve), so the head has no row:
    app.fit leaves it out but keeps the section"""
    KIND = "section-head"

    def __init__(self, label, aside="", inline=False, list_=False, summary=None, as_row=None):
        self.label, self.aside, self.inline = label, aside, inline
        self.list_, self.summary, self.as_row = list_, summary, as_row    # app.fit: never fold (it pages); the folded row's text; always a row

    def _lay(self, c, w):
        t = c.t
        lw, lh = c.m(self.label, t["lblFont"], caps=c.lbl_caps())
        lb = _base(c, self.label, t["lblFont"], caps=c.lbl_caps())
        aw = w - lw - 12
        if not self.aside or w <= 380 or aw < 20:
            return lw, 0, aw, None, 0, lh
        ah = c.m(self.aside, t["dateFont"], maxw=aw)[1]
        (lo, ao), rh = _brow((lb, lh), (_base(c, self.aside, t["dateFont"]), ah))
        return lw, lo, aw, ao, ah, rh

    def height(self, c, w):
        return 20 + self._lay(c, w)[-1] + 9 + _bw(c.t, "sepSoft")

    def draw(self, c, x, y, w):
        t = c.t
        lw, lo, aw, ao, ah, rh = self._lay(c, w)
        c.txt(self.label, t["lblFont"], x, y + 20 + lo, t["lblCol"], caps=c.lbl_caps(), ls=P.ls_em(t["lblLs"]), maxw=w)
        if ao is not None:
            c.txt(self.aside, t["dateFont"], x + w, y + 20 + ao, t["sub"], align=2, maxw=aw)
        P.rule(c.cr, x, y + self.height(c, w), w, t["sepSoft"])


class Toggle(Block):
    """label / sub ... ON|OFF (KS onLbl, when the page is wider than 300) + a switch
    (track 64x36, radius 999, 1.5px ink; on = accent track, on-accent knob;
    off = paper track, ink knob; padding 4, knob 25). armed: the first tap of a switch
    that needs a second ("Sure?", the sub in ink and bold, a dashed outline round the
    switch); sub_ink: a sub that is news (ink)"""
    KIND = "tog"

    def __init__(self, label, sub, on, fn, armed=False, sub_ink=False):
        self.label, self.sub, self.on, self.fn, self.armed, self.sub_ink = label, sub, on, fn, armed, sub_ink

    def _state(self):
        return "Sure?" if self.armed else "On" if self.on else "Off"

    def _st(self, c, w):
        return w > 300

    def _caps(self, c):
        return (c.t["lblVar"], c.t["lblCase"])

    def _tw(self, c, w):
        t = c.t
        stw = c.m(self._state(), t["onLblFont"], caps=self._caps(c))[0] + 14 if self._st(c, w) else 0
        return w - 14 - stw - 64

    def height(self, c, w):
        return max(58, 12 + max(_col_h(c, self.label, self.sub, self._tw(c, w)), 36) + 14) + _bw(c.t, "sep")

    def draw(self, c, x, y, w):
        t = c.t
        h = self.height(c, w)
        ch = h - 26 - _bw(t, "sep")                      # the content box (padding 12 / 14)
        tw = self._tw(c, w)
        mid = y + 12 + ch / 2
        ink = self.armed or self.sub_ink
        _col(c, self.label, self.sub, x, mid - _col_h(c, self.label, self.sub, tw) / 2, tw,
             sub_col=t["ink"] if ink else None, sub_weight=600 if self.armed else None)
        sx, sy = x + w - 64, mid - 18
        if self._st(c, w):
            s = self._state()
            sh = c.m(s, t["onLblFont"], caps=self._caps(c))[1]
            c.txt(s, t["onLblFont"], sx - 14, mid - sh / 2, caps=self._caps(c), align=2, ls=0.16)
        P.box(c.cr, sx, sy, 64, 36, "999px", t["swOnBg"] if self.on else t["swOffBg"], t["swOnSh"] if self.on else t["swOffSh"])
        if self.armed:                                  # outline: 2px dashed, offset 4
            P.box(c.cr, sx - 6, sy - 6, 64 + 12, 36 + 12, "999px", None, None, "2px dashed " + t["ink"])
        kx = sx + 64 - 5.5 - 25 if self.on else sx + 5.5
        P.box(c.cr, kx, sy + 5.5, 25, 25, "50%", t["swKOn"] if self.on else t["swKOff"], None, t["swKBd"])
        P.rule(c.cr, x, y + h, w, t["sep"])
        c.hit(x, y, w, h, self.fn)

    def summ(self):
        return "%s %s" % (self.label, self._state().lower())


class Stepper(Block):
    """label | − value + ; opts [(value, text)]. The controls wrap below the label
    when it would get less than 200 px (the design's flex: 1 1 200px)"""
    KIND = "step"

    def __init__(self, label, sub, opts, cur, set_fn):
        self.label, self.sub, self.opts, self.set_fn = label, sub, opts, set_fn
        vals = [o[0] for o in opts]
        self.i = vals.index(cur) if cur in vals else min(range(len(vals)), key=lambda j: abs(vals[j] - cur) if isinstance(cur, (int, float)) else 0)

    def _vw(self, c):
        return max(96, c.m(self.opts[self.i][1], c.t["valFont"])[0])

    def _ctl(self, c):
        return 48 + 14 + self._vw(c) + 14 + 48

    def _wraps(self, c, w):
        return 200 + 14 + self._ctl(c) > w

    def height(self, c, w):
        bw = _bw(c.t, "sep")
        if self._wraps(c, w):
            return max(64, 12 + _col_h(c, self.label, self.sub, w) + 10 + 48 + bw)
        return max(64, 12 + max(_col_h(c, self.label, self.sub, w - 14 - self._ctl(c)), 48) + bw)

    def draw(self, c, x, y, w):
        t, i = c.t, self.i
        h = self.height(c, w)
        if self._wraps(c, w):
            th = _col_h(c, self.label, self.sub, w)
            _col(c, self.label, self.sub, x, y + 6, w)
            bx, by = x, y + 6 + th + 10
        else:
            ch = h - 12 - _bw(t, "sep")
            tw = w - 14 - self._ctl(c)
            _col(c, self.label, self.sub, x, y + 6 + (ch - _col_h(c, self.label, self.sub, tw)) / 2, tw)
            bx, by = x + w - self._ctl(c), y + 6 + (ch - 48) / 2
        n = len(self.opts)
        vw = self._vw(c)
        for ix, glyph, ok, j in ((bx, "remove", i > 0, i - 1), (bx + 48 + 14 + vw + 14, "add", i < n - 1, i + 1)):
            _btn(c, ix, by, 48, 48)
            c.icon(glyph, 24, ix + 24, by + 24, t["ink"] if ok else OFF)
            if ok:
                c.hit(ix, by, 48, 48, (lambda j=j: self.set_fn(self.opts[j][0])))
        v = self.opts[i][1]
        vh = c.m(v, t["valFont"])[1]
        c.txt(v, t["valFont"], bx + 48 + 14 + vw / 2, by + (48 - vh) / 2, align=1)
        P.rule(c.cr, x, y + h, w, t["sep"])

    def summ(self):
        return self.opts[self.i][1]


class Slider(Block):
    """label ... value; a groove 0..100 in steps of 10 (tap sets)"""
    KIND = "slider"

    def __init__(self, label, val, set_fn, unit=" %"):
        self.label, self.val, self.set_fn, self.unit = label, val, set_fn, unit

    def _row(self, c, w):
        t = c.t
        v = "%d%s" % (self.val, self.unit)
        vw, vh = c.m(v, t["valFont"])
        lh = c.m(self.label, t["rowFont"], maxw=w - vw - 12, wrap=True, lh=LH_LBL)[1]
        return v, vw, _brow((_base(c, self.label, t["rowFont"], LH_LBL), lh), (_base(c, v, t["valFont"]), vh))

    def height(self, c, w):
        return 14 + self._row(c, w)[2][1] + 4 + 44 + 10 + _bw(c.t, "sep")

    def draw(self, c, x, y, w):
        t = c.t
        v, vw, ((lo, vo), rh) = self._row(c, w)
        c.txt(self.label, t["rowFont"], x, y + 14 + lo, maxw=w - vw - 12, wrap=True, lh=LH_LBL)
        c.txt(v, t["valFont"], x + w, y + 14 + vo, align=2)
        ty = y + 14 + rh + 4
        _groove(c, x, ty, w, self.val)
        kw, kh = P.px(t["knobW"]), P.px(t["knobH"])
        kx = x + w * self.val / 100 + P.px(t["knobML"])
        P.box(c.cr, kx, ty + P.px(t["knobTop"]), kw, kh, t["knobR"], t["knobBg"], None, t["knobBd"])
        P.rule(c.cr, x, y + self.height(c, w), w, t["sep"])

        def tap(ex, ey, x=x, w=w):
            v = max(0, min(100, round((ex - x) / w * 10) * 10))
            self.set_fn(v)
        tap.xy = True                       # called with the tap's position
        tap.arrow = lambda d, _shift: self.set_fn(max(0, min(100, self.val + d * 10)))
        c.hits.append(((x - 10, ty, w + 20, 44), tap))

    def summ(self):
        return "%d%s" % (self.val, self.unit)


def _groove(c, x, ty, w, pct):
    t = c.t
    P.box(c.cr, x, ty + P.px(t["grooveTop"]), w, P.px(t["grooveH"]), t["grooveR"], t["grooveBg"], t["grooveSh"])
    if pct > 0:
        P.fill(c.cr, x, ty + P.px(t["fillTop"]), w * pct / 100, P.px(t["fillH"]), t["ink"])


def _fs(f, px):
    """the CSS font f at px (the design's font:...;font-size:Npx)"""
    import re
    return re.sub(r"[\d.]+px(/[\d.]+)?", "%gpx" % px, f, count=1)



class Meter(Block):
    KIND = "meter"

    def __init__(self, label, sub, val, pct):
        self.label, self.sub, self.val, self.pct = label, sub, val, pct

    def _row(self, c, w):
        t = c.t
        vw, vh = c.m(self.val, t["valFont"])
        cw = w - vw - 12
        ch = _col_h(c, self.label, self.sub, cw, gap=2)
        return cw, _brow((_base(c, self.label, t["rowFont"], LH_LBL), ch), (_base(c, self.val, t["valFont"]), vh))

    def height(self, c, w):
        return 16 + self._row(c, w)[1][1] + 40 + 4 + _bw(c.t, "sep")

    def draw(self, c, x, y, w):
        t = c.t
        cw, ((lo, vo), rh) = self._row(c, w)
        _col(c, self.label, self.sub, x, y + 16 + lo, cw, gap=2)
        c.txt(self.val, t["valFont"], x + w, y + 16 + vo, align=2)
        _groove(c, x, y + 16 + rh, w, max(0, min(100, self.pct)))
        P.rule(c.cr, x, y + self.height(c, w), w, t["sep"])

    def summ(self):
        return self.label


class Segments(Block):
    """label/sub over a segmented control (a 1.5px outline, radius KS segR,
    padding 3; segments 48 high in the row font at KS segFs; the current one in
    KS segOn bg/fg/border/shadow); opts [(value, text)]"""
    KIND = "seg"

    def __init__(self, label, sub, opts, cur, set_fn):
        self.label, self.sub, self.opts, self.cur, self.set_fn = label, sub, opts, cur, set_fn

    def _cols(self, c, w):
        """one row if the labels fit (the design's estimate: 0.56 em a letter), else two
        to a row; a segment is never squeezed"""
        px = float(re.search(r"([\d.]+)px", c.t["segFont"]).group(1))
        return len(self.opts) if sum(len(l) * px * 0.56 + 16 for _, l in self.opts) + 9 <= w else 2

    def height(self, c, w):
        rows = -(-len(self.opts) // self._cols(c, w))
        return 12 + _col_h(c, self.label, self.sub, w) + 10 + 3 + 48 + 3 + 3 + 16 + _bw(c.t, "sep") + 51 * (rows - 1)

    def draw(self, c, x, y, w):
        t = c.t
        _col(c, self.label, self.sub, x, y + 12, w)
        ty = y + 12 + _col_h(c, self.label, self.sub, w) + 10
        cols = self._cols(c, w)
        rows = -(-len(self.opts) // cols)
        P.box(c.cr, x, ty, w, 54 + 3 + 51 * (rows - 1), t["trayR"], None, None, "1.5px solid " + t["ink"])
        sw = (w - 3 - 6) / cols
        # (a label wider than its segment even so: every label one size smaller, down to 70 %)
        f = t["segFont"]
        widest = max(c.m(label, f)[0] for _, label in self.opts)
        if widest > sw - 12:
            px = float(re.search(r"([\d.]+)px", f).group(1))
            f = _fs(f, max(px * 0.7, px * (sw - 12) / widest))
        for i, (v, label) in enumerate(self.opts):
            sx = x + 1.5 + 3 + (i % cols) * sw
            sy = ty + 4.5 + (i // cols) * 51
            on = v == self.cur
            if on:
                P.box(c.cr, sx, sy, sw, 48, t["segR"], t["segOnBg"], None if t["segOnSh"] == "none" else t["segOnSh"], t["segOnBd"])
            sh = c.m(label, f)[1]
            c.txt(label, f, sx + sw / 2, sy + (48 - sh) / 2, t["segOnFg"] if on else t["ink"], align=1, maxw=sw - 4)
            c.hit(sx, sy, sw, 48, (lambda v=v: self.set_fn(v)))
        P.rule(c.cr, x, y + self.height(c, w), w, t["sep"])

    def summ(self):
        return next((label for v, label in self.opts if v == self.cur), None)


_DPREV = {}


def _dither_previews():
    """the design's previews (dithPrev): one 64 x 36 gradient with a
    circle, in pure black (#111) and white per pattern -> {value: cairo surface}"""
    if _DPREV:
        return _DPREV
    W, H = 64, 36
    N = W * H
    val = []
    for y in range(H):
        for x in range(W):
            dx, dy = (x - W * 0.7) / (H * 0.42), (y - H * 0.48) / (H * 0.42)
            d = math.sqrt(dx * dx + dy * dy)
            val.append(min(1.0, max(0.0, x / (W - 1) * 0.9 + ((1 - d * d) * 0.45 if d < 1 else 0))))
    b8 = [0, 32, 8, 40, 2, 34, 10, 42, 48, 16, 56, 24, 50, 18, 58, 26, 12, 44, 4, 36, 14, 46, 6, 38, 60, 28, 52, 20, 62, 30, 54, 22,
          3, 35, 11, 43, 1, 33, 9, 41, 51, 19, 59, 27, 49, 17, 57, 25, 15, 47, 7, 39, 13, 45, 5, 37, 63, 31, 55, 23, 61, 29, 53, 21]

    def fr(v):
        return v - math.floor(v)

    def mk(th):
        return [1 if val[i] > th(i % W, i // W) else 0 for i in range(N)]

    def dizzy():                        # error diffusion in a shuffled order (the design's PRNG)
        v, out, done = list(val), [0] * N, [0] * N
        order, r = list(range(N)), 7
        for i in range(N - 1, 0, -1):
            r = r * 16807 % 2147483647
            j = int(r / 2147483647 * (i + 1))
            order[i], order[j] = order[j], order[i]
        for p_ in order:
            x, y = p_ % W, p_ // W
            out[p_] = 1 if v[p_] >= 0.5 else 0
            done[p_] = 1
            e = v[p_] - out[p_]
            nb = [ny * W + nx for oy in (-1, 0, 1) for ox in (-1, 0, 1)
                  for nx, ny in ((x + ox, y + oy),) if (ox or oy) and 0 <= nx < W and 0 <= ny < H and not done[ny * W + nx]]
            for q in nb:
                v[q] += e / len(nb)
        return out
    sets = {"off": mk(lambda x, y: 0.5),
            "bayer": mk(lambda x, y: (b8[(y % 8) * 8 + x % 8] + 0.5) / 64),
            "blue": mk(lambda x, y: fr(0.7548776662 * x + 0.56984029 * y + 0.37 * fr(math.sin(x * 12.9898 + y * 78.233) * 43758.5453))),
            "dizzy": dizzy()}
    for k, bits in sets.items():
        sf = cairo.ImageSurface(cairo.FORMAT_RGB24, W, H)
        st = sf.get_stride()
        buf = bytearray(st * H)
        for i, b in enumerate(bits):
            g = 255 if b else 17
            o = (i // W) * st + (i % W) * 4
            buf[o:o + 4] = bytes((g, g, g, 0))
        sf.get_data()[:] = bytes(buf)
        sf.mark_dirty()
        _DPREV[k] = sf
    return _DPREV


class Dither(Block):
    """the dithering picker. label/sub over tiles (four in a row, two below
    400 px), each a preview of the pattern over its name; the selected
    tile in the segmented control's "on" style. opts [(value, text)], the
    value a key of _dither_previews ("off", "bayer", "blue", "dizzy")"""
    KIND = "dither-pattern"

    def __init__(self, label, sub, opts, cur, set_fn):
        self.label, self.sub, self.opts, self.cur, self.set_fn = label, sub, opts, cur, set_fn

    def _tile(self, w):
        n = 2 if w < 400 else len(self.opts)
        tw = (w - 8 - 8 * (n - 1)) / n
        ih = max(40, min(64, (tw - 12) * 0.56))
        return tw, ih, 6 + ih + 6 + 22 + 6, n

    def _tray(self, w):
        tw, ih, th, n = self._tile(w)
        rows = -(-len(self.opts) // n)
        return rows * th + (rows - 1) * 8

    def height(self, c, w):
        return 12 + _col_h(c, self.label, self.sub, w) + 14 + 8 + self._tray(w) + 16 + _bw(c.t, "sep")

    def draw(self, c, x, y, w):
        t, cr = c.t, c.cr
        _col(c, self.label, self.sub, x, y + 12, w)
        ty = y + 12 + _col_h(c, self.label, self.sub, w) + 14
        tw, ih, th, n = self._tile(w)
        P.box(cr, x, ty, w, self._tray(w) + 8, t["trayR"], None, None, "1.5px solid " + t["ink"])
        prev = _dither_previews()
        for i, (v, label) in enumerate(self.opts):
            sx, sy = x + 4 + (i % n) * (tw + 8), ty + 4 + (i // n) * (th + 8)
            on = v == self.cur
            if on:
                P.box(cr, sx, sy, tw, th, t["segR"], None, None if t["segOnSh"] == "none" else t["segOnSh"], t["segOnBd"])
            fg = t["ink"]
            iw = tw - 12
            P.box(cr, sx + 6, sy + 6, iw, ih, "3px", None, "0 0 0 1px " + t["ink"])
            sf = prev.get(v)
            if sf:
                cr.save()
                P.rrect(cr, sx + 6, sy + 6, iw, ih, 2)
                cr.clip()
                cr.translate(sx + 6, sy + 6)
                cr.scale(iw / sf.get_width(), ih / sf.get_height())
                cr.set_source_surface(sf, 0, 0)
                cr.get_source().set_filter(cairo.FILTER_NEAREST)     # image-rendering: pixelated
                cr.paint()
                cr.restore()
            fw = 600 if on else 400
            lh = c.m(label, t["rowFont"], weight=fw)[1]
            c.txt(label, t["rowFont"], sx + tw / 2, sy + 6 + ih + 6 + (22 - lh) / 2, fg, weight=fw, align=1, maxw=tw - 4)
            c.hit(sx, sy, tw, th, (lambda v=v: self.set_fn(v)))
        P.rule(cr, x, y + self.height(c, w), w, t["sep"])

    def summ(self):
        label = next((label for v, label in self.opts if v == self.cur), None)
        return None if label is None else "No dithering" if self.cur == "off" else label + " dithering"


class Radio(Block):
    KIND = "radio"

    def __init__(self, label, sub, on, fn, disabled=False):
        self.label, self.sub, self.on, self.fn, self.dis = label, sub, on, fn, disabled
        self.focus = on                     # a page opens at its selected row (app.fit)

    def summ(self):
        return self.label if self.on else None

    def _ch(self, c, w):
        return _col_h(c, self.label, self.sub, w - 40, weight=600 if self.on else 400, llh=None)

    def height(self, c, w):
        return max(54, 26 + self._ch(c, w)) + _bw(c.t, "sep")

    def draw(self, c, x, y, w):
        t = c.t
        col = OFF if self.dis else t["ink"]
        h = self.height(c, w)
        ch = h - _bw(t, "sep")
        cy = y + ch / 2
        rx = x + w - 26                     # label left, the radio at the right
        P.box(c.cr, rx, cy - 13, 26, 26, "50%", None, None, "1.5px solid " + col)
        if self.on:
            P.box(c.cr, rx + 6, cy - 7, 14, 14, "50%", col)
        _col(c, self.label, self.sub, x, y + (ch - self._ch(c, w)) / 2, w - 40, weight=600 if self.on else 400, llh=None, col=col)
        P.rule(c.cr, x, y + h, w, t["sep"])
        if not self.dis:
            c.hit(x, y, w, h, self.fn)


def _badge_w(c, b):
    return 8 + 16 + 4 + c.m(b[0], "500 12.5px 'IBM Plex Sans'")[0] + 10


def _badge(c, b, right, mid):
    """an outlined pill, its right edge at right: [icon] word (never by colour alone) -> its width"""
    w = _badge_w(c, b)
    x = right - w
    P.box(c.cr, x, mid - 13, w, 26, "999px", None, None, "1px solid " + c.t["ink"])
    c.icon(b[1], 16, x + 8 + 8, mid)
    f = "500 12.5px 'IBM Plex Sans'"
    c.txt(b[0], f, x + 8 + 16 + 4, mid - c.m(b[0], f)[1] / 2)
    return w


class Item(Block):
    """[icon] label (lock) / sub ... [value] [action button] [chevron]. The label
    wraps; the value wraps right-aligned in at most 46 % of the row. When the text
    would get less than 64 px (a narrow window), the action button goes under it
    (the design overflows there). sum_txt: what a collapsed section shows of it
    (else the value, else the label)"""
    KIND = "item"

    min_h = 58

    def __init__(self, label, sub="", icon=None, fn=None, val=None, act=None, act_fn=None,
                 primary=False, chev=False, lock=False, bold=False, sum_txt=None, frame=False, act_off=False, badge=None):
        self.badge = badge                  # (word, icon): where an app comes from, an outlined pill
        self.label, self.sub, self.icon, self.fn, self.val = label, sub, icon, fn, val
        self.act, self.act_fn, self.primary, self.chev, self.lock, self.bold = act, act_fn, primary, chev, lock, bold
        self.sum_txt, self.frame, self.act_off = sum_txt, frame, act_off   # frame: an app's icon in a 44 px box

    def _parts(self, c, w):
        """-> label x, label width, value width, button width, button under the text"""
        t = c.t
        right, aw, vw = w, 0, 0
        if self.chev:
            right -= 26 + 14
        if self.badge and w >= 380:         # (a narrow row: under the name, in the sub)
            right -= _badge_w(c, self.badge) + 12
        if self.val is not None:
            vw = min(c.m(self.val, t["valFont"])[0] + 1, 0.46 * w)
            right -= vw + 14
        lx = (44 + 14 if self.frame else 24 + 14) if self.icon else 0
        if self.act:
            aw = c.m(self.act, t["btnFont"])[0] + 52
            if right - lx - aw - 16 < 64:
                return lx, right - lx, vw, aw, True
            right -= aw + 16
        return lx, right - lx, vw, aw, False

    def _th(self, c, lw):
        return _col_h(c, self.label, self.sub, lw - (22 if self.lock else 0), weight=600 if self.bold else 400, sub_w=lw,
                      gap=5 if self.frame else 6)

    def _vh(self, c, vw):
        return c.m(self.val, c.t["valFont"], maxw=vw, wrap=True, lh=1.3)[1] if self.val is not None else 0

    def height(self, c, w):
        lx, lw, vw, aw, under = self._parts(c, w)
        pad = 22 if self.frame else 26                  # padding 10/12 (an app row), else 12/14
        if under:
            return max(self.min_h, pad + max(self._th(c, lw) + 8 + 48, self._vh(c, vw), 26) + _bw(c.t, "sep"))
        return max(self.min_h, pad + max(self._th(c, lw), self._vh(c, vw), 48 if self.act else 0, 44 if self.frame else 26) + _bw(c.t, "sep"))

    def draw(self, c, x, y, w):
        t = c.t
        h = self.height(c, w)
        pt, pb = (10, 12) if self.frame else (12, 14)
        ch = h - pt - pb - _bw(t, "sep")
        mid = y + pt + ch / 2
        lx, lw, vw, aw, under = self._parts(c, w)
        th = self._th(c, lw)
        right = x + w
        if self.chev:
            c.icon("chevron_right", 26, right - 13, mid)
            right -= 26 + 14
        if self.badge and w >= 380:
            right -= _badge(c, self.badge, right, mid) + 12
        if self.act:
            if under:                                   # under the text, at its left
                ax, ay = x + lx, mid - (th + 8 + 48) / 2 + th + 8
            else:
                ax, ay = right - aw, mid - 24
                right = ax - 14
            prim = self.primary and not self.act_off
            bg, fg, sh = (t["pwrBg"], t["pwrFg"], t["pwrSh"]) if prim else (t["btnBg"], OFF if self.act_off else t["ink"], t["btnSh"])
            _btn(c, ax, ay, aw, 48, bg, sh)
            bh = c.m(self.act, t["btnFont"], weight=600 if prim else None)[1]
            c.txt(self.act, t["btnFont"], ax + aw / 2, ay + 24 - bh / 2, fg, align=1, weight=600 if prim else None)
            if not self.act_off:
                c.hit(ax, ay, aw, 48, self.act_fn)
        if self.val is not None:
            vh = self._vh(c, vw)
            c.txt(self.val, t["valFont"], right, mid - vh / 2, align=2, maxw=vw, wrap=True, lh=1.3)
        if self.icon and self.frame:
            P.box(c.cr, x, mid - 22, 44, 44, "10px", None, None, "1.5px solid " + t["ink"])
            c.icon(self.icon, 24, x + 22, mid)
        elif self.icon:
            c.icon(self.icon, 22, x + 12, mid)
        fw = 600 if self.bold else 400
        top = mid - (th + 8 + 46 if under else th) / 2
        tw = _col(c, self.label, self.sub, x + lx, top, lw - (22 if self.lock else 0), weight=fw, sub_w=lw,
                  gap=5 if self.frame else 6)
        if self.lock:
            llh = c.m(self.label, t["rowFont"], weight=fw, maxw=lw - 22, wrap=True, lh=LH_LBL)[1]
            c.icon("lock", 16, x + lx + tw + 6 + 8, top + llh / 2, t["sub"])
        P.rule(c.cr, x, y + h, w, t["sep"])
        # the row's tap first: the action button's hit (added above) wins as it is checked last-first
        if self.fn:
            c.hits.insert(0, ((x, y, w, h), self.fn))

    def summ(self):
        return self.sum_txt if self.sum_txt is not None else self.val if self.val is not None else self.label


class KV(Block):
    KIND = "kv"

    def __init__(self, label, val):
        self.label, self.val = label, val

    def _lay(self, c, w):
        t = c.t
        vw, vh = c.m(self.val, t["valFont"])
        vw = min(vw, 0.6 * w)
        return vw, vh, c.m(self.label, t["rowFont"], maxw=w - vw - 16, wrap=True, lh=LH_LBL)[1]

    def height(self, c, w):
        vw, vh, lh = self._lay(c, w)
        return max(52, 26 + max(lh, vh)) + _bw(c.t, "sep")

    def draw(self, c, x, y, w):
        t = c.t
        vw, vh, lh = self._lay(c, w)
        h = self.height(c, w)
        ch = h - _bw(t, "sep")
        c.txt(self.label, t["rowFont"], x, y + (ch - lh) / 2, maxw=w - vw - 16, wrap=True, lh=LH_LBL)
        c.txt(self.val, t["valFont"], x + w, y + (ch - vh) / 2, align=2, maxw=vw)
        P.rule(c.cr, x, y + h, w, t["sep"])

    def summ(self):
        return self.val


class Button(Block):
    KIND = "btn"
    def __init__(self, label, icon, primary, fn, disabled=False):
        self.label, self.icon, self.primary, self.fn, self.dis = label, icon, primary, fn, disabled

    def height(self, c, w):
        return 22 + 48 + 6

    def draw(self, c, x, y, w):
        t = c.t
        prim = self.primary and not self.dis
        bg, sh = (t["pwrBg"], t["pwrSh"]) if prim else (t["btnBg"], t["btnSh"])
        fg = OFF if self.dis else t["pwrFg"] if prim else t["ink"]
        fw = 600 if prim else None
        lw, lh = c.m(self.label, t["btnFont"], weight=fw)
        bw = 14 + 22 + 6 + lw + 22
        by = y + 22
        P.box(c.cr, x, by, bw, 48, "999px", bg, sh)
        c.icon(self.icon, 22, x + 14 + 11, by + 24, fg)
        c.txt(self.label, t["btnFont"], x + 14 + 22 + 6, by + (48 - lh) / 2, fg, weight=fw)
        if not self.dis:
            c.hit(x, by, bw, 48, self.fn)


class Field(Block):
    """a text field (the password): label, a tray with the value, caret, placeholder"""

    def __init__(self, label, val, placeholder, caret=True, on=None):
        self.label, self.val, self.ph, self.caret, self.on = label, val, placeholder, caret, on

    def height(self, c, w):
        t = c.t
        return 18 + c.m(self.label, t["lblFont"], caps=c.lbl_caps())[1] + 10 + 52 + 8

    def draw(self, c, x, y, w):
        t = c.t
        lh = c.m(self.label, t["lblFont"], caps=c.lbl_caps())[1]
        c.txt(self.label, t["lblFont"], x, y + 18, t["lblCol"], caps=c.lbl_caps(), ls=P.ls_em(t["lblLs"]))
        fy = y + 18 + lh + 10
        # the design's search field: 52 high, 1.5px ink, radius KS segR; focused: a 2px
        # ring outside (the accessibility round's focus ring)
        P.box(c.cr, x, fy, w, 52, t["trayR"], t["paper"], "inset 0 0 0 1.5px " + t["ink"] + (
            ",0 0 0 2px %s,0 0 0 4px %s" % (t["paper"], t["ink"]) if self.caret else ""))
        vw, vh = c.m(self.val or " ", t["fieldFont"], maxw=w - 60)
        if self.val:
            c.txt(self.val, t["fieldFont"], x + 14, fy + (52 - vh) / 2, maxw=w - 60)
        else:
            vw = 0
        if self.on:
            c.hit(x, fy, w, 52, self.on)
        if not self.caret:                  # the other field (an 802.1X login has two)
            return
        cx = x + 14 + vw + 2
        P.fill(c.cr, cx, fy + 14, 2, 24, t["ink"])
        if self.ph and not self.val:
            ph = c.m(self.ph, t["dateFont"])[1]
            c.txt(self.ph, t["dateFont"], cx + 2 + 8, fy + (52 - ph) / 2, t["sub"])



class Pager(Block):
    """the page's pager, pinned to the card's bottom (app.fit pages a long page)"""
    KIND = "pager"

    def __init__(self, n, of, set_fn, pad=12):
        self.n, self.of, self.set_fn, self.pad = n, of, set_fn, pad

    def height(self, c, w):
        return 66

    def draw(self, c, x, y, w):
        t = c.t
        P.rule(c.cr, x, y + 1, w, "1px solid " + t["ink"])
        by = y + 1 + (65 - 48) / 2
        for bx, glyph, ok, j in ((x, "chevron_left", self.n > 0, self.n - 1), (x + w - 48, "chevron_right", self.n < self.of - 1, self.n + 1)):
            _btn(c, bx, by, 48, 48)
            c.icon(glyph, 26, bx + 24, by + 24, t["ink"] if ok else OFF)
            if ok:
                c.hit(bx, by, 48, 48, (lambda j=j: self.set_fn(j)))
        s = "Page %d of %d" % (self.n + 1, self.of)
        sh = c.m(s, t["pageFont"])[1]
        c.txt(s, t["pageFont"], x + w / 2, by + (48 - sh) / 2, align=1)

    @staticmethod
    def small(c, right, by, pg):
        """the compact header's pager, its right edge at right: ‹ 1/2 › -> its width"""
        t = c.t
        f = "500 13px 'IBM Plex Mono',monospace"
        s = "%d/%d" % (pg.n + 1, pg.of)
        tw, th = c.m(s, f)
        total = 48 + 4 + tw + 4 + 48
        x = right - total
        for bx, glyph, ok, j in ((x, "chevron_left", pg.n > 0, pg.n - 1), (right - 48, "chevron_right", pg.n < pg.of - 1, pg.n + 1)):
            _btn(c, bx, by, 48, 48, r="999px")
            c.icon(glyph, 26, bx + 24, by + 24, t["ink"] if ok else OFF)
            if ok:
                c.hit(bx, by, 48, 48, (lambda j=j: pg.set_fn(j)))
        c.txt(s, f, x + 48 + 4, by + (48 - th) / 2)
        return total



class Note(Block):
    KIND = "note"

    def __init__(self, label):
        self.label = label

    def height(self, c, w):
        return 16 + c.m(self.label, c.t["dateFont"], maxw=w, wrap=True)[1] + 10

    def draw(self, c, x, y, w):
        c.txt(self.label, c.t["dateFont"], x, y + 16, c.t["sub"], maxw=w, wrap=True)


THEME_BY = {"white": "Bright paper, crisp ink, nothing in the way",
            "editorial": "Serif type and fine rules, set like a good magazine",
            "stationery": "Cloth, cardstock and Garamond from the writing desk"}


class Rotation(Block):
    """design , Appearance: the four orientations as cells (a device with a
    T turned to where the screen's top will be, the name, a tag). sel: the locked
    one (ink filled), now: the sensor's in auto (dashed)"""
    KIND = "rot"
    NAMES = ("Upright", "Turned right", "Upside down", "Turned left")
    SIDES = ("Top at the top", "Top at the right", "Top at the bottom", "Top at the left")

    def __init__(self, auto, lock, sensor, fn):
        self.auto, self.lock, self.sensor, self.fn = auto, lock, sensor, fn      # degrees; fn(deg)

    def _grid(self, w):
        n = max(1, min(4, int((w + 8) // (84 + 8))))       # repeat(auto-fit, minmax(84px, 1fr)), gap 8
        return n, (w - 8 * (n - 1)) / n

    def _cell_h(self, c, w):
        t = c.t
        nh = c.m("Upright", _fsz(t["dateFont"], 13), lh=1.2)[1]
        th = c.m("Top at the top", _fsz(t["dateFont"], 11), lh=1.2)[1]
        return 8 + 38 + 5 + 6 + nh + 6 + th + 6             # the grid row stretches to a cell with a tag

    def height(self, c, w):
        n, _ = self._grid(w)
        rows = (4 + n - 1) // n
        return 8 + rows * self._cell_h(c, w) + (rows - 1) * 8 + 12 + _bw(c.t, "sep")

    def draw(self, c, x, y, w):
        t, cr = c.t, c.cr
        n, cw = self._grid(w)
        ch = self._cell_h(c, w)
        paper = t.get("cardBg") or "#ffffff"
        for i in range(4):
            d = i * 90
            sel, now = not self.auto and self.lock == d, self.auto and self.sensor == d
            cx, cy = x + (i % n) * (cw + 8), y + 8 + (i // n) * (ch + 8)
            fg = paper if sel else t["ink"]
            bd = "2px dashed " + t["ink"] if now else "2px solid " + t["ink"] if sel else "1px solid " + t["sub"]
            P.box(cr, cx, cy, cw, ch, t["segR"], t["ink"] if sel else None, None, bd)
            dx, dy = cx + (cw - 28) / 2, cy + 8                 # the device: 28x38, border 1.5, radius 4
            P.box(cr, dx, dy, 28, 38, "4px", None, None, "1.5px solid " + fg)
            P.box(cr, dx + 9, dy + 38 + 6 - 2.5, 10, 2.5, "1px", fg)       # the button bar under it
            f = _fsz(t["rowFont"], 16)
            tw, th = c.m("T", f, weight=700)
            cr.save()
            cr.translate(dx + 14, dy + 19)
            cr.rotate(math.radians(d))
            c.txt("T", f, -tw / 2, -th / 2, fg, weight=700)
            cr.restore()
            fw = 600 if sel or now else 400
            ny = dy + 38 + 5 + 6
            nf = _fsz(t["dateFont"], 13)
            nh = c.txt(self.NAMES[i], nf, cx + cw / 2, ny, fg, weight=fw, lh=1.2, align=1, maxw=cw - 8)[1]
            if w >= 480 or now or sel:
                c.txt("Now" if now else "Locked" if sel else self.SIDES[i], _fsz(t["dateFont"], 11),
                      cx + cw / 2, ny + nh + 6, fg, lh=1.2, align=1, maxw=cw - 8)
            c.hit(cx, cy, cw, ch, (lambda d=d: self.fn(d)))
        P.rule(cr, x, y + self.height(c, w), w, t["sep"])

    def summ(self):
        cur = self.sensor if self.auto else self.lock
        return ("Auto · " if self.auto else "Locked · ") + self.NAMES[(cur // 90) % 4]


class Themes(Block):
    """theme tiles: three side by side (a miniature bar + card + button in each
    theme, the name, a byline, the state) or, compact (a narrow or low
    page), ONE theme as a row: a small preview, the name / byline / state, a radio.
    Compact rows are one block each, so a page can fold and page them."""
    KIND = "themes"

    def __init__(self, tiles, compact=False):
        self.tiles, self.compact = tiles, compact     # [(id, name, theme dict, on, fn)]

    def th(self, c):
        return 120 if c.narrow else 250     # narrow: the bar and the top of the card

    def _byd(self, c, w):
        return not self.compact or not (w < 300 or c.short)

    def _text_h(self, c, x_t, name, by, w, gap):
        t = c.t
        h = c.m(name, x_t["nameFont"], maxw=w, wrap=True)[1]
        if by:
            h += gap + c.m(by, t["dateFont"], maxw=w, wrap=True, lh=1.35)[1]
        return h + gap + c.m("Tap to use", t["dateFont"], maxw=w, wrap=True, lh=LH_SUB)[1]

    def _text(self, c, x_t, name, by, on, x, y, w, gap):
        t = c.t
        y += c.txt(name, x_t["nameFont"], x, y, maxw=w, wrap=True)[1] + gap
        if by:
            y += c.txt(by, t["dateFont"], x, y, t["ink"], maxw=w, wrap=True, lh=1.35)[1] + gap
        c.txt("In use" if on else "Tap to use", t["dateFont"], x, y, t["sub"], maxw=w, wrap=True, lh=LH_SUB)

    def _cmp(self, c, w):
        pw = 64 if w < 300 else 72 if c.short else 92
        ph, rp = (44, 8) if c.short else (60, 12)
        return pw, ph, rp, w - pw - 16 - 16 - 26

    def height(self, c, w):
        if self.compact:
            pw, ph, rp, tw = self._cmp(c, w)
            tid, name, x_t, on, fn = self.tiles[0]
            by = THEME_BY.get(tid) if self._byd(c, w) else None
            return 6 + rp + max(ph, self._text_h(c, x_t, name, by, tw, 4), 26) + rp + _bw(c.t, "sep")
        tw = (w - 2 * 18) / 3
        return 20 + self.th(c) + 12 + max(self._text_h(c, x_t, name, THEME_BY.get(tid), tw, 2) for tid, name, x_t, on, fn in self.tiles)

    def draw(self, c, x, y, w):
        if self.compact:
            return self._draw_row(c, x, y, w)
        t, cr = c.t, c.cr
        th = self.th(c)
        tw = (w - 2 * 18) / 3
        for i, (tid, name, x_t, on, fn) in enumerate(self.tiles):
            tx, ty = x + i * (tw + 18), y + 20
            ink = t["ink"]
            ring = "0 0 0 3px %s,0 0 0 6px #ffffff,0 0 0 7px %s" % (ink, ink) if on else "0 0 0 1px " + ink
            P.box(cr, tx, ty, tw, th, "4px", "#ffffff", ring)
            cr.save()
            P.rrect(cr, tx, ty, tw, th, 4)
            cr.clip()
            P.fill(cr, tx, ty, tw, 30, x_t["barBg"])
            bb = P.border(x_t["barBorder"])
            if bb:
                P.rule(cr, tx, ty + 30 + bb[0], tw, x_t["barBorder"])
            if x_t["stitch"] != "none":
                for sy in (ty + 4, ty + 26 - 1):
                    P.rule(cr, tx + 4, sy + 1, tw - 8, "1px dashed rgba(255,255,255,.6)")
            P.box(cr, tx + 18, ty + 52, tw - 36, 120, x_t["cardR"], "#ffffff", x_t["cardSh"], x_t["cardBorder"])
            c.txt("Aa", x_t["headFont"], tx + 30, ty + 64, "#111111")
            ah = c.m("Aa", x_t["headFont"])[1]
            P.fill(cr, tx + 30, ty + 64 + ah + 9, (tw - 60) * 0.8, 2, "#111111")
            P.fill(cr, tx + 30, ty + 64 + ah + 20, (tw - 60) * 0.55, 2, "#111111")
            P.box(cr, tx + 18, ty + th - 22 - 22, 48, 22, x_t["btnR"], x_t["btnBg"], x_t["btnSh"])
            cr.restore()
            self._text(c, x_t, name, THEME_BY.get(tid), on, tx, ty + th + 12, tw, 2)
            c.hit(tx, ty, tw, self.height(c, w) - 20, fn)

    def _draw_row(self, c, x, y, w):
        t, cr = c.t, c.cr
        tid, name, x_t, on, fn = self.tiles[0]
        pw, ph, rp, tw = self._cmp(c, w)
        by = THEME_BY.get(tid) if self._byd(c, w) else None
        h = self.height(c, w)
        ry = y + 6
        mid = ry + (h - 6 - _bw(t, "sep")) / 2
        px_, py = x, mid - ph / 2
        P.box(cr, px_, py, pw, ph, "3px", "#ffffff", "0 0 0 1px " + t["ink"])
        cr.save()
        P.rrect(cr, px_, py, pw, ph, 3)
        cr.clip()
        P.fill(cr, px_, py, pw, 12, x_t["barBg"])
        bb = P.border(x_t["barBorder"])
        if bb:
            P.rule(cr, px_, py + 12 + bb[0], pw, x_t["barBorder"])
        P.box(cr, px_ + 8, py + 17, pw - 16, ph - 17 - 6, "3px", "#ffffff", None, x_t["cardBorder"])
        af = _fs(x_t["headFont"], 15)
        ah = c.m("Aa", af)[1]
        c.txt("Aa", af, px_ + 8 + 6, py + 17 + (ph - 23 - ah) / 2, "#111111")
        cr.restore()
        tx = x + pw + 16
        self._text(c, x_t, name, by, on, tx, mid - self._text_h(c, x_t, name, by, tw, 4) / 2, tw, 4)
        rx = x + w - 26
        P.box(cr, rx, mid - 13, 26, 26, "50%", None, None, "2px solid " + t["ink"])
        if on:
            P.box(cr, rx + 7, mid - 6, 12, 12, "50%", t["ink"])
        P.rule(cr, x, y + h, w, t["sep"])
        c.hit(x, ry, w, h - 6, fn)

    def summ(self):
        return next((name for tid, name, x_t, on, fn in self.tiles if on), None)


class Curve(Block):
    """today's schedule: a title + aside, per row a label/range and 24 hour bars
    (past hours filled, the rest outlined), an hour axis"""
    KIND = "curve"

    def __init__(self, title, aside, rows, now):
        self.title, self.aside, self.rows, self.now = title, aside, rows, now   # rows [(label, range, [24 x 0..100])]

    def bar_h(self, c):
        return 22 if c.short else 36

    def _title(self, c):
        t = c.t
        a = self.aside or "x"
        return _brow((_base(c, self.title, t["lblFont"], caps=c.lbl_caps()), c.m(self.title, t["lblFont"], caps=c.lbl_caps())[1]),
                     (_base(c, a, t["dateFont"], LH_SUB), c.m(a, t["dateFont"], lh=LH_SUB)[1]))

    def _row(self, c, label, rng):
        t = c.t
        return _brow((_base(c, label, t["rowFont"], LH_LBL), c.m(label, t["rowFont"], lh=LH_LBL)[1]),
                     (_base(c, rng, t["valFont"]), c.m(rng, t["valFont"])[1]))

    def height(self, c, w):
        t = c.t
        rows = sum(self._row(c, l, r)[1] + 6 + self.bar_h(c) + 10 for l, r, _ in self.rows)
        return 14 + self._title(c)[1] + 10 + rows + c.m("00", t["pageFont"])[1] + 12 + _bw(t, "sepSoft")

    def draw(self, c, x, y, w):
        t, cr = c.t, c.cr
        (to, ao), th = self._title(c)
        c.txt(self.title, t["lblFont"], x, y + 14 + to, t["sub"], caps=c.lbl_caps(), ls=P.ls_em(t["lblLs"]))
        if self.aside:
            c.txt(self.aside, t["dateFont"], x + w, y + 14 + ao, t["sub"], align=2, lh=LH_SUB)
        yy = y + 14 + th + 10
        bh = self.bar_h(c)
        bw = (w - 23 * 2) / 24
        for label, rng, vals in self.rows:
            (lo, vo), rh = self._row(c, label, rng)
            c.txt(label, t["rowFont"], x, yy + lo, lh=LH_LBL)
            c.txt(rng, t["valFont"], x + w, yy + vo, align=2)
            yy += rh + 6
            for i, v in enumerate(vals):
                h = max(3, round(v * bh / 100))
                bx = x + i * (bw + 2)
                if i <= int(self.now):
                    P.fill(cr, bx, yy + bh - h, bw, h, t["ink"])
                else:
                    P.box(cr, bx, yy + bh - h, bw, h, "0", None, "inset 0 0 0 1px " + t["ink"])
            yy += bh + 10
        labels = ["00", "06", "12", "18", "24"]               # justify-content: space-between
        ws = [c.m(s, t["pageFont"])[0] for s in labels]
        gap = (w - sum(ws)) / (len(labels) - 1)
        ax = x
        for s, sw in zip(labels, ws):
            c.txt(s, t["pageFont"], ax, yy, t["sub"])
            ax += sw + gap
        P.rule(cr, x, y + self.height(c, w), w, t["sepSoft"])


    def summ(self):
        return self.aside


class Offset(Block):
    """the user's shift of a schedule: label / sub, the value now (big), a groove
    -50..+50 with a centre mark (tap sets, steps of 5), Reset"""
    KIND = "offset"

    def __init__(self, label, sub, val, off, set_fn):
        self.label, self.sub, self.val, self.off, self.set_fn = label, sub, val, off, set_fn

    def _top(self, c, w):
        """the label/sub column | the big value, on one baseline"""
        t = c.t
        blh = P.font(t["bigFont"])[1]
        vw, vh = c.m(self.val, t["bigFont"], lh=blh)
        cw = w - vw - 12
        ch = _col_h(c, self.label, self.sub, cw, gap=2)
        return cw, blh, _brow((_base(c, self.label, t["rowFont"], LH_LBL), ch), (_base(c, self.val, t["bigFont"], blh), vh))

    def height(self, c, w):
        return 10 + self._top(c, w)[2][1] + 4 + 48 + 6 + _bw(c.t, "sep")

    def draw(self, c, x, y, w):
        t = c.t
        cw, blh, ((lo, vo), top_h) = self._top(c, w)
        _col(c, self.label, self.sub, x, y + 10 + lo, cw, gap=2)
        c.txt(self.val, t["bigFont"], x + w, y + 10 + vo, align=2, lh=blh)
        gy = y + 10 + top_h + 4
        rw, rh = c.m("Reset", t["btnFont"])
        rw += 48
        gw = w - 14 - rw
        P.box(c.cr, x, gy + P.px(t["grooveTop"]), gw, P.px(t["grooveH"]), t["grooveR"], t["grooveBg"], t["grooveSh"])
        P.fill(c.cr, x + gw / 2 - 1, gy + 8, 2, 30, t["ink"])
        v = self.off
        if v:
            P.fill(c.cr, x + gw * min(50, 50 + v) / 100, gy + P.px(t["fillTop"]), gw * abs(v) / 100, P.px(t["fillH"]), t["ink"])
        kx = x + gw * (50 + v) / 100 + P.px(t["knobML"])
        P.box(c.cr, kx, gy + P.px(t["knobTop"]), P.px(t["knobW"]), P.px(t["knobH"]), t["knobR"], t["knobBg"], None, t["knobBd"])
        bx = x + w - rw
        _btn(c, bx, gy, rw, 48)
        c.txt("Reset", t["btnFont"], bx + 24, gy + (48 - rh) / 2, t["ink"] if v else OFF)
        if v:
            c.hit(bx, gy, rw, 48, lambda: self.set_fn(0))
        P.rule(c.cr, x, y + self.height(c, w), w, t["sep"])

        def tap(ex, ey, x=x, gw=gw):
            self.set_fn(max(-50, min(50, round(((ex - x) / gw * 100 - 50) / 5) * 5)))
        tap.xy = True
        c.hits.append(((x - 10, gy, gw + 20, 48), tap))

    def summ(self):
        return self.val
