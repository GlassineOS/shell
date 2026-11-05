"""What the setup pages offer: languages, keyboard layouts and their key maps, the
design's sample places and networks (the demo), and the software packs (read from
packs.conf when there, else the design's ten as a demo)."""
import os

LANGS = [("en-GB", "English (UK)", "English"), ("en-US", "English (US)", "English"), ("de", "Deutsch", "German"),
         ("fr", "Français", "French"), ("es", "Español", "Spanish"), ("nb", "Norsk bokmål", "Norwegian"),
         ("nl", "Nederlands", "Dutch"), ("pl", "Polski", "Polish")]

# (id, name, xkb "layout [variant]")
LAYOUTS = [("en-gb", "English (UK)", "gb"), ("en-us", "English (US)", "us"), ("de", "German", "de"),
           ("de-nd", "German, no dead keys", "de nodeadkeys"), ("fr", "French (AZERTY)", "fr"), ("no", "Norwegian", "no")]
ROWS = {"en-gb": ["qwertyuiop[", "asdfghjkl;'", "zxcvbnm,./"], "en-us": ["qwertyuiop[", "asdfghjkl;'", "zxcvbnm,./"],
        "de": ["qwertzuiopü", "asdfghjklöä", "yxcvbnm,.-"], "de-nd": ["qwertzuiopü", "asdfghjklöä", "yxcvbnm,.-"],
        "fr": ["azertyuiop^", "qsdfghjklmù", "wxcvbn,;:!"], "no": ["qwertyuiopå", "asdfghjkløæ", "zxcvbnm,.-"]}
TEST = {"en-gb": "Greetings from Dresden", "en-us": "Greetings from Dresden", "de": "Grüße aus Dresden",
        "de-nd": "Grüße aus Dresden", "fr": "Bonjour de Dresde", "no": "Hilsen fra Dresden"}

# the demo's places: (name, zone, zone line, time 24 h, time 12 h)
CITIES = [("Dresden, Germany", "Europe/Berlin", "Europe/Berlin · CET, UTC+1", "14:51", "2:51 pm"),
          ("Dresden, Ohio, USA", "America/New_York", "America/New_York · EDT, UTC−4", "09:51", "9:51 am"),
          ("Dresden, Ontario, Canada", "America/Toronto", "America/Toronto · EDT, UTC−4", "09:51", "9:51 am")]

# the demo's networks: (ssid, signal 1-4, security line, lock icon)
NETS = [("Fernweh", 4, "WPA2 · strong", "lock"), ("FRITZ!Box 7590 KQ", 3, "WPA2 · good", "lock"),
        ("Vodafone-7C21", 2, "WPA3 · fair", "lock"), ("Telekom_FON", 2, "Open · sign-in page", ""),
        ("eduroam", 1, "Enterprise · weak", "badge")]

DEMO_PACKS = [
    {"id": "reading", "icon": "menu_book", "name": "Reading", "desc": "Books, PDFs and comics, tuned for e-ink",
     "apps": "KOReader · Foliate · Calibre", "mb": 420, "rec": True},
    {"id": "notes", "icon": "stylus_note", "name": "Note-taking", "desc": "Handwriting with pen pressure, and typed notes",
     "apps": "Xournal++ · Rnote · Joplin", "mb": 340, "rec": True},
    {"id": "office", "icon": "description", "name": "Office", "desc": "Documents, spreadsheets and slides",
     "apps": "LibreOffice · Gnumeric", "mb": 1150},
    {"id": "writing", "icon": "edit_document", "name": "Writing", "desc": "Distraction-free Markdown with grammar checks",
     "apps": "Apostrophe · LanguageTool · Pandoc", "mb": 260},
    {"id": "containers", "icon": "deployed_code", "name": "Containers", "desc": "Run containers with Podman, under your own account",
     "apps": "Podman · Compose", "mb": 60},
    {"id": "dev", "icon": "code", "name": "Development", "desc": "Editors, compilers and version control",
     "apps": "Git · VSCodium · Python 3 · build tools", "mb": 1400},
    {"id": "science", "icon": "nights_stay", "name": "Science & education", "desc": "Sky charts, maps, flash cards, GIS",
     "apps": "Stellarium · Marble · WordQuiz · SAGA", "mb": 780},
    {"id": "sync", "icon": "sync", "name": "Sync & backup", "desc": "Keep folders in step with your other devices",
     "apps": "Syncthing · Déjà Dup · Nextcloud", "mb": 180},
    {"id": "android", "icon": "android", "name": "Android apps", "desc": "Run Android apps in a window (Waydroid)",
     "apps": "Waydroid · F-Droid", "mb": 1900},
    {"id": "games", "icon": "chess", "name": "Games", "desc": "Slow games that suit a slow screen",
     "apps": "Chess · Sudoku · Solitaire · Crosswords", "mb": 150},
]

PACKS_CONF = "/usr/share/glassine/setup/packs.conf"


def read_packs(path=PACKS_CONF):
    """packs.conf: one [id] section per pack with name, icon, desc, apps, mb, rec,
    apk (Alpine packages), flatpak (Flathub app ids), status (offered|demo).
    Only status=offered packs are shown by the real setup."""
    if not os.path.exists(path):
        return None
    packs, cur = [], None
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            cur = {"id": line[1:-1], "apk": [], "flatpak": [], "mb": 0, "rec": False, "status": "demo"}
            packs.append(cur)
        elif cur is not None and "=" in line:
            k, v = (x.strip() for x in line.split("=", 1))
            if k in ("apk", "flatpak"):
                cur[k] = v.split()
            elif k == "mb":
                cur[k] = int(v or 0)
            elif k == "rec":
                cur[k] = v in ("1", "yes", "true")
            else:
                cur[k] = v
    return packs


def size(mb):
    return ("%.2f" % (mb / 1000)).rstrip("0").rstrip(".") + " GB" if mb >= 1000 else "%d MB" % mb


def size1(mb):
    """a pack card's size (one decimal)"""
    return "%.1f GB" % (mb / 1000) if mb >= 1000 else "%d MB" % mb
