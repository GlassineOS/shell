"""The three voices of glassine-settings in the E-ink scheme (claude.ai
design "E-ink Shell": EinkSettingsCard). The values are GlassineOS/ui's voice data
(glassine_ui.voices: scheme, shell, card); this file only maps them
onto the keys the blocks draw with, plus the widget constants the design writes inline
in its markup (toggle 64x36, pills 48 high, radius 999px, 1.5px ink keylines...).
Only E-ink: no colour palettes, no paper/night schemes, the pen choice is moot. CSS-like strings are interpreted by glassine_ui.paint.
"""
import re

from glassine_ui import voices

NAMES = {v: voices.title(v) for v in voices.NAMES}


def _voice(v):
    g = voices.voice(v)
    il, s, k = g["scheme"], g["shell"], g["card"]
    ink, paper, soft = il["ink"], il["paper"], il["ink-soft"]
    acc, on_acc, dim, sheet = il["accent"], il["on-accent"], il["dim"], il["sheet"]
    line = "1.5px solid " + ink                     # the markup's keyline (border:1.5px solid ink)
    t = dict(s)                                     # the shell's values (theme tiles' miniatures)
    t.update({
        "k": k, "il": il, "inline": g["inline"],
        "screenBg": paper, "ink": ink, "paper": paper, "sheet": sheet, "dim": dim,
        "accent": acc, "onAccent": on_acc, "highlight": il["highlight"],
        "sub": k["subC"],
        "fontUI": k["F"],
        # type: the card's KS (row, sub, val, big, head, title, tab)
        "rowFont": k["row"], "dateFont": k["sub"], "valFont": k["val"], "bigFont": k["big"],
        "lblFont": k["head"], "lblVar": k["headVar"], "lblCase": k["headCase"], "lblLs": k["headLs"],
        "lblCol": ink,                               # section heads are ink (no sub colour)
        "onLblFont": k["onLbl"],
        "headFont": k["title"], "headVar": k["titleVar"], "headLs": k["titleLs"],
        "headH": k["headH"],
        "tabFont": k["tab"], "tabLs": k["tabLs"], "tabR": k["tabR"],
        "tabOnBg": k["tabOnBg"], "tabOnFg": k["tabOnFg"], "tabOnSh": k["tabOnSh"], "tabOffSh": k["tabSh"],
        "segFs": k["segFs"], "zoneFs": k["zoneFs"],
        # rules: sep between rows, rule under a section head (and the header's line)
        "sep": k["sep"], "sepSoft": k["rule"], "headRule": "1px solid " + ink, "sheetTop": k["sheetTop"],
        # the folder card
        "cardBg": paper, "cardBorder": line, "cardR": k["cardR"], "cardSh": k["cardSh"],
        "emboss": k["frame"], "frameCol": il["rule"],
        # pills (back, Refresh, a row's button, the pager): 48 high, outlined
        "btnBg": paper, "btnSh": "inset 0 0 0 1.5px " + ink + ("" if k["pillSh"] == "none" else "," + k["pillSh"]),
        "btnR": "999px", "btnFont": k["row"],
        # the primary action (a sheet's b1): filled with the accent
        "pwrBg": acc, "pwrFg": on_acc, "pwrSh": "inset 0 0 0 1.5px " + acc, "pwrR": "999px",
        # toggle: track 64x36 radius 999 1.5px ink, knob 25 with a 1px ink ring
        "swW": "64px", "swH": "36px", "swR": "999px", "swK": "25px", "swKR": "50%",
        "swOnBg": acc, "swOnSh": "inset 0 0 0 1.5px " + ink + ("" if k["tglSh"] == "none" else "," + k["tglSh"]),
        "swOffBg": paper, "swOffSh": "inset 0 0 0 1.5px " + ink + ("" if k["tglSh"] == "none" else "," + k["tglSh"]),
        "swKOn": on_acc, "swKOff": ink, "swKBd": "1px solid " + ink, "swKSh": "none", "swKOffBd": "1px solid " + ink,
        # segmented control / trays: a 1.5px outline, segments 48 high
        "trayBg": paper, "traySh": "inset 0 0 0 1.5px " + ink, "trayR": k["segR"],
        "segR": k["segOnR"], "segFont": re.sub(r"[\d.]+px", k["segFs"], k["row"], count=1),
        "segOnBg": k["segOnBg"], "segOnFg": k["segOnFg"], "segOnBd": k["segOnBd"], "segOnSh": k["segOnSh"],
        # slider: track 10 high (top 17) radius 6, fill 5 high (top 19.5), knob 6x30 ink bar
        "grooveTop": "17px", "grooveH": "10px", "grooveR": "6px", "grooveBg": paper, "grooveSh": "inset 0 0 0 1.5px " + ink,
        "fillTop": "19.5px", "fillH": "5px",
        "knobTop": "7px", "knobML": "-3px", "knobW": "6px", "knobH": "30px", "knobR": "3px", "knobBg": ink, "knobBd": "none",
        "radioSh": "none",
        # the keyboard on the card (sheet ground, keys 50 high)
        "keyR": k["segOnR"], "keyFont": k["row"], "keyFnFont": k["row"], "keyBg": paper, "kbdBg": sheet,
        "fieldFont": k["row"], "codeFont": k["big"],
        "pageFont": k["sub"], "nBodyFont": k["sub"], "nameFont": s.get("nameFont", k["row"]),
        "monoFont": "400 13px/21px 'IBM Plex Mono',monospace",
        "ws": s.get("ws", ["1", "2", "3"]), "home": "Settings",
    })
    return t


THEMES = {v: _voice(v) for v in voices.NAMES}
OFF = THEMES["white"]["dim"]          # disabled controls (the design's DIM, var(--il-dim))
