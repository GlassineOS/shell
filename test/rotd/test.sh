#!/bin/sh
# Host test of glassine-rotd (spec 005): the decision logic by replay, then the daemon
# against a stand-in IIO accelerometer (sysfs files and a FIFO as its character device,
# under GLASSINE_ROTD_SYSROOT) and stock sway 1.10.1 headless: auto turns, a paused
# stream (the power daemon's suspend), fixed turns with the stream off and its rate put
# back, back to auto, a new sway getting the turn again, a missing sensor with a fixed
# turn. What it can't show: a real IIO buffer (its read after a disable), real axes.
# test.sh exit 1 on a failed check
set -eu
D=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$D/../../src" && pwd)
IMG=$(docker build -q -t glassine-rotdtest "$D")
docker run --rm -v "$SRC":/src:ro -v "$D":/t:ro "$IMG" sh /t/run.sh
