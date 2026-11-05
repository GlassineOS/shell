# The desktop accounts' record in the home folder (sourced; busybox sh, as root).
# The accounts live in /etc, which a full reset empties; the home survives it. So
# the record keeps them next to the homes: a full reset brings the account back by
# itself (glassine-setup then asks only for what the reset removed), and a wiped
# home takes the record with it (the setup asks for the account again).
# ar_save FILE record every account with a uid in [1000, 65534): its passwd,
# shadow and private-group lines and its group memberships
# (nothing written when there is none)
# ar_restore FILE no such account in /etc/passwd: add the recorded ones back
# (groups that exist get their members again) -> 0; else 1
# ar_have 0 when an account with a uid in [1000, 65534) exists
# FILE is root's alone (0600: it holds password hashes). Test overrides: AR_ETC.

AR_ETC=${AR_ETC:-/etc}

ar_have() {
	awk -F: '$3 >= 1000 && $3 < 65534 { f = 1 } END { exit !f }' "$AR_ETC/passwd"
}

ar_save() {
	ar_have || return 0
	_ar_users=$(awk -F: '$3 >= 1000 && $3 < 65534 { print $1 }' "$AR_ETC/passwd")
	mkdir -p "${1%/*}" && chmod 700 "${1%/*}" || return 1
	(
		umask 077
		{
			echo "# glassine accounts record (ar_save); do not edit"
			awk -F: '$3 >= 1000 && $3 < 65534 { print "passwd " $0 }' "$AR_ETC/passwd"
			for _u in $_ar_users; do
				grep "^$_u:" "$AR_ETC/shadow" | sed 's/^/shadow /'
			done
			# the users' own groups (gid >= 1000) whole, and every other membership by name
			awk -F: '$3 >= 1000 && $3 < 65534 { print "group " $0 }' "$AR_ETC/group"
			awk -F: -v users=" $(echo $_ar_users) " '$3 < 1000 || $3 >= 65534 {
				n = split($4, m, ",")
				for (i = 1; i <= n; i++) if (index(users, " " m[i] " ")) print "member " $1 " " m[i]
			}' "$AR_ETC/group"
		} > "$1.new" && mv -f "$1.new" "$1"
	)
}

ar_restore() {
	[ -s "$1" ] || return 1
	ar_have && return 1
	# groups first (a user's primary group must exist), then users, then passwords
	sed -n 's/^group //p' "$1" | while IFS= read -r _l; do
		grep -q "^${_l%%:*}:" "$AR_ETC/group" || echo "$_l" >> "$AR_ETC/group"
	done
	sed -n 's/^passwd //p' "$1" | while IFS= read -r _l; do
		grep -q "^${_l%%:*}:" "$AR_ETC/passwd" || echo "$_l" >> "$AR_ETC/passwd"
	done
	sed -n 's/^shadow //p' "$1" | while IFS= read -r _l; do
		_n=${_l%%:*}
		if grep -q "^$_n:" "$AR_ETC/shadow"; then
			awk -F: -v n="$_n" -v l="$_l" '$1 == n { print l; next } { print }' "$AR_ETC/shadow" > "$AR_ETC/shadow.ar" &&
				cat "$AR_ETC/shadow.ar" > "$AR_ETC/shadow" && rm -f "$AR_ETC/shadow.ar"
		else
			echo "$_l" >> "$AR_ETC/shadow"
		fi
	done
	sed -n 's/^member //p' "$1" | while read -r _g _u; do
		awk -F: -v g="$_g" -v u="$_u" 'BEGIN { OFS = ":" }
			$1 == g { n = split($4, m, ","); for (i = 1; i <= n; i++) if (m[i] == u) { print; next }
			          $4 = ($4 == "" ? u : $4 "," u) } { print }' "$AR_ETC/group" > "$AR_ETC/group.ar" &&
			cat "$AR_ETC/group.ar" > "$AR_ETC/group" && rm -f "$AR_ETC/group.ar"
	done
	return 0
}
