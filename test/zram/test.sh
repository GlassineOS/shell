#!/bin/sh
# Host test of the zram service script (rootfs/usr/sbin/glassine-zram) in an Alpine
# container, without zram: a fake root (GLASSINE_ZRAM_ROOT) holds the zram sysfs
# files, /proc/swaps, /proc/meminfo, /proc/mounts, vm settings and userdata;
# losetup, mount, mkfs.btrfs, swapon and friends are stubs that log. Checks
# what it writes and decides: defaults from the RAM size, /etc/glassine/zram,
# deploy.conf paths, the spill (btrfs, plain file, none: not mounted, no room,
# size 0, no kernel support), a broken image recreated, stop, park, writeback,
# status and usage. shellcheck on the script and the service.
#   test.sh            exit 1 on a failed check
set -eu
D=$(cd "$(dirname "$0")" && pwd)
docker run --rm -v "$D/../../rootfs":/o:ro -v "$D":/t:ro alpine:3.24 sh -c 'apk add -q shellcheck >/dev/null 2>&1; sh /t/inner.sh'
