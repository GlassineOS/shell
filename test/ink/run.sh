#!/bin/sh
# In the container (test.sh): /r this repository, /out the logs; $SWAY stock|glassine.
set -eu
cd /tmp
cp -r /r/src /tmp/src
sh /tmp/src/wayland/build-ink.sh /tmp/src /tmp/libglassine-ink.so
mkdir -p /tmp/gtk/3.0.0/modules /tmp/gtk/modules && cp /tmp/libglassine-ink.so /tmp/gtk/modules/
export XDG_RUNTIME_DIR=/tmp/rt; mkdir -p -m 700 $XDG_RUNTIME_DIR
fail=0
ok() { echo "ok   $SWAY: $*"; }
bad() { echo "FAIL $SWAY: $*"; fail=1; }

if [ "$SWAY" = glassine ]; then BIN=/usr/bin/sway-glassine; else BIN=/usr/bin/sway; fi
cp $BIN /tmp/sway                   # (file capabilities: no exec in a container)
printf 'output HEADLESS-1 resolution 1000x800 position 0 0\ndefault_border none\nfocus_on_window_activation none\n' > /tmp/c
# glassine-epdd-desktop's control FIFO, read by a stand-in: sway writes its ink report there
mkfifo /run/glassine-epdd-desktop.ctl
(while :; do cat /run/glassine-epdd-desktop.ctl; done) > /tmp/fifo.log 2>/dev/null &
FIFO=$!
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -c /tmp/c >/out/sway-$SWAY.log 2>&1 &
SW=$!
for i in $(seq 1 100); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1 GDK_BACKEND=wayland
# the module from /tmp/gtk/modules (GTK_PATH), the canvas = GtkDrawingArea here
GTK_PATH=/tmp/gtk GTK_MODULES=glassine-ink GLASSINE_INK_LOG=1 GLASSINE_INK_WIDGETS=GtkDrawingArea NO_AT_BRIDGE=1 \
    timeout 120 python3 /r/test/ink/canvas.py >/tmp/app.out 2>/tmp/app.err || true
cp /tmp/app.out /out/app-$SWAY.out; cp /tmp/app.err /out/app-$SWAY.err; cp /tmp/fifo.log /out/fifo-$SWAY.log
kill $SW 2>/dev/null || true; kill $FIFO 2>/dev/null || true

if grep -q 'Failed to load module' /tmp/app.err; then bad "the module does not load: $(grep 'Failed to load' /tmp/app.err)"; else ok "the module loads"; fi
grep -q '^DONE' /tmp/app.out && ok "the app ran through" || bad "the app did not finish"
if [ "$SWAY" = stock ]; then
    grep -q 'glassine-ink: no glassine_ink_manager_v1: off' /tmp/app.err && ok "no protocol: off" || bad "no protocol: not off"
    grep -q 'glassine-ink: areas' /tmp/app.err && bad "no protocol: areas sent" || ok "no protocol: nothing sent"
    exit $fail
fi
grep -q 'glassine-ink: on' /tmp/app.err && ok "protocol found: on" || bad "the module did not find glassine_ink_manager_v1"
grep -i 'protocol error' /tmp/app.err && bad "a protocol error" || ok "no protocol error"
python3 /r/test/ink/check.py /tmp/app.out /tmp/fifo.log /tmp/app.err || fail=1
exit $fail
