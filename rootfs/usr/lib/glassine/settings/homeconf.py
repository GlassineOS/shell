"""Favourite apps in Home's layout file (GlassineOS/home spec 001 FR-043).

~/.config/glassine/home.conf ($XDG_CONFIG_HOME/glassine/home.conf) is Home's:
a TOML subset with `version`, `favourites` (a list of desktop ids) and the
`[[widget]]` tables. Settings changes only the `favourites` value and keeps
every other byte (a comment after it on its last line too); it writes a temporary file
beside it, syncs and renames it, holding an exclusive flock on home.conf.lock (Home takes the same lock). A file
with a newer version than VERSION, or one Settings can't read, is not written.
Without the file: ~/.config/glassine-shell/favourites (the shell's old list),
imported once as Home does; a write then creates the file with only `version`
and `favourites`.

No GLib here: the host tests use it as it is.
"""
import fcntl
import os
import re
import tempfile
import tomllib

VERSION = 1                       # the newest layout version Settings knows (Home's CONF_VERSION)


def default_path():
    x = os.environ.get("XDG_CONFIG_HOME", "")
    base = x if x.startswith("/") else os.path.join(os.environ.get("HOME", "/"), ".config")
    return os.path.join(base, "glassine", "home.conf")


def old_path():
    """the shell's old list (one id a line), as Home imports it"""
    return os.path.join(os.environ.get("HOME", "/"), ".config", "glassine-shell", "favourites")


class Refused(Exception):
    """the file is there but must not be written (a newer version, unreadable)"""


def toml_str(s):
    """a basic string as Home's writer puts it (toml.c put_str)"""
    out = ['"']
    for ch in s:
        c = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif c < 0x20 or c == 0x7f:
            out.append("\\u%04x" % c)
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def fav_line(favs):
    return "favourites = [%s]\n" % ", ".join(toml_str(f) for f in favs)


def _parse(text):
    """-> the parsed table; Refused for a file Settings can't read or a newer one"""
    try:
        t = tomllib.loads(text)
    except tomllib.TOMLDecodeError as e:
        raise Refused("Home's layout file can't be read (%s); not changing it" % e)
    v = t.get("version")
    if not isinstance(v, int) or isinstance(v, bool) or v < 1:
        raise Refused("Home's layout file has no version; not changing it")
    if v > VERSION:
        raise Refused("Home's layout file was written by a newer Home (version %d); not changing it" % v)
    f = t.get("favourites", [])
    if not isinstance(f, list) or not all(isinstance(x, str) for x in f):
        raise Refused("Home's layout file: favourites is not a list of app ids; not changing it")
    return t


_KEY = re.compile(r'[ \t]*(?:favourites|"favourites"|\'favourites\')[ \t]*=')
_VERSION = re.compile(r'[ \t]*(?:version|"version"|\'version\')[ \t]*=')
_BARE = r'(?:[A-Za-z0-9_-]+|"[^"\n]*"|\'[^\'\n]*\')'
_ANYKEY = re.compile(r'[ \t]*%s(?:[ \t]*\.[ \t]*%s)*[ \t]*=' % (_BARE, _BARE))   # any key of the top table


def _value_span(text, i):
    """(the end of the value starting at text[i] (after `=`), the end of its line, the
    newline included): strings, comments and nested brackets are skipped, an array may
    span lines. What follows the value on its last line (blanks, a comment) is after the
    first end and before the second"""
    depth = 0
    n = len(text)
    stop = None                  # where the value stopped: its line's comment or newline
    while i < n:
        c = text[i]
        if c == "#":
            if depth == 0:
                stop = i
                break
            while i < n and text[i] != "\n":
                i += 1
            continue
        if text.startswith('"""', i) or text.startswith("'''", i):      # a string over lines
            q = text[i:i + 3]
            i += 3
            while i < n and not text.startswith(q, i):
                i += 2 if q[0] == '"' and text[i] == "\\" else 1
            i += 3
            continue
        if c == '"' or c == "'":
            q = c
            i += 1
            while i < n and text[i] != q and text[i] != "\n":
                i += 2 if q == '"' and text[i] == "\\" else 1
            i += 1
            continue
        if c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
        elif c == "\n" and depth == 0:
            stop = i
            break
        i += 1
    i = min(i, n)
    if stop is None:
        stop = i
    vend = stop
    while vend > 0 and text[vend - 1] in " \t":
        vend -= 1
    e = text.find("\n", i)
    return vend, (n if e < 0 else e + 1)


def _value_end(text, i):
    return _value_span(text, i)[1]


def _top_lines(text):
    """(start, end) of each key line of the top-level table (before the first table header);
    the continuation lines of any key's array or string are left out (they start inside the
    value, and may start with a [ that is no table header)"""
    i, n = 0, len(text)
    while i < n:
        e = text.find("\n", i)
        e = n if e < 0 else e + 1
        line = text[i:e]
        if line.lstrip().startswith("["):
            return
        yield i, e
        m = _ANYKEY.match(line)
        if m:
            e = _value_end(text, i + m.end())
        i = e


def replace_favourites(text, favs):
    """text with only its favourites value changed (added after version when missing): what
    follows the value on its last line, a comment, stays"""
    line = fav_line(favs)
    after_version = None
    for s, e in _top_lines(text):
        m = _KEY.match(text[s:e])
        if m:
            vend, lend = _value_span(text, s + m.end())
            tail = text[vend:]
            return text[:s] + line.rstrip("\n") + tail + ("" if tail.endswith("\n") else "\n")
        m = _VERSION.match(text[s:e])
        if m:
            after_version = _value_end(text, s + m.end())
    if after_version is None:
        raise Refused("Home's layout file has no version line; not changing it")
    pre = text[:after_version]
    if not pre.endswith("\n"):
        pre += "\n"
    return pre + line + text[after_version:]


def read(path=None):
    """-> (favourites, the file is there); Refused is not raised: an unreadable
    file shows as it is in Home (its own parse decides), favourites empty"""
    path = path or default_path()
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        return read_old(), False
    except (OSError, UnicodeDecodeError):
        return [], True
    try:
        return list(_parse(text).get("favourites", [])), True
    except Refused:
        return [], True


def read_old():
    try:
        with open(old_path(), encoding="utf-8") as f:
            return [l.strip() for l in f if l.strip()]
    except (OSError, UnicodeDecodeError):
        return []


def update(change, path=None):
    """change(favourites) -> the new list; applied to the file under its lock.
    -> the list written. Raises Refused (not written) or OSError."""
    path = path or default_path()
    d = os.path.dirname(path)
    os.makedirs(d, mode=0o700, exist_ok=True)
    lk = os.open(path + ".lock", os.O_RDWR | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW, 0o600)
    try:
        fcntl.flock(lk, fcntl.LOCK_EX)
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except FileNotFoundError:
            text = None
        except UnicodeDecodeError:
            raise Refused("Home's layout file is not UTF-8 text; not changing it")
        if text is None:
            favs = change(read_old())
            new = "version = %d\n" % VERSION + fav_line(favs)
        else:
            favs = change(list(_parse(text).get("favourites", [])))
            new = replace_favourites(text, favs)
            if new == text:
                return favs
            t = _parse(new)                     # what Home will read: only favourites changed
            old = _parse(text)
            old["favourites"] = favs
            t.setdefault("favourites", [])
            if t != old:
                raise Refused("Home's layout file: the change would touch more than favourites; not changing it")
        fd, tmp = tempfile.mkstemp(dir=d, prefix=".home.conf.")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(new)
                f.flush()
                os.fsync(f.fileno())
            os.replace(tmp, path)
        except BaseException:
            try:
                os.unlink(tmp)
            except OSError:
                pass
            raise
        dfd = os.open(d, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(dfd)
        finally:
            os.close(dfd)
        return favs
    finally:
        os.close(lk)            # the lock goes with the descriptor


def set_fav(aid, on, path=None):
    """aid in or out of the favourites (a new one goes last) -> the list written"""
    return update(lambda fs: [f for f in fs if f != aid] + ([aid] if on else []), path)
