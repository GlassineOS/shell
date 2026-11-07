#!/bin/sh
# End-to-end host test of glassine-shell-wl and glassine-swayd together (spec 001):
# both from this tree, on stock sway headless (Alpine 3.24, pixman, the shell
# test's container). Unlike test/shell, nothing stands in for glassine-swayd's
# side: the divider lists and Home's state the shell acts on are the ones
# glassine-swayd sends, the taps go to glassine-swayd's own handles, and the
# checks read what changed in sway's tree. Covered: divider mode with 2, 3
# and 4 windows (levels from swayd's list, the pair of 3 is level 2), Swap,
# swap within a half, Direction and the crossing toggle reaching swayd and
# changing the tree, Home shown and hidden by the bar's button (a foot window
# with Home's app_id), the empty screen when the last app closes and an app
# filling it, `launch ID new` on a new screen, and the launch argument
# reaching the app's argv (stub apps that record it) or being refused.
# Logs: OUT/*.log. exit 1 on a failed check.
#   test.sh [OUT]
set -eu
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../.." && pwd)
OUT=${1:-$D/out}
mkdir -p "$OUT"
UI=$(sh "$R/test/ui.sh")          # GlassineOS/ui (test/ui.sh)
# the image by its id: the tag is shared with other checkouts, which may rebuild it meanwhile
IMG=$(docker build -q -t glassine-shelltest "$R/test/shell")
timeout 600 docker run --rm -v "$R":/r:ro -v "$UI":/ui:ro -v "$OUT":/out "$IMG" \
    timeout 540 sh /r/test/e2e/run.sh
