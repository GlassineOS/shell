#!/bin/sh
# Runs inside alpine: real accounts, real shadow hashes, the real helpers.
O=/o
adduser -D kid; adduser -D anna; adduser -D ben
addgroup -S desktop 2>/dev/null; addgroup -S wheel 2>/dev/null
for u in kid anna ben; do addgroup $u desktop; done; addgroup anna wheel; addgroup ben wheel
echo 'kid:kidpass' | chpasswd -c sha512; echo 'anna:annapass' | chpasswd -c sha512; echo 'root:rootpass' | chpasswd -c sha512
passwd -l ben >/dev/null 2>&1
mkdir -p /usr/lib/glassine /usr/share/zoneinfo/Europe /tmp/stub; : > /usr/share/zoneinfo/Europe/Berlin
cp $O/usr/lib/glassine/admin-auth.sh /usr/lib/glassine/
cp $O/usr/sbin/glassine-checkpass $O/usr/sbin/glassine-settings-root $O/usr/sbin/glassine-power-root /usr/sbin/
chmod +x /usr/sbin/glassine-checkpass /usr/sbin/glassine-settings-root /usr/sbin/glassine-power-root
printf '#!/bin/sh\necho "$*" >> /tmp/ran\n' > /usr/sbin/glassine; chmod +x /usr/sbin/glassine
for b in glassine-slot glassine-reboot-clean flatpak; do cp /usr/sbin/glassine /usr/sbin/$b; done
F=0
ok() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; F=$((F+1)); fi; }
R=/usr/sbin/glassine-settings-root; P=/usr/sbin/glassine-power-root
as() { _u=$1; shift; DOAS_USER=$_u "$@" </dev/null >/dev/null 2>&1; }
auth() { printf '%s' "$2" | DOAS_USER=$1 $R auth >/dev/null 2>&1; }
ran() { grep -q "$1" /tmp/ran 2>/dev/null; }

ok "kid: apk-add refused with 77, nothing ran" "as kid $R apk-add foo; [ \$? = 77 ] && ! ran 'add'"
ok "kid: apk-del, apk-upgrade refused" "as kid $R apk-del foo; a=\$?; as kid $R apk-upgrade; [ \$a\$? = 7777 ] && ! ran del"
ok "kid: timezone, ntp, gpu, freeze, ddr, uninstall refused" "for v in 'timezone Europe/Berlin' 'ntp off' 'gpu panel on' 'freeze x off' 'ddr saver' 'uninstall a.b'; do as kid $R \$v; [ \$? = 77 ] || exit 1; done; true"
ok "kid: nothing changed on disk" "[ ! -e /etc/timezone ] && [ ! -e /etc/glassine/ntp-off ]"
ok "kid: rotation, apk-update, apk-ack work" "as kid $R rotation 90 && [ \"\$(cat /etc/glassine/rotation)\" = 'lock 90' ] && as kid $R apk-update && as kid $R apk-ack && ran update"
ok "android without slots.conf: refused (3), no slot write" "as kid $P android; [ \$? = 3 ] && ! ran set-active"
mkdir -p /etc/glassine; printf '# the second system\nANDROID_SLOT=a\n' > /etc/glassine/slots.conf; cp /usr/sbin/glassine /usr/sbin/reboot
ok "kid: android and recovery need no password" "as kid $P android && as kid $P recovery && ran 'set-active a --successful' && ran recovery"
ok "kid: menu needs no password, the start menu request through glassine-reboot-clean" "as kid $P menu && grep -qx menu /tmp/ran"
printf '#!/bin/sh\necho "$*" >> /tmp/ran\nexit 3\n' > /usr/sbin/glassine-reboot-clean
ok "menu where the loader takes no request: 3, as glassine-reboot-clean says" "as kid $P menu; [ \$? = 3 ]"
cp /usr/sbin/glassine /usr/sbin/glassine-reboot-clean
echo 'ANDROID_SLOT=a;x' > /etc/glassine/slots.conf
ok "android: a malformed slot is refused" "as kid $P android; [ \$? = 3 ]"
printf '#!/bin/sh\nexit 1\n' > /usr/sbin/glassine-slot; echo ANDROID_SLOT=b > /etc/glassine/slots.conf; : > /tmp/ran
ok "android: a failed slot write doesn't reboot" "as kid $P android; [ \$? = 1 ] && ! ran ."
ok "the session verb is gone" "as anna $P session sway; [ \$? != 0 ] && ! ran sway"
ok "admin: timezone without a password" "as anna $R timezone Europe/Berlin && [ \"\$(cat /etc/timezone)\" = Europe/Berlin ]"
ok "admin: apk-add still needs the password" "as anna $R apk-add foo; [ \$? = 77 ] && ! ran 'add'"
ok "auth-check: admin yes, password no, for an admin" "as anna $R auth-check admin && { as anna $R auth-check password; [ \$? = 77 ]; }"
ok "auth-check: both no for a kid" "as kid $R auth-check admin; a=\$?; as kid $R auth-check password; [ \$a\$? = 7777 ]"
t0=$(date +%s); auth anna wrong; rc=$?; t1=$(date +%s)
ok "wrong password: exit 1, at least 2 s" "[ $rc = 1 ] && [ $((t1-t0)) -ge 2 ]"
ok "wrong password: no access" "as anna $R apk-add foo; [ \$? = 77 ]"
ok "a non-admin's password is no use" "auth kid kidpass; [ \$? = 1 ]; as kid $R apk-add foo; [ \$? = 77 ]"
ok "a locked admin matches nothing, empty refused" "auth kid '!'; a=\$?; auth kid ''; [ \$a\$? = 11 ]"
ok "admin's own password" "auth anna annapass && as anna $R apk-add foo && ran 'apk --no-progress add foo\|add foo'"
ok "another user's token doesn't count" "as kid $R apk-add other; [ \$? = 77 ] && ! ran other"
ok "kid + an admin's password: works for the kid" "auth kid annapass && as kid $R apk-add kidpkg && ran kidpkg"
ok "root's password counts while it has one" "rm -f /run/glassine/admin-auth/anna; auth anna rootpass && as anna $R auth-check password"
ok "a token older than 5 min is dead" "read u _ </proc/uptime; echo \$(( \${u%%.*} - 301 )) > /run/glassine/admin-auth/kid; as kid $R auth-check password; [ \$? = 77 ]"
ok "a token from the future or garbage is dead" "echo 99999999999 > /run/glassine/admin-auth/kid; as kid $R auth-check password; a=\$?; echo zz > /run/glassine/admin-auth/kid; as kid $R auth-check password; [ \$a\$? = 7777 ]"
ok "a wrong try ends the kept password" "auth anna annapass; auth anna nope; as anna $R auth-check password; [ \$? = 77 ]"
ok "token dir is root's alone" "[ \"\$(stat -c %a /run/glassine/admin-auth)\" = 700 ]"
ok "root is never asked" "$R auth-check password && env -u DOAS_USER $R apk-add rootpkg"
adduser -D -G wheel pat; addgroup pat desktop; echo 'pat:patpass' | chpasswd -c sha512
ok "an admin by primary group wheel" "rm -f /run/glassine/admin-auth/pat; auth pat patpass && as pat $R auth-check password"
auth anna annapass; mv /etc/shadow /etc/shadow.away; t0=$(date +%s); auth anna annapass; rc=$?; t1=$(date +%s); mv /etc/shadow.away /etc/shadow
ok "unreadable shadow: exit 2, no wait" "[ $rc = 2 ] && [ $((t1-t0)) -lt 2 ]"
ok "unreadable shadow: the kept password stays" "as anna $R auth-check password"
ok "checkpass: no crypt in libc is exit 2" "printf x | python3 -I -c \"import ctypes; ctypes.CDLL.__getattr__ = lambda s, n: (_ for _ in ()).throw(AttributeError(n)); import runpy, sys; sys.argv = ['c']; runpy.run_path('/usr/sbin/glassine-checkpass', run_name='__main__')\" 2>/dev/null; [ \$? = 2 ]"
ok "checkpass ignores SHADOW and GROUP" "printf annapass | SHADOW=/dev/null GROUP=/dev/null /usr/sbin/glassine-checkpass"
# ---- Restart and Restart fully (GlassineOS/boot spec 013: glassine-deploy restart) ----
cp /usr/sbin/glassine /usr/sbin/reboot; cp /usr/sbin/glassine /usr/sbin/glassine-switch-hook; : > /tmp/ran
ok "no restart step: Restart is the user-started reboot, restart-check prints nothing" "as kid $P reboot && grep -qx -- --user-shutdown /tmp/ran && ! grep -q deploy /tmp/ran && [ -z \"\$(DOAS_USER=kid $P restart-check)\" ]"
# a restart step that says soft, returns after its restart (a stand-in: the real one
# doesn't return when it restarts) and logs the rest
printf '#!/bin/sh\necho "deploy $*" >> /tmp/ran\ncase "$*" in "restart --check") exit 0 ;; "restart --check --porcelain") printf "glassine-deploy 1\\ntype=restart\\trestart=soft\\trestart_reason=\\n" ;; esac\n' > /usr/sbin/glassine-deploy; chmod +x /usr/sbin/glassine-deploy
: > /tmp/ran
ok "Restart: glassine-deploy restart (soft where it can be), no --check before it" "as kid $P reboot && grep -qx 'deploy restart' /tmp/ran && ! grep -q 'restart --cold' /tmp/ran && ! grep -qx 'deploy restart --check' /tmp/ran"
: > /tmp/ran
ok "Restart fully: restart --cold" "as kid $P reboot-full && grep -qx 'deploy restart --cold' /tmp/ran"
ok "restart-check: the step's record" "DOAS_USER=kid $P restart-check | grep -q '^type=restart	restart=soft'"
: > /tmp/ran
ok "a restart step that refuses (it returns): an ordinary reboot after it, with the hook" "as kid $P reboot && grep -qx 'deploy restart' /tmp/ran && grep -qx -- --user-shutdown /tmp/ran"
mkdir -p /run/glassine; : > /run/glassine/restarting; : > /tmp/ran
ok "Restart: the step returns while its soft restart runs (restarting): no reboot after it" "as kid $P reboot && grep -qx 'deploy restart' /tmp/ran && ! grep -qx -- --user-shutdown /tmp/ran"
rm -f /run/glassine/restarting
printf '#!/bin/sh\necho "deploy $*" >> /tmp/ran\nexit 2\n' > /usr/sbin/glassine-deploy; : > /tmp/ran
ok "an older glassine-deploy (no restart: usage 2): its refusal, then the reboot" "as kid $P reboot && grep -qx 'deploy restart' /tmp/ran && ! grep -q 'restart --check' /tmp/ran && grep -qx -- --user-shutdown /tmp/ran && [ -z \"\$(DOAS_USER=kid $P restart-check)\" ]"
rm -f /usr/sbin/glassine-deploy /usr/sbin/glassine-switch-hook
# ---- the shutdown screen (spec 009): first, before the shutdown; never for a request that can still be refused ----
printf '#!/bin/sh\necho "screen $*" >> /tmp/ran\n' > /usr/sbin/glassine-shutdown-screen; chmod +x /usr/sbin/glassine-shutdown-screen
cp /usr/sbin/glassine /usr/sbin/poweroff; cp /usr/sbin/glassine /usr/sbin/glassine-switch-hook
first() { [ "$(head -n 1 /tmp/ran)" = "$1" ]; }
: > /tmp/ran
ok "screen: Power off shows it first (poweroff), then the hook and poweroff" "as kid $P poweroff && first 'screen poweroff' && grep -qx -- --user-shutdown /tmp/ran && [ \"\$(sed -n 3p /tmp/ran)\" = '' ] && [ -e /run/glassine-poweroff ]"
: > /tmp/ran
ok "screen: Restart shows it first (restart)" "as kid $P reboot && first 'screen restart'"
: > /tmp/ran
ok "screen: Restart fully shows it first (restart)" "as kid $P reboot-full && first 'screen restart'"
cp /usr/sbin/glassine /usr/sbin/glassine-slot; echo ANDROID_SLOT=a > /etc/glassine/slots.conf; : > /tmp/ran
ok "screen: Android after the slot switch (other)" "as kid $P android && first 'set-active a --successful' && grep -qx 'screen other' /tmp/ran"
printf '#!/bin/sh\nexit 1\n' > /usr/sbin/glassine-slot; : > /tmp/ran
ok "screen: a failed Android switch shows nothing" "as kid $P android; ! grep -q screen /tmp/ran"
rm -f /etc/glassine/slots.conf; : > /tmp/ran
ok "screen: no second system shows nothing" "as kid $P android; ! grep -q screen /tmp/ran"
: > /tmp/ran
ok "screen: recovery and menu leave it to the session's stop (their request may be refused)" "as kid $P recovery && as kid $P menu && ! grep -q screen /tmp/ran"
rm -f /usr/sbin/glassine-shutdown-screen /usr/sbin/poweroff /usr/sbin/glassine-switch-hook
echo "$F failed"; [ $F = 0 ]
