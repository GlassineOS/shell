#!/bin/sh
# Host test of glassine-predict (spec 006, FR-003 to FR-008a): a model built from the
# small corpus here by the real builder, then the contract (ranking, malformed and
# stale requests, random bytes), the private-field rule (one check per purpose and
# hint: nothing written, nothing on stderr), learning, removing, putting back and
# erasing words, the words command and the file modes. What it can't show: the English
# model's quality (tools/predict-model/build.sh and `glassine-predict bench` measure
# that) or the speed on a device.
set -eu
D=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$D/../../src" && pwd)
IMG=$(docker build -q -t glassine-predicttest "$D")
docker run --rm -v "$SRC":/src:ro -v "$D":/t:ro "$IMG" sh /t/run.sh
