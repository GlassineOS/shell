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

# the stop (supervise-daemon'"'"'s TERM): real sleep, pkill and pgrep; returns within about
# a second whatever runs (the session, a pause between starts), and no process of the
# desktop user is left, not even one that ignores TERM
adduser -D -u 1100 user 2>/dev/null
stoprun() {   # $1: session (a session that runs, with a leftover that ignores TERM) or die (dies at once: the loop pauses)
    rm -rf /t; mkdir -p /t/sbin /run; rm -f /run/glassine-session-failed
    if [ "$1" = session ]; then
        printf "#!/bin/sh\ntrap \"exit 0\" TERM\nsu -s /bin/sh user -c \"trap \\\"\\\" TERM; exec /bin/sleep 100\" &\n/bin/sleep 100 & wait \$!\n" > /t/sbin/glassine-session-sway
    else
        printf "#!/bin/sh\nexit 1\n" > /t/sbin/glassine-session-sway
    fi
    chmod +x /t/sbin/*
    sed "s|/usr/sbin/glassine-session-|/t/sbin/glassine-session-|g" /o/usr/sbin/glassine-session > /t/glassine-session
    GLASSINE_USER=user sh /t/glassine-session > /t/out 2>&1 &
    M=$!
    if [ "$1" = session ]; then /bin/sleep 1.5; else   # three quick deaths: the 30 s pause
        i=0; until grep -q "trying again in 30 s" /t/out || [ $i -ge 100 ]; do /bin/sleep 0.1; i=$((i + 1)); done
        /bin/sleep 0.3
    fi
    [ "$1" = session ] && { pgrep -u user >/dev/null || echo "FAIL $1: the leftover never started"; }
    read -r u0 x < /proc/uptime; kill -TERM $M
    i=0; while [ -e /proc/$M ] && [ "$(cut -d" " -f3 /proc/$M/stat)" != Z ] && [ $i -lt 100 ]; do /bin/sleep 0.05; i=$((i + 1)); done
    read -r u1 x < /proc/uptime
    ms=$(awk -v a=$u0 -v b=$u1 "BEGIN { printf \"%d\", (b - a) * 1000 }")
    wait $M 2>/dev/null
    pgrep -u user >/dev/null && left=yes || left=no
    pkill -KILL -u user 2>/dev/null; pkill -KILL -f "sleep 100" 2>/dev/null
}
stoprun session
is "stop with the session running and a leftover that ignores TERM: done within 1.8 s ($ms ms)" "$([ $ms -lt 1800 ] && echo yes)" yes
is "stop: no process of the desktop user left" "$left" no
grep -q "still running as user after TERM: .*sleep" /t/out && echo "ok   stop: the leftover named in the log" || { echo "FAIL stop: the leftover not named"; cat /t/out; fail=1; }
stoprun die
grep -q "trying again in 30 s" /t/out || { echo "FAIL stop: never reached the pause"; fail=1; }
is "stop during the 30 s pause after a failed round: done within 0.8 s ($ms ms)" "$([ $ms -lt 800 ] && echo yes)" yes

exit $fail
'
