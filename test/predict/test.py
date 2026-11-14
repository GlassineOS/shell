# SPDX-License-Identifier: GPL-2.0-or-later
# (see test.sh) test.py PREDICTOR MODELDIR
import os
import random
import select
import shutil
import stat
import subprocess
import sys
import tempfile

BIN, MODELS = sys.argv[1], sys.argv[2]
failed = 0


def check(name, got, want):
    global failed
    if got == want:
        print("ok   " + name)
    else:
        failed = 1
        print("FAIL " + name)
        print("     got:  %r" % (got,))
        print("     want: %r" % (want,))


class Home:
    """A fresh home: data, config, and the environment pointing at them."""

    def __init__(self):
        self.root = tempfile.mkdtemp(prefix="predict-")
        self.data = os.path.join(self.root, "data")
        self.conf = os.path.join(self.root, "conf")
        self.store = os.path.join(self.data, "glassine", "predict")
        self.env = dict(os.environ, XDG_DATA_HOME=self.data, XDG_CONFIG_HOME=self.conf,
                        HOME=self.root, GLASSINE_PREDICT_MODELS=MODELS)

    def tree(self):
        """every file and directory under the root, with mode and contents"""
        out = []
        for d, dirs, files in os.walk(self.root):
            for n in sorted(dirs + files):
                p = os.path.join(d, n)
                st = os.lstat(p)
                body = open(p, "rb").read() if stat.S_ISREG(st.st_mode) else None
                out.append((os.path.relpath(p, self.root), oct(st.st_mode), body))
        return sorted(out)

    def words(self, *args, umask=None):
        pre = (lambda: os.umask(umask)) if umask is not None else None
        r = subprocess.run([BIN, "words", *args], env=self.env, capture_output=True, text=True,
                           timeout=10, preexec_fn=pre)
        return r.returncode, r.stdout, r.stderr


class Predictor:
    def __init__(self, home, umask=None):
        pre = (lambda: os.umask(umask)) if umask is not None else None
        self.p = subprocess.Popen([BIN], env=home.env, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, preexec_fn=pre)
        self.buf = b""

    def send(self, data):
        if isinstance(data, str):
            data = data.encode()
        self.p.stdin.write(data)
        self.p.stdin.flush()

    def line(self, timeout=5.0):
        while b"\n" not in self.buf:
            r, _, _ = select.select([self.p.stdout], [], [], timeout)
            if not r:
                return None
            chunk = os.read(self.p.stdout.fileno(), 65536)
            if not chunk:
                return None
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode()

    def lines(self, n):
        return [self.line() for _ in range(n)]

    def ask(self, req):
        self.send(req + "\n")
        return self.line()

    def close(self):
        self.p.stdin.close()
        rest = self.p.stdout.read()
        err = self.p.stderr.read().decode()
        code = self.p.wait(timeout=10)
        return code, rest.decode(), err


def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")


def predict(rid, before, word="", field="normal", lang="en"):
    return "\t".join(["predict", rid, lang, field, esc(before), esc(word)])


def words_of(reply):
    return reply.split("\t")[2:] if reply else None


# ---- ranking
h = Home()
p = Predictor(h)
check("hello: the contract version", p.ask("hello\th1\t1"), "hello\th1\t1")
check("next words after 'I would like to '", words_of(p.ask(predict("a", "I would like to ")))[:2], ["read", "go"])
check("completion of 'tom' after 'See you '", words_of(p.ask(predict("b", "See you ", "tom"))), ["tomorrow"])
check("the word being typed may be in the text before (no WORD)", words_of(p.ask(predict("c", "See you tom"))), ["tomorrow"])
check("a capital typed gives capitals", words_of(p.ask(predict("d", "", "Wea"))), ["Weather"])
check("all capitals typed give all capitals", words_of(p.ask(predict("e", "", "WEA"))), ["WEATHER"])
check("names keep their capital", words_of(p.ask(predict("f", "We went to ", "lon"))), ["London"])
check("sentence start with auto_capitalization capitalizes",
      words_of(p.ask(predict("g", "It is raining. ", "", "normal,auto_capitalization")))[0][0].isupper(), True)
check("at most three, best first", len(words_of(p.ask(predict("h", "The ")))), 3)
check("unknown language: no model, no words", p.ask(predict("i", "The ", lang="xx")), "predict\ti")
check("a number purpose gets no words", p.ask(predict("j", "The ", field="number")), "predict\tj")
long_ctx = ("blah " * 2000) + "Thank you "
check("a 10 KB context: only its end counts", words_of(p.ask(predict("k", long_ctx)))[0], "for")
check("text after a break (digits) has no context", words_of(p.ask(predict("l", "42 ", "th")))[0] in ("the", "this", "there", "they", "think", "things", "thank", "thanks"), True)
code, rest, err = p.close()
check("ranking: clean exit, nothing on stderr", (code, rest, err), (0, "", ""))

# ---- the contract: malformed lines, stale requests, random bytes
h = Home()
p = Predictor(h)
bad = [
    ("no tabs", "nonsense", "error\t-\tunknown-verb"),
    ("a verb alone", "hello", "error\t-\tbad-line"),
    ("unknown verb", "frobnicate\tq1\ten", "error\tq1\tunknown-verb"),
    ("bad id", "predict\tbad id!\ten\tnormal\tx\t", "error\t-\tbad-id"),
    ("too few fields", "predict\tq2\ten\tnormal\tx", "error\tq2\tbad-line"),
    ("too many fields", "predict\tq3\ten\tnormal\tx\ty\tz", "error\tq3\tbad-line"),
    ("bad escape", "predict\tq4\ten\tnormal\tx\\q\t", "error\tq4\tbad-escape"),
    ("bad language", "predict\tq5\tEN!\tnormal\tx\t", "error\tq5\tbad-lang"),
    ("bad field", "predict\tq6\ten\tnormal,Sensitive Data\tx\t", "error\tq6\tbad-field"),
    ("empty field", "predict\tq7\ten\t\tx\t", "error\tq7\tbad-field"),
    ("remove: not a word", "remove\tq8\ten\tno way", "error\tq8\tbad-word"),
    ("empty line", "", "error\t-\tbad-line"),
]
for name, req, want in bad:
    check("malformed: " + name, p.ask(req), want)
check("escapes: a tab and newline in the context are text",
      words_of(p.ask(predict("q9", "x\tThank\nThank you "))), words_of(p.ask(predict("q10", "Thank you "))))
p.send(b"predict\tq11\ten\tnormal\t" + b"a" * 40000 + b"\t\n")
check("an overlong line: one error, no desync", [p.line(), p.ask("hello\tq12\t1")], ["error\t-\ttoo-long", "hello\tq12\t1"])
p.send(b"predict\tq13\ten\tnormal\tx\x00y\t\n")
check("a NUL byte: an error", p.line(), "error\t-\tbad-line")
# several predictions in one write: only the newest is answered, every request gets one reply
p.send(predict("s1", "Thank you ") + "\n" + predict("s2", "See you ") + "\n" +
       "learn\tl1\ten\tnormal\tzebrafish\n" + predict("s3", "I would like to ") + "\n")
got = p.lines(4)
check("stale predictions are dropped, in order, the newest answered",
      [g.split("\t")[0] + " " + g.split("\t")[1] for g in got], ["drop s1", "drop s2", "learn l1", "predict s3"])
check("the newest gets its words", words_of(got[3])[:2], ["read", "go"])
random.seed(6)
for i in range(300):
    blob = bytes(random.randrange(256) for _ in range(random.randrange(1, 200)))
    p.send(blob.replace(b"\n", b"") + b"\n")
    ans = p.line(2)
    if ans is None or not ans.startswith(("error", "predict", "drop", "learn", "remove", "restore", "hello")):
        check("random bytes %d" % i, ans, "one reply")
        break
p.send(b"predict\tz\t" + bytes(range(1, 256)).replace(b"\n", b"") + b"\n")
p.line()
check("after random bytes it still answers", p.ask("hello\tfin\t1"), "hello\tfin\t1")
code, rest, err = p.close()
check("contract: clean exit", code, 0)
check("contract: nothing typed reaches stderr", "Thank" in err or "zebra" in err, False)

# ---- the private-field rule: one check per purpose and hint
for field in ["password", "pin", "normal,sensitive_data", "normal,hidden_text", "alpha,completion,sensitive_data",
              "name,hidden_text"]:
    h = Home()
    os.makedirs(h.conf)
    before = h.tree()
    p = Predictor(h)
    replies = [p.ask(predict("p1", "my secret is ", "hunter", field)),
               p.ask("learn\tp2\ten\t%s\thunterzwei" % field),
               p.ask("learn\tp3\ten\t%s\thunterzwei" % field)]
    code, rest, err = p.close()
    check("private %s: no words, nothing learned" % field, replies,
          ["predict\tp1", "learn\tp2\tskip", "learn\tp3\tskip"])
    check("private %s: nothing written, nothing on stderr" % field, (h.tree() == before, err, code), (True, "", 0))

# terminal: words, but no learning; an unknown hint never learns; learning off
for field, why in [("terminal", "terminal"), ("normal,some_future_hint", "unknown hint"), ("email", "email"),
                   ("url", "url")]:
    h = Home()
    p = Predictor(h)
    r = p.ask("learn\tt1\ten\t%s\tzonkword" % field)
    p.close()
    check("%s: not learned" % why, (r, os.path.exists(h.store)), ("learn\tt1\tskip", False))
h = Home()
p = Predictor(h)
check("terminal: still suggests", words_of(p.ask(predict("t2", "Thank you ", "", "terminal")))[0], "for")
p.close()
h = Home()
os.makedirs(os.path.join(h.conf, "glassine"))
open(os.path.join(h.conf, "glassine", "predict.conf"), "w").write("# settings\nlearning = off\n")
p = Predictor(h)
r = p.ask("learn\tt3\ten\tnormal\tzonkword")
p.close()
check("learning off: not learned", (r, os.path.exists(h.store)), ("learn\tt3\tskip", False))

# email and URL fields follow addresses = off | suggest | learn (default suggest)
for value in [None, "bogus", "off", "suggest", "learn"]:
    for field in ["email", "url"]:
        h = Home()
        os.makedirs(os.path.join(h.conf, "glassine"))
        if value is not None:
            open(os.path.join(h.conf, "glassine", "predict.conf"), "w").write("addresses = %s\n" % value)
        p = Predictor(h)
        words = words_of(p.ask(predict("a1", "Thank you ", "", field)))
        r = p.ask("learn\ta2\ten\t%s\tzonkword" % field)
        p.close()
        mode = {"off": "off", "learn": "learn"}.get(value, "suggest")
        name = "addresses %s, %s" % (value or "missing", field)
        check(name + ": words", bool(words), mode != "off")
        check(name + ": learning", (r, os.path.exists(h.store)),
              ("learn\ta2\tok", True) if mode == "learn" else ("learn\ta2\tskip", False))
h = Home()
os.makedirs(os.path.join(h.conf, "glassine"))
conf = os.path.join(h.conf, "glassine", "predict.conf")
open(conf, "w").write("addresses = learn\n")
p = Predictor(h)
check("addresses learn: private hints still win",
      [p.ask(predict("a3", "Thank you ", "", "email,sensitive_data")), p.ask("learn\ta4\ten\temail,hidden_text\tzonkword"),
       p.ask("learn\ta5\ten\turl,some_future_hint\tzonkword")], ["predict\ta3", "learn\ta4\tskip", "learn\ta5\tskip"])
check("addresses learn: number fields still get nothing",
      [p.ask(predict("a6", "Thank you ", "", "number")), p.ask("learn\ta7\ten\tdate\tzonkword")], ["predict\ta6", "learn\ta7\tskip"])
open(conf, "w").write("addresses = off\n")
check("the setting is read at the next request", p.ask(predict("a8", "Thank you ", "", "email")), "predict\ta8")
p.close()

# ---- learning
h = Home()
p = Predictor(h, umask=0)
check("an unknown word is not suggested after one use",
      [p.ask("learn\tl1\ten\tnormal\tGlassine"), words_of(p.ask(predict("l2", "I like ", "gla")))], ["learn\tl1\tok", []])
p.ask("learn\tl3\ten\tnormal\tGlassine")
check("after two uses it is, in the person's spelling", words_of(p.ask(predict("l4", "I like ", "gla"))), ["Glassine"])
check("a single letter is not learned", p.ask("learn\tl5\ten\tnormal\tq"), "learn\tl5\tskip")
check("other languages don't see it", words_of(p.ask(predict("l6", "", "gla", lang="de"))), [])
check("before learning, 't' doesn't give 'tea' first", words_of(p.ask(predict("l6b", "Xyzzy ", "t")))[0] != "tea", True)
for i in range(10):
    p.ask("learn\tm%d\ten\tnormal\ttea" % i)
check("learned use lifts a known word", words_of(p.ask(predict("l7", "Xyzzy ", "t")))[0], "tea")
code, rest, err = p.close()
check("learning: written on exit", (code, err), (0, ""))
mode = lambda f: oct(stat.S_IMODE(os.stat(f).st_mode))
check("store: the file is 0600 under umask 000", mode(os.path.join(h.store, "words")), "0o600")
check("store: its directory is 0700", mode(h.store), "0o700")
check("store: the lock is 0600", mode(os.path.join(h.store, "lock")), "0o600")
rc, out, err = h.words("list")
check("words list: most used first, with counts",
      [l.split("\t")[:3] for l in out.splitlines()], [["en", "tea", "10"], ["en", "Glassine", "2"]])
check("words list -s: search", h.words("list", "-s", "LASS")[1].split("\t")[1], "Glassine")
check("words list -l: other language empty", h.words("list", "-l", "de")[1], "")

# a second predictor sees the file; learned words wait (debounced) but count at once
p = Predictor(h)
check("a new predictor knows the learned word", words_of(p.ask(predict("n1", "", "gla"))), ["Glassine"])

# ---- removing words
check("remove a learned word through the contract", p.ask("remove\tr1\ten\tglassine"), "remove\tr1\tok")
check("removed: never suggested at once", words_of(p.ask(predict("r2", "", "gla"))), [])
check("removed: not learned again", p.ask("learn\tr3\ten\tnormal\tGlassine"), "learn\tr3\tskip")
check("remove a base word", [p.ask("remove\tr4\ten\ttomorrow"), words_of(p.ask(predict("r5", "See you ", "tom")))],
      ["remove\tr4\tok", []])
check("typing a removed word stays possible: other words still come",
      words_of(p.ask(predict("r6", "See you ")))[0], "soon")
rc, out, err = h.words("removed")
check("words removed: the list", sorted(out.splitlines()), ["glassine", "tomorrow"])
check("words list: the removed word is gone", "Glassine" in h.words("list")[1], False)
check("restore through the contract", [p.ask("restore\tr7\ten\ttomorrow"), words_of(p.ask(predict("r8", "See you ", "tom")))],
      ["restore\tr7\tok", ["tomorrow"]])
check("restore of a word not removed", p.ask("restore\tr9\ten\tbanana"), "restore\tr9\tnone")
# the words command changes what the running predictor suggests
check("words remove (all languages)", h.words("remove", "Thank")[0], 0)
check("the running predictor follows the words command", words_of(p.ask(predict("r10", "", "Tha")))[0], "Thanks")
check("words restore", h.words("restore", "thank")[0], 0)
check("restored: suggested again", words_of(p.ask(predict("r11", "", "Tha")))[0], "Thank")
h.words("remove", "-l", "en", "sea")
# erase: learned words AND the removed list; pending words of a running predictor are dropped
p.ask("learn\te1\ten\tnormal\tpendingword")
p.ask("learn\te2\ten\tnormal\tpendingword")
check("words erase", h.words("erase")[0], 0)
check("after erase the predictor forgets the pending words", words_of(p.ask(predict("e3", "", "pendingw"))), [])
code, rest, err = p.close()
check("erase: nothing learned is left, the removed list is empty", (h.words("list")[1], h.words("removed")[1]), ("", ""))
check("erase: the base word is suggested again", "sea" in words_of(Predictor(h).ask(predict("e4", "In the summer we go to the ", "s"))), True)
check("words: usage errors", [h.words("bogus")[0], h.words("remove")[0], h.words("remove", "two words", )[0],
                              h.words("list", "-l", "*")[0]], [2, 2, 2, 2])

# learned words are written after a pause, and on SIGTERM
import time
h = Home()
p = Predictor(h)
p.ask("learn\tw1\ten\tnormal\tpausedword")
check("not written while typing", os.path.exists(os.path.join(h.store, "words")), False)
time.sleep(2.6)
check("written after 2 s without a request", "pausedword" in h.words("list")[1], True)
p.ask("learn\tw2\ten\tnormal\tsignalword")
p.p.terminate()
check("SIGTERM: exits 0 and writes", (p.p.wait(timeout=10), "signalword" in h.words("list")[1]), (0, True))

# many learned words: the most used one with a beginning still comes first; base words keep
# the model's spelling; a capital at a sentence start doesn't replace a lowercase spelling
h = Home()
p = Predictor(h)
random.seed(3)
batch = []
for i in range(300):
    w = "s" + "".join(random.choice("abcdefghijklmnopqrstuvwxy") for _ in range(5))
    batch += ["learn\tc%d\ten\tnormal\t%s" % (i, w)] * 2
batch += ["learn\tz%d\ten\tnormal\tszzzlast" % i for i in range(50)]
p.send("\n".join(batch) + "\n")
p.lines(len(batch))
check("300 learned words with 's': the most used is suggested", words_of(p.ask(predict("c1", "Xyzzy ", "s")))[0], "szzzlast")
p.ask("learn\tc2\ten\tnormal\tThe")
check("a base word keeps the model's spelling", words_of(p.ask(predict("c3", "I saw ", "th")))[0], "the")
for w in ["kindle", "Kindle"]:
    p.ask("learn\tc4\ten\tnormal\t" + w)
check("a capital at a sentence start keeps the learned lowercase", words_of(p.ask(predict("c5", "", "kind"))), ["kindle"])
p.ask("learn\tc6\ten\tnormal\tKINDLE")
check("... another spelling replaces it", words_of(p.ask(predict("c7", "", "kind"))), ["KINDLE"])
p.close()

# replies keep the order of the requests, errors included
h = Home()
p = Predictor(h)
p.send(predict("o1", "Thank you ") + "\n" + "x" * 40000 + "\nhello\to2\t1\nab\x00c\nhello\to3\t1\n")
check("errors keep their place", [l.split("\t")[0] + " " + l.split("\t")[2] for l in p.lines(5)],
      ["predict for", "error too-long", "hello 1", "error bad-line", "hello 1"])
p.close()

# a store that can't be written: no busy loop, still answering
h = Home()
open(h.data, "w").write("a file where the data directory should be")
p = Predictor(h)
p.ask("learn\tu1\ten\tnormal\tunwritable")
time.sleep(3)
cpu = sum(int(x) for x in open("/proc/%d/stat" % p.p.pid).read().rsplit(")", 1)[1].split()[11:13])
check("an unwritable store: no busy loop (CPU ticks)", cpu < 30, True)
check("... and it still answers", p.ask("hello\tu2\t1"), "hello\tu2\t1")
code, rest, err = p.close()
check("... exits 0", code, 0)

# ---- contract version 2: several languages
def pairs(reply):
    f = reply.split("\t")[2:] if reply else []
    return list(zip(f[0::2], f[1::2]))


h = Home()
p = Predictor(h)
check("v2: hello 2", p.ask("hello\th\t2"), "hello\th\t2")
check("v2: a newer version asked gets 2", p.ask("hello\th\t7"), "hello\th\t2")
check("v2: English context, English words", pairs(p.ask(predict("m1", "I would like to ", lang="en,de")))[:2],
      [("read", "en"), ("go", "en")])
check("v2: German context, German words", pairs(p.ask(predict("m2", "Ich möchte ein ", lang="en,de")))[0], ("Buch", "de"))
check("v2: a German context with an English word first in the list", pairs(p.ask(predict("m3", "Danke für ", lang="en,de")))[0][1], "de")
check("v2: a completion only German has", pairs(p.ask(predict("m4", "", "Hau", lang="en,de")))[0], ("Haus", "de"))
check("v2: both languages at a sentence start", sorted(set(l for _, l in pairs(p.ask(predict("m5", "", "", lang="en,de"))))),
      ["de", "en"])
for bad in ["en,en", "en,de,fr,it,es", "en,EN!", "", "en,"]:
    check("v2: bad language list %r" % bad, p.ask(predict("m6", "x ", lang=bad)), "error\tm6\tbad-lang")
check("v2: a language without a model adds nothing", pairs(p.ask(predict("m7", "Ich möchte ein ", lang="xx,de")))[0], ("Buch", "de"))
# which language learns
check("learn: a picked suggestion learns in its language",
      p.ask("learn\tq1\ten,de\tnormal\tI would like to \tBuch\tde"), "learn\tq1\tok\tde")
check("learn: a typed word only German knows", p.ask("learn\tq2\ten,de\tnormal\t\tZeitung\t"), "learn\tq2\tok\tde")
check("learn: a typed word only English knows", p.ask("learn\tq3\ten,de\tnormal\tIch \tweather\t"), "learn\tq3\tok\ten")
check("learn: an unknown word after German", p.ask("learn\tq4\ten,de\tnormal\tIch möchte ein \tFlugzeugträger\t"),
      "learn\tq4\tok\tde")
check("learn: an unknown word after English", p.ask("learn\tq5\ten,de\tnormal\tI would like to \tsnorkel\t"),
      "learn\tq5\tok\ten")
check("learn: an unknown word without context: the first language",
      p.ask("learn\tq6\tde,en\tnormal\t\tquokka\t"), "learn\tq6\tok\tde")
check("learn: a word both know follows the context",
      [p.ask("learn\tq7\ten,de\tnormal\tWir fahren \tin\t"), p.ask("learn\tq8\ten,de\tnormal\tWe went \tin\t")],
      ["learn\tq7\tok\tde", "learn\tq8\tok\ten"])
check("learn: a picked language not active is ignored", p.ask("learn\tq9\ten,de\tnormal\t\tquokkas\tfr"), "learn\tq9\tok\ten")
check("learn: the v1 form still works (no context: first language)", p.ask("learn\tq10\ten,de\tnormal\tbilby"), "learn\tq10\tok\ten")
# removing a word removes it in every language
check("remove: the German word is gone in German too",
      [p.ask("remove\tr1\ten\tHaus"), "Haus" in [w for w, _ in pairs(p.ask(predict("r2", "", "Hau", lang="en,de")))]],
      ["remove\tr1\tok", False])
check("restore: back everywhere", [p.ask("restore\tr3\tde\thaus"), pairs(p.ask(predict("r4", "", "Hau", lang="en,de")))[0]],
      ["restore\tr3\tok", ("Haus", "de")])
p.close()
check("words list -l: the language per word", sorted(l.split("\t")[1] for l in h.words("list", "-l", "de")[1].splitlines()),
      ["Buch", "Flugzeugträger", "Zeitung", "in", "quokka"])
check("words remove -l is gone (removal is for every language)", h.words("remove", "-l", "de", "Buch")[0], 2)

# the languages setting wins over the request's list
h = Home()
os.makedirs(os.path.join(h.conf, "glassine"))
conf = os.path.join(h.conf, "glassine", "predict.conf")
open(conf, "w").write("languages = de\n")
p = Predictor(h)
p.ask("hello\th\t2")
check("languages in predict.conf win over the request", pairs(p.ask(predict("s1", "I would like to ", lang="en")))[0][1], "de")
check("... and an empty request list uses them", pairs(p.ask(predict("s2", "Ich möchte ein ", lang="")))[0], ("Buch", "de"))
open(conf, "w").write("languages = en, de\n")
check("... read at the next request, spaces allowed", pairs(p.ask(predict("s3", "", "Hau", lang="")))[0], ("Haus", "de"))
open(conf, "w").write("languages = en,!!\n")
check("an invalid setting falls back to the request", pairs(p.ask(predict("s4", "I would like to ", lang="en")))[0][1], "en")
check("... and with no request list it's an error", p.ask(predict("s5", "x ", lang="")), "error\ts5\tbad-lang")
p.close()

# a store this version can't read is left alone
h = Home()
os.makedirs(h.store)
open(os.path.join(h.store, "words"), "w").write("glassine-words 2 0\nsomething new\n")
p = Predictor(h)
r = [p.ask("learn\tv1\ten\tnormal\tnewword"), p.ask("remove\tv2\ten\tnewword")]
p.close()
check("a newer store version: no learning, no overwrite", (r, open(os.path.join(h.store, "words")).read()),
      (["learn\tv1\tskip", "error\tv2\tstore"], "glassine-words 2 0\nsomething new\n"))

# a broken model: no words from it, no crash
broken = tempfile.mkdtemp()
data = open(os.path.join(MODELS, "en.gpm"), "rb").read()
open(os.path.join(broken, "en.gpm"), "wb").write(data[: len(data) // 2])
h = Home()
h.env["GLASSINE_PREDICT_MODELS"] = broken
p = Predictor(h)
r = p.ask(predict("b1", "Thank you "))
code, rest, err = p.close()
check("a truncated model: empty answer, one line on stderr", (r, code, err.count("\n")), ("predict\tb1", 0, 1))
shutil.rmtree(broken)

sys.exit(failed)
