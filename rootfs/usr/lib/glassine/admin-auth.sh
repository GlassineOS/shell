# Sourced by the root helpers (glassine-settings-root, glassine-power-root), which every desktop
# user may run through doas without a password. They decide per command who gets it:
# no check anyone with a desktop account
# auth_need_admin an administrator (group wheel), or anyone within AUTH_KEEP
# seconds of entering an administrator's password
# auth_need_password anyone within AUTH_KEEP seconds of entering that password
# Refused = exit 77, nothing done; the app then asks (glassine-settings page admin-auth)
# and sends the password to `glassine-settings-root auth`. Root itself is never asked.
# doas sets DOAS_USER and clears the rest of the environment, so it can't be forged.
AUTH_DIR=/run/glassine/admin-auth
AUTH_KEEP=300
AUTH_REFUSED=77

auth_user() { printf '%s' "${DOAS_USER:-root}"; }

auth_is_admin() {
	[ "$(auth_user)" = root ] && return 0
	id -nG "$(auth_user)" 2>/dev/null | tr ' ' '\n' | grep -qx wheel
}

# /proc/uptime counts suspend and doesn't jump with the clock
auth_now() { read -r _u _ < /proc/uptime; echo "${_u%%.*}"; }

auth_fresh() {
	[ "$(auth_user)" = root ] && return 0
	_t=$(cat "$AUTH_DIR/$(auth_user)" 2>/dev/null) || return 1
	case "$_t" in ''|*[!0-9]*) return 1 ;; esac
	_n=$(auth_now)
	[ "$_n" -ge "$_t" ] && [ $((_n - _t)) -lt "$AUTH_KEEP" ]
}

auth_refuse() { echo "this needs an administrator's password" >&2; exit "$AUTH_REFUSED"; }
auth_need_admin() { auth_is_admin || auth_fresh || auth_refuse; }
auth_need_password() { auth_fresh || auth_refuse; }

# the password on stdin. One check at a time, 2 s after a wrong one.
auth_submit() {
	mkdir -p "${AUTH_DIR%/*}"
	( umask 077; mkdir -p "$AUTH_DIR"; exec 9>"$AUTH_DIR/.lock"; flock 9
	  if /usr/sbin/glassine-checkpass; then
		auth_now > "$AUTH_DIR/$(auth_user)"
		logger -t glassine-auth -p auth.notice "administrator password accepted for $(auth_user)" 2>/dev/null
		exit 0
	  fi
	  rm -f "$AUTH_DIR/$(auth_user)"
	  logger -t glassine-auth -p auth.warning "wrong administrator password from $(auth_user)" 2>/dev/null
	  sleep 2
	  exit 1 )
}
