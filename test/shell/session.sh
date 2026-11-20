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
# Settings: runs once (a second start turns its page, here: a second foot, closed by the test);
# it maps after a moment, as a real app does (a window before its placeholder takes the screen it was started on)
printf '#!/bin/sh\nsleep 0.5\nexec foot -a glassine-settings sleep 600\n' > $HOME/settings-standin; chmod +x $HOME/settings-standin
printf '[Desktop Entry]\nType=Application\nName=Settings\nExec=%s/settings-standin\nIcon=preferences-system-symbolic\nSingleMainWindow=true\n' "$HOME" > $HOME/.local/share/applications/glassine-settings.desktop

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
GLASSINE_SHELL_HITS=$HITS $B/glassine-shell -v >$L 2>&1 &
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
    $B/glassine-shell theme $th; sleep 1
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
    tap bar qs; tap pop qs-power; shot $P-power; key Escape; key Escape
done
$B/glassine-shell theme white; sleep 1

# ---- the Power popup: Hibernate only where hibernation is set up -----------------
hib_on() {      # a stand-in glassine-hibernate and its swap file (deploy.conf's paths)
    mkdir -p /etc/glassine /tmp/hib-ud/media/glassine/swap
    printf '#!/bin/sh\nexit 0\n' > /usr/sbin/glassine-hibernate && chmod 755 /usr/sbin/glassine-hibernate
    printf 'USERDATA_MNT=/tmp/hib-ud\nHIBERNATE_SIZE_MB=1536\n' > /etc/glassine/deploy.conf
    : > /tmp/hib-ud/media/glassine/swap/hibernate
}
rm -f /usr/sbin/glassine-hibernate
tap bar qs; tap pop qs-power
expect "Power without hibernation: 4 rows + 2 restart targets, no Hibernate" "$(nh pop pwr-act) $(nh pop pwr-act 1)" "6 0"
expect "Power: Restart and Restart fully (target 7) side by side" "$(nh pop pwr-act 2) $(nh pop pwr-act 7)" "1 1"
key Escape; key Escape
hib_on
tap bar qs; tap pop qs-power; shot white-$O-power-hibernate
expect "Power with hibernation set up: Hibernate (row 1) shown" "$(nh pop pwr-act) $(nh pop pwr-act 1)" "7 1"
key Escape; key Escape
sed -i 's/^HIBERNATE_SIZE_MB=.*/HIBERNATE_SIZE_MB=0/' /etc/glassine/deploy.conf
tap bar qs; tap pop qs-power
expect "HIBERNATE_SIZE_MB=0: no Hibernate" "$(nh pop pwr-act 1)" "0"
key Escape; key Escape
rm -rf /usr/sbin/glassine-hibernate /etc/glassine/deploy.conf /tmp/hib-ud
# ---- the Start menu target: only where the board's loader takes the request ----------
printf '#!/bin/sh\n[ "$1 $2" = "can-request menu" ]\n' > /usr/sbin/glassine-loader-mark && chmod 755 /usr/sbin/glassine-loader-mark
tap bar qs; tap pop qs-power; sleep 1
expect "Power where the loader takes a menu request: Start menu (target 6) shown, 7 targets in all" "$(nh pop pwr-act 6) $(nh pop pwr-act)" "1 7"
shot white-$O-power-menu
key Escape; key Escape
hib_on
tap bar qs; tap pop qs-power; sleep 1
OH=$(swaymsg -t get_outputs -r | python3 -c 'import json,sys; print(json.load(sys.stdin)[0]["rect"]["height"])')
expect "Power with hibernation and the Start menu: 8 targets, the card inside the screen" "$(nh pop pwr-act) $(awk -v h=$OH '$1 == "pwr-cancel" { print ($5 + $7 + 22 <= h) ? "in" : "out" }' $HITS/pop)" "8 in"
shot white-$O-power-hibernate-menu
key Escape; key Escape
rm -rf /usr/sbin/glassine-hibernate /etc/glassine/deploy.conf /tmp/hib-ud
printf '#!/bin/sh\nexit 3\n' > /usr/sbin/glassine-loader-mark
tap bar qs; tap pop qs-power; sleep 1
expect "Power where the loader takes none (can-request 3): no Start menu" "$(nh pop pwr-act 6)" "0"
key Escape; key Escape
rm -f /usr/sbin/glassine-loader-mark
# ---- Restart's second line: soft or cold, from the restart step (GlassineOS/boot 013) ----
# stand-ins: doas runs the helper; the helper prints the step's record
command -v doas >/dev/null || { printf '#!/bin/sh\n[ "$1" = -n ] && shift\nexec "$@"\n' > /usr/bin/doas; chmod 755 /usr/bin/doas; RM_DOAS=1; }
[ -e /usr/sbin/glassine-power-root ] && cp /usr/sbin/glassine-power-root /tmp/pr.away
for k in 'soft	restart_reason=' 'cold	restart_reason=not-healthy' 'cold	restart_reason=kernel-candidate'; do
    printf '#!/bin/sh\n[ "$1" = restart-check ] && printf "glassine-deploy 1\\ntype=restart\\trestart=%s\\n"\n' "$k" > /usr/sbin/glassine-power-root
    chmod 755 /usr/sbin/glassine-power-root
    tap bar qs; tap pop qs-power; sleep 1
    shot white-$O-power-restart-$(printf '%s' "$k" | sed 's/[^a-z-]*restart_reason=/-/; s/-$//')
    key Escape; key Escape
done
rm -f /usr/sbin/glassine-power-root; [ -e /tmp/pr.away ] && mv /tmp/pr.away /usr/sbin/glassine-power-root
[ -z "${RM_DOAS:-}" ] || rm -f /usr/bin/doas

# ---- checks that change nothing --------------------------------------------------
sm -q "workspace number 2"; sleep 0.8
expect "the bar: Home leftmost, then the screen button" "$(python3 $T/hit.py --all $HITS/bar home | wc -l) $(python3 $T/hit.py $HITS/bar home | cut -d' ' -f1) $(nh bar screen)" "1 31 1"
mark; tap bar home; sent "Home button: home toggle" "home toggle"
mark; $B/glassine-shell home; sleep 0.8; sent "glassine-shell home: home toggle" "home toggle"
mark; $B/glassine-shell toggle launcher; sleep 0.8; sent "toggle launcher (the sway gesture action) = Home" "home toggle"
$B/glassine-shell toggle window; sleep 0.8
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
expect "move: picking an app alone: Close and Immersive around it, no New screen" "$(nh pop ov-closeapp) $(nh pop ov-full) $(nh pop ov-new)" "1 1 0"
expect "move: its arrows: 4 beside the two halves of screen 2, 2 beside screen 3's whole half, none on 4 or its own" "$(nh pop ov-put)" "6"
expect "move: the header's x is undo while moving" "$(nh pop ov-cancel - 0)$(nh pop ov-close)" "10"
tap pop ov-pick - $NOTES
expect "move: tapping another app picks it" "$(nh pop ov-cancel - $NOTES)" "1"
expect "move: Notes (2 on its screen): Close, Immersive, New screen" "$(nh pop ov-closeapp) $(nh pop ov-full) $(nh pop ov-new)" "1 1 1"
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
mark; tap pop ov-full - $READER; sent "move: Immersive: full" "full $READER"
key Escape; sleep 0.5
# immersive (spec 001 FR-019, FR-061, FR-064, FR-012, FR-065): Reader in sway's fullscreen over
# the bar on screen 3; the bar comes up over it on `bar show` for 5 s after its last touch
fs() { swaymsg -t get_tree -r | python3 -c '
import json,sys
def w(n):
    if n.get("shell") and n.get("fullscreen_mode"): print(n["app_id"])
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): w(c)
w(json.load(sys.stdin))'; }
logn() { grep -c "$1" $L; }
sm -q "workspace number 1"; sleep 0.8
mark; $B/glassine-shell bar show; sleep 0.8
expect "bar show without an immersive app: nothing" "$(logn 'no immersive app on this screen') $(logn 'the bar on the overlay layer')" "1 0"
sm -q "[con_id=$((0x$READER))] focus"; sleep 1        # (its screen: the moves above renumbered them)
RS=$(swaymsg -t get_workspaces -r | python3 -c 'import json,sys; w=sorted(json.load(sys.stdin),key=lambda x:x["num"]); print([i+1 for i,x in enumerate(w) if x["focused"]][0])')
NS=$(logn 'snap: [0-9]* windows')
expect "immersive: Reader in sway's fullscreen" "$(fs)" "reader"
QS=$(python3 $T/hit.py $HITS/bar qs)
NV=$(logn 'view 1'); ptr tap $QS; sleep 1
expect "immersive: the bar is under it (a tap there opens nothing)" "$(logn 'view 1')" "$NV"
$B/glassine-shell bar show; sleep 0.8
expect "bar show: the bar on the overlay layer, above the immersive app" "$(logn 'the bar on the overlay layer')" "1"
expect "... its title and close button are the immersive window's" "$(python3 $T/hit.py --all $HITS/bar close | cut -d' ' -f2)" "$READER"
shot white-$O-immersive-bar 0.5
ptr tap $QS; sleep 1
expect "... the raised bar takes a tap: quick settings" "$(view)" "1"
expect "... the popup's backdrop over the immersive app: on the overlay layer" "$(grep 'backdrop on the' $L | tail -1 | sed 's/.*backdrop on the //')" "overlay layer"
sleep 5
expect "... a popup opened from it keeps the bar up (5 s gone)" "$(logn 'back under the immersive app')" "0"
tap pop qs-power
expect "... the popup is above its backdrop: its targets work (Power)" "$(view)" "5"
# a point of the app outside the popup (its targets' box)
OUTP=$(python3 - $HITS/pop $LW $LH <<'PY'
import sys
b=[list(map(float,l.split()[3:])) for l in open(sys.argv[1]) if len(l.split())==7]
x0=min(r[0] for r in b); y0=min(r[1] for r in b); x1=max(r[0]+r[2] for r in b); y1=max(r[1]+r[3] for r in b)
W,H=float(sys.argv[2]),float(sys.argv[3])
for x,y in ((20,H-20),(W-20,H-20),(20,80),(W/2,H-20)):
    if not (x0-10<=x<=x1+10 and y0-10<=y<=y1+10): print("%d %d"%(x,y)); break
PY
)
ptr tap $OUTP; sleep 1
expect "... a tap outside the popup over the immersive app closes it" "$(view)" "0"
expect "... and the bar goes with it (its 5 s are over)" "$(logn 'back under the immersive app')" "1"
NV=$(logn 'view 1'); ptr tap $QS; sleep 1
expect "... the bar under it again: a tap there opens nothing" "$(logn 'view 1')" "$NV"
$B/glassine-shell bar show; sleep 6.5
expect "bar show, not touched: back under after 5 s" "$(logn 'back under the immersive app')" "2"
$B/glassine-shell toggle overview; sleep 1
expect "Overview over the immersive app (the gesture)" "$(view)" "2"
expect "Overview: the immersive screen has stills of its own (FR-065)" "$([ "$(logn 'snap: [0-9]* windows')" -gt "$NS" ] && echo yes)" "yes"
expect "Overview: its title \"<App> immersive · <others> hidden\", the heavy ring (FR-012)" "$(grep "overview: screen $RS: " $L | tail -1 | sed "s/.*overview: screen $RS: //" | grep -cE '^Reader immersive · (Calendar · Mail|Mail · Calendar) hidden \(ring\)$')" "1"
tap pop ov-pick - $READER
expect "Overview: the immersive app's action is Leave immersive" "$(nh pop ov-full 1 $READER) $(grep "Reader's action" $L | tail -1 | sed 's/.*action //')" "1 Leave immersive"
mark; tap pop ov-full 1 $READER; sent "Overview: Leave immersive: full" "full $READER"
sleep 1
expect "... Reader leaves immersive" "$(fs)" ""
key Escape; sleep 0.5
tap bar screen
tap pop ov-pick - $TERM_
mark; tap pop ov-put; sent "move: an arrow: place (the moving app beside another)" "place $TERM_ "
key Escape; sleep 0.5
# a launch of an app with a window thaws it first (a frozen copy never answers a
# second start's D-Bus hand-off): a stand-in glassine-bgprio socket logs the datagrams
rm -f /run/glassine-bgprio.sock /tmp/thaw.log
python3 -c 'import socket
s=socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM); s.bind("/run/glassine-bgprio.sock")
while True: open("/tmp/thaw.log","a").write(s.recv(64).decode()+"\n")' &
TP=$!; sleep 0.5
$B/glassine-shell launch quiet new; sleep 1
expect "launch of an app without a window: no thaw" "$(cat /tmp/thaw.log 2>/dev/null | wc -l)" "0"
$B/glassine-shell launch notes here; sleep 1
expect "launch of an app with a window: its window thawed first" "$(grep -c "^thaw $NOTES$" /tmp/thaw.log 2>/dev/null)" "1"
kill $TP; rm -f /run/glassine-bgprio.sock /tmp/thaw.log
key Escape; sleep 0.5
mark; $B/glassine-shell launch quiet new; sleep 1; sent "launch ID new: next new before the app" "next new"
mark; $B/glassine-shell launch quiet here; sleep 1; sent "launch ID here: next here" "next here"
mark; $B/glassine-shell launch extra new; sleep 3
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
# Settings from quick settings goes where a drawer tap would put it (Appearance >
# Windows: a screen of its own by default), not beside the apps here
sm -q "workspace number 1"; sleep 0.5
mark; tap bar qs; tap pop qs-settings; sleep 3
sent "Settings from quick settings: next new (a screen of its own)" "next new"
expect "... and the placeholder made after it" "$(grep -o 'tile: next new\|placeholder for Settings' $L | tail -2 | cut -c1-4 | tr '\n' ' ')" "tile plac "
expect "... its window alone on its screen" "$(swaymsg -t get_tree -r | python3 -c '
import json,sys
def walk(n,ws=None):
    if n.get("type")=="workspace": ws=n
    if n.get("app_id")=="glassine-settings": print(sum(1 for _ in views(ws)))
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): walk(c,ws)
def views(n):
    if n.get("app_id") or n.get("window_properties"): yield n
    for c in n.get("nodes",[]): yield from views(c)
walk(json.load(sys.stdin))')" "1"
sm -q "workspace number 1"; sleep 0.5
mark; tap bar qs; tap pop qs-settings; sleep 1.5
expect "Settings again while it runs: no new place, its screen comes forward" "$(since | grep -c 'next ') $(swaymsg -t get_workspaces -r | python3 -c 'import json,sys; print([w["name"] for w in json.load(sys.stdin) if w["focused"]][0])' | grep -vc '^1$')" "0 1"
sm -q "[app_id=glassine-settings] kill"; sleep 1
mkdir -p $HOME/.config/glassine; touch $HOME/.config/glassine/app-shared-workspace
sm -q "workspace number 1"; sleep 0.5
mark; tap bar qs; tap pop qs-settings; sleep 3
expect "Settings with apps sharing a screen (the setting off): no next new" "$(since | grep -c 'next new')" "0"
sm -q "[app_id=glassine-settings] kill"; sleep 1
rm -f $HOME/.config/glassine/app-shared-workspace

# the bar (FR-002, FR-005): the screen button right of Home, the focused window's
# title and its close button
sm -q "workspace number 7; exec foot -a closeme sleep 600"; sleep 1.5
CM=$(tree id closeme)
expect "bar: a close button for the focused window" "$(python3 $T/hit.py --all $HITS/bar close | cut -d' ' -f2)" "$CM"
expect "bar: the screen button starts right after Home" "$(python3 - $HITS/bar <<'PY'
import sys
h = {}
for ln in open(sys.argv[1]):
    p = ln.split()
    if len(p) == 7: h[p[0]] = list(map(float, p[3:]))
print(int(h["screen"][0] - (h["home"][0] + h["home"][2])))
PY
)" "6"
send home 1 7; sleep 0.5
expect "bar: no close button while Home is shown" "$(nh bar close)" "0"
send home 0 0; sleep 0.5
tap bar close; sleep 1.5
expect "bar: the close button closes that window" "$(swaymsg -t get_tree -r | grep -c '"app_id": "closeme"')" "0"

# Overview previews follow a change at once: a screen whose windows moved is put
# together from its windows' pictures (no new still needed)
sm -q "workspace number 1"; sleep 2.5      # a still of screen 1 (SNAP_IDLE_MS)
tap bar screen
mark; tap pop ov-pick - $TERM_; tap pop ov-new - $TERM_; sleep 1.5
expect "Overview: a moved window's screen is drawn from its windows' pictures" "$(grep -c "put together from its windows' pictures" $L | awk '{ print ($1 > 0) }')" "1"
key Escape; sleep 0.5

send home 1 3
mark; $B/glassine-shell launch quiet new; sleep 1; sent "launch with an empty screen under Home: next hole" "next hole"
send home 0 0
tap bar screen
C=$(nh pop scr-close)
mark; tap pop scr-close $((C - 1)); sent "close the last screen: scrclose" "scrclose $C"
key Escape

# glassine-swayd down (its restart loop leaves stock sway in charge): a screen of 4 columns
# of 4 windows and a floating one, 17 in all, in the bar and in Overview
kill $SWAYDPID; sleep 0.5
sm -q "workspace number 9"; sleep 0.3
for c in 1 2 3 4; do sm -q "exec foot -a col$c sleep 600"; sleep 0.8; done
for c in 1 2 3 4; do
    sm -q "[app_id=col$c] focus; splitv"
    for r in 2 3 4; do sm -q "exec foot -a c$c$r sleep 600"; sleep 0.8; done
done
sm -q "exec foot -a c17 sleep 600"; sleep 1.2
sm -q "[app_id=c17] floating enable"; sleep 1
expect "17 windows on one screen: 16 tiled" "$(tree count | awk '{ print $NF }')" "16"
tap bar screen
expect "17 windows on one screen: the bar and Overview draw them, the shell still runs" "$(view) $(kill -0 $SHELLPID 2>/dev/null && echo runs)" "2 runs"
key Escape

# ---- the shutdown screen (spec 009, SC-001): last, the shell takes nothing after it --------
# portrait: with the panel daemon's control FIFO (held: "full", then "pause" in a read of its
# own); landscape: without it (a PC: shown, nothing written)
sm -q "workspace number 2"; sleep 0.5
tap bar qs; sleep 0.5                   # a popup open under it
if [ "$O" = landscape ]; then mv /run/glassine-epdd-desktop.ctl /run/epdd-ctl.away; want=shown; else want=held; fi
EL=$(wc -l < /out/epdd-ctl.log)
$B/glassine-shell shutdown-screen "Restarting…"; sleep 2
expect "shutdown screen: the answer is $want" "$(cat $XDG_RUNTIME_DIR/glassine-shutdown-screen.shown 2>/dev/null)" "$want"
ms=$(sed -n 's/.*shutdown screen [a-z]*, \([0-9]*\) ms after the request.*/\1/p' $L | tail -1)
expect "shutdown screen: answered within a second ($ms ms)" "$([ -n "$ms" ] && [ "$ms" -lt 1000 ] && echo yes)" "yes"
grim -t ppm -o HEADLESS-1 /tmp/shut-$O.ppm; grim -o HEADLESS-1 /out/shots/shutdown-$O.png
if r=$(python3 $T/check_shutdown.py /tmp/shut-$O.ppm 2>&1); then ok "shutdown screen: the mark and the line, over the windows, the bar and the popup"
else bad "shutdown screen: the picture" "$r"; fi
mark
ptr tap $(python3 $T/hit.py $HITS/bar screen - - 0 2>/dev/null || echo 100 20); sleep 1
key Escape; send toggle quick; $B/glassine-shell shutdown-screen "Powered off"; sleep 1.5
grim -t ppm -o HEADLESS-1 /tmp/shut2-$O.ppm
expect "shutdown screen: a tap, a key, a request, a second shutdown-screen change nothing" \
    "$(cmp -s /tmp/shut-$O.ppm /tmp/shut2-$O.ppm && echo same)|$(since)" "same|"
expect "shutdown screen: the later request is ignored" "$(grep -c 'the shutdown screen is up, ignored' $L | awk '{ print ($1 >= 2) }')" "1"
if [ "$O" = landscape ]; then
    mv /run/epdd-ctl.away /run/glassine-epdd-desktop.ctl
else
    hold=$(tail -n +$((EL + 1)) /out/epdd-ctl.log | python3 -c '
import sys
reads = [r.strip().split("\n") for r in sys.stdin.read().split("\n--\n")]
f = [i for i, r in enumerate(reads) if "full" in r]
p = [i for i, r in enumerate(reads) if "pause" in r]
pop = [i for i, r in enumerate(reads) if any(l.startswith("popup 0 0 ") for l in r)]
print("ok" if len(f) == 1 and len(p) == 1 and f[0] < p[0] and pop and pop[0] <= f[0] else f"full {f} pause {p} overlay {pop} of {len(reads)}")')
    expect "shutdown screen: the overlay, then \"full\", then \"pause\" in a read of its own" "$hold" "ok"
fi

grep -q 'glassine-shell: ' $L || bad "the shell logged nothing" ""
kill $SWAYPID 2>/dev/null
sleep 0.5
exit $fail
