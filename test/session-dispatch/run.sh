#!/bin/sh
# Host test of the session loop (rootfs/usr/sbin/glassine-session) in an Alpine
# container: stubs for pkill, date, sleep and the sway session (log what they
# are asked to do); checks that 3 quick sway deaths or one give-up (exit 3) mark
# the boot failed and the session is tried again after the pause.
set -eu
D=$(cd "$(dirname "$0")" && pwd)
O=$(cd "$D/../../rootfs" && pwd)
docker run --rm -v "$O":/o:ro alpine:3.24 sh -c '
set -u
fail=0
setup() {   # $1: sway stub behaviour (die|giveup)
    rm -rf /t; mkdir -p /t/bin /t/sbin /run; rm -f /run/glassine-session-failed
    printf "#!/bin/sh\necho \"pkill \$*\" >> /t/log\n" > /t/bin/pkill
    # sleep 30 = the pause after a failed boot: the test ends there
    printf "#!/bin/sh\necho \"sleep \$*\" >> /t/log\n[ \"\$1\" = 30 ] && kill -9 \$PPID\nexit 0\n" > /t/bin/sleep
    # date +%%s: each call 10 s later (a sway death after 10 s = quick)
    printf "#!/bin/sh\nn=\$(cat /t/clock 2>/dev/null || echo 1000); n=\$((n + 10)); echo \$n > /t/clock\n[ \"\$1\" = +%%s ] && echo \$n || echo \"date \$n\"\n" > /t/bin/date
    rc=1; [ "$1" = giveup ] && rc=3
    printf "#!/bin/sh\necho \"sway session\" >> /t/log\nexit $rc\n" > /t/sbin/glassine-session-sway
    chmod +x /t/bin/* /t/sbin/*
    : > /t/log
    sed "s|/usr/sbin/glassine-session-|/t/sbin/glassine-session-|g" /o/usr/sbin/glassine-session > /t/glassine-session
    GLASSINE_USER=user PATH=/t/bin:$PATH sh /t/glassine-session > /t/out 2>&1
}
check() { if grep -q "$2" /t/log; then echo "ok   $1"; else echo "FAIL $1 (no \"$2\")"; cat /t/log /t/out; fail=1; fi; }
checkn() { n=$(grep -c "$2" /t/log); if [ "$n" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 ($n x \"$2\", want $3)"; cat /t/log; fail=1; fi; }
marker() { [ -e /run/glassine-session-failed ] && echo "ok   $1" || { echo "FAIL $1"; fail=1; }; }

setup die
checkn "sway dying: 3 tries before the pause" "sway session" 3
marker "sway dying: failed marker"
check "sway dying: the user'"'"'s leftovers ended" "pkill -u user"
check "sway dying: tried again after 30 s" "sleep 30"

setup giveup
checkn "sway gave up (exit 3): one try before the pause" "sway session" 1
marker "sway gave up: failed marker"
check "sway gave up: tried again after 30 s" "sleep 30"

exit $fail
'
