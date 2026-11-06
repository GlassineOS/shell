"""glassine-setup's three voices in the E-ink scheme (EinkSetup). The
values are GlassineOS/ui's voice data (glassine_ui.voices: scheme, setup);
this adds what the design computes in code: the eyebrow line, the step numerals' colours.
Only E-ink: one pen, no paper/night schemes."""
from glassine_ui import voices

VOICES = voices.NAMES
NAMES = {v: voices.title(v) for v in VOICES}
ROMAN = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII"]


def _resolve(v, il):
    """the few var(--il-x) the generator leaves inside nested step colours"""
    if isinstance(v, str) and v.startswith("var(--il-"):
        return il.get(v[9:-1].split(",")[0], v)
    return v


def voice(v):
    g = voices.voice(v)
    k, il = dict(g["setup"]), g["scheme"]
    for key in ("stepOn", "stepDone", "stepTodo", "sel", "segOn"):
        k[key] = {a: _resolve(b, il) for a, b in k[key].items()}
    k["il"] = il
    k["ink"], k["paper"], k["soft"], k["sheet"] = il["ink"], il["paper"], il["ink-soft"], il["sheet"]
    k["accent"], k["onAccent"], k["highlight"], k["pen"] = il["accent"], il["on-accent"], il["highlight"], il["pen"]
    k["voice"] = v
    return k


def eyebrow(v, i, name, n=8):
    """the line over a step's title: Step 2 of 8 · Look / § 2 · look / II. look (n: the
    number of steps asked this time)"""
    if v == "editorial":
        return "§ %d · %s" % (i, name.lower())
    if v == "stationery":
        return "%s. %s" % (ROMAN[i - 1], name.lower())
    return "Step %d of %d · %s" % (i, n, name)


THEMES = {v: voice(v) for v in VOICES}
