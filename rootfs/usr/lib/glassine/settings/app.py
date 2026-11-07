"""glassine-settings: the window (the "E-ink Shell" design: one folder card, the
topics as index tabs on its right edge, sub-pages inside the card; the
layout follows the window, e.g. a tiled half or quarter: icon-only tabs and a
More tab, a too-tall page folds its sections, then pages with a pinned pager).

One GTK window draws everything itself (cairo + Pango, the design's values):
pages instead of scrolling, taps act on release, no animations. The theme is
glassine-shell's ($XDG_RUNTIME_DIR/glassine-shell.theme, followed live). A secured Wi-Fi
network's password is typed on the card's own keyboard (the design's), so it
works without an input method.

  glassine-settings [PAGE]            open (or raise) at a topic: wifi bt sound eink
                                 apps power keyboard appearance time storage about
  glassine-settings app ID            open at an app's page (ID: its .desktop file's
                                 name without .desktop, e.g. firefox-esr)
  glassine-settings --demo            the design's sample data instead of the device
  glassine-settings --render OUT.png [--page P] [--theme T] [--sheet S] [--demo]
                                 draw one page into a PNG (no display needed)
"""
import os
import re
import threading
import time

import cairo
import gi
gi.require_version("Gtk", "3.0")
gi.require_version("Gdk", "3.0")
from gi.repository import Gdk, Gio, GLib, Gtk  # noqa: E402

from . import blocks as B  # noqa: E402
from glassine_ui import paint as P  # noqa: E402
from . import pages  # noqa: E402
from . import packages  # noqa: E402
from .theme import THEMES  # noqa: E402

W0, H0 = 702, 884                 # the panel below glassine-shell's bar, logical px
OUTLINE = os.environ.get("GLASSINE_SETTINGS_OUTLINE") == "1"   # draw the widget boxes + markers
RUN = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
THEME_FILE = RUN + "/glassine-shell.theme"
TOPICS = [t[0] for t in pages.TABS]
# the pages Settings opens on from its command line (glassine-shell launch glassine-settings new PAGE,
# spec 001 FR-045): the tabs and the sub-pages that need nothing chosen first; a tab's section as "power#2"
START = TOPICS + ["packages", "pkg-updates", "pkg-changes", "origins", "render-apps", "light-range", "time-zone", "bt-add"]
START_ALIAS = {"network": "wifi", "bluetooth": "bt", "display": "eink", "clock": "time", "updates": "pkg-updates"}


def start_page(page):
    """the page to open for a command-line PAGE, None: not one (the default page then)"""
    if not page:
        return None
    base, hash_, sec = page.partition("#")
    base = START_ALIAS.get(base, base)
    if base not in START or (hash_ and (base not in TOPICS or not sec.isdigit())):
        return None
    return base + hash_ + sec

class Layout:
    """the design's G: the card, tabs and page metrics for a W x H window.
    head_h is the voice's (KS headH: 80 / 86 / 92); a window narrower than 560 or
    shorter than 700 gets the compact header: 60 high, the pager inside it, no card
    Refresh (the shell's top bar has one)"""

    def __init__(self, W, H, t=None):
        self.W, self.H = W, H
        self.labels, self.short = W >= 560, H < 600          # tab labels; a low window
        self.m = 15 if self.labels else 10
        self.tab_w = 158 if self.labels else 56
        self.compact = not self.labels or H < 700
        self.head_h = 60 if self.compact else P.px((t or {}).get("headH", "80px"))
        self.pad_x = 28 if self.labels else 18
        self.card_r = self.tab_w + (22 if self.labels else 8)  # the card's right margin: the tabs
        self.card_h = H - 2 * self.m - 8
        self.cw = W - self.m - self.card_r - 2 * self.pad_x  # the page's width
        self.narrow = self.cw < 400                           # buttons lose their labels
        self.avail = self.card_h - 3 - self.head_h - 4        # the page's height (the design's cap)
        self.tab_h = 64 if self.labels else 54
        self.tab_cap = (self.card_h - 15 + 5) // (self.tab_h + 5)
        self.pager_pad = 14                                   # the pager bar: 66 high


def summary(items):
    """a collapsed section's sub line: what its widgets say, at most three, then "+N more" """
    parts = [x for x in (b.summ() for b in items) if x]
    return " · ".join(parts[:3] + (["+%d more" % (len(parts) - 3)] if len(parts) > 3 else []))


class Fit:
    """what fit() decided: the blocks of the shown page, the pager, the markers"""

    def __init__(self, blocks, pager, mode, cap, page, title=None):
        self.blocks, self.pager, self.mode, self.cap, self.page, self.title = blocks, pager, mode, cap, page, title


def fit(ui, blocks, c, L):
    """the design's FIT. A page taller than the card collapses its
    tallest sections first: each becomes one row (its head's label, a summary of
    its widgets) that opens the section as a sub-page ("eink#1"), until the page
    fits. A section that is a single link row stays as it is, and if collapsing
    would leave one row or less, nothing collapses. Still too tall: pages, the
    pager pinned to the card's bottom; a head never ends a page. Heights are the
    blocks' own (the design measures offsetHeight + 1; so does this). Every block
    gets the markers: wkind, wsec (its section's label), wsi (its index; -1 = before
    the first head). -> Fit"""
    PV = ui.page
    P, _, si = PV.partition("#")
    SI = int(si) if si.isdigit() else None
    hs = {}

    def est(b):
        if id(b) not in hs:
            hs[id(b)] = b.height(c, L.cw) + 1
        return hs[id(b)]

    def S(a):
        return sum(map(est, a))
    lead, secs = [], []
    for b in blocks:
        if isinstance(b, B.Head):
            secs.append((b, []))
        elif secs:
            secs[-1][1].append(b)
        else:
            lead.append(b)
    for i, (hd, items) in enumerate(secs):
        for b in [hd] + items:
            b.wsec, b.wsi = hd.label, i
    for b in lead:
        b.wsec, b.wsi = "", -1

    def is_link(sc):
        return len(sc[1]) == 1 and getattr(sc[1][0], "chev", False)

    def row(i, sc):
        r = B.Item(sc[0].label, sc[0].as_row or sc[0].summary or summary(sc[1]), chev=True, fn=lambda: ui.go("%s#%d" % (P, i)))
        r.min_h = 64 if L.compact else 80
        r.wkind, r.wsec, r.wsi = "section-link", sc[0].label, i
        return r
    rows, title = [], None
    if SI is not None and SI < len(secs):
        V, title = secs[SI][1], secs[SI][0].label
    else:
        SI = None
        V = [b for k, b in enumerate(blocks) if not (isinstance(b, B.Head) and (
            b.inline or (k + 1 < len(blocks) and getattr(blocks[k + 1], "chev", False) and
                         (k + 2 >= len(blocks) or isinstance(blocks[k + 2], B.Head)))))]
        rows = [None if is_link(sc) else row(i, sc) for i, sc in enumerate(secs)]
    cap = L.avail
    flat = V
    if SI is None and secs:
        def sec_h(sc):
            return S(sc[1] if sc[0].inline else [sc[0]] + sc[1])
        # a section marked as_row (an app's E-ink) is a row from the start. Then the
        # tallest first; with three or more titled sections every one may fold (the
        # page becomes an index), with fewer one stays open; a list never folds, it pages
        n_secs = len(secs) + (1 if lead else 0)
        keep = 0 if len(secs) >= 3 else 1
        order = sorted((i for i in range(len(secs)) if rows[i] and not secs[i][0].list_),
                       key=lambda i: (-(sec_h(secs[i]) - est(rows[i])), -i)) if n_secs > 2 else []
        cs = {i for i, sc in enumerate(secs) if sc[0].as_row}
        forced = bool(cs)

        def build():
            r = list(lead)
            for i, sc in enumerate(secs):
                if is_link(sc):
                    r.append(sc[1][0])
                elif i in cs:
                    r.append(rows[i])
                else:
                    r += ([] if sc[0].inline else [sc[0]]) + sc[1]
            return r
        if forced:
            flat = build()
        for i in order:
            if S(flat) <= cap or len(cs) >= n_secs - keep:
                break
            if i in cs:
                continue
            cs.add(i)
            flat = build()
        if sum(1 for b in flat if not isinstance(b, B.Head)) <= 1:
            flat, cs = V, set()
        folded = any(not secs[i][0].as_row for i in cs)
    else:
        folded = False
    if S(flat) <= cap:
        return Fit(flat, None, "collapsed" if folded else "open", cap, PV, title)
    # roomy: the pager takes its bar at the card's bottom; compact: it's in the header
    budget = cap - (0 if L.compact else L.pager_pad + 52)
    pgs, used = [[]], 0
    for b in flat:
        if used + est(b) > budget and pgs[-1]:
            pgs.append([])
            used = 0
        pgs[-1].append(b)
        used += est(b)
    for i in range(len(pgs) - 1):                          # a head goes with its rows
        while pgs[i] and isinstance(pgs[i][-1], B.Head):
            pgs[i + 1].insert(0, pgs[i].pop())
    pgs = [pg for pg in pgs if pg]
    n = ui.pp.get(PV, 0)
    if n == "focus":                                         # the selected row's page (time-city)
        n = next((i for i, pg in enumerate(pgs) if any(getattr(b, "focus", False) for b in pg)), 0)
    n = max(0, min(len(pgs) - 1, n))
    ui.pp[PV] = n
    return Fit(pgs[n], B.Pager(n, len(pgs), lambda j: ui.set_pp(PV, j), L.pager_pad),
               "collapsed+paged" if folded else "paged", cap, PV, title)


class Sheet:
    """a sheet on the card. parts: blocks with draw(c, x, y, w) -> height
    (the Packages sheets' lists, warnings, files); size: a mono line over the buttons"""

    def __init__(self, title, body, ok_label=None, ok=None, code=None, cancel="Cancel", icon=None, parts=None, size=None):
        self.title, self.body, self.ok_label, self.ok, self.code, self.cancel = title, body, ok_label, ok, code, cancel
        self.icon, self.parts, self.size = icon, parts, size


class UI:
    """the app's state and what pages call back into"""

    def __init__(self, be, redraw):
        self.be, self.redraw = be, redraw
        self.page = "eink"
        self.kfocus = None               # t13: the Tab focus (an index into the drawn targets)
        self.pp = {}                     # page (a section's sub-page too: "eink#1") -> its page number (fit), or "focus"
        self.pick = None                 # the choice page: {key, title, opts, cur, set, from}
        self.tp = None                   # the tabs' page when they need a More tab
        self.L = Layout(W0, H0)
        self.net = self.dev = self.region = self.app = None
        self.net_info = {}
        self.pw, self.show_pw = "", False
        self.osk_up = False              # the system keyboard was asked for (Settings draws none)
        self.own = set()                 # (app, "render"|"freeze"): an Own row turned on, the value still the default
        # wifi-new: asked = opened from glassine-nm-agent's notice (the saved secret failed or is
        # missing), eap = an 802.1X login (user name + password), fld = the field being typed
        self.asked, self.eap, self.user, self.fld = False, False, "", "pw"
        self.ask_uuid = None             # the profile the notice was about (glassine-nm-agent --uuid)
        self.sheet = None
        self.pairing = None
        self.auth = None                 # the admin-auth page: {why, then, err, busy}
        self.hostname = getattr(be, "hostname", None) or os.uname().nodename
        self.names = self._app_names()
        from . import backend as _be
        self.pk = packages.State(self, packages.Demo() if isinstance(be, _be.Demo) else packages.Real())
        be.added_fn = lambda: self.pk.d.added
        be.on_change = self.redraw
        be.on_error = lambda t, x: self.show(Sheet(t, x, cancel="OK"))
        be.on_agent = self.agent
        be.on_auth = lambda why, then: GLib.idle_add(lambda: (self.ask_admin(why, then), False)[1])

    # -- navigation
    def go(self, page, dev=None, region=None, reset=None, net=None, app=None):
        self.page, self.sheet = page, None
        self.kfocus = None                              # t13: the Tab focus starts again on a new page
        if app is not None:
            self.app = app
        if dev is not None:
            self.dev = dev
        if region is not None:
            self.region = region
        if net is not None:
            self.net = net
        if reset:
            self.pp[reset] = 0
        if page == "wifi-new":
            self.pw, self.user = "", ""
            self.fld = "user" if self.eap else "pw"
        if page == "time-city":                          # open at the current city's page
            self.pp[page] = "focus"
        self.be.refresh(page.split("#")[0])
        self.sync_osk()
        self.redraw()

    def typing(self):
        """a drawn text field has the focus (and takes the keys)"""
        if self.sheet:
            return False
        p = self.page.split("#")[0]
        return p in ("wifi-new", "admin-auth") or (p == "packages" and self.pk.kb)

    def sync_osk(self):
        """a text field asks the system's keyboard daemon: the window shrinks and the
        page re-fits to what is left. The fields are drawn, not GTK entries, so no
        text-input auto-show"""
        want = self.typing()
        if want != self.osk_up:
            self.osk_up = want
            self.be.osk_show(want)

    def refresh(self):
        """the header's Refresh: re-read the page, redraw, then one full e-ink refresh"""
        self.be.refresh(self.page.split("#")[0])
        if self.page.split("#")[0] in packages.PAGES:
            self.pk.d.refresh()
        self.redraw()
        self.be.refresh_screen()

    def back_of(self, page):
        """where Back goes: a section's sub-page to its topic, the choice page to
        the page it was opened from, else the page's parent"""
        if "#" in page:
            return page.split("#")[0]
        if page == "pick" and self.pick:
            return self.pick["from"]
        return pages.PARENT.get(page)

    def open_pick(self, title, opts, cur, set_fn):
        """a choice row (the design's CH): its options as a page of radio rows
        ("#Name" entries are group heads); a tap sets and goes back"""
        self.pick = {"title": title, "opts": opts, "cur": cur, "set": set_fn, "from": self.page}
        pages.PARENT["pick"] = self.page.split("#")[0]      # the tab that stays lit
        self.go("pick")

    def picked(self, v):
        fr = self.pick["from"]
        self.pick["set"](v)
        self.go(fr)

    def set_pp(self, page, n):
        self.pp[page] = n
        self.redraw()

    def more_tabs(self, np):
        self.tp = ((self.tp or 0) + 1) % np
        self.redraw()

    def show(self, sheet):
        self.sheet = sheet
        self.redraw()

    # -- Wi-Fi
    def net_obj(self, be):
        return next((n for n in be.nets if n["ssid"] == self.net), None)

    def open_net(self, n):
        if n["ssid"] == self.be.conn or n["known"]:
            self.net_info = self.be.net_info(n["ssid"])
            self.go("wifi-net", net=n["ssid"])
        elif n["lock"]:
            self.asked = self.eap = False
            self.ask_uuid = None
            self.go("wifi-new", net=n["ssid"])
        else:
            self.connect(n["ssid"])

    def ask(self, ssid, eap=False, uuid=None):
        """glassine-nm-agent's notice was tapped: NetworkManager needs this network's secret
        (uuid: that profile exactly; an SSID can have several)"""
        self.asked, self.eap, self.ask_uuid = True, eap, uuid
        self.go("wifi-new", net=ssid)

    def connect(self, ssid, pw=None, user=None):
        self.be.connect(ssid, pw, user, self.ask_uuid if self.asked else None)
        self.ask_uuid = None
        self.pw = self.user = ""
        self.go("wifi")

    # -- an administrator's password (admin-auth page; the helpers' exit 77)
    def admin_ok(self, level="password"):
        return self.be.admin_ok(level)

    def ask_admin(self, why, then):
        """asks, then runs then() where it was asked from"""
        back = self.page if self.page != "admin-auth" else (self.auth or {}).get("back")
        pages.PARENT["admin-auth"] = (back or "apps").split("#")[0]
        self.auth = {"why": why, "then": then, "back": back or "apps"}
        self.eap, self.fld, self.show_pw = False, "pw", False
        self.pw = ""
        self.go("admin-auth")

    def auth_submit(self):
        a = self.auth
        if not a or a.get("busy") or not self.pw:
            return
        pw, self.pw = self.pw, ""
        a.update(busy=True, err=None)
        self.redraw()

        def work():
            good = self.be.admin_auth(pw)

            def done():
                if self.auth is not a:
                    return False
                a["busy"] = False
                if good:
                    self.auth, self.show_pw = None, False
                    self.go(a["back"])
                    a["then"]()
                elif good is None:
                    a["err"] = "The password couldn’t be checked. Try again, or restart the device."
                else:
                    a["err"] = "That isn’t an administrator’s password."
                    self.redraw()
                return False
            GLib.idle_add(done)
        threading.Thread(target=work, daemon=True).start()

    def secret_ok(self):
        if self.page == "admin-auth":
            return bool(self.pw)
        return bool(self.user and self.pw) if self.eap else len(self.pw) >= 8

    def focus(self, fld):
        self.fld = fld
        self.redraw()

    def type(self, ch):
        if self.page.split("#")[0] in packages.PAGES:
            self.pk.key(ch)
            return
        if self.eap and self.fld == "user":
            self.user += ch
        else:
            self.pw += ch
        self.redraw()

    def set_net_auto(self, on):
        self.net_info["auto"] = on
        self.be.set_autoconnect(self.net, on)
        self.redraw()

    def flip_show_pw(self):
        self.show_pw = not self.show_pw
        self.redraw()

    def sheet_forget_net(self):
        ssid = self.net

        def ok():
            self.be.forget(ssid)
            self.go("wifi")
        self.show(Sheet("Forget %s?" % ssid, "The saved password is removed. You will need it again to reconnect.", "Forget", ok))

    # -- Bluetooth
    def sheet_forget_dev(self, d):
        def ok():
            self.be.dev_forget(d["id"])
            self.go("bt")
        self.show(Sheet("Forget %s?" % d["name"], "The device is unpaired. Pair it again from Add device to use it.", "Forget", ok))

    def pair(self, d):
        self.pairing = d
        sh = Sheet("Pairing with %s" % d["name"], "Waiting for the device…", cancel="Cancel")
        sh.on_cancel = lambda: self.be.cancel_pair(d["id"])
        self.show(sh)
        self.be.pair(d["id"])

    def agent(self, kind, name, code, reply):
        """the pairing agent asks (kind confirm | display) or pairing ended (done)"""
        if kind == "done":
            if self.sheet and self.sheet.title.startswith("Pair"):
                self.sheet = None
            self.go("bt")
            return
        if kind == "confirm":
            def ok():
                self.sheet = None
                reply(True)
                self.redraw()
            sh = Sheet("Pair with %s?" % name, "Check that the device shows the same code.", "Pair", ok, code=code)
            sh.on_cancel = lambda: reply(False)
        else:
            # DisplayPasskey has no answer: the device pairs once the code is typed
            sh = Sheet("Pair with %s?" % name, "Type this code on the keyboard, then press Enter.", code=code, cancel="Cancel")
            d = self.pairing
            if d:
                sh.on_cancel = lambda: self.be.cancel_pair(d["id"])
            if reply:
                sh.ok_label, sh.ok = "Pair", lambda: (reply(True), setattr(self, "sheet", None), self.redraw())
        self.show(sh)

    # -- time
    def set_zone(self, region, city):
        self.be.set_zone(region, city)
        self.go("time")

    def clock(self, h24):
        t = time.localtime()
        return time.strftime("%H:%M", t) if h24 else time.strftime("%-I:%M ", t) + ("am" if t.tm_hour < 12 else "pm")

    # -- e-ink profiles: show an app's name, not its match string
    def _app_names(self):
        names = {}
        for d in ("/usr/share/applications", os.path.expanduser("~/.local/share/applications")):
            try:
                files = os.listdir(d)
            except OSError:
                continue
            for f in files:
                if not f.endswith(".desktop"):
                    continue
                name, wmc = None, None
                try:
                    with open(os.path.join(d, f), errors="replace") as fh:
                        for l in fh:
                            if l.startswith("Name=") and not name:
                                name = l[5:].strip()
                            elif l.startswith("StartupWMClass="):
                                wmc = l.split("=", 1)[1].strip().lower()
                            elif l.startswith("[") and name:
                                if l.strip() != "[Desktop Entry]":
                                    break
                except OSError:
                    continue
                if name:
                    for k in (wmc, f[:-8].lower(), f[:-8].lower().split(".")[-1]):
                        if k:
                            names.setdefault(k, name)
        return names

    def app_name(self, match):
        return self.names.get(match.lower()) or match[:1].upper() + match[1:]

    # -- what the keys do in a text field (the system's keyboard or a real one)
    def key(self, k):
        if self.page.split("#")[0] in packages.PAGES:   # the Packages search field
            self.pk.key(k)
            return
        if k == "back":
            if self.eap and self.fld == "user":
                self.user = self.user[:-1]
            else:
                self.pw = self.pw[:-1]
        elif k == "done":
            if self.eap and self.fld == "user":
                self.fld = "pw"                     # Enter on name: on to the password
            elif self.page == "admin-auth":
                self.auth_submit()
                return
            elif self.secret_ok():
                self.connect(self.net, self.pw, self.user if self.eap else None)
                return
        self.redraw()


class Renderer:
    """draws the whole window into a cairo context; collects the hits"""

    def __init__(self, ui):
        self.ui = ui
        self.hits = []
        self.markers = None

    def theme(self):
        return THEMES.get(self.ui.be.theme) or THEMES["white"]

    def draw(self, cr, W, H, scale=2):
        ui, t = self.ui, self.theme()
        L = ui.L = Layout(W, H, t)
        c = B.Ctx(cr, t, scale)
        c.narrow, c.short, c.labels = L.narrow, L.short, L.labels
        P.fill(cr, 0, 0, W, H, t["screenBg"])
        page = pages.build(ui, ui.be)
        cx, cy, cw, ch = L.m, L.m, W - L.m - L.card_r, L.card_h
        self.card = (cx, cy, cw, ch)
        P.box(cr, cx, cy, cw, ch, t["cardR"], t["cardBg"], t["cardSh"], t["cardBorder"])
        if t["emboss"] != "none":                       # stationery: a frame 7 px inside
            P.box(cr, cx + 7, cy + 7, cw - 14, ch - 14, "3px", None, None, "1px solid " + t["frameCol"])
        # the page: collapsed / paged to the card, the pager at its bottom
        f = fit(ui, page.blocks, c, L)
        if f.title is not None:                         # a section's sub-page
            page.title, page.back_label = f.title, pages.SHORT.get(f.page.split("#")[0]) or page.title
            page.back = f.page.split("#")[0]
        self.header(c, page, cx, cy, cw, f.pager if L.compact else None)
        x, y, w = cx + L.pad_x, cy + L.head_h + 2, L.cw
        cr.save()
        cr.rectangle(cx, cy + L.head_h, cw, ch - L.head_h)
        cr.clip()
        marks = []
        for b in f.blocks:
            h = b.height(c, w)
            b.draw(c, x, y, w)
            marks.append(dict(kind=getattr(b, "wkind", None) or b.KIND, label=getattr(b, "label", "") or getattr(b, "title", "") or "",
                              section=getattr(b, "wsec", ""), index=getattr(b, "wsi", -1), x=x, y=round(y, 2), w=w, h=round(h, 2)))
            y += h
        if f.pager and L.compact:
            marks.append(dict(kind="pager", label="%d/%d" % (f.pager.n + 1, f.pager.of), section="", index=-1,
                              x=x, y=cy, w=w, h=L.head_h))
        elif f.pager:
            py = cy + ch - 1.5 - f.pager.height(c, w)
            f.pager.draw(c, x, py, w)
            marks.append(dict(kind="pager", label="Page %d of %d" % (f.pager.n + 1, f.pager.of), section="", index=-1,
                              x=x, y=round(py, 2), w=w, h=f.pager.height(c, w)))
        if OUTLINE:
            for m in marks:
                self.outline(c, m)
        cr.restore()
        # the markers (the design's data-settings-page/data-fit/data-cap-px and data-widget...)
        self.markers = dict(page=f.page, fit=f.mode, cap_px=f.cap, avail=L.avail, cw=L.cw, header="compact" if L.compact else "roomy",
                            widgets=marks)
        self.tabs(c, cx + cw, cy + 15)
        self.hits = c.hits
        if ui.sheet:
            self.hits = []
            self.sheet(c, cx, cy, cw, ch)
        self.ring(cr, t)

    def ring(self, cr, t):
        """design the Tab focus, a 2 px ring 2 px outside the control
        (Interleaf's focus token)"""
        i = getattr(self.ui, "kfocus", None)
        if i is None or not self.hits:
            return
        x, y, w, h = self.hits[i % len(self.hits)][0]
        P.box(cr, x - 4, y - 4, w + 8, h + 8, "6px", None, "inset 0 0 0 2px " + t["il"]["focus"])

    def pill(self, c, x, y, label, icon=None, isz=22, fn=None, pad=(14, 18), gap=6, show_label=True, fg=None, h=48, bold=False,
             primary=False):
        """the design's pill: 48 high, radius 999, a 1.5px ink outline (primary: filled
        with the accent), [icon] [label] -> its width"""
        t, cr = c.t, c.cr
        f = t["rowFont"]
        lw, lh = c.m(label, f, weight=600 if bold or primary else None) if show_label and label else (0, 0)
        bw = pad[0] + (isz if icon else 0) + (gap if icon and lw else 0) + lw + pad[1]
        bg, sh = (t["pwrBg"], t["pwrSh"]) if primary else (t["btnBg"], t["btnSh"])
        fg = fg or (t["pwrFg"] if primary else t["ink"])
        P.box(cr, x, y, bw, h, "999px", bg, sh)
        ix = x + pad[0]
        if icon:
            c.icon(icon, isz, ix + isz / 2, y + h / 2, fg)
            ix += isz + gap
        if lw:
            c.txt(label, f, ix, y + (h - lh) / 2, fg, weight=600 if bold or primary else None)
        if fn:
            c.hit(x, y, bw, h, fn)
        return bw

    def pill_w(self, c, label, icon=None, isz=22, pad=(14, 18), gap=6, show_label=True, bold=False):
        lw = c.m(label, c.t["rowFont"], weight=600 if bold else None)[0] if show_label and label else 0
        return pad[0] + (isz if icon else 0) + (gap if icon and lw else 0) + lw + pad[1]

    def header(self, c, page, x, y, w, pager=None):
        """[back pill] title ... Refresh pill, a 1px rule under it. Compact (Layout): no
        Refresh, the title 22 px (19 narrow), the pager's arrows and "1/2" at the right"""
        t, cr, L = c.t, c.cr, self.ui.L
        hh = L.head_h
        hx, right = x + L.pad_x, x + w - L.pad_x
        gap = 22 if L.labels else 8
        by = y + (hh - 48) / 2
        if not L.compact:
            rpad = (14, 18) if L.labels else (12, 12)
            rw = self.pill_w(c, "Refresh", "refresh", 22, rpad, show_label=L.labels)
            self.pill(c, right - rw, by, "Refresh", "refresh", 22, lambda: self.ui.refresh(), rpad, show_label=L.labels)
            right -= rw + gap
        if pager:
            right -= B.Pager.small(c, right, by, pager) + gap
        if page.back:
            lbl = getattr(page, "back_label", None) or pages.SHORT.get(page.back.split("#")[0], "Settings")
            bpad = (12, 20) if L.labels else (12, 12)
            maxw = 0.45 * (w - 2 * L.pad_x)
            bw = self.pill_w(c, lbl, "chevron_left", 24, bpad, show_label=L.labels)
            if bw > maxw:                                   # max-width 45 %, the label ellipsized
                lbl_w = maxw - (bpad[0] + 24 + 6 + bpad[1])
                P.box(cr, hx, by, maxw, 48, "999px", t["btnBg"], t["btnSh"])
                c.icon("chevron_left", 24, hx + bpad[0] + 12, by + 24)
                lh = c.m(lbl, t["rowFont"])[1]
                c.txt(lbl, t["rowFont"], hx + bpad[0] + 30, by + (48 - lh) / 2, maxw=lbl_w)
                c.hit(hx, by, maxw, 48, lambda: self.ui.go(page.back))
                bw = maxw
            else:
                self.pill(c, hx, by, lbl, "chevron_left", 24, lambda: self.ui.go(page.back), bpad, show_label=L.labels)
            hx += bw + gap
        caps = (t["headVar"], "none")
        f = t["headFont"] if not L.compact else re.sub(r"[\d.]+px", "22px" if L.labels else "19px", t["headFont"], count=1)
        th = c.m(page.title or " ", f, caps=caps)[1]
        c.txt(page.title, f, hx, y + (hh - th) / 2, caps=caps, maxw=right - hx, ls=P.ls_em(t["headLs"]))
        P.rule(cr, x, y + hh, w, t["headRule"])

    def tabs(self, c, x, y):
        """the index tabs: outlined, the current one wider, filled (tabOnBg),
        open towards the card; more than fit: a page of them + a More tab that cycles"""
        t, cr, ui, L = c.t, c.cr, self.ui, self.ui.L
        top = pages.top_of(ui.page)
        tl = [(pid, icon, label, (lambda pid=pid: ui.go(pid))) for pid, icon, label in pages.TABS]
        sub = None
        if len(tl) > L.tab_cap:
            per = max(1, L.tab_cap - 1)
            np = -(-len(tl) // per)
            ai = next((i for i, x_ in enumerate(tl) if x_[0] == top), 0)
            tp = min(np - 1, ui.tp if ui.tp is not None else ai // per)
            ui.tp = tp
            sub = "%d/%d" % (tp + 1, np)
            tl = tl[tp * per:tp * per + per] + [(None, "more_vert", "More · " + sub, lambda: ui.more_tabs(np))]
        th = L.tab_h
        ink = t["ink"]
        for i, (pid, icon, label, fn) in enumerate(tl):
            on = pid == top
            tw, ml = (L.tab_w + 12, -4) if on else (L.tab_w, 0)
            ty = y + i * (th + 5)
            bg, fg = (t["tabOnBg"], t["tabOnFg"]) if on else (t["paper"], ink)
            sh = t["tabOnSh"] if on else t["tabOffSh"]
            if sh != "none":
                P.box(cr, x + ml, ty, tw, th, t["tabR"], None, sh)
            # border 1.5 ink, no left border on the current tab (it opens into the card)
            P.box(cr, x + ml, ty, tw, th, t["tabR"], bg, None, "1.5px solid " + ink)
            if on:
                P.fill(cr, x + ml - 1, ty + 1.5, 3, th - 3, bg)
            if L.labels:
                c.icon(icon, 23, x + ml + 13 + 11.5, ty + th / 2, fg)
                lh = c.m(label, t["tabFont"])[1]
                c.txt(label, t["tabFont"], x + ml + 13 + 23 + 11, ty + (th - lh) / 2, fg,
                      ls=P.ls_em(t["tabLs"]), maxw=tw - 13 - 23 - 11 - 13)
            elif pid is None:                   # icon-only More: the icon over "1/2"
                sf = "500 10px 'IBM Plex Mono'"
                sh_ = c.m(sub, sf)[1]
                top_y = ty + (th - 23 - 3 - sh_) / 2
                c.icon(icon, 23, x + ml + tw / 2, top_y + 11.5, fg)
                c.txt(sub, sf, x + ml + tw / 2, top_y + 23 + 3, fg, align=1)
            else:
                c.icon(icon, 23, x + ml + tw / 2, ty + th / 2, fg)
            c.hit(x + ml, ty, tw, th, fn)

    def sheet(self, c, cx, cy, cw, ch):
        """a sheet lies on the card under its header (the page stays behind
        it): [icon] title, lines, the optional parts, then the buttons right-aligned
        over a rule (b2 outlined, b1 filled with the accent)"""
        t, cr, sh, L = c.t, c.cr, self.ui.sheet, self.ui.L
        x, y, w = cx, cy + L.head_h, cw
        h = ch - L.head_h
        r = t["cardR"]
        P.box(cr, x + 1.5, y, w - 3, h - 1.5, "0 0 %s %s" % (r, r), t["paper"])
        P.rule(cr, x + 1.5, y + P.border(t["sheetTop"])[0] if P.border(t["sheetTop"]) else y, w - 3, t["sheetTop"])
        c.hit(cx, cy, cw, ch, lambda: None)                     # the page behind does not take taps
        px, iw = x + L.pad_x, w - 2 * L.pad_x
        yy = y + 24
        caps = (t["headVar"], "none")
        tx = px
        if getattr(sh, "icon", None):
            c.icon(sh.icon, 30, px + 15, yy + 15)
            tx = px + 30 + 14
        th = c.txt(sh.title, t["headFont"], tx, yy, caps=caps, maxw=px + iw - tx, wrap=True, ls=P.ls_em(t["headLs"]))[1]
        yy += max(th, 30 if getattr(sh, "icon", None) else 0) + 14
        if sh.body:
            yy += c.txt(sh.body, t["dateFont"], px, yy, t["sub"], maxw=iw, wrap=True)[1] + 14
        for part in getattr(sh, "parts", None) or []:
            yy += part.draw(c, px, yy, iw) + 14
        if sh.code:
            cw_, chh = c.m(sh.code, t["codeFont"])
            bw, bh = cw_ + 60, chh + 28
            P.box(cr, px + (iw - bw) / 2, yy, bw, bh, t["trayR"], t["trayBg"], t["traySh"])
            c.txt(sh.code, t["codeFont"], px + iw / 2, yy + 14, align=1, ls=0.12)
            yy += bh + 14
        # the buttons: bottom, over a rule
        by = y + h - 1.5 - 20 - 48
        if getattr(sh, "size", None):
            sf = "400 15px/1.3 'IBM Plex Mono',monospace"
            sh_h = c.m(sh.size, sf)[1]
            c.txt(sh.size, sf, px, by - 14 - 14 - sh_h, t["ink"])
        P.rule(cr, px, by - 14, iw, t["sepSoft"])
        bx = px + iw
        if sh.ok_label:
            bw = self.pill_w(c, sh.ok_label, pad=(26, 26), bold=True)
            bx -= bw
            self.pill(c, bx, by, sh.ok_label, None, fn=sh.ok, pad=(26, 26), primary=getattr(sh, "ok_enabled", True))
            bx -= 12
        if sh.cancel:
            bw = self.pill_w(c, sh.cancel, pad=(24, 24))
            bx -= bw
            self.pill(c, bx, by, sh.cancel, None, fn=getattr(sh, "cancel_fn", None) or self.close_sheet, pad=(24, 24))
        self.hits = c.hits

    def outline(self, c, m):
        """--outline / GLASSINE_SETTINGS_OUTLINE=1: a widget's box and its markers, to check
        the boundaries against the design's data-widget boxes"""
        cr = c.cr
        cr.save()
        cr.set_source_rgba(0.85, 0, 0, 0.9)
        cr.set_line_width(1)
        cr.set_dash([3, 2])
        cr.rectangle(m["x"] - 4 + 0.5, m["y"] + 0.5, m["w"] + 8 - 1, m["h"] - 1)
        cr.stroke()
        cr.restore()
        tag = "%s %s%s h%g" % (m["kind"], m["index"], (" " + m["section"]) if m["section"] else "", m["h"])
        tw, th = c.m(tag, "400 9px 'IBM Plex Mono'")
        P.fill(cr, m["x"] + m["w"] + 4 - tw - 4, m["y"], tw + 4, th, "#ffffff")
        c.txt(tag, "400 9px 'IBM Plex Mono'", m["x"] + m["w"] + 2, m["y"], (0.85, 0, 0, 1), align=2)

    def close_sheet(self):
        sh = self.ui.sheet
        self.ui.sheet = None
        if sh is not None and getattr(sh, "on_cancel", None):
            sh.on_cancel()
        self.ui.redraw()

    def at(self, x, y):
        """the tap target at x, y (the last drawn wins: it is on top)"""
        return self.hit(x, y)[1]

    def hit(self, x, y):
        """(its rect, the tap target) at x, y; (None, None) for none"""
        for r, fn in reversed(self.hits):
            hx, hy, hw, hh = r
            if hx <= x < hx + hw and hy <= y < hy + hh:
                return tuple(r), fn
        return None, None


class Window(Gtk.ApplicationWindow):
    def __init__(self, app, ui):
        super().__init__(application=app, title="Settings")
        self.ui = ui
        self.r = Renderer(ui)
        self.set_default_size(W0, H0)
        self.area = Gtk.DrawingArea()
        self.area.set_can_focus(True)
        self.area.add_events(Gdk.EventMask.BUTTON_PRESS_MASK | Gdk.EventMask.BUTTON_RELEASE_MASK | Gdk.EventMask.KEY_PRESS_MASK |
                             Gdk.EventMask.BUTTON1_MOTION_MASK)
        self.area.connect("draw", self.on_draw)
        self.area.connect("button-press-event", self.on_press)
        self.area.connect("button-release-event", self.on_release)
        self.area.connect("motion-notify-event", self.on_motion)
        self.connect("key-press-event", self.on_key)
        self.connect("destroy", lambda *_: self.ui.be.osk_show(False) if self.ui.osk_up else None)   # the keyboard goes with the window
        self.add(self.area)
        self.pressed = None
        self.drag = None                # a held knob (a hit with .grab: none at present): move/done
        self.pending = 0

    def redraw(self):
        """one repaint per burst of changes (e-ink: every paint is a panel update)"""
        if not self.pending:
            self.pending = GLib.timeout_add(30, self._redraw)

    def _redraw(self):
        self.pending = 0
        self.area.queue_draw()
        return False

    def on_draw(self, area, cr):
        a = area.get_allocation()
        self.r.draw(cr, a.width, a.height, area.get_scale_factor())
        return True

    def on_press(self, area, ev):
        if ev.button == 1 and ev.type == Gdk.EventType.BUTTON_PRESS:
            # the target's rect, not the callable: every draw makes new ones, and a
            # draw can land between press and release (the tap that focuses the
            # window repaints it as active: , every such tap was lost)
            self.pressed, fn = self.r.hit(ev.x, ev.y)
            if getattr(fn, "grab", None):
                self.drag, self.pressed = fn.grab(ev.x, ev.y), None
        return True

    def on_motion(self, area, ev):
        if self.drag is not None and self.drag.move(ev.x, ev.y):
            self.redraw()                   # one paint per stop crossed
        return True

    def on_release(self, area, ev):
        if self.drag is not None:           # the drag ends wherever the finger is
            d, self.drag = self.drag, None
            if ev.button == 1:
                d.done(ev.x, ev.y)
            else:
                d.cancel()
            self.redraw()
            return True
        rect, fn = self.r.hit(ev.x, ev.y)
        if ev.button == 1 and fn is not None and rect == self.pressed:
            if getattr(fn, "xy", False):
                fn(ev.x, ev.y)
            else:
                fn()
        self.pressed = None
        return True

    def on_key(self, w, ev):
        """a hardware keyboard: types into the password field, Escape closes a sheet / goes back"""
        ui = self.ui
        name = Gdk.keyval_name(ev.keyval) or ""
        # t13: Tab / Shift-Tab move the focus over every control, Enter or Space press it
        hits = self.r.hits
        if name in ("Tab", "ISO_Left_Tab") and hits:
            back = name == "ISO_Left_Tab" or ev.state & Gdk.ModifierType.SHIFT_MASK
            i = getattr(ui, "kfocus", None)
            i = i if i is not None else (0 if back else -1)
            ui.kfocus = (i + (-1 if back else 1)) % len(hits)
            self.redraw()
            return True
        typing = ui.typing()
        if getattr(ui, "kfocus", None) is not None and hits and (name in ("Return", "KP_Enter") and not typing or
                                                                  name == "space" and not typing):
            fn = hits[ui.kfocus % len(hits)][1]
            if not getattr(fn, "xy", False) and not getattr(fn, "grab", None):   # (a slider moves with the arrows)
                fn()
            self.redraw()
            return True
        # sliders and the range move with the arrow keys (Shift: the range's slow end)
        if name in ("Left", "Right", "Up", "Down") and getattr(ui, "kfocus", None) is not None and hits and not typing:
            arrow = getattr(hits[ui.kfocus % len(hits)][1], "arrow", None)
            if arrow:
                arrow(1 if name in ("Right", "Up") else -1, bool(ev.state & Gdk.ModifierType.SHIFT_MASK))
                self.redraw()
                return True
        if name == "Escape":                    # the top sheet, the keyboard, then a step back
            if ui.sheet:
                self.r.close_sheet()
            elif ui.page.split("#")[0] == "packages" and ui.pk.kb:
                ui.pk.kb = False
                ui.sync_osk()
                self.redraw()
            elif ui.back_of(ui.page):
                ui.go(ui.back_of(ui.page))
            return True
        if typing:
            if name == "BackSpace":
                ui.key("back")
            elif name in ("Return", "KP_Enter"):
                ui.key("done")
            else:
                ch = chr(Gdk.keyval_to_unicode(ev.keyval) or 0)
                if ch.isprintable() and ch:
                    ui.type(ch)
            return True
        return False


class App(Gtk.Application):
    def __init__(self, be):
        super().__init__(application_id="org.glassine.Settings", flags=Gio.ApplicationFlags.HANDLES_COMMAND_LINE)
        self.be = be
        self.win = None

    def do_command_line(self, cl):
        argv = cl.get_arguments()[1:]
        uuid = None
        if "--uuid" in argv and argv.index("--uuid") + 1 < len(argv):
            i = argv.index("--uuid")
            uuid, argv = argv[i + 1], argv[:i] + argv[i + 2:]
        args = [a for a in argv if not a.startswith("-")]
        page = args[0] if args else None
        ask = args[1] if page == "wifi-ask" and len(args) > 1 else None   # wifi-ask SSID [--eap] [--uuid U] (glassine-nm-agent)
        app = args[1] if page == "app" and len(args) > 1 else None        # app ID (the launcher: hold a tile)
        page = start_page(page) if page not in ("app", "wifi-ask") else page
        if not self.win:
            self.ui = UI(self.be, lambda: self.win and self.win.redraw())
            self.win = Window(self, self.ui)
            self.win.show_all()
            self.watch_theme()
            self.ui.go(page if page and page not in ("app", "wifi-ask") else "eink")
        elif page and page not in ("app", "wifi-ask"):
            self.ui.go(page)
        if app:
            self.ui.go("app" if self.be.app(app) else "apps", app=app)
        if ask:
            self.ui.ask(ask, "--eap" in argv, uuid)
        self.win.present()
        return 0

    def watch_theme(self):
        """glassine-shell's theme file: a change restyles the card (one full repaint)"""
        self.mon = Gio.File.new_for_path(THEME_FILE).monitor_file(Gio.FileMonitorFlags.NONE, None)

        def changed(*a):
            t = P_read(THEME_FILE)
            if t in THEMES and t != self.be.theme:
                self.be.theme = t
                self.win.redraw()
        self.mon.connect("changed", changed)


def P_read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return ""


def render(argv, be):
    """--render OUT.png [--page P [--app ID]] [--theme T] [--sheet forget-net|forget-dev|pair] [--size WxH]
    [--ask SSID [--eap] [--uuid U] [--user U]] [--pw PW] [--pp N] [--pick KEY] [--pk PRESET] [--markers OUT.json] [--outline]
    P may be a section's sub-page ("power#2"); --pick KEY opens a choice row's page (gestures: g3Up ...)"""
    global OUTLINE
    OUTLINE = OUTLINE or "--outline" in argv
    def opt(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default
    out = opt("--render")
    W, H = (int(v) for v in opt("--size", "%dx%d" % (W0, H0)).split("x"))
    if opt("--theme"):
        be.theme = opt("--theme")
    ui = UI(be, lambda: None)
    page = opt("--page", "wifi")
    extra = {"wifi-new": dict(net="FRITZ!Box 7590 KL"), "wifi-net": dict(net="Pi-Home"), "bt-dev": dict(dev="kb"),
             "time-city": dict(region="Europe"), "app": dict(app=opt("--app", "Firefox ESR"))}.get(page.split("#")[0], {})
    ui.go(page, **extra)
    be.settle()                                         # the real backend reads in threads
    if "--pp" in argv:
        ui.pp[page] = int(opt("--pp"))
    if "--pick" in argv:                                # a choice row's page, opened from PAGE
        ui.L = Layout(W, H)
        row = next(b for b in pages.build(ui, be).blocks if getattr(b, "pick_key", None) == opt("--pick"))
        row.fn()
    if "--ask" in argv:                                 # glassine-nm-agent's deep link: wifi-ask SSID [--eap]
        ui.ask(opt("--ask"), "--eap" in argv, opt("--uuid"))
    if "--user" in argv:
        ui.user, ui.fld = opt("--user"), "pw"
    if ui.page == "wifi-new" and "--pw" in argv:
        ui.pw = opt("--pw")
    if page == "wifi-net":
        ui.net_info = be.net_info(ui.net)
    if "--pk" in argv:                                  # a Packages state (the design's pkPreset)
        packages.preset(ui, opt("--pk"))
    if page == "admin-auth":                            # the password page, also with a wrong one
        ui.ask_admin("To change packages", lambda: None)
        ui.pw = opt("--pw") or ""
        if "--auth-err" in argv:
            ui.auth["err"] = "That isn’t an administrator’s password."
    sheet = opt("--sheet")
    if sheet == "forget-net":
        ui.net = "Pi-Home"
        ui.sheet_forget_net()
    elif sheet == "pair":
        ui.agent("display", "MX Keys Mini", "482 916", None)
    elif sheet == "pkg-error":                  # a title that wraps: the text below must clear it
        from .packages import Lines, MonoBox
        ui.show(Sheet("Checking for updates failed", None, "Close", None, icon="error", cancel="Details",
                      parts=[Lines([("apk stopped with this message. Nothing was changed.", False)]),
                             MonoBox("ERROR: Unable to lock database: Read-only file system")]))
    elif sheet == "error":
        ui.show(Sheet("Could not connect to Bibliothek", "Secrets were required, but not provided.", cancel="OK"))
    surf = cairo.ImageSurface(cairo.FORMAT_RGB24, W * 2, H * 2)
    surf.set_device_scale(2, 2)
    cr = cairo.Context(surf)
    r = Renderer(ui)
    r.draw(cr, W, H, 2)
    surf.write_to_png(out)
    if "--markers" in argv:
        import json
        with open(opt("--markers"), "w") as f:
            json.dump(r.markers, f, indent=1)


def main(argv):
    from . import backend
    be = backend.Demo() if "--demo" in argv else backend.Real()
    if not isinstance(be, backend.Demo) or "--render" not in argv:
        t = P_read(THEME_FILE)
        if t in THEMES:
            be.theme = t
    if "--render" in argv:
        render(argv, be)
        return 0
    GLib.set_prgname("glassine-settings")
    return App(be).run([a for a in argv if a != "--demo"])
