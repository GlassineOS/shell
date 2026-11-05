#!/bin/sh
# glassine-setup-apply for real, as root, in a throwaway Alpine 3.22 container:
# --check rejections, the apply (account, zone, host, profile files, groups), a
# re-run (idempotent), a failing pack (the rest go on), --dry-run changes nothing;
# the admin password, the accounts record in /home, and the resets: "Reset system"
# (setup.conf kept: only Wi-Fi/software asked), a full reset (/etc emptied: the
# account comes back from the record, the system items are asked), a wiped home
# (the account is asked; the homeless one is replaced), needs.py at each point.
# test/setup/apply-test.sh
set -eu
D=$(cd "$(dirname "$0")" && pwd)
P=$(cd "$D/../.." && pwd)
docker run --rm -v "$P":/p:ro alpine:3.24 sh -ec '
apk add -q python3 tzdata >/dev/null
for g in input video audio wheel aid_inet aid_net_raw desktop; do addgroup -S $g 2>/dev/null || true; done
mkdir -p /etc/skel/.config && echo seed > /etc/skel/.config/seed
cat > /tmp/fake-apk <<EOS
#!/bin/sh
echo "\$@" >> /tmp/apk.log
case " \$* " in *" gdb "*) echo "ERROR: unable to select packages: gdb (no such package)" >&2; exit 1 ;; esac
exit 0
EOS
cat > /tmp/fake-flatpak <<EOS
#!/bin/sh
echo "\$@" >> /tmp/flatpak.log
EOS
chmod +x /tmp/fake-apk /tmp/fake-flatpak
export GLASSINE_SETUP_NOSLEEP=/tmp/no-sleep GLASSINE_SETUP_APK=/tmp/fake-apk GLASSINE_SETUP_FLATPAK=/tmp/fake-flatpak GLASSINE_SETUP_NOLIVE=1
export GLASSINE_SETUP_PACKS=/p/rootfs/usr/share/glassine/setup/packs.conf
export GLASSINE_SETUP_RECORD=/home/.glassine/accounts GLASSINE_SETUP_ARLIB=/p/rootfs/usr/lib/glassine/accounts-record.sh
mkdir -p /etc/NetworkManager/system-connections
needs() { PYTHONPATH=/p/rootfs/usr/lib/glassine python3 -c "from setup.needs import needs; print(\" \".join(needs()))"; }
AR=". /p/rootfs/usr/lib/glassine/accounts-record.sh"
A="python3 /p/rootfs/usr/sbin/glassine-setup-apply"
fail=0
t() { if eval "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
sha_ok() { case $(grep ^anna: /etc/shadow | cut -d: -f2) in [$]6[$]*) return 0 ;; esac; grep ^anna: /etc/shadow | cut -c1-12; return 1; }
plan() { printf "{\"language\":\"de\",\"voice\":\"editorial\",\"zone\":\"%s\",\"ntp\":false,\"clock24\":false,\"layout\":\"de-nd\",\"osk\":false,\"hostname\":\"anna-note\",\"user\":{\"name\":\"%s\",\"fullname\":\"Anna Krause\",\"password\":\"%s\",\"admin\":true,\"autologin\":false},\"packs\":[%s]}" "$@"; }
GOOD=$(plan Europe/Berlin anna "Sternenstaub!2026" "\"web\",\"store\"")

out=$(plan Mars/Base root short "\"android\"" | $A --check || true)
t "check refuses a system name"   "echo \"\$out\" | grep -q \"invalid user.name a system account\""
t "check refuses a short password" "echo \"\$out\" | grep -q \"invalid user.password\""
t "check refuses an unknown zone"  "echo \"\$out\" | grep -q \"invalid zone\""
t "check refuses a demo pack"      "echo \"\$out\" | grep -q \"not offered: android\""
out=$(echo "$GOOD" | sed "s/}\$/,\"admin_password\":\"a:b:cdefgh\"}/" | $A --check || true)
t "check refuses a colon in the admin password" "echo \"\$out\" | grep -q \"invalid admin_password\""
out=$(echo "$GOOD" | sed "s/}\$/,\"admin_password\":\"short\"}/" | $A --check || true)
t "check refuses a short admin password" "echo \"\$out\" | grep -q \"invalid admin_password\""
t "check passes a good plan"       "echo \"\$GOOD\" | $A --check | grep -qx \"ok check\""

out=$(echo "$GOOD" | $A --dry-run)
t "dry run lists 6 steps"          "echo \"\$out\" | grep -qx \"steps 6\""
t "dry run creates nobody"         "! grep -q ^anna: /etc/passwd"
t "fresh: needs everything"        "[ \"\$(needs)\" = \"account look system network software\" ]"

out=$(echo "$GOOD" | $A || true)
echo "$out" | sed "s/^/     | /"
t "apply ends done"                "echo \"\$out\" | tail -1 | grep -qx done"
t "user anna (uid >= 1000)"        "[ \$(id -u anna) -ge 1000 ]"
t "password set (sha512)"          "sha_ok"
t "groups incl. wheel"             "id -nG anna | grep -qw wheel && id -nG anna | grep -qw video && id -nG anna | grep -qw desktop"
t "home from skel"                 "[ \"\$(cat /home/anna/.config/seed)\" = seed ]"
t "home owned by anna"             "[ \"\$(stat -c %U /home/anna/.config/seed)\" = anna ]"
t "zone"                           "[ \$(readlink /etc/localtime) = /usr/share/zoneinfo/Europe/Berlin ] && grep -qx Europe/Berlin /etc/timezone"
t "ntp off"                        "[ -e /etc/glassine/ntp-off ]"
t "hostname + hosts"               "grep -qx anna-note /etc/hostname && grep -q \"127.0.1.1.anna-note\" /etc/hosts"
t "LANG"                           "grep -q LANG=de_DE.UTF-8 /etc/profile.d/glassine-locale.sh"
t "voice"                          "grep -qx editorial /home/anna/.config/glassine-shell/theme"
t "keyboard"                       "grep -qx \"de nodeadkeys\" /home/anna/.config/glassine/kbd-layout && grep -qx german,special /home/anna/.config/glassine/osk-layers"
t "12 h clock, no OSK auto"        "[ -e /home/anna/.config/glassine-shell/clock12 ] && [ -e /home/anna/.config/glassine/osk-no-auto ]"
t "packs: apk + flatpak"           "grep -q \"add firefox-esr glassine-firefox\" /tmp/apk.log && grep -q io.github.kolunmi.Bazaar /tmp/flatpak.log"
t "setup.conf without secrets"     "grep -qx user=anna /etc/glassine/setup.conf && ! grep -q Sternenstaub /etc/glassine/setup.conf"
t "done marker"                    "[ -e /etc/glassine/setup-done ]"
t "no-sleep marker released"       "[ ! -e /tmp/no-sleep ]"

GOOD2=$(plan Europe/Berlin anna "Mondlicht#2027" "\"dev\",\"news\"")
out=$(echo "$GOOD2" | sed "s/\"admin\":true/\"admin\":false/" | sed "s/\"clock24\":false/\"clock24\":true/" | $A || true)
echo "$out" | sed "s/^/     | /"
t "re-run: a failing pack goes on" "echo \"\$out\" | grep -q \"^fail pack-dev\" && echo \"\$out\" | grep -q \"^ok pack-news\""
t "re-run ends done-with-skips"    "echo \"\$out\" | tail -1 | grep -qx done-with-skips"
t "re-run: one anna"               "[ \$(grep -c ^anna: /etc/passwd) = 1 ]"
t "re-run: admin off drops wheel"  "! id -nG anna | grep -qw wheel"
t "re-run: 24 h clock"             "[ ! -e /home/anna/.config/glassine-shell/clock12 ]"
t "not root: refused"              "! su nobody -s /bin/sh -c \"echo x | $A\" >/dev/null 2>&1"

# the record (cleanup saved it): root only, holds the passwd/shadow/memberships
t "record saved, 0600 in a 0700 dir" "[ \$(stat -c %a /home/.glassine/accounts) = 600 ] && [ \$(stat -c %a /home/.glassine) = 700 ]"
t "record has anna"                "grep -q \"^passwd anna:\" /home/.glassine/accounts && grep -q \"^shadow anna:\" /home/.glassine/accounts && grep -q \"^member video anna\" /home/.glassine/accounts"
t "after setup: only Wi-Fi asked"  "[ \"\$(needs)\" = network ]"
touch /etc/NetworkManager/system-connections/home.nmconnection
t "Wi-Fi saved: nothing asked"     "[ -z \"\$(needs)\" ]"

# the admin password
R0=$(grep ^root: /etc/shadow | cut -d: -f2)
out=$(printf "{\"language\":\"de\",\"zone\":\"Europe/Berlin\",\"layout\":\"de\",\"hostname\":\"anna-note\",\"admin_password\":\"glassine\"}" | $A || true)
echo "$out" | sed "s/^/     | /"
t "no-user plan: 4 steps"          "echo \"\$out\" | grep -qx \"steps 4\" && echo \"\$out\" | tail -1 | grep -qx done"
t "root password changed"          "[ \"\$(grep ^root: /etc/shadow | cut -d: -f2)\" != \"\$R0\" ]"
t "no-user plan keeps the user"    "grep -qx user=anna /etc/glassine/setup.conf && [ \$(grep -c ^anna: /etc/passwd) = 1 ]"
t "no-user plan writes anna profile" "grep -qx de /home/anna/.config/glassine/kbd-layout"

# "Reset system": setup.conf + accounts kept (migrate list), user packages + Wi-Fi gone
rm -f /etc/NetworkManager/system-connections/*
printf "P:firefox-esr\n" > /tmp/installed; mkdir -p /lib/apk/db; [ -e /lib/apk/db/installed ] && cp /lib/apk/db/installed /tmp/installed.keep
sed -i "s/^packs=.*/packs=web/" /etc/glassine/setup.conf
t "reset system: Wi-Fi + software" "[ \"\$(needs)\" = \"network software\" ]"
cat /tmp/installed >> /lib/apk/db/installed
t "a pack still installed: not asked" "[ \"\$(needs)\" = network ]"
[ -e /tmp/installed.keep ] && cp /tmp/installed.keep /lib/apk/db/installed
echo "network=skipped" >> /etc/glassine/setup.conf
sed -i "s/^packs=.*/packs=/" /etc/glassine/setup.conf
t "skipped + none for now: nothing" "[ -z \"\$(needs)\" ]"

# full reset: /etc back to the image (no anna, no setup.conf), /home kept
H=$(grep ^anna: /etc/shadow | cut -d: -f2)
deluser anna; rm -f /etc/glassine/setup.conf /etc/glassine/setup-done
t "full reset, before glassine-home: account asked" "needs | grep -qw account"
sh -c "$AR && ar_restore /home/.glassine/accounts"
t "record restores anna"           "[ \$(grep -c ^anna: /etc/passwd) = 1 ] && [ \"\$(grep ^anna: /etc/shadow | cut -d: -f2)\" = \"\$H\" ]"
t "restored memberships"           "id -nG anna | grep -qw video && id -nG anna | grep -qw desktop"
t "restore again: no-op"           "! sh -c \"$AR && ar_restore /home/.glassine/accounts\" && [ \$(grep -c ^anna: /etc/passwd) = 1 ]"
t "full reset: system items, look kept" "[ \"\$(needs)\" = \"system network software\" ]"

# wiped home: anna stays in /etc but homeless; the setup asks for the account; ben replaces her
rm -rf /home/anna /home/.glassine
t "wiped home: account + look asked" "needs | grep -q \"^account look\""
out=$(plan Europe/Berlin ben "Kometen?schweif9" "" | $A || true)
echo "$out" | sed "s/^/     | /"
t "homeless anna replaced by ben"  "! grep -q ^anna: /etc/passwd && [ \$(grep -c ^ben: /etc/passwd) = 1 ]"
t "ben home from skel, 750"        "[ \"\$(cat /home/ben/.config/seed)\" = seed ] && [ \$(stat -c %a /home/ben) = 750 ]"
t "record now has ben only"        "grep -q \"^passwd ben:\" /home/.glassine/accounts && ! grep -q anna /home/.glassine/accounts"
exit $fail
'
