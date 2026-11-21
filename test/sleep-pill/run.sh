#!/bin/sh
# In the container (test.sh): /r this repository, /ui GlassineOS/ui, /out the shots.
set -eu
make -s -C /ui install O=/tmp/ui-out PREFIX=/usr >/dev/null
make -s -C /r OUT=/tmp CFLAGS="-O2 -Wall -Werror" /tmp/glassine-sleep-pill
P=/tmp/glassine-sleep-pill T=/tmp/t
mkdir -p $T
fail=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fail=1; }

# a panel adapter as a device ships it: a landscape panel mounted turned, so the UI is portrait
printf '# a panel\npanel_width=1872 # px\npanel_height=1404\nrotation=ccw\n' > $T/epd.conf
for v in white editorial stationery; do
    for r in 0 90 180 270; do
        rm -f $T/p.pam
        if GLASSINE_EPD_CONF=$T/epd.conf $P -t $v -r $r -s "Since 20:11" -p /out/$v-$r.png $T/p.pam >/dev/null &&
            python3 /r/test/sleep-pill/check.py $T/p.pam 1404 1872 $r; then
            ok "$v, turn $r"
        else
            bad "$v, turn $r"
        fi
        [ ! -e $T/p.pam.tmp ] || bad "$v, turn $r: a .tmp file is left"
    done
done

# the voice from the theme file: HOME's, else the first account's from uid 1000 (as root)
mkdir -p $T/home/.config/glassine-shell && echo editorial > $T/home/.config/glassine-shell/theme
out=$(HOME=$T/home GLASSINE_EPD_CONF=$T/epd.conf $P -r 0 $T/p.pam)
case $out in *"(editorial, "*) ok "voice from HOME's theme file" ;; *) bad "voice from HOME: $out" ;; esac
adduser -D -h /tmp/deskhome -u 1000 desk 2>/dev/null || true
mkdir -p /tmp/deskhome/.config/glassine-shell && echo stationery > /tmp/deskhome/.config/glassine-shell/theme
out=$(HOME=/nonexistent GLASSINE_EPD_CONF=$T/epd.conf $P -r 0 $T/p.pam)
case $out in *"(stationery, "*) ok "voice from the desktop user's theme file" ;; *) bad "voice from the user: $out" ;; esac
rm -rf /tmp/deskhome/.config
out=$(HOME=/nonexistent GLASSINE_EPD_CONF=$T/epd.conf $P -r 0 $T/p.pam)
case $out in *"(white, "*) ok "no theme file: White" ;; *) bad "no theme file: $out" ;; esac

# rotation none: the panel's own size is the screen
printf 'panel_width=1404\npanel_height=1872\nrotation=none\n' > $T/upright.conf
rm -f $T/p.pam
GLASSINE_EPD_CONF=$T/upright.conf $P -t white -r 0 $T/p.pam >/dev/null &&
    python3 /r/test/sleep-pill/check.py $T/p.pam 1404 1872 0 && ok "rotation=none keeps the panel's size" || bad "rotation=none"
# -W/-H over the adapter; a small screen draws at scale 1
rm -f $T/p.pam
GLASSINE_EPD_CONF=$T/epd.conf $P -t white -W 600 -H 800 -r 0 $T/p.pam >/dev/null &&
    python3 /r/test/sleep-pill/check.py $T/p.pam 600 800 0 && ok "-W/-H win over the adapter" || bad "-W/-H"

# failures: no size, unwritable output: exit != 0, no file, no .tmp
rm -f $T/p.pam
if GLASSINE_EPD_CONF=$T/missing.conf $P -t white $T/p.pam 2>/dev/null; then bad "no adapter: exit 0"; else ok "no adapter: exit != 0"; fi
[ ! -e $T/p.pam ] && [ ! -e $T/p.pam.tmp ] && ok "no adapter: no file" || bad "no adapter: a file is left"
printf 'rotation=ccw\n' > $T/nosize.conf
if GLASSINE_EPD_CONF=$T/nosize.conf $P -t white $T/p.pam 2>/dev/null; then bad "no panel size: exit 0"; else ok "no panel size: exit != 0"; fi
if GLASSINE_EPD_CONF=$T/epd.conf $P -t white $T/nodir/p.pam 2>/dev/null; then bad "unwritable: exit 0"; else ok "unwritable output: exit != 0"; fi
if $P 2>/dev/null; then bad "no OUT: exit 0"; else ok "no OUT: usage"; fi

# hibernation: the same pill with its own words; an unknown mode is a usage error
for v in white editorial stationery; do
    rm -f $T/p.pam
    GLASSINE_EPD_CONF=$T/epd.conf $P -m hibernate -t $v -r 0 -s "Since 20:11" -p /out/$v-hibernate.png $T/p.pam >/dev/null &&
        python3 /r/test/sleep-pill/check.py $T/p.pam 1404 1872 0 && ok "$v, hibernate" || bad "$v, hibernate"
done
cmp -s /out/white-0.png /out/white-hibernate.png && bad "hibernate draws the sleep pill" || ok "hibernate's pill differs from sleep's"
if GLASSINE_EPD_CONF=$T/epd.conf $P -m nap -t white $T/p.pam 2>/dev/null; then bad "-m nap: exit 0"; else ok "-m nap: usage"; fi

# resume (spec 010's notice): the words change, nothing else: the same box as hibernate's,
# for every voice and turn (no time text: -s "" for hibernate's equal)
for v in white editorial stationery; do
    for r in 0 90 180 270; do
        rm -f $T/h.pam $T/r.pam
        GLASSINE_EPD_CONF=$T/epd.conf $P -m hibernate -t $v -r $r -s "" $T/h.pam >/dev/null
        GLASSINE_EPD_CONF=$T/epd.conf $P -m resume -t $v -r $r -p /out/$v-resume-$r.png $T/r.pam >/dev/null &&
            python3 /r/test/sleep-pill/check.py $T/r.pam 1404 1872 $r || { bad "$v, resume, turn $r"; continue; }
        hb=$(sed -n 's/^# at //p;s/^WIDTH //p;s/^HEIGHT //p' $T/h.pam | tr '\n' ' ')
        rb=$(sed -n 's/^# at //p;s/^WIDTH //p;s/^HEIGHT //p' $T/r.pam | tr '\n' ' ')
        [ "$hb" = "$rb" ] && ok "$v, resume, turn $r: hibernate's box ($rb)" || bad "$v, resume, turn $r: box $rb, hibernate's $hb"
    done
done
cmp -s /out/white-resume-0.png /out/white-hibernate.png && bad "resume draws the hibernate pill" || ok "resume's pill differs from hibernate's"

# over a picture (-b, spec 010's start picture): the frame is the upright picture turned
# back by the transform, the picture exact outside the notice; grey stays grey, RGB RGB
python3 - $T <<'PY'
import sys
t = sys.argv[1]
def pic(name, m, w, h):
    n = 1 if m == "P5" else 3
    px = bytearray()
    for y in range(h):
        for x in range(w):
            v = (x * 7 + y * 3 + (x * y) % 13) & 255
            px += bytes([v]) if n == 1 else bytes([v, (v * 5) & 255, 255 - v])
    open(f"{t}/{name}", "wb").write(f"{m}\n{w} {h}\n255\n".encode() + px)
pic("up-grey.pnm", "P5", 1404, 1872)
pic("side-grey.pnm", "P5", 1872, 1404)
pic("up-rgb.pnm", "P6", 600, 800)
pic("side-rgb.pnm", "P6", 800, 600)
PY
for c in "up-grey 0 1404 1872" "side-grey 90 1404 1872" "up-grey 180 1404 1872" "side-grey 270 1404 1872" \
         "up-rgb 0 600 800" "side-rgb 90 600 800" "side-rgb 270 600 800"; do
    set -- $c
    rm -f $T/s.pnm $T/n.pam
    $P -m resume -t editorial -r $2 -W $3 -H $4 $T/n.pam >/dev/null
    if $P -m resume -t editorial -r $2 -b $T/$1.pnm -p /out/start-$1-$2.png $T/s.pnm >/dev/null &&
        python3 /r/test/sleep-pill/check_start.py $T/$1.pnm $2 $T/n.pam $T/s.pnm >/dev/null; then
        ok "over a picture: $1, turn $2"
    else
        bad "over a picture: $1, turn $2: $(python3 /r/test/sleep-pill/check_start.py $T/$1.pnm $2 $T/n.pam $T/s.pnm 2>&1)"
    fi
    [ ! -e $T/s.pnm.tmp ] || bad "over a picture: a .tmp file is left"
done
printf 'P2\n2 2\n255\n0 0 0 0\n' > $T/ascii.pnm
rm -f $T/s.pnm
if $P -m resume -b $T/ascii.pnm $T/s.pnm 2>/dev/null; then bad "over a text PNM: exit 0"; else ok "over a text PNM: exit != 0"; fi
if $P -m resume -b $T/missing.pnm $T/s.pnm 2>/dev/null; then bad "over no picture: exit 0"; else ok "over no picture: exit != 0"; fi
[ ! -e $T/s.pnm ] && ok "no picture: no file" || bad "no picture: a file is left"

# the start picture (glassine-start-picture) with stand-ins for the screen: the turn from
# the turn file, the exits
SP=/r/rootfs/usr/bin/glassine-start-picture
printf '#!/bin/sh\n[ -z "${SHOTRC:-}" ] || exit $SHOTRC\ncp %s/$PICNAME.pnm "$1"\n' $T > $T/shot; chmod +x $T/shot
sp() { GLASSINE_SCREEN_PICTURE=$T/shot GLASSINE_SLEEP_PILL=$P GLASSINE_ROTATION=$T/turn "$SP" "$@"; }
mkdir -p $T/home/.config/glassine-shell && echo editorial > $T/home/.config/glassine-shell/theme
for c in "up-grey none 0" "up-grey 0 0" "side-grey 90 90" "side-grey 270 270" "up-rgb 180 180"; do
    set -- $c
    rm -f $T/turn $T/st.pnm $T/n.pam; [ $2 = none ] || echo $2 > $T/turn
    w=1404 h=1872; case $1 in *rgb) w=600 h=800 ;; esac
    HOME=$T/home $P -m resume -r $3 -W $w -H $h $T/n.pam >/dev/null
    if HOME=$T/home PICNAME=$1 sp $T/st.pnm && python3 /r/test/sleep-pill/check_start.py $T/$1.pnm $3 $T/n.pam $T/st.pnm >/dev/null; then
        ok "start picture: $1, turn file $2"
    else
        bad "start picture: $1, turn file $2: $(python3 /r/test/sleep-pill/check_start.py $T/$1.pnm $3 $T/n.pam $T/st.pnm 2>&1)"
    fi
    ls $T/st.pnm.* >/dev/null 2>&1 && bad "start picture: a temporary file is left"
done
rm -f $T/st.pnm
r=0; SHOTRC=3 PICNAME=up-grey sp $T/st.pnm || r=$?
[ $r = 3 ] && [ ! -e $T/st.pnm ] && ok "start picture: no screen: exit 3, no file" || bad "start picture: no screen: exit $r"
echo 0 > $T/turn; r=0
GLASSINE_SLEEP_PILL=/bin/false GLASSINE_SCREEN_PICTURE=$T/shot GLASSINE_ROTATION=$T/turn PICNAME=up-grey "$SP" $T/st.pnm 2>/dev/null || r=$?
[ $r = 3 ] && [ ! -e $T/st.pnm ] && ! ls $T/st.pnm.* >/dev/null 2>&1 && ok "start picture: no notice: exit 3, no picture" || bad "start picture: no notice: exit $r"
r=0; sp 2>/dev/null || r=$?; [ $r = 2 ] && ok "start picture: no OUT: usage" || bad "start picture: no OUT"

# the default time text: "Since HH:MM" now (the text itself is in the pixels; only the run is checked)
GLASSINE_EPD_CONF=$T/epd.conf $P -t white $T/p.pam >/dev/null && ok "default time text" || bad "default time text"

exit $fail
