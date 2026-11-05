"""What the setup asks of the system. Demo: the design's sample data, nothing applied.
Real: reads the system (places from tzdata, networks from NetworkManager, packs from
packs.conf) and applies through glassine-setup-apply."""
import os
import subprocess
import time

from . import data as D

INSTALL_STEPS = ["Language, time zone and keyboard", "Creating your account", "Joining Wi-Fi",
                 "Writing the look and screen settings"]


def _run(*cmd, **kw):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=kw.pop("timeout", 20), **kw)
    except (OSError, subprocess.SubprocessError):
        return None


class Demo:
    """the design's sample data; the Install step runs a timed demo"""

    def __init__(self):
        self._packs = D.DEMO_PACKS
        self.t0 = None

    def packs(self):
        return self._packs

    def init(self, s):
        pass

    def needs(self):
        """the demo asks everything (renders: GLASSINE_SETUP_DEMO_NEEDS=account,look,...)"""
        from . import needs as N
        v = os.environ.get("GLASSINE_SETUP_DEMO_NEEDS")
        return [x for x in v.split(",") if x in N.ORDER] if v is not None else list(N.ORDER)

    def osk(self, on):
        """the on-screen keyboard for a drawn text field"""

    def installed(self, app):
        return len(app.chosen())

    def account_made(self):
        return False

    def places(self, q):
        return D.CITIES

    def nets(self):
        return D.NETS

    def free(self):
        return "18.4 GB"

    def battery(self):
        return "84 %"

    def connect(self, ssid, pw, app):
        app.set(joined=ssid)

    def refresh_screen(self):
        pass

    def finish(self):
        import gi
        gi.require_version("Gtk", "3.0")
        from gi.repository import Gtk
        Gtk.main_quit()

    def steps(self, app):
        return INSTALL_STEPS + ["Installing " + p["name"] for p in app.chosen()] + ["Cleaning up"]

    def preview(self, app):
        """for renders: the design's moment (the fifth step running)"""
        self.t0 = time.monotonic() - 1e9
        self.fixed = app.s.inst_i

    def apply(self, s, packs, redraw):
        from gi.repository import GLib
        self.t0, self.fixed = time.monotonic(), None

        def tick():
            redraw()
            return self.t0 is not None
        GLib.timeout_add(3000, tick)

    def progress(self, app):
        steps = self.steps(app)
        i = self.fixed if getattr(self, "fixed", None) is not None else int((time.monotonic() - (self.t0 or 0)) / 3)
        if i >= len(steps) and self.t0:
            self.t0 = None
            app.go("done")
        return self._progress(steps, min(i, len(steps)), app)

    @staticmethod
    def _progress(steps, i, app, notes=None):
        n = len(steps)
        out = []
        for j, label in enumerate(steps):
            state = "done" if j < i else "now" if j == i else "todo"
            note = (notes or {}).get(j)
            if note is None and len(INSTALL_STEPS) <= j < n - 1:
                p = app.chosen()[j - len(INSTALL_STEPS)]
                note = D.size(p["mb"]) if state != "done" else ""
            out.append((label, state, note or ""))
        # one line, no bar or percent (the design): "(i/n) Item", MB only while a pack
        # installs: its finished packs of the whole download
        now = "(%d/%d) %s" % (i + 1, n, steps[i]) if i < n else "Finishing"
        first = len(INSTALL_STEPS)
        chosen = app.chosen()
        mb = ""
        if chosen and first <= i < first + len(chosen):
            mb = "%d of %d MB" % (sum(p["mb"] for p in chosen[:i - first]), sum(p["mb"] for p in chosen))
        return {"pct": mb, "now": now, "steps": out}


APPLY = "/usr/sbin/glassine-setup-apply"
TAB = "/usr/share/zoneinfo/zone1970.tab"


def _read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return ""


class Real(Demo):
    """the system: zones from tzdata, Wi-Fi through nmcli, packs from packs.conf
    (status=offered only), the apply through doas glassine-setup-apply"""

    def __init__(self):
        import threading
        self.threading = threading
        self._packs = [p for p in (D.read_packs() or []) if p.get("status") == "offered"]
        self._zones = None
        self._nets, self._nets_t = [], 0
        self.lines, self.labels, self.ids, self.finished = [], [], [], False
        self.redraw = lambda: None

    def needs(self):
        """glassine-session's list (it runs as root: the saved Wi-Fi is root's to read), else our own look"""
        from . import needs as N
        got = N.from_run()
        return got if got is not None else N.needs()

    def init(self, s):
        """the State's defaults on a real device: empty account fields, the current
        system's answers (a reset asks again only what it removed)"""
        from . import needs as N
        conf = N.read_conf() or {}
        s.query, s.city = "", 0
        s.name = s.user = s.pw = s.pw2 = ""
        s.host = _read("/etc/hostname") or "glassine"
        s.net, s.joined, s.wpw = None, self._current_ssid(), ""
        rec = [p["id"] for p in self._packs if p.get("rec")]
        s.packs = [p for p in (conf.get("packs") or "").split() if any(q["id"] == p for q in self._packs)] or rec
        s.layout = conf.get("layout") or ("de" if _read("/etc/glassine/osk-layers").startswith("german") else "en-us")
        s.lang = conf.get("language") or s.lang
        s.voice = conf.get("voice") or s.voice
        # what the steps not asked again keep: the zone, NTP, the desktop user's clock + OSK
        self._zone = _read("/etc/timezone") or conf.get("zone") or "UTC"
        s.ntp = not os.path.exists("/etc/glassine/ntp-off")
        u = N.desktop_user()
        if u and os.path.isdir(u[1]):
            s.clock = "12" if os.path.exists(u[1] + "/.config/glassine-shell/clock12") else "24"
            s.osk = not os.path.exists(u[1] + "/.config/glassine/osk-no-auto")
        s.tried = ""

    # -- places
    def zones(self):
        if self._zones is None:
            zs = []
            for line in _read(TAB).splitlines():
                if line and not line.startswith("#"):
                    f = line.split("\t")
                    if len(f) >= 3:
                        zs.append(f[2])
            self._zones = sorted(set(zs))
        return self._zones

    def places(self, q):
        import datetime
        try:
            from zoneinfo import ZoneInfo
        except ImportError:
            ZoneInfo = None
        q = q.strip().lower()
        cur = _read("/etc/timezone") or "UTC"
        if q:
            hits = [z for z in self.zones() if q in z.split("/")[-1].replace("_", " ").lower()]
            hits.sort(key=lambda z: (not z.split("/")[-1].lower().startswith(q), z))
        else:
            hits = [cur]
        out = []
        for z in hits[:8]:
            parts = z.split("/")
            name = parts[-1].replace("_", " ") + ", " + " / ".join(p.replace("_", " ") for p in parts[:-1]) if len(parts) > 1 else z
            t24 = t12 = line = ""
            if ZoneInfo:
                try:
                    now = datetime.datetime.now(ZoneInfo(z))
                    off = now.utcoffset().total_seconds() / 3600
                    line = "%s · %s, UTC%s" % (z, now.tzname(), ("%+g" % off).replace("-", "−") if off else "")
                    t24, t12 = now.strftime("%H:%M"), now.strftime("%-I:%M %p").lower()
                except Exception:   # a broken zone file: show the name only
                    line = z
            out.append((name, z, line or z, t24, t12))
        return out

    # -- Wi-Fi
    def _current_ssid(self):
        r = _run("nmcli", "-t", "-f", "ACTIVE,SSID", "dev", "wifi")
        for line in (r.stdout.splitlines() if r and r.returncode == 0 else []):
            if line.startswith("yes:"):
                return line[4:].replace("\\:", ":") or None
        return None

    def nets(self):
        """the scan (cached; refreshed in the background every 20 s)"""
        if time.monotonic() - self._nets_t > 20:
            self._nets_t = time.monotonic()
            self.threading.Thread(target=self._scan, daemon=True).start()
        return self._nets

    def _scan(self):
        r = _run("nmcli", "-t", "-f", "SSID,SIGNAL,SECURITY", "dev", "wifi", "list", timeout=30)
        nets, seen = [], set()
        for line in (r.stdout.splitlines() if r and r.returncode == 0 else []):
            f = line.replace("\\:", "\0").split(":")
            if len(f) < 3:
                continue
            ssid, sig, sec = (x.replace("\0", ":") for x in f[:3])
            if not ssid or ssid in seen:
                continue
            seen.add(ssid)
            sig = int(sig or 0)
            bars = 4 if sig >= 75 else 3 if sig >= 50 else 2 if sig >= 25 else 1
            word = {4: "strong", 3: "good", 2: "fair", 1: "weak"}[bars]
            enterprise = "802.1X" in sec
            open_ = not sec or sec == "--"
            line = ("Enterprise" if enterprise else "Open" if open_ else sec.split()[-1]) + " · " + word
            nets.append((ssid, bars, line, "badge" if enterprise else "" if open_ else "lock"))
        self._nets = nets[:7]
        self._idle(self.redraw)

    def connect(self, ssid, pw, app):
        app.s.net_err = ""

        def go():
            r = _run("nmcli", "dev", "wifi", "connect", ssid, *(["password", pw] if pw else []), timeout=60)
            if r and r.returncode == 0:
                self._idle(lambda: app.set(joined=ssid, wpw=""))
            else:
                err = ((r.stderr or r.stdout).strip().splitlines() or ["failed"])[-1] if r else "nmcli failed"
                self._idle(lambda: app.set(net_err=err[:80]))
        self.threading.Thread(target=go, daemon=True).start()

    # -- the system
    def free(self):
        try:
            st = os.statvfs("/home")
            return D.size(st.f_bavail * st.f_frsize // 1000000)
        except OSError:
            return "?"

    def battery(self):
        import glob
        for d in glob.glob("/sys/class/power_supply/*"):
            if _read(d + "/type") == "Battery" and _read(d + "/capacity"):
                st = _read(d + "/status")
                return _read(d + "/capacity") + " %" + (" · charging" if st == "Charging" else "")
        return ""

    def osk(self, on):
        if self.s_osk():
            subprocess.Popen(["/usr/bin/glassine-osk", "show" if on else "hide"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def s_osk(self):
        """the Keyboard step's "On-screen keyboard" choice applies at once; a keyboard
        attached: glassine-swayd hides wvkbd itself"""
        return getattr(getattr(self, "app", None), "s", None) is None or self.app.s.osk

    def refresh_screen(self):
        from gi.repository import GLib
        GLib.timeout_add(250, lambda: (_run("glassine-refresh"), False)[1])

    @staticmethod
    def _idle(fn):
        from gi.repository import GLib
        GLib.idle_add(lambda: (fn(), False)[1])

    # -- the apply
    def plan(self, s, app):
        """only what was asked (apply: an absent key keeps what the system has)"""
        st = app.steps
        zone = (self.places(s.query)[s.city:s.city + 1] or [("", "UTC")])[0][1] if "location" in st \
            else getattr(self, "_zone", "UTC")
        plan = {"language": s.lang, "zone": zone,
                "ntp": s.ntp, "clock24": s.clock == "24", "layout": s.layout, "osk": s.osk, "hostname": s.host}
        if "look" in st:
            plan["voice"] = s.voice
        if "account" in st:
            plan["user"] = {"name": s.user, "fullname": s.name, "password": s.pw, "admin": s.admin, "autologin": s.auto}
            plan["admin_password"] = s.apw
        if "software" in st:
            plan["packs"] = list(s.packs)
        if "network" in st and not s.joined:
            plan["network"] = "skipped"
        return plan

    def apply(self, s, packs, redraw):
        import json
        self.redraw, self.lines, self.finished = redraw, [], False
        app = self.app
        data = json.dumps(self.plan(s, app))
        r = _run(APPLY, "--dry-run", input=data)        # the step labels (no root needed)
        self.labels = [ln.split(" ", 3)[3] for ln in (r.stdout.splitlines() if r else []) if ln.startswith("step ")]
        self.ids = [ln.split(" ", 3)[2] for ln in (r.stdout.splitlines() if r else []) if ln.startswith("step ")]
        if not r or r.returncode:
            self.lines = [ln for ln in (r.stdout.splitlines() if r else ["invalid plan apply-missing"])] + ["failed check"]
            return

        def go():
            try:
                p = subprocess.Popen(["doas", "-n", APPLY], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True)
                p.stdin.write(data)
                p.stdin.close()
                for line in p.stdout:
                    self.lines.append(line.rstrip("\n"))
                    self._idle(self.redraw)
                p.wait()
                if not any(ln.startswith(("done", "failed")) for ln in self.lines):
                    self.lines.append("failed exit-%d" % p.returncode)
            except OSError as e:
                self.lines.append("failed " + str(e))
            self._idle(self.redraw)
        self.threading.Thread(target=go, daemon=True).start()

    def progress(self, app):
        st = {}
        cur = -1
        notes = {}
        last = ""
        for ln in self.lines:
            f = ln.split(" ", 2)
            if f[0] == "step":
                cur = int(f[1]) - 1
            elif f[0] in ("ok", "skip") and len(f) > 1 and f[1] in self.ids:
                st[self.ids.index(f[1])] = "done"
                notes[self.ids.index(f[1])] = "skipped" if f[0] == "skip" else ""
            elif f[0] == "fail" and len(f) > 1 and f[1] in self.ids:
                st[self.ids.index(f[1])] = "fail"
                notes[self.ids.index(f[1])] = "failed"
            last = ln
        i = cur if cur >= 0 and st.get(cur) is None else cur + 1
        out = self._progress(self.labels, max(i, 0), app, notes)
        out["steps"] = [(lb, st.get(j, s), n) if st.get(j) != "fail" else (lb, "fail", n)
                        for j, (lb, s, n) in enumerate(out["steps"])]
        if last.startswith("done") and not self.finished:
            self.finished = True
            self._idle(lambda: app.go("done") if app.s.step == "installing" else None)
        elif last.startswith("failed") or last == "failed check":
            out["failed"] = "; ".join(ln.split(" ", 1)[1] for ln in self.lines if ln.startswith(("invalid", "fail "))) or last
        return out

    def account_made(self):
        return "ok account" in self.lines

    def installed(self, app):
        ok = {ln.split(" ")[1] for ln in self.lines if ln.startswith("ok pack-")}
        return len([p for p in app.chosen() if "pack-" + p["id"] in ok])

    def finish(self):
        from gi.repository import Gtk
        Gtk.main_quit()
