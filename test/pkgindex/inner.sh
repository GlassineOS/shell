#!/bin/sh
# Runs inside alpine (see test.sh).
S=/o/etc/NetworkManager/dispatcher.d/60-glassine-pkgindex
STAMP=/var/lib/glassine/pkgindex-stamp
L=/tmp/calls
F=0
ok() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; F=$((F+1)); fi; }
# stub NAME RC: logs its name, arguments, GLASSINE_APK_ACK and whether stdin is a terminal
stub() {
    mkdir -p "${1%/*}"
    printf '#!/bin/sh\necho "%s $* ack=${GLASSINE_APK_ACK:-} tty=$([ -t 0 ] && echo y || echo n)" >> %s\nexit %s\n' "$1" "$L" "$2" > "$1"
    chmod +x "$1"
}
# run IFACE ACTION: the script as NetworkManager runs it (no terminal), then wait for its refresh
run() {
    GLASSINE_PKGINDEX_WAIT=0 sh $S "$@" </dev/null; _r=$?
    # the refresh runs in the background, a fork of the script (same command line)
    i=0; while [ $i -lt 100 ] && pgrep -f "sh $S" >/dev/null; do sleep 0.1; i=$((i+1)); done
    return $_r
}
reset() { rm -f $STAMP $L; : > $L; }
APK=/usr/local/sbin/apk     # before /sbin/apk on the container's PATH: the real apk-tools is never run

stub /usr/sbin/glassine 0; stub $APK 0
reset
ok "not up: nothing" "run wlan0 down && [ ! -s $L ] && [ ! -e $STAMP ]"
ok "up, wrapper installed: glassine apk update -q, acknowledged, no terminal; stamp" \
    "run wlan0 up && grep -qx '/usr/sbin/glassine apk update -q ack=1 tty=n' $L && ! grep -q '^$APK' $L && [ -e $STAMP ]"
: > $L
ok "up again the same day: nothing" "run wlan0 up && [ ! -s $L ]"
reset; stub /usr/sbin/glassine 1
ok "the update fails: no stamp, retried on the next up" "run wlan0 up && grep -q '^/usr/sbin/glassine apk update' $L && [ ! -e $STAMP ]"
reset; rm /usr/sbin/glassine
ok "no wrapper: apk update -q" "run wlan0 up && grep -q '^$APK update -q' $L && [ -e $STAMP ]"
reset; stub /usr/sbin/glassine 0; touch -d '2 days ago' $STAMP 2>/dev/null || touch -t "$(date -d @$(( $(date +%s) - 172800 )) +%Y%m%d%H%M)" $STAMP
ok "a stamp older than a day: refreshed" "run wlan0 up && grep -q '^/usr/sbin/glassine apk update' $L"
ok "shellcheck" "shellcheck -s sh $S"
[ $F = 0 ] || cat $L
echo "$F failed"; [ $F = 0 ]
