#!/bin/sh
# In the container (test.sh): /r this repository.
S=/r/rootfs/usr/sbin/glassine-shutdown-screen
F=0
ok() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; F=$((F + 1)); fi; }
export GLASSINE_RUN=/tmp/run GLASSINE_USER_RUN=/tmp/users GLASSINE_SHELL=/tmp/shell
U=/tmp/users/1000
# the shell: logs its request (and whether the lock existed then), answers with ANSWER
# (held | shown | none: no answer | slow: after 10 s), or fails (ANSWER=fail)
cat > /tmp/shell <<'R'
#!/bin/sh
[ -d /tmp/run/glassine-shutdown-screen.lock ] && l=lock || l=nolock
printf '%s|%s|%s\n' "$1" "$2" "$l" >> /tmp/req
case ${ANSWER:-held} in
	fail) exit 1 ;;
	none) ;;
	slow) (sleep 10; echo held > "$XDG_RUNTIME_DIR/glassine-shutdown-screen.shown") & ;;
	*) echo "${ANSWER:-held}" > "$XDG_RUNTIME_DIR/glassine-shutdown-screen.shown" ;;
esac
exit 0
R
chmod +x /tmp/shell
fresh() {
	rm -rf /tmp/run /tmp/users /tmp/req; mkdir -p /tmp/run/glassine $U
	python3 -c 'import socket,sys; socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM).bind(sys.argv[1])' $U/glassine-shell
}
req() { [ "$(cat /tmp/req 2>/dev/null)" = "$1" ]; }
keep() { [ -e /tmp/run/glassine/screen-keep ]; }
rec() { [ "$(cat /tmp/run/glassine-shutdown-screen 2>/dev/null)" = "$1" ]; }

for c in "restart|Restarting…|keep" "menu|Restarting…|keep" "recovery|Starting recovery…|keep" \
         "other|Starting Android…|-" "poweroff|Powered off|-" "bootloader|Restarting…|-"; do
	IFS='|' read -r t w k <<X
$c
X
	fresh
	ok "$t: '$w' asked of the shell after the lock; held: recorded, screen-keep: $k" \
		"$S $t && req 'shutdown-screen|$w|lock' && rec $t && { [ $k = keep ] && keep || { [ $k = - ] && ! keep; }; }"
done

fresh; echo next=poweroff > /tmp/run/glassine/shutdown
ok "no target: the shutdown record's next=" "$S && req 'shutdown-screen|Powered off|lock'"
fresh; printf 'next=restart\nnext=poweroff\n' > /tmp/run/glassine/shutdown
ok "no target: the record's last next=" "$S && req 'shutdown-screen|Powered off|lock'"
fresh; : > /tmp/run/glassine/restarting
ok "no target, no record: a soft restart's record is a restart" "$S && req 'shutdown-screen|Restarting…|lock' && keep"
fresh; : > /tmp/run/glassine-poweroff
ok "no target: the power menu's power-off mark" "$S && req 'shutdown-screen|Powered off|lock' && ! keep"
fresh
ok "no target at all: 'Shutting down…', no screen-keep" "$S && req 'shutdown-screen|Shutting down…|lock' && ! keep && rec unknown"
fresh; echo next=nonsense > /tmp/run/glassine/shutdown
ok "an unknown target word: 'Shutting down…'" "$S && req 'shutdown-screen|Shutting down…|lock'"

fresh; $S restart; : > /tmp/req
ok "a second call (any target) does nothing" "$S poweroff && [ ! -s /tmp/req ] && rec restart"

fresh
ok "answer 'shown' (a display that doesn't keep its picture): no record, no screen-keep, the lock stays" \
	"ANSWER=shown $S restart && [ ! -e /tmp/run/glassine-shutdown-screen ] && ! keep && [ -d /tmp/run/glassine-shutdown-screen.lock ]"
fresh; t0=$(date +%s); ANSWER=none $S restart; rc=$?; t1=$(date +%s)
ok "no answer: exit 0 within 5 s, nothing recorded" "[ $rc = 0 ] && [ $((t1 - t0)) -le 5 ] && [ ! -e /tmp/run/glassine-shutdown-screen ] && ! keep"
fresh; t0=$(date +%s); ANSWER=slow $S restart; rc=$?; t1=$(date +%s)
ok "a late answer: exit 0 within 5 s, nothing recorded" "[ $rc = 0 ] && [ $((t1 - t0)) -le 5 ] && [ ! -e /tmp/run/glassine-shutdown-screen ] && ! keep"
fresh
ok "a request that fails: exit 0, nothing recorded" "ANSWER=fail $S restart && [ ! -e /tmp/run/glassine-shutdown-screen ] && ! keep"
fresh; rm -f $U/glassine-shell
ok "no session's shell (no socket): exit 0, nothing asked, the lock stays" \
	"$S restart && [ ! -e /tmp/req ] && [ ! -e /tmp/run/glassine-shutdown-screen ] && [ -d /tmp/run/glassine-shutdown-screen.lock ]"
fresh; rm -f $U/glassine-shell; : > $U/glassine-shell
ok "a plain file where the socket would be: not a shell" "$S restart && [ ! -e /tmp/req ]"
sh -n /r/rootfs/etc/init.d/glassine-session && ok "the session service parses" true || ok "the session service parses" false
grep -q 'stop_pre' /r/rootfs/etc/init.d/glassine-session && grep -q 'RC_GOINGDOWN' /r/rootfs/etc/init.d/glassine-session
ok "the session's stop runs it only when the system goes down" "[ \$? = 0 ]"
echo "$F failed"; [ $F = 0 ]
