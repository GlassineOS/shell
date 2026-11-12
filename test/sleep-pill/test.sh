#!/bin/sh
# Host test of glassine-sleep-pill (spec 004) in a container: the three voices and the four
# turns, each output checked as glassine-epdd-desktop's reader checks it (check.py), the
# screen size from a panel adapter file, and the failures that must leave no file.
# Shots: OUT/VOICE-ROT.png (the whole screen, the pill on white).
#   test.sh [OUT]
set -eu
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../.." && pwd)
OUT=${1:-$D/out}
mkdir -p "$OUT"
UI=$(sh "$R/test/ui.sh")          # GlassineOS/ui: the voices and the drawing (test/ui.sh)
IMG=$(docker build -q -t glassine-pilltest "$D")
docker run --rm -v "$R":/r:ro -v "$UI":/ui:ro -v "$OUT":/out "$IMG" sh /r/test/sleep-pill/run.sh
