#!/bin/sh
# Host renders of glassine-settings: every page and sheet in every theme,
# from the demo backend (the design's sample data), into OUT/shots/THEME-PAGE.png
# (1404x1768: the panel below the bar, at scale 2). No display needed.
# test.sh [OUT] THEMES="white …" PAGES="wifi …" WINS="full column row quarter"
set -eu
D=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$D/../.." && pwd)
OUT=${1:-$D/out}
mkdir -p "$OUT"
UI=$(sh "$REPO/test/ui.sh")       # GlassineOS/ui: the voices and the drawing (test/ui.sh)
docker build -q -t glassine-settingstest "$D" >/dev/null
docker run --rm -e THEMES="${THEMES:-white editorial stationery}" ${PAGES:+-e PAGES="$PAGES"} ${WINS:+-e WINS="$WINS"} ${PKS:+-e PKS="$PKS"} \
    -v "$REPO":/src:ro -v "$UI":/ui:ro -v "$D":/t:ro -v "$OUT":/out glassine-settingstest sh /t/run.sh
