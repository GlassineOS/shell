#!/bin/sh
# The desktop health check (health.d/40-glassine-desktop) with BusyBox, in a container:
# stand-in processes started by absolute path, as the session starts them.
set -eu
D=$(cd "$(dirname "$0")" && pwd)
docker run --rm -v "$D/../../rootfs":/o:ro -v "$D":/t:ro alpine:3.24 sh -c 'apk add -q python3 >/dev/null 2>&1; sh /t/inner.sh'
