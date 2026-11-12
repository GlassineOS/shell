#!/bin/sh
# (see test.sh) inside the container: /src = src, /t = this dir
set -u
gcc -O2 -Wall -Werror -o /tmp/rotd /src/glassine-rotd.c -lm || exit 1
fail=0
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fail=1; fi; }
G=9.80665

# ---- the logic, by replay: screen plane (right, down) in g per phase of seconds at 4 Hz
# rotd.conf maps right = -y, down = x (each record: x y z)
printf '# a test device\nSENSOR=test-accel\nOUTPUT = HEADLESS-1\nAXES="-y x"  \nHOLD_MS=500\n' > /tmp/rotd.conf
cap() { python3 -c '
import struct, sys
g, out = 9.80665, open(sys.argv[1], "wb")
for ph in sys.argv[2:]:
    n, x, y, z = ph.split(":")
    for i in range(int(float(n) * 4)):
        out.write(struct.pack("<3i", *(round(float(v) * g / 0.000015258) for v in (x, y, z))))' "$@"; }
# portrait, turned clockwise (right edge down), flat, near the diagonal, counter-clockwise,
# upside down, one portrait sample (shorter than the hold), upside down, portrait
cap /tmp/cap 2:1:0:0 2:0:-1:0 2:0:0:1 2:0.7:-0.7:0 2:0:1:0 2:-1:0:0 0.25:1:0:0 1.75:-1:0:0 2:1:0:0
expect "replay: 270, 90, 180, 0; flat, the diagonal and a short blip keep the turn" \
	"$(/tmp/rotd -c /tmp/rotd.conf -r /tmp/cap | tr -s ' ' | tr '\n' ',')" \
	" 2.50 s transform 270, 8.50 s transform 90, 10.50 s transform 180, 14.50 s transform 0,"
cap /tmp/cap2 1:1:0:0
expect "replay without a device file: Android axes (x towards the right edge reads -g)" \
	"$(/tmp/rotd -c /nonexistent -r /tmp/cap2 | tr -s ' ')" " 0.50 s transform 90"
printf 'AXES=x x\n' > /tmp/bad.conf
expect "a bad AXES line keeps the default" "$(/tmp/rotd -c /tmp/bad.conf -r /tmp/cap2 2>/dev/null | tr -s ' ')" " 0.50 s transform 90"

# ---- the daemon: a stand-in accelerometer, headless sway
F=/tmp/fs/sys/bus/iio/devices
mkdir -p $F/iio:device0 $F/iio:device3/buffer $F/iio:device3/scan_elements /tmp/fs/dev
echo other-sensor > $F/iio:device0/name
echo test-accel > $F/iio:device3/name
echo 0.001 > $F/iio:device3/in_accel_scale
echo 100 > $F/iio:device3/in_accel_sampling_frequency
echo 0 > $F/iio:device3/buffer/enable; echo 2 > $F/iio:device3/buffer/length
for a in in_accel_x_en in_accel_y_en in_accel_z_en; do echo 0 > $F/iio:device3/scan_elements/$a; done
echo 1 > $F/iio:device3/scan_elements/in_timestamp_en
mkfifo /tmp/fs/dev/iio:device3
B=$F/iio:device3/buffer/enable R=$F/iio:device3/in_accel_sampling_frequency

export XDG_RUNTIME_DIR=/run/user/0; mkdir -p -m 700 $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1404x1872 scale 2\n' > /tmp/c
cp /usr/bin/sway /tmp/sway          # the package has cap_sys_nice: no exec in a container
startsway() {
	WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -c /tmp/c >>/tmp/sway.log 2>&1 &
	SWAYPID=$!
	for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.*.sock >/dev/null 2>&1 && break; sleep 0.2; done
	export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*.sock)
}
startsway
xform() { swaymsg -t get_outputs -r | python3 -c 'import json,sys; print([o["transform"] for o in json.load(sys.stdin) if o["name"]=="HEADLESS-1"][0])'; }
mkfifo /run/glassine-epdd-desktop.ctl
(while :; do cat /run/glassine-epdd-desktop.ctl; done) > /tmp/ctl.log 2>/dev/null &
pose() { echo "$*" > /tmp/pose; }
v() { for f; do printf '%s ' "$(cat "$f")"; done; }     # (rotd writes values without a newline)
pose $G 0 0       # portrait: x = down
: > /tmp/rotation; echo auto > /tmp/rotation
# rotd finds sway by the socket glob (as the service does), not SWAYSOCK
env -u SWAYSOCK GLASSINE_ROTD_SYSROOT=/tmp/fs /tmp/rotd -v -c /tmp/rotd.conf -m /tmp/rotation -s /tmp/state 2>/tmp/rotd.log &
ROTD=$!
for i in $(seq 1 30); do [ "$(cat $B)" = 1 ] && break; sleep 0.2; done
python3 /t/feed.py /tmp/fs/dev/iio:device3 &
FEED=$!
sleep 1.5
expect "streaming: the scan elements, no timestamp, the rate" \
	"$(v $B $R $F/iio:device3/scan_elements/in_accel_x_en $F/iio:device3/scan_elements/in_timestamp_en)" "1 4 1 0 "
expect "portrait: no turn" "$(xform) $(cat /tmp/state 2>/dev/null || echo none)" "normal none"
pose 0 -$G 0; sleep 2           # right edge down: y = -g
expect "turned clockwise: transform 270, state, the e-ink hold first" "$(xform) $(cat /tmp/state) $(grep -c rotate /tmp/ctl.log)" "270 270 1"
# the power daemon's suspend: the buffer off, no samples; then on again
pose none; echo 0 > $B; sleep 2.5
expect "a paused stream stays paused (rotd doesn't turn it on), rotd is alive" "$(cat $B) $(kill -0 $ROTD && echo alive)" "0 alive"
echo 1 > $B; pose 0 $G 0; sleep 2
expect "after the pause: counter-clockwise turns to 90" "$(xform) $(cat /tmp/state)" "90 90"
echo "lock 180" > /tmp/rotation; sleep 2
expect "lock 180: turned, the stream off, its rate put back" "$(xform) $(cat /tmp/state) $(v $B $R)" "180 180 0 100 "
pose $G 0 0; sleep 1.5
expect "locked: the sensor is not followed" "$(xform)" "180"
echo lock > /tmp/rotation; sleep 1.5
expect "lock without a turn: stays" "$(xform) $(cat $B)" "180 0"
echo auto > /tmp/rotation; sleep 3
expect "auto again: streaming, portrait turns back to normal" "$(v $B $R)$(xform) $(cat /tmp/state)" "1 4 normal 0"
pose 0 $G 0; sleep 2
expect "turned to 90 again" "$(xform)" "90"
# (SWAYSOCK is set here: the new sway makes its socket at the same path again)
kill $SWAYPID; wait $SWAYPID 2>/dev/null
startsway; sleep 2.5
expect "a new sway gets the turn again" "$(xform)" "90"
kill $ROTD; wait $ROTD 2>/dev/null
expect "stop: the stream off, the rate back" "$(v $B $R)$(tail -1 /tmp/rotd.log | sed 's/.*: //')" "0 100 stop"
grep -q "right .* down .* m/s2" /tmp/rotd.log || { echo "FAIL -v logs the screen-plane gravity"; fail=1; }

# no such sensor: a fixed turn still works, the missing sensor is said once
printf 'SENSOR=missing\nOUTPUT=HEADLESS-1\n' > /tmp/none.conf
echo "lock 270" > /tmp/rotation
env -u SWAYSOCK GLASSINE_ROTD_SYSROOT=/tmp/fs /tmp/rotd -c /tmp/none.conf -m /tmp/rotation -s /tmp/state 2>/tmp/rotd2.log &
ROTD=$!; sleep 2
echo auto > /tmp/rotation; sleep 6.5
expect "no sensor: lock 270 applied, auto retries quietly" "$(xform) $(grep -c 'no accelerometer' /tmp/rotd2.log) $(kill -0 $ROTD && echo alive)" "270 1 alive"
kill $ROTD $FEED $SWAYPID 2>/dev/null
[ $fail = 0 ] || { echo "--- sway.log"; tail -20 /tmp/sway.log; echo "--- rotd.log"; cat /tmp/rotd.log; echo "--- rotd2.log"; cat /tmp/rotd2.log; }
exit $fail
