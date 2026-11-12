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

# the default time text: "Since HH:MM" now (the text itself is in the pixels; only the run is checked)
GLASSINE_EPD_CONF=$T/epd.conf $P -t white $T/p.pam >/dev/null && ok "default time text" || bad "default time text"

exit $fail
