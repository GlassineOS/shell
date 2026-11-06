#!/bin/sh
# Host test of the session loop (rootfs/usr/sbin/glassine-session) in an Alpine
# container. Stubs: the sway session follows a plan (die: exits at once, 10 s
# by the stub clock; long: runs, the clock jumps 100 s; giveup: exit 3) and
# ends the test when the plan is used up; pkill and date log or fake; sleep in
# the loop itself logs the pause (the 2 s between starts as a gap), the 60 s up-timer sleeps briefly for real.
# Checks: 3 quick deaths or one give-up mark the boot failed, the pause doubles
# (30, 60, 120 ... 300), sway starts again after it, a lasting session resets
# the count and the pause and clears the mark.
set -eu
D=$(cd "$(dirname "$0")" && pwd)
O=$(cd "$D/../../rootfs" && pwd)
docker run --rm -v "$O":/o:ro alpine:3.24 sh -c '
set -u
fail=0
run() {   # $1: the plan (space separated), $2: stop after this many pauses
    rm -rf /t; mkdir -p /t/bin /t/sbin /run; rm -f /run/glassine-session-failed
    for x in $1; do echo $x; done > /t/plan
    echo "$2" > /t/stopat
    printf "#!/bin/sh\necho \"pkill \$*\" >> /t/log\n" > /t/bin/pkill
    cat > /t/bin/sleep <<"S"
#!/bin/sh
if [ "$PPID" = "$(cat /t/main)" ]; then
    [ "$1" = 2 ] && { echo "gap" >> /t/log; exit 0; }   # between two starts
    echo "pause $1" >> /t/log
    n=$(grep -c "^pause" /t/log)
    [ "$n" -ge "$(cat /t/stopat)" ] && kill -9 $PPID
    exit 0
fi
exec /bin/sleep 0.3
S
    # date +%s: each call 10 s later (a death after 10 s = quick)
    printf "#!/bin/sh\nn=\$(cat /t/clock 2>/dev/null || echo 1000); n=\$((n + 10)); echo \$n > /t/clock\n[ \"\$1\" = +%%s ] && echo \$n || echo \"date \$n\"\n" > /t/bin/date
    cat > /t/sbin/glassine-session-sway <<"S"
#!/bin/sh
x=$(head -n1 /t/plan); sed -i 1d /t/plan
[ -z "$x" ] && { kill -9 $(cat /t/main); exit 0; }
echo "sway $x" >> /t/log
case $x in
long) /bin/sleep 1; echo $(($(cat /t/clock) + 100)) > /t/clock; exit 0 ;;
giveup) exit 3 ;;
*) exit 1 ;;
esac
S
    chmod +x /t/bin/* /t/sbin/*
    : > /t/log
    sed "s|/usr/sbin/glassine-session-|/t/sbin/glassine-session-|g" /o/usr/sbin/glassine-session > /t/glassine-session
    GLASSINE_USER=user PATH=/t/bin:$PATH sh /t/glassine-session > /t/out 2>&1 &
    echo $! > /t/main
    wait 2>/dev/null
}
got() { sed -n "s/^pause //p" /t/log | tr "\n" " "; }
is() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 (\"$2\", want \"$3\")"; cat /t/log; fail=1; fi; }
n() { grep -c "^$1\$" /t/log; }
marked() { [ -e /run/glassine-session-failed ] && echo yes || echo no; }

run "die die die die die die die die die die die die die die die die die die" 6
is "always dying: the pause doubles up to 5 min" "$(got)" "30 60 120 240 300 300 "
is "always dying: 3 starts per round, started again after each pause" "$(n "sway die")" 18
is "always dying: marked" "$(marked)" yes
grep -q "pkill -u user" /t/log && echo "ok   the user'"'"'s leftovers ended" || { echo "FAIL no pkill"; fail=1; }

run "giveup giveup" 9
is "gave up (exit 3): a pause after each give-up" "$(got)" "30 60 "
is "gave up: marked" "$(marked)" yes

run "die die die long" 9
is "a lasting session after a failed round clears the mark" "$(marked)" no

run "die die die long die die die" 9
is "a lasting session resets the pause and the count" "$(got)" "30 30 "

run "die die long die die long die die" 9
is "deaths around a lasting session: no failed round" "$(got)" ""
is "deaths around a lasting session: not marked" "$(marked)" no

exit $fail
'
