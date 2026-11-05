#!/bin/sh
# Host test of glassine-swayd : stock sway 1.10.1 headless (the
# device's version, without the glassine patches: glassine-swayd only talks IPC) at the
# panel's logical size, foot windows opened and closed, the shell's commands;
# checks the tiling (1-4 windows), the 4-apps rule, renumbering, swap/orient,
# a floating dialog, the hints glassine-epdd-desktop gets (a FIFO stand-in), swapdir, the
# physical-keyboard rule (fixtures) and sway's bare-Super release binding (wtype);
# the device's sway config + keys.conf pass `sway -C`, the key scripts shellcheck.
# test.sh exit 1 on a failed check
set -eu
D=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$D/../../src" && pwd)
docker build -q -t glassine-swaydtest "$D" >/dev/null
docker run --rm -v "$SRC":/src:ro -v "$D/../../rootfs":/o:ro -v "$D":/t:ro glassine-swaydtest sh /t/run.sh
