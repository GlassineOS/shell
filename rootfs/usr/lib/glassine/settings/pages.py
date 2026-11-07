"""The pages of glassine-settings (the design's renderVals): each builds its title, back/refresh and blocks from the app state
(ui) and the backend (be). Sub-pages open inside the card with a back button to
their topic. Long pages are collapsed/paged by app.fit, not here (a collapsed
section opens as its own sub-page, "eink#1"). A choice row (the design's CH)
opens the "pick" page: its options as radio rows.
"""
from . import blocks as B
from .theme import NAMES
from .backend import KBD_LAYOUTS, app_content
from . import packages

# the tabs on the card's right edge: (page, icon, label)
TABS = [("eink", "contrast", "E-ink"), ("apps", "apps", "Apps"), ("power", "battery_5_bar", "Power"),
        ("appearance", "palette", "Appearance"), ("wifi", "wifi", "Wi-Fi"), ("sound", "volume_up", "Sound"),
        ("light", "light_mode", "Light"), ("bt", "bluetooth", "Bluetooth"), ("keyboard", "keyboard", "Keyboard"),
        ("gestures", "swipe", "Gestures"), ("time", "schedule", "Date & time"), ("storage", "hard_drive", "Storage"), ("about", "info", "About")]
PARENT = {"wifi-net": "wifi", "wifi-new": "wifi", "bt-dev": "bt", "bt-add": "bt", "app": "apps", "origins": "apps", "render-apps": "eink",
          "light-range": "light", "time-zone": "time", "time-city": "time-zone",
          "packages": "apps", "pkg-search": "packages", "pkg": "packages", "pkg-changes": "packages",
          "pkg-updates": "packages", "pkg-output": "packages"}
SHORT = {"apps": "Apps", "gestures": "Gestures", "wifi": "Wi-Fi", "bt": "Bluetooth", "light": "Light", "eink": "E-ink",
         "time": "Date", "time-zone": "Regions"}
REFRESH = ("wifi", "bt-add", "power", "storage", "about")
FREEZES = [("off", "Off"), ("auto", "Auto"), ("always", "Always")]
FREEZE_SUB = {"off": "Keeps running on other screens",
              "auto": "Frozen when idle on another screen, but not while it plays audio",
              "always": "Frozen as soon as its screen is hidden. Thawed when you return"}
SIG = ["", "network_wifi_1_bar", "network_wifi_2_bar", "network_wifi_3_bar", "signal_wifi_4_bar"]
SIG_TXT = ["", "Weak", "Fair", "Good", "Excellent"]
# Dithering: one picker, Off + the three patterns, each a tile with a preview
DPATS = [("off", "Off"), ("bayer", "Bayer"), ("blue", "Blue"), ("dizzy", "Dizzy")]
DPAT_SUB = {"off": "Images show in flat greys, with visible banding",
            "bayer": "Regular grid pattern; steady while things move",
            "blue": "Fine, even grain; best for photos",
            "dizzy": "Diffused grain; sharpest detail, may shimmer in motion"}
GREYS = [("luminance", "Luminance"), ("luma", "Luma")]
GREY_SUB = {"luminance": "Colours keep their lightness: green stays light, blue dark",
            "luma": "The old formula: strong colours turn dark"}
# E-ink > Fluidity (backend.FLUIDS): HOW moving content is painted, one step per frame budget.
# (Apps > Content says WHAT counts as motion; the texts keep the two apart)
FLUID_OPTS = [("fluid", "Fluid"), ("balanced", "Balanced"), ("clear", "Clear"), ("calm", "Calm")]
FLUID_SUB = {"fluid": "How motion is drawn: up to 42 light updates a second. Smoothest scrolling and video",
             "balanced": "How motion is drawn: up to 21 firmer updates a second. A fainter trail behind moving content",
             "clear": "How motion is drawn: up to 10 full updates a second. Crisp, but scrolling moves in steps",
             "calm": "How motion is drawn: in full quality, one refresh at a time. Slow, but never faint"}
# where apps come from (the design's originInfo): label, icon, what it means, where it
# updates, can it be removed
ORIGINS = {"core": ("Core", "lock", "Runs the tablet", "With the system", False),
           "system": ("System", "inventory_2", "Comes with GlassineOS", "With the system", True),
           "package": ("Package", "package_2", "Added by you from Packages or a pack", "Packages › Updates", True),
           "flatpak": ("Flatpak", "storefront", "From Software (Flathub)", "In Software", True),
           "android": ("Android", "android", "Inside Android apps (Waydroid)", "In Android", True)}
# Apps > an app > Content (backend.CONTENTS): WHAT counts as motion for the app
CONTENT_OPTS = [("normal", "Normal"), ("pages", "Pages"), ("moving", "Moving")]
CONTENT_SUB = {"normal": "What counts as motion: scrolling, dragging and fast changes. Everything else gets one full-quality refresh",
               "pages": "For readers. A new page is not motion: one full-quality refresh, a full flash every 6th page. Only real scrolling uses Fluidity",
               "moving": "For games and video. Every change is motion, even slow ones with no finger down. Sharpens once the app pauses"}
CONTENT_NOTE = "Fluidity says how motion is drawn. Content says what counts as motion."
AHEADS = [("predict", "Predict"), ("run", "Early"), ("off", "After read")]
AHEAD_SUB = {"predict": "Sway draws so its frame lands just before the panel needs it",
             "run": "Sway draws the next frame as soon as one arrives",
             "off": "Sway draws after the panel took its frame. Less GPU contention"}
# an app's render profile (glassine-app-render, levers L1, L6)
RENDERS = [("gpu", "GPU"), ("short", "GPU, short jobs"), ("l3", "GPU, chunked"), ("cpu", "CPU")]
RENDER_SUB = {"gpu": "The graphics chip, as the desktop",
              "short": "The graphics chip, never chunked, even when chunking is on for all apps",
              "l3": "The graphics chip in small chunks: the panel keeps up beside a heavy game, the app runs a little slower",
              "cpu": "The processor: about one core while the app draws"}
RENDER_NOTE = "Changes apply the next time the app starts. Apps started from a terminal keep the default."
PROFS = [("saver", "Power saver"), ("balanced", "Balanced"), ("performance", "Performance")]
PROF_SUB = {"saver": "Slower memory, no touch boost. Least drain, heavy apps may stutter",
            "balanced": "Memory follows the processor: fast when used, slow while reading",
            "performance": "Memory always at full speed. Fastest, drains quickest"}
PKEYS = [("suspend", "Sleep", "The page stays on screen, a press wakes it"), ("ignore", "Nothing", "The key is ignored")]
# Gestures: the actions a swipe can run ("#" = a group head on the pick page)
GA = [("nothing", "Nothing", "The swipe is ignored"), ("refresh", "Full refresh", "Flashes the panel to clear ghosting"),
      ("#", "Screens", ""),
      ("screen-next", "Next screen", "Shows the screen to the right"),
      ("screen-prev", "Previous screen", "Shows the screen to the left"),
      ("move-next", "Window to next screen", "Moves the window under your fingers and follows it"),
      ("move-prev", "Window to previous screen", "Moves the window under your fingers and follows it"),
      ("overview", "Overview", "Opens the screen switcher"),
      ("#", "Window", ""),
      ("minimize", "Minimize window", "Hides it until you reopen it. A full-screen window leaves full screen first"),
      ("fullscreen", "Enter full screen", "The window fills its screen; the top bar stays"),
      ("unfullscreen", "Leave full screen", "Brings the other windows of the screen back"),
      ("swap", "Swap tiled windows", "Trades places with its neighbour"),
      ("close", "Close app", "Closes the window under your fingers"),
      ("#", "Keys", ""),
      ("page-up", "Page up", "Scrolls the window under your fingers up a page"),
      ("page-down", "Page down", "Scrolls the window under your fingers down a page"),
      ("#", "Open", ""),
      ("home", "Home", "Shows or hides Home"), ("quick", "Quick settings", "The quick settings popup"),
      ("notices", "Notifications", "Your notices")]
GA_NAME = {a: n for a, n, _ in GA if a != "#"}
# a two-finger tap: a right click first, then what a swipe can do
GT = [("right-click", "Right click", "Opens the menu an app shows on a mouse right click")] + \
     [(a, n, "The tap is ignored" if a == "nothing" else s) for a, n, s in GA]
GA_NAME["right-click"] = "Right click"
GDIRS = [("up", "Up", "swipe_up"), ("down", "Down", "swipe_down"), ("left", "Left", "swipe_left"), ("right", "Right", "swipe_right")]


def top_of(page):
    while page in PARENT:
        page = PARENT[page]
    return page


def city_name(c):
    return c.split("/")[-1].replace("_", " ")


class Page:
    def __init__(self, title, blocks, back=None, refresh=False):
        self.title, self.blocks, self.back, self.refresh = title, blocks, back, refresh


def hm(x):
    """hours (may be <0 or >24) -> HH:MM"""
    m = round(x * 60) % (24 * 60)
    return "%02d:%02d" % (m // 60, m % 60)


def sgn(v):
    return "±0" if v == 0 else "+%d" % v if v > 0 else "−%d" % -v


def phase(be, x):
    """the design's phase line for the hour x"""
    f = be.f_day(x)
    rise, set_ = be.sun
    if f == 0:
        return "Night since %s" % hm(set_ + 0.75) if x > 12 else "Night until %s" % hm(rise - 0.75)
    return "Day" if f == 1 else "Morning, brightening" if x < 12 else "Evening, dimming"


def choice(ui, label, sub, opts, cur, set_fn, icon=None, title=None, key=None):
    """the design's CH: a row with the current choice (wide: on the right; below
    360 px: in the sub line) that opens its options as a page. opts [(value,
    text, sub)], value "#": a group head"""
    v = next((t for x, t, _ in opts if x == cur), str(cur))
    if ui.L.cw < 360:
        it = B.Item(label, (sub + ": " + v) if sub else v, icon=icon, chev=True, sum_txt=v)
    else:
        it = B.Item(label, sub, icon=icon, val=v, chev=True, sum_txt=v)
    it.fn = lambda: ui.open_pick(title or label, opts, cur, set_fn)
    it.pick_key = key
    return it


def rng(lo, hi):
    return [(v, "%d %%" % v) for v in range(0, 101, 10) if lo < v < hi]


def global_dither(be):
    """the E-ink page's picker value: "off" or the pattern"""
    return be.dpat if be.dither else "off"


def set_global_dither(be, v):
    if v == "off":
        be.set_dither(False)
        return
    if v != be.dpat:
        be.set_dpat(v)
    if not be.dither:
        be.set_dither(True)


def idle_txt(ms):
    return "%g s" % (ms / 1000) if ms >= 1000 else "%d ms" % ms


def app_own(ui, a, key, differs):
    """an "Own ..." row is on when the value differs from the default or was just turned on"""
    return differs or (a["id"], key) in ui.own


def app_changes(ui, a):
    """how many of an app's settings are its own"""
    return sum((a["fluid"] is not None, app_content(a) != "normal", bool(a.get("dither")),
                a.get("render", "gpu") != "gpu", a["freeze"] != "auto"))


def app_summary(ui, a):
    """an app row's sub line (the design's ovTxt)"""
    n = app_changes(ui, a)
    return ("1 setting changed" if n == 1 else "%d settings changed" % n) if n else "Follows the defaults"


def build(ui, be):
    """the page for ui.page; a section's sub-page ("eink#1") builds its topic
    (app.fit shows the one section)"""
    p = ui.page.split("#")[0]
    back = PARENT.get(p)
    bl = []
    add = bl.append
    title = ""

    if p in packages.PAGES:                     # Apps > Packages
        title, blabel, back, bl = packages.build(ui, p)
        pg = Page(title, bl, back=back, refresh=False)
        pg.back_label = blabel
        return pg

    if p == "wifi":
        title = "Wi-Fi"
        add(B.Toggle("Wi-Fi", "Switching…" if be.wifi_busy else "Radio on" if be.wifi else "Radio off", be.wifi,
                     lambda: be.set_wifi(not be.wifi)))
        if be.wifi:
            add(B.Head("Networks", ("Scanned at %s, strongest first" % be.scan_at) if be.scan_at else "Scanning…"))
            nets = sorted(be.nets, key=lambda n: (n["ssid"] != be.conn, -n["sig"], n["ssid"].lower()))
            for n in nets:
                c = n["ssid"] == be.conn
                add(B.Item(n["ssid"], "Connected" if c else "Saved" if n["known"] else "Secured" if n["lock"] else "Open",
                           icon=SIG[n["sig"]], lock=n["lock"], bold=c, chev=True, fn=(lambda n=n: ui.open_net(n))))
            if not be.nets and be.scan_at:
                add(B.Note("No networks found. Tap Refresh to scan again."))
        else:
            add(B.Note("Turn Wi-Fi on to scan for networks."))

    elif p == "wifi-net":
        n = ui.net_obj(be)
        title = ui.net or ""
        if n:
            c = n["ssid"] == be.conn
            info = ui.net_info
            add(B.Head("Status"))
            add(B.KV("Connection", "Connected" if c else "Saved, not connected"))
            add(B.KV("Security", n["sec"]))
            add(B.KV("Signal", SIG_TXT[n["sig"]]))
            if c:
                add(B.KV("IP address", info.get("ip") or "–"))
                add(B.KV("MAC address", info.get("mac") or "–"))
            add(B.Toggle("Connect automatically", "When this network is in range", info.get("auto", True),
                         lambda: ui.set_net_auto(not info.get("auto", True))))
            if not c:
                add(B.Button("Connect", "wifi", True, lambda: ui.connect(n["ssid"])))
            add(B.Button("Forget network", "delete", False, lambda: ui.sheet_forget_net()))

    elif p == "wifi-new":
        n = ui.net_obj(be)
        title = ui.net or ""
        add(B.Head("Secured network", "WPA Enterprise" if ui.eap else n["sec"] if n else ""))
        if ui.asked:                                    # from glassine-nm-agent's notice
            add(B.Note("The network refused the saved login, or none is saved. Connect stores the new one."
                       if ui.eap else "The network refused the saved password, or none is saved. Connect stores the new one."))
        hint = "Type here"
        if ui.eap:
            add(B.Field("User name", ui.user, "" if ui.user or ui.fld != "user" else hint,
                        caret=ui.fld == "user", on=lambda: ui.focus("user")))
        add(B.Field("Password", ui.pw if ui.show_pw else "•" * len(ui.pw), "" if ui.pw or ui.fld != "pw" else hint,
                    caret=not ui.eap or ui.fld == "pw", on=(lambda: ui.focus("pw")) if ui.eap else None))
        add(B.Toggle("Show password", "", ui.show_pw, ui.flip_show_pw))
        add(B.Button("Connect", "wifi", True, lambda: ui.connect(ui.net, ui.pw, ui.user if ui.eap else None),
                     disabled=not ui.secret_ok()))

    elif p == "admin-auth":
        a = ui.auth or {}
        title = "Password"
        # the field first: in a small window the keyboard leaves room for little else
        add(B.Field(a.get("why") or "Administrator password", ui.pw if ui.show_pw else "•" * len(ui.pw),
                    "" if ui.pw else "Type here", caret=True))
        add(B.Button("Checking…" if a.get("busy") else "Continue", "key", True, ui.auth_submit, disabled=a.get("busy") or not ui.pw))
        add(B.Note(a.get("err") or "Enter an administrator’s password. It stays valid for 5 minutes."))
        add(B.Toggle("Show password", "", ui.show_pw, ui.flip_show_pw))

    elif p == "bt":
        title = "Bluetooth"
        add(B.Toggle("Bluetooth", "Switching…" if be.bt_busy and not be.bt else 'Visible as “%s”' % ui.hostname if be.bt else "Adapter off",
                     be.bt, lambda: be.set_bt(not be.bt)))
        if be.bt:
            add(B.Head("Paired devices"))
            for d in be.devs:
                add(B.Item(d["name"], "Connected" if d["conn"] else "Not connected", icon=d["icon"], bold=d["conn"],
                           act="Disconnect" if d["conn"] else "Connect", primary=not d["conn"],
                           act_fn=(lambda d=d: be.dev_connect(d["id"], not d["conn"])), chev=True,
                           fn=(lambda d=d: ui.go("bt-dev", dev=d["id"]))))
            if not be.devs:
                add(B.Note("No paired devices."))
            add(B.Button("Add device", "add", False, lambda: ui.go("bt-add")))

    elif p == "bt-dev":
        d = next((x for x in be.devs if x["id"] == ui.dev), None)
        title = d["name"] if d else ""
        if d:
            add(B.Head("Device"))
            add(B.KV("Type", d["type"]))
            add(B.KV("Address", d["addr"]))
            add(B.KV("Status", "Connected" if d["conn"] else "Not connected"))
            if d["conn"] and d.get("battery") is not None:
                add(B.KV("Battery", "%d %%" % d["battery"]))
            add(B.Toggle("Connect automatically", "When the device is turned on", d.get("trusted", True),
                         lambda: be.set_trusted(d["id"], not d.get("trusted", True))))
            add(B.Button("Forget device", "delete", False, lambda: ui.sheet_forget_dev(d)))

    elif p == "bt-add":
        title = "Add device"
        add(B.Head("Nearby", "Scanning…" if be.bt_busy else ("Scanned at %s" % be.bt_scan_at) if be.bt_scan_at else ""))
        for d in be.near:
            add(B.Item(d["name"], d["type"], icon=d["icon"], act="Pair", act_fn=(lambda d=d: ui.pair(d)),
                       fn=(lambda d=d: ui.pair(d))))
        if not be.near and not be.bt_busy:
            add(B.Note("No devices found. Put the device in pairing mode, then tap Refresh."))
        elif be.near:
            add(B.Note("Put the device in pairing mode, then tap Refresh if it is missing."))

    elif p == "sound":
        title = "Sound"
        if not be.sound_ok:
            add(B.Note("No sound output (PipeWire is not running)."))
        add(B.Slider("Volume", be.vol, be.set_vol))
        add(B.Toggle("Mute", "Silences all output", be.mute, lambda: be.set_mute(not be.mute)))
        add(B.Head("Output"))
        for o in be.outs[:4]:
            add(B.Radio(o["name"], o["sub"], o["id"] == be.out, (lambda o=o: be.set_out(o["id"])), disabled=not o.get("ok", True)))
        if not be.outs:
            add(B.Note("No outputs found."))
        add(B.Head("Input"))
        add(B.Slider("Microphone", be.mic, be.set_mic))

    elif p == "light":
        title = "Light"
        nar = ui.L.narrow
        add(B.Toggle("Managed light" if nar else "Managed brightness & warmth",
                     "" if nar else "Follows the time of day" if be.managed else "Set by hand",
                     be.managed, lambda: be.set_managed(not be.managed)))
        if be.managed:
            x = be.now_hour()
            lim = be.lim
            day = [be.sched(i + 0.5) for i in range(24)]
            add(B.Head("Today", phase(be, x), inline=True))    # the curve draws its own title
            add(B.Curve("Today", phase(be, x), [
                ("Brightness", "%d–%d %%" % (lim["b_min"], lim["b_max"]), [d[0] for d in day]),
                ("Warmth", "%d–%d %%" % (lim["w_min"], lim["w_max"]), [d[1] for d in day])], x))
            sb, sw = be.sched(x)
            cb, cw = be.cur_light()
            add(B.Head("Now", "Shifts the schedule up or down"))
            for label, k, sv, cur in (("Brightness", "b", sb, cb), ("Warmth", "w", sw, cw)):
                v = be.off[k]
                add(B.Offset(label, "Schedule %d %%" % sv + (" · " + sgn(v) if v else ""), "%d %%" % cur, v,
                             (lambda v, k=k: be.set_light_off(k, v))))
            add(B.Toggle("Keep a night glow", "At least 5 %" if nar else "Never below 5 %, so the light stays on",
                         be.glow, lambda: be.set_glow(not be.glow)))
            add(B.Head("Schedule", "Follows the sun in " + city_name(be.zone[1]) if be.located else "No location for this time zone"))
            add(B.Item("Range & schedule", "Brightness %d–%d %% · warmth %d–%d %%" % (lim["b_min"], lim["b_max"], lim["w_min"], lim["w_max"]),
                       chev=True, fn=lambda: ui.go("light-range")))
        else:
            add(B.Head("Front light"))
            add(B.Slider("Brightness", be.light, lambda v: be.set_light(light=v)))
            add(B.Slider("Warmth", be.warm, lambda v: be.set_light(warm=v)))

    elif p == "light-range":
        title = "Range & schedule"
        lim = be.lim
        add(B.Head("Brightness"))
        add(B.Stepper("At night", "Lowest, after dark", rng(-1, lim["b_max"]), lim["b_min"], lambda v: be.set_light_lim("b_min", v)))
        add(B.Stepper("By day", "Highest, at midday", rng(lim["b_min"], 101), lim["b_max"], lambda v: be.set_light_lim("b_max", v)))
        add(B.Head("Warmth"))
        add(B.Stepper("By day", "Coolest, at midday", rng(-1, lim["w_max"]), lim["w_min"], lambda v: be.set_light_lim("w_min", v)))
        add(B.Stepper("At night", "Warmest, after dark", rng(lim["w_min"], 101), lim["w_max"], lambda v: be.set_light_lim("w_max", v)))
        zone = "%s / %s" % (be.zone[0], city_name(be.zone[1]))
        rise, set_ = be.sun
        if be.located:
            add(B.Note("Day runs from sunrise (%s) to sunset (%s) in %s, with a 90-minute fade at each end." % (hm(rise), hm(set_), zone)))
        else:
            add(B.Note("The time zone %s has no location, so day runs from %s to %s, with a 90-minute fade at each end."
                       % (zone, hm(rise), hm(set_))))

    elif p == "eink":
        title = "E-ink"
        add(B.Head("Motion", "Scrolling, video, games"))
        add(B.Segments("Fluidity", FLUID_SUB[be.fluid], FLUID_OPTS, be.fluid, be.set_fluid))
        add(B.Head("Dithering", "For images and video"))
        cur = global_dither(be)
        add(B.Dither("Pattern", DPAT_SUB[cur], DPATS, cur, lambda v: set_global_dither(be, v)))
        if cur == "dizzy" or any(a.get("dither") == "dizzy" for a in be.apps):
            add(B.Toggle("Dots follow scrolling", "Dizzy: moved content keeps its dots instead of new ones (experimental)",
                         be.dzreuse, lambda: be.set_dzreuse(not be.dzreuse)))
        add(B.Head("Refresh", summary="After %s idle · Anti-ghosting %s" % (idle_txt(be.idle_ms), "on" if be.payback else "off")))
        add(B.Stepper("Idle before full refresh", "Clears ghosting after typing stops",
                      [(150, "150 ms"), (250, "250 ms"), (400, "400 ms"), (600, "600 ms"), (800, "800 ms"),
                       (1000, "1 s"), (1500, "1.5 s"), (2000, "2 s")], be.idle_ms, be.set_idle))
        add(B.Stepper("Wait for animations to settle", "Paints a short transition once, not its first frame",
                      [(0, "Off"), (40, "40 ms"), (70, "70 ms"), (100, "100 ms"), (150, "150 ms")],
                      be.debounce_ms, be.set_debounce))
        if be.debounce_ms:
            add(B.Toggle("Also with the keyboard up", "Letters you type appear a little later",
                         be.debounce_osk, lambda: be.set_debounce_osk(not be.debounce_osk)))
        add(B.Toggle("Anti-ghosting", "Predicts and cancels the ghosting fast paints leave",
                     be.payback, lambda: be.set_payback(not be.payback)))
        add(B.Toggle("Rail hammer", "Black and white get a little extra drive when they land: less ghosting while scrolling",
                     be.hammer, lambda: be.set_hammer(not be.hammer)))
        add(B.Head("Advanced"))
        add(B.Segments("Colours to grey", GREY_SUB[be.grey], GREYS, be.grey, be.set_grey))
        add(B.Toggle("Mouse pointer", "Shows the pointer of a mouse or touchpad on the panel (the pen and touch hide it)",
                     be.pointer, lambda: be.set_pointer(not be.pointer)))
        add(B.Toggle("Full CPU speed while scrolling", "The CPU stays at its top clock while a scroll glides on: a little smoother (24 instead of 22 fps), more power",
                     be.boost_run, lambda: be.set_boost_run(not be.boost_run)))
        add(B.Segments("Next frame", AHEAD_SUB.get(be.ahead, ""), AHEADS, be.ahead, be.set_ahead))
        add(B.Head("Rendering"))
        add(B.Toggle("Panel on the GPU", "Graphics chip draws the panel; less CPU", be.gpu_panel,
                     lambda: be.set_gpu_panel(not be.gpu_panel)))
        armed = be.desk_is_armed()
        if armed:
            apps = [ui.app_name(a) for a in be.desk_closes]
            sub = "Tap again to restart. " + ("Closes " + ", ".join(apps) if apps else "No apps open")
        elif be.gpu_fallback and be.gpu_desk:
            sub = "On the CPU since a GPU crash at %s. Back on the GPU after a reboot" % be.gpu_fallback
        else:
            sub = "Apps use the graphics chip. Restarts the desktop"
        # armed or not is decided at the tap (a stale page must not switch on one tap)
        add(B.Toggle("Desktop on the GPU", sub, be.gpu_desk,
                     lambda: be.set_gpu_desk(not be.gpu_desk) if be.desk_is_armed() else be.arm_desk(),
                     armed=armed, sub_ink=not armed and bool(be.gpu_fallback) and be.gpu_desk))
        nr = sum(1 for a in be.apps if a.get("render", "gpu") != "gpu")
        add(B.Item("Apps' drawing", ("%d app%s not on the plain GPU" % (nr, "" if nr == 1 else "s")) if nr else "All on the GPU",
                   chev=True, fn=lambda: ui.go("render-apps")))

    elif p == "render-apps":
        title = "Drawing"
        add(B.Note(RENDER_NOTE))
        for a in be.apps:
            if a.get("desk"):
                r = a.get("render", "gpu")
                add(B.Segments(a["name"], RENDER_SUB[r], RENDERS, r, lambda v, a=a: be.set_app_render(a["id"], v)))

    elif p == "origins":
        title = "App sources"
        for label, icon, means, upd, can in ORIGINS.values():
            add(B.Item(label, means + (" · updates " + upd.lower() if can else " · can’t be removed"), icon=icon))

    elif p == "apps":
        title = "Apps"
        shown = [a for a in be.apps if not a.get("removed")]
        custom = [a for a in shown if be.customised(a)]
        rest = [a for a in shown if not be.customised(a)]
        newer = {x[0] for x in ui.pk.d.newer}

        def row(a):
            org = be.origin(a)
            origin_label, ic = ORIGINS[org[0]][:2]
            sub = ("Newer than the system · " if org[1] in newer else "") + app_summary(ui, a)
            if ui.L.cw < 380:                   # no room for the pill: the word leads the line
                sub = origin_label + " · " + sub
            return B.Item(a["name"], sub, icon=a["icon"], chev=True, frame=True, fn=lambda a=a: ui.go("app", app=a["id"]),
                          badge=(origin_label, ic))
        # Packages at the top of Apps, the Apps tab stays lit
        add(B.Item("Packages", "Alpine packages, also software without a store page", icon="deployed_code", chev=True,
                   fn=lambda: ui.go("packages"), sum_txt="Packages"))
        add(B.Item("Where apps come from", " · ".join(o[0] for o in ORIGINS.values()), icon="help", chev=True,
                   fn=lambda: ui.go("origins")))
        if custom:
            add(B.Head("Customised", list_=True))
            bl.extend(row(a) for a in custom)
        if rest:
            add(B.Head("All apps", "%d installed" % len(shown), list_=True))
            bl.extend(row(a) for a in rest)
        if not be.apps:
            add(B.Note("No apps found."))

    elif p == "app":
        a = be.app(ui.app)
        title = a["name"] if a else "Apps"
        if not a:                               # removed, and the list was read again
            add(B.Note("This app has been removed."))
        if a:
            st = packages.status_block(ui.pk.d)
            if st:
                add(st)
            def toggle_own(key, differs, off):
                """an Own row: off puts the default back; on shows the control, the value still the default"""
                def fn():
                    if app_own(ui, a, key, differs):
                        ui.own.discard((a["id"], key))
                        off()
                    else:
                        ui.own.add((a["id"], key))
                    ui.redraw()
                return fn
            own_f, ct = a["fluid"], app_content(a)
            dflt = dict(FLUID_OPTS)[be.fluid]
            gd, dp = global_dither(be), a.get("dither")
            gname = "Off" if gd == "off" else dict(DPATS)[gd]
            add(B.Head("E-ink", as_row="Own fluidity %s · Own dithering %s" % (
                "on · " + dict(FLUID_OPTS)[own_f] if own_f else "off", "on · " + dict(DPATS).get(dp, dp) if dp else "off")))
            # Own fluidity: on starts as a copy of the default
            add(B.Toggle("Own fluidity", "Default is " + dflt if own_f else "Follows the default: " + dflt, bool(own_f),
                         lambda: be.set_app_fluid(a["id"], None if own_f else be.fluid)))
            if own_f:
                add(B.Segments("Fluidity", FLUID_SUB[own_f], FLUID_OPTS, own_f, lambda v: be.set_app_fluid(a["id"], v)))
            # Own dithering: on starts from the global pattern (Blue when that is
            # Off: starting at the default would change nothing)
            add(B.Toggle("Own dithering", "Default is " + gname if dp else "Follows the default: " + gname, bool(dp),
                         lambda: be.set_app_dither(a["id"], None if dp else gd if gd != "off" else "blue")))
            if dp:
                add(B.Dither("Pattern", DPAT_SUB.get(dp, ""), DPATS, dp, lambda v: be.set_app_dither(a["id"], v)))
            # Content: motion=always (a slow scene in a game fell back to
            # full refreshes) or pages=on (KOReader); one choice since
            # they contradict
            add(B.Segments("Content", CONTENT_SUB[ct], CONTENT_OPTS, ct, lambda v: be.set_app_content(a["id"], v)))
            calm = (a["fluid"] or be.fluid) == "calm"
            add(B.Note(CONTENT_NOTE + (" With Calm, motion is drawn in full quality anyway, so Moving changes little." if calm and ct == "moving" else "")))
            if a.get("desk"):
                r = a.get("render", "gpu")
                on = app_own(ui, a, "render", r != "gpu")
                add(B.Head("Rendering"))
                add(B.Toggle("Own rendering", "Default is GPU" if on else "Follows the default: GPU", on,
                             toggle_own("render", r != "gpu", lambda: be.set_app_render(a["id"], "gpu"))))
                if on:
                    add(B.Segments("Draws with", RENDER_SUB[r], RENDERS, r, lambda v: be.set_app_render(a["id"], v)))
                add(B.Note(RENDER_NOTE))
            if a["proc"]:
                on = app_own(ui, a, "freeze", a["freeze"] != "auto")
                add(B.Head("Background"))
                add(B.Toggle("Own freezing", "Default is Auto" if on else "Follows the default: Auto", on,
                             toggle_own("freeze", a["freeze"] != "auto", lambda: be.set_app_freeze(a["id"], "auto"))))
                if on:
                    add(B.Segments("Freeze in background", FREEZE_SUB[a["freeze"]], FREEZES, a["freeze"],
                                   lambda v: be.set_app_freeze(a["id"], v)))
            if a.get("desk"):
                add(B.Head("Home"))
                add(B.Toggle("Favourite", "Listed under Favourite apps on Home", a["fav"],
                             lambda: be.set_fav(a["id"], not a["fav"])))

                def reset():
                    be.reset_app(a["id"])
                    if a["fav"]:
                        be.set_fav(a["id"], False)
                    ui.own -= {k for k in ui.own if k[0] == a["id"]}
                add(B.Item("Reset to defaults", "Clear every setting for this app", act="Reset", act_fn=reset,
                           act_off=not (be.customised(a) or a["fav"])))
                org = be.origin(a)
                origin_label, _ic, means, upd, can = ORIGINS[org[0]]
                gone = a.get("removed")
                add(B.Head("Source", summary="%s · %s%s" % (origin_label, org[1], " · removed" if gone else "")))
                add(B.KV("Comes from", origin_label + " · " + means))
                add(B.KV("App id" if org[0] in ("flatpak", "android") else "Package", org[1]))
                if org[2]:
                    add(B.KV("Pack", org[2]))
                add(B.KV("Updates", upd))
                running = ui.pk.d.run is not None
                if gone:
                    add(B.KV("Status", "Removed"))
                else:
                    add(B.Item("Remove " + a["name"], "Runs the tablet; can’t be removed" if not can else packages.BUSY if running
                               else "Shows what goes with it before anything changes", act="Remove",
                               act_fn=ui.pk.ask("uninstall", a["id"]) if can else None, act_off=not can or running))

    elif p == "power":
        title = "Power & sleep"
        bt = be.batt
        add(B.Head("Battery", ("Read at %s" % be.batt_at) if be.batt_at else ""))
        if bt["pct"] >= 0:
            add(B.Meter("%d %%" % bt["pct"], bt["status"], bt.get("health", ""), bt["pct"]))
        else:
            add(B.Note("No battery found."))
        add(B.Head("Power mode"))
        if be.prof_ok:
            add(B.Segments("Profile", PROF_SUB.get(be.prof, ""), PROFS, be.prof, be.set_prof))
        else:
            add(B.Note("This system image has no memory-speed control, so it always runs at full speed."))
        add(B.Head("Buttons & cover"))
        add(choice(ui, "Power key", "A short press", PKEYS, be.pkey, lambda v: be.set_power("pkey", v), key="pkey"))
        add(B.Toggle("Sleep when the cover closes", "", be.cover, lambda: be.set_power("cover", not be.cover)))
        add(B.Head("Timers"))
        add(B.Stepper("Sleep after", "Without a touch or key press",
                      [(0, "Never"), (1, "1 min"), (2, "2 min"), (5, "5 min"), (10, "10 min"), (15, "15 min"), (30, "30 min"), (60, "1 h")],
                      be.sleep, lambda v: be.set_power("sleep", v)))
        add(B.Stepper("Wi-Fi off after", "While asleep",
                      [(0, "At once"), (5, "5 min"), (15, "15 min"), (30, "30 min"), (60, "1 h"), (-1, "Never")],
                      be.wifi_off, lambda v: be.set_power("wifi_off", v)))

    elif p == "keyboard":
        title = "Keyboard"
        add(B.Head("On-screen keyboard layout"))
        add(B.Radio("German", "QWERTZ", be.osk == "german", lambda: be.set_osk("german")))
        add(B.Radio("US English", "QWERTY", be.osk == "us", lambda: be.set_osk("us")))
        same = "German" if be.osk == "german" else "US English"
        add(B.Head("Hardware keyboard layout"))
        add(B.Radio("As the on-screen keyboard", same, be.kbd == "", lambda: be.set_kbd("")))
        for code, name, kind in KBD_LAYOUTS:
            add(B.Radio(name, kind, be.kbd == code, lambda code=code: be.set_kbd(code)))
        add(B.Head("Behaviour"))
        add(B.Toggle("Show on text focus", "Opens when a text field is tapped" +
                     ", unless a keyboard is connected", be.osk_auto, lambda: be.set_osk_auto(not be.osk_auto)))
        if be.shortcuts:
            add(B.Head("Keyboard shortcuts", "With a connected keyboard"))
            for keys, label in be.shortcuts:
                add(B.KV(label, keys))

    elif p == "appearance":
        title = "Appearance"
        from .theme import THEMES
        cmp = ui.L.cw < 560 or ui.L.avail < 520         # compact: one row per theme
        tiles = [(tid, NAMES[tid], THEMES[tid], tid == be.theme, (lambda tid=tid: be.set_theme(tid)))
                 for tid in ("white", "editorial", "stationery")]
        add(B.Head("Theme", "" if cmp else "The screen refreshes once on change"))
        if cmp:
            bl.extend(B.Themes([x], compact=True) for x in tiles)
        else:
            add(B.Themes(tiles))
        add(B.Note("Applies to the top bar, popups and GTK apps. The screen refreshes once." if cmp else
                   "The theme applies to the top bar, popups and every GTK app."))
        add(B.Head("Rotation", "Auto" if be.rot_auto else "Locked"))
        add(B.Toggle("Auto-rotate", "Follows the sensor. Tap an orientation to lock it" if be.rot_auto else
                     "Locked. The T marks the top of the screen", be.rot_auto, lambda: be.set_rot_auto(not be.rot_auto)))
        add(B.Rotation(be.rot_auto, be.rot_lock, be.rot_now, be.set_rot_lock))
        add(B.Head("Windows"))
        add(B.Toggle("Open apps on their own screen", "An app opened from the drawer gets a new screen" if be.own_ws else
                     "An app opened from the drawer joins the current screen (up to 4 windows)",
                     be.own_ws, lambda: be.set_own_ws(not be.own_ws)))

    elif p == "gestures":
        title = "Gestures"
        add(choice(ui, "Two-finger tap", "Both fingers down and up together", GT, be.gestures.get((2, "tap"), "right-click"),
                   (lambda v: be.set_gesture(2, "tap", v)), icon="mouse",
                   title="Two fingers · tap", key="g2Tap"))
        for n, lbl in ((3, "Three fingers"), (4, "Four fingers")):
            add(B.Head(lbl))
            for d, dn, icon in GDIRS:
                add(choice(ui, "Swipe " + d, "", GA, be.gestures.get((n, d), "nothing"),
                           (lambda v, n=n, d=d: be.set_gesture(n, d, v)), icon=icon,
                           title=("Swipe " + d) if ui.L.cw < 360 else "%s · %s" % (lbl, d), key="g%d%s" % (n, dn)))

    elif p == "pick" and ui.pick:
        title, back = ui.pick["title"], ui.pick["from"]
        for v, label, sub in ui.pick["opts"]:
            if v == "#":
                add(B.Head(label, sub))
            else:
                add(B.Radio(label, sub, v == ui.pick["cur"], (lambda v=v: ui.picked(v))))

    elif p == "time":
        title = "Date & time"
        add(B.Head("Clock"))
        add(B.Item("Time zone", "%s / %s" % (be.zone[0], city_name(be.zone[1])), chev=True,
                   fn=lambda: ui.go("time-zone", region=be.zone[0])))
        add(B.Toggle("24-hour clock", ui.clock(True) if be.h24 else ui.clock(False), be.h24, lambda: be.set_h24(not be.h24)))
        add(B.Toggle("Network time", ("Last synced " + be.synced) if be.ntp and be.synced else "When online" if be.ntp else "Set by hand",
                     be.ntp, lambda: be.set_ntp(not be.ntp)))

    elif p == "time-zone":
        title = "Time zone"
        add(B.Head("Region"))
        zones = be.zones()
        for r in sorted(zones):
            cur = r == be.zone[0]
            add(B.Item(r, ("Current: " + city_name(be.zone[1])) if cur else "%d cities" % len(zones[r]), bold=cur, chev=True,
                       fn=(lambda r=r: ui.go("time-city", region=r))))

    elif p == "time-city":
        title = ui.region or ""
        add(B.Head("City"))
        cities = be.zones().get(ui.region, [])
        for c in cities:
            on = be.zone == (ui.region, c)
            add(B.Radio(city_name(c), c.split("/")[0].replace("_", " ") if "/" in c else "", on,
                        (lambda c=c: ui.set_zone(ui.region, c))))

    elif p == "storage":
        title = "Storage"
        add(B.Head("Space", ("Measured at %s" % be.space_at) if be.space_at else "Measuring…"))
        for name, used, total in be.space:
            pct = round(used * 100 / total) if total else 0
            add(B.Meter(name, "%s used of %s" % (_gb(used), _gb(total)), "%d %%" % pct, pct))
        add(B.Head("Largest folders in /home"))
        if be.folders is None:
            add(B.Note("Measuring the folders…"))
        else:
            for name, size in be.folders:
                add(B.KV(name, _gb(size) if size >= 1e9 else "%d MB" % round(size / 1e6)))

    elif p == "about":
        title = "About"
        add(B.Head("Device"))
        for k, v in be.about():
            add(B.KV(k, v))
        add(B.Note("Updates are installed from the host with glassine-mainline update."))

    return Page(title, bl, back=back, refresh=p in REFRESH)


def _gb(b):
    if b < 1e9:
        return "%d MB" % round(b / 1e6)
    g = "%.1f" % (b / 1e9)
    return (g[:-2] if g.endswith(".0") else g) + " GB"  # "52 GB", "18.2 GB" as in the design
