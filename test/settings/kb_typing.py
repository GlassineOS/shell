"""Keyboard > Typing (spec 006 FR-006 to FR-008a): predict.conf read and written as
glassine-predict reads it (the other lines kept, atomic), the languages from the models,
the toggles writing `languages =` in order, the addresses choice, and the Words page: its
counts, the folder (opened when something opens folders, else its path to copy; never
made by the page) and Forget, through `glassine-predict words`: a stand-in that prints
what the multi-language predictor prints, the real one built from this repository on a
fixture store when a compiler is there, and a missing or failing one (a short error, no
crash).
Exit 1 on a failed check.
   typing.py REPO"""
import os
import shutil
import stat
import subprocess
import sys
import tempfile

import cairo

sys.path.insert(0, sys.argv[1] + "/rootfs/usr/lib/glassine")
from settings import app as A, backend as B, pages, predict as PR  # noqa: E402

fail = 0


def check(what, got, want):
    global fail
    if got == want:
        print("ok   " + what)
    else:
        print("FAIL %s\n     got:  %r\n     want: %r" % (what, got, want))
        fail = 1


T = tempfile.mkdtemp(dir=os.environ.get("TMPDIR"))
os.environ["HOME"] = T + "/home"
os.environ["XDG_CONFIG_HOME"] = T + "/config"
os.environ["XDG_DATA_HOME"] = T + "/data"
os.environ["GLASSINE_PREDICT_MODELS"] = T + "/models"
CONF = T + "/config/glassine/predict.conf"

# -- predict.conf ---------------------------------------------------------------------------
check("the path: $XDG_CONFIG_HOME", PR.conf_path(), CONF)
os.environ["XDG_CONFIG_HOME"] = "relative"
check("the path: a relative $XDG_CONFIG_HOME is ignored (as glassine-predict)", PR.conf_path(), T + "/home/.config/glassine/predict.conf")
os.environ["XDG_CONFIG_HOME"] = T + "/config"
check("no file: nothing set", PR.read_conf(), {})
os.makedirs(os.path.dirname(CONF))
orig = ("# my settings\n  learning = off  \nlanguagesx = no\nfuture_key = 7\n\naddresses=learn\n"
        "learning = on\n# trailing comment\nlearningfoo = 1")
open(CONF, "w").write(orig)
os.chmod(CONF, 0o600)
check("read: the last line of a key wins, look-alike keys aren't it",
      PR.read_conf(), {"learning": "on", "addresses": "learn"})
PR.write_conf({"learning": "off", "languages": "en,de"})
txt = open(CONF).read()
check("write: unknown keys, comments, blank lines and look-alikes kept; the key said once",
      txt, "# my settings\nlanguagesx = no\nfuture_key = 7\n\naddresses=learn\nlearning = off\n# trailing comment\n"
      "learningfoo = 1\nlanguages = en,de\n")
check("write: read back", PR.read_conf(), {"learning": "off", "addresses": "learn", "languages": "en,de"})
check("write: the file's mode kept", stat.S_IMODE(os.stat(CONF).st_mode), 0o600)
check("write: no temporary file left", sorted(os.listdir(os.path.dirname(CONF))), ["predict.conf"])
PR.write_conf({"languages": None})
check("write: None removes the key", "languages" in PR.read_conf(), False)
check("flags as glassine-predict reads them", [PR.flag(v) for v in (None, "on", "off", "false", "0", "yes")],
      [True, True, False, False, False, True])
check("addresses: anything else is suggest", [PR.addresses(v) for v in (None, "off", "learn", "x")], ["suggest", "off", "learn", "suggest"])
check("languages: valid lists only", [PR.parse_langs(v) for v in (None, "en, de", "en,en", "", "en,de,fr,es,it", "EN", "pt-BR")],
      [None, ["en", "de"], None, None, None, None, ["pt-BR"]])

# -- the models -----------------------------------------------------------------------------
os.makedirs(T + "/models")
import struct  # noqa: E402
open(T + "/models/en.gpm", "wb").write(b"GLPM" + struct.pack("<I", 1) + b"en".ljust(8, b"\0") + struct.pack("<I", 40000) + b"\0" * 64)
open(T + "/models/fr.gpm", "wb").write(b"GLPM" + struct.pack("<I", 9) + b"\0" * 80)      # a newer format: not installed
open(T + "/models/xx.txt", "w").write("not a model")
open(T + "/models/de.gpm", "wb").write(b"junk")
check("installed: valid models only, with their word count", PR.installed(), {"en": 40000})
check("a word count as the design shows it", PR.fmt_words(40000), "40,000 words")

# -- the backend's Typing section (the real one, its system parts left out) -----------------
R = B.Real
be = R.__new__(R)
B.Base.__init__(be)
errors = []
be.error = lambda t, x: errors.append(t)
be.changed = lambda: None
be.bg = lambda fn, *a: fn(*a)
be.refresh = lambda page: None                  # (the device reads: not here)
open(CONF, "w").write("# keep me\nlearning = off\naddresses = off\nsuggestions = off\nhandwriting = 0\n")
be.osk = "german"
be.read_typing()
check("read: the switches, the address choice", (be.kb_sugg, be.kb_hw, be.kb_learn, be.kb_addr), (False, False, False, "off"))
# the reported state: only English installed, `languages` unset, the layout German
check("unset, the layout's language not installed: the installed ones are active", (be.kb_active(), be.kb_dicts()), (["en"], ["en"]))
check("... its rows: only installed languages, never one without a dictionary shown as on",
      be.kb_lang_rows(), [("en", "English", 40000, True)])
MP = PR.MODEL_PKG
check("... the layout's language offered first, with why",
      be.kb_to_add([MP + "fr", MP + "en", MP + "de", MP + "es"]),
      [("de", "Deutsch", MP + "de", "layout"), ("es", "Español", MP + "es", None), ("fr", "Français", MP + "fr", None)])
check("... also when the indexes don't have it (nothing to install from)", be.kb_to_add([])[0], ("de", "Deutsch", None, "layout"))
check("... the Keyboard page's note: the package when the indexes have it",
      [PR.missing_layout(be.kb_models, "de", [MP + "en", MP + "de"]), PR.missing_layout(be.kb_models, "de", [MP + "en"]),
       PR.missing_layout(be.kb_models, "en", [MP + "en"])], [MP + "de", None, None])
check("active: the rule as glassine-predict's (set list, layout installed, installed by code, at most 4, none)",
      [PR.active(["fr", "en"], "de", {"en": 1}), PR.active(None, "de", {"en": 1, "de": 1}),
       PR.active(None, "sv", dict.fromkeys(("nl", "fr", "en", "es", "de"), 1)), PR.active(None, "de", {})],
      [["fr", "en"], ["de"], ["de", "en", "es", "fr"], ["de"]])
GPM = lambda c, n: b"GLPM" + struct.pack("<I", 1) + c.encode().ljust(8, b"\0") + struct.pack("<I", n) + b"\0" * 64  # noqa: E731
open(T + "/models/de.gpm", "wb").write(GPM("de", 30000))
be.kb_installed("de")
check("the layout's language installed: it alone is active, first in the rows",
      (be.kb_active(), be.kb_lang_rows()), (["de"], [("de", "Deutsch", 30000, True), ("en", "English", 40000, False)]))
for c in ("es", "it", "nl", "sv"):
    open(T + "/models/%s.gpm" % c, "wb").write(GPM(c, 1000))
be.read_typing()
rows = be.kb_lang_rows()
check("the language rows: the installed ones, the layout's first, then by name",
      [(c, n, w, on) for c, n, w, on in rows],
      [("de", "Deutsch", 30000, True), ("en", "English", 40000, False), ("es", "Español", 1000, False),
       ("it", "Italiano", 1000, False), ("nl", "Nederlands", 1000, False), ("sv", "Svenska", 1000, False)])
be.toggle_kb_lang("en")
check("a language on: languages = written in the rows' order", PR.read_conf().get("languages"), "de,en")
be.toggle_kb_lang("es")
be.toggle_kb_lang("de")
check("... then one off, one on: the order kept", PR.read_conf().get("languages"), "en,es")
be.toggle_kb_lang("es")
be.toggle_kb_lang("en")
check("the last one can't go off (an empty list would be unset)", (PR.read_conf().get("languages"), errors[-1:]),
      ("en", ["At least one language"]))
# a set list naming a language without a dictionary (installed, then removed)
PR.write_conf({"languages": "fr,en"})
be.read_typing()
check("a set list with one not installed: kept as set, only the installed one gives words", (be.kb_active(), be.kb_dicts()),
      (["fr", "en"], ["en"]))
check("... not a row; offered to install, with why", ("fr" in [r[0] for r in be.kb_lang_rows()], be.kb_to_add([MP + "fr"])),
      (False, [("fr", "Français", MP + "fr", "on")]))
PR.write_conf({"languages": "fr"})
be.read_typing()
check("a set list with none installed: no dictionary active", be.kb_dicts(), [])
be.toggle_kb_lang("en")
check("... turning an installed one on writes only the rows that are on", PR.read_conf().get("languages"), "en")
os.unlink(T + "/models/de.gpm")
open(T + "/models/de.gpm", "wb").write(b"junk")
be.read_typing()
for c in ("nl", "es", "it"):
    be.toggle_kb_lang(c)
check("four on, in the rows' order", PR.read_conf().get("languages"), "en,es,it,nl")
n = len(errors)
be.toggle_kb_lang("sv")
check("at most 4 languages", (len(PR.parse_langs(PR.read_conf()["languages"])), errors[n:]), (4, ["At most 4 languages"]))
be.set_kb_addr("learn")
be.set_kb_sugg(True)
be.set_kb_learn(True)
be.set_kb_hw(True)
c = PR.read_conf()
check("the address choice and the switches written", (c["addresses"], c["suggestions"], c["learning"], c["handwriting"]),
      ("learn", "on", "on", "on"))
check("... the comment still there", open(CONF).read().startswith("# keep me\n"), True)
be.read_typing()
check("... and read back", (be.kb_sugg, be.kb_hw, be.kb_learn, be.kb_addr), (True, True, True, "learn"))
check("handwriting samples: no recognizer on PATH", (be.kb_hw_n, be.kb_hw_writers), (None, 0))

# -- languages as packages (glassine-predict-model-LANG), the Packages flow (demo data) ------
check("model packages: the language from the name, not the parent or a -doc",
      [PR.model_lang(n) for n in ("glassine-predict-model-en", "glassine-predict-model-pt-BR", "glassine-predict-model",
                                  "glassine-predict-model-en-doc", "glassine-predict-modelx-en", "vim")],
      ["en", "pt-BR", None, None, None, None])
check("to add: in the indexes, without a model; layout's first then by name",
      PR.to_add(["glassine-predict-model", "glassine-predict-model-fr", "glassine-predict-model-en", "glassine-predict-model-es",
                 "glassine-predict-model-de", "glassine-predict-model-de-doc"], {"en": 1}, ["en"], "fr"),
      [("fr", "Français", "glassine-predict-model-fr", "layout"), ("de", "Deutsch", "glassine-predict-model-de", None),
       ("es", "Español", "glassine-predict-model-es", None)])
check("a model package's name in the change sheets", A.packages.pretty("glassine-predict-model-fr"), "Français for the keyboard")

dm = B.Demo()
dm.error = lambda t, x: errors.append(t)
ui = A.UI(dm, lambda: None)
ui.L = A.Layout(702, 884)
ui.go("kb-langs")


def blocks():
    return pages.build(ui, dm).blocks


def row(label):
    return next(b for b in blocks() if getattr(b, "label", None) == label)


labels = [getattr(b, "label", None) for b in blocks()]
check("Languages: On the keyboard = the installed, Add a language = the rest of the indexes",
      labels[labels.index("On the keyboard"):], ["On the keyboard", "English", "Deutsch", "Add a language", "Español", "Français",
                                                 "All keyboard languages"])
check("an Install row: the download size from the index", (row("Français").act, row("Français").sub), ("Install", "Download 2.8 MB"))
row("Français").act_fn()
check("Install: the existing flow's first-change note", ui.sheet.title, "Before your first change")
ui.sheet.ok()
check("... then the plan sheet", (ui.sheet.title, ui.sheet.ok_label), ("Install Français for the keyboard?", "Install"))
plan = ui.sheet.plan
ui.sheet.ok()
check("while it runs: the status line, every Install off and busy",
      (type(blocks()[0]).__name__, row("Español").act_off, row("Español").sub), ("Status", True, A.packages.BUSY))
ui.pk.d.run = None
ui.pk.d._apply(plan)
ui.pk.d.result = {"kind": "ok", "plan": plan, "files": [], "restart": ""}
ui.pk.show_result()
check("done: the result sheet", ui.sheet.title, "Installed Français for the keyboard")
labels = [getattr(b, "label", None) for b in blocks()]
check("... Français is on the keyboard now, off; no longer offered",
      (labels[labels.index("On the keyboard"):labels.index("Add a language")], row("Français").on, labels.count("Français")),
      (["On the keyboard", "English", "Deutsch", "Français"], False, 1))
ui.sheet = None
row("Español").act_fn()
plan = ui.sheet.plan
ui.sheet.ok()
ui.pk.d.run = None
ui.pk.d.result = {"kind": "error", "plan": plan, "msg": "ERROR: unable to select packages"}
ui.pk.show_result()
labels = [getattr(b, "label", None) for b in blocks()]
check("a failed install: the error sheet, the language still to add",
      (ui.sheet.title, "Español" in labels[labels.index("Add a language"):]), ("Español for the keyboard wasn’t installed", True))
ui.sheet = None
row("All keyboard languages").fn()
check("All keyboard languages: the Packages search for the models",
      (ui.page, ui.pk.q, sorted(ui.pk.res[2])), ("pkg-search", "glassine-predict-model",
                                                  sorted(PR.MODEL_PKG + c for c in ("en", "de", "fr", "es"))))
pd = ui.pk.d
pd.inst.discard(PR.MODEL_PKG + "es")
pd.db = {k: v for k, v in pd.db.items() if not k.startswith(PR.MODEL_PKG) or k in pd.inst}
ui.go("kb-langs")
check("no languages in the indexes: a note, no Install rows",
      [type(b).__name__ for b in blocks()][-3:], ["Head", "Note", "Item"])

# the reported state on the pages (demo): only English installed, `languages` unset, the layout German
os.environ["GLASSINE_DEMO_KB"] = "layout-missing"
dm = B.Demo()
dm.error = lambda t, x: errors.append(t)
ui = A.UI(dm, lambda: None)
ui.L = A.Layout(702, 884)
del os.environ["GLASSINE_DEMO_KB"]
ui.go("kb-langs")
labels = [getattr(b, "label", None) for b in blocks()]
check("layout German, no German dictionary: English on, German offered first",
      labels[labels.index("On the keyboard"):], ["On the keyboard", "English", "Add a language", "Deutsch", "Español", "Français",
                                                 "All keyboard languages"])
check("... English is on", row("English").on, True)
check("... German's line: the keyboard layout, Install", (row("Deutsch").sub, row("Deutsch").act, row("Deutsch").act_off),
      ("Your keyboard layout · Download 3.0 MB", "Install", False))
check("... no learned-words-only note (English has a dictionary)",
      any(type(b).__name__ == "Note" and "learned words" in b.label for b in blocks()), False)
ui.go("keyboard")
note = row("No dictionary for Deutsch")
check("Keyboard: the note under the layout, with Install", (note.sub, note.act, note.act_off),
      ("Suggestions in English for now · Download 3.0 MB", "Install", False))
check("... the Typing summary names the language that gives words", row("Suggestion languages").sub, "English")
note.act_fn()
ui.sheet.ok()                                   # (the first-change note)
check("Install from the note: the Languages page's flow", (ui.sheet.title, ui.sheet.ok_label),
      ("Install Deutsch for the keyboard?", "Install"))
plan = ui.sheet.plan
ui.sheet.ok()
ui.pk.d.run = None
ui.pk.d._apply(plan)
ui.pk.d.result = {"kind": "ok", "plan": plan, "files": [], "restart": ""}
ui.pk.show_result()
ui.sheet = None
labels = [getattr(b, "label", None) for b in blocks()]
check("... installed: the note is gone, German suggests", ("No dictionary for Deutsch" in labels,
                                                           dm.kb_active(), row("Suggestion languages").sub), (False, ["de"], "Deutsch"))
dm.osk = "us"
check("a layout whose language is installed: no note", any("No dictionary for" in (l or "") for l in
                                                            [getattr(b, "label", None) for b in blocks()]), False)
dm.kb_langs, dm.kb_models = ["fr"], {"en": 40000}
ui.go("kb-langs")
check("a set list with none installed: the page says learned words only",
      next((b.label for b in blocks() if type(b).__name__ == "Note" and "learned words" in b.label), None),
      "No language that is on has a dictionary, so suggestions come only from learned words. "
      "Install one below or turn on an installed one.")
check("... the chosen one offered with why", row("Français").sub, "Turned on, not installed · Download 2.8 MB")
ui.go("keyboard")
check("... the Keyboard summary says so", row("Suggestion languages").sub, "No dictionary · learned words only")

# -- the Words page: counts, the folder, Forget ---------------------------------------------
check("stats: the active languages first (their order), then installed and learned-only ones by name",
      PR.stats([("okay", 9, "en", 20406), ("Haus", 2, "de", 20410), ("hola", 1, "es", None), ("tea", 4, "en", 20400)],
               ["en", "de"], {"en": 40000, "fr": 40000}),
      ([("en", "English", 2), ("de", "Deutsch", 1), ("es", "Español", 1), ("fr", "Français", 0)], 4, 20410))
check("stats: nothing learned", PR.stats([], ["en"], {}), ([("en", "English", 0)], 0, None))
check("a day as the page says it", [PR.fmt_day(d, now=20406) for d in (20406, 20405, 20405 - 1)],
      ["Today", "Yesterday", "12 November 2025"])
check("a count of words", [PR.fmt_count(n) for n in (0, 1, 2140)], ["None", "1 word", "2,140 words"])
PR.today = lambda: 30000                        # (the page's "Today": not the test's)
for c in ("es", "it", "nl", "sv"):              # (the languages page's extra models: not here)
    if os.path.exists(T + "/models/%s.gpm" % c):
        os.unlink(T + "/models/%s.gpm" % c)
STORE = T + "/data/glassine/predict"
check("the folder: $XDG_DATA_HOME, as glassine-predict", PR.store_dir(), STORE)
os.environ["XDG_DATA_HOME"] = "relative"
check("the folder: a relative $XDG_DATA_HOME is ignored", PR.store_dir(), T + "/home/.local/share/glassine/predict")
os.environ["XDG_DATA_HOME"] = T + "/data"
check("the folder shown with ~", PR.short_path(T + "/home/.local/share/glassine/predict"), "~/.local/share/glassine/predict")

STUB = T + "/stub"
os.makedirs(STUB)
STATE = T + "/stub-state"
open(STUB + "/glassine-predict", "w").write("""#!/bin/sh
# a stand-in for glassine-predict words (the multi-language predictor's output)
S=%s
echo "$*" >> $S.log
[ -e $S.fail ] && { echo "glassine-predict: words: the store can't be written" >&2; exit 1; }
[ "$1" = words ] || exit 2
case $2 in
list) [ -e $S.erased ] || printf 'en\\tokay\\t96\\t20406\\nde\\tFeierabend\\t21\\t20407\\nen\\tteh\\t3\\t20400\\n' ;;
removed) [ -e $S.erased ] || printf 'gonna\\nwanna\\n' ;;
erase) touch $S.erased ;;
*) exit 2 ;;
esac
""" % STATE)
# xdg-mime names a file manager only while $S.mime exists; xdg-open writes what it opened
open(STUB + "/xdg-mime", "w").write("#!/bin/sh\n[ \"$*\" = 'query default inode/directory' ] && [ -e %s.mime ] && echo files.desktop\nexit 0\n" % STATE)
open(STUB + "/xdg-open", "w").write("#!/bin/sh\necho \"$*\" > %s.opened\n" % STATE)
for n in ("glassine-predict", "xdg-mime", "xdg-open"):
    os.chmod(STUB + "/" + n, 0o755)
PATH0 = os.environ["PATH"]
os.environ["PATH"] = STUB + ":" + PATH0
PR.write_conf({"languages": "en,de"})
be.read_typing()
be.kb_read_words()
check("list: most used first, with the language and the day",
      be.kb_words, [("okay", 96, "en", 20406), ("Feierabend", 21, "de", 20407), ("teh", 3, "en", 20400)])
check("removed: read", be.kb_gone, ["gonna", "wanna"])
check("no folder yet, nothing opens folders", (be.kb_dir, be.kb_dir_there, be.kb_opener), (STORE, False, False))


def page(name, u=None):
    u = u or A.UI(be, lambda: None)
    u.page = name
    s = cairo.ImageSurface(cairo.FORMAT_RGB24, 702, 884)
    A.Renderer(u).draw(cairo.Context(s), 702, 884, 1)
    u.L = A.Layout(702, 884)
    return pages.build(u, be).blocks


def rows(name, u=None):
    """the page's rows as (kind, label, value or sub or action)"""
    out = []
    for b in page(name, u):
        k = type(b).__name__
        if k == "KV":
            out.append((k, b.label, b.val))
        elif k == "Item":
            out.append((k, b.label, b.sub, b.act if not b.act_off else None))
        elif k in ("Head", "Note"):
            out.append((k, b.label))
    return out


def section(name, head, u=None):
    """the rows under a heading, up to the next one"""
    out, on = [], False
    for r in rows(name, u):
        if r[0] == "Head":
            on = r[1] == head
        elif on:
            out.append(r)
    return out


def first(name, kind, label, u=None):
    return next((b for b in page(name, u) if type(b).__name__ == kind and b.label == label), None)


check("words page: the counts per language, in all, removed, the last day; no word rows", [r for r in rows("kb-words") if r[0] == "KV"],
      [("KV", "English", "2 words"), ("KV", "Deutsch", "1 word"), ("KV", "In all", "3 words"), ("KV", "Removed", "2 words"),
       ("KV", "Last learned", "15 November 2025")])
check("... no folder: no link, it says nothing is learned yet; Forget is off",
      (section("kb-words", "Word folder"), section("kb-words", "Forget")),
      ([("Note", "No words are learned yet. Their folder is made when the first word is learned.")],
       [("Item", "Forget all learned words", "Nothing learned", None)]))
check("... and the page made no folder", os.path.exists(STORE), False)
os.makedirs(STORE)
be.kb_read_words()
check("a folder, nothing opens folders: the path with Copy and a note",
      section("kb-words", "Word folder"),
      [("Item", "Word folder", STORE, "Copy"),
       ("Note", "Nothing on this system opens folders. The words are in the file “words” there; a text editor opens it.")])
u = A.UI(be, lambda: None)
u.copied = STORE
check("... once copied, its button says so", first("kb-words", "Item", "Word folder", u).act, "Copied")
open(STATE + ".mime", "w").close()
be.kb_read_words()
link = first("kb-words", "Item", "Open the word folder")
check("something opens folders: the link with the folder's path", (be.kb_opener, link and link.sub, link and link.icon),
      (True, STORE, "folder"))
link.fn()
import time  # noqa: E402
for _ in range(50):
    if os.path.exists(STATE + ".opened"):
        break
    time.sleep(0.05)
check("... a tap opens the folder in the file manager", open(STATE + ".opened").read().strip() if os.path.exists(STATE + ".opened") else None, STORE)
os.unlink(STATE + ".mime")
open(STUB + "/xdg-mime", "w").write("#!/bin/sh\nexit 3\n")
check("xdg-mime failing: nothing opens folders", PR.folder_opener(), False)
os.environ["PATH"] = T + "/only-predict:" + PATH0
os.makedirs(T + "/only-predict")
os.symlink(STUB + "/glassine-predict", T + "/only-predict/glassine-predict")
check("no xdg-open: nothing opens folders", PR.folder_opener(), False)
os.environ["PATH"] = STUB + ":" + PATH0

hw = []
HW_ERASE0 = PR.hw_erase
PR.hw_erase = lambda: hw.append(1)
ui = A.UI(be, lambda: None)
ui.page = "kb-words"
check("Forget is on with words learned", first("kb-words", "Item", "Forget all learned words").act_off, False)
ui.sheet_forget_words()
check("Forget: the question says what goes", (ui.sheet.title, ui.sheet.ok_label, "list of removed words" in ui.sheet.body),
      ("Forget all learned words?", "Forget", True))
ui.sheet.ok()
check("... the words command's erase, the lists read again, no sheet left; handwriting untouched",
      (open(STATE + ".log").read().splitlines()[-3], be.kb_words, be.kb_gone, ui.sheet, hw), ("words erase", [], [], None, []))
check("... then nothing to forget", (first("kb-words", "Item", "Forget all learned words").act_off,
                                    first("kb-words", "KV", "Last learned").val), (True, "Nothing yet"))
os.unlink(STATE + ".erased")
be.kb_read_words()
open(STATE + ".fail", "w").close()
n = len(errors)
be.kb_forget()
check("a failing erase: a short error, the page still has a reason", (errors[n:], be.kb_err),
      (["Could not forget the learned words"], "glassine-predict: words: the store can't be written"))
check("... the folder is still offered", first("kb-words", "Item", "Word folder") is not None, True)
os.unlink(STATE + ".fail")
be.kb_read_words()
ui = A.UI(be, lambda: None)
ui.page = "keyboard"
ui.sheet_erase_learned()
ui.sheet.ok()
check("the keyboard page's erase: the words and the handwriting samples", (be.kb_words, be.kb_gone, ui.sheet, hw), ([], [], None, [1]))
item = first("keyboard#3", "Item", "Learned words")
check("the keyboard page's link to the Words page", item and item.sub, "None yet")
os.unlink(STATE + ".erased")
be.kb_read_words()
check("... with words: how many, how many removed", first("keyboard#3", "Item", "Learned words").sub, "3 words · 2 removed")

# the handwriting recognizer's samples stats (contract 1.3): the person's samples, and a
# note while the handwriting samples app keeps guest writers' stores beside them
open(STUB + "/glassine-handwriting", "w").write("""#!/bin/sh
S=%s
echo "$*" >> $S.hw.log
case "$*" in
"samples stats") printf 'learning\\ton\\nsamples\\t%%s\\nclient\\torg.example\\t3\\ndonation\\t0\\nwriters\\t%%s\\n' \\
    "$(cat $S.hw.n 2>/dev/null || echo 5)" "$(cat $S.hw.w 2>/dev/null || echo 2)" ;;
"samples erase") echo 0 > $S.hw.n; echo erased ;;
*) exit 2 ;;
esac
""" % STATE)
os.chmod(STUB + "/glassine-handwriting", 0o755)
WNOTE = "Handwriting of %d other writer%s is kept in the Handwriting samples app, and removed there."
be.read_typing()
check("handwriting: the person's samples and the writers counted", (be.kb_hw_n, be.kb_hw_writers), (5, 2))
check("... the erase row counts the person's samples", first("keyboard#3", "Item", "Erase what was learned").sub,
      "3 words · 5 handwriting samples · 2 removed")
check("... and the page says the other writers' samples stay", first("keyboard#3", "Note", WNOTE % (2, "s")) is not None, True)
open(STATE + ".hw.w", "w").write("1\n")
be.read_typing()
check("... one writer", first("keyboard#3", "Note", WNOTE % (1, "")) is not None, True)
open(STATE + ".hw.w", "w").write("0\n")
be.read_typing()
check("... no writers: no note", [b for b in page("keyboard#3") if type(b).__name__ == "Note" and "other writer" in b.label], [])
PR.hw_erase = HW_ERASE0
open(STATE + ".hw.w", "w").write("2\n")
ui.sheet_erase_learned()
ui.sheet.ok()
be.read_typing()
check("the erase: the recognizer's samples erase, the person's store only", (open(STATE + ".hw.log").read().splitlines()[-2:],
      be.kb_hw_n, be.kb_hw_writers), (["samples erase", "samples stats"], 0, 2))
open(STUB + "/glassine-handwriting", "w").write("#!/bin/sh\nexit 1\n")
be.read_typing()
check("a failing recognizer: no handwriting counts, no note", (be.kb_hw_n, be.kb_hw_writers), (None, 0))
os.unlink(STUB + "/glassine-handwriting")
be.read_typing()

# missing glassine-predict: the pages render, the actions say why
os.environ["PATH"] = T + "/empty"
be.kb_read_words()
check("missing: no words, a reason", (be.kb_words, be.kb_err), (None, "Word suggestions aren’t installed on this system."))
for p in ("keyboard", "keyboard#3", "kb-words", "kb-langs"):
    try:
        page(p)
        check("missing: %s renders" % p, True, True)
    except Exception as e:  # noqa: BLE001
        check("missing: %s renders" % p, repr(e), None)

# the real glassine-predict from this repository (when a compiler is there), on a store
# written as a fixture: the counts come from it, Forget empties it
cc = shutil.which("cc", path=PATH0) or shutil.which("gcc", path=PATH0)
if cc:
    src = sys.argv[1] + "/src"
    bindir = T + "/bin"
    os.makedirs(bindir)
    r = subprocess.run([cc, "-O1", "-o", bindir + "/glassine-predict", src + "/glassine-predict.c", src + "/predict/store.c", "-lm"],
                       capture_output=True, text=True)
    if r.returncode:
        check("the real predictor builds", r.stderr[-400:], "")
    else:
        os.environ["PATH"] = bindir + ":" + PATH0
        open(STORE + "/words", "w").write("glassine-words 1 0\n" + "".join("w\ten\tword%s\t%d\t20700\n" % (c, i + 1) for i, c in enumerate("abcde")) +
                                          "w\tde\tHaus\t4\t20760\nw\tfr\tmerci\t1\t20500\nx\t*\tteh\nx\t*\tgonna\nnot a line it knows\n")
        be.kb_read_words()
        check("real: the counts from the store", [r for r in rows("kb-words") if r[0] == "KV"],
              [("KV", "English", "5 words"), ("KV", "Deutsch", "1 word"), ("KV", "Français", "1 word"), ("KV", "In all", "7 words"),
               ("KV", "Removed", "2 words"), ("KV", "Last learned", "3 November 2026")])
        be.kb_forget()
        check("real: forgotten, the removed list too", (be.kb_words, be.kb_gone, be.kb_err), ([], [], None))
        check("real: the file is left with its first line only", open(STORE + "/words").read(), "glassine-words 1 1\n")
else:
    print("skip the real predictor: no compiler")
os.environ["PATH"] = PATH0
shutil.rmtree(T)
sys.exit(fail)
