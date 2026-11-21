#!/bin/sh
# Host test of the size of the interface (spec 012) in the drawn renders: the shell's bar,
# quick settings, the power view, notifications, Overview and the shutdown screen under
# headless sway, and the sleep pill and the start picture, in every voice, portrait and
# landscape (run.sh; the wall clock stands still: timeshim.c).
#   1. Default on a 1404 x 1872 panel (factor 2) draws byte for byte what the shell drew
#      before the interface had a size: the same renders from the source and GlassineOS/ui
#      of base.ref (the last commits without it) and from this tree, compared file by file.
#   2. Each interface size step on that panel (factor 1.6, 1.8, 2, 2.4): the renders, the
#      layout filling the output's logical size, and every target of the bar and the popups
#      against the 7 mm minimum (check.py), the text minimums logged by the shell.
# base.ref: "SHELL-COMMIT UI-COMMIT", the last ones before the interface had a size. A
# deliberate change of how the shell draws at Default moves it to that change's own base.
# Shots: OUT/before, OUT/after (1.), OUT/steps (2.); fonts cached in OUT/fonts.
#   test.sh [OUT]      THEMES ORIENTS; STEPS="1.6 1.8 2 2.4" (empty: none)
set -eu
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../.." && pwd)
OUT=${1:-$D/out}
THEMES=${THEMES:-white editorial stationery} ORIENTS=${ORIENTS:-portrait landscape} STEPS=${STEPS-1.6 1.8 2 2.4}
mkdir -p "$OUT"
UI=$(sh "$R/test/ui.sh")
read -r BASE_SHELL BASE_UI < "$D/base.ref"
# the base: this repository's and GlassineOS/ui's trees at base.ref
rm -rf "$OUT/base" && mkdir -p "$OUT/base/shell" "$OUT/base/ui"
git -C "$R" cat-file -e "$BASE_SHELL^{commit}" 2>/dev/null || git -C "$R" fetch -q origin "$BASE_SHELL"
git -C "$R" archive "$BASE_SHELL" src rootfs fonts Makefile | tar -x -C "$OUT/base/shell"
git -C "$UI" cat-file -e "$BASE_UI^{commit}" 2>/dev/null || git -C "$UI" fetch -q origin "$BASE_UI"
git -C "$UI" archive "$BASE_UI" | tar -x -C "$OUT/base/ui"
IMG=$(docker build -q -t glassine-sizetest "$D")
run() {         # SIDE SHELL-TREE UI-TREE SCALES
    docker run --rm -e SIDE="$1" -e SCALES="$4" -e THEMES="$THEMES" -e ORIENTS="$ORIENTS" \
        -v "$2":/s:ro -v "$3":/ui:ro -v "$R/test":/t:ro -v "$OUT":/out "$IMG" sh /t/size/run.sh
}
fail=0
run before "$OUT/base/shell" "$OUT/base/ui" 2 || fail=1
run after "$R" "$UI" 2 || fail=1
n=0 same=0
for f in "$OUT"/before/shots/*.png; do
    b=$(basename "$f")
    n=$((n + 1))
    if cmp -s "$f" "$OUT/after/shots/$b"; then same=$((same + 1)); else echo "FAIL Default at factor 2: $b differs from the base"; fail=1; fi
done
[ "$(ls "$OUT/after/shots" | wc -l)" = "$n" ] || { echo "FAIL Default: $(ls "$OUT/after/shots" | wc -l) renders after, $n before"; fail=1; }
[ $n -gt 0 ] && [ $same = $n ] && echo "ok   Default at factor 2: all $n renders byte-identical to the base"
if [ -n "$STEPS" ]; then
    run steps "$R" "$UI" "$STEPS" || fail=1
    python3 "$D/check.py" "$OUT/steps" $STEPS || fail=1
fi
exit $fail
