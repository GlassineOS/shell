#!/bin/sh
# (see test.sh) inside the container: /src = src, /t = this dir, /ui = GlassineOS/ui (ro)
set -u
make -s -C /ui install O=/tmp/ui-out PREFIX=/usr >/dev/null || exit 1   # (its own out: never the checkout's)
sh /src/wayland/build-swayd.sh /src /tmp/glassine-swayd || exit 1
export XDG_RUNTIME_DIR=/tmp/rt; mkdir -p -m 700 $XDG_RUNTIME_DIR
printf 'output HEADLESS-1 resolution 1404x1872 scale 2\ndefault_border none\ndefault_floating_border none\ngaps inner 20\ngaps outer -20\nsmart_gaps on\nfor_window [app_id="dlg"] floating enable\n' > /tmp/c
# a top bar (swaybar) for the dialog cases: invisible (no exclusive zone) until they dock it
printf 'bar {\n id top\n position top\n mode invisible\n}\n' >> /tmp/c
# keys.conf's bare-Super binding vs a Super chord (sway's release bindings, below)
printf 'bindsym --release Super_L exec touch /tmp/rel\nbindsym Mod4+Tab exec touch /tmp/tab\n' >> /tmp/c
cp /usr/bin/sway /tmp/sway          # the package has cap_sys_nice: no exec in a container
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 /tmp/sway -c /tmp/c >/tmp/sway.log 2>&1 &
for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1
mkfifo /run/glassine-epdd-desktop.ctl
(while :; do cat /run/glassine-epdd-desktop.ctl; done) > /tmp/hints.log 2>/dev/null &
# glassine-shell stand-in: its requests (NUL-separated words), one line each
python3 -c '
import socket,os
s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM); s.bind(os.environ["XDG_RUNTIME_DIR"]+"/glassine-shell")
f=open("/tmp/shell.log","a")
while True:
    m=s.recv(4096); f.write(m.replace(b"\0",b" ").decode()+"\n"); f.flush()' &
sleep 0.3
GLASSINE_SWAYD_TEST=1 /tmp/glassine-swayd -v 2>/tmp/swayd.log &
sleep 0.5
fail=0
# a window opens (waits up to 5 s for it: foot is slow to map under load), then the layout settles
term() { swaymsg -q "exec foot -a $1 sleep 600"; sleep 0.4; for i in $(seq 1 46); do swaymsg -t get_tree | grep -q "\"app_id\": \"$1\"" && break; sleep 0.1; done; sleep 0.8; }
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
expect "4 windows: 3 handles (the outer split + one per pair, levels 1 and 2)" "$(grep 'handle [0-9]: level' /tmp/swayd.log | tail -3 | sed 's/.*handle \([0-9]\): level \([012]\) \([a-z]*\).*/\1\2\3/' | tr '\n' ' ')" "00horizontal 11vertical 22vertical "
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
# the app list for glassine-epdd-desktop's profiles: app_id lower-cased, the focused one active
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
# launch placeholders (glassine-shell at a tap; here foot with its app id and title)
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
expect "keys.conf: its glassine scripts are in the overlay" "$(grep -o '/usr/s\?bin/glassine-[a-z0-9-]*' /o/usr/share/glassine/sway/keys.conf | sort -u | while read -r f; do [ -e /o$f ] || [ $f = /usr/bin/glassine-shell ] || [ $f = /usr/bin/glassine-swayd ] || echo $f; done)" ""
expect "keys.conf: no sway fullscreen (full screen is glassine-swayd's full: the bar stays)" "$(grep -E '^bindsym .* fullscreen' /o/usr/share/glassine/sway/keys.conf)" ""
expect "shellcheck: glassine-screenshot, glassine-kbd-layout, glassine-home-start" "$(shellcheck -s sh /o/usr/bin/glassine-screenshot /o/usr/bin/glassine-kbd-layout /o/usr/bin/glassine-home-start 2>&1)" ""
# the home app the session starts (glassine-home-start -n: the command): the app_id as
# glassine-swayd finds it, its desktop entry, else a command of that name, else nothing
HS=/tmp/hs; mkdir -p $HS/data/applications $HS/bin
hs() { env -u GLASSINE_HOME_APP_ID HOME=$HS XDG_DATA_HOME=$HS/data XDG_DATA_DIRS=/nonexistent PATH=$HS/bin:/usr/bin:/bin "$@" sh /o/usr/bin/glassine-home-start -n; echo "rc $?"; }
expect "home app: none installed, nothing set: nothing starts" "$(hs)" "rc 0"
printf '#!/bin/sh\n' > $HS/bin/glassine-home; chmod +x $HS/bin/glassine-home
expect "home app: glassine-home by default" "$(hs | tr '\n' ' ')" "glassine-home rc 0 "
mkdir -p /etc/glassine; printf 'other.Home extra\n' > /etc/glassine/home-app
printf '[Desktop Entry]\nType=Application\nName=Other\nExec=other-home --page %%U\n[Desktop Action x]\nExec=nope\n' > $HS/data/applications/other.Home.desktop
expect "home app: /etc/glassine/home-app's first word, from its desktop entry (field codes dropped)" "$(hs | tr '\n' ' ')" "other-home --page rc 0 "
printf '#!/bin/sh\n' > $HS/bin/mine; chmod +x $HS/bin/mine
expect "home app: \$GLASSINE_HOME_APP_ID first, a command of that name" "$(hs GLASSINE_HOME_APP_ID=mine | tr '\n' ' ')" "mine rc 0 "
rm -f /etc/glassine/home-app
# started again on every exit while the session runs (a clean one too), the pause doubling;
# stops once sway no longer answers (a crash leaves its socket file) or the Wayland display's
# lock is free; no session known: started once. A stand-in sway answers every IPC message.
FS=$HS/sway.sock
python3 -c '
import socket,struct,sys
s=socket.socket(socket.AF_UNIX); s.bind(sys.argv[1]); s.listen(8)
while True:
    c,_=s.accept(); h=c.recv(14)
    if len(h)==14:
        n,t=struct.unpack("=II",h[6:]); c.recv(n) if n else None
        p=b"{\"major\": 1, \"minor\": 0, \"patch\": 0, \"human_readable\": \"1\"}"
        c.sendall(b"i3-ipc"+struct.pack("=II",len(p),t)+p)
    c.close()' $FS & fsp=$!
printf '#!/bin/sh\necho run >> %s/runs\nexit $(($(wc -l < %s/runs) %% 2))\n' $HS $HS > $HS/bin/fakehome; chmod +x $HS/bin/fakehome
hr() { env -u WAYLAND_DISPLAY -u SWAYSOCK GLASSINE_HOME_APP_ID=fakehome HOME=$HS XDG_DATA_HOME=$HS/data XDG_DATA_DIRS=/nonexistent PATH=$HS/bin:/usr/bin:/bin "$@" sh /o/usr/bin/glassine-home-start; }
sleep 0.3; rm -f $HS/runs; hr SWAYSOCK=$FS & hp=$!
sleep 2     # starts at 0 s (exit 1) and 1 s (exit 0); the next waits until 3 s
expect "home app: started again after a failure and after a clean exit" "$(wc -l < $HS/runs)" "2"
kill $fsp; sleep 2.5
expect "home app: sway gone (its socket file left behind): not started again, the script ends" "$([ -S $FS ] && echo file) $(wc -l < $HS/runs) $(kill -0 $hp 2>/dev/null && echo running || echo ended)" "file 2 ended"
kill $hp 2>/dev/null; wait $hp 2>/dev/null
mkdir -p $HS/rt; python3 -c 'import socket,sys; socket.socket(socket.AF_UNIX).bind(sys.argv[1])' $HS/rt/wl-test
python3 -c 'import fcntl,sys,time; f=open(sys.argv[1],"w"); fcntl.flock(f,fcntl.LOCK_EX); time.sleep(600)' $HS/rt/wl-test.lock & lp=$!
sleep 0.3; rm -f $HS/runs; hr XDG_RUNTIME_DIR=$HS/rt WAYLAND_DISPLAY=wl-test & hp=$!
sleep 2; kill $lp; sleep 2.5
expect "home app: the Wayland display alone counts: its lock freed, the script ends" "$(wc -l < $HS/runs) $(kill -0 $hp 2>/dev/null && echo running || echo ended)" "2 ended"
kill $hp 2>/dev/null; wait $hp 2>/dev/null
rm -f $HS/runs
expect "home app: no session socket known: started once" "$(hr; echo "rc $? $(wc -l < $HS/runs)")" "rc 0 1"
expect "sway config: starts the home app with glassine-home-start" "$(grep -c '^exec /usr/bin/glassine-home-start$' /o/usr/share/glassine/sway/config)" "1"
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
# ---- spec 001 window model: a position per line, the new verbs, Home and the empty screen
lastdiv() { grep '^dividers ' /tmp/shell.log | tail -1; }
divs() { lastdiv | cut -d' ' -f2- | tr ' ' '\n' | sed 's/^\([0-9-]*,[0-9-]*,[0-9-]*,[0-9-]*,[01],[012]\),[0-9a-f]*,[0-9a-f]*,/\1,A,B,/' | tr '\n' ' '; }
# the shell's last `home` request; with an argument, waits up to 3 s for that one (swayd may lag)
lasthome() { for i in $(seq 1 30); do l=$(grep '^home ' /tmp/shell.log | tail -1); [ "$l" = "${1:-}" ] && break; [ -z "${1:-}" ] && break; sleep 0.1; done; echo "$l"; }
# views in sway's fullscreen (it covers the bar; workspaces report 1 always)
fsviews() { swaymsg -t get_tree -r | python3 -c '
import json,sys
n=0
def w(x):
    global n
    if x.get("shell") and x.get("fullscreen_mode"): n+=1
    for c in x.get("nodes",[])+x.get("floating_nodes",[]): w(c)
w(json.load(sys.stdin)); print(n)'; }
warned() { [ "$(grep -c "$1" /tmp/swayd.log)" -gt 0 ] && echo yes; }
# "app ws WxH" per view, sorted by app; the views marked `full`
sizes() { geo | tr '|' '\n' | awk '{ printf "%s%s %s %sx%s", (NR > 1 ? " | " : ""), $1, $2, $5, $6 }'; }
fullmarked() { swaymsg -t get_tree -r | python3 -c '
import json,sys
n=0
def w(x):
    global n
    if x.get("shell") and "_glassine_full" in x.get("marks",[]): n+=1
    for c in x.get("nodes",[])+x.get("floating_nodes",[]): w(c)
w(json.load(sys.stdin)); print(n)'; }
swaymsg -q '[shell=".*"] kill'; sleep 1.5
swaymsg -q 'workspace 1'; sleep 0.3
term A; term B; term C; term E
Q0="A 1 361 478 341 458 | B 1 0 478 341 458 | C 1 361 0 341 458 | E 1 0 0 341 458"
expect "model: quarters (E C / B A)" "$(geo)" "$Q0"
cmd "testdrag 1 0.4"
expect "inner line of half 0 drags alone" "$(geo)" "A 1 361 478 341 458 | B 1 0 478 341 458 | C 1 291 0 411 458 | E 1 0 0 271 458"
cmd "testdrag 2 0.415"
G1="A 1 291 478 411 458 | B 1 0 478 271 458 | C 1 291 0 411 458 | E 1 0 0 271 458"
expect "inner line of half 1 snaps to the other one" "$(geo)" "$G1"
expect "dividers: x,y,w,h,vert,level,a,b,pos (level 0 main, 1/2 inner)" "$(divs)" "4,0 0,916,1404,40,0,0,A,B,0.500 542,0,40,916,1,1,A,B,0.400 542,956,40,916,1,2,A,B,0.400 "
cmd "cross $(id A)"
expect "cross: every window keeps its quarter" "$(geo)" "$G1"
expect "cross: the other axis runs through (main line vertical, at the inner lines' mean)" "$(divs | cut -d' ' -f1-2)" "4,1 542,0,40,1872,1,0,A,B,0.400"
cmd "cross $(id A)"
expect "cross again: back, quarters kept" "$(geo)" "$G1"
expect "... the main line across again" "$(divs | cut -d' ' -f1-2)" "4,0 0,916,1404,40,0,0,A,B,0.500"
cmd "testdrag 2 0.6"
expect "inner lines separate (half 1 at 0.6)" "$(geo)" "A 1 431 478 271 458 | B 1 0 478 411 458 | C 1 291 0 411 458 | E 1 0 0 271 458"
cmd "cross $(id A)"
expect "cross with separate lines: they meet at their mean (0.5), quarters kept" "$(geo)" "A 1 361 478 341 458 | B 1 0 478 341 458 | C 1 361 0 341 458 | E 1 0 0 341 458"
cmd "swapin $(id E)"
expect "swapin: the two of E's half trade (left column)" "$(geo)" "A 1 361 478 341 458 | B 1 0 0 341 458 | C 1 361 0 341 458 | E 1 0 478 341 458"
cmd "direction $(id E)"
expect "direction on 4 windows: refused" "$(warned 'direction: 4 windows')" "yes"
cmd "full $(id E)"
expect "full: E over the work area, the others tiled underneath (sway fills its tile)" "$(geo)" "A 1 361 478 341 458 | B 1 0 0 341 936 | C 1 361 0 341 458 | E 1 0 0 702 936"
expect "full: no sway fullscreen (the bar stays above)" "$(fsviews)" "0"
expect "full: no handles under it" "$(grep 'handle [0-9]: level\|no handles' /tmp/swayd.log | tail -1 | grep -c 'no handles')" "1"
cmd "full $(id E)"
expect "full off: E back in its tile" "$(geo)" "A 1 361 478 341 458 | B 1 0 0 341 458 | C 1 361 0 341 458 | E 1 0 478 341 458"
cmd "full $(id E)"
kill_ E
expect "a full window closes: its gap closes (its column whole)" "$(geo)" "A 1 361 478 341 458 | B 1 0 0 341 936 | C 1 361 0 341 458"
# a window opens on a screen with a full one: full ends (a floating window would hide it)
cmd "full $(id C)"
term F
expect "a window opens while one is full: full ends, quarters" "$(sizes)" "A 1 341x458 | B 1 341x458 | C 1 341x458 | F 1 341x458"
expect "... no window left marked full" "$(fullmarked)" "0"
cmd "full $(id C)"
expect "... C full again" "$(fullmarked)" "1"
term G
expect "a fifth window while one is full: the 4-apps rule, a new screen, the view follows" "$(cur) $(geo_of G)" "1 2 focus 2 2 0 0 702 936"
expect "... full ended on screen 1" "$(fullmarked) $(sizes | cut -d'|' -f1-4)" "0 A 1 341x458 | B 1 341x458 | C 1 341x458 | F 1 341x458 "
swaymsg -q 'workspace 1'; sleep 0.5
# sway's own fullscreen (an app asked, or a gesture): the shell shows it as full screen, and
# its Exit full screen (`full W`) ends it in one tap
swaymsg -q "[app_id=F] fullscreen enable"; sleep 0.8
expect "an app in sway's fullscreen" "$(fsviews)" "1"
cmd "full $(id F)"
expect "full on a window in sway's fullscreen: back in its tile, not full" "$(fsviews) $(fullmarked) $(sizes | cut -d'|' -f1-4)" "0 0 A 1 341x458 | B 1 341x458 | C 1 341x458 | F 1 341x458 "
# `full` with no window (keys.conf's Super+F): the focused one
swaymsg -q "[app_id=A] focus"; sleep 0.5
cmd "full"
expect "full, no window named: the focused one, over the work area" "$(fullmarked) $(fsviews) $(geo_of A)" "1 0 1 0 0 702 936"
cmd "full"
expect "full, no window named, again: back in its tile" "$(fullmarked) $(geo_of A | cut -d' ' -f4-)" "0 341 458"
# two windows: direction, and place on the window's own screen
swaymsg -q '[shell=".*"] kill'; sleep 1.5
term P; term Q
expect "2 windows stacked" "$(geo)" "P 1 0 478 702 458 | Q 1 0 0 702 458"
cmd "direction $(id P)"
expect "direction: side by side" "$(geo)" "P 1 361 0 341 936 | Q 1 0 0 341 936"
cmd "direction $(id P)"
expect "direction again: rows" "$(geo)" "P 1 0 478 702 458 | Q 1 0 0 702 458"
cmd "place $(id Q) $(id P) up"
expect "place on its own screen, changing nothing: refused" "$(geo) $(warned 'changes nothing')" "P 1 0 478 702 458 | Q 1 0 0 702 458 yes"
cmd "place $(id Q) $(id P) right"
expect "place on its own screen: Q right of P" "$(geo)" "P 1 0 0 341 936 | Q 1 361 0 341 936"
term R
expect "3 windows: R the whole half" "$(geo)" "P 1 361 0 341 458 | Q 1 361 478 341 458 | R 1 0 0 341 936"
cmd "place $(id Q) $(id R) up"
expect "place on its own screen (3): Q above R, R's half splits" "$(geo)" "P 1 361 0 341 936 | Q 1 0 0 341 458 | R 1 0 478 341 458"
cmd "swapin $(id R)"
expect "swapin (3 windows): the pair trades" "$(geo)" "P 1 361 0 341 936 | Q 1 0 478 341 458 | R 1 0 0 341 458"
cmd "swapin $(id P)"
expect "swapin on the whole half: refused" "$(warned 'not in a split half')" "yes"
# screens: scrmove, scrclose (kept layouts follow the move)
swaymsg -q '[shell=".*"] kill'; sleep 1.5
term S1
swaymsg -q "workspace number 2"; term S2a; term S2b
cmd "direction $(id S2a)"
swaymsg -q "workspace number 3"; term S3
expect "screens: 3" "$(cur)" "1 2 3 focus 3"
L2="S2a 2 0 0 341 936 | S2b 2 361 0 341 936"
cmd "scrmove 2 -1"
expect "scrmove 2 -1: screen 2 is first, its layout with it" "$(geo)" "S1 2 0 0 702 936 | S2a 1 361 0 341 936 | S2b 1 0 0 341 936 | S3 3 0 0 702 936"
expect "... the view stays" "$(cur)" "1 2 3 focus 3"
swaymsg -q "workspace 1"; sleep 0.8
expect "... the kept layout survives a rebuild there" "$(geo | tr '|' '\n' | grep -c ' 1 ')" "2"
cmd "swaphalves $(id S2a)"
expect "... its kept layout followed (a rebuild there stays side by side)" "$(geo)" "S1 2 0 0 702 936 | S2a 1 0 0 341 936 | S2b 1 361 0 341 936 | S3 3 0 0 702 936"
swaymsg -q "workspace 3"; sleep 0.5
cmd "scrmove 1 1"
expect "scrmove 1 1: back" "$(geo)" "S1 1 0 0 702 936 | $L2 | S3 3 0 0 702 936"
cmd "scrmove 3 1"
expect "scrmove past the end: nothing" "$(cur)" "1 2 3 focus 3"
cmd "scrclose 2"
expect "scrclose 2: its apps closed, renumbered" "$(geo)" "S1 1 0 0 702 936 | S3 2 0 0 702 936"
expect "... the view stays" "$(cur)" "1 2 focus 2"
cmd "scrclose 2"
expect "scrclose the current screen: the view goes to the one before" "$(cur) $(geo)" "1 focus 1 S1 1 0 0 702 936"
cmd "scrclose 1"
expect "scrclose the last screen: refused" "$(geo) $(warned 'the last screen stays')" "S1 1 0 0 702 936 yes"
# next: where the next new window goes
cmd "next new"; term N1
expect "next new: a new screen after the current one, followed" "$(cur) $(geo_of N1)" "1 2 focus 2 2 0 0 702 936"
swaymsg -q "workspace 1"; sleep 0.5
cmd "next new"; term N2
expect "next new: inserted, the later screens move up" "$(cur) $(geo_of N2) $(geo_of N1)" "1 2 3 focus 2 2 0 0 702 936 3 0 0 702 936"
swaymsg -q "workspace 1"; sleep 0.5
cmd "next here"
swaymsg -q "workspace 3"; sleep 0.5
term H
expect "next here: beside the apps of the screen it was asked on" "$(geo_of H) | $(geo_of S1) | $(cur)" "1 0 0 702 458 | 1 0 478 702 458 | 1 2 3 focus 1"
term H2
expect "next is used once: the next window opens where the view is" "$(geo_of H2 | cut -d' ' -f1)" "1"
term H3
cmd "next here"
expect "next here on a full screen: refused" "$(warned 'next here: screen 1 is full')" "yes"
swaymsg -q "workspace 2"; sleep 0.5
cmd "next new"; sleep 10
term N3
expect "next is forgotten after 10 s" "$(cur) $(geo_of N3 | cut -d' ' -f1)" "1 2 3 focus 2 2"
cmd "next hole"
expect "next hole without an empty screen: refused" "$(warned 'next hole: no empty screen')" "yes"
# Home (a foot stand-in with its app_id) and the empty screen
swaymsg -q '[shell=".*"] kill'; sleep 1.5
swaymsg -q "workspace 1"; term A
swaymsg -q 'exec foot -a glassine-home sleep 600'; sleep 1.2
expect "home: kept in the scratchpad from its map" "$(geo_of glassine-home | cut -d' ' -f1)" "__i3_scratch"
cmd "home show"
expect "home show: over the work area of the current screen" "$(geo_of glassine-home)" "1 0 0 702 936"
expect "... not sway fullscreen (the bar stays above)" "$(fsviews)" "0"
expect "... told to the shell" "$(lasthome "home 1 0")" "home 1 0"
cmd "home hide"
expect "home hide" "$(geo_of glassine-home | cut -d' ' -f1) $(lasthome "home 0 0")" "__i3_scratch home 0 0"
cmd "home toggle"
expect "home toggle: shown" "$(lasthome "home 1 0")" "home 1 0"
cmd "home toggle"
expect "home toggle: hidden" "$(lasthome "home 0 0") $(geo_of A) $(geo_of glassine-home | cut -d' ' -f1)" "home 0 0 1 0 0 702 936 __i3_scratch"
# the on-screen keyboard's zone (no sway event when it changes): Home shown while it is up
# covers the area above it, and the whole work area again once it goes; the same for `full`
wvkbd-mobintl --hidden >/tmp/wvkbd.log 2>&1 &
sleep 1
osk() { sh /o/usr/bin/glassine-osk "$1"; sleep 2.5; }
# "ok" when geometry $1 is $2, the size to one text cell (foot, the stand-in, rounds its size to its cells)
fits() { [ -n "$1" ] && echo "$1 $2" | awk '$1 == $6 && $2 == $7 && $3 == $8 && $4 - $9 < 20 && $9 - $4 < 20 && $5 - $10 < 20 && $10 - $5 < 20 { print "ok" }'; }
osk show
KA=$(geo_of A)
expect "the keyboard up: the work area shrinks" "$(echo "$KA" | awk '$4 == 702 && $5 < 936 && $5 > 0 { print "smaller" }')" "smaller"
cmd "home show"
expect "home shown while the keyboard is up: the area above it" "$(fits "$(geo_of glassine-home)" "$KA")" "ok"
osk hide
expect "... the keyboard goes: Home covers the whole work area again" "$(fits "$(geo_of glassine-home)" "1 0 0 702 936")" "ok"
osk show
expect "... it comes back: Home fits the area above it" "$(fits "$(geo_of glassine-home)" "$KA")" "ok"
osk hide
cmd "home hide"
cmd "full"
osk show
expect "a full window fits the area above the keyboard" "$(fits "$(geo_of A)" "$KA")" "ok"
osk hide
expect "... and the whole work area once it goes" "$(fits "$(geo_of A)" "1 0 0 702 936")" "ok"
cmd "full"
# a turn: the layer surfaces lay themselves out after sway's output event; here the keyboard
# (raised and dropped by signal, its state file untouched) goes right after the turn
cmd "home show"
pkill -USR2 -x wvkbd-mobintl; sleep 1.5
swaymsg -q "output HEADLESS-1 transform 90"; pkill -USR1 -x wvkbd-mobintl; sleep 3
expect "a turn, the keyboard going right after: Home fits the turned work area" "$(fits "$(geo_of glassine-home)" "1 0 0 936 702")" "ok"
swaymsg -q "output HEADLESS-1 transform normal"; sleep 1.5
cmd "home hide"
pkill -x wvkbd-mobintl
swaymsg -q "workspace number 2"; sleep 1
expect "the view goes to a screen with no apps: it stays, empty, under Home" "$(cur) $(geo_of glassine-home) $(lasthome "home 1 2")" "1 2 focus 2 2 0 0 702 936 home 1 2"
swaymsg -q "workspace 1"; sleep 1
expect "... the view leaves it: Home hides, the screen goes" "$(cur) $(geo_of glassine-home | cut -d' ' -f1) $(lasthome "home 0 0")" "1 focus 1 __i3_scratch home 0 0"
swaymsg -q "workspace number 2"; term B
kill_ B
expect "the last window on the current screen closed: it stays, empty, under Home" "$(cur) $(geo_of glassine-home) $(lasthome "home 1 2")" "1 2 focus 2 2 0 0 702 936 home 1 2"
cmd "home hide"
expect "Home hidden: the empty screen goes" "$(cur) $(lasthome "home 0 0")" "1 focus 1 home 0 0"
swaymsg -q "workspace number 2"; term B; kill_ B
term C
expect "an app opening on the empty screen fills it, Home hides" "$(cur) $(geo_of C) $(lasthome "home 0 0")" "1 2 focus 2 2 0 0 702 936 home 0 0"
kill_ C
expect "empty again" "$(lasthome "home 1 2")" "home 1 2"
swaymsg -q "workspace 1"; sleep 1
expect "the view leaves the empty screen: Home hides, the screen goes" "$(cur) $(geo_of glassine-home | cut -d' ' -f1) $(lasthome "home 0 0")" "1 focus 1 __i3_scratch home 0 0"
swaymsg -q "workspace number 2"; term B; kill_ B
cmd "next hole"; term D
expect "next hole: into the empty screen" "$(cur) $(geo_of D) $(lasthome "home 0 0")" "1 2 focus 2 2 0 0 702 936 home 0 0"
cmd "scrclose 2"
expect "scrclose the current screen: no empty screen left behind" "$(cur) $(lasthome "home 0 0")" "1 focus 1 home 0 0"
swaymsg -q "workspace number 2"; term B; kill_ B
cmd "next new"; term N
expect "next new while on the empty screen: a new screen, the empty one goes" "$(cur) $(geo_of N) $(lasthome "home 0 0")" "1 2 focus 2 2 0 0 702 936 home 0 0"
kill_ N
expect "the last window of screen 2 closed: empty under Home" "$(lasthome "home 1 2")" "home 1 2"
cmd "home hide"
kill_ A
expect "the only screen emptied: under Home" "$(cur) $(lasthome "home 1 1")" "1 focus 1 home 1 1"
cmd "home hide"; cmd "home toggle"
expect "... Home stays until an app opens" "$(geo_of glassine-home) $(warned 'Home stays until an app opens')" "1 0 0 702 936 yes"
term F
expect "... an app opens: it fills the screen, Home hides" "$(geo_of F) $(lasthome "home 0 0")" "1 0 0 702 936 home 0 0"
cmd "home show"; term G
expect "a new window while Home is shown hides it" "$(lasthome "home 0 0") $(geo_of G | cut -d' ' -f1)" "home 0 0 1"
# dialogs (FR-055/FR-056): a stand-in app, a foot server (every window of it has its pid),
# maps a tiled main window and a floating dialog with another app_id
swaymsg -q '[shell=".*"] kill'; sleep 1.5
swaymsg -q 'workspace 1'; term X
swaymsg -q 'bar top mode dock'; sleep 1.5
# the work area of the focused workspace, logical px: "x y w h"
wa() { swaymsg -t get_workspaces -r | python3 -c 'import json,sys; r=[w["rect"] for w in json.load(sys.stdin) if w["focused"]][0]; print(r["x"], r["y"], r["width"], r["height"])'; }
WA=$(wa)
expect "dialogs: the bar docked, the work area starts below it" "$(echo "$WA" | awk '$2 > 0 && $4 == 936 - $2 { print "below" }')" "below"
# "ok" when geometry "ws x y w h" ($1) lies inside the work area $2 ("x y w h")
inside() { [ -n "$1" ] && echo "$1 $2" | awk '$2 >= $6 && $3 >= $7 && $2 + $4 <= $6 + $8 && $3 + $5 <= $7 + $9 { print "ok" }'; }
swaymsg -q 'for_window [app_id="^dlg-"] floating enable'
fsrv() { swaymsg -q "exec foot --server=$XDG_RUNTIME_DIR/$1.sock"; sleep 1; }
# footclient window $2 of server $1 (waits for it like term)
fc() { swaymsg -q "exec footclient -s $XDG_RUNTIME_DIR/$1.sock -a $2 sleep 600"; sleep 0.4; for i in $(seq 1 46); do swaymsg -t get_tree | grep -q "\"app_id\": \"$2\"" && break; sleep 0.1; done; sleep 0.8; }
pids() { swaymsg -t get_tree -r | python3 -c '
import json,sys
p={}
def w(n):
    if n.get("shell"): p[n["app_id"]]=n["pid"]
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): w(c)
w(json.load(sys.stdin)); print(len(set(p[a] for a in sys.argv[1:])))' "$@"; }
wsof() { geo_of "$1" | cut -d' ' -f1; }
# the dialog maps first, then the main window, which `next new` sends to a new screen
fsrv a; fc a dlg-a
cmd "next new"; fc a main-a
expect "dialogs: the stand-in's two windows have one pid" "$(pids dlg-a main-a)" "1"
expect "dialog mapped first, its main window sent to a new screen: the dialog goes along" "$(cur) $(wsof main-a) $(wsof dlg-a)" "1 2 focus 2 2 2"
expect "... inside the work area" "$(inside "$(geo_of dlg-a)" "$WA")" "ok"
# the main window moved first, then the dialog maps on another screen (where the view is)
fsrv b
cmd "next new"; fc b main-b
swaymsg -q 'workspace 1'; sleep 0.8
fc b dlg-b
expect "dialog mapped after its main window moved away: to the main window's screen, the view stays" "$(cur) $(wsof main-b) $(wsof dlg-b)" "1 2 3 focus 1 3 3"
expect "... inside the work area" "$(inside "$(geo_of dlg-b)" "$WA")" "ok"
# the overview's New screen and its arrows (place) move a main window with a dialog beside it
fsrv c; fc c main-c; fc c dlg-c
expect "dialog and its main window beside X" "$(wsof main-c) $(wsof dlg-c) $(wsof X)" "1 1 1"
cmd "newscreen $(id main-c) 0"; sleep 1
expect "newscreen: the dialog goes along" "$(wsof main-c) $(wsof dlg-c)" "2 2"
cmd "place $(id main-c) $(id X) right"; sleep 1
expect "place back: the dialog goes along" "$(wsof main-c) $(wsof dlg-c) $(wsof X)" "1 1 1"
fsrv d; cmd "next new"; fc d main-d; fc d dlg-d
expect "a main window with its dialog on a screen of its own" "$(cur) $(wsof main-d) $(wsof dlg-d)" "1 2 3 4 focus 2 2 2"
cmd "place $(id main-d) $(id X) down"; sleep 1
expect "place: the dialog goes along, the emptied screen goes" "$(cur) $(wsof main-d) $(wsof dlg-d) $(wsof X)" "1 2 3 focus 1 1 1 1"
# on its map: sway centres a floating window taller than the work area on the whole output
# (partly under the bar); one that fits it, in the work area
swaymsg -q 'for_window [app_id="^tall-|^small-"] floating enable'
termw() { swaymsg -q "exec foot -a $1 -w $2 sleep 600"; sleep 0.4; for i in $(seq 1 46); do swaymsg -t get_tree | grep -q "\"app_id\": \"$1\"" && break; sleep 0.1; done; sleep 0.8; }
termw tall-1 400x925
G=$(geo_of tall-1)
expect "a dialog mapped taller than the work area: inside it, as tall as it, centred" "$(inside "$G" "$WA") $(echo "$G $WA" | awk '{ dx = $2 + $4 / 2 - ($6 + $8 / 2) } $9 - $5 < 20 && dx * dx <= 4 { print "tall centred" }')" "ok tall centred"
termw small-1 300x300
G=$(geo_of small-1)
expect "a dialog that fits: where sway put it, centred in the work area, its size" "$(echo "$G $WA" | awk '{ dx = $2 + $4 / 2 - ($6 + $8 / 2); dy = $3 + $5 / 2 - ($7 + $9 / 2) } dx * dx <= 4 && dy * dy <= 4 && $4 > 280 && $4 <= 300 && $5 > 280 && $5 <= 300 { print "ok" }')" "ok"
swaymsg -q '[app_id=small-1] move absolute position 100 0'; sleep 1.2
expect "a dialog the user moves under the bar stays there" "$(geo_of small-1 | cut -d' ' -f2-3)" "100 0"
swaymsg -q 'bar top mode invisible'
echo "--- handle history"; grep -n "handle [0-9]: level\|no handles\|placeholder\|closed" /tmp/swayd.log | tail -40
grep -iE "error|fail" /tmp/swayd.log | grep -v "FIFO write failed" | head -5
[ $fail = 0 ] || { echo "--- swayd.log"; grep -n "placeholder\|kill" /tmp/swayd.log | head -20; tail -30 /tmp/swayd.log; }
exit $fail
