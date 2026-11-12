"""What glassine-settings reads and changes. Two backends with one interface:

  Demo   the design's sample data (host renders, tests: glassine-settings --demo)
  Real   the device: NetworkManager (nmcli), BlueZ (D-Bus, with a pairing
         agent), PipeWire (wpctl), /etc/glassine/power.conf (glassine-powerd; its managed
         light schedule in /run/glassine-light), the power mode (glassine-ddr),
         /etc/glassine/eink.conf (glassine-epdd-desktop), /etc/glassine/bgprio.conf (glassine-bgprio's
         freeze rules, via glassine-settings-root), the apps' .desktop files, the
         front light nodes, glassine-shell's theme, the keyboard files, the time
         zone (glassine-settings-root).

Attributes are plain values the pages read; methods change something and call
self.changed() when new values are in (slow ones run in a thread and report
back on the GLib main loop). Errors: self.error(title, text) -> a sheet.
"""
import json
import math
import os
import re
import subprocess
import threading
import time

from gi.repository import GLib

from . import homeconf      # favourites: Home's layout file (~/.config/glassine/home.conf)

HOME = os.environ.get("HOME", "/home/user")
RUN = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
POWER_CONF = "/etc/glassine/power.conf"
EINK_CONF = "/etc/glassine/eink.conf"
EPDD_CLASSES = "/run/glassine-epdd-desktop.classes"     # the window classes glassine-epdd-desktop has seen since it started
SEEN_CLASSES = HOME + "/.cache/glassine/epdd-classes"   # those, across boots
BGPRIO_CONF = "/etc/glassine/bgprio.conf"
MONO_ICONS = "/usr/share/icons/glassine-mono/scalable/apps/%s.svg"
APP_DIRS = ("/usr/share/applications", "/usr/local/share/applications", HOME + "/.local/share/applications",
            "/var/lib/flatpak/exports/share/applications", HOME + "/.local/share/flatpak/exports/share/applications")
OSK_LAYERS_USER = HOME + "/.config/glassine/osk-layers"
OSK_NO_AUTO = HOME + "/.config/glassine/osk-no-auto"
KBD_LAYOUT = HOME + "/.config/glassine/kbd-layout"     # the hardware keyboards' layout (KBD_LAYOUTS; absent: as the OSK)
KBD_LAYOUT_CMD = "/usr/bin/glassine-kbd-layout"  # writes sway's include + sets it live
# the hardware keyboard's layouts (xkb "LAYOUT [VARIANT]", as glassine-kbd-layout and glassine-setup write them)
KBD_LAYOUTS = [("de", "German", "QWERTZ"), ("de nodeadkeys", "German, no dead keys", "QWERTZ"),
               ("us", "English (US)", "QWERTY"), ("gb", "English (UK)", "QWERTY"),
               ("fr", "French", "AZERTY"), ("no", "Norwegian", "QWERTY")]
# the drawer opens each app on a workspace of its own (glassine-shell reads it at each launch)
# design : own workspace is the default; this file turns it off
APP_SHARED_WS = HOME + "/.config/glassine/app-shared-workspace"
APP_OWN_WS_OLD = HOME + "/.config/glassine/app-own-workspace"      # the old opt-in: no meaning now
CLOCK12 = HOME + "/.config/glassine-shell/clock12"
ROOT = "/usr/sbin/glassine-settings-root"
AUTH_REFUSED = 77                                # the helpers' "needs an administrator's password"
# a managed light never goes below this (%): in the dark the screen must stay readable
# (glassine-powerd and glassine-shell keep the same floor)
LIGHT_FLOOR = 5
LIGHT_RUN = "/run/glassine-light"                      # glassine-powerd: the zone's sunrise/sunset today
DDR_LEVEL = "/etc/glassine/ddr"                        # glassine-ddr's saved power mode
DDR_PARAMS = "/sys/module/qcom_cpufreq_hw/parameters"
ZONEINFO = "/usr/share/zoneinfo"
REGIONS = ("Africa", "America", "Antarctica", "Asia", "Atlantic", "Australia", "Europe", "Indian", "Pacific")
OSK_LAYERS = {"german": "german,special", "us": "full,special"}
EPD_GPU = "/etc/glassine/epd-gpu"                      # glassine-epdd-desktop: drive frames on the GPU
SWAY_GPU = "/etc/glassine/sway-gpu"                    # glassine-session-sway: sway renders with GLES
GPU_FALLBACK = "/run/glassine/gpu-fallback"      # this boot only (glassine-session-sway)
ROT_CONF = "/etc/glassine/rotation"                    # glassine-rotd: "auto" | "lock N"
APP_RENDER = HOME + "/.config/glassine/app-render.conf"   # glassine-app-render: "ID gpu|short|l3|cpu"
APP_RENDER_CMD = "/usr/bin/glassine-app-render"
APP_RENDERS = ("gpu", "short", "l3", "cpu")
ROT_RUN = "/run/glassine-rotation"                     # glassine-rotd: the output's transform now # glassine-session-sway: GLES sway died, pixman since
# Gestures: what a swipe of 3 or 4 fingers does; the sway session
# (0002-glassine-touch-gestures.patch) reads this file at each swipe: "FINGERS DIR ACTION"
GESTURES = HOME + "/.config/glassine/gestures"
# the sway session's keyboard shortcuts : its "## " labels, listed on
# the Keyboard page; next to this package (/usr/share/glassine/sway; the demo:
# the overlay's copy, so the list can't drift from the bindings)
KEYS_CONF = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../share/glassine/sway/keys.conf"))
KEY_NAMES = {"$mod": "Super", "Mod4": "Super", "Mod1": "Alt", "Control": "Ctrl", "Ctrl": "Ctrl", "space": "Space",
             "Return": "Enter", "Delete": "Del", "Super_L": "Super"}


def sway_shortcuts(path=KEYS_CONF):
    """[(keys, label)] from keys.conf: "## LABEL" above a bindsym (its keys,
    readable), or "## KEYS = LABEL" as written"""
    out, label = [], None
    try:
        with open(path) as f:
            lines = f.read().splitlines()
    except OSError:
        return out
    for ln in lines:
        ln = ln.strip()
        if ln.startswith("## "):
            keys, eq, lbl = ln[3:].partition(" = ")
            if eq:
                out.append((keys.strip(), lbl.strip()))
                label = None
            else:
                label = keys.strip()
        elif ln.startswith("bindsym ") and label:
            combo = next((w for w in ln.split()[1:] if not w.startswith("--")), "")
            keys = "+".join(KEY_NAMES.get(k, k.upper() if len(k) == 1 else k) for k in combo.split("+"))
            out.append((keys, label))
            label = None
        elif ln and not ln.startswith("#"):
            label = None
    return out
GESTURE_DIRS = ("up", "down", "left", "right")
GESTURE_ACTIONS = ("nothing", "refresh", "screen-next", "screen-prev", "move-next", "move-prev", "overview", "home",
                   "minimize", "fullscreen", "unfullscreen", "swap", "close", "quick", "notices",
                   "page-up", "page-down")
# ids of older files: read as the new ones, never written (README "Runtime interfaces"):
# maximize = full screen (it was hard to tell apart in a tiling WM), the launcher
# became Home, workspaces became screens
GESTURE_OLD = {"maximize": "fullscreen", "launcher": "home", "ws-next": "screen-next", "ws-prev": "screen-prev"}
# 3 fingers: screens left/right, full screen/minimize up/down; 4 fingers up: Home
# (spec 001 FR-030; it was Overview), down a full refresh, left/right move the window
GESTURE_DEFAULT = {(3, "up"): "fullscreen", (3, "down"): "minimize", (3, "left"): "screen-next", (3, "right"): "screen-prev",
                   (4, "up"): "home", (4, "down"): "refresh", (4, "left"): "move-next", (4, "right"): "move-prev",
                   (2, "tap"): "right-click"}
# a two-finger tap : a right click (sway has no long press = right click), or a swipe action
TAP_ACTIONS = ("right-click",) + GESTURE_ACTIONS


def read_gestures(path=GESTURES):
    g = dict(GESTURE_DEFAULT)
    try:
        with open(path) as f:
            for l in f:
                p = l.split("#")[0].split()
                if len(p) == 3:
                    p[2] = GESTURE_OLD.get(p[2], p[2])
                if len(p) == 3 and p[0] in ("3", "4") and p[1] in GESTURE_DIRS and p[2] in GESTURE_ACTIONS:
                    g[(int(p[0]), p[1])] = p[2]
                elif len(p) == 3 and p[0] == "2" and p[1] == "tap" and p[2] in TAP_ACTIONS:
                    g[(2, "tap")] = p[2]
    except OSError:
        pass
    return g


def write_gestures(g, path=GESTURES):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write("# glassine-settings > Gestures: FINGERS DIRECTION ACTION (the sway session reads it at each swipe)\n")
        f.write("2 tap %s\n" % g.get((2, "tap"), "right-click"))
        for n in (3, 4):
            for d in GESTURE_DIRS:
                f.write("%d %s %s\n" % (n, d, g.get((n, d), "nothing")))
    os.replace(tmp, path)


ARM_S = 6                                         # the desktop switch: a second tap within this


def hhmm(t=None):
    return time.strftime("%H:%M", time.localtime(t))


def rnd(v):
    """C's lround for v >= 0 (glassine-powerd computes the same schedule)"""
    return int(math.floor(v + 0.5))


def clamp100(v):
    return max(0, min(100, v))


def human(b):
    for unit, n in (("GB", 1e9), ("MB", 1e6), ("kB", 1e3)):
        if b >= n:
            return ("%.1f %s" % (b / n, unit)).replace(".0 ", " ")
    return "%d B" % b


# glassine-settings E-ink > Scrolling: the paint of a motion run's steps, adaptive
# (follows the speed, up to the fastest paint) or fixed (always that paint);
# the glassine-epdd-desktop policy's mmin/mmax steps (epd-policy.h). Payback is not a
# setting: glassine-epdd-desktop pays back every cut A2 (user); lines an older
# glassine-settings wrote for it are removed so the defaults apply
AHEAD = ("predict", "run", "off")          # glassine-epdd-desktop ahead= (settings "Next frame")
PAYBACK_KEYS = ("matched", "matched_from", "matched_cap", "model", "payback", "heal", "heal_from")
DITHER_PATS = ("blue", "bayer", "dizzy")   # glassine-epdd-desktop dither_pattern= / an app's dither=
APP_DITHERS = DITHER_PATS + ("off",)       # an app's dither= may also turn it off
# glassine-epdd-desktop's run steps (mmin=/mmax=/paint=), slowest first
LV = ("gc16", "du4", "a2", "a2_8", "a2_6", "a2_4", "a2_2")
# E-ink > Fluidity ("nonthreatening choices of fluidity";
# the lever is the frames a motion update gets): each step is the motion
# run's paint, glassine-epdd-desktop's mmax (mmin=gc16 curve=off still=range: still pages
# keep the sized choice). Calm: no motion paint, every change a GC16. An app's
# own: paint=gc16-STEP (Calm: paint=gc16). As glassine-shell's quick settings
FLUIDS = ("fluid", "balanced", "clear", "calm")
FLUID_LV = {"fluid": "a2_2", "balanced": "a2_4", "clear": "a2_8", "calm": "gc16"}


def fluid_of(lv):
    """the Fluidity step a run's fast end (an LV name) shows as (older lines: any step)"""
    i = LV.index(lv) if lv in LV else 6
    return "fluid" if i >= 6 else "balanced" if i >= 4 else "clear" if i >= 2 else "calm"


def fluid_paint(f):
    """an app's own Fluidity as its eink.conf paint="""
    return "gc16" if f == "calm" else "gc16-" + FLUID_LV[f]
FREEZES = ("off", "auto", "always")
# Apps > an app > Content (one choice, not two toggles):
# what counts as motion; Fluidity says how motion is drawn
CONTENTS = ("normal", "pages", "moving")


def app_content(a):
    """an app's Content (CONTENTS) from its motion=always / pages= (both set by
    hand: motion=always wins, as in glassine-epdd-desktop)"""
    return "moving" if a.get("always") else "pages" if a.get("pages") else "normal"
# an app's icon (Material Symbols) from its .desktop Categories, first hit wins
CAT_ICON = (("WebBrowser", "public"), ("TerminalEmulator", "terminal"), ("Email", "mail"), ("Calendar", "calendar_month"),
            ("FileManager", "folder"), ("Spreadsheet", "table_chart"), ("WordProcessor", "description"),
            ("Astronomy", "nights_stay"), ("Geography", "language"), ("Maps", "map"), ("Chess", "chess"),
            ("Game", "sports_esports"), ("Education", "school"), ("Science", "science"), ("Viewer", "menu_book"),
            ("Literature", "menu_book"), ("Audio", "music_note"), ("Video", "movie"), ("Graphics", "draw"),
            ("Office", "edit_note"), ("Development", "code"), ("PackageManager", "storefront"), ("Settings", "settings"),
            ("System", "settings"), ("Network", "language"), ("Utility", "build"))


# programs whose window belongs to another process name (glassine-bgprio's comm, the
# class glassine-epdd-desktop sees): KOReader's script runs luajit (eink.conf's default line)
PROC_ALIAS = {"koreader": "luajit"}


def desktop_apps():
    """the launchable apps (Type=Application, shown): dicts id, name, icon, match
    (glassine-epdd-desktop's: StartupWMClass, else the desktop id, lower case; a Flatpak: its id), ident (what an
    eink.conf line's MATCH may be part of), proc (glassine-bgprio's: a Flatpak's app
    id, else the program's name as ps shows it), by name"""
    seen = {}
    for d in APP_DIRS:
        try:
            files = sorted(os.listdir(d))
        except OSError:
            continue
        for f in files:
            if not f.endswith(".desktop") or f[:-8] in seen:
                continue
            e, sec = {}, None
            try:
                with open(os.path.join(d, f), errors="replace") as fh:
                    for l in fh:
                        l = l.strip()
                        if l.startswith("["):
                            sec = l
                            continue
                        if sec == "[Desktop Entry]" and "=" in l:
                            k, _, v = l.partition("=")
                            e.setdefault(k.strip(), v.strip())
            except OSError:
                continue
            seen[f[:-8]] = None
            if e.get("Type") != "Application" or e.get("NoDisplay") == "true" or e.get("Hidden") == "true" or not e.get("Name"):
                continue
            did = f[:-8]
            flatpak = "/flatpak/" in d
            prog = ""
            for t in e.get("Exec", "").split():
                if t == "env" or "=" in t:
                    continue
                prog = os.path.basename(t)
                break
            cats = e.get("Categories", "").split(";")
            icon = next((i for c, i in CAT_ICON if c in cats), "apps")
            if e.get("Icon") and os.path.exists(MONO_ICONS % e["Icon"]):    # its own, drawn for e-ink
                icon = e["Icon"]
            wmc = e.get("StartupWMClass", "").lower()
            if flatpak:
                prog = ""                              # "flatpak run ID": the id says it
                wmc = ""                               # (an X11 WM_CLASS: glassine-epdd-desktop sees the app_id + the Flatpak id,
                                                       # Chromium's "chromium-browser" line never matched)
            run_as = PROC_ALIAS.get(prog, prog)
            seen[did] = dict(id=did, name=e["Name"], icon=icon, match=wmc or did.lower(),
                             ident=" ".join(x for x in (wmc, did.lower(), prog.lower(), run_as) if x),
                             proc=did if flatpak else run_as[:15],
                             fp=("user" if d.startswith(HOME) else "system") if flatpak else "")
    apps = {}
    for a in seen.values():
        if a and (a["name"], bool(a["fp"])) not in apps:   # a Flatpak and a package of one name: two rows, the badges tell them apart
            apps[(a["name"], bool(a["fp"]))] = a
    return sorted(apps.values(), key=lambda a: a["name"].lower())


def seen_classes():
    """the window classes glassine-epdd-desktop matched its app lines against (EPDD_CLASSES:
    this epdd's run: the app_id + a Flatpak's id; an XWayland app's "class instance"),
    kept across boots in SEEN_CLASSES"""
    cur = [l for l in read(EPDD_CLASSES).splitlines() if l.strip() and not l.startswith(" ")]
    old = read(SEEN_CLASSES).splitlines()
    new = [l for l in cur if l not in old]
    if new:
        try:
            os.makedirs(os.path.dirname(SEEN_CLASSES), exist_ok=True)
            with open(SEEN_CLASSES, "a") as f:
                f.write("".join(l + "\n" for l in new))
        except OSError:
            pass
    return cur + [l for l in old if l not in cur]


def epdd_view(a, seen):
    """an app as glassine-epdd-desktop sees it: once its window was seen (a class with a word
    that is one of the app's names), its line's MATCH is a word of that class and
    ident the class itself (exactly what glassine-epdd-desktop's strstr finds); unseen: the
    desktop file's guess (desktop_apps). The guess missed: Chromium's
    StartupWMClass "chromium-browser" is the X11 class, sway says org.chromium.chromium"""
    did = a["id"].lower()
    names = set(a["ident"].split()) | {did, did.split(".")[-1]}
    hits = [c for c in seen if names & set(c.split())]
    if not hits:
        return
    cls = next((c for c in hits if did in c.split()), hits[0])
    words = cls.split()
    a["match"] = did if did in words else words[0]
    a["ident"] = cls


def read_app_render(path=APP_RENDER):
    """glassine-app-render's per-app profiles: {desktop id: short|l3|cpu} (gpu = no line)"""
    r = {}
    for l in read(path).splitlines():
        t = l.split("#")[0].split()
        if len(t) == 2 and t[1] in APP_RENDERS:
            r[t[0]] = t[1]
    return r


def paint_fluid(v):
    """an app line's paint=LO-HI | STEP as its Fluidity (the fast end), None if unknown"""
    lo, _, hi = v.partition("-")
    hi = hi or lo
    return fluid_of(hi) if lo in LV and hi in LV and LV.index(lo) <= LV.index(hi) else None


class Base:
    """the shared state + helpers; subclasses fill it"""

    def __init__(self):
        self.on_change = lambda: None
        self.on_error = lambda title, text: None
        self.on_agent = None             # (kind, name, code, reply(ok)) -> a pairing sheet
        self.on_auth = None              # (why, retry) -> the administrator password page
        self.added_fn = lambda: ()       # the packages added by (Packages)
        # wifi
        self.wifi, self.conn, self.nets, self.scan_at, self.wifi_busy = False, None, [], "", False
        self.wifi_dev = None
        # bluetooth
        self.bt, self.devs, self.near, self.bt_scan_at, self.bt_busy = False, [], [], "", False
        # sound
        self.vol, self.mute, self.outs, self.out, self.mic, self.sound_ok = 50, False, [], None, 50, False
        # light & e-ink
        self.light, self.warm, self.mode, self.profiles, self.idle_ms, self.dither = 0, 50, "auto", [], 250, True
        self.fluid = "fluid"            # Fluidity (FLUIDS): glassine-epdd-desktop's mmax
        self.payback = True             # Anti-ghosting: glassine-epdd-desktop payback=on|off
        self.ahead = "predict"          # Next frame: glassine-epdd-desktop ahead=predict|run|off (when sway may draw)
        self.dpat = "blue"              # dither_pattern=blue|bayer|dizzy (the dots of dither=on; an app's own: its dither=)
        self.dzreuse = False            # dizzy_reuse=on|off (dizzy: the dots move with a scroll)
        self.grey = "luminance"         # grey=luminance|luma (glassine-epdd-desktop: colours to grey)
        self.hammer = True              # hammer_top=2|0 (the rail hammer, glassine-epdd-desktop's default on)
        self.pointer = True             # pointer=on|off (sway: the mouse pointer on the panel, glassine-epdd-desktop's default on)
        self.boost_run = False          # boost_run=on|off (full CPU clock through a motion run; glassine-epdd-desktop's default off)
        self.debounce_ms = 150          # debounce_ms=N (the first still frame waits for quiet; 0 = off)
        self.debounce_osk = False       # debounce_osk=0|1 (also with the on-screen keyboard up)
        # Apps: dicts as desktop_apps + fluid (its own Fluidity or None) + always
        # (motion=always: Content "Moving") + pages (pages=: Content "Pages") + freeze (FREEZES)
        self.apps = []
        self.gestures = dict(GESTURE_DEFAULT)
        # rendering: panel = epdd's drive frames, desk = sway's renderer;
        # desk_armed: when the first tap of the desktop switch came (0 = not armed)
        self.gpu_panel, self.gpu_desk, self.gpu_fallback = False, False, ""
        self.desk_armed, self.desk_closes = 0, []         # desk_closes: app ids a restart closes
        # managed light: limits (%), the user's offsets, today's sun (local hours)
        self.managed = False
        self.lim = {"b_min": 20, "b_max": 80, "w_min": 10, "w_max": 90}
        self.off = {"b": 0, "w": 0}
        self.sun, self.located = (7.0, 19.0), False
        # power
        self.prof, self.prof_ok = "performance", False      # glassine-ddr level; the kernel has the voter
        self.batt = {"pct": -1, "status": "", "left": "", "health": ""}
        self.batt_at = ""
        self.pkey, self.cover, self.sleep, self.wifi_off = "suspend", True, 10, 0
        # keyboard, appearance, time
        self.osk, self.osk_auto, self.theme = "german", True, "white"
        self.kbd = ""                           # hardware keyboard layout: "" (as the OSK) | a KBD_LAYOUTS code
        self.shortcuts = sway_shortcuts()       # the sway session's (keys.conf)
        self.own_ws = True
        self.rot_auto, self.rot_lock, self.rot_now = True, 0, 0     # glassine-rotd (degrees, content turned clockwise)
        self.zone, self.h24, self.ntp, self.synced = ("Europe", "Berlin"), True, True, ""
        # storage, about
        self.space, self.folders, self.space_at = [], None, ""

    # -- the managed light's schedule (as glassine-powerd's managed_tick)
    def app(self, aid):
        return next((a for a in self.apps if a["id"] == aid), None)

    def customised(self, a):
        return (a["fluid"] is not None or a.get("always") or a.get("pages") or a["freeze"] != "auto" or bool(a.get("dither"))
                or a.get("render", "gpu") != "gpu")

    def favs(self):
        """Home's favourites (home.conf; without it the shell's old list, as Home imports it)"""
        return homeconf.read()[0]

    def set_fav(self, aid, on):
        """in or out of Home's favourites: only home.conf's favourites change (homeconf)"""
        try:
            fs = homeconf.set_fav(aid, on)
        except homeconf.Refused as e:
            self.error("Favourite not saved", str(e))
            fs = self.favs()
        except OSError as e:
            self.error("Favourite not saved", str(e))
            fs = self.favs()
        for a in self.apps:
            a["fav"] = a["id"] in fs
        self.changed()

    # -- where an app comes from (design origins): decides its badge, how it updates
    # and what Remove does -> (core|system|package|flatpak|android, package or app id, pack)
    def origin(self, a):
        return ("package", a["id"], None)

    def osk_show(self, on):
        """asks the system's keyboard for a drawn text field"""

    def app_removed(self, aid):
        """Remove ran through the change flow: its settings go too, the page says so"""
        a = self.app(aid)
        self.reset_app(aid)
        if a and a.get("fav"):
            self.set_fav(aid, False)
        if a:
            a["removed"] = True
        self.changed()

    def _try(self, fn):
        try:
            fn()
        except (OSError, RuntimeError) as e:
            self.error("Could not save the setting", str(e))
        self.changed()

    def reset_app(self, aid):
        self.set_app_fluid(aid, None)
        self.set_app_content(aid, "normal")
        self.set_app_dither(aid, None)
        self.set_app_freeze(aid, "auto")
        a = self.app(aid)
        if a and a.get("render", "gpu") != "gpu":
            self.set_app_render(aid, "gpu")

    # the desktop GPU switch (both backends; open_apps is theirs)
    def arm_desk(self):
        """the desktop switch's first tap: name what a restart closes"""
        self.desk_armed, self.desk_closes = time.time(), self.open_apps()
        self.changed()

    def desk_is_armed(self): return bool(self.desk_armed) and time.time() - self.desk_armed < ARM_S

    def now_hour(self):
        t = time.localtime()
        return t.tm_hour + t.tm_min / 60 + t.tm_sec / 3600

    def f_day(self, x):
        """0 at night, 1 by day; 90-minute fades centred on sunrise and sunset"""
        rise, set_ = self.sun
        c = lambda v: max(0.0, min(1.0, v))
        return c((x - (rise - 0.75)) / 1.5) * c((set_ + 0.75 - x) / 1.5)

    def sched(self, x):
        """-> the scheduled (brightness, warmth) % at hour x"""
        f, l = self.f_day(x), self.lim
        return rnd(l["b_min"] + (l["b_max"] - l["b_min"]) * f), rnd(l["w_max"] - (l["w_max"] - l["w_min"]) * f)

    def cur_light(self):
        """-> the (brightness, warmth) % that is on now"""
        if not self.managed:
            return self.light, self.warm
        b, w = self.sched(self.now_hour())
        b = clamp100(b + self.off["b"])
        return max(b, LIGHT_FLOOR), clamp100(w + self.off["w"])

    def changed(self):
        GLib.idle_add(lambda: (self.on_change(), False)[1])

    def error(self, title, text):
        GLib.idle_add(lambda: (self.on_error(title, text), False)[1])

    # an administrator's password (the root helpers' exit 77; UI.ask_admin)
    def admin_ok(self, level="password"):
        """would a command of that level run now? (no password needed, or entered recently)"""
        return run("doas", ROOT, "auth-check", level, timeout=10)[0] == 0

    def admin_auth(self, pw):
        """an administrator's password to the root helper -> True good, False wrong,
        None couldn't be checked (the helper's exit 2, or it didn't run)"""
        try:
            rc = subprocess.run(["doas", ROOT, "auth"], input=pw, text=True, capture_output=True, timeout=30).returncode
        except (OSError, subprocess.SubprocessError):
            return None
        return True if rc == 0 else False if rc == 1 else None

    def bg(self, fn, *a):
        """fn(*a) in a thread, then changed()"""
        def run():
            try:
                fn(*a)
            finally:
                self.changed()
        t = threading.Thread(target=run, daemon=True)
        self.threads = [x for x in getattr(self, "threads", []) if x.is_alive()] + [t]
        t.start()

    def settle(self, timeout=20):
        """wait for the background reads (--render: a snapshot of real values)"""
        end = time.time() + timeout
        while any(t.is_alive() for t in getattr(self, "threads", [])) and time.time() < end:
            for t in list(self.threads):
                t.join(max(0.05, end - time.time()))

    def refresh(self, page):
        """a page opens or its Refresh is tapped: re-read what it shows"""

    def refresh_screen(self):
        """the header's Refresh ("Refresh the screen"): one full
        e-ink refresh, after the redraw has reached the panel"""

    def zones(self):
        """{region: [city, ...]} (display names: _ -> space)"""
        out = {}
        for r in REGIONS:
            d = os.path.join(ZONEINFO, r)
            if not os.path.isdir(d):
                continue
            cities = []
            for root, dirs, files in os.walk(d):
                for f in files:
                    cities.append(os.path.relpath(os.path.join(root, f), d))
            if cities:
                out[r] = sorted(cities)
        return out

    def about(self):
        return []


# ---- the design's sample data -----------------------------------------------------


# the demo's apps (the design's appOrigins)
DEMO_ORG = {"Firefox ESR": ("system", "firefox", None), "KOReader": ("system", "koreader", None), "Terminal": ("core", "foot", None),
            "Stellarium": ("system", "stellarium", None), "Marble": ("package", "marble", None), "Software": ("core", "glassine-software", None),
            "Notes": ("system", "glassine-notes", None), "SAGA": ("package", "saga", None), "Gnumeric": ("package", "gnumeric", "Office"),
            "WordQuiz": ("package", "kwordquiz", None), "Files": ("core", "glassine-files", None), "Calendar": ("flatpak", "org.gnome.Calendar", None),
            "Mail": ("flatpak", "org.gnome.Geary", None), "Chess": ("package", "gnome-chess", "Games"), "OpenMW": ("flatpak", "org.openmw.OpenMW", None),
            "Transit": ("android", "com.example.transit", None)}


class Demo(Base):
    def admin_ok(self, level="password"):
        return True

    def admin_auth(self, pw):
        return pw == "glassine"

    def __init__(self):
        super().__init__()
        self.wifi, self.conn, self.scan_at = True, "Pi-Home", "20:11"
        self.nets = [dict(ssid=s, sig=g, lock=bool(l), known=bool(k), sec="WPA2 Personal" if l else "Open")
                     for s, g, l, k in [("Pi-Home", 4, 1, 1), ("Bibliothek", 3, 0, 0), ("FRITZ!Box 7590 KL", 3, 1, 0),
                                        ("Cafe Kranich", 2, 1, 1), ("Vodafone-2F4A", 2, 1, 0), ("eduroam", 1, 1, 1),
                                        ("DIRECT-roku-551", 1, 1, 0), ("Telekom_FON", 1, 0, 0)]]
        self.auto = {"Pi-Home": True, "Cafe Kranich": True, "eduroam": True}
        self.bt, self.bt_scan_at = True, "20:11"
        self.devs = [dict(id="kb", name="Logitech K380", icon="keyboard", type="Keyboard", addr="00:00:5E:00:53:01", conn=True, battery=80, trusted=True),
                     dict(id="hp", name="Sony WH-1000XM4", icon="headphones", type="Headset", addr="00:00:5E:00:53:02", conn=False, battery=None, trusted=True),
                     dict(id="pen", name="Wacom Bamboo Ink", icon="draw", type="Pen", addr="00:00:5E:00:53:03", conn=False, battery=None, trusted=True)]
        self.near = [dict(id="mx", name="MX Keys Mini", icon="keyboard", type="Keyboard"),
                     dict(id="jbl", name="JBL Flip 5", icon="speaker", type="Speaker"),
                     dict(id="px", name="Pixel 8", icon="smartphone", type="Phone")]
        self.vol, self.mic, self.sound_ok = 50, 60, True
        self.outs = [dict(id="spk", name="Speaker", sub="Built in", ok=True),
                     dict(id="hp", name="Sony WH-1000XM4", sub="Not connected", ok=False)]
        self.out = "spk"
        self.light, self.warm, self.mode, self.idle_ms, self.dither = 60, 100, "auto", 800, True
        if os.environ.get("GLASSINE_DEMO_DOTS"):   # (test.sh: a dither pattern's row)
            self.dpat = os.environ["GLASSINE_DEMO_DOTS"]
        self.fluid = "fluid"
        ov = {"KOReader": "calm", "Terminal": "balanced", "Stellarium": "clear"}
        frz = {"Terminal": "off", "Stellarium": "always", "Chess": "always"}
        ren = {"Software": "short", "Marble": "cpu", "Stellarium": "l3"}
        fav = ("KOReader", "Firefox ESR", "Notes", "Terminal", "Calendar", "Chess", "Files", "Mail")
        self.apps = [dict(id=n, name=n, icon=i, match=n.lower(), ident=n.lower(), proc=n.lower(), fluid=ov.get(n), always=n == "OpenMW", pages=n == "KOReader",
                          dither={"Stellarium": "dizzy", "KOReader": "bayer"}.get(n), freeze=frz.get(n, "auto"), fav=n in fav, desk=True,
                          render=ren.get(n, "gpu"), fp="system" if DEMO_ORG[n][0] == "flatpak" else "")
                     for n, i in [("Firefox ESR", "public"), ("KOReader", "menu_book"), ("Terminal", "terminal"),
                                  ("Stellarium", "nights_stay"), ("Marble", "language"), ("Software", "storefront"),
                                  ("Notes", "edit_note"), ("SAGA", "terrain"), ("Gnumeric", "table_chart"),
                                  ("WordQuiz", "quiz"), ("Files", "folder"), ("Calendar", "calendar_month"),
                                  ("Mail", "mail"), ("Chess", "chess"), ("OpenMW", "sports_esports"), ("Transit", "directions_transit")]]
        self.managed, self.sun, self.located = True, (7.3, 18.95), True
        self.prof, self.prof_ok = "balanced", True
        self.gpu_panel, self.gpu_desk, self.gpu_fallback = True, True, "19:42"
        self.hostname = "glassine"
        self.profiles = [["Firefox ESR", "fast"], ["KOReader", "quality"], ["Terminal", "fast"], ["Notes", "auto"],
                         ["Stellarium", "quality"], ["Gnumeric", "auto"], ["Marble", "fast"], ["Calendar", "auto"]]
        self.batt = {"pct": 69, "status": "Discharging · about 11 h left", "health": "Health 94 %"}
        self.batt_at = "20:11"
        self.pkey, self.cover, self.sleep, self.wifi_off = "suspend", True, 5, 15
        self.zone, self.synced = ("Europe", "Berlin"), "today at 19:02"
        self.space = [("Userdata", 18.2e9, 52e9), ("System", 6.1e9, 8e9)]
        self.folders = [("Books", 4.1e9), ("Downloads", 2.7e9), (".cache", 1.9e9), ("Music", 1.2e9), ("Maps", 0.82e9)]
        self.space_at = "20:11"

    def zones(self):
        return {"Africa": ["Cairo", "Johannesburg", "Lagos", "Nairobi"],
                "America": ["Chicago", "Los_Angeles", "Mexico_City", "New_York", "Sao_Paulo", "Toronto", "Vancouver"],
                "Asia": ["Bangkok", "Dubai", "Kolkata", "Seoul", "Shanghai", "Singapore", "Tokyo"],
                "Australia": ["Adelaide", "Brisbane", "Melbourne", "Perth", "Sydney"],
                "Europe": ["Amsterdam", "Athens", "Berlin", "Brussels", "Dublin", "Helsinki", "Lisbon", "London", "Madrid",
                           "Paris", "Prague", "Rome", "Stockholm", "Vienna", "Warsaw", "Zurich"],
                "Pacific": ["Auckland", "Fiji", "Honolulu"]}

    def net_info(self, ssid):
        c = ssid == self.conn
        return dict(ip="192.168.1.47" if c else "", mac="00:00:5E:00:53:04" if c else "", auto=self.auto.get(ssid, True))

    def about(self):
        return [("Model", "the device · 10.3″ e-ink"), ("Hostname", "glassine"), ("Kernel", "6.12.8-glassine"), ("Boot slot", "A"),
                ("Image", "glassine-ml 2026.09.28"), ("Battery health", "94 %"),
                ("IP address", "192.168.1.47" if self.conn else "–"), ("Uptime", "2 days, 4 h")]

    # changes: in memory only
    def set_wifi(self, on): self.wifi = on; self.changed()
    def connect(self, ssid, pw=None, user=None, uuid=None):
        self.conn = ssid
        for n in self.nets:
            if n["ssid"] == ssid:
                n["known"] = True
        self.changed()
    def forget(self, ssid):
        for n in self.nets:
            if n["ssid"] == ssid:
                n["known"] = False
        if self.conn == ssid:
            self.conn = None
        self.changed()
    def set_autoconnect(self, ssid, on): self.auto[ssid] = on; self.changed()
    def set_bt(self, on): self.bt = on; self.changed()
    def dev_connect(self, did, on):
        for d in self.devs:
            if d["id"] == did:
                d["conn"] = on
        self.changed()
    def dev_forget(self, did): self.devs = [d for d in self.devs if d["id"] != did]; self.changed()
    def set_trusted(self, did, on):
        for d in self.devs:
            if d["id"] == did:
                d["trusted"] = on
        self.changed()
    def discover(self): self.changed()
    def pair(self, did):
        d = next((x for x in self.near if x["id"] == did), None)
        if not d:
            return
        def reply(ok):
            if ok:
                self.devs.append(dict(d, addr="F0:5C:77:21:4B:" + did.upper()[:2], conn=True, battery=None, trusted=True))
                self.near = [x for x in self.near if x["id"] != did]
            self.changed()
        if self.on_agent:
            self.on_agent("display" if d["type"] == "Keyboard" else "confirm", d["name"], "482 916", reply)
    def cancel_pair(self, did): pass
    def set_vol(self, v): self.vol = v; self.changed()
    def set_mute(self, m): self.mute = m; self.changed()
    def set_out(self, oid): self.out = oid; self.changed()
    def set_mic(self, v): self.mic = v; self.changed()
    def set_light(self, light=None, warm=None):
        self.light = self.light if light is None else light
        self.warm = self.warm if warm is None else warm
        self.changed()
    def now_hour(self): return 20 + 11 / 60          # the design's clock
    def set_managed(self, on):
        if not on:
            b, w = self.cur_light()
            self.light, self.warm = round(b / 10) * 10, round(w / 10) * 10
        self.managed = on
        self.changed()
    def set_light_off(self, k, v): self.off[k] = v; self.changed()
    def set_light_lim(self, k, v): self.lim[k] = v; self.changed()
    def set_prof(self, v): self.prof = v; self.changed()
    def set_mode(self, m): self.mode = m; self.changed()
    def set_profile(self, app, m):
        for p in self.profiles:
            if p[0] == app:
                p[1] = m
        self.changed()
    def set_idle(self, ms): self.idle_ms = ms; self.changed()
    def set_dither(self, on): self.dither = on; self.changed()
    def set_dpat(self, v): self.dpat = v; self.changed()
    def set_dzreuse(self, on): self.dzreuse = on; self.changed()
    def set_grey(self, v): self.grey = v; self.changed()
    def set_hammer(self, on): self.hammer = on; self.changed()
    def set_pointer(self, on): self.pointer = on; self.changed()
    def set_boost_run(self, on): self.boost_run = on; self.changed()
    def set_debounce(self, ms): self.debounce_ms = ms; self.changed()
    def set_debounce_osk(self, on): self.debounce_osk = on; self.changed()
    def origin(self, a): return DEMO_ORG.get(a["id"], ("package", a["id"], None))
    def set_fluid(self, v): self.fluid = v; self.changed()
    def set_payback(self, on): self.payback = on; self.changed()
    def set_ahead(self, v): self.ahead = v; self.changed()
    def set_app_fluid(self, aid, v): self.app(aid)["fluid"] = v; self.changed()
    def set_app_content(self, aid, v):
        a = self.app(aid)
        a["always"], a["pages"] = v == "moving", v == "pages"
        self.changed()
    def set_app_dither(self, aid, v): self.app(aid)["dither"] = v; self.changed()
    def set_gesture(self, n, d, act): self.gestures[(n, d)] = act; self.changed()
    def set_app_freeze(self, aid, v): self.app(aid)["freeze"] = v; self.changed()
    def set_app_render(self, aid, v): self.app(aid)["render"] = v; self.changed()
    def set_fav(self, aid, on): self.app(aid)["fav"] = on; self.changed()
    def set_gpu_panel(self, on): self.gpu_panel = on; self.changed()
    def set_gpu_desk(self, on): self.gpu_desk, self.gpu_fallback, self.desk_armed = on, "", 0; self.changed()
    def open_apps(self): return ["firefox", "foot"]
    def set_power(self, key, val): setattr(self, key, val); self.changed()
    def set_osk(self, layout): self.osk = layout; self.changed()
    def set_kbd(self, lay): self.kbd = lay; self.changed()
    def set_osk_auto(self, on): self.osk_auto = on; self.changed()
    def set_own_ws(self, on): self.own_ws = on; self.changed()
    def set_theme(self, t): self.theme = t; self.changed()

    def set_rot_auto(self, on):
        self.rot_auto, self.rot_lock = on, self.rot_lock if on else self.rot_now
        self.changed()

    def set_rot_lock(self, d): self.rot_auto, self.rot_lock = False, d; self.changed()
    def set_zone(self, region, city): self.zone = (region, city); self.changed()
    def set_h24(self, on): self.h24 = on; self.changed()
    def set_ntp(self, on): self.ntp = on; self.changed()


# ---- the device -------------------------------------------------------------------


def tz_reload():
    """musl loads /etc/localtime once and keeps it while TZ stays the same string
    (as glassine-shell's tz_follow): flip TZ between two names of the same file so the
    next localtime() reads the new zone. A TZ the session set stays."""
    tz = os.environ.get("TZ")
    if tz not in (None, "/etc/localtime", ":/etc/localtime"):
        return
    os.environ["TZ"] = "/etc/localtime" if tz == ":/etc/localtime" else ":/etc/localtime"
    time.tzset()


def run(*cmd, timeout=20, check=False):
    """-> (returncode, stdout, stderr)"""
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, p.stdout, p.stderr.strip()
    except (OSError, subprocess.SubprocessError) as e:
        return 127, "", str(e)


def out(*cmd, timeout=20):
    return run(*cmd, timeout=timeout)[1]


def terse(line):
    """one nmcli -t line -> fields (':' separates, '\\:' is a colon)"""
    return [f.replace("\x00", ":").replace("\\\\", "\\") for f in line.replace("\\:", "\x00").split(":")]


def read(path, default=""):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


def _int(s, default):
    try:
        return int(s)
    except (TypeError, ValueError):
        return default


def read_int(path, default=0):
    try:
        return int(read(path, str(default)))
    except ValueError:
        return default


CORE_PKGS = ("foot", "thunar", "nautilus", "gnome-software", "glassine-settings", "glassine-settings")
CORE_IDS = ("io.github.kolunmi.Bazaar", "org.gnome.Software", "org.gnome.Nautilus", "thunar", "foot", "glassine-settings")
SETUP_CONF, PACKS_CONF = "/etc/glassine/setup.conf", "/usr/share/glassine/setup/packs.conf"


def conf_value(text, key):
    for l in text.splitlines():
        k, _, v = l.partition("=")
        if k.strip() == key:
            return v.strip()
    return ""


class Conf:
    """a key=value or @key=value file edited in place: comments, order and the
    other lines kept (group-writable files in root's /etc/glassine: no temp file +
    rename, the dir is not ours). Every edit holds an exclusive flock from the
    read to the write (glassine-shell locks the same file), and the write never truncates first: blocks reserved, then the new
    bytes over the old, then the length cut. A reader never sees an empty file,
    and a full disk fails the edit before anything changes."""

    def __init__(self, path, prefix=""):
        self.path, self.prefix = path, prefix

    def lines(self):
        """the file's lines; [] only when it doesn't exist (any other read error raises)"""
        try:
            with open(self.path) as f:
                return f.read().splitlines()
        except FileNotFoundError:
            return []
        except OSError as e:
            raise RuntimeError("%s: %s" % (self.path, e.strerror))

    def get(self, key, default=None):
        try:
            ls = self.lines()
        except RuntimeError:
            return default
        for l in ls:
            k, sep, v = l.partition("=")
            if sep and k.strip() == self.prefix + key:
                return v.strip()
        return default

    def set(self, key, val):
        self.set_many(((key, val),))

    def set_many(self, kv):
        """several keys in one write: the reader (glassine-epdd-desktop) never sees half of them"""
        def f(ls):
            ls = list(ls)
            for key, val in kv:
                done = False
                for i, l in enumerate(ls):
                    k, sep, _ = l.partition("=")
                    if sep and k.strip() == self.prefix + key:
                        ls[i], done = "%s%s=%s" % (self.prefix, key, val), True
                if not done:
                    ls.insert(self._insert_at(ls), "%s%s=%s" % (self.prefix, key, val))
            return ls
        self.edit(f)

    def unset(self, key):
        self.edit(lambda ls: [l for l in ls if l.partition("=")[0].strip() != self.prefix + key])

    def _insert_at(self, ls):
        return len(ls)

    def check(self, old, new, existed):
        """refuse an edit (RuntimeError); EinkConf guards its globals"""

    def edit(self, fn):
        """fn(lines) -> new lines, under the lock; no write when they are the same"""
        existed = True
        try:
            try:
                fd = os.open(self.path, os.O_RDWR)
            except FileNotFoundError:
                fd, existed = os.open(self.path, os.O_RDWR | os.O_CREAT, 0o664), False
        except OSError as e:
            raise RuntimeError("%s: %s" % (self.path, e.strerror))
        try:
            import fcntl
            fcntl.flock(fd, fcntl.LOCK_EX)
            data, chunk = b"", os.read(fd, 65536)
            while chunk:
                data += chunk
                chunk = os.read(fd, 65536)
            old = data.decode(errors="replace").splitlines()
            new = fn(old)
            if new == old:
                return
            self.check(old, new, existed)
            out = ("\n".join(new) + "\n").encode() if new else b""
            if len(out) > len(data):
                os.posix_fallocate(fd, 0, len(out))     # ENOSPC here: nothing changed yet
            os.pwrite(fd, out, 0)
            os.ftruncate(fd, len(out))
            os.fsync(fd)
        except OSError as e:
            raise RuntimeError("%s: %s" % (self.path, e.strerror))
        finally:
            os.close(fd)                                # (drops the lock)

    def write(self, ls):
        self.edit(lambda _: list(ls))


class EinkConf(Conf):
    """eink.conf: @key=value globals, 'MATCH mode=… touch=… ink=…' profiles, '*' = the rest"""

    def __init__(self):
        super().__init__(EINK_CONF, "@")

    def _insert_at(self, ls):
        for i, l in enumerate(ls):
            if l and not l.startswith("#") and not l.startswith("@"):
                return i
        return len(ls)

    def profiles(self):
        out = []
        for l in self.lines():
            p = l.split()
            if not p or p[0].startswith(("#", "@")):
                continue
            kv = dict(t.partition("=")[::2] for t in p[1:])
            out.append([p[0], kv.get("mode", "auto")])
        return out

    def prof_lines(self):
        """[(MATCH, {key: value})] of the app lines (not '*')"""
        out = []
        for l in self.lines():
            p = l.split()
            if p and not p[0].startswith(("#", "@")) and p[0] != "*":
                out.append((p[0], dict(t.partition("=")[::2] for t in p[1:])))
        return out

    def check(self, old, new, existed):
        """eink.conf's globals and its '*' line are never dropped as a side effect:
        an edit that leaves none of them where the file had some is refused, and
        so is any edit of a file that exists but reads empty (damaged: epdd
        creates it with a header; a stripped file once got rebuilt from app
        lines alone)"""
        keep = lambda ls: [l for l in ls if l.startswith(("#", "@")) or l.split()[:1] == ["*"]]
        if existed and not old:
            raise RuntimeError("%s is empty: not editing it (restore it first)" % self.path)
        if keep(old) and not keep(new):
            raise RuntimeError("%s: the edit would drop every global and the '*' line: refused" % self.path)

    def set_kv(self, match, key, val):
        """key=val on MATCH's line (None: off it); a new line holds only that key
        (glassine-epdd-desktop: an app line's missing mode=/touch= are the '*' line's); a line
        left with nothing but its MATCH goes"""
        def f(ls):
            done, out = False, []
            for l in ls:
                p = l.split()
                if p and p[0] == match and not done:
                    done = True
                    p = [t for t in p if not t.startswith(key + "=")]
                    if val is not None:
                        p.append("%s=%s" % (key, val))
                    if len(p) == 1:
                        continue
                    l = " ".join(p)
                out.append(l)
            if not done and val is not None:
                out.append("%s %s=%s" % (match, key, val))
            return out
        self.edit(f)

    def set_mode(self, match, mode):
        def f(ls):
            ls, done = list(ls), False
            for i, l in enumerate(ls):
                p = l.split()
                if p and p[0] == match:
                    if any(t.startswith("mode=") for t in p[1:]):
                        p = [p[0]] + ["mode=" + mode if t.startswith("mode=") else t for t in p[1:]]
                    else:
                        p.append("mode=" + mode)
                    ls[i], done = " ".join(p), True
            if not done:
                ls.append("%s mode=%s touch=%s ink=off" % (match, mode, "fast" if match == "*" else "auto"))
            return ls
        self.edit(f)


# the front light: this device's sysfs nodes, until they come from per-device config
FL = "/sys/class/backlight/frontlight-%s/brightness"
BL = "/sys/class/backlight/onyx_bl_%s/"


def light_write(br, ct):
    if os.path.exists(FL % "a"):
        ct = max(0, min(24, ct))
        total = 0 if br <= 0 else 255 if br >= 28 else 1 + 254 * br * br // (28 * 28)
        writes = ((FL % "a", total * ct // 24), (FL % "b", total * (24 - ct) // 24))
    else:
        writes = ((BL % "ct" + "brightness", ct), (BL % "br" + "brightness", br))
    for path, v in writes:
        try:
            with open(path, "w") as f:
                f.write("%d\n" % v)
        except OSError:
            pass


def dev_kind(icon):
    """BlueZ's Icon -> (our icon, type name)"""
    return {"input-keyboard": ("keyboard", "Keyboard"), "input-mouse": ("mouse", "Mouse"),
            "input-tablet": ("draw", "Pen"), "input-gaming": ("gamepad", "Game controller"),
            "audio-headset": ("headset", "Headset"), "audio-headphones": ("headphones", "Headphones"),
            "audio-card": ("speaker", "Speaker"), "phone": ("smartphone", "Phone"),
            "computer": ("computer", "Computer")}.get(icon or "", ("bluetooth", "Device"))


class Real(Base):
    def __init__(self):
        super().__init__()
        self.bus = None
        self.agent_id = 0
        self.power = Conf(POWER_CONF)
        self.eink = EinkConf()
        self.br_max = read_int(BL % "br" + "max_brightness", 28)
        self.ct_max = read_int(BL % "ct" + "max_brightness", 24)
        self._known = {}                 # ssid -> connection name
        self._desk = None                # desktop_apps, read once
        self._zones = None
        self.read_local()

    # -- cheap reads (files), at start and on every page open
    def read_local(self):
        pw = self.power
        self.pkey = pw.get("power_key", "suspend")
        self.cover = pw.get("cover", "suspend") != "ignore"
        self.sleep = int(pw.get("idle_sleep_min", "10") or 10)
        self.wifi_off = int(pw.get("wifi_off_after_min", "0") or 0)
        br, ct = int(pw.get("light_br", "0") or 0), int(pw.get("light_ct", "12") or 12)
        self.light = round(br * 100 / self.br_max / 10) * 10 if self.br_max else 0
        self.warm = round(ct * 100 / self.ct_max / 10) * 10 if self.ct_max else 0
        self.managed = (pw.get("light_managed", "0") or "0") not in ("0", "")
        for k in self.lim:
            self.lim[k] = clamp100(_int(pw.get("light_" + k), self.lim[k]))
        for k in self.off:
            self.off[k] = max(-50, min(50, _int(pw.get("light_%s_off" % k), 0)))
        run_ = dict(l.partition("=")[::2] for l in read(LIGHT_RUN).splitlines())
        try:
            self.sun = (float(run_["sunrise"]), float(run_["sunset"]))
            self.located = run_.get("located") == "1"
        except (KeyError, ValueError):
            self.sun, self.located = (7.0, 19.0), False      # glassine-powerd's fallback day
        self.prof = read(DDR_LEVEL) or "performance"
        self.prof_ok = os.path.exists(DDR_PARAMS + "/ddr_min_kbps")
        e = self.eink
        prof = e.profiles()
        star = [m for a, m in prof if a == "*"]
        self.mode = star[0] if star else "auto"
        self.profiles = [p for p in prof if p[0] != "*"]
        self.idle_ms = int(e.get("idle_ms", "250") or 250)
        self.dither = e.get("dither", "on") != "off"
        self.dpat = e.get("dither_pattern", "blue") if e.get("dither_pattern") in DITHER_PATS else "blue"
        self.dzreuse = e.get("dizzy_reuse") == "on"
        self.grey = "luma" if e.get("grey") == "luma" else "luminance"
        self.hammer = e.get("hammer_top", "2") not in ("0", "")
        self.pointer = e.get("pointer", "on") != "off"
        self.boost_run = e.get("boost_run", "off") == "on"
        try:
            self.debounce_ms = int(e.get("debounce_ms", "150"))
        except ValueError:
            self.debounce_ms = 150
        self.debounce_osk = e.get("debounce_osk", "0") == "1"
        self.fluid = fluid_of(e.get("mmax", "a2_2"))      # glassine-epdd-desktop's default: A2/2
        self.payback = e.get("payback", "on") != "off"
        self.ahead = e.get("ahead", "predict") if e.get("ahead", "predict") in AHEAD else "predict"
        self.read_apps()
        self.gestures = read_gestures()
        self.gpu_panel = os.path.exists(EPD_GPU)
        self.gpu_desk = os.path.exists(SWAY_GPU)
        fb = read(GPU_FALLBACK)
        self.gpu_fallback = fb.split(" ", 1)[0] if fb else ""
        lay = read(OSK_LAYERS_USER) or read("/etc/glassine/osk-layers") or OSK_LAYERS["german"]
        self.osk = "us" if lay.split(",")[0] == "full" else "german"
        self.osk_auto = not os.path.exists(OSK_NO_AUTO)
        self.kbd = read(KBD_LAYOUT) if read(KBD_LAYOUT) in [c for c, _, _ in KBD_LAYOUTS] else ""
        self.own_ws = not os.path.exists(APP_SHARED_WS)
        self.theme = read(RUN + "/glassine-shell.theme") or read(HOME + "/.config/glassine-shell/theme") or "white"
        rc = (read(ROT_CONF) or "auto").split()
        self.rot_auto = rc[0] != "lock"
        self.rot_lock = _int(rc[1], 0) % 360 // 90 * 90 if len(rc) > 1 else 0
        self.rot_now = _int(read(ROT_RUN), 0) % 360 // 90 * 90
        self.h24 = not os.path.exists(CLOCK12)
        z = os.path.realpath("/etc/localtime")
        rel = os.path.relpath(z, ZONEINFO) if z.startswith(ZONEINFO + "/") else read("/etc/timezone")
        if "/" in rel:
            r, _, c = rel.partition("/")
            self.zone = (r, c)
        self.ntp = not os.path.exists("/etc/glassine/ntp-off")
        st = "/var/lib/glassine/ntp-synced"
        self.synced = time.strftime("%-d %b at %H:%M", time.localtime(os.stat(st).st_mtime)) if os.path.exists(st) else ""
        self.read_battery()

    def read_apps(self):
        """each app's overrides: its eink.conf line (paint=) and bgprio.conf rule;
        eink.conf lines that are no listed app's become rows of their own"""
        if self._desk is None:
            self._desk = desktop_apps()
        profs = self.eink.prof_lines()
        nofrz, alw = set(), set()
        for l in read(BGPRIO_CONF).splitlines():
            t = l.split("#")[0].split()
            if t and t[0] in ("nofreeze", "freeze"):
                (nofrz if t[0] == "nofreeze" else alw).update(t[1:])
        used, apps = set(), []
        favs = set(self.favs())
        seen = seen_classes()
        for d in self._desk:
            a = dict(d, fav=d["id"] in favs, desk=True)
            epdd_view(a, seen)
            # glassine-epdd-desktop's rule: the first line whose MATCH is part of the app's class
            line = next((m for m, kv in profs if m == a["match"]), None) or \
                next((m for m, kv in profs if m in a["ident"]), None)
            if line:
                a["match"] = line
                used.add(line)
            apps.append(a)
        for m, kv in profs:
            if m not in used and not any(x["match"] == m for x in apps):
                apps.append(dict(id="eink:" + m, name=m[:1].upper() + m[1:], icon="apps", match=m, ident=m, proc=m[:15],
                                 fav=False, desk=False))
        kvs = dict(profs)
        ren = read_app_render()
        for a in apps:
            a["render"] = ren.get(a["id"], "gpu")
            kv = kvs.get(a["match"], {})
            a["fluid"] = paint_fluid(kv["paint"]) if "paint" in kv else None
            a["always"] = kv.get("motion") == "always"
            a["pages"] = kv.get("pages", "off") not in ("off", "0")
            a["dither"] = kv["dither"] if kv.get("dither") in APP_DITHERS else None
            a["freeze"] = "off" if a["proc"] in nofrz else "always" if a["proc"] in alw else "auto"
        self.apps = apps

    def read_battery(self):
        base = "/sys/class/power_supply/"
        for n in sorted(os.listdir(base)) if os.path.isdir(base) else []:
            p = base + n + "/"
            if read(p + "type") != "Battery":
                continue
            pct = read_int(p + "capacity", -1)
            status = read(p + "status") or "Unknown"
            left = ""
            tte = read_int(p + "time_to_empty_now", 0) or read_int(p + "time_to_empty_avg", 0)
            if tte and status == "Discharging":
                h = tte / 3600
                left = " · about %d h left" % round(h) if h >= 1 else " · about %d min left" % round(tte / 60)
            full = read_int(p + "charge_full", 0) or read_int(p + "energy_full", 0)
            design = read_int(p + "charge_full_design", 0) or read_int(p + "energy_full_design", 0)
            health = "%d %%" % round(full * 100 / design) if full and design else "–"
            self.batt = {"pct": pct, "status": status + left, "health": "Health " + health if health != "–" else "",
                         "health_v": health}
            break
        self.batt_at = hhmm()

    def refresh_screen(self):
        GLib.timeout_add(250, lambda: (run("glassine-refresh"), False)[1])

    def refresh(self, page):
        self.read_local()
        top = page.split("-")[0]
        if top == "wifi" or page == "about":
            self.bg(self.read_wifi, page == "wifi")
        if top == "bt" or top == "sound":
            self.bg(self.read_bt)
        if top == "sound":
            self.bg(self.read_sound)
        if page == "bt-add":
            self.discover()
        if page == "storage":
            self.bg(self.measure)

    # -- Wi-Fi (NetworkManager)
    def read_wifi(self, rescan=False):
        self.wifi = out("nmcli", "radio", "wifi").strip() == "enabled"
        for l in out("nmcli", "-t", "-f", "DEVICE,TYPE", "dev").splitlines():
            f = terse(l)
            if len(f) >= 2 and f[1] == "wifi":
                self.wifi_dev = f[0]
        known = {}
        for l in out("nmcli", "-t", "-f", "NAME,TYPE", "con", "show").splitlines():
            f = terse(l)
            if len(f) >= 2 and f[1] == "802-11-wireless":
                ssid = out("nmcli", "-g", "802-11-wireless.ssid", "con", "show", "id", f[0]).strip() or f[0]
                known[ssid] = f[0]
        self._known = known
        if not self.wifi:
            self.nets, self.conn = [], None
            return
        if rescan:
            run("nmcli", "dev", "wifi", "rescan", timeout=15)
        nets, conn = {}, None
        for l in out("nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "dev", "wifi", "list", "--rescan", "no").splitlines():
            f = terse(l)
            if len(f) < 4 or not f[1]:
                continue
            use, ssid, sig, sec = f[0] == "*", f[1], int(f[2] or 0), f[3]
            bars = 4 if sig >= 75 else 3 if sig >= 50 else 2 if sig >= 25 else 1
            if use:
                conn = ssid
            if ssid not in nets or nets[ssid]["sig"] < bars:
                lock = bool(sec and sec != "--")
                nets[ssid] = dict(ssid=ssid, sig=bars, lock=lock, known=ssid in known,
                                  sec=("WPA2/3 Personal" if "WPA" in sec else sec) if lock else "Open")
        self.nets, self.conn = list(nets.values()), conn
        self.scan_at = hhmm()

    def net_info(self, ssid):
        info = dict(ip="", mac="", auto=True)
        name = self._known.get(ssid)
        if name:
            info["auto"] = out("nmcli", "-g", "connection.autoconnect", "con", "show", "id", name).strip() != "no"
        if ssid == self.conn and self.wifi_dev:
            for l in out("nmcli", "-t", "-f", "IP4.ADDRESS,GENERAL.HWADDR", "dev", "show", self.wifi_dev).splitlines():
                k, _, v = l.partition(":")
                v = v.replace("\\:", ":")
                if k.startswith("IP4.ADDRESS") and not info["ip"]:
                    info["ip"] = v.split("/")[0]
                elif k == "GENERAL.HWADDR":
                    info["mac"] = v
        return info

    def set_wifi(self, on):
        self.wifi_busy = True
        self.changed()

        def go():
            run("nmcli", "radio", "wifi", "on" if on else "off")
            time.sleep(2 if on else 0)
            self.read_wifi(on)
            self.wifi_busy = False
        self.bg(go)

    def connect(self, ssid, pw=None, user=None, uuid=None):
        """pw (+ user: an 802.1X login) given for a saved profile = its secret failed
        (glassine-nm-agent's notice): store the new one in the profile, then bring it up.
        uuid (from the notice): exactly that profile; else the SSID's (the last one
        nmcli lists, when several share it)"""
        self.wifi_busy = True
        self.changed()

        def go():
            if uuid:
                ref = ["uuid", uuid]
            else:
                name = self._known.get(ssid)
                if name is None and (pw or user):
                    self.read_wifi()                    # opened from a notice before the first scan
                    name = self._known.get(ssid)
                ref = ["id", name] if name else None
            if ref and (pw or user):
                mod = ["802-1x.identity", user, "802-1x.password", pw] if user is not None else ["wifi-sec.psk", pw]
                rc, _, err = run("nmcli", "con", "mod", *ref, *mod)
                if not rc:
                    rc, _, err = run("nmcli", "con", "up", *ref, timeout=60)
            elif ref:
                rc, _, err = run("nmcli", "con", "up", *ref, timeout=60)
            else:
                cmd = ["nmcli", "dev", "wifi", "connect", ssid] + (["password", pw] if pw else [])
                rc, _, err = run(*cmd, timeout=60)
            if rc:
                self.error("Could not connect to " + ssid, err or "NetworkManager gave no reason.")
            self.read_wifi()
            self.wifi_busy = False
        self.bg(go)

    def forget(self, ssid):
        name = self._known.get(ssid, ssid)
        self.bg(lambda: (run("nmcli", "con", "delete", "id", name), self.read_wifi()))

    def set_autoconnect(self, ssid, on):
        name = self._known.get(ssid)
        if name:
            self.bg(lambda: run("nmcli", "con", "mod", "id", name, "connection.autoconnect", "yes" if on else "no"))

    # -- Bluetooth (BlueZ over D-Bus)
    def _bus(self):
        if self.bus is None:
            from gi.repository import Gio
            self.bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        return self.bus

    def _call(self, path, iface, method, args=None, rtype=None, timeout=25000):
        from gi.repository import Gio
        return self._bus().call_sync("org.bluez", path, iface, method, args, rtype, Gio.DBusCallFlags.NONE, timeout, None)

    def _prop(self, path, iface, name, value):
        self._call(path, "org.freedesktop.DBus.Properties", "Set", GLib.Variant("(ssv)", (iface, name, value)))

    def _objects(self):
        try:
            r = self._call("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", None,
                           GLib.VariantType.new("(a{oa{sa{sv}}})"))
            return r.unpack()[0]
        except Exception:
            return {}

    def read_bt(self):
        objs = self._objects()
        self.adapter = next((p for p, i in objs.items() if "org.bluez.Adapter1" in i), None)
        self.bt = bool(self.adapter and objs[self.adapter]["org.bluez.Adapter1"].get("Powered"))
        devs, near = [], []
        for p, i in objs.items():
            d = i.get("org.bluez.Device1")
            if not d:
                continue
            icon, kind = dev_kind(d.get("Icon"))
            e = dict(id=p, name=d.get("Alias") or d.get("Name") or d.get("Address", "?"), icon=icon, type=kind,
                     addr=d.get("Address", ""), conn=bool(d.get("Connected")), trusted=bool(d.get("Trusted")),
                     battery=i.get("org.bluez.Battery1", {}).get("Percentage"))
            if d.get("Paired"):
                devs.append(e)
            elif d.get("Name"):                       # nameless = noise
                near.append((d.get("RSSI", -999), e))
        self.devs = sorted(devs, key=lambda e: (not e["conn"], e["name"].lower()))
        self.near = [e for _, e in sorted(near, key=lambda x: -x[0])]

    def set_bt(self, on):
        self.bt_busy = True
        self.changed()

        def go():
            try:                     # rfkill first (as quick settings: blueman's mechanism), then BlueZ
                from gi.repository import Gio
                self._bus().call_sync("org.blueman.Mechanism", "/org/blueman/mechanism", "org.blueman.Mechanism",
                                      "SetRfkillState", GLib.Variant("(b)", (on,)), None, Gio.DBusCallFlags.NONE, 10000, None)
            except Exception:
                pass
            for _ in range(10):
                self.read_bt()
                if self.adapter:
                    break
                time.sleep(0.5)
            # the chip reloads its firmware after the unblock (about 1 s) and BlueZ
            # refuses Powered meanwhile: only the end state counts
            err = None
            for _ in range(20):
                try:
                    if self.adapter:
                        self._prop(self.adapter, "org.bluez.Adapter1", "Powered", GLib.Variant("b", on))
                    err = None
                except Exception as e:
                    err = _dbus_msg(e)
                time.sleep(0.5)
                self.read_bt()
                if self.bt == on:
                    err = None
                    break
            if err:
                self.error("Bluetooth", err)
            self.bt_busy = False
        self.bg(go)

    def dev_connect(self, did, on):
        def go():
            try:
                self._call(did, "org.bluez.Device1", "Connect" if on else "Disconnect", timeout=30000)
            except Exception as e:
                self.error("Could not %s" % ("connect" if on else "disconnect"), _dbus_msg(e))
            self.read_bt()
        self.bg(go)

    def dev_forget(self, did):
        def go():
            try:
                self._call(self.adapter, "org.bluez.Adapter1", "RemoveDevice", GLib.Variant("(o)", (did,)))
            except Exception as e:
                self.error("Could not forget the device", _dbus_msg(e))
            self.read_bt()
        self.bg(go)

    def set_trusted(self, did, on):
        self.bg(lambda: self._prop(did, "org.bluez.Device1", "Trusted", GLib.Variant("b", on)))

    def discover(self):
        """10 s of discovery, then the list is drawn once"""
        if self.bt_busy or not self.adapter:
            return
        self.bt_busy = True

        def go():
            try:
                self._call(self.adapter, "org.bluez.Adapter1", "StartDiscovery")
                time.sleep(10)
                self._call(self.adapter, "org.bluez.Adapter1", "StopDiscovery")
            except Exception:
                pass
            self.read_bt()
            self.bt_scan_at = hhmm()
            self.bt_busy = False
        self.bg(go)

    # the pairing agent: BlueZ asks it for confirmations/codes; the app shows a sheet
    AGENT = """<node><interface name='org.bluez.Agent1'>
      <method name='Release'/>
      <method name='RequestPinCode'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>
      <method name='DisplayPinCode'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
      <method name='RequestPasskey'><arg type='o' direction='in'/><arg type='u' direction='out'/></method>
      <method name='DisplayPasskey'><arg type='o' direction='in'/><arg type='u' direction='in'/><arg type='q' direction='in'/></method>
      <method name='RequestConfirmation'><arg type='o' direction='in'/><arg type='u' direction='in'/></method>
      <method name='RequestAuthorization'><arg type='o' direction='in'/></method>
      <method name='AuthorizeService'><arg type='o' direction='in'/><arg type='s' direction='in'/></method>
      <method name='Cancel'/></interface></node>"""
    AGENT_PATH = "/org/glassine/settings/agent"

    def _agent_register(self):
        if self.agent_id:
            return
        from gi.repository import Gio
        node = Gio.DBusNodeInfo.new_for_xml(self.AGENT)
        self.agent_id = self._bus().register_object(self.AGENT_PATH, node.interfaces[0], self._agent_call, None, None)
        self._call("/org/bluez", "org.bluez.AgentManager1", "RegisterAgent",
                   GLib.Variant("(os)", (self.AGENT_PATH, "KeyboardDisplay")))

    def _agent_call(self, conn, sender, path, iface, method, params, inv):
        name = next((d["name"] for d in self.near + self.devs if d["id"] == (params.unpack()[0] if params.n_children() else "")), "the device")
        code = lambda n: "%03d %03d" % divmod(n, 1000)
        if method == "RequestConfirmation":
            self.on_agent("confirm", name, code(params.unpack()[1]),
                          lambda ok: inv.return_value(None) if ok else inv.return_dbus_error("org.bluez.Error.Rejected", "rejected"))
        elif method == "DisplayPasskey":
            self.on_agent("display", name, code(params.unpack()[1]), lambda ok: None)
            inv.return_value(None)
        elif method == "DisplayPinCode":
            self.on_agent("display", name, params.unpack()[1], lambda ok: None)
            inv.return_value(None)
        elif method == "RequestPinCode":
            inv.return_value(GLib.Variant("(s)", ("0000",)))     # legacy devices; most take 0000
        elif method == "RequestPasskey":
            inv.return_value(GLib.Variant("(u)", (0,)))
        else:                                                      # Authorization, Service, Release, Cancel
            inv.return_value(None)

    def pair(self, did):
        def go():
            try:
                self._agent_register()
                try:
                    self._call(did, "org.bluez.Device1", "Pair", timeout=60000)
                except Exception as e:
                    if "AlreadyExists" not in _dbus_msg(e):
                        raise
                # trusted even when it was paired already (an untrusted
                # gamepad paired elsewhere was refused each time it reconnected itself)
                self._prop(did, "org.bluez.Device1", "Trusted", GLib.Variant("b", True))
                self._call(did, "org.bluez.Device1", "Connect", timeout=30000)
            except Exception as e:
                msg = _dbus_msg(e)
                if "AlreadyExists" not in msg:
                    self.error("Pairing failed", msg)
            GLib.idle_add(lambda: (self.on_agent and self.on_agent("done", "", "", None), False)[1])
            self.read_bt()
        self.bg(go)

    def cancel_pair(self, did):
        try:
            self._call(did, "org.bluez.Device1", "CancelPairing")
        except Exception:
            pass

    # -- sound (PipeWire via wpctl)
    def read_sound(self):
        rc, o, _ = run("wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@")
        self.sound_ok = rc == 0 and "Volume:" in o
        if self.sound_ok:
            m = re.search(r"Volume:\s*([\d.]+)", o)
            self.vol = int(round(float(m.group(1)) * 10) * 10) if m else self.vol
            self.mute = "MUTED" in o
        m = re.search(r"Volume:\s*([\d.]+)", out("wpctl", "get-volume", "@DEFAULT_AUDIO_SOURCE@"))
        if m:
            self.mic = int(round(float(m.group(1)) * 10) * 10)
        self.outs = parse_sinks(out("wpctl", "status"))
        outs = self.outs
        for o in outs:
            n = o["name"].lower()
            o["sub"] = "Bluetooth" if "bluez" in n or any(d["name"].lower() in n for d in self.devs) else "Built in"
        self.out = next((o["id"] for o in outs if o.get("default")), outs[0]["id"] if outs else None)

    def set_vol(self, v):
        self.vol = v
        self.changed()
        self.bg(lambda: run("wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "%.2f" % (v / 100)))

    def set_mute(self, m):
        self.mute = m
        self.changed()
        self.bg(lambda: run("wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "1" if m else "0"))

    def set_out(self, oid):
        self.out = oid
        self.changed()
        self.bg(lambda: (run("wpctl", "set-default", oid), self.read_sound()))

    def set_mic(self, v):
        self.mic = v
        self.changed()
        self.bg(lambda: run("wpctl", "set-volume", "@DEFAULT_AUDIO_SOURCE@", "%.2f" % (v / 100)))

    # -- light & e-ink
    def set_light(self, light=None, warm=None):
        self.light = self.light if light is None else light
        self.warm = self.warm if warm is None else warm
        br, ct = round(self.light * self.br_max / 100), round(self.warm * self.ct_max / 100)
        light_write(br, ct)
        self._try(lambda: (self.power.set("light_br", br), self.power.set("light_ct", ct)))

    # the managed light: power.conf for glassine-powerd (it follows within 10 s), the
    # light itself at once (the same values powerd computes)
    def _light_now(self):
        b, w = self.cur_light()
        light_write(rnd(b * self.br_max / 100), rnd(w * self.ct_max / 100))

    def set_managed(self, on):
        if on:
            self.managed = True
            self._light_now()
            self._try(lambda: self.power.set("light_managed", 1))
        else:                                   # by hand from what is on now
            b, w = self.cur_light()
            self.managed = False
            self._try(lambda: self.power.set("light_managed", 0))
            self.set_light(round(b / 10) * 10, round(w / 10) * 10)

    def set_light_off(self, k, v):
        self.off[k] = v
        self._light_now()
        self._try(lambda: self.power.set("light_%s_off" % k, v))

    def set_light_lim(self, k, v):
        self.lim[k] = v
        self._light_now()
        self._try(lambda: self.power.set("light_" + k, v))

    def set_prof(self, v):
        self.prof = v
        self.changed()
        self.bg(self._root, "ddr", v)

    def set_mode(self, m):
        self.mode = m
        self._try(lambda: self.eink.set_mode("*", m))

    def set_profile(self, app, m):
        for p in self.profiles:
            if p[0] == app:
                p[1] = m
        self._try(lambda: self.eink.set_mode(app, m))

    def set_idle(self, ms):
        self.idle_ms = ms
        self._try(lambda: self.eink.set("idle_ms", ms))

    def set_dither(self, on):
        self.dither = on
        self._try(lambda: self.eink.set("dither", "on" if on else "off"))

    def set_dpat(self, v):
        """the dither's dots: blue noise, the 8 x 8 Bayer grid or dizzy (error diffusion)"""
        self.dpat = v
        self._try(lambda: self.eink.set("dither_pattern", v))

    def set_dzreuse(self, on):
        """dizzy: a scroll's dots move with the content (glassine-epdd-desktop dizzy_reuse=)"""
        self.dzreuse = on
        self._try(lambda: self.eink.set("dizzy_reuse", "on" if on else "off"))

    def set_grey(self, v):
        """colours to grey: luminance (perceived lightness) or the old luma (glassine-epdd-desktop grey=;
        sway's own grey output follows at the desktop's next start)"""
        self.grey = v
        self._try(lambda: self.eink.set("grey", v))

    def set_hammer(self, on):
        """the rail hammer: arrivals at black / white get 2 frames more (glassine-epdd-desktop hammer_top=)"""
        self.hammer = on
        self._try(lambda: self.eink.set("hammer_top", "2" if on else "0"))

    def set_pointer(self, on):
        """the mouse pointer on the panel (sway; glassine-epdd-desktop pointer=)"""
        self.pointer = on
        self._try(lambda: self.eink.set("pointer", "on" if on else "off"))

    def set_debounce(self, ms):
        """the first still frame waits for quiet (glassine-epdd-desktop debounce_ms=)"""
        self.debounce_ms = ms
        self._try(lambda: self.eink.set("debounce_ms", str(ms)))

    def set_debounce_osk(self, on):
        """the same with the on-screen keyboard up (glassine-epdd-desktop debounce_osk=)"""
        self.debounce_osk = on
        self._try(lambda: self.eink.set("debounce_osk", "1" if on else "0"))

    def set_boost_run(self, on):
        """the touch boost's top CPU clock lasts through a scroll's glide (glassine-epdd-desktop boost_run=)"""
        self.boost_run = on
        self._try(lambda: self.eink.set("boost_run", "on" if on else "off"))

    def set_fluid(self, v):
        """Fluidity: the motion run's paint (FLUIDS); the rest of the range as
        glassine-shell writes it (older eink.confs: a range, the speed curve)"""
        if v not in FLUIDS:
            return
        self.fluid = v

        def w():
            self.eink.set_many((("motion", "adaptive"), ("still", "range"), ("curve", "off"),
                                ("mmin", "gc16"), ("mmax", FLUID_LV[v])))
        self._try(w)

    def set_payback(self, on):
        """Anti-ghosting: glassine-epdd-desktop's payback=on (its defaults) | off (the paint
        alone); the older single knobs go (they would override it)"""
        self.payback = on

        def w():
            for k in PAYBACK_KEYS:
                if k != "payback":
                    self.eink.unset(k)
            self.eink.set("payback", "on" if on else "off")
        self._try(w)

    def set_ahead(self, v):
        """Next frame: when glassine-epdd-desktop lets sway draw the next frame in a scroll —
        predict (timed to land just before the next read), run (at once), off
        (after the read: the app draws while the panel shows the last one)"""
        if v not in AHEAD:
            return
        self.ahead = v
        self._try(lambda: self.eink.set("ahead", v))

    def set_app_fluid(self, aid, v):
        """the app's own Fluidity: eink.conf paint= on its line (None: the global one)"""
        a = self.app(aid)
        if not a or (v is not None and v not in FLUIDS):
            return
        a["fluid"] = v

        def w():
            self.eink.set_kv(a["match"], "paint", fluid_paint(v) if v else None)
            self.eink.set_kv(a["match"], "curve", None)     # (an older line's speed curve)
        self._try(w)

    def set_app_content(self, aid, v):
        """Content (CONTENTS): what counts as motion for the app. moving = eink.conf
        motion=always, pages = pages=on (glassine-epdd-desktop PAGES: a page change is a partial
        refresh, every 6th a flash; the finger alone is no motion), normal = neither.
        One choice because they contradict (motion=always overrides pages=)"""
        a = self.app(aid)
        if not a or v not in CONTENTS:
            return
        a["always"], a["pages"] = v == "moving", v == "pages"

        def w():
            self.eink.set_kv(a["match"], "motion", "always" if v == "moving" else None)
            self.eink.set_kv(a["match"], "pages", "on" if v == "pages" else None)
        self._try(w)

    def set_app_dither(self, aid, v):
        """the app's own dither pattern: eink.conf dither=blue|bayer|dizzy on its line (None: the global one)"""
        a = self.app(aid)
        if not a or (v is not None and v not in APP_DITHERS):
            return
        a["dither"] = v
        self._try(lambda: self.eink.set_kv(a["match"], "dither", v))

    def set_gesture(self, n, d, act):
        if act not in (TAP_ACTIONS if (n, d) == (2, "tap") else GESTURE_ACTIONS):
            return
        self.gestures[(n, d)] = act
        self._try(lambda: write_gestures(self.gestures))

    def set_app_freeze(self, aid, v):
        a = self.app(aid)
        if not a or not a["proc"] or v not in FREEZES:
            return
        a["freeze"] = v
        self.changed()
        self.bg(self._root, "freeze", a["proc"], v)

    def set_app_render(self, aid, v):
        """the app's render profile: glassine-app-render writes the wrapped launchers
        (.desktop, D-Bus service) or the Flatpak override; the next launch takes it"""
        a = self.app(aid)
        if not a or not a.get("desk") or v not in APP_RENDERS:
            return
        a["render"] = v
        self.changed()

        def go():
            rc, _, err = run(APP_RENDER_CMD, aid, v, timeout=30)
            if rc:
                self.error("Could not change the setting", err or "glassine-app-render failed")
            a["render"] = read_app_render().get(aid, "gpu")
            self.changed()
        self.bg(go)

    # -- rendering
    def set_gpu_panel(self, on):
        self.gpu_panel = on
        self.changed()
        self.bg(self._root, "gpu", "panel", "on" if on else "off")

    def open_apps(self):
        """the app ids of this session's windows (the quick-settings switch names them too)"""
        rc, out, _ = run("swaymsg", "-t", "get_tree", timeout=5)
        names = []
        try:
            def walk(n):
                for k in n.get("nodes", []) + n.get("floating_nodes", []):
                    walk(k)
                a = n.get("app_id") or (n.get("window_properties") or {}).get("class")
                if a and n.get("pid") and a not in names:
                    names.append(a)
            walk(json.loads(out)) if rc == 0 else None
        except ValueError:
            pass
        return names

    def set_gpu_desk(self, on):
        self.gpu_desk, self.desk_armed = on, 0
        if on:
            self.gpu_fallback = ""
        self.changed()
        self.bg(self._root, "gpu", "sway", "on" if on else "off")

    # -- power (glassine-powerd re-reads power.conf)
    def set_power(self, key, val):
        setattr(self, key, val)
        k, v = {"pkey": ("power_key", val), "cover": ("cover", "suspend" if val else "ignore"),
                "sleep": ("idle_sleep_min", val), "wifi_off": ("wifi_off_after_min", val)}[key]
        self._try(lambda: self.power.set(k, v))

    # -- keyboard (glassine-wvkbd / glassine-swayd read these)
    def set_osk(self, layout):
        self.osk = layout

        def go():
            os.makedirs(os.path.dirname(OSK_LAYERS_USER), exist_ok=True)
            with open(OSK_LAYERS_USER, "w") as f:
                f.write(OSK_LAYERS[layout] + "\n")
            if not self.kbd:                    # the hardware keyboard follows it
                run(KBD_LAYOUT_CMD)
        self._try(go)

    def set_kbd(self, lay):
        """the hardware keyboards' layout: de | us, "" = as the on-screen keyboard"""
        self.kbd = lay

        def go():
            if lay:
                os.makedirs(os.path.dirname(KBD_LAYOUT), exist_ok=True)
                with open(KBD_LAYOUT, "w") as f:
                    f.write(lay + "\n")
            elif os.path.exists(KBD_LAYOUT):
                os.unlink(KBD_LAYOUT)
            run(KBD_LAYOUT_CMD)
        self._try(go)

    def set_osk_auto(self, on):
        self.osk_auto = on

        def go():
            if on:
                if os.path.exists(OSK_NO_AUTO):
                    os.unlink(OSK_NO_AUTO)
            else:
                os.makedirs(os.path.dirname(OSK_NO_AUTO), exist_ok=True)
                open(OSK_NO_AUTO, "w").close()
        self._try(go)

    def set_own_ws(self, on):
        self.own_ws = on

        def go():
            if on:
                if os.path.exists(APP_SHARED_WS):
                    os.unlink(APP_SHARED_WS)
            else:
                os.makedirs(os.path.dirname(APP_SHARED_WS), exist_ok=True)
                open(APP_SHARED_WS, "w").close()
            if os.path.exists(APP_OWN_WS_OLD):
                os.unlink(APP_OWN_WS_OLD)
        self._try(go)

    # -- appearance
    def set_theme(self, t):
        self.theme = t
        self.changed()
        self.bg(lambda: run("glassine-shell", "theme", t))

    def set_rot_auto(self, on):
        """auto off locks the orientation the screen has now (the design's TG)"""
        super().set_rot_auto(on)
        self.bg(self._root, "rotation", "auto" if on else str(self.rot_lock))

    def set_rot_lock(self, d):
        super().set_rot_lock(d)
        self.bg(self._root, "rotation", str(d))

    # -- date & time
    def zones(self):
        if self._zones is None:
            self._zones = super().zones()
        return self._zones

    def set_zone(self, region, city):
        self.zone = (region, city)
        self.changed()
        def go():
            self._root("timezone", region + "/" + city)
            tz_reload()
            run("glassine-shell", "status")                  # the bar's clock now, not at its next 30 s read
            self.changed()
        self.bg(go)

    def set_h24(self, on):
        self.h24 = on

        def go():
            if on:
                if os.path.exists(CLOCK12):
                    os.unlink(CLOCK12)
            else:
                os.makedirs(os.path.dirname(CLOCK12), exist_ok=True)
                open(CLOCK12, "w").close()
            run("glassine-shell", "status")
        self._try(go)

    def set_ntp(self, on):
        self.ntp = on
        self.changed()
        self.bg(self._root, "ntp", "on" if on else "off")

    def osk_show(self, on):
        if on and not self.osk_auto:                # Keyboard > Show when typing is off
            return
        try:
            subprocess.Popen(["/usr/bin/glassine-osk", "show" if on else "hide"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except OSError:
            pass

    def _owners(self):
        """desktop id -> the apk package that owns its file (the db in one read)"""
        if getattr(self, "_own", None) is None:
            self._own, pkg, indir = {}, None, False
            try:
                with open("/lib/apk/db/installed", errors="replace") as f:
                    for l in f:
                        if l.startswith("P:"):
                            pkg = l[2:].strip()
                        elif l.startswith("F:"):
                            indir = l[2:].strip() == "usr/share/applications"
                        elif indir and l.startswith("R:") and l.rstrip().endswith(".desktop"):
                            self._own[l[2:].strip()[:-8]] = pkg
            except OSError:
                pass
        return self._own

    def _pack_of(self, pkg):
        """the pack that brought pkg (packs the setup recorded, packs.conf's contents)"""
        if getattr(self, "_packs", None) is None:
            self._packs = {}
            chosen = re.split(r"[,\s]+", conf_value(read(SETUP_CONF), "packs"))
            cur = None
            for l in read(PACKS_CONF).splitlines():
                l = l.strip()
                if l.startswith("[") and l.endswith("]"):
                    cur = {"id": l[1:-1], "name": l[1:-1]}
                elif cur and "=" in l:
                    k, _, v = l.partition("=")
                    k = k.strip()
                    if k == "name":
                        cur["name"] = v.strip()
                    elif k == "apk" and cur["id"] in chosen:
                        for n in v.split():
                            self._packs[n] = cur
        p = self._packs.get(pkg)
        return p["name"] if p else None

    def origin(self, a):
        """no owner = our own kit in the overlay: it runs the tablet too"""
        if a["id"] in CORE_IDS:
            return ("core", a["id"], None)
        if a.get("fp"):
            return ("flatpak", a["id"], None)
        if a["id"].startswith("waydroid."):
            return ("android", a["id"][9:], None)
        pkg = self._owners().get(a["id"])
        if not pkg or pkg in CORE_PKGS:
            return ("core", pkg or a["id"], None)
        pack = self._pack_of(pkg)
        return ("package" if pack or pkg in self.added_fn() else "system", pkg, pack)

    def app_removed(self, aid):
        super().app_removed(aid)
        self._desk = self._own = None        # the next read_apps lists what is left

    def _root(self, *args):
        rc, _, err = run("doas", ROOT, *args, timeout=60)
        if rc == AUTH_REFUSED and self.on_auth:
            self.on_auth("To change this setting", lambda: self.bg(self._root, *args))
        elif rc:
            self.error("Could not change the setting", err or "glassine-settings-root failed")
        self.read_local()

    # -- storage (du in a thread: seconds on eMMC)
    def measure(self):
        sp = []
        for name, path in (("Userdata", HOME), ("System", "/")):
            try:
                v = os.statvfs(path)
                total, free = v.f_blocks * v.f_frsize, v.f_bavail * v.f_frsize
                sp.append((name, total - free, total))
            except OSError:
                pass
        self.space = sp
        self.folders = None
        self.changed()
        rc, o, _ = run("du", "-s", "-k", *[os.path.join(HOME, n) for n in os.listdir(HOME)], timeout=120)
        rows = []
        for l in o.splitlines():
            k, _, p = l.partition("\t")
            if k.isdigit():
                rows.append((os.path.basename(p), int(k) * 1024))
        self.folders = sorted(rows, key=lambda r: -r[1])[:5]
        self.space_at = hhmm()

    # -- about
    def about(self):
        up = float(read("/proc/uptime", "0").split()[0] or 0)
        d, h = int(up // 86400), int(up % 86400 // 3600)
        uptime = ("%d day%s, %d h" % (d, "" if d == 1 else "s", h)) if d else "%d h %d min" % (h, int(up % 3600 // 60))
        slot = re.search(r"androidboot\.slot_suffix=_(\w)", read("/proc/cmdline"))
        ip = ""
        if self.conn and self.wifi_dev:
            ip = self.net_info(self.conn)["ip"]
        return [("Model", "E-ink tablet"), ("Hostname", os.uname().nodename),
                ("Kernel", os.uname().release), ("Boot slot", slot.group(1).upper() if slot else "–"),
                ("Image", read("/etc/glassine-ml-id") or "–"), ("Battery health", self.batt.get("health_v", "–")),
                ("IP address", ip or "–"), ("Uptime", uptime)]



def parse_sinks(status):
    """wpctl status -> the Audio/Sinks entries [{id, name, default}]"""
    top = sub = None
    sinks = []
    for l in status.splitlines():
        s = l.strip().lstrip("│├└─ ").rstrip()
        if not s:
            continue
        if not l[:1].isspace() and l[:1] not in ("│", "├", "└"):
            top, sub = s.split()[0], None
            continue
        if s.endswith(":"):
            sub = s[:-1]
            continue
        if top == "Audio" and sub == "Sinks":
            m = re.match(r"(\*)?\s*(\d+)\.\s+(.*?)(\s+\[vol:.*)?$", s)
            if m:
                sinks.append(dict(id=m.group(2), name=m.group(3).strip(), default=bool(m.group(1)), ok=True, sub=""))
    return sinks


def _dbus_msg(e):
    m = str(e)
    return m.split(": ", 2)[-1] if m.startswith("GDBus.Error") else m
