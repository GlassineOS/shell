"""Settings' favourites in Home's layout file (settings/homeconf.py): only the
favourites value changes, byte for byte; a newer or unreadable file is not
written; a missing one is created with version + favourites, the shell's old
list imported once; the lock is Home's. Exit 1 on a failed check.
   homeconf.py REPO"""
import fcntl
import os
import sys
import tempfile
import threading
import time

sys.path.insert(0, sys.argv[1] + "/rootfs/usr/lib/glassine")
from settings import homeconf as H  # noqa: E402

fail = 0


def check(what, got, want):
    global fail
    if got == want:
        print("ok   home.conf: " + what)
    else:
        print("FAIL home.conf: %s\n     got:  %r\n     want: %r" % (what, got, want))
        fail = 1


tmp = tempfile.mkdtemp()
os.environ["HOME"] = tmp
os.environ.pop("XDG_CONFIG_HOME", None)
P = os.path.join(tmp, ".config", "glassine", "home.conf")
check("the path", H.default_path(), P)
os.environ["XDG_CONFIG_HOME"] = tmp + "/xdg"
check("the path: $XDG_CONFIG_HOME", H.default_path(), tmp + "/xdg/glassine/home.conf")
os.environ["XDG_CONFIG_HOME"] = "relative"
check("the path: a relative $XDG_CONFIG_HOME is ignored (as Home)", H.default_path(), P)
del os.environ["XDG_CONFIG_HOME"]


def put(text):
    os.makedirs(os.path.dirname(P), exist_ok=True)
    with open(P, "w") as f:
        f.write(text)


def get():
    with open(P) as f:
        return f.read()


# Home's own layout (conf_format) with a hand-made comment and a widget's props
HOME = ('# glassine-home: the widgets in order. Pages follow from packing.\n'
        'version = 1\n'
        'favourites = ["foot", "org.gnome.Calendar"]   # mine\n'
        '\n[[widget]]\nuuid = "0b5c3c2e-6a2b-4f0e-9c1d-1f2e3d4c5b6a"\nwidget = "home/favourites"\nsize = 2\n'
        'props = { title = "favourites = [\\"x\\"]", n = 3 }\n'
        '\n[[widget]]\nuuid = "1b5c3c2e-6a2b-4f0e-9c1d-1f2e3d4c5b6a"\nwidget = "builtin/reading"\nsize = "wide"\n')
put(HOME)
check("read", H.read(P), (["foot", "org.gnome.Calendar"], True))
H.set_fav("koreader", True, P)
check("add: only the favourites value changes (the comment after it stays)", get(),
      HOME.replace('favourites = ["foot", "org.gnome.Calendar"]   # mine\n',
                   'favourites = ["foot", "org.gnome.Calendar", "koreader"]   # mine\n'))
H.set_fav("foot", False, P)
check("remove: the order kept", H.read(P)[0], ["org.gnome.Calendar", "koreader"])
check("... the widgets byte for byte", get().split("\n", 3)[3], HOME.split("\n", 3)[3])
before = os.stat(P).st_mtime_ns
H.set_fav("koreader", True, P)
check("no change: not written", os.stat(P).st_mtime_ns, before)

# a list over several lines, with comments and a ] inside a string
ML = 'version = 1\nfavourites = [\n  "a",  # one\n  "b]c",\n]\nother = 1\n'
put(ML)
H.set_fav("d", True, P)
check("a list over lines: replaced whole", get(), 'version = 1\nfavourites = ["a", "b]c", "d"]\nother = 1\n')

# another key's array over lines, with a line that starts with [ (a nested array: no table header), before
# and after favourites; a string over lines with a [ line; the comment after a list that spans lines
ML2 = ('version = 1\nmatrix = [\n  [1, 2],\n  [3, 4],\n]\nnote = """\n[not a table]\n"""\n'
       'favourites = [\n  "a",\n  "b",\n] # kept\nafter = [\n  [5],\n]\n[[widget]]\nwidget = "x"\n')
put(ML2)
H.set_fav("d", True, P)
check("a valid file with [ lines inside other keys' values: written, not refused", get(),
      ML2.replace('favourites = [\n  "a",\n  "b",\n] # kept\n', 'favourites = ["a", "b", "d"] # kept\n'))
put('version = 1\nmatrix = [\n  [1, 2],\n]\n[[widget]]\nwidget = "x"\n')
H.set_fav("d", True, P)
check("... no favourites: added after the last top-level key's version, not twice",
      get(), 'version = 1\nfavourites = ["d"]\nmatrix = [\n  [1, 2],\n]\n[[widget]]\nwidget = "x"\n')
put('version = 1\nfavourites = ["a"]# glued\n')
H.set_fav("b", True, P)
check("a comment right after the list stays", get(), 'version = 1\nfavourites = ["a", "b"]# glued\n')
put('version = 1\nfavourites = ["a"]   ')
H.set_fav("b", True, P)
check("blanks after the list, no newline at the end: kept as they are", get(), 'version = 1\nfavourites = ["a", "b"]   \n')

# no favourites yet: after version
put('# x\nversion = 1\n\n[[widget]]\nuuid = "0b5c3c2e-6a2b-4f0e-9c1d-1f2e3d4c5b6a"\nwidget = "home/all-apps"\nsize = 3\n')
H.set_fav("foot", True, P)
check("no favourites key: added after version", get(),
      '# x\nversion = 1\nfavourites = ["foot"]\n\n[[widget]]\nuuid = "0b5c3c2e-6a2b-4f0e-9c1d-1f2e3d4c5b6a"\nwidget = "home/all-apps"\nsize = 3\n')

# escapes as Home writes them
H.update(lambda fs: ['q"uote', "back\\slash", "tab\tnl\n", "bell\x07"], P)
check("strings escaped as Home's writer", get().split("\n")[2],
      'favourites = ["q\\"uote", "back\\\\slash", "tab\\tnl\\n", "bell\\u0007"]')
check("... and read back", H.read(P)[0], ['q"uote', "back\\slash", "tab\tnl\n", "bell\x07"])

# docs/home-conf.md (GlassineOS/home): favourites belongs at the top, before the first [[widget]]
# (after it, TOML makes it a key of that widget); `widget = []` and a file without widgets are valid
W = ('\n[[widget]]\nuuid = "0b5c3c2e-6a2b-4f0e-9c1d-1f2e3d4c5b6a"\nwidget = "home/all-apps"\nsize = 3\n'
     'favourites = ["hand"]\n')
put("version = 1\n" + W)
check("a favourites key inside a widget is that widget's: not read", H.read(P)[0], [])
H.set_fav("foot", True, P)
check("... the top-level one goes before the first [[widget]], the widget's left as it is", get(),
      'version = 1\nfavourites = ["foot"]\n' + W)
put("version = 1\nwidget = []\n")
H.set_fav("foot", True, P)
check("widget = [] (all removed): kept, favourites at the top", get(), 'version = 1\nfavourites = ["foot"]\nwidget = []\n')
put("version = 1")
H.set_fav("foot", True, P)
check("version only, no newline: favourites on a line of its own", get(), 'version = 1\nfavourites = ["foot"]\n')

# refused: newer, broken, no version
for what, text in (("a newer version", 'version = 2\nfavourites = ["a"]\n'),
                   ("a file it can't read", 'version = 1\nfavourites = ["a"\n'),
                   ("no version", 'favourites = ["a"]\n')):
    put(text)
    try:
        H.set_fav("x", True, P)
        got = "written"
    except H.Refused as e:
        got = "refused" if str(e) else "refused, no reason"
    check("%s: refused" % what, (got, get()), ("refused", text))

# missing: the old list imported, then created with version + favourites only
os.unlink(P)
old = os.path.join(tmp, ".config", "glassine-shell", "favourites")
os.makedirs(os.path.dirname(old), exist_ok=True)
with open(old, "w") as f:
    f.write("glassine-settings\n\nfoot\n")
check("missing: the shell's old list shows", H.read(P), (["glassine-settings", "foot"], False))
H.set_fav("koreader", True, P)
check("missing: created with version and favourites only", get(),
      'version = 1\nfavourites = ["glassine-settings", "foot", "koreader"]\n')
check("... mode 0600", oct(os.stat(P).st_mode & 0o777), "0o600")
os.unlink(P)
os.unlink(old)
check("missing, no old list: none", H.read(P), ([], False))

# the lock: a writer waits for Home's flock
put(HOME)
lk = os.open(P + ".lock", os.O_RDWR | os.O_CREAT, 0o600)
fcntl.flock(lk, fcntl.LOCK_EX)
done = []
th = threading.Thread(target=lambda: done.append(H.set_fav("z", True, P)))
th.start()
time.sleep(0.4)
check("the lock held: Settings waits", (done, H.read(P)[0]), ([], ["foot", "org.gnome.Calendar"]))
os.close(lk)
th.join(5)
check("the lock free: written", H.read(P)[0], ["foot", "org.gnome.Calendar", "z"])
check("no temporary file left", sorted(os.listdir(os.path.dirname(P))), ["home.conf", "home.conf.lock"])
sys.exit(fail)
