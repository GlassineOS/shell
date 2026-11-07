#!/bin/sh
# Host test of glassine-shell-wl (spec 001 shell side: the bar's Home and screen
# buttons, Overview browse and move, divider mode, Home's launches) under stock
# sway headless (Alpine 3.24, pixman), in portrait and landscape and the three
# voices. glassine-swayd runs as in the session (this tree's; the last commit's
# when this tree's does not build: SWAYD=head forces it) behind a stand-in socket
# (proxy.py) that logs every verb the shell sends, so the checks read the verbs,
# not their effect. The shell's targets come from its GLASSINE_SHELL_HITS dump
# (hit.py); taps go through a virtual pointer (vptr.c), keys through wtype.
# Divider lists and Home's state are sent to the shell in glassine-swayd's
# contract format, the divider places taken from sway's tree (div.py).
# Shots: OUT/shots/VOICE-ORIENT-WHAT.png (grim); logs OUT/*.log. exit 1 on a
# failed check.
#   test.sh [OUT]      THEMES="white editorial stationery" ORIENTS="portrait landscape" SWAYD=live|head
set -eu
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../.." && pwd)
OUT=${1:-$D/out}
mkdir -p "$OUT"
rm -rf "$OUT/head" && mkdir -p "$OUT/head"
git -C "$R" archive HEAD src | tar -x -C "$OUT/head"
UI=$(sh "$R/test/ui.sh")          # GlassineOS/ui: the voices and the drawing (test/ui.sh)
docker build -q -t glassine-shelltest "$D" >/dev/null
docker run --rm -e THEMES="${THEMES:-white editorial stationery}" -e ORIENTS="${ORIENTS:-portrait landscape}" \
    -e SWAYD="${SWAYD:-live}" -v "$R":/r:ro -v "$UI":/ui:ro -v "$OUT":/out glassine-shelltest sh /r/test/shell/run.sh
