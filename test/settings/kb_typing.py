"""Keyboard > Typing (spec 006 FR-006 to FR-008a): predict.conf read and written as
glassine-predict reads it (the other lines kept, atomic), the languages from the models,
the toggles writing `languages =` in order, the addresses choice, and the learned words
page's remove / put back / erase through `glassine-predict words`: a stand-in that prints
what the multi-language predictor prints, the real one built from this repository when
a compiler is there, and a missing or failing one (a short error, no crash).
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
check("languages unset: the layout's language", be.kb_active(), ["de"])
rows = be.kb_lang_rows()
check("the language rows: the layout's first, then by name; installed or not",
      [(c, n, w, on) for c, n, w, on in rows if c in ("de", "en", "fr")],
      [("de", "Deutsch", None, True), ("en", "English", 40000, False), ("fr", "Français", None, False)])
be.toggle_kb_lang("en")
check("a language on: languages = written in the rows' order", PR.read_conf().get("languages"), "de,en")
be.toggle_kb_lang("fr")
be.toggle_kb_lang("de")
check("... then one off, one on: the order kept", PR.read_conf().get("languages"), "en,fr")
be.toggle_kb_lang("fr")
be.toggle_kb_lang("en")
check("the last one can't go off (an empty list would be unset)", (PR.read_conf().get("languages"), errors[-1:]),
      ("en", ["At least one language"]))
for c in ("de", "fr", "no"):
    be.toggle_kb_lang(c)
check("four on, in the rows' order", PR.read_conf().get("languages"), "de,en,fr,no")
n = len(errors)
be.toggle_kb_lang("es")
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
check("handwriting samples: no recognizer yet", be.kb_hw_n, None)

# -- the words command ----------------------------------------------------------------------
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
list) [ -e $S.erased ] || printf 'en\\tokay\\t96\\t20406\\nde\\tFeierabend\\t21\\t20407\\nen\\tteh\\t3\\t20400\\n' |
    while IFS= read -r l; do w=$(printf '%%s' "$l" | cut -f2); grep -qxF "$w" $S.gone || printf '%%s\\n' "$l"; done ;;
removed) cat $S.gone ;;
remove) echo "$3" >> $S.gone ;;
restore) awk -v w="$3" '$0 != w' $S.gone > $S.tmp; mv $S.tmp $S.gone ;;
erase) : > $S.gone; touch $S.erased ;;
*) exit 2 ;;
esac
""" % STATE)
os.chmod(STUB + "/glassine-predict", 0o755)
open(STATE + ".gone", "w").close()
PATH0 = os.environ["PATH"]
os.environ["PATH"] = STUB + ":" + PATH0
be.kb_read_words()
check("list: most used first, with the language", be.kb_words, [("okay", 96, "en"), ("Feierabend", 21, "de"), ("teh", 3, "en")])
be.kb_remove("teh")
check("remove: one call, no -l (every language)", open(STATE + ".log").read().splitlines()[-3], "words remove teh")
check("... the lists read again", ([w for w, _, _ in be.kb_words], be.kb_gone), (["okay", "Feierabend"], ["teh"]))
be.kb_restore("teh")
check("put back", ("words restore teh" in open(STATE + ".log").read(), be.kb_gone, len(be.kb_words)), (True, [], 3))
open(STATE + ".fail", "w").close()
n = len(errors)
be.kb_remove("okay")
check("a failing command: a short error, the page still has a reason", (errors[n:], be.kb_err),
      (["Could not remove “okay”"], "glassine-predict: words: the store can't be written"))
os.unlink(STATE + ".fail")
be.kb_remove("okay")
ui = A.UI(be, lambda: None)
ui.page = "keyboard"
ui.sheet_erase_learned()
ui.sheet.ok()
check("erase (through the sheet): both lists empty, no sheet left", (be.kb_words, be.kb_gone, ui.sheet), ([], [], None))
check("... one erase call", "words erase" in open(STATE + ".log").read(), True)

# the page renders in every state, the words search filters
def render(page, q=None):
    u = A.UI(be, lambda: None)
    u.page = page
    if q is not None:
        u.kbq, u.kb_typing = q, True
    s = cairo.ImageSurface(cairo.FORMAT_RGB24, 702, 884)
    A.Renderer(u).draw(cairo.Context(s), 702, 884, 1)
    u.L = A.Layout(702, 884)
    return [getattr(b, "label", "") for b in pages.build(u, be).blocks]


os.unlink(STATE + ".erased")
be.kb_read_words()
check("words page: the search keeps the matches", [x for x in render("kb-words", "EI") if x in ("okay", "Feierabend", "teh")], ["Feierabend"])
check("typing goes to the search field", (lambda u: (u.go("kb-words"), u.kb_focus(), u.type("o"), u.key("back"), u.type("k"),
                                                     u.typing(), u.kbq, u.pw)[5:])(A.UI(be, lambda: None)), (True, "k", ""))

# missing glassine-predict: the pages render, the actions say why
os.environ["PATH"] = T + "/empty"
be.kb_read_words()
check("missing: no words, a reason", (be.kb_words, be.kb_err), (None, "Word suggestions aren’t installed on this system."))
n = len(errors)
be.kb_restore("x")
check("missing: an action is a short error", errors[n:], ["Could not put back “x”"])
for p in ("keyboard", "keyboard#3", "kb-words", "kb-langs"):
    try:
        render(p)
        check("missing: %s renders" % p, True, True)
    except Exception as e:  # noqa: BLE001
        check("missing: %s renders" % p, repr(e), None)

# the real glassine-predict from this repository (when a compiler is there)
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
        PR.write_conf({"learning": "on", "languages": None})
        seed = "".join("learn\tl%d\ten\tnormal\t%s\n" % (i, w) for i, w in enumerate(["zonkword"] * 3 + ["tmrw"] * 2 + ["brb"]))
        subprocess.run([bindir + "/glassine-predict"], input=seed, capture_output=True, text=True, timeout=30)
        be.kb_read_words()
        check("real: learned words, most used first", [(w, n) for w, n, _ in be.kb_words or []], [("zonkword", 3), ("tmrw", 2), ("brb", 1)])
        be.kb_remove("tmrw")
        check("real: removed", ([w for w, _, _ in be.kb_words], be.kb_gone), (["zonkword", "brb"], ["tmrw"]))
        be.kb_restore("tmrw")
        check("real: put back (the counts were dropped)", ([w for w, _, _ in be.kb_words], be.kb_gone), (["zonkword", "brb"], []))
        be.kb_remove("brb")
        be.kb_erase()
        check("real: erased, the removed list too", (be.kb_words, be.kb_gone, be.kb_err), ([], [], None))
else:
    print("skip the real predictor: no compiler")
os.environ["PATH"] = PATH0
shutil.rmtree(T)
sys.exit(fail)
