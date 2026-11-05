#!/bin/sh
# Builds glassine-swayd (glassine-swayd.c + sway IPC + the drag handles, swayd-handles.c)
# into OUT: generates the layer-shell (protocols/, from sway 1.10.1) and
# input-method-v2 (from wlroots 0.18.2: the keyboard's auto-show) client code
# first. One place for every build: the mainline kit, build-in-container.sh,
# sway/build-arm64.sh, test/swayd, test/shell-sway.
# build-swayd.sh SRC OUT SRC = src
# Needs gcc, pkgconf, wayland-dev, wayland-protocols, json-c-dev, cairo-dev.
set -eu
S=$1 O=$2
G=$(mktemp -d)
trap 'rm -rf "$G"' EXIT
for x in "$S/wayland/protocols/wlr-layer-shell-unstable-v1.xml" "$S/wayland/protocols/input-method-unstable-v2.xml" \
         "$(pkg-config --variable=pkgdatadir wayland-protocols)/stable/xdg-shell/xdg-shell.xml"; do
    n=$(basename "$x" .xml)
    wayland-scanner client-header "$x" "$G/$n-client-protocol.h"
    wayland-scanner private-code "$x" "$G/$n-protocol.c"
done
gcc -O2 -Wall -Wno-format-truncation -I"$G" -o "$O" "$S/glassine-swayd.c" "$S/wayland/sway-ipc.c" "$S/wayland/swayd-handles.c" \
    "$G/wlr-layer-shell-unstable-v1-protocol.c" "$G/input-method-unstable-v2-protocol.c" "$G/xdg-shell-protocol.c" $(pkg-config --cflags --libs json-c wayland-client cairo) -lm
