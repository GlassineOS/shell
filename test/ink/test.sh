#!/bin/sh
# Host test of the GTK 3 module glassine-ink (spec 003), headless sway in containers.
# 1. Stock sway (no glassine_ink_manager_v1): the module loads (no "Failed to load
#    module") and stays off.
# 2. sway-glassine from the package repository (aarch64 only; on another host it runs
#    under qemu-user when binfmt has qemu-aarch64, else it is skipped with a note):
#    a GTK 3 app with a toolbar and a canvas in a scrolled window (canvas.py). sway's
#    ink report, read from a FIFO standing in for glassine-epdd-desktop's control
#    FIFO, must hold the app's own areas (S 1) and exactly the visible canvas, before
#    and after a second window narrows it; the second window (no canvas) gets none.
# Logs: OUT/*.log.  INK_GLASSINE=0 skips tier 2, =1 requires it.
#   test.sh [OUT]
set -eu
D=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$D/../.." && pwd)
OUT=${1:-$D/out}
mkdir -p "$OUT"
fail=0
IMG=$(docker build -q -t glassine-inktest-stock "$D")
docker run --rm -e SWAY=stock -v "$R":/r:ro -v "$OUT":/out "$IMG" sh /r/test/ink/run.sh || fail=1

case $(uname -m) in
aarch64|arm64) PLAT= ;;
*) PLAT=--platform=linux/arm64
   if [ ! -e /proc/sys/fs/binfmt_misc/qemu-aarch64 ]; then
       [ "${INK_GLASSINE:-}" != 1 ] || { echo "FAIL sway-glassine: no arm64 host and no qemu-aarch64"; exit 1; }
       INK_GLASSINE=0; echo "skip sway-glassine: not an arm64 host and no qemu-aarch64 binfmt"
   fi ;;
esac
if [ "${INK_GLASSINE:-}" != 0 ]; then
    IMG=$(docker build -q $PLAT --build-arg SWAY=glassine -t glassine-inktest-glassine "$D")
    docker run --rm $PLAT -e SWAY=glassine -v "$R":/r:ro -v "$OUT":/out "$IMG" sh /r/test/ink/run.sh || fail=1
fi
exit $fail
