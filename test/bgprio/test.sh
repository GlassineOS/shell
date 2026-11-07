#!/bin/sh
# Host test of glassine-bgprio: stock sway headless + glassine-bgprio as root in a
# container with its own writable cgroup v2 tree (--privileged --cgroupns=private:
# the container's cgroup only). foot windows on several workspaces; checks demote
# (glassine-bg), freeze (glassine-bg/frozen), OOM score, page-out of what froze
# (process_madvise; only where the host has swap), thaw by datagram (the socket
# only sway's user's) and on an output change, promote on a switch, the park
# and its stop by a thaw (a glassine-zram stub), "freeze NAME", the desktop's own
# long-named processes never frozen, only the desktop user's processes touched
# (a root child of a hidden app is never demoted or frozen), bg_uclamp_max, and
# the exit with sway (everything released). sway and its apps run as a desktop
# user, bgprio as root.
#   test.sh            exit 1 on a failed check
set -eu
D=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$D/../../src" && pwd)
docker build -q -t glassine-bgpriotest "$D" >/dev/null
docker run --rm --privileged --cgroupns=private -v "$SRC":/src:ro -v "$D":/t:ro glassine-bgpriotest sh /t/run.sh
