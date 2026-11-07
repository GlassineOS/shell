#!/bin/sh
# Builds glassine-shell-wl (glassine-shell with the Wayland backend, glassine-shell-wl.c) into
# OUT: generates the layer-shell (protocols/, from sway 1.10.1), screencopy
# (protocols/, from Alpine's wlr-protocols) and xdg-shell
# (wayland-protocols) and cursor-shape (+ tablet-v2, which it names; wayland-protocols)
# client code first. One place for every build: the mainline
# kit, build-in-container.sh, sway/build-arm64.sh, test/shell-sway.
# build-shell-wl.sh SRC OUT SRC = src
# Needs gcc, pkgconf, wayland-dev, wayland-protocols, json-c-dev, libxkbcommon-dev,
# pango-dev, librsvg-dev, glib-dev, linux-headers, and GlassineOS/ui installed (pkg-config
# glassine-ui: the voices and the drawing).
set -eu
S=$1 O=$2
G=$(mktemp -d)
trap 'rm -rf "$G"' EXIT
for x in "$S/wayland/protocols/wlr-layer-shell-unstable-v1.xml" \
         "$S/wayland/protocols/wlr-screencopy-unstable-v1.xml" \
         "$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml" \
         "$(pkg-config --variable=pkgdatadir wayland-protocols)/staging/cursor-shape/cursor-shape-v1.xml" \
         "$(pkg-config --variable=pkgdatadir wayland-protocols)/unstable/tablet/tablet-unstable-v2.xml"; do
    n=$(basename "$x" .xml)
    wayland-scanner client-header "$x" "$G/$n-client-protocol.h"
    wayland-scanner private-code "$x" "$G/$n-protocol.c"
done
gcc -O2 -Wall -Wno-format-truncation -I"$G" -o "$O" "$S/glassine-shell.c" "$S/glassine-shell-wl.c" \
    "$S/glassine-shell-notify.c" "$S/wayland/sway-ipc.c" "$G/wlr-layer-shell-unstable-v1-protocol.c" "$G/wlr-screencopy-unstable-v1-protocol.c" "$G/xdg-shell-protocol.c" \
    "$G/cursor-shape-v1-protocol.c" "$G/tablet-unstable-v2-protocol.c" \
    $(pkg-config --cflags --libs glassine-ui pangocairo librsvg-2.0 gio-2.0 wayland-client xkbcommon json-c) -lm
