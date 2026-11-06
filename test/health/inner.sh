#!/bin/sh
# Runs inside alpine (see test.sh). A stand-in is a copy of the python3 binary under
# the real name: its argv[0] is the absolute path, like the session's processes.
H=/o/etc/glassine-bootimg/health.d/40-glassine-desktop
PY=$(readlink -f /usr/bin/python3)
mkdir -p /etc/glassine /var/log /usr/sbin /usr/bin
F=0
ok() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; F=$((F+1)); fi; }
start() { [ -e "$1" ] || cp "$PY" "$1"; "$1" -c 'import time; time.sleep(600)' & sleep 0.3; }
stop() { pkill -f "^$1 " 2>/dev/null; sleep 0.3; }
check() { GLASSINE_HEALTH_MIN_AGE=${MIN:-0} sh $H > /tmp/out 2>&1; }
says() { grep -q -- "$1" /tmp/out; }

echo sway > /etc/glassine/session
for p in /usr/sbin/glassine-epdd-desktop /usr/bin/sway-glassine /usr/bin/glassine-swayd /usr/bin/glassine-shell-wl; do start $p; done
ok "sway session, all running by absolute path: healthy" "check"
ok "fresh processes fail the minimum age" "MIN=60 check; [ \$? = 1 ] && says 'glassine-epdd-desktop(restarted'"
stop /usr/bin/glassine-shell-wl
ok "the shell missing: unhealthy, named" "! check && says glassine-shell-wl"
start /usr/bin/glassine-shell-wl
stop /usr/sbin/glassine-epdd-desktop; start /usr/sbin/glassine-epdd-desktop-wl
ok "glassine-epdd-desktop-wl instead of glassine-epdd-desktop: healthy" "check"
stop /usr/sbin/glassine-epdd-desktop-wl; start /usr/sbin/glassine-epdd-desktop
mkdir -p /run/user/1000; : > /run/user/1000/glassine-shell-failed
ok "the bar given up: unhealthy" "! check && says shell-gave-up"
rm /run/user/1000/glassine-shell-failed
echo 'epd: DEAD LINK (x): link + rails down, hv_on 1, failure 1/5' > /var/log/glassine-epdd-desktop.log
ok "one recovered DEAD LINK: healthy" "check"
echo 'epd: DEAD LINK (x): link + rails down, hv_on 1, failure 5/5: GIVING UP, the panel is no longer driven' >> /var/log/glassine-epdd-desktop.log
ok "GIVING UP: unhealthy" "! check && says panel-link"
: > /var/log/glassine-epdd-desktop.log
printf '#!/usr/bin/python3\nimport time\ntime.sleep(600)\n' > /usr/bin/glassine-setup; chmod +x /usr/bin/glassine-setup
stop /usr/bin/glassine-shell-wl; stop /usr/bin/glassine-swayd
/usr/bin/glassine-setup & sleep 0.3; : > /run/glassine-setup.active
ok "first-run setup (a Python script) instead of the shell: healthy" "check"
rm /run/glassine-setup.active
echo "$F failed"; [ $F = 0 ]
