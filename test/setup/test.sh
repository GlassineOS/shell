#!/bin/sh
# Host renders of glassine-setup (EinkSetup): every step in every voice,
# from the demo backend, into OUT/shots/VOICE-STEP.png (1404x1872, scale 2). No display needed.
# test.sh [OUT] THEMES="white …" STEPS="welcome …"
set -eu
D=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$D/../.." && pwd)
OUT=${1:-$D/out}
mkdir -p "$OUT"
docker build -q -t glassine-settingstest "$D/../settings" >/dev/null
docker run --rm -e THEMES="${THEMES:-white editorial stationery}" ${STEPS:+-e STEPS="$STEPS"} \
    -v "$REPO":/src:ro -v "$D":/t:ro -v "$OUT":/out glassine-settingstest sh /t/run.sh
