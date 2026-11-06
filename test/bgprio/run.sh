#!/bin/sh
# (see test.sh) inside the container, as root: /src = src, /t = this dir
set -u
C=/sys/fs/cgroup
# the container's cgroup as a root of its own: processes out of it, the cpu
# controller on for children (as on a device's real root)
mkdir -p $C/init
for p in $(cat $C/cgroup.procs); do echo $p > $C/init/cgroup.procs 2>/dev/null; done
echo +cpu > $C/cgroup.subtree_control || { echo "FAIL no cpu controller"; exit 1; }
B=/tmp/glassine-bgprio
gcc -O2 -Wall -Werror -o $B /src/glassine-bgprio.c /src/wayland/sway-ipc.c $(pkg-config --cflags --libs json-c) -lpthread || exit 1
gcc -O2 -Wall -Werror -o /tmp/glassine-zpark /src/glassine-zpark.c || exit 1
fail=0
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fail=1; fi; }

$B -x 2>/dev/null; expect "an unknown argument: usage (exit 2)" "$?" 2

mkdir -p /etc/glassine && printf 'freeze_after 2\nreclaim always\nwriteback_after 5\npark_after 5\nfreeze footc\n' > /etc/glassine/bgprio.conf
cp /usr/bin/foot /tmp/footc                 # its own process name for the "freeze" rule
cp /usr/bin/foot /tmp/glassine-shell-wl     # a desktop name longer than comm keeps (15)
# glassine-zram stub: records the calls; a park takes 61 s (to be stopped by a thaw)
mkdir -p /usr/sbin && printf '#!/bin/sh\necho "$*" >> /tmp/zram.calls\n[ "$1" = park ] && exec sleep 61\nexit 0\n' > /usr/sbin/glassine-zram && chmod 755 /usr/sbin/glassine-zram
export XDG_RUNTIME_DIR=/run/user/0; mkdir -p -m 700 $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1404x1872 scale 2\n' > /tmp/c
cp /usr/bin/sway /tmp/sway          # the package has cap_sys_nice: no exec in a container
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -c /tmp/c >/tmp/sway.log 2>&1 &
SW=$!
for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1
pid_of() { swaymsg -t get_tree -r | python3 -c '
import json,sys
def w(n):
    if n.get("app_id")==sys.argv[1]: print(n["pid"], n["id"])
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): w(c)
w(json.load(sys.stdin))' $1; }
where() { for g in glassine-bg/frozen glassine-bg glassine-fg init; do grep -qx "$1" $C/$g/cgroup.procs 2>/dev/null && { echo $g; return; }; done; echo "?"; }
swaymsg -q "workspace number 1; exec foot -a A sleep 600"; sleep 1.2
swaymsg -q "workspace number 4; exec /tmp/glassine-shell-wl -a D sleep 600"; sleep 1.2
printf 'import os, time\nx = os.urandom(64 << 20)    # 64 MiB of anon memory, not zero pages\ntime.sleep(600)\n' > /tmp/hog.py
swaymsg -q "workspace number 2; exec foot -a B python3 /tmp/hog.py"; sleep 1.2
set -- $(pid_of A); PA=$1
set -- $(pid_of D); PD=$1
set -- $(pid_of B); PB=$1 IB=$2
SL0=$(cat /proc/$PA/timerslack_ns)  # this kernel's default for A
expect "D's comm is cut to 15 characters" "$(cat /proc/$PD/comm)" "glassine-shell-"
unset SWAYSOCK                      # bgprio finds the socket itself, as in the session
$B 2>/tmp/bgprio.log &
BG=$!
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*)
sleep 1
expect "started: all in front (on ws 2: B)" "$(where $PA) $(where $PB)" "init init"
[ -e $C/glassine-bg/cpu.uclamp.max ] && expect "background cap: bg_uclamp_max default 79" "$(cat $C/glassine-bg/cpu.uclamp.max)" "79.00" ||
    echo "skipped: no cpu.uclamp.max in this kernel"
expect "background group: cpu.weight 1" "$(cat $C/glassine-bg/cpu.weight)" "1"
sleep 4
expect "A hidden (ws 1): demoted after 3 s" "$(where $PA) $(where $PB)" "glassine-bg init"
expect "A demoted: 50 ms timer slack" "$(cat /proc/$PA/timerslack_ns)" "50000000"
swaymsg -q "workspace number 1"; sleep 0.5
expect "switch to 1: A in front at once" "$(where $PA)" "init"
expect "A in front: timer slack back to its default" "$(cat /proc/$PA/timerslack_ns)" "$SL0"
echo "     (waiting for the 30 s rescan: freeze after 2 s hidden)"
sleep 36
expect "B hidden 30 s+: frozen" "$(where $PB)" "glassine-bg/frozen"
expect "frozen: oom_score_adj 1000" "$(cat /proc/$PB/oom_score_adj)" "1000"
grep -q "^1$" $C/glassine-bg/frozen/cgroup.freeze && echo "ok   frozen group: cgroup.freeze 1" || { echo "FAIL frozen group not frozen"; fail=1; }
expect "D (comm glassine-shell-, a desktop name) hidden 30 s+: background, never frozen" "$(where $PD)" "glassine-bg"
sleep 1
if [ "$(wc -l < /proc/swaps)" -gt 1 ]; then
    po=$(grep -o 'paged out [0-9]* frozen process(es): anon [0-9]* -> [0-9]*' /tmp/bgprio.log | head -1)
    set -- $po; [ -n "$po" ] && [ "$7" -ge 60 ] && [ "$9" -lt 8 ] && echo "ok   64 MiB in B paged out: $po" || { echo "FAIL page-out of B's 64 MiB: '$po'"; fail=1; }
else
    echo "skipped: page-out of B (the host has no swap)"
fi
grep -q "paged out" /tmp/bgprio.log && echo "ok   page-out tried after the freeze: $(grep -o 'paged out.*' /tmp/bgprio.log | tail -1)" || { echo "FAIL no reclaim logged"; fail=1; }
python3 -c 'import socket,sys; s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM); s.sendto(("thaw %s" % sys.argv[1]).encode(), "/run/glassine-bgprio.sock")' $IB
sleep 0.5
expect "thaw CON_ID (the shell before a close): B thawed, still background" "$(where $PB)" "glassine-bg"
expect "thawed: oom_score_adj back" "$(cat /proc/$PB/oom_score_adj)" "0"
swaymsg -q "workspace number 2"; sleep 0.5
expect "switch to 2: B in front" "$(where $PB)" "init"
echo "bg_uclamp_max 50" >> /etc/glassine/bgprio.conf
swaymsg -q "workspace number 1"; echo "     (again: B hidden until frozen)"; sleep 36
expect "B frozen again" "$(where $PB)" "glassine-bg/frozen"
[ -e $C/glassine-bg/cpu.uclamp.max ] && expect "bg_uclamp_max 50 in the conf: read on the rescan" "$(cat $C/glassine-bg/cpu.uclamp.max)" "50.00"
expect "writeback_after 5: one glassine-zram writeback on a later rescan" "$(head -1 /tmp/zram.calls 2>/dev/null)" "writeback 1800"
expect "thawed within park_after: no park yet" "$(grep -c '^park' /tmp/zram.calls)" "0"
echo "     (park_after 5: the next rescan parks B)"; sleep 31
pk=$(grep '^park' /tmp/zram.calls | head -1)
case " $pk " in *" $PB "*) echo "ok   park of B's processes: $pk" ;; *) echo "FAIL no park of B ($PB): '$pk'"; fail=1 ;; esac
case " $pk " in *" $PD "*) echo "FAIL D (never frozen) parked: '$pk'"; fail=1 ;; *) echo "ok   D not parked" ;; esac
expect "park running" "$(pgrep -fx 'sleep 61' >/dev/null && echo yes || echo no)" "yes"
swaymsg -q "output HEADLESS-1 scale 1"; sleep 0.5
expect "an output change: every frozen app thawed" "$(where $PB)" "glassine-bg"
expect "thaw stops the running park" "$(pgrep -fx 'sleep 61' >/dev/null && echo running || echo stopped)" "stopped"
grep -q "park of .* stopped: it thawed" /tmp/bgprio.log && echo "ok   log: park stopped" || { echo "FAIL no park-stop log"; fail=1; }
swaymsg -q "workspace number 3; exec /tmp/footc -a C sleep 600"; sleep 1.2
set -- $(pid_of C); PC=$1
expect "C (freeze footc) in front on ws 3" "$(where $PC)" "init"
swaymsg -q "workspace number 2"; sleep 5
expect "freeze footc: C frozen once demoted (no freeze_after wait)" "$(where $PC)" "glassine-bg/frozen"
swaymsg -q "workspace number 3"; sleep 0.5
expect "C back in front: thawed" "$(where $PC)" "init"
grep -q "frozen glassine-shell-" /tmp/bgprio.log && { echo "FAIL a desktop name was frozen"; fail=1; } || echo "ok   log: the desktop name never frozen"
swaymsg -q "workspace number 2"; sleep 4
hidden() { case $(where $1) in glassine-bg*) echo hidden ;; *) where $1 ;; esac; }
expect "before the stop: A, C, D in the background (or frozen)" "$(hidden $PA) $(hidden $PC) $(hidden $PD)" "hidden hidden hidden"
kill $BG; sleep 0.5
expect "SIGTERM: bgprio exited" "$(kill -0 $BG 2>/dev/null && echo running || echo exited)" "exited"
expect "SIGTERM: everything back in front" "$(where $PA) $(where $PC) $(where $PD)" "init init init"
grep -q "stopped, everything back in front" /tmp/bgprio.log && echo "ok   log: stopped" || { echo "FAIL no stop log"; fail=1; }
expect "SIGTERM: the thaw socket removed" "$([ -e /run/glassine-bgprio.sock ] && echo there || echo gone)" "gone"
$B 2>/tmp/bgprio2.log &
BG=$!
sleep 1
kill $SW; sleep 1.5
expect "sway gone: bgprio exited" "$(kill -0 $BG 2>/dev/null && echo running || echo exited)" "exited"
grep -q "sway gone, exiting" /tmp/bgprio2.log && echo "ok   log: sway gone" || { echo "FAIL log"; cat /tmp/bgprio2.log; fail=1; }
expect "sway gone: the thaw socket removed" "$([ -e /run/glassine-bgprio.sock ] && echo there || echo gone)" "gone"

# glassine-zpark: usage and a dry run (finding real zram slots needs a zram swap device)
/tmp/glassine-zpark 2>/dev/null; expect "zpark without PID: usage (exit 2)" "$?" 2
/tmp/glassine-zpark -x 2>/dev/null; expect "zpark bad option: usage (exit 2)" "$?" 2
out=$(/tmp/glassine-zpark -n $$); expect "zpark -n PID: exit 0" "$?" 0
case "$out" in "glassine-zpark: 1 process(es), "*" swapped slot(s) on swap type "*) echo "ok   zpark -n: $out" ;; *) echo "FAIL zpark -n: '$out'"; fail=1 ;; esac
out=$(/tmp/glassine-zpark -n -c $C/init); expect "zpark -n -c CGROUP: exit 0" "$?" 0
/tmp/glassine-zpark -n -c /nonexistent 2>/dev/null; expect "zpark -c missing cgroup: exit 1" "$?" 1

[ $fail = 0 ] || { echo "--- bgprio.log"; cat /tmp/bgprio.log; }
exit $fail
