#!/bin/sh
# (see test.sh) inside the container: the size of the interface (spec 012), glassine-swayd's
# part: each output's factor from its density and interface size set as its scale, the
# runtime file output-scale, a step that changes only its display, the full refresh asked
# only for the main output, a display gone and back, the fallback without any size in mm,
# the text scale of whole-scale-only apps and the cached scales the next session starts at.
# /src = src, /o = rootfs, /ui = GlassineOS/ui (ro)
set -u
make -s -C /ui install O=/tmp/ui-out PREFIX=/usr >/dev/null || exit 1
sh /src/wayland/build-swayd.sh /src /tmp/glassine-swayd || exit 1
SD=/tmp/glassine-swayd
export XDG_RUNTIME_DIR=/tmp/rt HOME=/tmp/home XDG_CONFIG_HOME=/tmp/home/.config
C=$XDG_CONFIG_HOME/glassine OS=$XDG_RUNTIME_DIR/glassine/output-scale CACHE=$HOME/.cache/glassine/sway-output-scale.conf
mkdir -p -m 700 $XDG_RUNTIME_DIR; mkdir -p $C
fail=0
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fail=1; fi; }

# ---- without sway: the factor of a built-in panel, the text scale ----------------------
printf 'panel_width=1872\npanel_height=1404\nrotation=ccw\n' > /tmp/nomm.conf
printf 'panel_width=1872\npanel_height=1404\npanel_width_mm=209\npanel_height_mm=157\nrotation=ccw\n' > /tmp/mm.conf
expect "a panel reporting 157 x 209 mm (8.943 px/mm): factor 2.0032, snapped to 2" \
    "$(GLASSINE_EPD_CONF=/tmp/nomm.conf $SD --size-test EPD-1 EPD-1 1404 1872 157 209)" "8.9427 reported 100 2.000000"
expect "no mm anywhere (the panel reports none, epd.conf has no panel_*_mm): the fallback 4.464, factor 1" \
    "$(GLASSINE_EPD_CONF=/tmp/nomm.conf $SD --size-test EPD-1 EPD-1 1404 1872 0 0)" "4.4643 fallback 100 1.000000"
expect "the panel reports none, epd.conf gives its mm: as reported (either way up)" \
    "$(GLASSINE_EPD_CONF=/tmp/mm.conf $SD --size-test EPD-1 EPD-1 1404 1872 0 0)" "8.9427 reported 100 2.000000"
expect "... not for another output (an external one reporting none): the fallback" \
    "$(GLASSINE_EPD_CONF=/tmp/mm.conf $SD --size-test HDMI-A-1 HDMI-A-1 1404 1872 0 0)" "4.4643 fallback 100 1.000000"
expect "the text scale of whole-scale-only apps: none at a whole factor, else the factor over the nearest whole one" \
    "$(for f in 2 1 1.6 1.8 2.4 0.966667 3; do printf '%s=%s ' $f "$($SD --dpi-scale $f)"; done)" "2= 1= 1.6=0.8000 1.8=0.9000 2.4=1.2000 0.966667=0.9667 3= "
mkdir -p ${CACHE%/*}
printf '# x\noutput EPD-1 scale 1.600000\noutput HDMI-A-1 scale 1.000000\n' > $CACHE
expect "... the session's start: the main output's (the cache's first) factor" "$($SD --dpi-scale)" "0.8000"
rm -f $CACHE
expect "... no cache (a first start): nothing" "$($SD --dpi-scale)" ""

# ---- under headless sway: two outputs ---------------------------------------------------
# HEADLESS-1 (the main output: no built-in display, the first) a panel of 8.943 px/mm,
# HEADLESS-2 a coarse monitor of 3.6 px/mm at Large (120 %); both kept densities (headless
# outputs report no size)
printf '8.943 HEADLESS-1\n3.6 HEADLESS-2\n' > $C/output-density
printf '# the interface sizes\n120 HEADLESS-2\n' > $C/ui-size
printf 'output HEADLESS-1 resolution 1404x1872 scale 2\noutput HEADLESS-2 resolution 1280x800\noutput HEADLESS-3 resolution 1280x800\n' > /tmp/c
cp /usr/bin/sway /tmp/sway
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -c /tmp/c >/tmp/sway-size.log 2>&1 &
for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1
mkfifo /run/glassine-epdd-desktop.ctl 2>/dev/null
(while :; do cat /run/glassine-epdd-desktop.ctl; done) > /tmp/ctl.log 2>/dev/null &
$SD -v 2>/tmp/swayd-size.log &
SWP=$!
sleep 1
swaymsg -q create_output; sleep 1.2
# "NAME SCALE" per active output; output-scale as "FACTOR DENSITY SOURCE KEY" (4 places)
scales() { swaymsg -t get_outputs -r | python3 -c 'import json,sys; print(" ".join("%s %.4f" % (o["name"], o["scale"]) for o in json.load(sys.stdin) if o["active"]))'; }
pub() { python3 -c '
import sys
for l in open(sys.argv[1]):
    f, d, s, k = l.split(None, 3)
    print("%.4f %.3f %s %s" % (float(f), float(d), s, k.strip()), end="; ")' $OS 2>/dev/null; }
fulls() { grep -c '^full$' /tmp/ctl.log; }
expect "two outputs of different densities and sizes: each its own scale (2: 8.943 at Default; 3.6 x 0.224 x 1.2 = 0.968 -> 116/120)" \
    "$(scales)" "HEADLESS-1 2.0000 HEADLESS-2 0.9667"
expect "output-scale: one line each, the main output first" "$(pub)" \
    "2.0000 8.943 measured HEADLESS-1; 0.9667 3.600 measured HEADLESS-2; "
expect "the scales cached for the next session, the main output first" "$(grep '^output' $CACHE | tr '\n' ';')" \
    "output HEADLESS-1 scale 2.000000;output HEADLESS-2 scale 0.966667;"
expect "nothing asked of the panel daemon at the start" "$(fulls)" "0"
# a step on HEADLESS-2 (Settings writes the file whole: beside it, renamed)
printf '90 HEADLESS-2\n' > $C/ui-size.tmp && mv $C/ui-size.tmp $C/ui-size; sleep 1.2
expect "a step changes only its display (3.6 x 0.224 x 0.9 = 0.726 -> 87/120)" "$(scales)" "HEADLESS-1 2.0000 HEADLESS-2 0.7250"
expect "... output-scale replaced" "$(pub)" "2.0000 8.943 measured HEADLESS-1; 0.7250 3.600 measured HEADLESS-2; "
expect "... no full refresh for a display that is not the main one" "$(fulls)" "0"
printf '90 HEADLESS-2\n80 HEADLESS-1\n' > $C/ui-size.tmp && mv $C/ui-size.tmp $C/ui-size; sleep 1.2
expect "a step on the main output (8.943 x 0.224 x 0.8 = 1.603 -> 1.6), the other kept" "$(scales)" "HEADLESS-1 1.6000 HEADLESS-2 0.7250"
expect "... one full refresh asked" "$(fulls)" "1"
expect "... GDK_DPI_SCALE for whole-scale apps (no D-Bus here: tried and logged)" "$(grep -c 'whole-scale apps started from now on: GDK_DPI_SCALE=0.8000' /tmp/swayd-size.log)" "1"
echo 120 > $C/text-size.tmp && mv $C/text-size.tmp $C/text-size; sleep 1.2
expect "the text size: one full refresh, no scale changes" "$(fulls) $(scales)" "2 HEADLESS-1 1.6000 HEADLESS-2 0.7250"
# a display gone and back: disabled (sway keeps the output, glassine-swayd forgets it), its scale
# changed meanwhile, enabled: its kept size again at once
swaymsg -q output HEADLESS-2 disable; sleep 1
expect "a display gone: output-scale without it" "$(pub)" "1.6000 8.943 measured HEADLESS-1; "
swaymsg -q output HEADLESS-2 scale 1; swaymsg -q output HEADLESS-2 enable; sleep 1.2
expect "... back: its kept size (90 %) at once" "$(scales)" "HEADLESS-1 1.6000 HEADLESS-2 0.7250"
# unplugged and connected again: headless sway names the new output anew (HEADLESS-3); a
# display's key stays the same, so its line is kept under the key (lines stay when their
# display goes, FR-014)
swaymsg -q output HEADLESS-2 unplug; sleep 1
expect "unplugged: output-scale without it" "$(pub)" "1.6000 8.943 measured HEADLESS-1; "
expect "... its line kept" "$(grep -c 'HEADLESS-2' $C/ui-size)" "1"
printf '90 HEADLESS-2\n80 HEADLESS-1\n120 HEADLESS-3\n' > $C/ui-size.tmp && mv $C/ui-size.tmp $C/ui-size; sleep 1
swaymsg -q create_output; sleep 1.2
N=$(swaymsg -t get_outputs -r | python3 -c 'import json,sys; print([o["name"] for o in json.load(sys.stdin)][-1])')
expect "a display connected with a kept size (no kept density: the fallback 4.464; 120 %: 1.2)" "$N $(scales | sed 's/.*\(HEADLESS-[0-9]* [0-9.]*\)$/\1/')" "HEADLESS-3 HEADLESS-3 1.2000"
expect "... output-scale: its density from the fallback" "$(pub)" "1.6000 8.943 measured HEADLESS-1; 1.2000 4.464 fallback HEADLESS-3; "
printf '90 HEADLESS-2\n80 HEADLESS-1\n' > $C/ui-size.tmp && mv $C/ui-size.tmp $C/ui-size; sleep 1.2
expect "... its line removed (Default): the fallback factor 1" "$(scales)" "HEADLESS-1 1.6000 HEADLESS-3 1.0000"
expect "no full refresh for any of it" "$(fulls)" "2"
# the next session: the cached scales in a sway config (its include; and one without the file)
printf 'output HEADLESS-1 scale 2\ninclude %s\n' $CACHE > /tmp/inc.conf
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -d -C -c /tmp/inc.conf > /tmp/inc.log 2>&1
expect "sway -C: the cached scales included, no errors" "$(grep -c "Loading config from $CACHE" /tmp/inc.log) $(grep -E '\[ERROR\]' /tmp/inc.log | head -2)" "1 "
printf 'output HEADLESS-1 scale 2\ninclude /tmp/nonexistent/sway-output-scale.conf\n' > /tmp/inc2.conf
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -d -C -c /tmp/inc2.conf > /tmp/inc2.log 2>&1; r=$?
expect "sway -C: no cache yet (a first start): the include is skipped, no error" "$r $(grep -E '\[ERROR\]' /tmp/inc2.log | head -2)" "0 "
kill $SWP 2>/dev/null
grep -iE "error" /tmp/swayd-size.log | head -5
[ $fail = 0 ] || { echo "--- swayd-size.log"; tail -30 /tmp/swayd-size.log; }
exit $fail
