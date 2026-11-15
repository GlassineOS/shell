"""Power & sleep > Hibernate after (spec 007): hibernate_after_min read as glassine-powerd
reads it, the steps (a value set by hand as its own step), a write of each choice into
power.conf that keeps the other lines, the row only where hibernation is set up, and the
set-up check as the shell's Power popup makes it (pwr_hib_ok).
Exit 1 on a failed check.
   power.py REPO"""
import os
import sys
import tempfile

sys.path.insert(0, sys.argv[1] + "/rootfs/usr/lib/glassine")
from settings import app as A, backend as B, pages  # noqa: E402

fail = 0


def check(what, got, want):
    global fail
    if got == want:
        print("ok   " + what)
    else:
        print("FAIL %s\n     got:  %r\n     want: %r" % (what, got, want))
        fail = 1


# -- the value as glassine-powerd reads it (atoi of the first word; no key = 4320)
for raw, want in ((None, 4320), ("", 4320), ("  ", 4320), ("60", 60), ("4320", 4320), ("10080", 10080), ("0", 0),
                  ("-5", 0), ("never", 0), ("90", 90), ("90min", 90), (" 120 # two hours", 120), ("+30", 30)):
    check("hibernate_after_min %r reads as %d" % (raw, want), B.hib_minutes(raw), want)

# -- the steps and their labels
std = [v for v, _ in pages.HIB_STEPS]
check("the choices: 1 h, 6 h, 1 day, 3 days, 1 week, Never",
      pages.HIB_STEPS, [(60, "1 h"), (360, "6 h"), (1440, "1 day"), (4320, "3 days"), (10080, "1 week"), (0, "Never")])
for cur in std:
    check("a choice (%d) adds no step" % cur, pages.hib_steps(cur), pages.HIB_STEPS)
for cur, label, at in ((10, "10 min", 0), (90, "90 min", 1), (720, "12 h", 2), (2880, "2 days", 3), (20160, "14 days", 5),
                       (20001, "20001 min", 5)):
    s = pages.hib_steps(cur)
    check("a value set by hand (%d) is its own step %r at %d, Never last" % (cur, label, at),
          (s[at], len(s), s[-1], [v for v, _ in s if v] == sorted(v for v, _ in s if v)),
          ((cur, label), len(std) + 1, (0, "Never"), True))

# -- writing: each choice into power.conf, the other lines kept, read back the same
T = tempfile.mkdtemp(dir=os.environ.get("TMPDIR"))
conf = T + "/power.conf"
other = "# the power settings\npower_key=suspend\nidle_sleep_min=10\nlight_br=12\n"
with open(conf, "w") as f:
    f.write(other)
be = B.Real.__new__(B.Real)          # (only what set_power and read_local's power keys need)
be.power, be.changed = B.Conf(conf), lambda: None
errors = []
be.error = lambda t, x: errors.append((t, x))
check("no key: 3 days", B.hib_minutes(be.power.get("hibernate_after_min")), 4320)
for v in std:
    be.set_power("hib_after", v)
    lines = open(conf).read().splitlines()
    check("write %d: one key, read back the same" % v,
          ([l for l in lines if l.startswith("hibernate_after_min")], B.hib_minutes(be.power.get("hibernate_after_min")), be.hib_after),
          (["hibernate_after_min=%d" % v], v, v))
check("the other lines kept, in order", [l for l in open(conf).read().splitlines() if not l.startswith("hibernate")],
      other.splitlines())
check("no errors writing", errors, [])

# -- the set-up check (pwr_hib_ok in src/glassine-shell.c)
hib = T + "/glassine-hibernate"
with open(hib, "w") as f:
    f.write("#!/bin/sh\n")
mnt = T + "/userdata"
deploy = T + "/deploy.conf"


def setup(conf_text, swap="file", exe=True, mnt_=mnt, pre="media/glassine"):
    os.chmod(hib, 0o755 if exe else 0o644)
    with open(deploy, "w") as f:
        f.write(conf_text)
    d = "%s/%s/swap" % (mnt_, pre)
    os.makedirs(d, exist_ok=True)
    p = d + "/hibernate"
    if os.path.isdir(p):
        os.rmdir(p)
    elif os.path.exists(p):
        os.unlink(p)
    if swap == "file":
        open(p, "w").close()
    elif swap == "dir":
        os.mkdir(p)
    return B.hibernate_ready(hib, deploy)


base = "USERDATA_MNT=%s\n" % mnt
check("set up: helper, size, swap file", setup(base + "HIBERNATE_SIZE_MB=3072\n"), True)
check("no size line counts as set up", setup(base), True)
check("the helper not executable", setup(base, exe=False), False)
check("no helper", B.hibernate_ready(T + "/missing", deploy), False)
check("HIBERNATE_SIZE_MB=0", setup(base + "HIBERNATE_SIZE_MB=0\n"), False)
check("HIBERNATE_SIZE_MB='0'", setup(base + "HIBERNATE_SIZE_MB='0'\n"), False)
check('HIBERNATE_SIZE_MB="0"', setup(base + 'HIBERNATE_SIZE_MB="0"\n'), False)
check("# HIBERNATE_SIZE_MB=0 (a comment)", setup(base + "#HIBERNATE_SIZE_MB=0\n"), True)
check("no swap file", setup(base, swap=None), False)
check("a directory, not a file", setup(base, swap="dir"), False)
check("quoted mount and another prefix", setup("USERDATA_MNT='%s'\nUSERDATA_PREFIX=\"data/os\"\n" % mnt, pre="data/os"), True)
setup(base, swap=None)
check("a swap file only under another prefix than deploy.conf's", setup(base, pre="data/os"), False)
setup(base, swap=None)
real_stat = os.stat


def denied(p, *a, **k):
    if p.endswith("/swap/hibernate"):
        raise PermissionError(13, "Permission denied", p)
    return real_stat(p, *a, **k)


os.stat = denied
check("permission denied counts as there (root's directory)", B.hibernate_ready(hib, deploy), True)
os.stat = real_stat

# -- the page: the row and its note only where hibernation is set up, at the value read
NOTE = "When the device has slept on battery this long, it saves your session and turns off. The power key resumes it."


def power_page(hib_ok, after):
    dm = B.Demo()
    dm.hib_ok, dm.hib_after = hib_ok, after
    ui = A.UI(dm, lambda: None)
    ui.L = A.Layout(702, 884)
    ui.go("power")
    return pages.build(ui, dm).blocks


bl = power_page(True, 4320)
labels = [getattr(b, "label", None) for b in bl]
row = next((b for b in bl if getattr(b, "label", None) == "Hibernate after"), None)
check("set up: the row in Timers, after Wi-Fi off, then the note",
      labels[labels.index("Wi-Fi off after"):labels.index("Wi-Fi off after") + 3], ["Wi-Fi off after", "Hibernate after", NOTE])
check("the row shows 3 days", row and row.opts[row.i], (4320, "3 days"))
row = next(b for b in power_page(True, 90) if getattr(b, "label", None) == "Hibernate after")
check("a value set by hand shows as itself", row.opts[row.i], (90, "90 min"))
row = next(b for b in power_page(True, 0) if getattr(b, "label", None) == "Hibernate after")
check("Never", row.opts[row.i], (0, "Never"))
labels = [getattr(b, "label", None) for b in power_page(False, 4320)]
check("not set up: no row, no note", ("Hibernate after" in labels, NOTE in labels), (False, False))
sys.exit(fail)
