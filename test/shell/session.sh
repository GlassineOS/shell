#!/bin/sh
# (see test.sh) one sway session in $ORIENT: four screens with 1, 2, 3 and 4
# windows (glassine-swayd tiles them), then per voice the shots, then the checks
# (white voice): first the ones that change nothing, then the ones whose verb
# glassine-swayd may carry out. Positions are logical px (the output is scale 2).
set -u
T=/r/test/shell B=/tmp/b O=$ORIENT
if [ "$O" = landscape ]; then RES=1872x1404 LW=936 LH=702; else RES=1404x1872 LW=702 LH=936; fi
export HOME=/tmp/home-$O XDG_RUNTIME_DIR=/tmp/rt-$O
RT2=/tmp/rt2-$O                         # glassine-swayd's own runtime dir: its socket sits behind proxy.py
mkdir -p -m 700 $XDG_RUNTIME_DIR $RT2
mkdir -p $HOME/.local/share/applications $HOME/.config/glassine-shell
HITS=$XDG_RUNTIME_DIR/hits VERBS=/out/verbs-$O.log L=/out/shell-$O.log
mkdir -p $HITS; : > $VERBS
fail=0
ok() { echo "ok   $O: $1"; }
bad() { echo "FAIL $O: $1"; [ -n "${2:-}" ] && echo "     $2"; fail=1; }
expect() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "got: $2 | want: $3"; fi; }
shot() { sleep "${2:-1}"; grim -o HEADLESS-1 /out/shots/$1.png; }
sm() { swaymsg "$@"; }
tree() { swaymsg -t get_tree -r | python3 $T/tree.py "$@"; }
send() { python3 -c 'import socket,sys,os; socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM).sendto("\0".join(sys.argv[1:]).encode(),os.environ["XDG_RUNTIME_DIR"]+"/glassine-shell")' "$@"; sleep 0.8; }
swayd() { python3 -c 'import socket,sys; socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM).sendto(" ".join(sys.argv[2:]).encode(),sys.argv[1]+"/glassine-swayd")' $RT2 "$@"; sleep 1; }
ptr() { echo "$*" > $XDG_RUNTIME_DIR/vptr; }
key() { wtype -s 400 -k "$1"; sleep 0.6; }
view() { grep -o 'view [0-9]' $L | tail -1 | cut -d' ' -f2; }
nh() { python3 $T/hit.py --count $HITS/$1 "$2" "${3:--}" "${4:--}"; }
# tap a target of the bar or the popup by what it does: SURF NAME [ARG] [WIN] [N]
tap() {
    p=$(python3 $T/hit.py $HITS/$1 "$2" "${3:--}" "${4:--}" "${5:-0}") || { bad "no target: $*"; return 1; }
    ptr tap $p; sleep 1
}
mark() { VM=$(wc -l < $VERBS); }
since() { tail -n +$((VM + 1)) $VERBS | tr '\n' ';' | sed 's/;$//'; }
# the verbs since mark() include WANT (a substring)
sent() { if since | grep -qF -- "$2"; then ok "$1"; else bad "$1" "verbs: $(since) | want: $2"; fi; }

# apps (desktop ids = foot's app ids)
for a in terminal:Terminal:utilities-terminal-symbolic notes:Notes:accessories-text-editor-symbolic \
         files:Files:system-file-manager-symbolic reader:Reader:accessories-dictionary-symbolic \
         calendar:Calendar:x-office-calendar-symbolic mail:Mail:mail-unread-symbolic \
         browser:Browser:web-browser-symbolic stars:Stars:applications-science-symbolic \
         software:Software:system-software-install-symbolic maps:Maps:find-location-symbolic; do
    id=${a%%:*} r=${a#*:}
    printf '[Desktop Entry]\nType=Application\nName=%s\nExec=foot -a %s sleep 600\nIcon=%s\n' "${r%%:*}" "$id" "${r#*:}" \
        > $HOME/.local/share/applications/$id.desktop
done
# a launch target that opens no window (its placeholder closes by itself)
# a second copy started by `launch ID new` (its own app id: findable in the tree)
printf '[Desktop Entry]\nType=Application\nName=Extra\nExec=foot -a extra sleep 600\nIcon=utilities-terminal-symbolic\n' > $HOME/.local/share/applications/extra.desktop
printf '[Desktop Entry]\nType=Application\nName=Quiet\nExec=true\nIcon=utilities-terminal-symbolic\n' > $HOME/.local/share/applications/quiet.desktop

cat > /tmp/sway-$O.conf <<C
output HEADLESS-1 resolution $RES scale 2 bg #ffffff solid_color
default_border none
default_floating_border none
gaps inner 20
gaps outer -20
smart_gaps on
C
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 $B/sway -c /tmp/sway-$O.conf >/out/sway-$O.log 2>&1 &
SWAYPID=$!
for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1
python3 $T/proxy.py $XDG_RUNTIME_DIR/glassine-swayd $RT2/glassine-swayd $VERBS &
ln -sf $XDG_RUNTIME_DIR/glassine-shell $RT2/glassine-shell
ln -sf $XDG_RUNTIME_DIR/glassine-shell.theme $RT2/glassine-shell.theme
# sway's socket is in the session's runtime dir, swayd's own in RT2: WL first (assignments
# expand left to right: WAYLAND_DISPLAY=$XDG_RUNTIME_DIR/... after XDG_RUNTIME_DIR=$RT2 pointed into RT2)
WL=$XDG_RUNTIME_DIR/wayland-1
# the home app is a setting (FR-029): not glassine-home here, both look it up the same way
export GLASSINE_HOME_APP_ID=test-home
XDG_RUNTIME_DIR=$RT2 WAYLAND_DISPLAY=$WL $B/glassine-swayd -v 2>/out/swayd-$O.log &
SWAYDPID=$!
GLASSINE_SHELL_HITS=$HITS $B/glassine-shell-wl -v >$L 2>&1 &
SHELLPID=$!
mkfifo $XDG_RUNTIME_DIR/vptr
$B/vptr $XDG_RUNTIME_DIR/vptr $LW $LH 2>/out/vptr-$O.log &
sleep 1.5

open() { sm -q "workspace number $1; exec foot -a $2 sleep 600"; sleep 1; }
open 1 terminal
open 2 files; open 2 notes
open 3 mail; open 3 calendar; open 3 reader
open 4 software; open 4 stars; open 4 browser; open 4 maps
sleep 1
expect "four screens with 1, 2, 3 and 4 windows" "$(tree count)" "1 2 3 4"
expect "glassine-swayd draws its handles (it reached sway's Wayland socket)" "$(grep -c 'running (drag handles)' /out/swayd-$O.log)" "1"
TERM_=$(tree id terminal) NOTES=$(tree id notes) READER=$(tree id reader)

# ---- shots, per voice ----------------------------------------------------------
for th in $THEMES; do
    P=$th-$O
    $B/glassine-shell-wl theme $th; sleep 1
    sm -q "workspace number 1"; shot $P-bar-1app
    sm -q "workspace number 3"; shot $P-bar-3apps
    send home 1 5; shot $P-bar-hole
    send home 0 0
    swayd full $READER; sm -q "workspace number 2"; sleep 0.8
    tap bar screen; shot $P-overview-browse
    tap pop ov-close
    swayd full $READER; sleep 0.8
    N=$(tree narrow 3) && { tap bar screen; tap pop ov-pick - $N; shot $P-move-narrow; key Escape; key Escape; }
    W=$(tree wide 2 || tree wide 3) && { tap bar screen; tap pop ov-pick - $W; shot $P-move-wide; key Escape; key Escape; }
    [ "$O" = landscape ] && { tap bar screen; tap pop ov-pick - $NOTES; shot $P-move-landscape; key Escape; key Escape; }
    for s in 2 3 4; do
        sm -q "workspace number $s"; sleep 0.8
        send divider-tap $(tree div $s); shot $P-divider-$s
        key Escape
    done
    sm -q "workspace number 2"; sleep 0.5
done
$B/glassine-shell-wl theme white; sleep 1

# ---- checks that change nothing --------------------------------------------------
sm -q "workspace number 2"; sleep 0.8
expect "the bar: Home leftmost, then the screen button" "$(python3 $T/hit.py --all $HITS/bar home | wc -l) $(python3 $T/hit.py $HITS/bar home | cut -d' ' -f1) $(nh bar screen)" "1 31 1"
mark; tap bar home; sent "Home button: home toggle" "home toggle"
mark; $B/glassine-shell-wl home; sleep 0.8; sent "glassine-shell home: home toggle" "home toggle"
mark; $B/glassine-shell-wl toggle launcher; sleep 0.8; sent "toggle launcher (the sway gesture action) = Home" "home toggle"
$B/glassine-shell-wl toggle window; sleep 0.8
expect "toggle window (the screen menu) is gone: nothing opens" "$(view)" "0"
tap bar screen
expect "the screen button opens Overview" "$(view)" "2"
expect "Overview: 10 app slots for 1+2+3+4 windows (an app alone sits top left)" "$(nh pop ov-pick)" "10"
expect "Overview: earlier/close/later under each screen, faint at the ends" "$(nh pop scr-prev) $(nh pop scr-close) $(nh pop scr-next)" "3 4 3"
expect "Overview: an app alone has its slot top left (left of the preview)" \
    "$(python3 - $HITS/pop $TERM_ <<'PY'
import sys
L=[l.split() for l in open(sys.argv[1])]
g=[l for l in L if l[0]=="ov-goto" and l[1]=="0"][0]
p=[l for l in L if l[0]=="ov-pick" and l[2]==sys.argv[2]][0]
gx,gy,gh=float(g[3]),float(g[4]),float(g[6]); px,py=float(p[3]),float(p[4])+float(p[6])/2
print("left-top" if px<gx and py<gy+gh/2 else "elsewhere %s %s"%(p,g))
PY
)" "left-top"
tap bar screen
expect "the screen button again closes Overview" "$(view)" "0"
# the home app (its own app_id, kept in the scratchpad by glassine-swayd) is not an app of a
# screen: not among the minimized ones, which a minimized app is
sm -q "exec foot -a test-home sleep 600; exec foot -a extra sleep 600"; sleep 2
sm -q "[app_id=extra] move scratchpad"; sleep 1
tap bar screen
expect "the home app (GLASSINE_HOME_APP_ID) is not listed as minimized, a minimized app is" "$(nh pop ov-restore) $(python3 $T/hit.py --all $HITS/pop ov-restore | cut -d' ' -f2)" "1 $(tree id extra)"
expect "... nor among the screens' apps" "$(nh pop ov-pick)" "10"
tap bar screen
sm -q "[app_id=extra] kill; [app_id=test-home] kill"; sleep 1
send home 1 0
mark; tap bar screen; sent "Overview over Home: Home hides" "home hide"
key Escape
send home 0 0
# keyboard (FR-050)
tap bar screen
key Tab; key Return
expect "keyboard: Tab, Enter on the first target (close) closes Overview" "$(view)" "0"
tap bar screen
tap pop ov-pick - $TERM_
expect "move: picking an app alone: Close only around it" "$(nh pop ov-closeapp) $(nh pop ov-full) $(nh pop ov-new)" "1 0 0"
expect "move: its arrows: 4 beside the two halves of screen 2, 2 beside screen 3's whole half, none on 4 or its own" "$(nh pop ov-put)" "6"
expect "move: the header's x is undo while moving" "$(nh pop ov-cancel - 0)$(nh pop ov-close)" "10"
tap pop ov-pick - $NOTES
expect "move: tapping another app picks it" "$(nh pop ov-cancel - $NOTES)" "1"
expect "move: Notes (2 on its screen): Close, Full screen, New screen" "$(nh pop ov-closeapp) $(nh pop ov-full) $(nh pop ov-new)" "1 1 1"
expect "move: arrows: 3 on its own screen (the no-op left out), 4 on screen 1, 2 on screen 3" "$(nh pop ov-put)" "9"
expect "move: no screen buttons while moving" "$(nh pop scr-close)" "0"
tap pop ov-cancel - $NOTES
expect "move: tapping the moving app's own icon cancels" "$(nh pop ov-cancel)$(nh pop ov-close)" "01"
tap pop ov-pick - $NOTES
key Escape
expect "Escape: first cancels the move" "$(view) $(nh pop ov-close)" "2 1"
key Escape
expect "Escape: then closes Overview" "$(view)" "0"
# divider mode
sm -q "workspace number 2"; sleep 0.8
send divider-tap $(tree div 2)
expect "divider, 2 windows: Swap + Direction, no crossing" "$(view) $(nh pop div-swap) $(nh pop div-dir) $(nh pop div-cross)" "4 1 1 0"
key Escape
sm -q "workspace number 3"; sleep 0.8
D3T=$(tree div 3)
for LV in 1 2; do
D3=$(echo "$D3T" | awk -F, -v lv=$LV 'NR > 1 && $6 != 0 { $6 = lv } { print }' OFS=,)
send divider-tap $D3
expect "divider, 3 windows (inner line level $LV): a Swap on each divider, no Direction" "$(nh pop div-swap) $(nh pop div-dir) $(nh pop div-cross)" "2 0 0"
expect "divider, 3 windows (inner line level $LV): the main Swap a quarter along, away from the inner line" "$(python3 - $HITS/pop $D3 <<'PY'
import sys
L=[l.split() for l in open(sys.argv[1])]
d=[w.split(",") for w in sys.argv[3:]]
m=[x for x in d if x[5]=="0"][0]; inn=[x for x in d if x[5]!="0"][0]
s=[l for l in L if l[0]=="div-swap" and l[1]=="0"][0]
x,y,w,h,vert=[int(v) for v in m[:5]]
cx=(float(s[3])+float(s[5])/2)*2; cy=(float(s[4])+float(s[6])/2)*2
f=(cy-y)/h if vert else (cx-x)/w
ix=int(inn[0])+int(inn[2])/2; iy=int(inn[1])+int(inn[3])/2
c=(iy-y)/h if vert else (ix-x)/w
print("ok" if abs(f-(.75 if c<.5 else .25))<.03 else "f=%.2f c=%.2f"%(f,c))
PY
)" "ok"
J=$(grep '^div-swap' $HITS/pop | awk '{print $2}' | while read i; do echo "$D3" | sed -n "$((i + 2))p" | cut -d, -f6 | grep -q "^$LV$" && echo $i; done | head -1)
A3=$(echo "$D3" | awk -F, -v lv=$LV 'NR > 1 && $6 == lv { print $7 }')
mark; tap pop div-swap $J; sent "divider, 3 windows (inner line level $LV): its Swap sends swapin" "swapin $A3"
key Escape
done
sm -q "workspace number 4"; sleep 0.8
D4=$(tree div 4)
send divider-tap $D4
expect "divider, 2x2: three Swaps and the crossing toggle" "$(nh pop div-swap) $(nh pop div-dir) $(nh pop div-cross)" "3 0 1"
expect "divider, 2x2: the toggle sits on the through-line where the inner lines meet" "$(python3 - $HITS/pop $D4 <<'PY'
import sys
L=[l.split() for l in open(sys.argv[1])]
d=[[int(v) if i<6 else v for i,v in enumerate(w.split(","))] for w in sys.argv[3:]]
m=[x for x in d if x[5]==0][0]; inn=[x for x in d if x[5]!=0]
t=[l for l in L if l[0]=="div-cross"][0]
cx=(float(t[3])+float(t[5])/2)*2; cy=(float(t[4])+float(t[6])/2)*2
on=m[0]-1<=cx<=m[0]+m[2]+1 and m[1]-1<=cy<=m[1]+m[3]+1
if m[4]: want=sum(x[1]+x[3]/2 for x in inn)/2; got=cy
else: want=sum(x[0]+x[2]/2 for x in inn)/2; got=cx
print("ok" if on and abs(got-want)<3 else "toggle %.0f,%.0f main %s inner %s"%(cx,cy,m[:4],[x[:4] for x in inn]))
PY
)" "ok"
expect "divider, 2x2: the toggle is 44 px and round" "$(python3 $T/hit.py --all $HITS/pop div-cross | wc -l) $(grep '^div-cross' $HITS/pop | awk '{print $6"x"$7}')" "1 44.0x44.0"
key Escape
expect "Escape closes divider mode" "$(view)" "0"
send divider-tap 2,0 0,0,1404,40,0,0,1,2
expect "the old divider list (8 fields) still opens divider mode" "$(view) $(nh pop div-swap)" "4 1"
key Escape

# ---- checks whose verb glassine-swayd may carry out (the layout may change) ----------
sm -q "workspace number 4"; sleep 0.8
D4=$(tree div 4)
send divider-tap $D4
A=$(python3 $T/hit.py --all $HITS/pop div-cross | cut -d' ' -f2)
mark; tap pop div-cross; sent "the crossing toggle: cross" "cross $A"
I=$(grep '^div-swap' $HITS/pop | awk '{print $2}' | while read i; do echo "$D4" | sed -n "$((i + 2))p" | cut -d, -f6 | grep -q '^0$' && echo $i; done | head -1)
mark; tap pop div-swap $I; sent "Swap on the main divider: swaphalves" "swaphalves "
J=$(grep '^div-swap' $HITS/pop | awk '{print $2}' | grep -vx "$I" | head -1)
mark; tap pop div-swap $J; sent "Swap on an inner divider: swapin" "swapin "
expect "the buttons stay after a swap" "$(view)" "4"
key Escape
sm -q "workspace number 2"; sleep 0.8
send divider-tap $(tree div 2)
mark; tap pop div-dir; sent "Direction (two windows): direction" "direction "
key Escape
tap bar screen
mark; tap pop scr-next 0; sent "screen 1 later: scrmove 1 1" "scrmove 1 1"
expect "the page follows the moved screen" "$(view)" "2"
swayd scrmove 2 -1
key Escape
tap bar screen
tap pop ov-pick - $NOTES
mark; tap pop ov-new - $NOTES; sent "move: New screen: newscreen" "newscreen $NOTES 0"
tap pop ov-pick - $READER
mark; tap pop ov-full - $READER; sent "move: Full screen: full" "full $READER"
key Escape; sleep 0.5
tap bar screen
tap pop ov-pick - $TERM_
mark; tap pop ov-put; sent "move: an arrow: place (the moving app beside another)" "place $TERM_ "
key Escape; sleep 0.5
mark; $B/glassine-shell-wl launch quiet new; sleep 1; sent "launch ID new: next new before the app" "next new"
mark; $B/glassine-shell-wl launch quiet here; sleep 1; sent "launch ID here: next here" "next here"
mark; $B/glassine-shell-wl launch extra new; sleep 3
sent "launch ID new: next new reaches glassine-swayd" "next new"
expect "launch ID new: next new is sent before the placeholder is made" "$(grep -o 'tile: next new\|placeholder for Extra' $L | tail -2 | cut -c1-4 | tr '\n' ' ')" "tile plac "
expect "launch ID new: the new window gets a screen of its own" "$(swaymsg -t get_tree -r | python3 -c '
import json,sys
def walk(n,ws=None):
    if n.get("type")=="workspace": ws=n
    if n.get("app_id")=="extra": print(sum(1 for _ in views(ws)))
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): walk(c,ws)
def views(n):
    if n.get("app_id") or n.get("window_properties"): yield n
    for c in n.get("nodes",[]): yield from views(c)
walk(json.load(sys.stdin))')" "1"
sm -q "[app_id=extra] kill"; sleep 1
send home 1 3
mark; $B/glassine-shell-wl launch quiet new; sleep 1; sent "launch with an empty screen under Home: next hole" "next hole"
send home 0 0
tap bar screen
C=$(nh pop scr-close)
mark; tap pop scr-close $((C - 1)); sent "close the last screen: scrclose" "scrclose $C"
key Escape

# glassine-swayd down (its restart loop leaves stock sway in charge): a screen of 4 columns
# of 4 windows and a window marked full, 17 in all, in the bar and in Overview
kill $SWAYDPID; sleep 0.5
sm -q "workspace number 9"; sleep 0.3
for c in 1 2 3 4; do sm -q "exec foot -a col$c sleep 600"; sleep 0.8; done
for c in 1 2 3 4; do
    sm -q "[app_id=col$c] focus; splitv"
    for r in 2 3 4; do sm -q "exec foot -a c$c$r sleep 600"; sleep 0.8; done
done
sm -q "exec foot -a c17 sleep 600"; sleep 1.2
sm -q "[app_id=c17] floating enable, mark --add _glassine_full"; sleep 1
expect "17 windows on one screen: 16 tiled" "$(tree count | awk '{ print $NF }')" "16"
tap bar screen
expect "17 windows on one screen: the bar and Overview draw them, the shell still runs" "$(view) $(kill -0 $SHELLPID 2>/dev/null && echo runs)" "2 runs"
key Escape

grep -q 'glassine-shell: ' $L || bad "the shell logged nothing" ""
kill $SWAYPID 2>/dev/null
sleep 0.5
exit $fail
