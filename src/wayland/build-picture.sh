#!/bin/sh
# Builds glassine-screen-picture (spec 010, glassine-screen-picture.c) into OUT: the
# wlr-screencopy client code first, from the copy in protocols/.
# build-picture.sh SRC OUT   SRC = src
# Needs gcc, pkgconf, wayland-dev.
set -eu
S=$1 O=$2
G=$(mktemp -d)
trap 'rm -rf "$G"' EXIT
wayland-scanner client-header "$S/wayland/protocols/wlr-screencopy-unstable-v1.xml" "$G/wlr-screencopy-unstable-v1-client-protocol.h"
wayland-scanner private-code "$S/wayland/protocols/wlr-screencopy-unstable-v1.xml" "$G/wlr-screencopy-unstable-v1-protocol.c"
gcc -O2 -Wall -Wextra -I"$G" -o "$O" "$S/glassine-screen-picture.c" "$G/wlr-screencopy-unstable-v1-protocol.c" \
    $(pkg-config --cflags --libs wayland-client)
