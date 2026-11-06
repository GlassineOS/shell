#!/bin/sh
# (see test.sh) inside the container: /r = the repository (ro), /out = results
set -u
T=/r/test/shell B=/tmp/b
mkdir -p $B /out/shots /out/fonts
rm -f /out/shots/*.png /out/*.log
[ "$(ls /out/fonts | wc -l)" -ge 6 ] || sh /r/fonts/fetch-fonts.sh /out/fonts >/dev/null 2>&1 ||
    echo "note: the voices' fonts could not be fetched (offline?): the shots use fallback fonts"
mkdir -p /usr/share/fonts/glassine && cp /out/fonts/*.ttf /usr/share/fonts/glassine/ 2>/dev/null; fc-cache -f >/dev/null
cp -a /r/rootfs/usr/share/icons/glassine-mono /usr/share/icons/
sh /r/src/wayland/build-shell-wl.sh /r/src $B/glassine-shell-wl || exit 1
if [ "$SWAYD" != head ] && sh /r/src/wayland/build-swayd.sh /r/src $B/glassine-swayd 2>/out/swayd-build.log; then
    echo "glassine-swayd: this tree's"
else
    echo "glassine-swayd: the last commit's (this tree's: ${SWAYD:-live}, see swayd-build.log)"
    sh /out/head/src/wayland/build-swayd.sh /out/head/src $B/glassine-swayd || exit 1
fi
X=/usr/share/wlr-protocols/unstable/wlr-virtual-pointer-unstable-v1.xml G=/tmp/g
mkdir -p $G
wayland-scanner client-header $X $G/wlr-virtual-pointer-unstable-v1-client-protocol.h &&
    wayland-scanner private-code $X $G/wlr-virtual-pointer-unstable-v1-protocol.c &&
    gcc -O2 -Wall -I$G -o $B/vptr $T/vptr.c $G/wlr-virtual-pointer-unstable-v1-protocol.c $(pkg-config --cflags --libs wayland-client) || exit 1
cp /usr/bin/sway $B/sway             # the package has cap_sys_nice: no exec in a container
mkfifo /run/glassine-epdd-desktop.ctl  # the hints of glassine-swayd and the shell, read and dropped
(while :; do cat /run/glassine-epdd-desktop.ctl; done) >/dev/null 2>&1 &
fail=0
for o in $ORIENTS; do ORIENT=$o sh $T/session.sh || fail=1; done
n=$(ls /out/shots/*.png 2>/dev/null | wc -l)
[ $fail = 0 ] && echo "shell test: all checks passed, $n shots in out/shots" || echo "shell test: FAILED (see above), $n shots in out/shots"
exit $fail
