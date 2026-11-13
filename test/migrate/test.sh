#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# The carry list holds every file the fast defaults (glassine_defaults in
# glassine-session-sway) create: the defaults marker is carried, so a flag missing from
# the list is lost on the next update and never set again.
set -eu
cd "$(dirname "$0")/../.."
list=rootfs/usr/share/glassine/migrate.d/glassine-shell.list
fn=$(sed -n '/^glassine_defaults() {/,/^}/p' rootfs/usr/sbin/glassine-session-sway)
[ -n "$fn" ] || { echo "migrate: glassine_defaults not found"; exit 1; }
# flags from the "for f in ...; do ... : > /etc/glassine/$f" loops, and literal paths
loops=$(printf '%s\n' "$fn" | sed -n 's/^ *for f in \([a-z0-9 -]*\); do$/\1/p' | tr ' ' '\n')
paths=$(printf '%s\n' "$fn" | grep -oE '/etc/glassine/[a-z0-9.-]+' | sed 's|/etc/glassine/||')
fail=0; n=0
for f in $(printf '%s\n%s\n' "$loops" "$paths" | grep -v '^$' | grep -v '^defaults-$' | sort -u); do
	n=$((n + 1))
	if grep -qxF "/etc/glassine/$f" "$list"; then echo "ok   $f carried"
	else echo "FAIL $f is written by the fast defaults but not in $list"; fail=$((fail + 1)); fi
done
[ "$n" -gt 5 ] || { echo "migrate: only $n files found in glassine_defaults"; exit 1; }
echo "$fail failed"
[ "$fail" = 0 ]
