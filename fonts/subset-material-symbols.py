#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Vendor Material Symbols Outlined for the design boards (design/fonts/).

    subset-material-symbols.py           write the font, its stylesheet and NOTICE
    subset-material-symbols.py --check   fail when a board names an icon the font lacks

Run through tools/in-alpine (`make boards-icons`, `make boards-icons-check`): fontTools and
brotli are in that image. The source is the pinned google/material-design-icons commit below,
checked by sha256; the output is the same on every run.

What this changes in Google's font (Apache-2.0, which has no reserved font name; the licence
is copied beside the output, and the same list goes to NOTICE and into the font's description):
  - only the ligatures of icons the boards name are kept, with what they need: the characters
    a-z, A-Z, 0-9 and "_" and their glyphs, the icon glyphs and their filled forms; the other
    ligatures and all code points in the Private Use Area are removed;
  - the variable axes are reduced to what the boards ask Google Fonts for
    (opsz 24, wght 400, GRAD 0); FILL stays variable, 0 to 1;
  - the result is compressed as woff2.

An icon counts as named when a board has its ligature name as the whole text of an element in
this font, as a whole string literal, or as a literal prefix completed with a number
('filter_' + n), since the boards pick icons in their logic; a string that only happens to be
an icon name just keeps a few more glyphs.
"""
import hashlib
import html
import os
import re
import shutil
import sys
import urllib.request

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

COMMIT = "10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7"
BASE = "https://raw.githubusercontent.com/google/material-design-icons/" + COMMIT
FONT_URL = BASE + "/variablefont/MaterialSymbolsOutlined%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf"
FONT_SHA256 = "f7837ba8573d9a7a74931618ced8307bc81f96d49af296bf6c8052c772ed5282"
LICENSE_URL = BASE + "/LICENSE"
LICENSE_SHA256 = "58d1e17ffe5109a7ae296caafcadfdbe6a7d176f0bc4ab01e12a689b0499d8bd"

FAMILY = "Material Symbols Outlined"
AXES = {"opsz": 24, "wght": 400, "GRAD": 0}
BASENAME = "material-symbols-outlined"
# upper case too: the font maps both to one glyph, and a chip with text-transform: uppercase
# reaches the ligature in capitals
KEEP_CHARS = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ "
CHANGES = [
    "Subset to the icons the design boards name (their ligatures and filled forms); "
    "other ligatures and Private Use Area code points removed.",
    "Variable axes reduced to opsz 24, wght 400, GRAD 0; FILL stays variable.",
    "Compressed as woff2.",
]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DESIGN = os.path.join(ROOT, "design")
OUT = os.path.join(DESIGN, "fonts")
CACHE = os.path.join(ROOT, "out", "icon-font")

ELEMENT_TEXT = re.compile(r"Material Symbols Outlined[^>]*>\s*([a-z0-9_]+)\s*<")
STRING = re.compile(r"""(?:["'`]|&quot;|&#39;)([a-z0-9_]+)(?:["'`]|&quot;|&#39;)""")
# a prefix the logic completes with a number ('filter_' + n)
PREFIX = re.compile(r"""['`]([a-z0-9_]+_)['`]\s*\+""")


def fetch(url, sha256, name):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, name)
    if not os.path.exists(path):
        with urllib.request.urlopen(url) as r, open(path + ".part", "wb") as f:
            shutil.copyfileobj(r, f)
        os.replace(path + ".part", path)
    with open(path, "rb") as f:
        got = hashlib.sha256(f.read()).hexdigest()
    if got != sha256:
        os.unlink(path)
        sys.exit("%s: checksum mismatch: %s" % (name, got))
    return path


def ligature_subtables(font):
    for lookup in font["GSUB"].table.LookupList.Lookup:
        for st in lookup.SubTable:
            st = getattr(st, "ExtSubTable", st)
            if st.LookupType == 4:
                yield st


def ligatures(font):
    """{ligature name: glyph}. The font maps upper and lower case to the same glyphs."""
    chars = {}
    for code, glyph in font.getBestCmap().items():
        chars.setdefault(glyph, chr(code).lower())
    names = {}
    for st in ligature_subtables(font):
        for first, ligs in st.ligatures.items():
            for lig in ligs:
                names["".join(chars[g] for g in [first] + lig.Component)] = lig.LigGlyph
    return names, chars


def named_in_boards(known):
    used = set()
    for folder, dirs, files in os.walk(DESIGN):
        dirs[:] = [d for d in dirs if d not in ("fonts", "references")]
        for name in files:
            if name.endswith((".dc.html", ".json", ".js")):
                with open(os.path.join(folder, name), encoding="utf-8") as f:
                    text = f.read()
                used.update(m.group(1) for m in ELEMENT_TEXT.finditer(text))
                used.update(m.group(1) for m in STRING.finditer(text))
                for m in PREFIX.finditer(text):
                    used.update(k for k in known if k.startswith(m.group(1)) and k[len(m.group(1))].isdigit())
    return used & known


def keep_ligatures(font, used):
    names, chars = ligatures(font)
    for st in ligature_subtables(font):
        kept = {}
        for first, ligs in st.ligatures.items():
            left = [l for l in ligs if "".join(chars[g] for g in [first] + l.Component) in used]
            if left:
                kept[first] = left
        st.ligatures = kept


def build(source, used):
    font = TTFont(source, recalcTimestamp=False)
    keep_ligatures(font, used)
    opt = subset.Options()
    opt.layout_features = ["*"]
    opt.name_IDs = ["*"]
    opt.notdef_outline = True
    opt.glyph_names = False
    sub = subset.Subsetter(opt)
    sub.populate(unicodes=[ord(c) for c in KEEP_CHARS])
    sub.subset(font)
    font = instancer.instantiateVariableFont(font, AXES)
    for rec in list(font["name"].names):
        if rec.nameID == 10:
            font["name"].removeNames(nameID=10)
            break
    font["name"].setName("Modified from Google's Material Symbols Outlined: " + " ".join(CHANGES), 10, 3, 1, 0x409)
    font.flavor = "woff2"
    return font


def font_names(path):
    font = TTFont(path)
    return set(ligatures(font)[0])


def main():
    check = "--check" in sys.argv[1:]
    source = fetch(FONT_URL, FONT_SHA256, "MaterialSymbolsOutlined.ttf")
    names, _ = ligatures(TTFont(source, lazy=True))
    used = named_in_boards(set(names))
    target = os.path.join(OUT, BASENAME + ".woff2")
    if check:
        missing = sorted(used - font_names(target))
        if missing:
            sys.exit("%s lacks: %s (make boards-icons)" % (target, " ".join(missing)))
        print("icon font: %d icons named by the boards, all present" % len(used))
        return
    license_path = fetch(LICENSE_URL, LICENSE_SHA256, "LICENSE")
    os.makedirs(OUT, exist_ok=True)
    build(source, used).save(target, reorderTables=False)
    shutil.copyfile(license_path, os.path.join(OUT, BASENAME + ".LICENSE"))
    with open(os.path.join(OUT, BASENAME + ".NOTICE"), "w") as f:
        f.write("%s, from google/material-design-icons at commit %s,\n"
                "under the Apache License 2.0 (the file beside this one).\n\nChanged from the original:\n" % (FAMILY, COMMIT))
        f.writelines("- %s\n" % c for c in CHANGES)
        f.write("\nMade by fonts/subset-material-symbols.py.\n")
    with open(os.path.join(OUT, "fonts.css"), "w") as f:
        f.write("@font-face {\n  font-family: '%s';\n  font-style: normal;\n  font-weight: 400;\n  font-display: block;\n"
                "  src: url(%s.woff2) format('woff2');\n}\n" % (FAMILY, BASENAME))
    print("icon font: %d icons, %d bytes (source %d bytes)" % (len(used), os.path.getsize(target), os.path.getsize(source)))


if __name__ == "__main__":
    main()
