"""What the first-run setup still has to ask: each step is
asked only when what it sets is missing, so a reset brings back exactly the steps
whose answers it removed.

  account   no desktop account (uid 1000..65533) with its home folder. A full reset
            keeps the account: glassine-home restores it from the record in the home
            (accounts-record.sh); a wiped home takes the record with it.
  look      with the account, or with the system items when the desktop user's
            ~/.config/glassine-shell/theme is missing (a home that kept its look
            keeps it)
  system    language, location, keyboard: /etc/glassine/setup.conf is missing
            (a full reset empties /etc; "Reset system" carries it, the migrate list)
  network   no saved connection and setup.conf does not say network=skipped
  software  setup.conf missing, or it names packs none of whose packages are
            installed any more ("Reset system" removes user packages); "None for
            now" records packs= (empty) and is not asked again

  needs()        -> the missing items, in that order ([] = nothing to ask)
  steps(needs)   -> the setup's steps for them (Welcome and Review frame them)
Test overrides (env): GLASSINE_SETUP_ROOT (a directory standing in for /),
GLASSINE_SETUP_PACKS (packs.conf, as glassine-setup-apply).
"""
import os

ROOT = os.environ.get("GLASSINE_SETUP_ROOT", "")
SETUP_CONF = "/etc/glassine/setup.conf"
PACKS_CONF = os.environ.get("GLASSINE_SETUP_PACKS", "/usr/share/glassine/setup/packs.conf")
RUN_NEEDS = "/run/glassine-setup.needs"          # glassine-session writes it for the setup session
ORDER = ["account", "look", "system", "network", "software"]
STEP_OF = {"look": ["look"], "system": ["location", "keyboard"], "network": ["network"], "account": ["account"],
           "software": ["software"]}
ALL_STEPS = ["welcome", "look", "location", "keyboard", "network", "account", "software", "review"]


def p(path):
    return ROOT + path


def read_conf(path=SETUP_CONF):
    out = {}
    try:
        for line in open(p(path), encoding="utf-8"):
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                out[k.strip()] = v.strip()
    except OSError:
        return None
    return out


def desktop_user():
    """(name, home) of the first account with a uid in [1000, 65534), or None"""
    try:
        for line in open(p("/etc/passwd")):
            f = line.rstrip("\n").split(":")
            if len(f) >= 7 and f[2].isdigit() and 1000 <= int(f[2]) < 65534:
                return f[0], f[5]
    except OSError:
        pass
    return None


def _installed():
    names = set()
    try:
        for line in open(p("/lib/apk/db/installed"), encoding="utf-8", errors="replace"):
            if line.startswith("P:"):
                names.add(line[2:].strip())
    except OSError:
        pass
    return names


def _packs():
    """packs.conf -> {id: (apk names, flatpak ids)}"""
    out, cur = {}, None
    try:
        lines = open(PACKS_CONF if "GLASSINE_SETUP_PACKS" in os.environ else p(PACKS_CONF), encoding="utf-8").read().splitlines()
    except OSError:
        return out
    for line in lines:
        line = line.strip()
        if line.startswith("[") and line.endswith("]"):
            cur = out.setdefault(line[1:-1], ([], []))
        elif cur is not None and "=" in line and not line.startswith("#"):
            k, v = (x.strip() for x in line.split("=", 1))
            if k == "apk":
                cur[0][:] = v.split()
            elif k == "flatpak":
                cur[1][:] = v.split()
    return out


def needs():
    conf = read_conf()
    out = []
    u = desktop_user()
    if u is None or not os.path.isdir(p(u[1])):
        out.append("account")
    if "account" in out or (conf is None and not os.path.exists(p(u[1]) + "/.config/glassine-shell/theme")):
        out.append("look")
    if conf is None:
        out.append("system")
    try:
        saved = [f for f in os.listdir(p("/etc/NetworkManager/system-connections")) if not f.startswith(".")]
    except OSError:
        saved = []
    if not saved and (conf or {}).get("network") != "skipped":
        out.append("network")
    if conf is None:
        out.append("software")
    else:
        chosen = (conf.get("packs") or "").split()
        if chosen:
            packs, inst = _packs(), _installed()
            have = any(n in inst for c in chosen for n in packs.get(c, ([], []))[0]) or \
                any(os.path.isdir(p("/var/lib/flatpak/app/" + i)) for c in chosen for i in packs.get(c, ([], []))[1])
            if not have:
                out.append("software")
    return out


def steps(items):
    """the steps to show for these items, Welcome first, Review last"""
    want = {s for i in items for s in STEP_OF.get(i, [])}
    return [s for s in ALL_STEPS if s in ("welcome", "review") or s in want]


def from_run():
    """the items glassine-session found (it runs as root: the saved Wi-Fi is root's to read)"""
    try:
        return [x for x in open(RUN_NEEDS).read().split() if x in ORDER]
    except OSError:
        return None
