#!/bin/sh
# Host test of glassine-shutdown-screen (spec 009, SC-002) in an Alpine container
# (BusyBox, as on the device): a stand-in renderer and a stand-in panel daemon's FIFO.
set -eu
R=$(cd "$(dirname "$0")/../.." && pwd)
docker run --rm -v "$R":/r:ro alpine:3.24 sh /r/test/shutdown-screen/run.sh
