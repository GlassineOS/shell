#!/bin/sh
# Host renders of glassine-setup (EinkSetup): every step in every voice,
# from the demo backend, into OUT/shots/VOICE-STEP.png (1404x1872, scale 2). No display needed.
# test.sh [OUT] THEMES="white …" STEPS="welcome …"
set -eu
D=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$D/../.." && pwd)
OUT=${1:-$D/out}
mkdir -p "$OUT"
UI=$(sh "$REPO/test/ui.sh")       # GlassineOS/ui: the voices and the drawing (test/ui.sh)
# the image by its id: the tag is shared with other checkouts, which may rebuild it meanwhile
IMG=$(docker build -q -t glassine-settingstest "$D/../settings")
docker run --rm -e THEMES="${THEMES:-white editorial stationery}" ${STEPS:+-e STEPS="$STEPS"} \
    -v "$REPO":/src:ro -v "$UI":/ui:ro -v "$D":/t:ro -v "$OUT":/out "$IMG" sh /t/run.sh
