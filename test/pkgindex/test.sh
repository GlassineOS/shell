#!/bin/sh
# Host test of the NetworkManager dispatcher script that refreshes the apk index
# (rootfs/etc/NetworkManager/dispatcher.d/60-glassine-pkgindex), with BusyBox in an
# Alpine container: the glassine wrapper and apk are stubs that log. Checks that the
# update goes through the wrapper when it is installed (with GLASSINE_APK_ACK=1, no
# terminal), through apk otherwise, the once-a-day stamp, a failed update. shellcheck
# on the script.
#   test.sh            exit 1 on a failed check
set -eu
D=$(cd "$(dirname "$0")" && pwd)
docker run --rm -v "$D/../../rootfs":/o:ro -v "$D":/t:ro alpine:3.24 sh -c 'apk add -q shellcheck >/dev/null 2>&1; sh /t/inner.sh'
