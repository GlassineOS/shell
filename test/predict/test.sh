#!/bin/sh
# Host test of glassine-predict (spec 006, FR-003 to FR-008a): models built from the
# small English and German corpora here by the real builder, then the contract (ranking,
# several languages at once and which language learns, malformed and
# stale requests, random bytes), the private-field rule (one check per purpose and
# hint: nothing written, nothing on stderr), learning, removing, putting back and
# erasing words, the words command and the file modes. What it can't show: the real
# model's quality (tools/predict-model/build.sh and `glassine-predict bench` measure
# that) or the speed on a device.
set -eu
D=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$D/../../src" && pwd)
IMG=$(docker build -q -t glassine-predicttest "$D")
docker run --rm -v "$SRC":/src:ro -v "$D":/t:ro "$IMG" sh /t/run.sh
