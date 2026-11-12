#!/bin/sh
# (see test.sh) inside the container: /r = the repository (ro), /out = results.
# Portrait, the white voice; positions are logical px (the output is scale 2).
set -u
T=/r/test/shell E=/r/test/e2e B=/tmp/b
mkdir -p $B
rm -f /out/*.log /out/argv-*
cp -a /r/rootfs/usr/share/icons/glassine-mono /usr/share/icons/
make -s -C /ui install O=/tmp/ui-out PREFIX=/usr >/dev/null || exit 1
sh /r/src/wayland/build-shell.sh /r/src $B/glassine-shell || exit 1
sh /r/src/wayland/build-swayd.sh /r/src $B/glassine-swayd || exit 1
X=/usr/share/wlr-protocols/unstable/wlr-virtual-pointer-unstable-v1.xml G=/tmp/g
mkdir -p $G
wayland-scanner client-header $X $G/wlr-virtual-pointer-unstable-v1-client-protocol.h &&
    wayland-scanner private-code $X $G/wlr-virtual-pointer-unstable-v1-protocol.c &&
    gcc -O2 -Wall -I$G -o $B/vptr $T/vptr.c $G/wlr-virtual-pointer-unstable-v1-protocol.c $(pkg-config --cflags --libs wayland-client) || exit 1
cp /usr/bin/sway $B/sway             # the package has cap_sys_nice: no exec in a container
mkfifo /run/glassine-epdd-desktop.ctl
(while :; do cat /run/glassine-epdd-desktop.ctl; done) >/dev/null 2>&1 &

LW=702 LH=936
export HOME=/tmp/home XDG_RUNTIME_DIR=/tmp/rt
RT2=/tmp/rt2                            # glassine-swayd's runtime dir: its socket behind proxy.py (the verbs log)
mkdir -p -m 700 $XDG_RUNTIME_DIR $RT2
mkdir -p $HOME/.local/share/applications
HITS=$XDG_RUNTIME_DIR/hits VERBS=/out/verbs.log L=/out/shell.log SL=/out/swayd.log
mkdir -p $HITS; : > $VERBS
fail=0
ok() { echo "ok   e2e: $1"; }
bad() { echo "FAIL e2e: $1"; [ -n "${2:-}" ] && echo "     $2"; fail=1; }
expect() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "got: $2 | want: $3"; fi; }
sm() { swaymsg "$@"; }
tree() { swaymsg -t get_tree -r | python3 $T/tree.py "$@"; }
ptr() { echo "$*" > $XDG_RUNTIME_DIR/vptr; }
nh() { python3 $T/hit.py --count $HITS/$1 "$2" "${3:--}" "${4:--}"; }
tap() {
    p=$(python3 $T/hit.py $HITS/$1 "$2" "${3:--}" "${4:--}" "${5:-0}") || { bad "no target: $*"; return 1; }
    ptr tap $p; sleep 1
}
key() { wtype -s 400 -k "$1"; sleep 0.6; }
view() { grep -o 'view [0-9]' $L | tail -1 | cut -d' ' -f2; }
mark() { VM=$(wc -l < $VERBS); LM=$(wc -l < $L); }
since() { tail -n +$((VM + 1)) $VERBS | tr '\n' ';' | sed 's/;$//'; }
logsince() { tail -n +$((LM + 1)) $L; }
# a window opens (up to 5 s: foot is slow under load), then the layout settles
waitapp() { for i in $(seq 1 50); do swaymsg -t get_tree | grep -q "\"app_id\": \"$1\"" && break; sleep 0.1; done; sleep 0.8; }
gone() { for i in $(seq 1 50); do swaymsg -t get_tree | grep -q "\"app_id\": \"$1\"" || break; sleep 0.1; done; sleep 0.8; }
term() { sm -q "exec foot -a $1 sleep 600"; waitapp $1; }
kill_() { sm -q "[app_id=$1] kill"; gone $1; }
# "app ws x y w h" per view (logical px), sorted by app
geo() { swaymsg -t get_tree -r | python3 -c '
import json,sys
out=[]
def w(n,ws):
    if n.get("type")=="workspace": ws=n["name"]
    if n.get("shell") and n.get("app_id")!="glassine-home": r=n["rect"]; out.append("%s %s %d %d %d %d" % (n["app_id"],ws,r["x"],r["y"],r["width"],r["height"]))
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): w(c,ws)
w(json.load(sys.stdin),"")
print(" | ".join(sorted(out)))'; }
geo_of() { swaymsg -t get_tree -r | python3 -c '
import json,sys
def w(n,ws):
    if n.get("type")=="workspace": ws=n["name"]
    if n.get("app_id")==sys.argv[1]: r=n["rect"]; print("%s %d %d %d %d" % (ws,r["x"],r["y"],r["width"],r["height"]))
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): w(c,ws)
w(json.load(sys.stdin),"")' "$1"; }
cur() { swaymsg -t get_workspaces -r | python3 -c 'import json,sys; w=json.load(sys.stdin); print(" ".join(x["name"] for x in sorted(w,key=lambda x:x["num"])), "focus", [x["name"] for x in w if x["focused"]][0])'; }
# the shell's last parsed divider list (its log): "N,ORIENT0 LEVEL:A:B ..."; with WANT, waits up to 3 s for it
divs() { for i in $(seq 1 30); do d=$(grep -o 'dividers [0-9],[0-9].*\|divider-tap [0-9],[0-9].*' $L | tail -1 | cut -d' ' -f2-); [ -z "${1:-}" ] && break; echo "$d" | grep -q -- "$1" && break; sleep 0.1; done; echo "$d"; }
levels() { divs "${1:-}" | tr ' ' '\n' | tail -n +2 | cut -d: -f1 | tr '\n' ' ' | sed 's/ $//'; }
# the shell's last Home state (its log), waiting up to 3 s for WANT
homest() { for i in $(seq 1 30); do h=$(grep -o 'home [01] hole [0-9]*' $L | tail -1); [ "$h" = "$1" ] && break; sleep 0.1; done; echo "$h"; }
# tap glassine-swayd's handle of LEVEL on screen NUM (its centre, from sway's tree: the gap between the tiles)
taphandle() {
    p=$(tree div $1 | awk -F, -v lv=$2 'NR > 1 && $6 == lv { printf "%d %d\n", ($1 + $3 / 2) / 2, ($2 + $4 / 2) / 2; exit }')
    [ -n "$p" ] || { bad "no line of level $2 on screen $1"; return 1; }
    ptr tap $p; sleep 1.2
}
# the index (the shell's hit ARG) of the divider of LEVEL in the shell's list
dividx() { divs | tr ' ' '\n' | tail -n +2 | awk -F: -v lv=$1 '$1 == lv { print NR - 1; exit }'; }

# apps (desktop ids = foot's app ids); Home's stand-in is foot with Home's app_id
for id in P Q R S U V W X Y extra; do
    printf '[Desktop Entry]\nType=Application\nName=%s\nExec=foot -a %s sleep 600\nIcon=utilities-terminal-symbolic\n' "$id" "$id" \
        > $HOME/.local/share/applications/$id.desktop
done
# the argument's apps: argrec records its argv (one word a line) and becomes a window
cat > $B/argrec <<'S'
#!/bin/sh
id=$1; shift
for a in "$@"; do printf '%s\n' "$a"; done > /out/argv-$id
exec foot -a "$id" sleep 600
S
chmod +x $B/argrec
app() { printf '[Desktop Entry]\nType=Application\nName=%s\nExec=%s\nIcon=utilities-terminal-symbolic\n' "$1" "$2" > $HOME/.local/share/applications/$1.desktop; }
app recf "$B/argrec recf --open %f --end"
app recn "$B/argrec recn"
app recu "$B/argrec recu @@u %U @@"
app recq "\"$B/argrec\" recq \"two words\" %u"
# Exec lines that can't be made into a command line (the launch must start nothing and send no `next`):
# an unbalanced quote; only a field code (no program word, FR-046)
app badq "$B/argrec badq \"unbalanced"
app badf "%f"
# Exec lines that are made into a command line but can't run: the reason is logged (a bare VAR=x prog
# worked under sh -c and is a program called "VAR=x" now)
app badx "/tmp/no-such-program --flag"
app badv "FOO=1 $B/argrec badv"
# Settings runs once (SingleMainWindow, as its own desktop file): argsingle hands a second
# start's argv over (records it) and exits, as a GApplication does
cat > $B/argsingle <<'S'
#!/bin/sh
id=$1; shift
if [ -e /tmp/running-$id ]; then for a in "$@"; do printf '%s\n' "$a"; done > /out/handover-$id; exit 0; fi
: > /tmp/running-$id
for a in "$@"; do printf '%s\n' "$a"; done > /out/argv-$id
exec foot -a "$id" sleep 600
S
chmod +x $B/argsingle
app glassine-settings "$B/argsingle glassine-settings"
echo SingleMainWindow=true >> $HOME/.local/share/applications/glassine-settings.desktop

cat > /tmp/sway.conf <<C
output HEADLESS-1 resolution 1404x1872 scale 2 bg #ffffff solid_color
default_border none
default_floating_border none
gaps inner 20
gaps outer -20
smart_gaps on
C
WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 $B/sway -c /tmp/sway.conf >/out/sway.log 2>&1 &
SWAYPID=$!
for i in $(seq 1 50); do ls $XDG_RUNTIME_DIR/sway-ipc.* >/dev/null 2>&1 && break; sleep 0.2; done
export SWAYSOCK=$(ls $XDG_RUNTIME_DIR/sway-ipc.*) WAYLAND_DISPLAY=wayland-1
python3 $T/proxy.py $XDG_RUNTIME_DIR/glassine-swayd $RT2/glassine-swayd $VERBS &
ln -sf $XDG_RUNTIME_DIR/glassine-shell $RT2/glassine-shell
ln -sf $XDG_RUNTIME_DIR/glassine-shell.theme $RT2/glassine-shell.theme
WAYLAND_DISPLAY=/tmp/rt/wayland-1 XDG_RUNTIME_DIR=$RT2 $B/glassine-swayd -v 2>$SL &
GLASSINE_SHELL_HITS=$HITS $B/glassine-shell -v >$L 2>&1 &
mkfifo $XDG_RUNTIME_DIR/vptr
$B/vptr $XDG_RUNTIME_DIR/vptr $LW $LH 2>/out/vptr.log &
sleep 1.5
sm -q "exec foot -a glassine-home sleep 600"; sleep 1.5
expect "Home's stand-in waits in the scratchpad" "$(geo_of glassine-home | cut -d' ' -f1)" "__i3_scratch"

sm -q "workspace number 1"; term P; term Q
sm -q "workspace number 2"; term R; term S; term U
sm -q "workspace number 3"; term V; term W; term X; term Y
expect "three screens with 2, 3 and 4 windows" "$(tree count)" "2 3 4"

# ---- divider mode from glassine-swayd's own list -------------------------------------
sm -q "workspace number 1"; sleep 1
mark; taphandle 1 0
expect "2 windows: a tap on swayd's handle opens divider mode" "$(view)" "4"
expect "2 windows: swayd's list has the main line only" "$(levels)" "0"
expect "2 windows: Swap and Direction" "$(nh pop div-swap) $(nh pop div-dir) $(nh pop div-cross)" "1 1 0"
G0=$(geo)
tap pop div-dir
expect "Direction reaches swayd: side by side" "$(geo)" "P 1 361 52 341 884 | Q 1 0 52 341 884 | R 2 361 504 341 432 | S 2 0 504 341 432 | U 2 0 52 702 432 | V 3 361 504 341 432 | W 3 0 504 341 432 | X 3 361 52 341 432 | Y 3 0 52 341 432"
expect "... swayd's new list reaches the shell (the main line vertical)" "$(divs '^2,1' | cut -d' ' -f1)" "2,1"
tap pop div-swap 0
expect "Swap (2 windows) reaches swayd: the two trade" "$(geo | cut -d'|' -f1-2)" "P 1 0 52 341 884 | Q 1 361 52 341 884 "
expect "the buttons stay after a swap" "$(view)" "4"
key Escape
expect "Escape closes divider mode" "$(view)" "0"

sm -q "workspace number 2"; sleep 1
taphandle 2 2
expect "3 windows: divider mode" "$(view)" "4"
expect "3 windows: swayd's list: the main line and the pair's line at level 2" "$(levels)" "0 2"
expect "3 windows: a Swap on each line, no Direction, no crossing" "$(nh pop div-swap) $(nh pop div-dir) $(nh pop div-cross)" "2 0 0"
expect "3 windows: the pair before (R right of S)" "$(geo_of R) $(geo_of S)" "2 361 504 341 432 2 0 504 341 432"
mark; tap pop div-swap "$(dividx 2)"
expect "Swap on the pair's line: swapin, the pair trades" "$(since | grep -o 'swapin [0-9a-f]*' | cut -d' ' -f1) $(geo_of R) $(geo_of S)" "swapin 2 0 504 341 432 2 361 504 341 432"
mark; tap pop div-swap "$(dividx 0)"
expect "Swap on the main line: swaphalves, the pair goes on top" "$(since | grep -o swaphalves) $(geo_of U) $(geo_of R)" "swaphalves 2 0 504 702 432 2 0 52 341 432"
expect "... swayd's list follows: the pair is half 0 now (level 1)" "$(levels '0:.* 1:')" "0 1"
key Escape

sm -q "workspace number 3"; sleep 1
G4=$(geo)
taphandle 3 0
expect "4 windows: swayd's list: the main line and both inner lines" "$(levels)" "0 1 2"
expect "4 windows: three Swaps and the crossing toggle" "$(nh pop div-swap) $(nh pop div-dir) $(nh pop div-cross)" "3 0 1"
O0=$(divs | cut -d' ' -f1)
mark; tap pop div-cross
expect "the crossing toggle reaches swayd: cross" "$(since | grep -o 'cross [0-9a-f]*' | cut -d' ' -f1)" "cross"
expect "... every window keeps its quarter" "$(geo)" "$G4"
expect "... the other axis runs through (swayd's list, orientation flipped)" "$(divs | cut -d' ' -f1)" "$([ "$O0" = 4,0 ] && echo 4,1 || echo 4,0)"
mark; tap pop div-swap "$(dividx 1)"
expect "Swap on an inner line of 2x2: swapin, the tree changes" "$(since | grep -o swapin) $([ "$(geo)" != "$G4" ] && echo changed)" "swapin changed"
key Escape

# ---- Home and the empty screen ---------------------------------------------------------
sm -q "workspace number 1"; sleep 1
mark; tap bar home
expect "the bar's Home button: swayd shows Home over the work area" "$(geo_of glassine-home) $(homest 'home 1 hole 0')" "1 0 52 702 884 home 1 hole 0"
expect "... the button looks pressed (one Home target on the bar)" "$(nh bar home)" "1"
tap bar home
expect "the Home button again: hidden" "$(geo_of glassine-home | cut -d' ' -f1) $(homest 'home 0 hole 0')" "__i3_scratch home 0 hole 0"
# the sway session's gesture requests (sway runs these): Home, Overview and the old names
$B/glassine-shell home; sleep 1
expect "gesture request home: Home shown" "$(homest 'home 1 hole 0')" "home 1 hole 0"
$B/glassine-shell toggle launcher; sleep 1
expect "the old toggle launcher: Home hidden" "$(homest 'home 0 hole 0')" "home 0 hole 0"
$B/glassine-shell toggle overview; sleep 1
expect "gesture request toggle overview: Overview" "$(view)" "2"
key Escape
$B/glassine-shell toggle workspaces; sleep 1
expect "the old toggle workspaces: Overview" "$(view)" "2"
key Escape
mark; $B/glassine-shell launch extra new; waitapp extra
expect "launch ID new: next new reaches swayd before the app" "$(since | grep -o 'next new')" "next new"
expect "launch ID new: a new screen after the current one, the app alone there" "$(cur) $(geo_of extra)" "1 2 3 4 focus 2 2 0 52 702 884"
kill_ extra
expect "the last app on the screen closes: the screen stays, empty, under Home" "$(cur) $(geo_of glassine-home) $(homest 'home 1 hole 2')" "1 2 3 4 focus 2 2 0 52 702 884 home 1 hole 2"
mark; $B/glassine-shell launch extra new; waitapp extra
expect "launch from Home with an empty screen: next hole, the app fills it, Home hides" \
    "$(since | grep -o 'next hole') $(cur) $(geo_of extra) $(homest 'home 0 hole 0')" "next hole 1 2 3 4 focus 2 2 0 52 702 884 home 0 hole 0"
kill_ extra
expect "empty again" "$(homest 'home 1 hole 2')" "home 1 hole 2"
tap bar home
expect "Home hidden by its button: the empty screen goes" "$(cur) $(homest 'home 0 hole 0')" "1 2 3 focus 1 home 0 hole 0"

# ---- the launch argument reaches the app ----------------------------------------------
sm -q "workspace number 1"; sleep 0.8
# a shell would run $(id) and `id` (the name would change and the file not be found)
mkdir -p '/tmp/data/a dir'
F="/tmp/data/a dir/b \$(id) \`id\`; \"q\" 'it''s' & * ~.epub"
: > "$F"
argv() { for i in $(seq 1 50); do [ -s /out/argv-$1 ] && break; sleep 0.1; done; tr '\n' '|' < /out/argv-$1 2>/dev/null; }
launch_arg() { rm -f /out/argv-$1; mark; $B/glassine-shell launch "$@"; waitapp $1; }
launch_arg recf here "$F"
expect "%f: the file in its place, one word" "$(argv recf)" "--open|$F|--end|"
expect "... next here before the app" "$(since | grep -o 'next here')" "next here"
kill_ recf
launch_arg recn here "$F"
expect "no field code: the file appended" "$(argv recn)" "$F|"
kill_ recn
launch_arg recu here "$F"
expect "%U (a Flatpak's @@u %U @@): the file between the markers" "$(argv recu)" "@@u|$F|@@|"
kill_ recu
launch_arg recq here "$F"
expect "a quoted Exec line: its words kept, %u the file" "$(argv recq)" "two words|$F|"
kill_ recq
launch_arg recf here
expect "no argument: the field code gives nothing" "$(argv recf)" "--open|--end|"
kill_ recf
launch_arg glassine-settings here updates
expect "Settings: the page as its argument" "$(argv glassine-settings)" "updates|"
kill_ glassine-settings; rm -f /tmp/running-glassine-settings
launch_arg glassine-settings here network#1
expect "Settings: an alias with a section is a page" "$(argv glassine-settings)" "network#1|"
kill_ glassine-settings; rm -f /tmp/running-glassine-settings
# refused: nothing starts, no `next`, a log line
: > /tmp/data/ok.epub; chmod +x /tmp/data/ok.epub    # (it could be run)
for bad_ in "recn here book.epub:relative" "recn here /tmp/data/none.epub:nonexistent" \
            "glassine-settings here ../x:not a page" "glassine-settings here /etc/passwd:a file for Settings" \
            "recn over /tmp/data/ok.epub:neither new nor here" "recn here /tmp/data/ok.epub /tmp/data/ok.epub:two arguments" \
            "nosuchapp here /tmp/data/ok.epub:no such app" \
            "badq new:an Exec line with an unbalanced quote" "badf new:an Exec line of only a field code" \
            "recn here /tmp/data:a directory" "recn here /dev/null:a device" \
            "glassine-settings here nope:an unknown Settings page" "glassine-settings here packages#1:a section of a sub-page" \
            "glassine-settings here power#:a section without a number" \
            "badf here /tmp/data/ok.epub:an Exec line whose first word is the field code (the file would be the command)"; do
    a=${bad_%%:*} why=${bad_#*:}
    rm -f /out/argv-*; mark
    # shellcheck disable=SC2086
    $B/glassine-shell launch $a; sleep 1.5
    expect "refused ($why): nothing started, no next, logged" \
        "$(ls /out/argv-* 2>/dev/null | wc -l) $(since | grep -c next) $(logsince | grep -c 'refused')" "0 0 1"
done
expect "after the refusals: screen 1 as it was" "$(tree count)" "2 3 4"

# ---- an app that runs once and is open (FR-049) ------------------------------------------
focused() { swaymsg -t get_tree -r | python3 -c '
import json,sys
def w(n):
    if n.get("focused"): print(n.get("app_id") or n.get("name"))
    for c in n.get("nodes",[])+n.get("floating_nodes",[]): w(c)
w(json.load(sys.stdin))'; }
handover() { for i in $(seq 1 30); do [ -s /out/handover-$1 ] && break; sleep 0.1; done; tr '\n' '|' < /out/handover-$1 2>/dev/null; }
sm -q "workspace number 1"; sleep 0.5
launch_arg glassine-settings here
expect "Settings started beside the two on screen 1" "$(tree count)" "3 3 4"
sm -q "workspace number 3"; sleep 0.8
rm -f /out/handover-*; mark
$B/glassine-shell launch glassine-settings new updates; sleep 2
expect "Settings open, launch new updates: no next, no placeholder, no new screen" \
    "$(since | grep -c next) $(logsince | grep -c 'placeholder for') $(tree count)" "0 0 3 3 4"
expect "... its screen shown, its window focused" "$(cur) $(focused)" "1 2 3 focus 1 glassine-settings"
expect "... the running Settings got the page" "$(handover glassine-settings)" "updates|"
sm -q "workspace number 2"; sleep 0.8
rm -f /out/handover-*; mark
$B/glassine-shell launch glassine-settings here power; sleep 2
expect "Settings open, launch here power: focused the same way" \
    "$(since | grep -c next) $(tree count) $(cur) $(focused) $(handover glassine-settings)" "0 3 3 4 1 2 3 focus 1 glassine-settings power|"
sm -q "workspace number 1"; sleep 0.5
# Home shown over an empty screen, Settings open on another one: the launch hides Home (swayd drops the empty
# screen) and the shell goes to Settings' screen; the two requests go to different sockets
mark; $B/glassine-shell launch extra new; waitapp extra
kill_ extra
expect "an empty screen under Home, Settings open on screen 1" "$(cur) $(homest 'home 1 hole 2')" "1 2 3 4 focus 2 home 1 hole 2"
rm -f /out/handover-*; mark
$B/glassine-shell launch glassine-settings new updates; sleep 2.5
expect "Home over an empty screen, launch new updates: Settings' screen shown, its window focused, no new screen" \
    "$(cur) $(focused) $(since | grep -c next)" "1 2 3 focus 1 glassine-settings 0"
expect "... Home hidden, the empty screen gone" "$(homest 'home 0 hole 0') $(tree count)" "home 0 hole 0 3 3 4"
expect "... the running Settings got the page" "$(handover glassine-settings)" "updates|"
sm -q "workspace number 1"; sleep 0.5
mark; $B/glassine-shell launch extra new; waitapp extra
mark; $B/glassine-shell launch extra new; sleep 3
expect "an app that doesn't run once, open already: a second window on a new screen" \
    "$(since | grep -o 'next new') $(cur) $(geo | tr '|' '\n' | grep -c '^ *extra ')" "next new 1 2 3 4 5 focus 3 2"
sm -q "workspace number 1"; sleep 0.8
kill_ extra
kill_ glassine-settings; rm -f /tmp/running-glassine-settings
expect "the extra screens went with their apps" "$(cur) $(tree count)" "1 2 3 focus 1 2 3 4"

# ---- an Exec line that can't run says why (the child's errno) ---------------------------------
for bad_ in "badx:/tmp/no-such-program" "badv:FOO=1"; do
    a=${bad_%%:*} w=${bad_#*:}
    rm -f /out/argv-*; mark
    $B/glassine-shell launch $a here; sleep 1.5
    expect "an Exec that can't run ($w): logged with the reason, nothing started" \
        "$(logsince | grep -c "can't run \"$w\": No such file or directory") $(ls /out/argv-* 2>/dev/null | wc -l)" "1 0"
    sleep 6                            # the placeholder of a process that is gone leaves by itself (PH_GONE_S)
    expect "... its placeholder went" "$(tree count)" "2 3 4"
done

kill $SWAYPID 2>/dev/null
sleep 0.5
[ $fail = 0 ] && echo "e2e test: all checks passed" || { echo "e2e test: FAILED (see above)"; tail -20 $SL; }
exit $fail
