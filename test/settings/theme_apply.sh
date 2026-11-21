#!/bin/sh
# glassine-theme-apply's text settings (spec 012 FR-032): text-scaling-factor is the text
# size (~/.config/glassine/text-size), never the interface size; the font sizes stay the
# voice's; a value already set is not set again (every set restyles the GTK apps).
# theme_apply.sh REPO   (a fake gsettings keeps the keys in a file; flatpak is absent)
set -u
R=$1
T=$(mktemp -d)
export HOME=$T/home XDG_CONFIG_HOME=$T/home/.config
mkdir -p "$T/bin" "$XDG_CONFIG_HOME/glassine" "$T/share"
cat > "$T/bin/gsettings" <<'EOF'
#!/bin/sh
# gsettings get|set SCHEMA KEY [VALUE]: the keys in $GS_DB, the sets in $GS_LOG
k="$2 $3"
case $1 in
    get) v=$(grep -F "$k=" "$GS_DB" 2>/dev/null | tail -n 1); [ -n "$v" ] && echo "${v#*=}" ;;
    set) echo "$k=$4" >> "$GS_DB"; echo "$3 $4" >> "$GS_LOG" ;;
esac
EOF
chmod +x "$T/bin/gsettings"
export PATH="$T/bin:$PATH" GS_DB=$T/db GS_LOG=$T/log
# the GTK 4 css is copied from /usr/share/glassine/gtk4: give it one
mkdir -p /usr/share/glassine/gtk4 && cp "$R"/rootfs/usr/share/glassine/gtk4/*.css /usr/share/glassine/gtk4/
fail=0
ok() { if [ "$1" = "$2" ]; then echo "ok   theme-apply: $3"; else echo "FAIL theme-apply: $3: got '$1', want '$2'"; fail=1; fi; }
get() { grep -F "org.gnome.desktop.interface $1=" "$GS_DB" | tail -n 1 | sed 's/^[^=]*=//'; }
run() { : > "$GS_LOG"; sh "$R/rootfs/usr/bin/glassine-theme-apply" "$1" >/dev/null 2>&1; }

run white
ok "$(get text-scaling-factor)" 1 "no text-size file: the factor 1"
ok "$(get font-name)" "'IBM Plex Sans 10'" "the voice's font at its own size"
ok "$(get cursor-size)" 24 "the pointer 24 logical px"
for p in 105 125 150; do
    echo $p > "$XDG_CONFIG_HOME/glassine/text-size"
    run white
    ok "$(get text-scaling-factor)" "$(echo $p | awk '{ printf "%g", $1 / 100 }')" "text size $p %"
    ok "$(get font-name)" "'IBM Plex Sans 10'" "text size $p %: the font size stays"
done
# a double as GVariant prints it reads back as the same number: not set again
printf 'org.gnome.desktop.interface text-scaling-factor=1.5000000000000000\n' >> "$GS_DB"
run white
ok "$(cat "$GS_LOG")" "" "nothing set again when nothing changed"
for bad in 99 155 112 abc ""; do
    printf '%s\n' "$bad" > "$XDG_CONFIG_HOME/glassine/text-size"
    run editorial
    ok "$(get text-scaling-factor)" 1 "text size '$bad' counts as 100 %"
done
# the interface size never reaches the text settings
echo 125 > "$XDG_CONFIG_HOME/glassine/text-size"
printf '80 Glassine Panel Unknown\n' > "$XDG_CONFIG_HOME/glassine/ui-size"
run stationery
ok "$(get text-scaling-factor)" 1.25 "an interface size of 80 % leaves the factor at the text size"
ok "$(get font-name)" "'EB Garamond 11'" "... and the font at the voice's size"
rm -rf "$T"
exit $fail
