#!/bin/sh
# (see test.sh) inside the container: /src = src, /t = this dir
set -u
sh /src/wayland/build-swayd.sh /src /tmp/glassine-swayd || exit 1
export XDG_RUNTIME_DIR=/tmp/rt; mkdir -p -m 700 $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1404x1872 scale 2\ndefault_border none\ndefault_floating_border none\ngaps inner 20\ngaps outer -20\nsmart_gaps on\nfor_window [app_id="dlg"] floating enable\n' > /tmp/c
# keys.conf's bare-Super binding vs a Super chord (sway's release bindings, below)
printf 'bindsym --release Super_L exec touch /tmp/rel\nbindsym Mod4+Tab exec touch /tmp/tab\n' >> /tmp/c
cp /usr/bin/sway /tmp/sway          # the package has cap_sys_nice: no exec in a container
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -c /tmp/c >/tmp/sway.log 2>&1 &
for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1
mkfifo /run/glassine-epdd-desktop.ctl
(while :; do cat /run/glassine-epdd-desktop.ctl; done) > /tmp/hints.log 2>/dev/null &
/tmp/glassine-swayd -v 2>/tmp/swayd.log &
sleep 0.5
fail=0
term() { swaymsg -q "exec foot -a $1 sleep 600"; sleep 1.2; }
kill_() { swaymsg -q "[app_id=$1] kill"; sleep 1.2; }
# "app ws x y w h" per view (logical px), sorted by app
geo() { swaymsg -t get_tree -r | python3 -c '
import json,sys
out=[]
def w(n,ws):
    if n.get("type")=="workspace": ws=n["name"]
    if n.get("shell"): r=n["rect"]; out.append("%s %s %d %d %d %d" % (n["app_id"],ws,r["x"],r["y"],r["width"],r["height"]))
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): w(c,ws)
w(json.load(sys.stdin),"")
print(" | ".join(sorted(out)))'; }
cur() { swaymsg -t get_workspaces -r | python3 -c 'import json,sys; w=json.load(sys.stdin); print(" ".join(x["name"] for x in sorted(w,key=lambda x:x["num"])), "focus", [x["name"] for x in w if x["focused"]][0])'; }
id() { swaymsg -t get_tree -r | python3 -c "
import json,sys
def w(n):
    if n.get('app_id')=='$1': print('%x' % n['id'])
    for c in n.get('nodes',[])+n.get('floating_nodes',[]): w(c)
w(json.load(sys.stdin))"; }
expect() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fail=1; fi; }
cmd() { python3 -c "import socket,sys,os; socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM).sendto(sys.argv[1].encode(),os.environ['XDG_RUNTIME_DIR']+'/glassine-swayd')" "$1"; sleep 1; }

term A
expect "1 window: whole workspace" "$(geo)" "A 1 0 0 702 936"
term B
expect "2 windows: newest on top" "$(geo)" "A 1 0 478 702 458 | B 1 0 0 702 458"
term C
expect "3 windows: newest on top, the rest side by side" "$(geo)" "A 1 361 478 341 458 | B 1 0 478 341 458 | C 1 0 0 702 458"
term Q
expect "4 windows: quarters, the newest two on top" "$(geo)" "A 1 361 478 341 458 | B 1 0 478 341 458 | C 1 361 0 341 458 | Q 1 0 0 341 458"
sleep 0.5
expect "4 windows: 3 handles (the outer split + one per pair)" "$(grep 'handle [0-9]: level' /tmp/swayd.log | tail -3 | sed 's/.*handle \([0-9]\): level \([01]\) \([a-z]*\).*/\1\2\3/' | tr '\n' ' ')" "00horizontal 11vertical 21vertical "
cmd "orient 0 1"
expect "4 windows turned: still quarters, pairs side by side" "$(geo)" "A 1 361 478 341 458 | B 1 361 0 341 458 | C 1 0 478 341 458 | Q 1 0 0 341 458"
cmd "orient 0 0"
cmd "turn $(id A)"
expect "turn (the overview): 4 windows still quarters, turned" "$(geo)" "A 1 361 478 341 458 | B 1 361 0 341 458 | C 1 0 478 341 458 | Q 1 0 0 341 458"
cmd "turn $(id A)"
term D
expect "5th window: to a new workspace" "$(geo)" "A 1 361 478 341 458 | B 1 0 478 341 458 | C 1 361 0 341 458 | D 2 0 0 702 936 | Q 1 0 0 341 458"
expect "5th window: the view follows" "$(cur)" "1 2 focus 2"
swaymsg -q "workspace 1"; sleep 0.8
kill_ Q
expect "4 -> 3: the first on top again" "$(geo)" "A 1 361 478 341 458 | B 1 0 478 341 458 | C 1 0 0 702 458 | D 2 0 0 702 936"
term dlg
expect "a dialog floats, does not count" "$(swaymsg -t get_tree -r | grep -c '"app_id": "dlg"')" "1"
kill_ dlg
swaymsg -q "workspace 1"; sleep 0.8
kill_ C
expect "closed one: the whole half went, the split half is the screen (its direction kept)" "$(geo)" "A 1 361 0 341 936 | B 1 0 0 341 936 | D 2 0 0 702 936"
cmd "swap $(id A) $(id B)"
expect "swap: A first" "$(geo)" "A 1 0 0 341 936 | B 1 361 0 341 936 | D 2 0 0 702 936"
cmd "orient 0 1"
expect "orient 0 1: side by side" "$(geo)" "A 1 0 0 341 936 | B 1 361 0 341 936 | D 2 0 0 702 936"
cmd "orient 0 0"
term E
expect "3 again (after swap: A B kept in order, E new on top)" "$(geo)" "A 1 0 478 341 458 | B 1 361 478 341 458 | D 2 0 0 702 936 | E 1 0 0 702 458"
# swapdir (keys.conf: Super+Shift+arrows): the focused tile trades places with its neighbour
swaymsg -q "[app_id=A] focus"; sleep 0.5
cmd "swapdir right"
expect "swapdir right: A and B trade" "$(geo)" "A 1 361 478 341 458 | B 1 0 478 341 458 | D 2 0 0 702 936 | E 1 0 0 702 458"
swaymsg -q "[app_id=A] focus"; sleep 0.5
cmd "swapdir up"
expect "swapdir up: A and E trade" "$(geo)" "A 1 0 0 702 458 | B 1 0 478 341 458 | D 2 0 0 702 936 | E 1 361 478 341 458"
swaymsg -q "[app_id=A] focus"; sleep 0.5
cmd "swapdir up"
expect "swapdir up at the top: nothing moves" "$(geo)" "A 1 0 0 702 458 | B 1 0 478 341 458 | D 2 0 0 702 936 | E 1 361 478 341 458"
/tmp/glassine-swayd swapdir down; sleep 1          # the command line form (keys.conf runs this)
expect "glassine-swayd swapdir down: A and B trade" "$(geo)" "A 1 0 478 341 458 | B 1 0 0 702 458 | D 2 0 0 702 936 | E 1 361 478 341 458"
# a gap: workspace 3 with F, then 2 emptied while on 1
swaymsg -q "workspace number 3"; term F
swaymsg -q "workspace 1"; sleep 0.5
kill_ D
expect "workspace 2 emptied: 3 renumbered to 2" "$(cur)" "1 2 focus 1"
expect "... F now on 2" "$(geo | tr '|' '\n' | grep '^ *F')" " F 2 0 0 702 936"
n=$(grep -c "^win " /tmp/hints.log)
[ "$n" -gt 5 ] && echo "ok   hints: $n win lines" || { echo "FAIL hints: $n"; fail=1; }
bad=$(awk '$1=="win" && ($2<0 || $3<0 || $2+$4>1404 || $3+$5>1872) {print}' /tmp/hints.log | head -3)
expect "hints: in portrait panel px" "$bad" ""
grep -q "^win 0 0 1404 916$" /tmp/hints.log && echo "ok   hints: a top half (x2, above the 40 px gap) seen" || { echo "FAIL no top-half hint"; head /tmp/hints.log; fail=1; }
# the app list for glassine-epdd-desktop's profiles (P13-9): app_id lower-cased, the focused one active
grep -qE "^app [0-9]+ [0-9]+ [0-9]+ [0-9]+ [a-f]$" /tmp/hints.log && echo "ok   apps: windows with their app_id" || { echo "FAIL no app lines"; fail=1; }
expect "apps: the last list's active window" "$(grep '^active' /tmp/hints.log | tail -1)" "active $(swaymsg -t get_tree -r | python3 -c '
import json,sys
def w(n):
    if n.get("focused") and n.get("app_id"): print(n["app_id"].lower())
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): w(c)
w(json.load(sys.stdin))')"
n0=$(grep -c "^apps " /tmp/hints.log)
python3 -c 'import socket,os; s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM); s.sendto(b"apps?", os.environ["XDG_RUNTIME_DIR"]+"/glassine-swayd")'
sleep 0.8
expect "apps?: the list sent again (glassine-epdd-desktop restarted)" "$(grep -c '^apps ' /tmp/hints.log)" "$((n0 + 1))"
# launch placeholders (glassine-shell-wl at a tap; here foot with its app id and title)
ph() { swaymsg -q "exec foot -a glassine-placeholder -T 'glassine-ph 0 $1' sleep 600"; sleep 1.2; }
geo_of() { geo | tr '|' '\n' | sed 's/^ *//; s/ *$//' | grep "^$1 " | cut -d' ' -f2-; }
swaymsg -q "workspace 2"; sleep 0.5
ph realapp
P=$(geo_of glassine-placeholder)
expect "placeholder: a tile at once (newest on top)" "$P" "2 0 0 702 458"
swaymsg -q "exec WAYLAND_DEBUG=client foot -a realapp sleep 600 2>/tmp/realapp.wl"; sleep 1.2
expect "the app is only ever told its tile's size" "$(grep -a 'xdg_toplevel#[0-9]*\.configure(' /tmp/realapp.wl | grep -av 'configure(0, 0' | sed 's/.*configure(\([0-9]*, [0-9]*\).*/\1/' | sort -u | tr '\n' ';')" "702, 458;"
expect "the app takes the placeholder's tile" "$(geo_of realapp)" "$P"
expect "... the placeholder closed" "$(geo_of glassine-placeholder)" ""
expect "... F where it was, nothing moved away" "$(geo_of F)" "2 0 478 702 458"
swaymsg -q "workspace 1"; sleep 0.5
term G
ph realapp2
expect "placeholder on a full workspace: to the first with room, the view follows" "$(cur)" "1 2 focus 2"
P=$(geo_of glassine-placeholder)
term realapp2
expect "... the app takes its tile there" "$(geo_of realapp2)" "$P"
expect "... workspace 1 untouched" "$(geo | tr '|' '\n' | grep -c ' 1 ')" "4"
expect "... the placeholder closed" "$(geo_of glassine-placeholder)" ""
term H
term other
expect "a non-matching app with no placeholder: an ordinary new window (overflow)" "$(geo_of other | cut -d' ' -f1)" "3"
# handles (saw them vanish): after every step above the
# focused workspace with 2-3 tiles must have handles; a turned output moves them
hl() { grep "handle [0-9]: level" /tmp/swayd.log | tail -${1:-1}; }
swaymsg -q "workspace 1"; sleep 0.8
h0=$(hl)
[ -n "$h0" ] && echo "ok   handles on workspace 1: $h0" || { echo "FAIL no handle lines"; fail=1; }
swaymsg -q "output HEADLESS-1 transform 90"; sleep 1
h1=$(hl)
[ "$h1" != "$h0" ] && echo "ok   turned output: handles moved ($h1)" || { echo "FAIL turned output: handles not moved ($h1)"; fail=1; }
swaymsg -q "output HEADLESS-1 transform 0"; sleep 1
# a physical keyboard : the classification, from a saved get_inputs
# reply + /proc/bus/input/devices (fixtures: the device's own keys, a virtual
# keyboard, a BT keyboard, a BT mouse's media-key node, a vendor mismatch)
expect "physical keyboard: only the BT keyboard counts" "$(/tmp/glassine-swayd --kbd-test /t/inputs.json /t/devices.txt)" "1 Keychron K3"
awk -v RS= -v ORS='\n\n' '!/Keychron/' /t/devices.txt > /tmp/dev-nokbd
expect "physical keyboard: gone from /proc, not counted" "$(/tmp/glassine-swayd --kbd-test /t/inputs.json /tmp/dev-nokbd)" "0"
expect "physical keyboard: /proc unreadable, not counted" "$(/tmp/glassine-swayd --kbd-test /t/inputs.json /nonexistent)" "0"
# sway's input events reach swayd: wtype's virtual keyboard comes and goes (not
# physical; sway 1.10 sends only its "removed", a libinput device also "added")
n0=$(grep -c "inputs: " /tmp/swayd.log)
wtype -s 300 x; sleep 0.8
expect "input event: re-checked when a device goes" "$([ $(grep -c "inputs: " /tmp/swayd.log) -gt $n0 ] && echo yes)" "yes"
expect "... a virtual keyboard is no physical one" "$(grep "inputs: " /tmp/swayd.log | tail -1 | sed 's/.*inputs: //')" "0 physical keyboards"
# bare Super (released alone) vs a Super chord: the drawer only for the bare one
rm -f /tmp/rel /tmp/tab
wtype -P Super_L -p Super_L; sleep 0.5
expect "Super released alone: its release binding" "$(ls /tmp/rel /tmp/tab 2>/dev/null | tr '\n' ' ')" "/tmp/rel "
rm -f /tmp/rel /tmp/tab
wtype -P Super_L -M logo -k Tab -m logo -p Super_L; sleep 0.5
expect "Super+Tab: the chord only, no release binding" "$(ls /tmp/rel /tmp/tab 2>/dev/null | tr '\n' ' ')" "/tmp/tab "
# the device's sway config with keys.conf : sway's own check, and
# keys.conf's commands exist in the overlay
sed 's|/usr/share/glassine/sway/keys.conf|/o/usr/share/glassine/sway/keys.conf|' /o/usr/share/glassine/sway/config > /tmp/devcfg
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -d -C -c /tmp/devcfg > /tmp/devcfg.log 2>&1
expect "sway -C: keys.conf read" "$(grep -c 'Loading config from /o/usr/share/glassine/sway/keys.conf' /tmp/devcfg.log)" "1"
expect "sway -C: no errors, no binding bound twice" "$(grep -E '\[ERROR\]|Overwriting' /tmp/devcfg.log | head -3)" ""
expect "keys.conf: its glassine scripts are in the overlay" "$(grep -o '/usr/s\?bin/glassine-[a-z0-9-]*' /o/usr/share/glassine/sway/keys.conf | sort -u | while read -r f; do [ -e /o$f ] || [ $f = /usr/bin/glassine-shell-wl ] || [ $f = /usr/bin/glassine-swayd ] || echo $f; done)" ""
expect "shellcheck: glassine-screenshot, glassine-kbd-layout" "$(shellcheck -s sh /o/usr/bin/glassine-screenshot /o/usr/bin/glassine-kbd-layout 2>&1)" ""
# the hardware keyboard layout (glassine-kbd-layout): as the OSK's first language, or the
# user's choice; the include sway loads, and set live (a libinput keyboard: none here,
# so the IPC command's success is what can be checked)
H=/tmp/kh; mkdir -p $H/.config/glassine
kl() { HOME=$H XDG_CONFIG_HOME= sh /o/usr/bin/glassine-kbd-layout "$@"; }
expect "layout: no files: de (wvkbd's default german)" "$(kl -n)" "de"
echo "full,special" > $H/.config/glassine/osk-layers
expect "layout: follows the OSK (us)" "$(kl -n)" "us"
echo de > $H/.config/glassine/kbd-layout
expect "layout: the user's choice wins" "$(kl -n)" "de"
kl
expect "layout: the include sway loads" "$(sed -n 2p $H/.config/glassine/sway-keyboard.conf)" "input type:keyboard xkb_layout de"
expect "layout: sway takes the command" "$(swaymsg -q 'input type:keyboard xkb_layout de' 2>&1; echo $?)" "0"
# design shell.overview.move + shell.divider: place, the halves, the gap closing
swaymsg -q '[shell=".*"] kill'; sleep 1.5
term P; term Q
swaymsg -q "workspace number 2"; sleep 0.5; term R
expect "place: before" "$(geo)" "P 1 0 478 702 458 | Q 1 0 0 702 458 | R 2 0 0 702 936"
cmd "place $(id R) $(id Q) left"
expect "place: R left of Q, a half splits, the empty screen goes" "$(geo)" "P 1 0 478 702 458 | Q 1 361 0 341 458 | R 1 0 0 341 458"
expect "place: the view follows" "$(cur)" "1 focus 1"
cmd "swaphalves $(id Q)"
expect "swaphalves: the pair and the whole one trade" "$(geo)" "P 1 0 0 702 458 | Q 1 361 478 341 458 | R 1 0 478 341 458"
cmd "swap $(id R) $(id Q)"
expect "inner swap: inside the half" "$(geo)" "P 1 0 0 702 458 | Q 1 0 478 341 458 | R 1 361 478 341 458"
kill_ P
expect "gap: the split half is the screen now, its direction kept" "$(geo)" "Q 1 0 0 341 936 | R 1 361 0 341 936"
swaymsg -q '[shell=".*"] kill'; sleep 1.5
term A; term B; term C; term E
expect "quarters" "$(geo)" "A 1 361 478 341 458 | B 1 0 478 341 458 | C 1 361 0 341 458 | E 1 0 0 341 458"
kill_ B
expect "gap: quarters lose one, its half is whole again" "$(geo)" "A 1 0 478 702 458 | C 1 361 0 341 458 | E 1 0 0 341 458"
swaymsg -q '[shell=".*"] kill'; sleep 1.5
term P; swaymsg -q "workspace number 2"; sleep 0.5; term R
cmd "place $(id R) $(id P) right"
expect "place: alone, right = side by side" "$(geo)" "P 1 0 0 341 936 | R 1 361 0 341 936"
# Full / New screen: right after the window's own screen, the later ones move up
swaymsg -q "workspace number 2"; term Q; swaymsg -q "workspace 1"; sleep 0.5
cmd "newscreen $(id R) 0"; sleep 1.2
expect "newscreen: inserted, no switch" "$(cur)" "1 2 3 focus 1"
expect "... R on 2, Q moved up to 3, P alone" "$(geo)" "P 1 0 0 702 936 | Q 3 0 0 702 936 | R 2 0 0 702 936"
cmd "newscreen $(id P) 1"; sleep 1
expect "newscreen: alone on its screen, nothing happens" "$(cur)" "1 2 3 focus 1"
# the arrows onto a maximized window: it comes back to its tile, side by side
swaymsg -q "[app_id=Q] fullscreen enable"; sleep 0.8
cmd "place $(id R) $(id Q) right"; sleep 1.5
expect "place onto a maximized window" "$(geo)" "P 1 0 0 702 936 | Q 2 0 0 341 936 | R 2 361 0 341 936"
expect "... not maximized any more, screens closed up" "$(cur)" "1 2 focus 1"
swaymsg -q "workspace 2"; sleep 0.5
cmd "newscreen $(id Q) 1"; sleep 1.2
expect "newscreen with follow" "$(cur)" "1 2 3 focus 3"
expect "... Q on 3, R alone" "$(geo)" "P 1 0 0 702 936 | Q 3 0 0 702 936 | R 2 0 0 702 936"
echo "--- handle history"; grep -n "handle [0-9]: level\|no handles\|placeholder\|closed" /tmp/swayd.log | tail -40
grep -iE "error|fail" /tmp/swayd.log | grep -v "FIFO write failed" | head -5
[ $fail = 0 ] || { echo "--- swayd.log"; grep -n "placeholder\|kill" /tmp/swayd.log | head -20; tail -30 /tmp/swayd.log; }
exit $fail
