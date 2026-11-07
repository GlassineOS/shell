"""glassine-setup: the first-run setup (design "E-ink Shell":
EinkSetup), full screen before the shell. Eight steps in the header (Welcome, Look,
Location, Keyboard, Network, Account, Software, Review), then Installing and Done.
Nothing is written until Install (Wi-Fi excepted: the packs need it, the design's
Connect joins at once). E-ink only: the Look step offers the three voices, no pen or
scheme.

Drawing and the voices are GlassineOS/ui's (glassine_ui: CSS-like boxes, Pango text, symbolic icons).
Every paint is a panel update, so the window redraws once per change.

    glassine-setup [--demo]                     the app (--demo: sample data, nothing applied)
    glassine-setup --demo --render OUT.png [--step S] [--theme V] [--size WxH]
                   [--layout ID] [--packs a,b] [--joined SSID] [--big]
"""
import math
import sys

import cairo
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gdk, GLib, Gtk  # noqa: E402

from glassine_ui import paint as P  # noqa: E402

from . import data as D  # noqa: E402
from . import needs as needs_mod  # noqa: E402
from .theme import NAMES, THEMES, eyebrow  # noqa: E402

W0, H0 = 702, 936                       # the panel (the design's device frame)
STEPS = needs_mod.ALL_STEPS                       # every step; Setup.steps = the ones still needed
SNAMES = {"welcome": "Welcome", "look": "Look", "location": "Location", "keyboard": "Keyboard", "network": "Network",
          "account": "Account", "software": "Software", "review": "Review"}
PAGES = {
    "welcome": ("Welcome to GlassineOS", "A Linux system for e-ink tablets. Setup takes about five minutes, and nothing is written until the last step."),
    "look": ("Choose a look", "The voice sets type, edges and words. This page already follows your choice."),
    "location": ("Where are you?", "Sets the time zone, the clock and how dates and numbers are written."),
    "keyboard": ("Keyboard layout", "For an attached keyboard and the on-screen one. Keys you type light up below."),
    "network": ("Connect to Wi-Fi", "Needed for software packs and the time. You can skip this and connect later."),
    "account": ("Your account", "The only account on the tablet. Its password guards your files."),
    "software": ("Software packs", "Pick what you want installed now. %d selected."),
    "review": ("Set up this tablet?", "Nothing changes until you tap Install. Tap a row to change it."),
    "installing": ("Setting up", "About six minutes. Keep the tablet on charge or above 30 %."),
}
VDESC = {"white": "The instrument. Plex Sans, ink keylines, a highlighter for what is current.",
         "editorial": "The journal. Source Serif, double rules, rubricated labels, square corners.",
         "stationery": "The desk. Garamond on laid paper, a leather bar with gilt stitching."}
TIPS = [("swipe_up", "Swipe three fingers up for the overview of your screens"),
        ("touch_app", "Hold an app in the drawer for its menu and settings"),
        ("palette", "Settings › Appearance changes the voice")]
DONE_EYEBROW = {"white": "Ready", "editorial": "finis", "stationery": "the desk is laid"}
SIG = ["", "network_wifi_1_bar", "network_wifi_2_bar", "network_wifi_3_bar", "network_wifi"]
COMMON = {"password", "12345678", "123456789", "qwertyui", "iloveyou", "sunshine", "princess", "password1", "glassine"}


def fsz(f, scale):
    """a CSS font at scale (Larger text)"""
    import re
    return re.sub(r"[\d.]+px", lambda m: "%gpx" % (float(m.group(0)[:-2]) * scale), f, count=1) if scale != 1 else f


class State:
    """the choices; nothing here touches the system"""

    def __init__(self):
        self.step = "welcome"
        self.voice = "white"
        self.lang, self.big = "en-GB", False
        self.query, self.city = "Dres", 0
        self.ntp, self.clock = True, "24"
        self.layout, self.osk = "de", True
        self.tried = ""                                 # typed into Try it
        self.net, self.joined, self.wpw, self.show_pw = "Fernweh", None, "correct-horse-7", False
        self.name, self.user, self.host = "Anna Krause", "anna", "anna-note"
        self.pw = self.pw2 = "Sternenstaub!2026"
        self.admin, self.auto = True, False
        self.apw = "glassine"                           # root's password (the image's default)
        self.packs = ["reading", "notes", "containers"]
        self.focus = None                               # the text field keys go to
        self.inst_i = 4                                 # Installing: the step running (demo)
        self.net_err = ""                               # the last Connect's error
        self.kfocus = None                              # Tab focus: an index into the drawn targets (t13)

    def pw_rules(self):
        return [(len(self.pw) >= 8, "At least 8 characters"), (bool(self.pw) and self.pw == self.pw2, "Both entries match"),
                (self.pw.lower() not in COMMON, "Not on the list of common passwords")]

    def account_ok(self):
        import re
        return bool(self.name.strip() and re.fullmatch(r"[a-z_][a-z0-9_-]{0,31}", self.user) and
                    re.fullmatch(r"[a-z0-9][a-z0-9-]{0,62}", self.host) and all(ok for ok, _ in self.pw_rules()) and
                    len(self.apw) >= 8 and ":" not in self.apw)


class Setup:
    """the app's state + its actions (pages call back into it)"""

    def __init__(self, be, redraw):
        self.be, self.s, self.redraw = be, State(), redraw
        be.app = self
        self.packs = be.packs()
        self.needs = be.needs()                         # what is missing (needs.py): only those steps are asked
        self.steps = needs_mod.steps(self.needs)
        be.init(self.s)

    def go(self, step):
        if self.s.focus:
            self.be.osk(False)
        self.s.step, self.s.focus, self.s.kfocus = step, None, None
        if step == "installing":
            self.be.apply(self.s, self.packs, self.redraw)
        self.redraw()
        self.be.refresh_screen()

    def set(self, **kw):
        if kw.get("focus") and kw["focus"] != self.s.focus:
            self.be.osk(True)            # the fields are drawn, not GTK entries: no text-input auto-show
        for k, v in kw.items():
            setattr(self.s, k, v)
        self.redraw()

    def flip(self, k):
        setattr(self.s, k, not getattr(self.s, k))
        self.redraw()

    def toggle_pack(self, pid):
        p = self.s.packs
        self.s.packs = [x for x in p if x != pid] if pid in p else p + [pid]
        self.redraw()

    def key(self, ch=None, back=False):
        f = self.s.focus
        if not f:
            return
        v = getattr(self.s, f)
        setattr(self.s, f, v[:-1] if back else v + ch)
        self.redraw()

    def chosen(self):
        return [p for p in self.packs if p["id"] in self.s.packs]


class Renderer:
    def __init__(self, app):
        self.app = app
        self.hits = []

    # -- helpers
    def hit(self, x, y, w, h, fn):
        if fn:
            self.hits.append(((x, y, w, h), fn))

    def at(self, x, y):
        for (hx, hy, hw, hh), fn in reversed(self.hits):
            if hx <= x < hx + hw and hy <= y < hy + hh:
                return (hx, hy, hw, hh), fn
        return None, None

    def f(self, key):
        return fsz(self.k[key], self.scale)

    def txt(self, s, f, x, y, col=None, **kw):
        return P.text(self.cr, s, f, x, y, col or self.k["ink"], **kw)

    def m(self, s, f, **kw):
        return P.measure(self.cr, s, f, **kw)

    def icon(self, name, sz, cx, cy, col=None):
        return P.icon(self.cr, name, sz, cx, cy, col or self.k["ink"], self.dscale)

    def lbl(self, s, x, y, w=None, sep=False):
        """a section label (the voice's lbl: mono caps / small caps / gold), optionally over a rule"""
        k = self.k
        caps = (k["lblVar"], k["lblCase"])
        h = self.m(s, self.f("lbl"), caps=caps)[1]
        self.txt(s, self.f("lbl"), x, y, k["lblC"], caps=caps, ls=P.ls_em(k["lblLs"]))
        if sep:
            P.rule(self.cr, x, y + h + 6, w, k["sepStrong"])
            return h + 6 + P.border(k["sepStrong"])[0]
        return h

    def toggle(self, x, cy, on, fn):
        k = self.k
        P.box(self.cr, x, cy - 16, 56, 32, "16px", k["accent"] if on else k["paper"], "inset 0 0 0 1.5px " + k["ink"])
        P.box(self.cr, x + (27 if on else 3), cy - 13, 26, 26, "13px", k["onAccent"] if on else k["ink"])
        self.hit(x - 8, cy - 24, 72, 48, fn)

    def button(self, x, y, label, icon=None, fn=None, primary=False, icon_after=False):
        """-> its width"""
        k = self.k
        f = self.f("btnF")
        lw, lh = self.m(label, f)
        if primary:
            pad_l, pad_r = 22, 16
        else:
            pad_l, pad_r = (12, 18) if icon else (14, 14)
        w = pad_l + lw + pad_r + (22 + 6 if icon else 0)
        bg, fg, sh = (k["priBg"], k["priFg"], k["priSh"]) if primary else (k["btnBg"], k["ink"], k["btnSh"])
        P.box(self.cr, x, y, w, 48 if primary or icon else 40, k["btnR"], k["paper"] if bg == "transparent" else bg, sh)
        h = 48 if primary or icon else 40
        tx = x + pad_l
        if icon and not icon_after:
            self.icon(icon, 22, tx + 11, y + h / 2, fg)
            tx += 22 + 6
        self.txt(label, f, tx, y + (h - lh) / 2, fg)
        if icon and icon_after:
            self.icon(icon, 22, tx + lw + 6 + 11, y + h / 2, fg)
        self.hit(x, y, w, h, fn)
        return w

    def button_w(self, label, icon=None, primary=False):
        lw = self.m(label, self.f("btnF"))[0]
        return (22 + 16 if primary else (12 + 18 if icon else 28)) + lw + (22 + 6 if icon else 0)

    def row_bg(self, x, y, w, h, on, r=None):
        k = self.k
        st = k["sel"] if on else None
        if st:
            P.box(self.cr, x, y, w, h, r or k["rowR"], k["paper"] if st["bg"] == "transparent" else st["bg"],
                  None if st["sh"] == "none" else st["sh"])
        return (st or {}).get("fg", k["ink"])

    def field(self, x, y, w, h, value, focus, icon=None, ls=0, right=None, fn=None, bg=None):
        k = self.k
        P.box(self.cr, x, y, w, h, k["fieldR"], k["paper"] if (bg or k["fieldBg"]) == "transparent" else (bg or k["fieldBg"]),
              k["fieldFocus"] if focus else k["field"])
        tx = x + 14
        if icon:
            self.icon(icon, 20, tx + 10, y + h / 2)
            tx += 20 + 10
        f = self.f("row")
        vw, vh = self.m(value or " ", f)
        vw = vw + len(value or "") * ls * P.font(f)[0].get_size() / 1024 if value else 0
        self.txt(value, f, tx, y + (h - vh) / 2, ls=ls, maxw=w - (tx - x) - 50)
        if focus:
            P.fill(self.cr, tx + vw + 2, y + h / 2 - 11, 2, 22, k["pen"])
        self.hit(x, y, w, h, fn)                         # before right: its own targets (the eye) win
        if right:
            right()

    def eye(self, rx, y, h):
        """a password field's show/hide button at its right end"""
        s = self.app.s
        self.icon("visibility_off" if s.show_pw else "visibility", 22, rx - 14 - 11, y + h / 2)
        self.hit(rx - 48, y, 48, h, lambda: self.app.flip("show_pw"))

    # -- the window
    def draw(self, cr, W, H, scale=2):
        app = self.app
        s = app.s
        self.k, self.cr, self.dscale, self.hits = THEMES[s.voice], cr, scale, []
        self.scale = 1.15 if s.big and s.step != "welcome" else 1
        k = self.k
        P.fill(cr, 0, 0, W, H, k["paper"])
        hh = P.px(k["headH"])
        self.header(0, 0, W, hh)
        foot = s.step != "installing"
        fh = 84 if foot else 0
        cr.save()
        cr.rectangle(0, hh, W, H - hh - fh)
        cr.clip()
        x, y, w = 32, hh + P.border(k["headRule"])[0] if P.border(k["headRule"]) else hh, W - 64
        y += 26
        if s.step != "done":
            y = self.page_head(x, y, w)
        getattr(self, "p_" + s.step)(x, y, w, H - fh - 18)
        cr.restore()
        if foot:
            self.footer(0, H - fh, W, fh)
        self.ring(hh)

    def ring(self, head_h):
        """t13: the Tab focus, a 2 px ring 2 px outside the control; on the stationery
        header focus-on-leather, elsewhere focus"""
        i = self.app.s.kfocus
        if i is None or not self.hits:
            return
        (x, y, w, h), _ = self.hits[i % len(self.hits)]
        il = self.k["il"]
        col = il["focus-on-leather"] if y < head_h and self.k["voice"] == "stationery" else il["focus"]
        fr = self.k["inline"]
        o = P.px(fr["focusOff"]) + P.px(fr["focusW"])
        P.box(self.cr, x - o, y - o, w + 2 * o, h + 2 * o, fr["focusR"], None, "inset 0 0 0 %s %s" % (fr["focusW"], col))

    def header(self, x, y, w, h):
        k, s = self.k, self.app.s
        P.fill(self.cr, x, y, w, h, k["headBg"])
        if k["headRule"] != "none":
            P.rule(self.cr, x, y + h, w, k["headRule"])
        if k["stitch"] != "none":
            for sy in (y + 7, y + h - 8.5):
                P.rule(self.cr, x + 7, sy, w - 14, "1.5px dashed " + k["il"]["gilt"])
        fg = k["headFg"]
        self.logo(x + 24, y + (h - 34) / 2, 34, fg)
        tx = x + 24 + 34 + 14
        bf, sf = k["brand"], k["hsub"]
        bh = self.m("GlassineOS", bf, caps=(k["brandVar"], "none"))[1]
        steps = self.app.steps
        si = steps.index(s.step) if s.step in steps else -1
        sub = ("First-run setup" if k["voice"] == "white" else "first-run setup") if si >= 0 else \
            "Installing" if s.step == "installing" else "Setup complete"
        sh = self.m(sub, sf)[1]
        ty = y + (h - bh - 1 - sh) / 2
        self.txt("GlassineOS", bf, tx, ty, k["brandC"], caps=(k["brandVar"], "none"), ls=P.ls_em(k["brandLs"]))
        self.txt(sub, sf, tx, ty + bh + 1, fg)
        right = x + w - 24
        if si >= 0:
            nums = k["nums"][:len(steps)]
            sw = P.px(k["stepW"])
            ws = [max(sw, self.m(n, k["stepFont"])[0] + 8) for n in nums]
            sx = right - sum(ws) - 6 * 7
            for i, n in enumerate(nums):
                st = k["stepOn"] if i == si else k["stepDone"] if i < si else k["stepTodo"]
                bg = k["headBg"] if st["bg"] == "transparent" else st["bg"]   # paints over an outer shadow, as CSS
                sh_ = None if st["sh"] == "none" else st["sh"]
                P.box(self.cr, sx, y + h / 2 - 16, ws[i], 32, k["stepR"], bg, sh_)
                nh = self.m(n, k["stepFont"])[1]
                self.txt(n, k["stepFont"], sx + ws[i] / 2, y + h / 2 - nh / 2, st["fg"], align=1)
                if i <= si:                           # a finished step can be tapped to go back
                    self.hit(sx - 3, y + h / 2 - 24, ws[i] + 6, 48, (lambda st_=steps[i]: self.app.go(st_)))
                sx += ws[i] + 6
        else:
            b = self.app.be.battery()
            if b:
                bw = self.m(b, k["mono"])[0]
                self.txt(b, k["mono"], right - bw, y + h / 2 - self.m(b, k["mono"])[1] / 2, fg)
                self.icon("battery_5_bar", 20, right - bw - 6 - 10, y + h / 2, fg)

    def logo(self, x, y, size, fg):
        """the GlassineOS mark: a sheet outline, a highlighted sheet over it, a pen stroke"""
        cr, k = self.cr, self.k
        sc = size / 48
        cr.save()
        cr.translate(x, y)
        cr.scale(sc, sc)
        P.rrect(cr, 2, 4, 30, 34, 5)
        cr.set_source_rgba(*P.color(fg))
        cr.set_line_width(2.5 if size < 60 else 2)
        cr.stroke()
        cr.translate(29, 27)
        cr.rotate(math.radians(6))
        cr.translate(-29, -27)
        P.rrect(cr, 14, 10, 30, 34, 5)
        cr.set_source_rgba(*P.color(k["highlight"]))
        cr.fill_preserve()
        if size >= 60:
            cr.set_source_rgba(*P.color(k["ink"]))
            cr.set_line_width(1)
            cr.stroke()
        else:
            cr.new_path()
        cr.move_to(20, 30)
        cr.curve_to(26, 24, 30, 34, 38, 26)
        cr.set_source_rgba(*P.color(k["pen"]))
        cr.set_line_width(3 if size < 60 else 2.5)
        cr.set_line_cap(cairo.LINE_CAP_ROUND)
        cr.stroke()
        cr.restore()

    def page_head(self, x, y, w):
        k, s = self.k, self.app.s
        steps = self.app.steps
        si = steps.index(s.step) if s.step in steps else -1
        eb = eyebrow(k["voice"], si + 1, SNAMES[s.step], len(steps)) if si >= 0 else ("Installing" if k["voice"] == "white" else "installing")
        y += self.lbl(eb, x, y) + 8
        title, lead = PAGES[s.step]
        if s.step == "software":
            lead = lead % len(s.packs)
        y += self.txt(title, self.f("display"), x, y, maxw=w, wrap=True, ls=P.ls_em(k["displayLs"]))[1] + 8
        y += self.txt(lead, self.f("lead"), x, y, k["soft"], maxw=min(w, 600), wrap=True)[1]
        return y + 16

    def footer(self, x, y, w, h):
        k, s, app = self.k, self.app.s, self.app
        P.fill(self.cr, x, y, w, h, k["paper"])
        P.rule(self.cr, x, y, w, k["footRule"])
        steps = self.app.steps
        si = steps.index(s.step) if s.step in steps else -1
        by = y + (h - 48) / 2
        lx, rx = x + 32, x + w - 32
        if si > 0:
            lx += self.button(lx, by, "Back", "arrow_back", lambda: app.go(steps[si - 1])) + 14
        if s.step == "review":
            nl, ni, nf = "Install", "download", lambda: app.go("installing")
        elif s.step == "done":
            nl, ni, nf = "Start", "arrow_forward", app.be.finish
        else:
            nl, ni = ("Begin" if s.step == "welcome" else "Continue"), "arrow_forward"
            nf = (lambda: app.go(steps[si + 1])) if s.step != "account" or s.account_ok() else None
        nw = self.button_w(nl, ni, True)
        if nf is None:                                   # Continue waits for a valid account
            P.box(self.cr, rx - nw, by, nw, 48, k["btnR"], None, "inset 0 0 0 1.5px " + k["il"]["dim"])
            lh = self.m(nl, self.f("btnF"))[1]
            self.txt(nl, self.f("btnF"), rx - nw + 22, by + (48 - lh) / 2, k["il"]["dim"])
            self.icon(ni, 22, rx - 16 - 11, by + 24, k["il"]["dim"])
        else:
            self.button(rx - nw, by, nl, ni, nf, primary=True, icon_after=True)
        rx -= nw + 14
        if s.step in ("network", "software"):
            sl = "None for now" if s.step == "software" else "Skip"
            sf = self.f("btnF")
            sw_, sh_ = self.m(sl, sf)
            sx = rx - sw_ - 28
            self.txt(sl, sf, sx + 14, by + (48 - sh_) / 2)
            P.fill(self.cr, sx + 14, by + (48 + sh_) / 2 + 1, sw_, 1, k["ink"])
            fn = (lambda: (app.set(packs=[]), app.go("review"))) if s.step == "software" else (lambda: app.go("account"))
            self.hit(sx, by, sw_ + 28, 48, fn)
            rx = sx - 14
        status = {"software": "%d packs · %s download · %s free" % (len(s.packs), D.size(sum(p["mb"] for p in app.chosen())), app.be.free()),
                  "network": ("Connected to " + s.joined) if s.joined else s.net_err or "Not connected",
                  "review": "%d steps · %s" % (len(app.be.steps(app)), D.size(sum(p["mb"] for p in app.chosen())) + " download" if app.chosen() else "nothing to download"),
                  "done": "Restart is not needed"}.get(s.step, "")
        if status:
            sf = self.f("sub")
            tw, th = self.m(status, sf, maxw=rx - lx)
            self.txt(status, sf, lx + (rx - lx - tw) / 2, by + (48 - th) / 2, k["soft"], maxw=rx - lx)

    # -- the steps
    def p_welcome(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        y += self.lbl("Language", x, y, w, sep=True) + 14
        for code, native, en in D.LANGS:
            on = code == s.lang
            fg = self.row_bg(x, y, w, 54, on)
            rh = self.m(native, self.f("row"))[1]
            self.txt(native, self.f("row"), x + 16, y + (54 - rh) / 2, fg)
            if on:
                self.icon("check", 22, x + w - 16 - 12, y + 27, fg)
            else:
                eh = self.m(en, self.f("sub"))
                self.txt(en, self.f("sub"), x + w - 16 - 24 - 14, y + (54 - eh[1]) / 2, k["soft"], align=2)
            self.hit(x, y, w, 54, (lambda c=code: app.set(lang=c)))
            y += 54 + 4
        y += 10
        P.rule(self.cr, x, y, w, k["sep"])
        cy = y + 12 + 16
        self.icon("accessibility_new", 22, x + 11, cy)
        rh = self.m("Larger text", self.f("row"))[1]
        self.txt("Larger text", self.f("row"), x + 22 + 12, cy - rh / 2)
        n = "Applies from the next page"
        nh = self.m(n, self.f("sub"))[1]
        self.txt(n, self.f("sub"), x + w - 56 - 12, cy - nh / 2, k["soft"], align=2)
        self.toggle(x + w - 56, cy, s.big, lambda: app.flip("big"))

    def p_look(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        for v in ("white", "editorial", "stationery"):
            on = v == s.voice
            ch = 96 + 24
            P.box(self.cr, x, y, w, ch, k["cardR"], k["cardOnBg"] if on else k["paper"], k["cardOnSh"] if on else k["cardFlat"])
            self.mini(v, x + 12, y + 12)
            tx = x + 12 + 200 + 18
            vk = THEMES[v]
            nf = fsz(vk["packF"], 22 / P.font(vk["packF"])[0].get_size() * 1024)
            nh = self.m(NAMES[v], nf, caps=(vk["packVar"], "none"))[1]
            dw = w - (tx - x) - 12 - 24 - 18
            dh = self.m(VDESC[v], self.f("sub"), maxw=dw, wrap=True)[1]
            ty = y + (ch - nh - 4 - dh) / 2
            self.txt(NAMES[v], nf, tx, ty, caps=(vk["packVar"], "none"), ls=P.ls_em(vk["packLs"]))
            self.txt(VDESC[v], self.f("sub"), tx, ty + nh + 4, k["soft"], maxw=dw, wrap=True)
            if on:
                self.icon("check_circle", 24, x + w - 12 - 12, y + ch / 2)
            self.hit(x, y, w, ch, (lambda v=v: app.set(voice=v)))
            y += ch + 14
        self.txt("Everything here can be changed later in Settings › Appearance.", self.f("sub"), x, y + 4, k["soft"], maxw=w, wrap=True)

    def mini(self, v, x, y):
        """a voice's miniature (200 x 96): its bar, Aa, a label, a highlight, a button"""
        cr, vk = self.cr, THEMES[v]
        il, mi = vk["il"], vk["inline"]
        r = mi["miniR"]
        P.box(cr, x, y, 200, 96, r, il["paper"], "inset 0 0 0 1px " + il["ink"])
        cr.save()
        P.rrect(cr, x, y, 200, 96, P.px(r))
        cr.clip()
        P.fill(cr, x, y, 200, 24, mi["miniBarBg"])
        if mi["miniBarRule"] != "none":
            P.rule(cr, x, y + 24, 200, mi["miniBarRule"])
        if mi["miniStitch"] != "none":
            for sy in (y + 3, y + 20):
                P.rule(cr, x + 3, sy, 194, mi["miniStitch"])
        cr.restore()
        P.box(cr, x, y, 200, 96, r, None, "inset 0 0 0 1px " + il["ink"])
        tf = vk["display"]
        import re
        tf = re.sub(r"[\d.]+px(/[\d.]+)?", "40px/1", tf, count=1)
        self.txt("Aa", tf, x + 12, y + 32 - 4, il["ink"])
        lf = re.sub(r"[\d.]+px(/[\d.]+)?", mi["miniLblPx"], vk["lbl"], count=1)
        self.txt(mi["miniLbl"], lf, x + 62, y + 36, vk["lblC"], caps=(vk["lblVar"], vk["lblCase"]), ls=0.08)
        P.box(cr, x + 62, y + 54, 62, 10, mi["miniHlR"], il["highlight"])
        P.box(cr, x + 200 - 10 - 42, y + 96 - 10 - 22, 42, 22, vk["btnR"], il["accent"], mi["miniBtnSh"])

    def p_location(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        cities = app.be.places(s.query)

        def right():
            n = "%d places" % len(cities)
            nh = self.m(n, self.f("sub"))[1]
            self.txt(n, self.f("sub"), x + w - 16, y + 26 - nh / 2, k["soft"], align=2)
        self.field(x, y, w, 52, s.query, s.focus == "query" or s.focus is None, icon="search", right=right,
                   fn=lambda: app.set(focus="query"))
        y += 52 + 14
        for i, c in enumerate(cities[:4]):
            on = i == s.city
            rh = 60
            fg = self.row_bg(x, y, w, rh, on)
            self.icon("location_on", 22, x + 16 + 11, y + rh / 2, fg)
            nh = self.m(c[0], self.f("row"))[1]
            zh = self.m(c[2], self.f("sub"))[1]
            ty = y + (rh - nh - 2 - zh) / 2
            self.txt(c[0], self.f("row"), x + 16 + 22 + 14, ty, fg, maxw=w - 160)
            self.txt(c[2], self.f("sub"), x + 16 + 22 + 14, ty + nh + 2, k["soft"], maxw=w - 160)
            t = c[4] if s.clock == "12" else c[3]
            th = self.m(t, k["mono"])[1]
            self.txt(t, k["mono"], x + w - 16, y + (rh - th) / 2, fg, align=2)
            self.hit(x, y, w, rh, (lambda i=i: app.set(city=i)))
            y += rh + 4
        y += 10
        n = "Suggested from your network’s address. No location service is used."
        nh = self.m(n, self.f("sub"))[1]
        self.icon("wifi_find", 18, x + 9, y + nh / 2, k["soft"])
        self.txt(n, self.f("sub"), x + 18 + 8, y, k["soft"], maxw=w - 26)
        y += nh + 14 + 6
        y += self.lbl("Time and formats", x, y, w, sep=True)
        c = cities[s.city] if s.city < len(cities) else None
        tz = c[1] if c else "UTC"
        y = self.kv_row(x, y, w, 58, "Time zone", None, lambda cx, cy: self.mono_r(tz, cx, cy))
        y = self.kv_row(x, y, w, 58, "Set time automatically", "From the network once it is connected",
                        lambda cx, cy: self.toggle(cx - 56, cy, s.ntp, lambda: app.flip("ntp")))
        y = self.kv_row(x, y, w, 62, "Clock", None, lambda cx, cy: self.seg(cx, cy, [("24", "24-hour"), ("12", "12-hour")], s.clock,
                                                                           lambda v: app.set(clock=v)))
        fmt = "31.10.2026 · 1.234,56" if tz.startswith("Europe/") else "10/31/2026 · 1,234.56"
        self.kv_row(x, y, w, 58, "Formats", "Dates, numbers and the first day of the week",
                    lambda cx, cy: (self.icon("chevron_right", 22, cx - 11, cy), self.mono_r(fmt, cx - 22 - 12, cy)), last=True)

    def mono_r(self, t, rx, cy):
        th = self.m(t, self.k["mono"])[1]
        self.txt(t, self.k["mono"], rx, cy - th / 2, align=2)

    def kv_row(self, x, y, w, h, label, sub, right, last=False):
        k = self.k
        lh = self.m(label, self.f("row"))[1]
        sh = self.m(sub, self.f("sub"))[1] if sub else 0
        ty = y + (h - lh - (2 + sh if sub else 0)) / 2
        self.txt(label, self.f("row"), x, ty)
        if sub:
            self.txt(sub, self.f("sub"), x, ty + lh + 2, k["soft"])
        right(x + w, y + h / 2)
        if not last:
            P.rule(self.cr, x, y + h, w, k["sep"])
        return y + h + (P.border(k["sep"])[0] if not last else 0)

    def seg(self, rx, cy, opts, cur, fn):
        k = self.k
        f = self.f("row")
        ws = [self.m(lbl, f, weight=600 if v == cur else None)[0] + 32 for v, lbl in opts]
        tw = sum(ws) + 3 * (len(opts) + 1)
        x = rx - tw
        P.box(self.cr, x, cy - 23, tw, 46, k["btnR"], k["paper"], k["traySh"])
        sx = x + 3
        for (v, lbl), sw in zip(opts, ws):
            on = v == cur
            st = k["segOn"] if on else {"bg": "transparent", "fg": k["ink"], "sh": "none"}
            P.box(self.cr, sx, cy - 20, sw, 40, k["segR"], k["paper"] if st["bg"] == "transparent" else st["bg"],
                  None if st["sh"] == "none" else st["sh"])
            lh = self.m(lbl, f, weight=600 if on else None)[1]
            self.txt(lbl, f, sx + 16, cy - lh / 2, st["fg"], weight=600 if on else None)
            self.hit(sx, cy - 24, sw, 48, (lambda v=v: fn(v)))
            sx += sw + 3

    def p_keyboard(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        for lid, name, code in D.LAYOUTS:
            on = lid == s.layout
            fg = self.row_bg(x, y, w, 48, on)
            rh = self.m(name, self.f("row"))[1]
            self.txt(name, self.f("row"), x + 16, y + (48 - rh) / 2, fg)
            ch = self.m(code, k["mono"])[1]
            self.txt(code, k["mono"], x + w - 16 - 24 - 14, y + (48 - ch) / 2, k["soft"], align=2)
            if on:
                self.icon("check", 22, x + w - 16 - 12, y + 24, fg)
            self.hit(x, y, w, 48, (lambda lid=lid: app.set(layout=lid, tried="")))
            y += 48 + 3
        y += 9 + 4
        y += self.lbl("Try it", x, y) + 12
        test = s.tried or D.TEST[s.layout]
        self.field(x, y, w, 48, test, True, fn=lambda: app.set(focus="tried"))
        y += 48 + 12
        rows = D.ROWS[s.layout]
        typed = set(test.lower())
        kh = 12 + 3 * 40 + 2 * 6 + 12
        P.box(self.cr, x, y, w, kh, k["cardR"], k["sheet"], k["cardFlat"])
        ky = y + 12
        kw = min(44, (w - 24 - 36 - 5 * 10) / 11)
        for i, r in enumerate(rows):
            kx = x + 12 + [0, 14, 36][i]
            for ch in r:
                hot = ch in typed
                P.box(self.cr, kx, ky, kw, 40, k["keyR"], k["highlight"] if hot else k["paper"], k["keySh"])
                th = self.m(ch, self.f("keyF"))[1]
                self.txt(ch, self.f("keyF"), kx + kw / 2, ky + (40 - th) / 2, align=1)
                kx += kw + 5
            ky += 40 + 6
        y += kh + 12
        self.kv_row(x, y, w, 56, "On-screen keyboard", "Opens in text fields when no keyboard is attached",
                    lambda cx, cy: self.toggle(cx - 56, cy, s.osk, lambda: app.flip("osk")), last=True)

    def p_network(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        for ssid, sig, sec, lock in app.be.nets():
            on, joined = ssid == s.net, ssid == s.joined
            open_ = on and not joined and lock != "badge"
            h = 58 + (4 + 48 + 10 + self.m("x", self.f("sub"), maxw=w - 32, wrap=True)[1] * 2 + 16 if open_ else 0)
            if on:
                P.box(self.cr, x, y, w, h, k["rowR"], k["inline"]["netOnBg"], k["cardFlat"])
            self.icon(SIG[sig], 24, x + 16 + 12, y + 29)
            sub = ("Connected · " + sec.split(" · ")[0]) if joined else sec
            nh = self.m(ssid, self.f("row"))[1]
            sh = self.m(sub, self.f("sub"))[1]
            ty = y + (58 - nh - 1 - sh) / 2
            self.txt(ssid, self.f("row"), x + 16 + 24 + 14, ty)
            self.txt(sub, self.f("sub"), x + 16 + 24 + 14, ty + nh + 1, k["soft"])
            li = "check" if joined else lock
            if li:
                self.icon(li, 20, x + w - 16 - 10, y + 29, k["soft"])
            self.hit(x, y, w, 58, (lambda n=ssid: app.set(net=n, focus="wpw")))
            if open_:
                fy = y + 58 + 4
                cw_ = self.button_w("Connect", None, True)
                fw = w - 32 - 10 - cw_

                def eye(fx=x + 16, fy_=fy, fw_=fw):
                    self.eye(fx + fw_, fy_, 48)
                pwv = s.wpw if s.show_pw else "•" * len(s.wpw)
                self.field(x + 16, fy, fw, 48, pwv, s.focus in ("wpw", None), icon="key", ls=0 if s.show_pw else 0.18,
                           right=eye, fn=lambda: app.set(focus="wpw"), bg=k["paper"])
                self.button(x + w - 16 - cw_, fy, "Connect", None, lambda n=ssid: app.be.connect(n, s.wpw, app), primary=True)
                self.txt("Only used to fetch software packs and the time. The password is kept in the system keyring.",
                         self.f("sub"), x + 16, fy + 48 + 10, k["soft"], maxw=w - 32, wrap=True)
            y += h + 6
        P.rule(self.cr, x, y + 6, w, k["sep"])
        rh = self.m("Hidden network…", self.f("row"))[1]
        self.icon("add", 22, x + 16 + 11, y + 6 + 12 + rh / 2)
        self.txt("Hidden network…", self.f("row"), x + 16 + 22 + 12, y + 6 + 12)

    def p_account(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        lw = 132
        fx, fw = x + lw + 16, w - lw - 16
        pwv = s.pw if s.show_pw else "•" * len(s.pw)
        pw2v = s.pw2 if s.show_pw else "•" * len(s.pw2)
        rows = [("Your name", "name", s.name, "Shown on the lock screen"), ("Username", "user", s.user, "Your home folder: /home/" + s.user),
                ("Device name", "host", s.host, ""), ("Password", "pw", pwv, ""), ("Confirm", "pw2", pw2v, "")]
        focus = s.focus or "pw2"
        for label, fid, val, hint in rows:
            lh = self.m(label, self.f("row"))[1]
            self.txt(label, self.f("row"), x, y + (44 - lh) / 2)
            right = (lambda fy=y: self.eye(fx + fw, fy, 44)) if fid == "pw" else None
            self.field(fx, y, fw, 44, val, focus == fid, ls=0.12 if fid in ("pw", "pw2") and not s.show_pw else 0,
                       right=right, fn=lambda fid=fid: app.set(focus=fid))
            y += 44
            if hint:
                y += 4 + self.txt(hint, self.f("sub"), fx, y + 4, k["soft"])[1]
            y += 8
        rules = s.pw_rules()
        rh = sum(self.m(t, self.f("sub"))[1] for _, t in rules) + 3 * 2 + 20
        P.box(self.cr, fx, y + 2, fw, rh, k["cardR"], k["sheet"])
        ry = y + 2 + 10
        for ok, t in rules:
            th = self.m(t, self.f("sub"))[1]
            self.icon("check_circle" if ok else "radio_button_unchecked", 20, fx + 16 + 10, ry + th / 2)
            self.txt(t, self.f("sub"), fx + 16 + 20 + 10, ry, k["ink"] if ok else k["soft"])
            ry += th + 3
        y += 2 + rh + 8 + 4
        P.rule(self.cr, x, y, w, k["sep"])
        y += P.border(k["sep"])[0]
        y = self.kv_row(x, y, w, 56, "Use it for administrator tasks", "Software updates and system settings ask for it",
                        lambda cx, cy: self.toggle(cx - 56, cy, s.admin, lambda: app.flip("admin")))
        y = self.kv_row(x, y, w, 56, "Log in without the password", "Anyone who wakes the tablet gets to your files",
                        lambda cx, cy: self.toggle(cx - 56, cy, s.auto, lambda: app.flip("auto")))
        # root's password: the image's default is glassine
        y += 12
        label = "Admin password"
        lh = self.m(label, self.f("row"))[1]
        self.txt(label, self.f("row"), x, y + (44 - lh) / 2)
        apv = s.apw if s.show_pw else "•" * len(s.apw)
        self.field(fx, y, fw, 44, apv, focus == "apw", ls=0.12 if not s.show_pw else 0,
                   right=lambda fy=y: self.eye(fx + fw, fy, 44), fn=lambda: app.set(focus="apw"))
        hint = ("The system account's (root), for system tasks and the recovery. " +
                ("Still the default, glassine." if s.apw == "glassine" else "At least 8 characters." if len(s.apw) < 8 else "Changed from the default."))
        self.txt(hint, self.f("sub"), fx, y + 44 + 4, k["soft"], maxw=fw, wrap=True)

    def p_software(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        cw = (w - 12) / 2
        for i, p in enumerate(app.packs):
            cx, cy = x + (i % 2) * (cw + 12), y + (i // 2) * (100 + 12)
            on = p["id"] in s.packs
            P.box(self.cr, cx, cy, cw, 100, k["cardR"], k["cardOnBg"] if on else k["paper"], k["cardOnSh"] if on else k["cardFlat"])
            ix, iy = cx + 14, cy + 12
            pf = self.f("packF")
            nh = self.m(p["name"], pf, caps=(k["packVar"], "none"))[1]
            rowh = max(nh, 24)
            self.icon(p["icon"], 22, ix + 11, iy + rowh / 2)
            right = cx + cw - 14
            # the checkbox
            P.box(self.cr, right - 24, iy + rowh / 2 - 12, 24, 24, k["cbR"], k["accent"] if on else None,
                  None if on else "inset 0 0 0 1.5px " + k["ink"])
            if on:
                self.icon("check", 18, right - 12, iy + rowh / 2, k["onAccent"])
            right -= 24 + 10
            if p.get("rec"):
                tf = k["tagF"]
                caps = (k["lblVar"], k["lblCase"])
                tw, th = self.m(k["rec"], tf, caps=caps)
                tw += len(k["rec"]) * 0.06 * P.font(tf)[0].get_size() / 1024
                P.box(self.cr, right - tw - 12, iy + rowh / 2 - th / 2 - 2, tw + 12, th + 4, k["tagR"], k["highlight"])
                self.txt(k["rec"], tf, right - tw - 6, iy + rowh / 2 - th / 2, caps=caps, ls=0.06)
                right -= tw + 12 + 10
            self.txt(p["name"], pf, ix + 22 + 10, iy + (rowh - nh) / 2, caps=(k["packVar"], "none"), ls=P.ls_em(k["packLs"]),
                     maxw=right - (ix + 32))
            dy = iy + rowh + 5
            self.txt(p["desc"], self.f("sub"), ix, dy, maxw=cw - 28)
            sz = D.size1(p["mb"])
            szw, szh = self.m(sz, k["mono"])
            self.txt(sz, k["mono"], cx + cw - 14, cy + 100 - 12 - szh, align=2)
            ah = self.m(p["apps"], self.f("appsF"))[1]
            self.txt(p["apps"], self.f("appsF"), ix, cy + 100 - 12 - ah, k["soft"], maxw=cw - 28 - szw - 8)
            self.hit(cx, cy, cw, 100, (lambda pid=p["id"]: app.toggle_pack(pid)))
        y += ((len(app.packs) + 1) // 2) * 112 + 4
        self.txt("Packs install after setup and can be added or removed any time in Software. Containers start only when you use "
                 "them, so they don’t cost battery while the tablet sleeps.", self.f("sub"), x, y, k["soft"], maxw=w, wrap=True)

    def p_review(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        P.rule(self.cr, x, y, w, k["sepStrong"])
        y += P.border(k["sepStrong"])[0]
        city = app.be.places(s.query)
        c = city[s.city] if s.city < len(city) else None
        chosen = app.chosen()
        rows = [("translate", "Language", dict((a, b) for a, b, _ in D.LANGS)[s.lang], "welcome"),
                ("palette", "Look", NAMES[s.voice], "look"),
                ("schedule", "Location", "%s · %s · %s-hour" % (c[0].split(",")[0], c[1], s.clock) if c else "UTC", "location"),
                ("keyboard", "Keyboard", dict((a, b) for a, b, _ in D.LAYOUTS)[s.layout], "keyboard"),
                ("wifi", "Network", s.joined or "Not connected — packs install later", "network"),
                ("person", "Account", "%s (%s)%s%s · admin password %s" % (
                    s.name, s.user, " · administrator" if s.admin else "", " · no password at login" if s.auto else "",
                    "glassine (default)" if s.apw == "glassine" else "changed"), "account"),
                ("deployed_code", "Software", ", ".join(p["name"] for p in chosen) + " · " + D.size(sum(p["mb"] for p in chosen))
                 if chosen else "None", "software")]
        rows = [r for r in rows if r[3] in app.steps]
        caps = (k["lblVar"], k["lblCase"])
        for icon, label, val, step in rows:
            bw = self.button_w("Change")
            vw = w - 26 - 14 - 120 - 14 - 14 - bw
            vh = self.m(val, self.f("row"), maxw=vw, wrap=True)[1]
            h = max(58, 16 + vh)
            cy = y + h / 2
            self.icon(icon, 22, x + 11, cy)
            lh = self.m(label, self.f("lbl"), caps=caps)[1]
            self.txt(label, self.f("lbl"), x + 26 + 14, cy - lh / 2, k["lblC"], caps=caps, ls=P.ls_em(k["lblLs"]))
            self.txt(val, self.f("row"), x + 26 + 14 + 120 + 14, cy - vh / 2, maxw=vw, wrap=True)
            self.button(x + w - bw, cy - 20, "Change", None, lambda st=step: app.go(st))
            P.rule(self.cr, x, y + h, w, k["sep"])
            y += h + P.border(k["sep"])[0]
        y += 16
        n = "Setup writes only to the GlassineOS partition. Android and TWRP stay where they are: Power › Restart into Android still works afterwards."
        nh = self.m(n, self.f("sub"), maxw=w - 32 - 24 - 14, wrap=True)[1]
        P.box(self.cr, x, y, w, nh + 28, k["cardR"], k["paper"], k["noteSh"])
        self.icon("info", 24, x + 16 + 12, y + 14 + 12)
        self.txt(n, self.f("sub"), x + 16 + 24 + 14, y + 14, maxw=w - 32 - 24 - 14, wrap=True)

    def p_installing(self, x, y, w, bottom):
        k, app = self.k, self.app
        st = app.be.progress(app)
        th = self.m(st["now"], k["mono"])[1]
        self.txt(st["now"], k["mono"], x, y)
        self.txt(st["pct"], k["mono"], x + w, y, align=2)
        y += th + 18
        P.rule(self.cr, x, y, w, k["sepStrong"])
        y += P.border(k["sepStrong"])[0]
        for label, state, note in st["steps"]:
            icon = {"done": "check_circle", "now": "pending", "fail": "error"}.get(state, "radio_button_unchecked")
            fg = k["soft"] if state == "todo" else k["ink"]
            self.icon(icon, 22, x + 11, y + 26, fg)
            lh = self.m(label, self.f("row"), weight=600 if state == "now" else None)[1]
            self.txt(label, self.f("row"), x + 22 + 14, y + 26 - lh / 2, fg, weight=600 if state == "now" else None)
            if note:
                nh = self.m(note, k["mono"])[1]
                self.txt(note, k["mono"], x + w, y + 26 - nh / 2, fg, align=2)
            P.rule(self.cr, x, y + 52, w, k["sep"])
            y += 52 + P.border(k["sep"])[0]
        y += 18
        if st.get("failed"):
            self.install_failed(x, y, w, st["failed"])
            return
        t = "The screen redraws once per step, not continuously. You can put the tablet down; it won’t sleep until setup is done."
        nh = self.m(t, self.f("sub"), maxw=w - 32 - 24 - 14, wrap=True)[1]
        P.box(self.cr, x, y, w, nh + 28, k["cardR"], k["sheet"])
        self.icon("contrast", 24, x + 16 + 12, y + 14 + 12)
        self.txt(t, self.f("sub"), x + 16 + 24 + 14, y + 14, maxw=w - 32 - 24 - 14, wrap=True)

    def install_failed(self, x, y, w, why):
        """the apply stopped: why, Try again (a re-run is safe), and Start anyway once the account exists"""
        k, app = self.k, self.app
        t = "Setup stopped: " + why
        tw = w - 32 - 24 - 14
        nh = self.m(t, self.f("sub"), maxw=tw, wrap=True)[1]
        P.box(self.cr, x, y, w, nh + 28, k["cardR"], k["paper"], k["noteSh"])
        self.icon("warning", 24, x + 16 + 12, y + 14 + 12)
        self.txt(t, self.f("sub"), x + 16 + 24 + 14, y + 14, maxw=tw, wrap=True)
        y += nh + 28 + 18
        bx = x + self.button(x, y, "Try again", "restart_alt", lambda: app.go("installing")) + 14
        if app.be.account_made():
            self.button(bx, y, "Start anyway", "arrow_forward", app.be.finish)

    def p_done(self, x, y, w, bottom):
        k, s, app = self.k, self.app.s, self.app
        x, w = x + 12, w - 24
        first = s.name.split()[0] if s.name.split() else s.user
        n = app.be.installed(app)
        lead = ("%d packs installed. " % n if n else "") + "Your tablet is ready to read, write and work on."
        caps = (k["lblVar"], k["lblCase"])
        parts = [96, self.m(DONE_EYEBROW[k["voice"]], self.f("lbl"), caps=caps)[1],
                 self.m("All set, %s." % first, self.f("display"))[1], self.m(lead, self.f("lead"), maxw=w, wrap=True)[1],
                 3 * 58 + 3]
        total = sum(parts) + 22 * 4
        y = y + max(0, (bottom - y - total) / 2)
        self.logo(x, y, 96, k["ink"])
        y += 96 + 22
        self.txt(DONE_EYEBROW[k["voice"]], self.f("lbl"), x, y, k["lblC"], caps=caps, ls=P.ls_em(k["lblLs"]))
        y += parts[1] + 22
        self.txt("All set, %s." % first, self.f("display"), x, y, ls=P.ls_em(k["displayLs"]))
        y += parts[2] + 22
        self.txt(lead, self.f("lead"), x, y, k["soft"], maxw=w, wrap=True)
        y += parts[3] + 22
        P.rule(self.cr, x, y, w, k["sep"])
        for icon, label in TIPS:
            self.icon(icon, 24, x + 12, y + 29)
            lh = self.m(label, self.f("row"))[1]
            self.txt(label, self.f("row"), x + 24 + 14, y + 29 - lh / 2, maxw=w - 38)
            y += 58
            P.rule(self.cr, x, y, w, k["sep"])


class Window(Gtk.Window):
    def __init__(self, app):
        super().__init__(title="GlassineOS setup")
        self.app = app
        self.r = Renderer(app)
        self.set_default_size(W0, H0)
        self.area = Gtk.DrawingArea()
        self.area.set_can_focus(True)
        self.area.add_events(Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK | Gdk.EventMask.KEY_PRESS_MASK)
        self.area.connect("draw", lambda a, cr: (self.r.draw(cr, a.get_allocated_width(), a.get_allocated_height(),
                                                             a.get_scale_factor()), True)[1])
        self.area.connect("button-press-event", self.press)
        self.area.connect("button-release-event", self.release)
        self.connect("key-press-event", self.on_key)
        self.add(self.area)
        self.pressed = None
        self.pending = 0
        self.connect("destroy", Gtk.main_quit)

    def redraw(self):
        if not self.pending:
            self.pending = GLib.timeout_add(30, self._redraw)

    def _redraw(self):
        self.pending = 0
        self.area.queue_draw()
        return False

    def press(self, area, ev):
        if ev.button == 1 and ev.type == Gdk.EventType.BUTTON_PRESS:
            self.pressed = self.r.at(ev.x, ev.y)[0]
        return True

    def release(self, area, ev):
        rect, fn = self.r.at(ev.x, ev.y)
        if ev.button == 1 and fn and rect == self.pressed:
            fn()
        self.pressed = None
        return True

    def on_key(self, w, ev):
        s, hits = self.app.s, self.r.hits
        if ev.keyval in (Gdk.KEY_Tab, Gdk.KEY_ISO_Left_Tab) and hits:
            back = ev.keyval == Gdk.KEY_ISO_Left_Tab or ev.state & Gdk.ModifierType.SHIFT_MASK
            i = s.kfocus if s.kfocus is not None else (0 if back else -1)
            s.kfocus = (i + (-1 if back else 1)) % len(hits)
            self.redraw()
            return True
        if s.kfocus is not None and hits and (ev.keyval in (Gdk.KEY_Return, Gdk.KEY_KP_Enter) or
                                              (ev.keyval == Gdk.KEY_space and not s.focus)):
            k = s.kfocus % len(hits)
            hits[k][1]()
            self.redraw()
            return True
        if ev.keyval == Gdk.KEY_BackSpace:
            self.app.key(back=True)
        else:
            ch = chr(Gdk.keyval_to_unicode(ev.keyval)) if Gdk.keyval_to_unicode(ev.keyval) else ""
            if ch and ch.isprintable():
                self.app.key(ch)
        return True


def render(argv, be):
    def opt(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default
    W, H = (int(v) for v in opt("--size", "%dx%d" % (W0, H0)).split("x"))
    app = Setup(be, lambda: None)
    s = app.s
    s.voice = opt("--theme", "white")
    s.step = opt("--step", "welcome")
    if opt("--layout"):
        s.layout = opt("--layout")
    if opt("--packs") is not None:
        s.packs = [p for p in opt("--packs").split(",") if p]
    if opt("--joined"):
        s.joined = opt("--joined")
    s.big = "--big" in argv
    if s.step == "installing":
        be.preview(app)
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, W * 2, H * 2)
    surf.set_device_scale(2, 2)
    Renderer(app).draw(cairo.Context(surf), W, H, 2)
    surf.write_to_png(opt("--render"))
    return 0


def main(argv):
    GLib.set_prgname("glassine-setup")             # the Wayland app_id (the setup sway config makes it full screen)
    from . import backend
    be = backend.Demo() if "--demo" in argv else backend.Real()
    if "--render" in argv:
        return render(argv, be)
    win = None
    app = Setup(be, lambda: win and win.redraw())
    win = Window(app)
    win.fullscreen()
    win.show_all()
    Gtk.main()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
