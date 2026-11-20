#!/bin/sh
# In the container (test.sh): /r this repository.
S=/r/rootfs/usr/sbin/glassine-shutdown-screen
F=0
ok() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; F=$((F + 1)); fi; }
export GLASSINE_RUN=/tmp/run GLASSINE_SLEEP_PILL=/tmp/render
# the renderer: logs its arguments, writes the picture (or fails with FAIL_RENDER)
cat > /tmp/render <<'R'
#!/bin/sh
echo "$*" > /tmp/render.args
[ -z "${FAIL_RENDER:-}" ] || exit 1
for a; do o=$a; done
echo P5 > "$o"
R
chmod +x /tmp/render
fresh() { rm -rf /tmp/run /tmp/render.args /tmp/got; mkdir -p /tmp/run/glassine; }
# a panel daemon: the FIFO and one reader of one line
daemon() { mkfifo /tmp/run/glassine-epdd-desktop.ctl; (head -n 1 /tmp/run/glassine-epdd-desktop.ctl > /tmp/got) & RD=$!; }
got() { wait $RD 2>/dev/null; [ "$(cat /tmp/got 2>/dev/null)" = "$1" ]; }
said() { grep -q -- "-s $1" /tmp/render.args; }
keep() { [ -e /tmp/run/glassine/screen-keep ]; }

for c in "restart|Restarting…|shutdown-screen RESTARTING|keep" "menu|Restarting…|shutdown-screen RESTARTING|keep" \
         "recovery|Starting recovery…|shutdown-screen|keep" "other|Starting Android…|shutdown-screen|-" \
         "poweroff|Powered off|shutdown-screen POWERED OFF|-" "bootloader|Restarting…|shutdown-screen RESTARTING|-"; do
    IFS='|' read -r t w l k <<X
$c
X
    fresh; daemon
    ok "$t: '$w', the daemon gets '$l', screen-keep: $k" "$S $t && got '$l' && said '$w' && grep -q -- '-m screen' /tmp/render.args && [ \"\$(cat /tmp/run/glassine-shutdown-screen)\" = $t ] && [ -e /tmp/run/glassine-shutdown-screen.pgm ] && { [ $k = keep ] && keep || { [ $k = - ] && ! keep; }; }"
done

fresh; daemon; echo next=poweroff > /tmp/run/glassine/shutdown
ok "no target: the shutdown record's next=" "$S && got 'shutdown-screen POWERED OFF' && said 'Powered off'"
fresh; daemon; : > /tmp/run/glassine/restarting
ok "no target, no record: a soft restart's record is a restart" "$S && got 'shutdown-screen RESTARTING' && keep"
fresh; daemon; : > /tmp/run/glassine-poweroff
ok "no target: the power menu's power-off mark" "$S && said 'Powered off' && ! keep"
fresh; daemon
ok "no target at all: 'Shutting down…', no screen-keep" "$S && got 'shutdown-screen' && said 'Shutting down…' && ! keep && [ \"\$(cat /tmp/run/glassine-shutdown-screen)\" = unknown ]"
fresh; daemon; echo next=nonsense > /tmp/run/glassine/shutdown
ok "an unknown target word: 'Shutting down…'" "$S && said 'Shutting down…'"

fresh; daemon; $S restart; got 'shutdown-screen RESTARTING'; rm -f /tmp/render.args
ok "a second call (any target) does nothing" "$S poweroff && [ ! -e /tmp/render.args ] && [ \"\$(cat /tmp/run/glassine-shutdown-screen)\" = restart ]"

fresh
ok "no panel daemon (no FIFO): exit 0, nothing left" "$S restart && [ ! -e /tmp/run/glassine-shutdown-screen ] && ! keep"
fresh; mkfifo /tmp/run/glassine-epdd-desktop.ctl
t0=$(date +%s); $S restart; rc=$?; t1=$(date +%s)
ok "a FIFO nobody reads: exit 0 within 5 s, nothing recorded" "[ $rc = 0 ] && [ $((t1 - t0)) -le 5 ] && [ ! -e /tmp/run/glassine-shutdown-screen ] && ! keep"
fresh; daemon
ok "a renderer that fails: no stale picture, the daemon still asked (its fallback)" "FAIL_RENDER=1 $S restart && got 'shutdown-screen RESTARTING' && [ ! -e /tmp/run/glassine-shutdown-screen.pgm ]"
cp /tmp/render /tmp/render.ok; printf '#!/bin/sh\nsleep 10\n' > /tmp/render
fresh; daemon; t0=$(date +%s); $S restart; t1=$(date +%s)
ok "a renderer that hangs: cut after 3 s, the daemon still asked" "got 'shutdown-screen RESTARTING' && [ $((t1 - t0)) -le 5 ]"
cp /tmp/render.ok /tmp/render
sh -n /r/rootfs/etc/init.d/glassine-session && ok "the session service parses" true || ok "the session service parses" false
grep -q 'stop_pre' /r/rootfs/etc/init.d/glassine-session && grep -q 'RC_GOINGDOWN' /r/rootfs/etc/init.d/glassine-session
ok "the session's stop runs it only when the system goes down" "[ \$? = 0 ]"
echo "$F failed"; [ $F = 0 ]
