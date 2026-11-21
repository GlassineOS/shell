#!/bin/sh
# (see test.sh) in the container: /s a shell tree (src, rootfs, fonts), /ui a GlassineOS/ui tree,
# /t this tree's test/ (the harness), /out the results: /out/$SIDE/shots, logs, hit dumps.
# SIDE (a name), SCALES ("2" or several), ORIENTS, THEMES.
set -u
B=/tmp/b O=/out/$SIDE
rm -rf $O
# the results belong to whoever owns /out (the host's user, not the container's root)
trap 'chown -R $(stat -c %u:%g /out) /out' EXIT
mkdir -p $B $O/shots $O/hits /out/fonts
[ "$(ls /out/fonts | wc -l)" -ge 6 ] || sh /s/fonts/fetch-fonts.sh /out/fonts >/dev/null 2>&1 ||
    echo "note: the voices' fonts could not be fetched (offline?): the shots use fallback fonts"
mkdir -p /usr/share/fonts/glassine && cp /out/fonts/*.ttf /usr/share/fonts/glassine/ 2>/dev/null; fc-cache -f >/dev/null
cp -a /s/rootfs/usr/share/icons/glassine-mono /usr/share/icons/
make -s -C /ui install O=/tmp/ui-out PREFIX=/usr >/dev/null || exit 1
sh /s/src/wayland/build-shell.sh /s/src $B/glassine-shell || exit 1
make -s -C /s OUT=$B $B/glassine-sleep-pill || exit 1
gcc -O2 -shared -fPIC -o $B/timeshim.so /t/size/timeshim.c || exit 1
X=/usr/share/wlr-protocols/unstable/wlr-virtual-pointer-unstable-v1.xml G=/tmp/g
mkdir -p $G
wayland-scanner client-header $X $G/wlr-virtual-pointer-unstable-v1-client-protocol.h &&
    wayland-scanner private-code $X $G/wlr-virtual-pointer-unstable-v1-protocol.c &&
    gcc -O2 -Wall -I$G -o $B/vptr /t/shell/vptr.c $G/wlr-virtual-pointer-unstable-v1-protocol.c $(pkg-config --cflags --libs wayland-client) || exit 1
cp /usr/bin/sway $B/sway
export GLASSINE_TEST_TIME=1768473000       # (timeshim: the wall clock stands still)
fail=0

# the sleep pill and the start picture at the panel's Default (its adapter with the size in mm;
# a program without spec 012 reads none of the mm keys)
T=/tmp/pill; mkdir -p $T
printf 'panel_width=1872\npanel_height=1404\npanel_width_mm=209\npanel_height_mm=157\nrotation=ccw\n' > $T/epd.conf
python3 - $T <<'PY'
import sys
t = sys.argv[1]
w, h = 1404, 1872
px = bytearray(((x * 7 + y * 3 + (x * y) % 13) & 255) for y in range(h) for x in range(w))
open(f"{t}/up.pnm", "wb").write(f"P5\n{w} {h}\n255\n".encode() + px)
PY
for th in $THEMES; do
    for r in 0 90 180 270; do
        GLASSINE_EPD_CONF=$T/epd.conf GLASSINE_USER_RUN=/tmp/none $B/glassine-sleep-pill -t $th -r $r -s "Since 20:11" -p $O/shots/pill-$th-$r.png $T/p.pam >/dev/null || fail=1
    done
    GLASSINE_EPD_CONF=$T/epd.conf GLASSINE_USER_RUN=/tmp/none $B/glassine-sleep-pill -m resume -t $th -r 0 -b $T/up.pnm -p $O/shots/start-$th.png $T/s.pnm >/dev/null || fail=1
done

stop() {        # PID...: each one ended (sway can outlive a plain TERM while clients go)
    for p; do kill $p 2>/dev/null; done
    for i in $(seq 1 30); do alive=0; for p; do kill -0 $p 2>/dev/null && alive=1; done; [ $alive = 0 ] && break; sleep 0.1; done
    for p; do kill -9 $p 2>/dev/null; done; wait "$@" 2>/dev/null
}
session() {     # ORIENT SCALE
    o=$1 f=$2
    if [ "$o" = landscape ]; then RES=1872x1404; else RES=1404x1872; fi
    # (sway takes SWAYSOCK as its own socket's path: the last session's must go)
    unset SWAYSOCK WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
    export HOME=/tmp/home-$o-$f XDG_RUNTIME_DIR=/tmp/rt-$o-$f
    rm -rf $HOME $XDG_RUNTIME_DIR; mkdir -p -m 700 $HOME/.config/glassine-shell $HOME/.config/glassine $XDG_RUNTIME_DIR
    # the panel's density (spec 011's kept one: a headless output reports no size): 8.943 px/mm,
    # whose Default factor is 2 (a shell without spec 012 reads none of it)
    printf '8.943 HEADLESS-1\n' > $HOME/.config/glassine/output-density
    H=$O/hits/$o-$f; mkdir -p $H; L=$O/shell-$o-$f.log
    printf 'output HEADLESS-1 resolution %s scale %s bg #ffffff solid_color\ndefault_border none\ngaps inner 20\ngaps outer -20\nsmart_gaps on\n' $RES $f > /tmp/sway.conf
    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 $B/sway -c /tmp/sway.conf >$O/sway-$o-$f.log 2>&1 &
    SP=$!
    for i in $(seq 1 100); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
    ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 || { echo "FAIL $o $f: sway did not start"; tail -5 $O/sway-$o-$f.log; fail=1; return; }
    export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1
    export DBUS_SESSION_BUS_ADDRESS=$(dbus-daemon --session --fork --print-address)
    set -- $(swaymsg -t get_outputs -r | python3 -c 'import json,sys; r=json.load(sys.stdin)[0]["rect"]; print(r["width"], r["height"])')
    LW=$1 LH=$2
    LD_PRELOAD=$B/timeshim.so GLASSINE_SHELL_HITS=$H $B/glassine-shell -v >$L 2>&1 &
    SH=$!
    mkfifo $XDG_RUNTIME_DIR/vptr
    $B/vptr $XDG_RUNTIME_DIR/vptr $LW $LH 2>/dev/null &
    VP=$!
    sleep 1.5
    swaymsg -q "workspace number 1; exec foot -a notes sleep 600; exec foot -a files sleep 600"; sleep 1.5
    swaymsg -q "workspace number 2; exec foot -a mail sleep 600"; sleep 1.5
    swaymsg -q "workspace number 1"; sleep 0.5
    for n in "Calendar|Meeting at 11|The room changed: B 2.04" "Mail|2 new messages|From the reading group"; do
        IFS='|' read -r a t b <<N
$n
N
        gdbus call --session --dest org.freedesktop.Notifications --object-path /org/freedesktop/Notifications \
            --method org.freedesktop.Notifications.Notify "$a" 0 "" "$t" "$b" '[]' '{}' 0 >/dev/null || echo "note: notification not sent"
    done
    sleep 1
    shot() { sleep 1.2; grim -o HEADLESS-1 $O/shots/$th-$o-$f-$1.png; for s in bar pop; do [ -e $H/$s ] && cp $H/$s $H/$th-$1.$s && { [ ! -e $H/$s.tap ] || cp $H/$s.tap $H/$th-$1.$s.tap; }; done; }
    for th in $THEMES; do
        $B/glassine-shell theme $th; sleep 1.5
        shot bar
        $B/glassine-shell toggle quick; shot quick; $B/glassine-shell close; sleep 0.5
        $B/glassine-shell toggle power; shot power; $B/glassine-shell close; sleep 0.5
        $B/glassine-shell toggle notices; shot notices; $B/glassine-shell close; sleep 0.5
        p=$(python3 /t/shell/hit.py $H/bar screen) && { echo "tap $p" > $XDG_RUNTIME_DIR/vptr; shot overview; $B/glassine-shell close; sleep 0.5; }
    done
    $B/glassine-shell theme white; sleep 1
    $B/glassine-shell shutdown-screen "Restarting…"; sleep 2
    th=white; shot shutdown
    grep 'glassine-shell: size\|wayland: output' $L | head -3 > $O/size-$o-$f.txt
    stop $SH $VP $SP
    pkill dbus-daemon 2>/dev/null
}
for o in $ORIENTS; do for f in $SCALES; do session $o $f; done; done

# two outputs of different scales: the shell follows its output's factor (it starts again at
# a new one, as glassine-shell-run does it) and, its output gone, goes to the other one at
# that one's factor; a launch placeholder moved to the other output draws again at its factor
moves() {
    unset SWAYSOCK WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS
    export HOME=/tmp/home-moves XDG_RUNTIME_DIR=/tmp/rt-moves
    rm -rf $HOME $XDG_RUNTIME_DIR; mkdir -p -m 700 $HOME/.config/glassine $HOME/.local/share/applications $XDG_RUNTIME_DIR
    printf '[Desktop Entry]\nType=Application\nName=Quiet\nExec=sleep 20\nIcon=utilities-terminal-symbolic\n' > $HOME/.local/share/applications/quiet.desktop
    L=$O/moves.log; : > $L
    printf 'output HEADLESS-1 resolution 1404x1872 scale 2 bg #ffffff solid_color\noutput HEADLESS-2 resolution 1280x800 position 702 0 scale 1.25 bg #ffffff solid_color\n' > /tmp/sway.conf
    WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 $B/sway -c /tmp/sway.conf >$O/sway-moves.log 2>&1 &
    SP=$!
    for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
    export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1
    swaymsg -q create_output; sleep 0.5
    (while :; do LD_PRELOAD=$B/timeshim.so $B/glassine-shell -v >>$L 2>&1; [ $? = 75 ] || break; done) &
    RUNP=$!
    sleep 2
    seen() { if grep -q "$2" $L; then echo "ok   $1"; else echo "FAIL $1 (no \"$2\" in moves.log)"; fail=1; fi; }
    seen "two outputs: the shell on the first at its factor" "output HEADLESS-1 1404x1872, logical 702x936, factor 2.0000"
    grim -o HEADLESS-1 $O/shots/moves-1-factor2.png
    swaymsg -q focus output HEADLESS-2
    $B/glassine-shell launch quiet; sleep 1.5
    swaymsg -q '[app_id=glassine-placeholder] move to output HEADLESS-1'; sleep 1.5
    seen "a placeholder moved to the other output: drawn again at its factor (2)" "placeholder Quiet: scale 2.0000"
    swaymsg -q '[app_id=glassine-placeholder] move to output HEADLESS-2'; sleep 1.5
    t=$(grep -o 'placeholder Quiet: scale [0-9.]*' $L | tail -1)
    [ "$t" = "placeholder Quiet: scale 1.2500" ] && echo "ok   ... and back: at 1.25 again" || { echo "FAIL ... and back: $t"; fail=1; }
    swaymsg -q '[app_id=glassine-placeholder] kill'
    swaymsg -q output HEADLESS-1 scale 1.6; sleep 2.5
    seen "its output at another factor: the shell starts again" "the output now 877x1170 at 1.6000 (was 702x936 at 2.0000): restart"
    seen "... and draws at it, for the new logical size" "output HEADLESS-1 1404x1872, logical 877x1170, factor 1.6000"
    grim -o HEADLESS-1 $O/shots/moves-2-factor1.6.png
    swaymsg -q output HEADLESS-1 unplug; sleep 2.5
    seen "its output gone: the shell starts again on the other one" "the output HEADLESS-1 is gone: restart"
    seen "... at that one's factor" "output HEADLESS-2 1280x800, logical 1024x640, factor 1.2500"
    grim -o HEADLESS-2 $O/shots/moves-3-other-output-1.25.png
    kill $RUNP 2>/dev/null; pkill -f "$B/glassine-shell" 2>/dev/null; stop $RUNP $SP
}
[ "$SIDE" = steps ] && moves
echo "$SIDE: $(ls $O/shots | wc -l) shots"
exit $fail
