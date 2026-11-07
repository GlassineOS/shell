#!/bin/sh
# (see test.sh) inside the glassine-settingstest container: /src = repo (ro), /out = results
set -u
P=/src
export PYTHONPATH=/ui/python      # GlassineOS/ui: glassine_ui (the voices, the drawing)
mkdir -p /out/fonts /out/shots
[ "$(ls /out/fonts | wc -l)" -ge 6 ] || sh $P/fonts/fetch-fonts.sh /out/fonts
mkdir -p /usr/share/fonts/glassine && cp /out/fonts/* /usr/share/fonts/glassine/ && fc-cache -f >/dev/null
cp -a $P/rootfs/usr/share/icons/glassine-mono /usr/share/icons/ && gtk-update-icon-cache -q -f /usr/share/icons/glassine-mono
run() { python3 -c "import sys; sys.path[:0]=['$P/rootfs/usr/lib/glassine','$P/rootfs/usr/lib/glassine']; from setup.app import main; sys.exit(main(sys.argv))" "$@"; }
fail=0
for t in ${THEMES:-white editorial stationery}; do
    for st in ${STEPS:-welcome look location keyboard network account software review installing done}; do
        run --demo --render /out/shots/$t-$st.png --step $st --theme $t 2>/out/err.txt || { echo "FAIL $t $st"; cat /out/err.txt; fail=1; }
    done
    run --demo --render /out/shots/$t-network-joined.png --step network --joined Fernweh --theme $t 2>/out/err.txt || { echo "FAIL $t joined"; cat /out/err.txt; fail=1; }
    GLASSINE_SETUP_DEMO_NEEDS=network,software run --demo --render /out/shots/$t-reset-software.png --step software --theme $t 2>/out/err.txt || { echo "FAIL $t reset"; cat /out/err.txt; fail=1; }
    run --demo --render /out/shots/$t-look-big.png --step look --big --theme $t 2>/out/err.txt || { echo "FAIL $t big"; cat /out/err.txt; fail=1; }
done
# setup stands alone: only its own package and GlassineOS/ui, no glassine-settings beside it
mkdir -p /tmp/alone && cp -a $P/rootfs/usr/lib/glassine/setup /tmp/alone/
if python3 -c "import sys; sys.path[:0]=['/tmp/alone']; from setup.app import main; sys.exit(main(sys.argv))" \
        --demo --render /tmp/alone/look.png --step look --theme stationery 2>/out/err.txt &&
        python3 -c "import sys; sys.path[:0]=['/tmp/alone']; import setup.app; sys.exit('settings' in sys.modules)"; then
    echo "ok   setup draws without glassine-settings"
else
    echo "FAIL setup needs glassine-settings"; cat /out/err.txt; fail=1
fi
# the real backend end to end (this container is throwaway, root): packs.conf, the apply
# behind a doas shim, apk/flatpak faked
mkdir -p /usr/share/glassine/setup && cp $P/rootfs/usr/share/glassine/setup/packs.conf /usr/share/glassine/setup/
cp $P/rootfs/usr/sbin/glassine-setup-apply /usr/sbin/ && chmod +x /usr/sbin/glassine-setup-apply
printf '#!/bin/sh\n[ "$1" = -n ] && shift\nexec "$@"\n' > /usr/bin/doas && chmod +x /usr/bin/doas
printf '#!/bin/sh\necho "$@" >> /tmp/fake.log\n' > /tmp/fake && chmod +x /tmp/fake
for g in input video audio wheel desktop; do addgroup -S $g 2>/dev/null; done
GLASSINE_SETUP_APK=/tmp/fake GLASSINE_SETUP_FLATPAK=/tmp/fake GLASSINE_SETUP_NOLIVE=1 GLASSINE_SETUP_NOSLEEP=/tmp/no-sleep \
    python3 /t/flow.py $P || fail=1
echo "renders: $(ls /out/shots | wc -l)"
exit $fail
