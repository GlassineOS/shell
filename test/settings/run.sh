#!/bin/sh
# (see test.sh) inside the glassine-settingstest container: /src = repo (ro), /out = results
set -u
P=/src
mkdir -p /out/fonts /out/shots
[ "$(ls /out/fonts | wc -l)" -ge 6 ] || sh $P/fonts/fetch-fonts.sh /out/fonts
mkdir -p /usr/share/fonts/glassine && cp /out/fonts/* /usr/share/fonts/glassine/ && fc-cache -f >/dev/null
cp -a $P/rootfs/usr/share/icons/glassine-mono /usr/share/icons/ && gtk-update-icon-cache -q -f /usr/share/icons/glassine-mono
S=$P/rootfs/usr/bin/glassine-settings
fail=0
# the design's tiled windows: the panel 702x884, halves with a 2 px gap
# (land: the panel turned, below the bar; opt-in: WINS="... land")
size() { case $1 in full) echo 702x884 ;; column) echo 350x884 ;; row) echo 702x441 ;; quarter) echo 350x441 ;; land) echo 936x650 ;; lhalf) echo 462x650 ;; esac; }
for t in ${THEMES:-white editorial stationery}; do
    for w in ${WINS:-full column row quarter}; do
        n=$t; [ $w = full ] || n=$t-$w
        for pg in ${PAGES:-wifi wifi-net wifi-new bt bt-dev bt-add sound light light-range eink render-apps apps origins app power keyboard gestures appearance time time-zone time-city storage about admin-auth}; do
            extra=; [ "$pg" = wifi-new ] && extra="--pw kranich"; [ "$pg" = admin-auth ] && extra="--pw secret --auth-err"
            python3 $S --demo --render /out/shots/$n-$pg.png --page $pg --theme $t --size $(size $w) $extra 2>/out/err.txt || { echo "FAIL $n $pg"; cat /out/err.txt; fail=1; }
        done
    done
    # glassine-nm-agent's deep link (wifi-ask SSID [--eap]): a refused WPA2 password, an 802.1X login
    python3 $S --demo --render /out/shots/$t-wifi-ask.png --page wifi --ask "FRITZ!Box 7590 KL" --pw kranich --theme $t 2>/out/err.txt || { echo "FAIL $t wifi-ask"; cat /out/err.txt; fail=1; }
    python3 $S --demo --render /out/shots/$t-wifi-ask-eap.png --page wifi --ask eduroam --eap --user sample@example.org --pw x --theme $t 2>/out/err.txt || { echo "FAIL $t wifi-ask-eap"; cat /out/err.txt; fail=1; }
    # the design's per-app pages (6b Firefox ESR full, 6c KOReader in the left half)
    python3 $S --demo --render /out/shots/$t-column-app-koreader.png --page app --app KOReader --theme $t --size $(size column) 2>/out/err.txt || { echo "FAIL $t app KOReader"; cat /out/err.txt; fail=1; }
    python3 $S --demo --render /out/shots/$t-app-marble.png --page app --app Marble --theme $t 2>/out/err.txt || { echo "FAIL $t app Marble"; cat /out/err.txt; fail=1; }
    # per-app render profiles: GNOME Software on "GPU, short jobs", in the left half
    python3 $S --demo --render /out/shots/$t-column-app-software.png --page app --app Software --theme $t --size $(size column) 2>/out/err.txt || { echo "FAIL $t app Software"; cat /out/err.txt; fail=1; }
    # Content: the demo OpenMW is Moving, KOReader Pages
    python3 $S --demo --render /out/shots/$t-app-openmw.png --page "app#0" --app OpenMW --theme $t 2>/out/err.txt || { echo "FAIL $t app OpenMW"; cat /out/err.txt; fail=1; }
    python3 $S --demo --render /out/shots/$t-app-koreader-content.png --page "app#0" --app KOReader --theme $t 2>/out/err.txt || { echo "FAIL $t app KOReader content"; cat /out/err.txt; fail=1; }
    python3 $S --demo --render /out/shots/$t-app-koreader.png --page app --app KOReader --theme $t 2>/out/err.txt || { echo "FAIL $t app KOReader"; cat /out/err.txt; fail=1; }   # Uninstall
    python3 $S --demo --render /out/shots/$t-app-software.png --page app --app Software --theme $t 2>/out/err.txt || { echo "FAIL $t app Software"; cat /out/err.txt; fail=1; }   # a system app: no Uninstall
    # the choice lists (a gesture's action, the power key) and a
    # collapsed section's own page (the quarter window folds the e-ink page)
    python3 $S --demo --render /out/shots/$t-pick-g3Up.png --page gestures --pick g3Up --theme $t 2>/out/err.txt || { echo "FAIL $t pick g3Up"; cat /out/err.txt; fail=1; }
    python3 $S --demo --render /out/shots/$t-pick-g2Tap.png --page gestures --pick g2Tap --theme $t 2>/out/err.txt || { echo "FAIL $t pick g2Tap"; cat /out/err.txt; fail=1; }
    python3 $S --demo --render /out/shots/$t-column-pick-pkey.png --page power --pick pkey --theme $t --size $(size column) 2>/out/err.txt || { echo "FAIL $t pick pkey"; cat /out/err.txt; fail=1; }
    python3 $S --demo --render /out/shots/$t-quarter-eink-1.png --page "eink#1" --theme $t --size $(size quarter) --markers /out/shots/$t-quarter-eink-1.json 2>/out/err.txt || { echo "FAIL $t eink#1"; cat /out/err.txt; fail=1; }
    # the sway session's keyboard shortcuts (keys.conf's "## " labels): the Keyboard page's section
    python3 $S --demo --render /out/shots/$t-keyboard-shortcuts.png --page "keyboard#3" --theme $t 2>/out/err.txt || { echo "FAIL $t keyboard#3"; cat /out/err.txt; fail=1; }
    # the e-ink page with another dither pattern: its pattern row
    GLASSINE_DEMO_DOTS=bayer python3 $S --demo --render /out/shots/$t-eink-dots.png --page "eink#1" --theme $t 2>/out/err.txt || { echo "FAIL $t eink-dots"; cat /out/err.txt; fail=1; }
    # an app's own dither pattern (the demo's Stellarium: dizzy)
    GLASSINE_DEMO_DOTS=blue python3 $S --demo --render /out/shots/$t-app-dots.png --page "app#0" --app Stellarium --theme $t 2>/out/err.txt || { echo "FAIL $t app-dots"; cat /out/err.txt; fail=1; }
    GLASSINE_DEMO_DOTS=blue python3 $S --demo --render /out/shots/$t-column-app-dots.png --page "app#0" --app Stellarium --theme $t --size $(size column) 2>/out/err.txt || { echo "FAIL $t column app-dots"; cat /out/err.txt; fail=1; }
    # Apps > Packages, the design's pkPreset states (PKS= to choose)
    for pk in ${PKS:-home kb uninstall-KOReader uninstall-Calendar uninstall-Transit uninstall-Gnumeric uninstall-done-Calendar search search-lib detail detail-installed ack install install-p2 remove progress ok notes error output changes upd-never upd-list upd-none}; do
        python3 $S --demo --render /out/shots/$t-pk-$pk.png --page apps --pk $pk --theme $t 2>/out/err.txt || { echo "FAIL $t pk $pk"; cat /out/err.txt; fail=1; }
    done
    python3 $S --demo --render /out/shots/$t-column-pk-search.png --page apps --pk search --theme $t --size $(size column) 2>/out/err.txt || { echo "FAIL $t column pk search"; cat /out/err.txt; fail=1; }
    python3 $S --demo --render /out/shots/$t-column-pk-install.png --page apps --pk install --theme $t --size $(size column) 2>/out/err.txt || { echo "FAIL $t column pk install"; cat /out/err.txt; fail=1; }
    for sp in forget-net:wifi-net pair:bt-add error:wifi pkg-error:apps; do
        sh=${sp%%:*}; pg=${sp#*:}
        python3 $S --demo --render /out/shots/$t-sheet-$sh.png --page $pg --theme $t --sheet $sh 2>/out/err.txt || { echo "FAIL $t sheet $sh"; cat /out/err.txt; fail=1; }
    done
done
# keys.conf's labels parse: every "## " line gives one entry (none lost to a stray line)
n=$(cd $P/rootfs/usr/lib/glassine && python3 -c 'from settings.backend import sway_shortcuts; print(len(sway_shortcuts()))')
want=$(grep -c '^## ' $P/rootfs/usr/share/glassine/sway/keys.conf)
[ "$n" = "$want" ] && [ "$n" -ge 15 ] && echo "ok   keyboard shortcuts: $n from keys.conf" || { echo "FAIL keyboard shortcuts: $n parsed, $want labels"; fail=1; }
python3 /t/tab.py $P || fail=1
echo "renders: $(ls /out/shots | wc -l)"
exit $fail
