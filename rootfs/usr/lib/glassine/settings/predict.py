"""Keyboard > Typing: the word predictor's settings and the learned words (shell spec 006,
FR-006 to FR-008a; docs/predictor.md).

  predict.conf   $XDG_CONFIG_HOME/glassine/predict.conf (~/.config/glassine/predict.conf):
                 `KEY = VALUE` lines, the last line of a key wins. glassine-predict reads
                 `learning`, `addresses` and `languages`; `suggestions` and `handwriting`
                 are the keyboard's (on unless `off`, `false` or `0`). Settings changes
                 only the lines of the key it sets and keeps every other line (comments,
                 unknown keys); it writes a temporary file beside it, syncs and renames it.
  the words      only through `glassine-predict words list|removed|erase` (the store's own
                 lock); Settings never opens the store. The Words page counts them (stats())
                 and offers their folder, $XDG_DATA_HOME/glassine/predict
                 (~/.local/share/glassine/predict), as glassine-predict finds it: opened in
                 the file manager (xdg-open) when one handles folders (xdg-mime's default
                 for inode/directory), else its path to copy. Settings never creates it.
  the languages  a language is installed when its base model is there:
                 $GLASSINE_PREDICT_MODELS/LANG.gpm (/usr/share/glassine/predict/LANG.gpm)
                 with a valid header. The active ones are glassine-predict's (active(),
                 docs/predictor.md "Active languages"): `languages` when set; else the
                 layout's language when installed; else the installed ones by code, at most 4;
                 else the layout's (learned words only). Only installed languages have a
                 switch; one without a model is offered to install, never shown as on.
  the packages   each language's model is the package glassine-predict-model-LANG; the
                 languages to add are those the package indexes have (packages.py's data,
                 the same the Packages page shows), installed through its change flow.
  handwriting    the samples belong to the handwriting recognizer (GlassineOS/handwriting
                 spec 001, FR-010), which doesn't exist yet: hw_samples() and hw_erase()
                 are the place to call it.

No GLib here: the host tests use it as it is.
"""
import datetime
import os
import re
import shutil
import struct
import subprocess
import tempfile
import time

KEYS = ("suggestions", "handwriting", "learning", "addresses", "languages")
ADDRESSES = ("off", "suggest", "learn")
MAX_LANGS = 4                     # the predictor takes at most 4 languages (docs/predictor.md)
LANG_RE = re.compile(r"^[a-z]{2,3}([-_][A-Za-z0-9]+)?$")
MODEL_PKG = "glassine-predict-model-"     # + LANG: one language's base model
NOT_LANG = ("doc", "dev", "dbg", "lang")  # subpackage suffixes that aren't a language
CMD = "glassine-predict"          # found on PATH (/usr/bin on the device)
# the languages' own names (the design lists them so); others show their code
NAMES = {"en": "English", "de": "Deutsch", "fr": "Français", "es": "Español", "it": "Italiano",
         "nl": "Nederlands", "pt": "Português", "pl": "Polski", "sv": "Svenska", "no": "Norsk",
         "nb": "Norsk", "da": "Dansk", "fi": "Suomi", "cs": "Čeština"}


def conf_path():
    x = os.environ.get("XDG_CONFIG_HOME", "")
    if x.startswith("/"):
        return os.path.join(x, "glassine", "predict.conf")
    return os.path.join(os.environ.get("HOME", "/"), ".config", "glassine", "predict.conf")


def models_dir():
    return os.environ.get("GLASSINE_PREDICT_MODELS") or "/usr/share/glassine/predict"


def model_lang(pkg):
    """glassine-predict-model-LANG -> LANG, else None"""
    if not pkg.startswith(MODEL_PKG):
        return None
    code = pkg[len(MODEL_PKG):]
    if not LANG_RE.match(code) or re.split(r"[-_]", code)[-1] in NOT_LANG:
        return None
    return code


def store_dir():
    """the learned words' folder, as glassine-predict finds it (store_dir in store.c): an
    absolute $XDG_DATA_HOME, else $HOME/.local/share; None without a usable home"""
    x, h = os.environ.get("XDG_DATA_HOME", ""), os.environ.get("HOME", "")
    if x.startswith("/"):
        return os.path.join(x, "glassine", "predict")
    if h.startswith("/"):
        return os.path.join(h, ".local", "share", "glassine", "predict")
    return None


def short_path(path):
    """the path with the home folder as ~"""
    h = os.environ.get("HOME", "").rstrip("/")
    if h and (path == h or path.startswith(h + "/")):
        return "~" + path[len(h):]
    return path


def folder_opener(timeout=5):
    """True when something opens a folder: xdg-open is there and xdg-mime names a default
    application for inode/directory"""
    if not shutil.which("xdg-open") or not shutil.which("xdg-mime"):
        return False
    try:
        p = subprocess.run(("xdg-mime", "query", "default", "inode/directory"), capture_output=True,
                           text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return False
    return p.returncode == 0 and bool(p.stdout.strip())


def open_folder(path):
    """the folder in the file manager (xdg-open, not waited for); False when it can't start"""
    try:
        subprocess.Popen(("xdg-open", path), stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                         stderr=subprocess.DEVNULL, start_new_session=True)
    except OSError:
        return False
    return True


def lang_name(code):
    return NAMES.get(code) or NAMES.get(re.split(r"[-_]", code)[0], code)


# -- predict.conf ---------------------------------------------------------------------------

def _key_of(line):
    """the key a line sets as glassine-predict reads it (conf_get), else None"""
    s = line.lstrip(" \t")
    for k in KEYS:
        if s.startswith(k) and s[len(k):].lstrip(" \t").startswith("="):
            return k
    return None


def _val_of(line):
    return line.split("=", 1)[1].strip(" \t\r\n")


def read_conf(path=None):
    """-> {key: value} for the keys Settings knows (the last line of each wins)"""
    out = {}
    try:
        with open(path or conf_path(), encoding="utf-8", errors="replace") as f:
            for line in f:
                k = _key_of(line)
                if k:
                    out[k] = _val_of(line)
    except OSError:
        pass
    return out


def write_conf(changes, path=None):
    """changes {key: value or None}: None removes the key's lines, a value replaces the
    last line of the key (earlier ones are removed, so the file says it once) or is added
    at the end. Every other line stays as it is."""
    path = path or conf_path()
    try:
        with open(path, encoding="utf-8", errors="surrogateescape") as f:
            lines = f.read().splitlines(keepends=True)
    except FileNotFoundError:
        lines = []
    for k, v in changes.items():
        if v is not None and ("\n" in v or len(k) + len(v) + 4 > 250):
            raise ValueError("%s: value too long" % k)
        idx = [i for i, l in enumerate(lines) if _key_of(l) == k]
        new = "%s = %s\n" % (k, v) if v is not None else None
        if new and idx:
            lines[idx[-1]] = new
            idx = idx[:-1]
        for i in reversed(idx):
            del lines[i]
        if new and not any(_key_of(l) == k for l in lines):
            if lines and not lines[-1].endswith("\n"):
                lines[-1] += "\n"
            lines.append(new)
    d = os.path.dirname(path)
    os.makedirs(d, exist_ok=True)
    mode = 0o644
    try:
        mode = os.stat(path).st_mode & 0o777
    except OSError:
        pass
    fd, tmp = tempfile.mkstemp(prefix=".predict.conf.", dir=d)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", errors="surrogateescape") as f:
            f.write("".join(lines))
            f.flush()
            os.fsync(f.fileno())
        os.chmod(tmp, mode)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def flag(v, default=True):
    """on/off as glassine-predict reads `learning`: off, false, 0 are off"""
    if v is None:
        return default
    return v not in ("off", "false", "0")


def addresses(v):
    return v if v in ("off", "learn") else "suggest"


def parse_langs(v):
    """`languages` as glassine-predict takes it -> [codes], or None (unset or invalid)"""
    if v is None:
        return None
    out = [x.strip() for x in v.split(",")]
    if not out or len(out) > MAX_LANGS or len(set(out)) != len(out) or not all(LANG_RE.match(x) for x in out):
        return None
    return out


# -- the languages --------------------------------------------------------------------------

def model_words(path):
    """the number of words in a base model (gpm.h: magic GLPM, version 1, nvocab at 16),
    None when the file isn't a valid model"""
    try:
        with open(path, "rb") as f:
            h = f.read(20)
    except OSError:
        return None
    if len(h) < 20 or h[:4] != b"GLPM" or struct.unpack_from("<I", h, 4)[0] != 1:
        return None
    return struct.unpack_from("<I", h, 16)[0]


def installed(d=None):
    """{code: words} of the base models there"""
    d = d or models_dir()
    out = {}
    try:
        names = os.listdir(d)
    except OSError:
        return out
    for n in names:
        if n.endswith(".gpm") and LANG_RE.match(n[:-4]):
            w = model_words(os.path.join(d, n))
            if w is not None:
                out[n[:-4]] = w
    return out


def active(langs, layout, models):
    """the active languages as glassine-predict takes them (docs/predictor.md, "Active
    languages"): langs (`languages`, None: unset) as it is; else the layout's language when
    its model is installed; else the installed languages by code, at most MAX_LANGS; else,
    with none installed, the layout's language (learned words only)"""
    if langs:
        return list(langs)
    if layout in models:
        return [layout]
    if models:
        return sorted(models)[:MAX_LANGS]
    return [layout]


def with_model(active, models):
    """the active languages that have a model: those that give base suggestions"""
    return [c for c in active if c in models]


def lang_rows(models, active, layout):
    """the switches of the Languages page (the installed languages: one without a model is
    never shown as on), in the order they are shown and written: the layout's language
    first, then by name -> [(code, name, words, on)]"""
    order = sorted(models, key=lambda c: (c != layout, lang_name(c).casefold(), c))
    return [(c, lang_name(c), models[c], c in active) for c in order]


def to_add(pkgs, models, active, layout):
    """the languages offered to install -> [(code, name, package or None, why)]: the
    layout's language without a model first (why "layout"), then the active ones without a
    model (why "on", a `languages` list naming them), then the indexes' other packages
    without a model, by name (why None). package: in the indexes, else None (nothing to
    install from)"""
    pk = {}
    for n in pkgs:
        c = model_lang(n)
        if c and c not in models:
            pk[c] = n
    first = [c for c in [layout] + list(active) if c not in models]
    first = [c for i, c in enumerate(first) if c not in first[:i]]
    rest = sorted((c for c in pk if c not in first), key=lambda c: (lang_name(c).casefold(), c))
    return [(c, lang_name(c), pk.get(c), "layout" if c == layout else "on" if c in active else None)
            for c in first + rest]


def missing_layout(models, layout, pkgs):
    """the Keyboard page's note: the layout language's package when its model isn't
    installed but the indexes have it, else None"""
    if layout in models:
        return None
    return next((n for n in pkgs if model_lang(n) == layout), None)


def toggled(rows, code):
    """the active list after a tap on code's toggle (the rows' order), or a reason it
    can't change: ("last", None) / ("max", None)"""
    on = [c for c, _, _, a in rows if a]
    if code in on:
        if len(on) == 1:
            return "last", None
        return None, [c for c in on if c != code]
    if len(on) >= MAX_LANGS:
        return "max", None
    return None, [c for c, _, _, a in rows if a or c == code]


def fmt_words(n):
    return "{:,} words".format(n)


def stats(words, active, models):
    """the Words page's counts: [(code, name, learned words)] for the active languages (in
    their order), then the installed ones and any other with learned words (by name), the
    total, and the day of the last use (None: nothing learned)"""
    per, last = {}, None
    for _, _, lang, day in words:
        per[lang] = per.get(lang, 0) + 1
        if day is not None and (last is None or day > last):
            last = day
    rest = sorted((set(models) | set(per)) - set(active), key=lambda c: (lang_name(c).casefold(), c))
    rows = [(c, lang_name(c), per.get(c, 0)) for c in list(active) + rest]
    return rows, len(words), last


def today():
    """the store's day of now (days since 1970-01-01, UTC)"""
    return int(time.time() // 86400)


def fmt_day(day, now=None):
    """a store day as the page says it: Today, Yesterday, or the date (3 November 2026)"""
    now = today() if now is None else now
    if day == now:
        return "Today"
    if day == now - 1:
        return "Yesterday"
    d = datetime.date(1970, 1, 1) + datetime.timedelta(days=day)
    return "%d %s %d" % (d.day, MONTHS[d.month - 1], d.year)     # (English, as the page; not the locale's)


MONTHS = ("January", "February", "March", "April", "May", "June", "July", "August", "September",
          "October", "November", "December")


def fmt_count(n):
    """a number of words: None, 1 word, 2,140 words"""
    return "None" if not n else "1 word" if n == 1 else "{:,} words".format(n)


# -- the words command ----------------------------------------------------------------------

class Failed(Exception):
    """glassine-predict is missing or failed: a short text for the person"""


def words_cmd(*args, timeout=15):
    try:
        p = subprocess.run((CMD, "words") + args, capture_output=True, text=True, timeout=timeout)
    except FileNotFoundError:
        raise Failed("Word suggestions aren’t installed on this system.")
    except (OSError, subprocess.SubprocessError) as e:
        raise Failed(str(e))
    if p.returncode != 0:
        msg = (p.stderr.strip().splitlines() or [""])[-1]
        raise Failed(msg or "glassine-predict words %s stopped with status %d." % (args[0], p.returncode))
    return p.stdout


def learned():
    """-> [(word, count, lang, day)], most used first (the command's order); day: of the
    last use, days since 1970-01-01 (UTC), None when the line has none"""
    out = []
    for line in words_cmd("list").splitlines():
        f = line.split("\t")
        if len(f) >= 3:
            try:
                day = int(f[3]) if len(f) >= 4 and f[3].isdigit() else None
                out.append((f[1], int(f[2]), f[0], day))
            except ValueError:
                pass
    return out


def removed():
    """-> [word]; a line is `WORD`, or `LANG WORD` from a version 1 predictor"""
    return [line.split("\t")[-1] for line in words_cmd("removed").splitlines() if line.split("\t")[-1]]


def erase():
    words_cmd("erase")


# -- handwriting samples (the recognizer, GlassineOS/handwriting spec 001) -------------------

def hw_samples():
    """the number of handwriting samples kept for this person, None while there is no
    recognizer to ask"""
    return None


def hw_erase():
    """erase the handwriting samples and adaptation (the recognizer's own erase): nothing
    to do while there is no recognizer"""
