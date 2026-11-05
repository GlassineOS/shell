"""Apps > Packages: search, install and remove Alpine
packages, see what you changed against the system (the base), check for and
install updates, read the full apk output of a failed change.

Pages: packages, pkg-search, pkg (one package), pkg-changes, pkg-updates,
pkg-output. Sheets on the card: the first-change note (once), the plan (what comes
along / what goes, paged 6 at a time, a warning when packages that are part of the
system go too), and the result (done / done with notes / error).

Data: Demo (the design's sample data) or Real:
  - search and package details: the APKINDEX files apk keeps (/var/cache/apk and
    local repositories), parsed in-process; installed: /lib/apk/db/installed;
  - a plan: `apk add -s` / `apk del -s -r` as the user (simulation needs no root);
  - your changes: `glassine-deploy packages --porcelain` (root, via the helper),
    nothing when the system was not booted from a deployment;
  - a change: glassine-settings-root apk-add | apk-del | apk-upgrade | apk-update, which
    runs `glassine apk` (the root is writable for that run only) or plain apk;
    progress = apk's "(i/N) Verb name" lines.
"""
import glob
import io
import os
import re
import socket
import subprocess
import tarfile
import threading
import time

from . import blocks as B
from . import paint as P
from .blocks import Block, _bw
from .theme import OFF

ROOT = "/usr/sbin/glassine-settings-root"
ACK = "/etc/glassine/apk-acknowledged"          # glassine apk's own first-change acknowledgement
CHECKED = os.path.expanduser("~/.cache/glassine/packages-checked")
PAGES = ("packages", "pkg-search", "pkg", "pkg-changes", "pkg-updates", "pkg-output")
PER = 6                                          # plan items per sheet page
LIMIT = 100                                      # search results shown
BUSY = "Another change is running"
MONO = "400 13px/21px 'IBM Plex Mono',monospace"
NAME_OK = re.compile(r"^[A-Za-z0-9][A-Za-z0-9+._-]*$")
STEP = re.compile(r"^\((\d+)/(\d+)\) (Installing|Upgrading|Downgrading|Replacing|Reinstalling|Purging|Removing) (\S+) \((.*)\)")


def mb(b):
    """bytes -> the design's "48 MB" / "0.9 MB" """
    m = b / 1e6
    return ("%.1f MB" % m) if m < 10 else ("%d MB" % round(m))


def pretty(n):
    return {"retroarch": "RetroArch"}.get(n, n)


def when(t):
    """a time -> "today at 22:41" / "on 30 Oct at 22:41" """
    lt = time.localtime(t)
    if time.strftime("%Y%m%d", lt) == time.strftime("%Y%m%d"):
        return time.strftime("today at %H:%M", lt)
    return time.strftime("on %-d %b at %H:%M", lt)


def split_nv(s):
    """"name-1.2.3-r0" -> ("name", "1.2.3-r0")"""
    p = s.rsplit("-", 2)
    return (p[0], p[1] + "-" + p[2]) if len(p) == 3 else (s, "")


def _vkey(v):
    """a rough apk version order (enough to pick the newest of two repositories)"""
    return [(0, int(x)) if x.isdigit() else (1, x) for x in re.findall(r"\d+|[a-z]+", v)]


# -- the plan: what a change does --------------------------------------------------

class Plan:
    def __init__(self, op, name, items, sys=(), dl=0, use=0, err=None, out=""):
        self.op, self.name, self.items, self.sys = op, name, items, list(sys)
        self.dl, self.use, self.err, self.out = dl, use, err, out
        self.order = [n for n, _ in items]
        pr = pretty(name)
        n = len(items)
        up = sum(1 for _, t in items if "→" in t)
        new = n - up
        if op in ("remove", "uninstall"):
            self.title = "Remove %s?" % pr
            self.what = "1 package removed" if n == 1 else "%d packages removed" % n
            self.size = "Frees %s" % mb(use) if use else ""
            self.cta = "Remove" if n == 1 else "Remove %d packages" % n
            self.verb, self.done = "Removing", "Removed %s" % pr + ("" if n == 1 else " (%d packages)" % n)
        elif op == "update":
            self.title = "Update %d packages?" % n if name == "*" else "Update %s?" % pr
            self.what = "%d upgraded" % n
            self.size = "Download %s" % mb(dl) + (" · uses %s more" % mb(use) if use > 0 else "")
            self.cta = "Update all" if name == "*" else "Update"
            self.verb = "Upgrading"
            self.done = "Updated %d packages" % n if name == "*" else "Updated %s" % pr
        else:
            parts = []
            if new:
                parts.append("1 new package" if new == 1 else "%d new packages" % new)
            if up:
                parts.append("%d upgraded" % up)
            self.what = ", ".join(parts) or "Nothing to change"
            self.size = "Download %s · uses %s" % (mb(dl), mb(max(use, 0))) if dl else ""
            self.verb = "Installing"
            if op == "restore":
                self.title, self.cta, self.done = "Restore %s?" % pr, "Restore", "Restored %s" % pr
                self.what = "Puts the system’s package back"
            elif op == "downgrade":
                self.title, self.cta, self.done = "Restore the system’s %s?" % pr, "Restore", "Restored the system’s %s" % pr
                self.what, self.verb = "Replaces your newer version", "Downgrading"
            else:
                self.title, self.cta = "Install %s?" % pr, "Install"
                self.done = "Installed %s" % pr + ("" if n <= 1 else " (%d packages)" % n)
        self.after = ""
        self.warn, self.cmd, self.on_done = None, None, None   # uninstall: (text, names); the command; what follows success


def uninstall_plan(name, org, size="", base=None):
    """Remove on an app's page, by where it comes from (the design's pkPlan). base: the
    plan apk simulated for a package (more than one item keeps apk's words). org =
    backend.origin()"""
    kind, id_, pack = org
    tag = {"system": "part of the system", "flatpak": "flatpak", "android": "android"}.get(kind) or (
        "from the %s pack" % pack if pack else "added by you")
    p = base or Plan("uninstall", name, [(id_, tag)])
    p.title, p.done = "Remove %s?" % name, "Removed %s" % name
    single = len(p.items) == 1
    if single:
        p.items = [(id_, tag)]
        p.cta = "Remove"
    if size:
        p.size = "Frees " + size
    if kind == "system":
        if single:
            p.what = "1 package removed · part of the system"
            p.sys = [id_]
        p.warn = ("%s is part of the system" % name, "Restore it any time under Packages › Your changes.")
        p.after = "%s is gone until you restore it under Packages › Your changes." % name
    elif kind == "flatpak":
        p.what, p.after = "1 Flatpak removed · your documents stay", "Runtimes it shared stay until no other app needs them."
    elif kind == "android":
        p.what, p.sys = "1 Android app removed", [id_]
        p.warn = ("Its data inside Android is deleted", "Files it saved to the tablet’s own storage stay.")
    elif single:
        p.what = "1 package removed · your documents stay"
    return p


def parse_sim(text):
    """apk -s output -> [(name, tag, verb)]"""
    items = []
    for line in text.splitlines():
        m = STEP.match(line.strip())
        if not m:
            continue
        verb, n, ver = m.group(3), m.group(4), m.group(5)
        if verb in ("Upgrading", "Downgrading"):
            tag = ver.replace(" -> ", " → ")
        elif verb in ("Purging", "Removing"):
            tag = "removed"
        else:
            tag = "new"
        items.append((n, tag, verb))
    return items


# -- data -----------------------------------------------------------------------------

class Base:
    # the shell's bar shows a running change (shell.change-running): every assignment
    # of run starts a 1 s poll that tells it the step until run ends
    _run = None
    _told = None

    @property
    def run(self):
        return self._run

    @run.setter
    def run(self, v):
        idle = self._run is None
        self._run = v
        if v is not None and idle:
            from gi.repository import GLib
            GLib.timeout_add(1000, self._tell_shell)
        self._tell_shell()

    def _tell_shell(self):
        r = self._run
        label = "%d/%d" % (r.get("i", 1), r.get("n", 1)) if r and r.get("plan") is not None else None
        if label != self._told:
            self._told = label
            words = ["change", str(os.getpid()), label] if label else ["change"]
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as s:
                    s.sendto("\0".join(words).encode(), os.path.join(os.environ.get("XDG_RUNTIME_DIR", "/run"), "glassine-shell"))
            except OSError:
                pass
        return r is not None

    """what the pages read; on_change redraws"""

    def __init__(self):
        self.on_change = lambda: None
        self.run = None              # a change running: {op, name, plan, i, n, verb, item}
        self.result = None           # its outcome: {kind ok|notes|error, plan, files, restart, msg}
        self.output = None           # the last failed change: {cmd, when, rc, lines}
        self.upd_state = "never"     # never | none | list
        self.updates = []            # [(name, installed, available)]
        self.upd_dl = 0
        self.checked = None          # a time
        self.added, self.removed, self.newer = [], [], []   # newer: [(name, version, base version, change)]
        self.based = True            # booted from a deployment (there is a system to compare with)

    # what the pages ask
    def installed(self, n):
        return n in self.inst

    def info(self, n):
        return self.db.get(n) or {}

    def acked(self):
        return False

    def ack(self):
        pass

    def refresh(self):
        pass

    def ensure(self):
        pass

    def restart_desktop(self):
        pass

    def plan_uninstall(self, a, org, done):
        """the demo: the design's sizes; Real asks apk and flatpak"""
        done(uninstall_plan(a["name"], org, {"system": "74 MB", "flatpak": "18 MB", "android": "62 MB"}.get(org[0], "38 MB")))


class Demo(Base):
    """the design's sample data (EinkSettingsCard pkData), changes run on a 1 s timer"""

    def __init__(self):
        super().__init__()
        P_ = lambda v, d, dl, size, url: {"v": v, "d": d, "dl": dl, "size": size, "url": url}
        self.db = {
            "retroarch": P_("1.21.0-r0", "Frontend for emulators, game engines and media players: one interface for the libretro cores",
                            "12 MB", "38 MB", "retroarch.com"),
            "retroarch-assets": P_("1.21.0-r0", "Menu icons, fonts and wallpapers for RetroArch", "18 MB", "52 MB", "github.com/libretro/retroarch-assets"),
            "libretro-mgba": P_("0.10.3-r0", "Game Boy Advance core", "1.2 MB", "3.4 MB", "mgba.io"),
            "libretro-snes9x": P_("1.62.3-r0", "Super Nintendo core", "1.4 MB", "3.9 MB", "github.com/libretro/snes9x"),
            "libretro-genesis-plus-gx": P_("1.7.4-r0", "Mega Drive, Master System and Game Gear core", "1.1 MB", "3.0 MB",
                                           "github.com/libretro/Genesis-Plus-GX"),
            "libretro-nestopia": P_("1.52.1-r0", "NES core (Nestopia UE)", "0.8 MB", "2.2 MB", "github.com/libretro/nestopia"),
            "libretro-gambatte": P_("0.5.0-r0", "Game Boy and Game Boy Color core", "0.5 MB", "1.4 MB", "github.com/libretro/gambatte-libretro"),
            "libretro-picodrive": P_("2.04-r0", "Master System, Mega Drive and 32X core", "1.0 MB", "2.8 MB", "github.com/libretro/picodrive"),
            "libretro-fceumm": P_("0.0.1-r0", "NES core (FCEUmm)", "0.9 MB", "2.4 MB", "github.com/libretro/libretro-fceumm"),
            "libretro-beetle-pce-fast": P_("0.0.1-r0", "PC Engine core", "0.6 MB", "1.7 MB", "github.com/libretro/beetle-pce-fast-libretro"),
            "libretro-mupen64plus-next": P_("2.6-r0", "Nintendo 64 core", "2.9 MB", "8.4 MB", "github.com/libretro/mupen64plus-libretro-nx"),
            "libretro-dosbox-pure": P_("0.9.9-r0", "DOS core", "1.6 MB", "4.5 MB", "github.com/schellingb/dosbox-pure"),
            "libretro-core-info": P_("1.21.0-r0", "Information files for libretro cores", "0.1 MB", "0.6 MB", "github.com/libretro/libretro-core-info"),
            "htop": P_("3.3.0-r0", "Interactive process viewer", "0.2 MB", "0.5 MB", "htop.dev"),
            "vim": P_("9.1.0707-r0", "Improved vi-style text editor", "1.9 MB", "3.6 MB", "vim.org"),
            "nano": P_("8.2-r0", "Small, friendly text editor", "0.6 MB", "2.4 MB", "nano-editor.org"),
            "sdl2": P_("2.30.9-r0", "Simple DirectMedia Layer: low-level audio, input and graphics", "0.8 MB", "2.3 MB", "libsdl.org"),
            "firefox": P_("142.0.1-r0", "Web browser", "78 MB", "254 MB", "mozilla.org/firefox"),
            "mesa": P_("24.2.8-r0", "Open-source OpenGL and Vulkan drivers", "21 MB", "88 MB", "mesa3d.org"),
        }
        self.retro = ["retroarch", "retroarch-assets", "libretro-mgba", "libretro-snes9x", "libretro-genesis-plus-gx", "libretro-nestopia",
                      "libretro-gambatte", "libretro-picodrive", "libretro-fceumm", "libretro-beetle-pce-fast", "libretro-mupen64plus-next",
                      "libretro-dosbox-pure", "libretro-core-info"]
        libs = ["libcurl", "libxml2", "libpng", "libjpeg-turbo", "libwebp", "libuv", "libffi", "libarchive", "libseccomp", "libinput", "libdrm",
                "libevdev", "libnotify", "libsecret", "libsoup3", "libssh2", "libtasn1", "libunistring", "libusb", "libva", "libvorbis",
                "libwacom", "libxkbcommon", "libzip", "libgcrypt", "libgpg-error", "libidn2", "libmount", "libnftnl", "libpciaccess", "libpsl",
                "librsvg", "libsndfile", "libtirpc"]
        self.lib = []
        for i, b in enumerate(libs):
            for sfx, d in (("", b + ": shared library"), ("-dev", "Development files for " + b), ("-doc", "Documentation for " + b)):
                self.lib.append(b + sfx)
                self.db[b + sfx] = P_("%d.%d.%d-r0" % (1 + i % 3, i * 7 % 20, i % 5), d, "0.3 MB", "0.9 MB", "pkgs.alpinelinux.org")
        self.inst = {"libretro-mgba", "htop", "vim", "sdl2"}
        self.added = ["htop", "vim"]
        self.removed = ["nano"]
        self.newer = [("firefox", "142.0.1", "142.0", "raised"), ("mesa", "24.2.8", "24.2.7", "raised")]
        self.upd_all = [("firefox", "142.0", "142.0.1"), ("mesa", "24.2.7", "24.2.8"), ("labwc", "0.8.1", "0.8.2"),
                        ("syncthing", "1.28.1", "1.28.2"), ("koreader", "2024.11", "2025.04"), ("openssl", "3.3.2", "3.3.3"),
                        ("curl", "8.11.0", "8.11.1"), ("sqlite-libs", "3.47.1", "3.47.2"), ("linux-firmware", "20241110", "20241210")]
        self.checked = time.mktime(time.strptime(time.strftime("%Y-%m-%d") + " 22:41", "%Y-%m-%d %H:%M"))
        self._ack = False
        self._t = None

    def acked(self):
        return self._ack

    def ack(self):
        self._ack = True

    def search(self, q):
        q = q.strip().lower()
        if q in ("lib", "lib-"):
            return 312, self.lib[:LIMIT]
        if re.search(r"retro|libretro", q):
            return len(self.retro), self.retro
        hits = [n for n in self.db if q and (q in n or q in self.db[n]["d"].lower())]
        return len(hits), hits

    def plan(self, op, name, done):
        if op == "install" and name == "retroarch":
            order = ["retroarch-joypad-autoconfig", "libretro-core-info", "retroarch-assets", "ffmpeg-libavcodec", "ffmpeg-libavformat",
                     "ffmpeg-libswscale", "ffmpeg-libswresample", "mbedtls", "miniupnpc", "libudev-zero", "retroarch", "mesa-dri-gallium"]
            p = Plan(op, name, [(n, "24.2.7 → 24.2.8" if n == "mesa-dri-gallium" else "new") for n in order], dl=48e6, use=160e6)
            p.done, p.after = "Installed RetroArch (12 packages)", "RetroArch is in the app drawer under Games."
        elif op == "remove" and name == "sdl2":
            sys_ = ["koreader", "stellarium", "glassine-games"]
            order = sys_ + ["sdl2_ttf", "sdl2_image", "sdl2"]
            p = Plan(op, name, [(n, "part of the system" if n in sys_ else "removed") for n in order], sys_, use=182e6)
            p.after = "KOReader and Stellarium are gone until you restore them under Your changes."
        elif op == "update" and name == "*":
            p = Plan(op, name, [(n, "%s → %s" % (a, b)) for n, a, b in self.updates], dl=186e6, use=4e6)
        else:
            tag = {"install": "new", "remove": "removed", "restore": "system version", "downgrade": "system version", "update": "upgrade"}[op]
            p = Plan(op, name, [(name, tag)], dl=1e6, use=3e6)
        done(p)

    def start(self, plan):
        from gi.repository import GLib
        self.run = {"plan": plan, "i": 1, "n": len(plan.items)}
        self._line()

        def tick():
            r = self.run
            if r is None or r.get("fixed"):
                return False
            if r["i"] >= r["n"]:
                self._apply(plan)
                self.run = None
                notes = plan.op == "update" and plan.name == "*"
                self.result = {"kind": "notes" if notes else "ok", "plan": plan,
                               "files": ["/etc/xdg/labwc/rc.xml.apk-new", "/etc/conf.d/syncthing.apk-new"] if notes else [],
                               "restart": "Restart the desktop to use the new version of labwc." if notes else ""}
            else:
                r["i"] += 1
                self._line()
            self.on_change()
            return self.run is not None
        GLib.timeout_add(1000, tick)

    def _line(self):
        r = self.run
        p = r["plan"]
        n, tag = p.items[min(r["i"], r["n"]) - 1]
        r["verb"] = "Upgrading" if "→" in tag else p.verb
        r["item"] = n
        r["mbtext"] = ("%.1f of %d MB" % (p.dl / 1e6 * r["i"] / r["n"], round(p.dl / 1e6))) if p.dl else p.what

    def _apply(self, p):
        if p.op == "install":
            self.inst.add(p.name)
            if p.name not in self.removed and p.name not in self.added:
                self.added.append(p.name)
            self.removed = [x for x in self.removed if x != p.name]
        elif p.op == "remove":
            gone = set(p.order)
            self.inst -= gone
            if p.name not in self.added:
                self.removed += [x for x in p.order if x not in self.added and x not in self.removed]
            self.added = [x for x in self.added if x not in gone]
        elif p.op == "restore":
            self.removed = [x for x in self.removed if x != p.name]
            self.inst.add(p.name)
        elif p.op == "downgrade":
            self.newer = [x for x in self.newer if x[0] != p.name]
        elif p.op == "update" and p.name == "*":
            self.upd_state, self.updates = "none", []

    def check(self, done=None):
        self.upd_state, self.updates, self.upd_dl = "list", list(self.upd_all), 186e6
        self.on_change()


class Real(Base):
    def __init__(self):
        super().__init__()
        self.db, self.inst, self.world = {}, {}, set()
        self.idx_at = 0
        self.lock = threading.Lock()
        self.proc = None
        self.loaded = False
        try:
            self.checked = os.path.getmtime(CHECKED)
        except OSError:
            self.checked = None

    # the indexes and the installed database
    def _indexes(self):
        files = glob.glob("/var/cache/apk/APKINDEX.*.tar.gz")
        arch = os.uname().machine
        try:
            for line in open("/etc/apk/repositories"):
                line = line.split("#")[0].strip()
                if line.startswith("@"):
                    line = line.split(None, 1)[1] if " " in line else ""
                if line.startswith("/"):
                    f = os.path.join(line, arch, "APKINDEX.tar.gz")
                    if os.path.exists(f):
                        files.append(f)
        except OSError:
            pass
        return files

    def load(self):
        """(re)read the indexes when they changed; the installed database always"""
        files = self._indexes()
        at = max([os.path.getmtime(f) for f in files] or [0])
        if at != self.idx_at:
            db = {}
            for f in files:
                try:
                    with tarfile.open(f, "r:gz") as tf:
                        m = tf.extractfile("APKINDEX")
                        self._parse(io.TextIOWrapper(m, "utf-8", "replace"), db)
                except (OSError, KeyError, tarfile.TarError):
                    continue
            self.db, self.idx_at = db, at
        inst = {}
        try:
            with open("/lib/apk/db/installed", encoding="utf-8", errors="replace") as fh:
                self._parse(fh, inst, installed=True)
        except OSError:
            pass
        self.inst = inst
        try:
            self.world = set(re.split(r"[<>=~]", w)[0] for w in open("/etc/apk/world").read().split())
        except OSError:
            self.world = set()

    @staticmethod
    def _parse(fh, db, installed=False):
        e = {}

        def put():
            n = e.get("P")
            if not n:
                return
            old = db.get(n)
            if installed or old is None or _vkey(e.get("V", "")) > _vkey(old["v"]):
                db[n] = {"v": e.get("V", ""), "d": e.get("T", ""), "dl": int(e.get("S", 0) or 0), "size": int(e.get("I", 0) or 0),
                         "url": re.sub(r"^https?://", "", e.get("U", "")).rstrip("/")}
        for line in fh:
            line = line.rstrip("\n")
            if not line:
                put()
                e = {}
            elif len(line) > 2 and line[1] == ":" and line[0] in "PVTSIU" and line[0] not in e:
                e[line[0]] = line[2:]
        put()

    def installed(self, n):
        return n in self.inst

    def info(self, n):
        d = dict(self.db.get(n) or self.inst.get(n) or {})
        if not d:
            return {}
        if n in self.inst and not d.get("size"):
            d["size"] = self.inst[n]["size"]
        d["dl"] = mb(d["dl"]) if d.get("dl") else ""
        d["size"] = mb(d["size"]) if d.get("size") else ""
        return d

    def search(self, q):
        q = q.strip().lower()
        if not q:
            return 0, []
        hits = []
        for n, d in self.db.items():
            ln = n.lower()
            if q in ln:
                hits.append((0 if ln == q else 1 if ln.startswith(q) else 2, n))
            elif q in d["d"].lower():
                hits.append((3, n))
        hits.sort()
        return len(hits), [n for _, n in hits[:LIMIT]]

    def acked(self):
        return os.path.exists(ACK)

    def ack(self):
        subprocess.run(["doas", ROOT, "apk-ack"], capture_output=True, timeout=10)

    def ensure(self):
        """the first visit reads the indexes and asks for your changes (not at start-up)"""
        if not self.loaded:
            self.loaded = True
            self.refresh()

    def refresh(self):
        self.load()
        self.changes()

    def restart_desktop(self):
        subprocess.Popen(["doas", ROOT, "restart-desktop"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def changes(self):
        """your changes against the base, from glassine-deploy (root reads the base)"""
        def work():
            try:
                r = subprocess.run(["doas", ROOT, "pkg-changes"], capture_output=True, text=True, timeout=60)
            except (OSError, subprocess.TimeoutExpired):
                return
            added, removed, newer, based = [], [], [], r.returncode == 0
            for line in r.stdout.splitlines()[1:]:
                f = dict(x.split("=", 1) for x in line.split("\t") if "=" in x)
                if f.get("type") != "package":
                    continue
                c = f.get("change")
                if c == "added":
                    added.append(f.get("entry", ""))
                elif c == "removed":
                    removed.append(f.get("entry", ""))
                elif c in ("raised", "lowered", "changed"):
                    newer.append((f.get("name", ""), f.get("version", ""), f.get("base_version", ""), c))
            from gi.repository import GLib

            def apply():
                self.added, self.removed, self.newer, self.based = added, removed, newer, based
                self.on_change()
            GLib.idle_add(apply)
        threading.Thread(target=work, daemon=True).start()

    # a plan: apk's simulation as the user
    def plan(self, op, name, done):
        def work():
            if op == "remove":
                cmd = ["apk", "del", "-s", "-r", name]
            elif op == "update":
                cmd = ["apk", "upgrade", "-s"] + ([] if name == "*" else [name])
            elif op == "downgrade":
                bv = next((b for n, _, b, _ in self.newer if n == name), "")
                cmd = ["apk", "add", "-s", "%s=%s" % (name, bv)]
            else:
                cmd = ["apk", "add", "-s", name]
            try:
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
                out, rc = r.stdout + r.stderr, r.returncode
            except (OSError, subprocess.TimeoutExpired) as e:
                out, rc = str(e), 1
            items = parse_sim(out)
            tagged = []
            dl = use = 0
            for n, tag, verb in items:
                d = self.db.get(n) or {}
                old = self.inst.get(n) or {}
                if verb in ("Purging", "Removing"):
                    use += old.get("size", 0)
                    tagged.append((n, tag))
                else:
                    dl += d.get("dl", 0)
                    use += d.get("size", 0) - old.get("size", 0)
                    tagged.append((n, tag))
            sys_ = []
            if op == "remove":
                sys_ = [n for n, _ in tagged if n in self.world and n not in self.added and n != name]
                tagged = [(n, "part of the system" if n in sys_ else t) for n, t in tagged]
            p = Plan(op, name, tagged, sys_, dl=dl, use=use, out=out, err=None if rc == 0 else out.strip())
            if op == "install" and name in self.db:
                p.after = ""
            from gi.repository import GLib
            GLib.idle_add(lambda: done(p))
        threading.Thread(target=work, daemon=True).start()

    def plan_uninstall(self, a, org, done):
        """Remove on an app's page: a package through apk's simulation (what goes with it
        shows), a Flatpak or an Android app through its own tool"""
        kind, id_, _pack = org
        if kind in ("system", "package"):
            self.plan("remove", id_, lambda p: done(uninstall_plan(a["name"], org, base=p)))
            return

        def work():
            size = ""
            if kind == "flatpak":
                try:
                    size = subprocess.run(["flatpak", "info", "--show-size", id_], capture_output=True, text=True, timeout=30).stdout.strip()
                except (OSError, subprocess.TimeoutExpired):
                    pass
            p = uninstall_plan(a["name"], org, size)
            if kind == "android":
                p.cmd = ["waydroid", "app", "remove", id_]
            elif a.get("fp") == "user":
                p.cmd = ["flatpak", "uninstall", "--user", "-y", "--noninteractive", id_]
            else:
                p.cmd = ["doas", ROOT, "uninstall", id_]
            self._idle(lambda: done(p))
        threading.Thread(target=work, daemon=True).start()

    def _start_cmd(self, plan):
        """a Flatpak's or an Android app's removal: same run, same result"""
        self.run = {"plan": plan, "i": 1, "n": 1, "verb": plan.verb, "item": plan.items[0][0], "mbtext": plan.what}

        def work():
            try:
                r = subprocess.run(plan.cmd, capture_output=True, text=True, timeout=300)
                rc, lines = r.returncode, (r.stdout + r.stderr).splitlines()
            except (OSError, subprocess.TimeoutExpired) as e:
                rc, lines = 1, [str(e)]
            if rc == 77 and self.on_refused:     # (AUTH_REFUSED: not an administrator)
                self.run = None
                self._idle(lambda: self.on_refused(plan))
                return
            self._idle(lambda: self._finish(plan, rc, lines, " ".join(plan.cmd)))
        threading.Thread(target=work, daemon=True).start()

    # a change: the root helper, its lines streamed
    def start(self, plan):
        if plan.cmd:
            self._start_cmd(plan)
            return
        op, name = plan.op, plan.name
        if op == "remove":
            args = ["apk-del", name]
        elif op == "update":
            args = ["apk-upgrade"] + ([] if name == "*" else [name])
        elif op == "downgrade":
            bv = next((b for n, _, b, _ in self.newer if n == name), "")
            args = ["apk-add", "%s=%s" % (name, bv)]
        else:
            args = ["apk-add", name]
        self.run = {"plan": plan, "i": 1, "n": max(1, len(plan.items)), "verb": plan.verb, "item": name, "mbtext": plan.what}
        cmdline = "apk " + " ".join({"apk-add": ["add"], "apk-del": ["del"], "apk-upgrade": ["upgrade"]}[args[0]] + args[1:])

        def work():
            lines, last = [], 0
            try:
                pr = subprocess.Popen(["doas", ROOT] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
                self.proc = pr
                for line in pr.stdout:
                    line = line.rstrip("\n")
                    lines.append(line)
                    m = STEP.match(line)
                    if m:
                        i, n = int(m.group(1)), int(m.group(2))
                        self._step(plan, i, n, m.group(3), m.group(4))
                        now = time.monotonic()
                        if now - last >= 1:              # at most one redraw a second (e-ink)
                            last = now
                            self._idle(self.on_change)
                rc = pr.wait()
            except OSError as e:
                lines.append(str(e))
                rc = 1
            self._idle(lambda: self._finish(plan, rc, lines, cmdline))
        threading.Thread(target=work, daemon=True).start()

    @staticmethod
    def _idle(fn):
        from gi.repository import GLib
        GLib.idle_add(lambda: (fn(), False)[1])

    def _step(self, plan, i, n, verb, item):
        if self.run is None:
            return
        done = sum((self.db.get(x) or {}).get("dl", 0) for x in plan.order[:i - 1])
        self.run.update(i=i, n=n, verb=verb, item=item,
                        mbtext=("%.1f of %s" % (done / 1e6, mb(plan.dl))) if plan.dl and plan.op != "remove" else plan.what)

    def _finish(self, plan, rc, lines, cmdline):
        self.run = None
        self.load()
        if rc == 0:
            files = sorted(set(re.findall(r"(/\S+\.apk-new)", "\n".join(lines))))
            desk = [n for n in plan.order if re.match(r"^(sway|sway-glassine|wlroots.*|glassine-epdd-desktop|glassine-epdd)$", n)]
            restart = ("Restart the desktop to use the new version of %s." % desk[0]) if desk else ""
            self.result = {"kind": "notes" if files or restart else "ok", "plan": plan, "files": files, "restart": restart}
            if plan.op == "update" and plan.name == "*":
                self.upd_state, self.updates = "none", []
        else:
            msg = "\n".join(x for x in lines if x.startswith(("ERROR", "  ", "    "))) or "\n".join(lines[-6:])
            self.result = {"kind": "error", "plan": plan, "msg": msg}
        self.output = {"cmd": cmdline, "when": time.time(), "rc": rc, "lines": lines}
        self.changes()
        self.on_change()

    def check(self, done=None):
        """Check for updates: apk update (root), then apk version -l '<' (user)"""
        self.run = {"plan": None, "i": 1, "n": 1, "verb": "Checking", "item": "for updates", "mbtext": "Fetching the package lists"}
        self.on_change()

        def work():
            try:
                r = subprocess.run(["doas", ROOT, "apk-update"], capture_output=True, text=True, timeout=300)
                ok, out = r.returncode == 0, r.stdout + r.stderr
                v = subprocess.run(["apk", "version", "-l", "<"], capture_output=True, text=True, timeout=120).stdout
            except (OSError, subprocess.TimeoutExpired) as e:
                ok, out, v = False, str(e), ""
            ups = []
            for line in v.splitlines()[1:]:
                m = re.match(r"^(\S+)\s+<\s+(\S+)", line)
                if m:
                    n, a = split_nv(m.group(1))
                    ups.append((n, a, m.group(2)))

            def apply():
                self.run = None
                self.load()
                if ok:
                    os.makedirs(os.path.dirname(CHECKED), exist_ok=True)
                    open(CHECKED, "w").close()
                    self.checked = time.time()
                    self.updates = ups
                    self.upd_dl = sum((self.db.get(n) or {}).get("dl", 0) for n, _, _ in ups)
                    self.upd_state = "list" if ups else "none"
                else:
                    self.output = {"cmd": "apk update", "when": time.time(), "rc": 1, "lines": out.splitlines()}
                    self.result = {"kind": "error", "plan": None, "msg": out.strip()[-600:]}
                self.on_change()
            self._idle(apply)
        threading.Thread(target=work, daemon=True).start()


# -- the page's blocks ---------------------------------------------------------------

class Search(Block):
    """the search field (52 high, 1.5px ink, radius KS segR; focused: the caret and a ring)"""
    KIND = "search"

    def __init__(self, val, focus, on, submit=None):
        self.val, self.focus_, self.on, self.submit = val, focus, on, submit
        self.label = "Search"

    def height(self, c, w):
        return 14 + 52 + 14

    def draw(self, c, x, y, w):
        t = c.t
        fy = y + 14
        if self.submit:                                     # the form's button, 52 high
            bw = c.m("Search", t["rowFont"])[0] + 40
            bh = c.m("Search", t["rowFont"])[1]
            P.box(c.cr, x + w - bw, fy, bw, 52, t["trayR"], None, None, "1.5px solid " + t["ink"])
            c.txt("Search", t["rowFont"], x + w - bw + 20, fy + (52 - bh) / 2)
            c.hit(x + w - bw, fy, bw, 52, self.submit)
            w -= bw + 10
        P.box(c.cr, x, fy, w, 52, t["trayR"], t["paper"], "inset 0 0 0 1.5px " + t["ink"] + (
            ",0 0 0 2px %s,0 0 0 4px %s" % (t["paper"], t["ink"]) if self.focus_ else ""))
        c.icon("search", 22, x + 14 + 11, fy + 26)
        tx = x + 14 + 22 + 10
        s = self.val or "Package name or words from its description"
        tw, th = c.m(s, t["rowFont"], maxw=w - (tx - x) - 20)
        c.txt(s, t["rowFont"], tx, fy + (52 - th) / 2, t["ink"] if self.val else t["sub"], maxw=w - (tx - x) - 20)
        if self.focus_:
            cx = tx + (tw + 2 if self.val else 0)
            P.fill(c.cr, cx, fy + 14, 2, 24, t["ink"])
        c.hit(x, fy, w, 52, self.on)


class Pkg(Block):
    """a result row: name, version (mono 12.5), an Installed pill, the description, chevron"""
    KIND = "pkg"

    def __init__(self, name, ver, sub, inst, on):
        self.label, self.ver, self.sub, self.inst, self.on = name, ver, sub, inst, on

    def height(self, c, w):
        t = c.t
        h1 = max(c.m(self.label, t["rowFont"])[1], 24 if self.inst else 0)
        h2 = c.m(self.sub or " ", t["dateFont"])[1]
        return max(48, 12 + h1 + 5 + h2 + 13) + _bw(t, "sep")

    def draw(self, c, x, y, w):
        t = c.t
        h = self.height(c, w)
        ch = h - _bw(t, "sep")
        c.icon("chevron_right", 26, x + w - 13, y + ch / 2)
        tw = w - 26 - 14
        h1 = max(c.m(self.label, t["rowFont"])[1], 24 if self.inst else 0)
        vf = "400 12.5px/1 'IBM Plex Mono',monospace"
        pw = 0
        if self.inst:                                       # the pill: check + INSTALLED, 24 high
            caps = c.lbl_caps()
            lw, lh = c.m("Installed", t["onLblFont"], caps=caps)
            lw += len("Installed") * 0.08 * P.font(t["onLblFont"])[0].get_size() / 1024     # letter-spacing .08em
            pw = 5 + 16 + 3 + lw + 9
            px = x + tw - pw
            P.box(c.cr, px, y + 12 + (h1 - 24) / 2, pw, 24, "999px", None, None, "1.5px solid " + t["ink"])
            c.icon("check", 16, px + 5 + 8, y + 12 + h1 / 2)
            c.txt("Installed", t["onLblFont"], px + 5 + 16 + 3, y + 12 + (h1 - lh) / 2, caps=caps, ls=0.08)
        vw = c.m(self.ver, vf)[0] if self.ver else 0
        nmax = tw - (pw + 10 if pw else 0) - (vw + 10 if vw else 0)
        nw, nh = c.m(self.label, t["rowFont"], maxw=nmax)
        c.txt(self.label, t["rowFont"], x, y + 12 + (h1 - nh) / 2, maxw=nmax)
        if self.ver:
            vh = c.m(self.ver, vf)[1]
            c.txt(self.ver, vf, x + min(nw, nmax) + 10, y + 12 + (h1 - vh) / 2 + 1, t["sub"])
        if self.sub:
            c.txt(self.sub, t["dateFont"], x, y + 12 + h1 + 5, t["sub"], maxw=tw)
        P.rule(c.cr, x, y + h, w, t["sep"])
        c.hit(x, y, w, h, self.on)


class Status(Block):
    """the running change at the top of every package page: (i/N) Verb name + MB"""
    KIND = "status"

    def __init__(self, label, val, icon="pending"):
        self.label, self.val, self.icon = label, val, icon

    def height(self, c, w):
        t = c.t
        return 14 + 12 + max(26, c.m(self.label, t["rowFont"])[1] + 5 + c.m(self.val or " ", "400 13.5px/1.2 'IBM Plex Mono',monospace")[1]) + 12 + 8

    def draw(self, c, x, y, w):
        t = c.t
        h = self.height(c, w) - 14 - 8
        P.box(c.cr, x, y + 14, w, h, t["trayR"], None, None, "2px solid " + t["ink"])
        c.icon(self.icon, 26, x + 16 + 13, y + 14 + h / 2)
        tx = x + 16 + 26 + 14
        lh = c.m(self.label, t["rowFont"])[1]
        vf = "400 13.5px/1.2 'IBM Plex Mono',monospace"
        vh = c.m(self.val or " ", vf)[1]
        ty = y + 14 + (h - lh - 5 - vh) / 2
        c.txt(self.label, t["rowFont"], tx, ty, maxw=w - (tx - x) - 16)
        c.txt(self.val, vf, tx, ty + lh + 5, t["sub"], maxw=w - (tx - x) - 16)


class Para(Block):
    KIND = "para"

    def __init__(self, text):
        self.label = text

    def height(self, c, w):
        return 16 + c.m(self.label or " ", c.t["rowFont"], maxw=w, wrap=True)[1] + 12

    def draw(self, c, x, y, w):
        c.txt(self.label, c.t["rowFont"], x, y + 16, maxw=w, wrap=True)


class Mono(Block):
    """apk's output: mono 13/21 on the sheet ground, pre-wrapped"""
    KIND = "mono"

    def __init__(self, text):
        self.label = text

    def height(self, c, w):
        return 6 + 10 + c.m(self.label or " ", MONO, maxw=w - 28, wrap=True)[1] + 10 + 6

    def draw(self, c, x, y, w):
        t = c.t
        h = self.height(c, w) - 12
        P.box(c.cr, x, y + 6, w, h, t["segR"], t["sheet"])
        c.txt(self.label, MONO, x + 14, y + 16, maxw=w - 28, wrap=True)


# -- the sheets' parts: draw(c, x, y, w) -> height ------------------------------------

class Lines:
    def __init__(self, lines):
        self.lines = lines                               # [(text, sub)]

    def draw(self, c, x, y, w):
        t, yy = c.t, y
        for i, (s, sub) in enumerate(self.lines):
            if i:
                yy += 14
            yy += c.txt(s, t["dateFont"] if sub else t["rowFont"], x, yy, t["sub"] if sub else t["ink"], maxw=w, wrap=True)[1]
        return yy - y


class Warn:
    def __init__(self, title, names):
        self.title, self.names = title, names

    def draw(self, c, x, y, w):
        t = c.t
        tw = w - 32 - 24 - 12
        nf = "400 13.5px/1.4 'IBM Plex Mono',monospace"
        th = c.m(self.title, t["rowFont"], weight=600, maxw=tw, wrap=True)[1]
        nh = c.m(self.names, nf, maxw=tw, wrap=True)[1]
        h = 12 + th + 4 + nh + 12
        P.box(c.cr, x, y, w, h, t["trayR"], None, None, "2px solid " + t["ink"])
        c.icon("warning", 24, x + 16 + 12, y + 12 + 12)
        c.txt(self.title, t["rowFont"], x + 16 + 24 + 12, y + 12, weight=600, maxw=tw, wrap=True)
        c.txt(self.names, nf, x + 16 + 24 + 12, y + 12 + th + 4, maxw=tw, wrap=True)
        return h


class PlanList:
    """the plan's list: a head (+ a round 48 pager when paged), items mono 14.5 with a tag"""

    def __init__(self, head, items, bold, pg, of, go):
        self.head, self.items, self.bold, self.pg, self.of, self.go = head, items, bold, pg, of, go

    def draw(self, c, x, y, w):
        t = c.t
        P.rule(c.cr, x, y, w, t["sepSoft"])
        yy = y + _bw(t, "sepSoft")
        caps = c.lbl_caps()
        hw, lh = c.m(self.head, t["lblFont"], caps=caps)
        hw += len(self.head) * P.ls_em(t["lblLs"]) * P.font(t["lblFont"])[0].get_size() / 1024
        c.txt(self.head, t["lblFont"], x, yy + (48 - lh) / 2, caps=caps, ls=P.ls_em(t["lblLs"]))
        if self.of > 1:
            lbl = "%d/%d" % (self.pg + 1, self.of)
            lw = max(34, c.m(lbl, t["dateFont"])[0])
            if hw + 12 + 48 + 10 + lw + 10 + 48 > w:        # a narrow window: the pager under the head
                yy += 48
            bx = x + w - 48
            for gx, glyph, ok, j in ((bx - 10 - lw - 10 - 48, "chevron_left", self.pg > 0, self.pg - 1),
                                     (bx, "chevron_right", self.pg < self.of - 1, self.pg + 1)):
                P.box(c.cr, gx, yy, 48, 48, "999px", None, None, "1.5px solid " + t["ink"])
                c.icon(glyph, 24, gx + 24, yy + 24, t["ink"] if ok else OFF)
                if ok:
                    c.hit(gx, yy, 48, 48, (lambda j=j: self.go(j)))
            sh = c.m(lbl, t["dateFont"])[1]
            c.txt(lbl, t["dateFont"], bx - 10 - lw / 2, yy + (48 - sh) / 2, align=1)
            if hw + 12 + 48 + 10 + lw + 10 + 48 > w:
                yy += 8
        yy += 48
        nf = "400 14.5px/1.3 'IBM Plex Mono',monospace"
        for n, tag in self.items:
            P.rule(c.cr, x, yy, w, t["sep"])
            yy += _bw(t, "sep")
            tw, th = c.m(tag, t["dateFont"])
            fw = 600 if n in self.bold else 400
            nh = c.m(n, nf, weight=fw)[1]
            rh = max(nh, th)
            c.txt(n, nf, x, yy + 9 + (rh - nh) / 2, weight=fw, maxw=w - tw - 12)
            c.txt(tag, t["dateFont"], x + w, yy + 9 + (rh - th) / 2, t["sub"], align=2)
            yy += 9 + rh + 9
        return yy - y


class Files:
    def __init__(self, head, text, files):
        self.head, self.text, self.files = head, text, files

    def draw(self, c, x, y, w):
        t = c.t
        iw = w - 32
        ff = "400 13.5px/1.5 'IBM Plex Mono',monospace"
        hh = c.m(self.head, t["rowFont"], weight=600)[1]
        th = c.m(self.text, t["dateFont"], maxw=iw, wrap=True)[1]
        fh = [c.m(f, ff, maxw=iw, wrap=True)[1] for f in self.files]
        h = 12 + hh + 6 + th + sum(6 + x_ for x_ in fh) + 12
        P.box(c.cr, x, y, w, h, t["segR"], t["sheet"])
        yy = y + 12
        c.txt(self.head, t["rowFont"], x + 16, yy, weight=600)
        yy += hh + 6
        c.txt(self.text, t["dateFont"], x + 16, yy, t["sub"], maxw=iw, wrap=True)
        yy += th
        for f, fh_ in zip(self.files, fh):
            yy += 6
            c.txt(f, ff, x + 16, yy, maxw=iw, wrap=True)
            yy += fh_
        return h


class Boxed:
    """an outlined line with an icon (restart_alt: Restart the desktop...)"""

    def __init__(self, icon, text):
        self.icon, self.text = icon, text

    def draw(self, c, x, y, w):
        t = c.t
        tw = w - 32 - 24 - 12
        th = c.m(self.text, t["rowFont"], maxw=tw, wrap=True)[1]
        h = 12 + max(24, th) + 12
        P.box(c.cr, x, y, w, h, t["segR"], None, None, "1.5px solid " + t["ink"])
        c.icon(self.icon, 24, x + 16 + 12, y + h / 2)
        c.txt(self.text, t["rowFont"], x + 16 + 24 + 12, y + (h - th) / 2, maxw=tw, wrap=True)
        return h


class MonoBox:
    def __init__(self, text):
        self.text = text

    def draw(self, c, x, y, w):
        t = c.t
        th = c.m(self.text, MONO, maxw=w - 28, wrap=True)[1]
        P.box(c.cr, x, y, w, 12 + th + 12, t["segR"], t["sheet"])
        c.txt(self.text, MONO, x + 14, y + 12, maxw=w - 28, wrap=True)
        return 12 + th + 12


# -- the state the pages keep (on the UI) and the pages ---------------------------------

class State:
    def __init__(self, ui, data):
        self.ui, self.d = ui, data
        self.q, self.kb = "", False
        self.sel, self.frm = None, "pkg-search"
        self.plan_pg = 0
        self.res = None                                    # the last search: (q, total, names)
        self.wait = None                                   # the sheet shown while apk works out a plan
        data.on_change = self.changed
        data.on_refused = lambda p: self.ui.ask_admin("To remove %s" % pretty(p.name), lambda: self.d.start(p))

    def changed(self):
        if self.d.result and not self.d.run:
            self.show_result()
        self.ui.redraw()

    # navigation
    def open(self, n):
        self.sel, self.frm, self.kb = n, self.ui.page, False
        self.ui.go("pkg")

    def to(self, page):
        self.kb = False
        self.ui.go(page)

    def search(self):
        self.kb = False
        self.res = (self.q,) + self.d.search(self.q)
        self.ui.pp["pkg-search"] = 0
        self.ui.go("pkg-search")

    def focus(self):
        self.kb = True
        self.ui.sync_osk()
        self.ui.redraw()

    def key(self, k):
        """the search field's keys (the system's keyboard or a real one)"""
        if k == "back":
            self.q = self.q[:-1]
        elif k == "done":
            self.search()
            return
        else:
            self.q += k
        self.ui.redraw()

    # a change: the note (once), the plan, the run, the result
    def ask(self, op, name):
        if self.d.run:
            return None
        return lambda: self._ask(op, name)

    def _ask(self, op, name):
        from .app import Sheet
        if not self.d.acked():
            sh = Sheet("Before your first change", None, "OK", lambda: (self.d.ack(), self._ask(op, name)), icon="info",
                       parts=[Lines([("Packages you add or remove are kept when the system updates. Manual edits of the system are not.", False),
                                     ("This note appears only once.", True)])])
            self.ui.show(sh)
            return
        self.plan_pg = 0
        self.wait = Sheet("Working out what changes…", None, None, None, icon="pending",
                          parts=[Lines([("apk is reading the package lists.", True)])])
        self.ui.show(self.wait)
        if op == "uninstall":                               # name: the app's id
            a = self.ui.be.app(name)

            def planned(p):
                p.on_done = lambda: self.ui.be.app_removed(name)
                self.show_plan(p)
            self.d.plan_uninstall(a, self.ui.be.origin(a), planned)
            return
        self.d.plan(op, name, self.show_plan)

    def show_plan(self, p):
        from .app import Sheet
        cur = self.ui.sheet
        if cur is None or (cur is not self.wait and getattr(cur, "plan", None) is not p):
            return                                          # cancelled meanwhile
        if p.err or not p.items:
            sh = Sheet("%s can’t be %s" % (pretty(p.name) if p.name != "*" else "The updates",
                                         {"remove": "removed", "uninstall": "removed", "update": "updated"}.get(p.op, "installed")), None, None, None, icon="error",
                       cancel="Close", parts=[Lines([("apk said:" if p.err else "Nothing would change.", False)])] + ([MonoBox(p.err[-700:])] if p.err else []))
            self.ui.show(sh)
            return
        of = max(1, -(-len(p.items) // PER))
        pg = max(0, min(of - 1, self.plan_pg))
        parts = [Lines([(p.what, False)])]
        if p.warn:
            parts.append(Warn(p.warn[0], p.warn[1]))
        elif p.sys:
            parts.append(Warn("Also removes %d package%s that %s part of the system" % (len(p.sys), "" if len(p.sys) == 1 else "s",
                                                                                     "is" if len(p.sys) == 1 else "are"), ", ".join(p.sys)))
        parts.append(PlanList("Removed" if p.op in ("remove", "uninstall") else "What comes along", p.items[pg * PER:pg * PER + PER], set(p.sys), pg, of,
                              lambda j: (setattr(self, "plan_pg", j), self.show_plan(p))))
        sh = Sheet(p.title, None, p.cta, lambda: self.start(p), icon="delete" if p.op in ("remove", "uninstall") else "download", parts=parts, size=p.size)
        sh.plan = p
        self.ui.show(sh)

    def start(self, p):
        self.ui.sheet = None
        self.d.result = None
        # apk asks for the password; a Flatpak's or Android app's removal doesn't
        # (a system Flatpak: admins only, the helper's exit 77 comes back through on_refused)
        if not p.cmd and not self.ui.admin_ok():
            self.ui.ask_admin("To change packages", lambda: self.start(p))
            return
        self.d.start(p)
        self.ui.redraw()

    def show_result(self):
        from .app import Sheet
        r, self.d.result = self.d.result, None
        p = r["plan"]
        if r["kind"] != "error" and p and p.on_done:
            p.on_done()
        if r["kind"] == "error":
            title = ("%s wasn’t %s" % (pretty(p.name) if p.name != "*" else "The updates",
                                       {"remove": "removed", "uninstall": "removed", "update": "updated"}.get(p.op, "installed"))) if p else "Checking for updates failed"
            sh = Sheet(title, None, "Close", None, icon="error", cancel="Details",
                       parts=[Lines([("%s stopped with this message. Nothing was changed." % ("apk" if not (p and p.cmd) else os.path.basename(p.cmd[0]).capitalize()), False)]), MonoBox(r["msg"] or "(no message)")])
            sh.ok = lambda: self.ui.show(None)
            sh.cancel_fn = lambda: (self.ui.show(None), self.ui.go("pkg-output"))
        elif r["kind"] == "notes":
            parts = [Lines([("Done, with %s to know." % ("two things" if r["files"] and r["restart"] else "one thing"), False)])]
            if r["files"]:
                n = len(r["files"])
                parts.append(Files("%d settings file%s kept" % (n, "" if n == 1 else "s"),
                                   "Your version stays; the new default was saved next to it as …apk-new.", r["files"]))
            if r["restart"]:
                parts.append(Boxed("restart_alt", r["restart"]))
                sh = Sheet(p.done, None, "Restart desktop", lambda: (self.ui.show(None), self.d.restart_desktop()), icon="check_circle",
                           cancel="Later", parts=parts)
            else:
                sh = Sheet(p.done, None, "Done", lambda: self.ui.show(None), icon="check_circle", cancel=None, parts=parts)
        else:
            sh = Sheet(p.done, None, "Done", lambda: self.ui.show(None), icon="check_circle", cancel=None,
                       parts=[Lines([(p.after, False)])] if p.after else None)
        self.ui.sheet = sh


TITLES = {"packages": ("Packages", "Apps", "apps"), "pkg-changes": ("Your changes", "Packages", "packages"),
          "pkg-updates": ("Updates", "Packages", "packages")}


def status_block(d):
    """the running change's line, or None (also on an app's page)"""
    r = d.run
    if not r:
        return None
    return Status("(%d/%d) %s %s" % (r["i"], r["n"], r["verb"], r["item"]) if r.get("plan") else "%s %s" % (r["verb"], r["item"]),
                  r.get("mbtext", ""))


def build(ui, p):
    """-> (title, back label, back page, blocks)"""
    st, d = ui.pk, ui.pk.d
    d.ensure()
    running = d.run is not None
    bl = []
    add = bl.append
    sub = lambda s: BUSY if running else s

    def status():
        s = status_block(d)
        if s:
            add(s)

    def btn(label, s, act, fn):
        return B.Item(label, sub(s), act=act, act_fn=fn, act_off=running or fn is None)

    status()
    if p == "packages":
        title, bl_, bp = TITLES[p]
        add(B.Head("Search"))
        add(Search(st.q, st.kb, st.focus, st.search))
        add(B.Note("Alpine packages, including software without a store page, such as RetroArch. Flatpaks are in Software."))
        add(B.Head("Your changes"))
        for label, lst in (("Added by you", d.added), ("Removed by you", d.removed), ("Newer than the system", [n for n, *_ in d.newer])):
            add(B.Item(label, ", ".join(lst) or "None", val=str(len(lst)), chev=True, fn=lambda: st.to("pkg-changes")))
        add(B.Head("Updates"))
        chk = (lambda: (d.check(), st.to("pkg-updates"))) if not running else None
        if d.upd_state == "list":
            add(B.Item("%d updates available" % len(d.updates), "Last checked " + when(d.checked), icon="system_update", chev=True,
                       fn=lambda: st.to("pkg-updates")))
        else:
            add(btn("Check for updates", ("Up to date · last checked " + when(d.checked)) if d.upd_state == "none"
                    else "Never checked. Nothing is fetched until you tap Check.", "Check", chk))
        return title, bl_, bp, bl

    if p == "pkg-search":
        q, total, names = st.res or (st.q, 0, [])
        add(B.Head("%d result%s" % (total, "" if total == 1 else "s"), "Showing %d" % LIMIT if total > LIMIT else "Alpine packages"))
        if total > LIMIT:
            add(B.Note("More than %d results; the first %d are shown. Refine your search to see the rest." % (LIMIT, LIMIT)))
        if not names:
            add(B.Note("No package matches. Try part of the name, or a word from its description."))
        for n in names:
            i = d.info(n)
            add(Pkg(n, i.get("v", ""), i.get("d", ""), d.installed(n), lambda n=n: st.open(n)))
        return "“%s”" % q, "Packages", "packages", bl

    if p == "pkg":
        n = st.sel or ""
        i = d.info(n)
        inst = d.installed(n)
        add(Para(i.get("d", "")))
        add(B.Head("Package"))
        add(B.KV("Version", i.get("v", "") or "–"))
        add(B.KV("Status", ("Installed · added by you" if n in d.added else "Installed") if inst else "Not installed"))
        add(B.KV("Download", i.get("dl", "") or "–"))
        add(B.KV("Installed size", i.get("size", "") or "–"))
        add(B.KV("Homepage", i.get("url", "") or "–"))
        add(btn(("Remove " if inst else "Install ") + pretty(n),
                "Shows what goes with it before anything changes" if inst else "Shows what comes along before anything changes",
                "Remove" if inst else "Install", st.ask("remove" if inst else "install", n)))
        back = st.frm if st.frm in PAGES and st.frm != "pkg" else "pkg-search"
        return n, {"pkg-changes": "Your changes", "pkg-search": "Results", "pkg-updates": "Updates"}.get(back, "Packages"), back, bl

    if p == "pkg-changes":
        title, bl_, bp = TITLES[p]
        add(B.Note("Kept when the system updates. Manual edits of the system are not." if d.based
                   else "This system was not started from a deployment: there is no system to compare with."))
        add(B.Head("Added by you", "%d packages" % len(d.added)))
        for n in d.added:
            i = d.info(n)
            add(btn(n, "%s · %s" % (i.get("v", ""), i.get("d", "")), "Remove", st.ask("remove", n)))
        if not d.added:
            add(B.KV("Nothing yet", ""))
        add(B.Head("Removed by you"))
        for n in d.removed:
            add(btn(n, "Part of the system · removed by you", "Restore", st.ask("restore", n)))
        if not d.removed:
            add(B.KV("Nothing yet", ""))
        add(B.Head("Newer than the system"))
        for n, v, bv, c in d.newer:
            what = {"raised": "%s · the system has %s", "lowered": "%s · older than the system’s %s"}.get(c, "%s · the system has %s")
            add(btn(n, what % (v, bv), "Restore", st.ask("downgrade", n)))
        if not d.newer:
            add(B.KV("Nothing yet", ""))
        return title, bl_, bp, bl

    if p == "pkg-updates":
        title, bl_, bp = TITLES[p]
        chk = (lambda: d.check()) if not running else None
        if d.upd_state == "never":
            add(B.Note("GlassineOS never checks on its own. Checking fetches the package lists, about 2 MB; nothing is installed."))
            add(btn("Check for updates", "Never checked", "Check", chk))
        elif d.upd_state == "none":
            add(B.KV("Status", "Up to date"))
            add(B.KV("Last checked", when(d.checked)[0].upper() + when(d.checked)[1:]))
            add(btn("Check again", "Fetches the package lists, about 2 MB", "Check", chk))
        else:
            add(btn("%d updates" % len(d.updates), "Download %s · last checked %s" % (mb(d.upd_dl), when(d.checked)), "Update all",
                    st.ask("update", "*")))
            add(B.Head("Available"))
            for n, a, b in d.updates:
                add(btn(n, "%s → %s" % (a, b), "Update", st.ask("update", n)))
        return title, bl_, bp, bl

    if p == "pkg-output":
        o = d.output
        if not o:
            add(B.Note("No output yet: a failed change shows its full apk output here."))
            return "apk output", "Packages", "packages", bl
        add(B.Note("%s · %s · exit status %d" % (o["cmd"], when(o["when"]), o["rc"])))
        L = o["lines"]
        for i in range(0, len(L), 6):
            add(Mono("\n".join(L[i:i + 6])))
        return "apk output", st.sel or "Packages", "pkg" if st.sel else "packages", bl
    return "", "Apps", "apps", bl


def preset(ui, name):
    """the design's pkPreset states for the host renders (Demo data): search,
    search-lib, detail, detail-installed, ack, install, install-p2, remove, progress,
    ok, notes, error, output, changes, upd-never, upd-list, upd-none, kb"""
    st, d = ui.pk, ui.pk.d
    retro = lambda: setattr(st, "sel", "retroarch") or setattr(st, "frm", "pkg-search")
    if name in ("search", "search-lib"):
        st.q = "retroarch" if name == "search" else "lib"
        st.search()
    elif name == "kb":
        st.q, st.kb = "retro", True
        ui.go("packages")
    elif name in ("detail", "ack", "install", "install-p2", "error", "ok"):
        retro()
        ui.go("pkg")
        if name == "ack":
            st._ask("install", "retroarch")
        elif name in ("install", "install-p2"):
            d.ack()
            st.plan_pg = 1 if name == "install-p2" else 0
            st._ask("install", "retroarch")
        elif name in ("ok", "error"):
            d.plan("install", "retroarch", lambda p: setattr(d, "result", {"kind": name, "plan": p, "msg": ERR_DEMO}))
            if name == "ok":
                d.inst |= {"retroarch"}
                d.added.append("retroarch")
            st.show_result()
    elif name == "detail-installed":
        st.sel, st.frm = "htop", "pkg-changes"
        ui.go("pkg")
    elif name == "remove":
        d.ack()
        st.sel, st.frm = "sdl2", "pkg-search"
        ui.go("pkg")
        st._ask("remove", "sdl2")
    elif name == "progress":
        st.sel, st.frm = "vim", "pkg-changes"
        ui.go("pkg")
        d.plan("install", "retroarch", lambda p: setattr(d, "run", {"plan": p, "i": 3, "n": 12, "verb": "Installing",
                                                                    "item": p.order[2], "mbtext": "12.4 of 48 MB", "fixed": True}))
    elif name == "notes":
        d.upd_state = "none"
        ui.go("pkg-updates")
        d.updates = list(d.upd_all)
        d.plan("update", "*", lambda p: setattr(d, "result", {"kind": "notes", "plan": p, "files": ["/etc/xdg/labwc/rc.xml.apk-new",
               "/etc/conf.d/syncthing.apk-new"], "restart": "Restart the desktop to use the new version of labwc."}))
        st.show_result()
    elif name.startswith("uninstall-"):
        # the design's Remove on an app's page: uninstall-APP, uninstall-done-APP
        app = name.split("-")[-1]
        d.ack()
        ui.go("app", app=app)
        if "-done-" in name:
            a = ui.be.app(app)
            d.plan_uninstall(a, ui.be.origin(a), lambda p: setattr(d, "result", {"kind": "ok", "plan": p}))
            ui.be.app_removed(app)
            st.show_result()
        else:
            st._ask("uninstall", app)
    elif name == "output":
        st.sel = "retroarch"
        d.output = {"cmd": "apk add retroarch", "when": time.time(), "rc": 1, "lines": ERR_FULL}
        ui.go("pkg-output")
    elif name == "changes":
        ui.go("pkg-changes")
    elif name.startswith("upd-"):
        d.upd_state = name[4:]
        if d.upd_state == "list":
            d.check()
        ui.go("pkg-updates")
    else:
        ui.go("packages")


ERR_DEMO = ("ERROR: unable to select packages:\n  retroarch-1.21.0-r0:\n    conflicts: retroarch-git-1.21.0_git20261028-r0\n"
            "      [cmd:retroarch=1.21.0-r0]\n    satisfies: world[retroarch]")
ERR_FULL = (["fetch https://dl-cdn.alpinelinux.org/alpine/v3.21/main/aarch64/APKINDEX.tar.gz",
             "fetch https://dl-cdn.alpinelinux.org/alpine/v3.21/community/aarch64/APKINDEX.tar.gz",
             "fetch https://repo.glassine.org/v3.21/aarch64/APKINDEX.tar.gz"] + ERR_DEMO.split("\n") +
            ["  retroarch-git-1.21.0_git20261028-r0:", "    conflicts: retroarch-1.21.0-r0", "      [cmd:retroarch=1.21.0_git20261028-r0]",
             "    satisfies: world[retroarch-git]", "", "apk add retroarch: exit status 1", "Nothing was changed."])
