#!/bin/sh
# The administrator tiers of the root helpers, in a container: real accounts and hashes.
set -eu
D=$(cd "$(dirname "$0")" && pwd)
docker run --rm -v "$D/../../rootfs":/o:ro -v "$D":/t:ro alpine:3.24 sh -c 'apk add -q python3 >/dev/null 2>&1; sh /t/inner.sh'
