#!/bin/sh
# Builds libglassine-ink.so (the GTK 3 module, glassine-ink-gtk.c) into OUT: the
# glassine_ink_v1 client code first, from the copy in protocols/ (GlassineOS/protocols).
# build-ink.sh SRC OUT   SRC = src
# Needs gcc, pkgconf, wayland-dev, gtk+3.0-dev.
set -eu
S=$1 O=$2
G=$(mktemp -d)
trap 'rm -rf "$G"' EXIT
wayland-scanner client-header "$S/wayland/protocols/glassine-ink-v1.xml" "$G/glassine-ink-v1-client-protocol.h"
wayland-scanner private-code "$S/wayland/protocols/glassine-ink-v1.xml" "$G/glassine-ink-v1-protocol.c"
gcc -O2 -Wall -shared -fPIC -I"$G" -o "$O" "$S/wayland/glassine-ink-gtk.c" "$G/glassine-ink-v1-protocol.c" \
    $(pkg-config --cflags --libs gtk+-3.0 wayland-client)
