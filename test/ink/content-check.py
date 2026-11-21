"""content-check.py MODE APP_ERR: what the module said (its "glassine-ink: content KIND"
lines) after each step the app announced ("STEP NAME"), against what the rule gives."""
import sys

mode, err = sys.argv[1], open(sys.argv[2]).read()
got, cur = {}, None
for line in err.splitlines():
    if line.startswith("STEP "):
        cur = line.split()[1]
        got[cur] = []
    elif line.startswith("glassine-ink: content ") and cur:
        t = line.split()
        got[cur].append(t[2] + ("!" if line.endswith("(the app's word)") else ""))

want = {
    "idle-repaint": [], "tap": ["page"], "repaint-after-tap": [], "tap-without-repaint": [],
    "late-repaint": [], "press": [], "wiggle": [], "drag-1": ["motion"], "drag-2": ["motion"],
    "drop": [], "scroll": [], "own-page": ["page!"], "own-none": ["none!"], "tap-again": ["page"],
}
if mode != "asked":         # nothing by rule; the app's own word still counts
    want = {k: [x for x in v if x.endswith("!")] for k, v in want.items()}
fail = 0
for k, w in want.items():
    ok = got.get(k) == w
    print(("ok  " if ok else "FAIL"), "glassine: content %s: %s: %s" % (mode, k, " ".join(got.get(k, ["(step missing)"])) or "nothing said"),
          "" if ok else "(want %s)" % (" ".join(w) or "nothing"))
    fail |= not ok
sys.exit(fail)
