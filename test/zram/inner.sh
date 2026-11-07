#!/bin/sh
# (see test.sh) inside the container: /o = rootfs, /t = this dir
set -u
R=/f                    # the fake root
S=/s                    # stubs, their log and state
Z=$R/sys/block/zram0
UD=$R/run/glassine/userdata
IMG=$UD/media/glassine/zram/spill.btrfs
fail=0
is() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1"; echo "     got:  $2"; echo "     want: $3"; fail=1; fi; }
has() { if grep -qF -- "$3" "$2"; then echo "ok   $1"; else echo "FAIL $1: no '$3' in $2"; sed 's/^/     /' "$2"; fail=1; fi; }
hasnt() { if grep -qF -- "$3" "$2"; then echo "FAIL $1: '$3' in $2"; sed 's/^/     /' "$2"; fail=1; else echo "ok   $1"; fi; }
mib() { if [ -f "$1" ]; then echo $(( $(stat -c %s "$1") / 1048576 )); else echo none; fi; }

stub() { printf '#!/bin/sh\n%s\n' "$2" > $S/bin/$1; chmod 755 $S/bin/$1; }
stubs() {
    rm -rf $S/bin; mkdir -p $S/bin
    stub modprobe 'echo "modprobe $*" >> /s/log; exit 1'
    stub mkswap 'echo "mkswap $*" >> /s/log'
    stub swapon 'echo "swapon $*" >> /s/log; echo "/dev/zram0 partition 4000000 0 100" >> /f/proc/swaps'
    stub swapoff 'echo "swapoff $*" >> /s/log; sed -i "/^\/dev\/zram0 /d" /f/proc/swaps'
    stub mkfs.btrfs 'echo "mkfs.btrfs $*" >> /s/log'
    stub fallocate 'echo "fallocate $*" >> /s/log; truncate -s "$2" "$3"'
    stub df 'echo "Filesystem 1024-blocks Used Available Capacity Mounted"; echo "x 99999999 1000 $(cat /s/st/free_kb) 1% /"'
    stub mountpoint '[ "$1" = -q ] && shift; grep -qxF -- "$1" /s/st/mounted'
    stub umount 'echo "umount $*" >> /s/log; grep -vxF -- "$1" /s/st/mounted > /s/st/m; cat /s/st/m > /s/st/mounted
t=${1#/f}; awk -v t="$t" "\$2 != t" /f/proc/mounts > /s/st/pm; cat /s/st/pm > /f/proc/mounts'
    # mount -t btrfs -o OPTS SRC TARGET; /s/st/mount-fail: the next mount fails (a broken image)
    stub mount 'echo "mount $*" >> /s/log
[ -e /s/st/mount-fail ] && { rm /s/st/mount-fail; exit 32; }
echo "$6" >> /s/st/mounted; echo "$5 ${6#/f} $2 $4 0 0" >> /f/proc/mounts'
    # busybox: -f prints the next free device, DEV FILE binds; util-linux (/s/st/util-linux): --show
    stub losetup 'echo "losetup $*" >> /s/log
n=$(cat /s/st/loops 2>/dev/null || echo 0)
case "$1" in
--help) [ -e /s/st/util-linux ] && echo "     --direct-io[=<on|off>]  open backing file with O_DIRECT" || echo "BusyBox losetup"; exit 0 ;;
-d) exit 0 ;;
-f) [ $# = 1 ] && { echo /dev/loop$((n + 1)); exit 0; } ;;
esac
case " $* " in *" --show "*) [ -e /s/st/util-linux ] || exit 1; echo $((n + 1)) > /s/st/loops; echo /dev/loop$((n + 1)); exit 0 ;; esac
echo ${1#/dev/loop} > /s/st/loops'
    mkdir -p /usr/sbin
    printf '#!/bin/sh\necho "zpark $*" >> /s/log\n' > /usr/sbin/glassine-zpark; chmod 755 /usr/sbin/glassine-zpark
}

# a fresh device: zram files as the kernel offers them, 3.68 GiB RAM, userdata mounted, 20 GiB free
fresh() {
    rm -rf $R $S/st $S/log; mkdir -p $Z $R/proc/sys/vm $R/etc/glassine $UD/media $S/st
    for f in reset comp_algorithm recomp_algorithm algorithm_params compressed_writeback disksize mem_limit \
             writeback_limit_enable idle writeback; do : > $Z/$f; done
    echo none > $Z/backing_dev
    echo "100 30 40" > $Z/bd_stat
    echo "1073741824 268435456 285212672 0 300000000 0 0 0" > $Z/mm_stat
    printf 'Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n' > $R/proc/swaps
    echo "MemTotal:        3858000 kB" > $R/proc/meminfo
    echo 60 > $R/proc/sys/vm/swappiness; echo 3 > $R/proc/sys/vm/page-cluster
    : > $R/proc/mounts
    echo $UD > $S/st/mounted
    echo 20971520 > $S/st/free_kb
    : > $S/log
    stubs
}
run() { GLASSINE_ZRAM_ROOT=$R PATH=$S/bin:$PATH sh /o/usr/sbin/glassine-zram "$@" 2>$S/err; }
# the same with only the stubs and the plain tools the script and stubs use on
# PATH: a host's own mkfs.btrfs or losetup can't stand in for a missing one
run_bare() {
    rm -rf $S/sys; mkdir -p $S/sys
    for t in awk cat chmod dd df dirname grep mkdir rm sed stat sync tail truncate; do
        p=$(command -v $t) && ln -s "$p" $S/sys/$t
    done
    GLASSINE_ZRAM_ROOT=$R PATH=$S/bin:$S/sys /bin/sh /o/usr/sbin/glassine-zram "$@" 2>$S/err
}
z() { cat $Z/$1; }

echo "--- defaults: btrfs spill on userdata"
fresh
run start; is "start: exit 0" $? 0
is "first algorithm lz4" "$(z comp_algorithm)" lz4
is "second algorithm zstd, priority 1" "$(z recomp_algorithm)" "algo=zstd priority=1"
is "its level 3" "$(z algorithm_params)" "priority=1 level=3"
is "raw pages to the spill" "$(z compressed_writeback)" no
is "size = the RAM" "$(z disksize)" 3858000K
is "RAM limit = a quarter of it" "$(z mem_limit)" 964500K
is "no writeback limit" "$(z writeback_limit_enable)" 0
is "backing device = the loop over the wb file" "$(z backing_dev)" /dev/loop2
is "image: 2048 MiB in USERDATA_PREFIX/zram" "$(mib $IMG)" 2048
is "spill directory mode 700" "$(stat -c %a $UD/media/glassine/zram)" 700
is "wb file: 4096 MiB, sparse, on the btrfs" "$(mib $R/run/glassine-zram/wb)" 4096
has "btrfs made with its label" $S/log "mkfs.btrfs -q -f -m single -d single -L glassine-zram /dev/loop1"
has "mounted compress-force=zstd:9" $S/log "mount -t btrfs -o compress-force=zstd:9,noatime /dev/loop1 $R/run/glassine-zram"
has "image loop: busybox losetup binds it" $S/log "losetup /dev/loop1 $IMG"
has "swap at priority 100" $S/log "swapon -p 100 /dev/zram0"
is "swappiness 20" "$(cat $R/proc/sys/vm/swappiness)" 20
is "page-cluster 0" "$(cat $R/proc/sys/vm/page-cluster)" 0
has "says where the spill is" $S/err "spill to /dev/loop2 (btrfs zstd:9 in $IMG)"

: > $S/log
run start; is "start again: exit 0" $? 0
has "start again: already on" $S/err "already on"
hasnt "start again: nothing redone" $S/log "mkswap"

run status >/dev/null; is "status: exit 0" $? 0
run status > $S/out 2>&1
has "status: compression ratio" $S/out "RAM: stored 1024 MiB in 272 MiB (ratio 3.8), peak 286 MiB"
has "status: the spill" $S/out "spill /dev/loop2"
has "status: on disk" $S/out "disk: 0 MiB of pages, read back 0 MiB, written 0 MiB in total"

: > $S/log
run park 123 456; is "park: exit 0" $? 0
has "park: glassine-zpark writeback only (-R), the spill files and the PIDs" $S/log "zpark -R -f $R/run/glassine-zram/wb -f $IMG 123 456"
run park -c /sys/fs/cgroup/glassine-bg/frozen
has "park -c CGROUP passed on" $S/log "zpark -R -f $R/run/glassine-zram/wb -f $IMG -c /sys/fs/cgroup/glassine-bg/frozen"

run writeback 600; is "writeback 600: exit 0" $? 0
is "writeback: idle age" "$(z idle)" 600
is "writeback: idle pages" "$(z writeback)" idle
run writeback; is "writeback default age 1800" "$(z idle)" 1800
rm $Z/idle; mkdir $Z/idle                       # an age refused (no CONFIG_ZRAM_TRACK_ENTRY_ACTIME)
run writeback 600; is "idle age refused: exit 1" $? 1
has "idle age refused: names the kernel option" $S/err "idle age 600 refused (the kernel needs CONFIG_ZRAM_TRACK_ENTRY_ACTIME)"
rmdir $Z/idle; : > $Z/idle

: > $S/log
run stop; is "stop: exit 0" $? 0
has "stop: swap off" $S/log "swapoff /dev/zram0"
is "stop: zram reset" "$(z reset)" 1
has "stop: wb loop detached" $S/log "losetup -d /dev/loop2"
has "stop: spill unmounted" $S/log "umount $R/run/glassine-zram"
has "stop: image loop detached" $S/log "losetup -d /dev/loop1"
is "stop: the image stays" "$(mib $IMG)" 2048
run stop; is "stop when off: exit 0" $? 0

echo "--- stop when swapoff fails: nothing torn down"
fresh; run start
stub swapoff 'echo "swapoff $*" >> /s/log; exit 1'
: > $S/log; : > $Z/reset
run stop; is "swapoff fails: exit 1" $? 1
has "swapoff fails: says so" $S/err "stop failed: swapoff /dev/zram0"
is "swapoff fails: no zram reset" "$(z reset)" ""
hasnt "swapoff fails: no loop detached" $S/log "losetup -d"
hasnt "swapoff fails: spill not unmounted" $S/log "umount"
has "swapoff fails: zram0 still swap" $R/proc/swaps "/dev/zram0 "
is "swapoff fails: spill still attached" "$(z backing_dev)" /dev/loop2

echo "--- the service reports what the command returns"
sed "s|/usr/sbin/glassine-zram|$S/zram-cmd|" /o/etc/init.d/glassine-zram > $S/svc
svc() { printf '#!/bin/sh\nexit %s\n' "$2" > $S/zram-cmd; chmod 755 $S/zram-cmd
    ( ebegin() { :; }; eend() { echo "eend $1"; }; . $S/svc; $1 ); }
is "service stop: a failed stop reported" "$(svc stop 1)" "eend 1"
is "service stop: success" "$(svc stop 0)" "eend 0"
is "service start: a failed start reported" "$(svc start 1)" "eend 1"

echo "--- a failed start leaves nothing attached"
fresh; stub mkswap 'echo "mkswap $*" >> /s/log; exit 1'
run start; is "mkswap fails: exit 1" $? 1
has "mkswap fails: says so" $S/err "start failed: mkswap"
is "mkswap fails: zram reset" "$(z reset)" 1
has "mkswap fails: wb loop detached" $S/log "losetup -d /dev/loop2"
has "mkswap fails: spill unmounted" $S/log "umount $R/run/glassine-zram"
has "mkswap fails: image loop detached" $S/log "losetup -d /dev/loop1"
hasnt "mkswap fails: no swapon" $S/log "swapon"
fresh; stub swapon 'echo "swapon $*" >> /s/log; exit 1'
run start; is "swapon fails: exit 1" $? 1
has "swapon fails: spill unmounted" $S/log "umount $R/run/glassine-zram"

echo "--- the next boot: the image is reused, a broken one recreated"
echo none > $Z/backing_dev; rm -f $S/st/loops; : > $S/log
run start
hasnt "image of the right size: not reallocated" $S/log "fallocate"
hasnt "btrfs mounts: not made again" $S/log "mkfs.btrfs"
is "wb file new" "$(mib $R/run/glassine-zram/wb)" 4096
run stop; echo none > $Z/backing_dev; rm -f $S/st/loops; : > $S/log
touch $S/st/mount-fail
run start
has "btrfs doesn't mount: made again" $S/log "mkfs.btrfs -q -f -m single -d single -L glassine-zram"
is "... and the spill attached" "$(z backing_dev)" /dev/loop2

echo "--- util-linux losetup: direct I/O for the image"
fresh; touch $S/st/util-linux
run start
has "image loop with --direct-io=on" $S/log "losetup --direct-io=on -f --show $IMG"
has "wb loop without" $S/log "losetup -f --show $R/run/glassine-zram/wb"
is "backing device" "$(z backing_dev)" /dev/loop2

echo "--- no spill"
fresh; : > $S/st/mounted
run start; is "userdata not mounted: exit 0" $? 0
is "userdata not mounted: no backing device" "$(z backing_dev)" none
has "userdata not mounted: says so" $S/err "userdata not mounted at /run/glassine/userdata: no spill"
is "userdata not mounted: nothing written under its mount point" "$(ls $UD/media)" ""
has "userdata not mounted: zram still on" $S/log "swapon -p 100 /dev/zram0"

fresh; echo WB_SIZE_MB=0 > $R/etc/glassine/zram
run start
is "WB_SIZE_MB=0: no backing device" "$(z backing_dev)" none
has "WB_SIZE_MB=0: says so" $S/err "WB_SIZE_MB 0: no spill"

fresh; echo 5242880 > $S/st/free_kb
run start
is "5 GiB free, reserve 4 GiB: no backing device" "$(z backing_dev)" none
is "no room: no image" "$(mib $IMG)" none
has "no room: says so" $S/err "only 5120 MiB free"
fresh; echo 5242880 > $S/st/free_kb; echo USERDATA_RESERVE_MB=1024 > $R/etc/glassine/zram
run start
is "USERDATA_RESERVE_MB=1024: the image fits" "$(mib $IMG)" 2048

fresh; rm $Z/backing_dev
run start; is "no writeback in the kernel: exit 0" $? 0
has "no writeback in the kernel: says so" $S/err "no zram writeback in this kernel: no spill"
has "no writeback in the kernel: zram on" $S/log "swapon -p 100 /dev/zram0"

: > $S/log
run park 123; is "park without a spill: exit 0" $? 0
has "park without a spill: recompression only (-W), no files" $S/log "zpark -W 123"
run writeback; is "writeback without a spill: exit 0" $? 0
echo RECOMP_LEVEL=0 > $R/etc/glassine/zram; : > $S/log
run park 123; is "park without a spill or recompression: exit 0" $? 0
hasnt "park without a spill or recompression: no glassine-zpark" $S/log "zpark"

echo "--- no btrfs-progs: a plain file"
fresh; rm $S/bin/mkfs.btrfs
run_bare start
P=$UD/media/glassine/zram/writeback
is "plain file: WB_SIZE_MB" "$(mib $P)" 4096
is "plain file: no image" "$(mib $IMG)" none
is "plain file: the backing device" "$(z backing_dev)" /dev/loop1
has "plain file: says so" $S/err "no mkfs.btrfs (btrfs-progs): plain spill file"
: > $S/log; run park 7
has "plain file: park flushes it" $S/log "zpark -R -f $P 7"

echo "--- the btrfs spill fails after its mount: undone, the plain file instead"
P=$UD/media/glassine/zram/writeback
fresh; stub truncate 'case "$3" in */run/glassine-zram/wb) echo "truncate $*" >> /s/log; exit 1 ;; esac; exec /usr/bin/truncate "$@"'
run start; is "wb file fails: exit 0" $? 0
has "wb file fails: says so" $S/err "the writeback file $R/run/glassine-zram/wb failed"
has "wb file fails: btrfs unmounted" $S/log "umount $R/run/glassine-zram"
has "wb file fails: image loop detached" $S/log "losetup -d /dev/loop1"
is "wb file fails: no wb file left" "$(mib $R/run/glassine-zram/wb)" none
is "wb file fails: the image's space goes to the plain file" "$(mib $IMG)" none
is "wb file fails: plain file" "$(mib $P)" 4096
is "wb file fails: its loop is the backing device" "$(z backing_dev)" /dev/loop2
has "wb file fails: the log names the plain file" $S/err "spill to /dev/loop2 ($P)"
: > $S/log; run park 7
has "wb file fails: park flushes the plain file" $S/log "zpark -R -f $P 7"
hasnt "wb file fails: ... and not the btrfs files" $S/log "spill.btrfs"
fresh; stub truncate 'case "$3" in */run/glassine-zram/wb) exit 1 ;; esac; exec /usr/bin/truncate "$@"'
stub umount 'echo "umount $*" >> /s/log; exit 1'
run start; is "... and the btrfs won't unmount: exit 0" $? 0
has "btrfs won't unmount: says so" $S/err "umount $R/run/glassine-zram failed: no spill"
is "btrfs won't unmount: no backing device" "$(z backing_dev)" none
is "btrfs won't unmount: no plain file next to it" "$(mib $P)" none
has "btrfs won't unmount: zram on in RAM" $S/log "swapon -p 100 /dev/zram0"

echo "--- settings and paths"
fresh
printf 'ZRAM_SIZE=1G\nZRAM_MEM_LIMIT=256M\nZRAM_ALGO=lzo\nRECOMP_LEVEL=0\nSWAPPINESS=30\nSPILL_LEVEL=3\nSPILL_MB=512\nWB_SIZE_MB=1024\n' > $R/etc/glassine/zram
run start
is "ZRAM_SIZE" "$(z disksize)" 1G
is "ZRAM_MEM_LIMIT" "$(z mem_limit)" 256M
is "ZRAM_ALGO" "$(z comp_algorithm)" lzo
is "RECOMP_LEVEL=0: no second algorithm" "$(z recomp_algorithm)" ""
is "SWAPPINESS" "$(cat $R/proc/sys/vm/swappiness)" 30
has "SPILL_LEVEL" $S/log "compress-force=zstd:3,noatime"
is "SPILL_MB" "$(mib $IMG)" 512
is "WB_SIZE_MB" "$(mib $R/run/glassine-zram/wb)" 1024

fresh; printf 'RECOMP_ALGO=lz4hc\nRECOMP_LEVEL=9\n' > $R/etc/glassine/zram
run start
is "RECOMP_ALGO" "$(z recomp_algorithm)" "algo=lz4hc priority=1"
is "RECOMP_LEVEL" "$(z algorithm_params)" "priority=1 level=9"

fresh; printf 'USERDATA_MNT=/data\nUSERDATA_PREFIX=media/other\n' > $S/deploy.conf
mkdir -p $R/data/media; echo $R/data > $S/st/mounted
export GLASSINE_DEPLOY_CONF=$S/deploy.conf; run start; unset GLASSINE_DEPLOY_CONF
is "deploy.conf (GLASSINE_DEPLOY_CONF): USERDATA_MNT + USERDATA_PREFIX" "$(mib $R/data/media/other/zram/spill.btrfs)" 2048
fresh; printf 'USERDATA_MNT=/data\nUSERDATA_PREFIX=media/other\n' > $R/etc/glassine/deploy.conf
mkdir -p $R/data/media; echo $R/data > $S/st/mounted
run start
is "/etc/glassine/deploy.conf read" "$(mib $R/data/media/other/zram/spill.btrfs)" 2048
fresh; printf 'USERDATA_MNT="/data"\nUSERDATA_PREFIX='"'"'media/other'"'"'\n' > $R/etc/glassine/deploy.conf
mkdir -p $R/data/media; echo $R/data > $S/st/mounted
run start
is "deploy.conf: quoted values (it is shell)" "$(mib $R/data/media/other/zram/spill.btrfs)" 2048
fresh; printf 'USERDATA_MNT=data\n' > $R/etc/glassine/deploy.conf
run start
is "deploy.conf: a relative USERDATA_MNT: no spill" "$(z backing_dev)" none
has "deploy.conf: a relative USERDATA_MNT: says so" $S/err "USERDATA_MNT data: not an absolute path without '..': no spill"
fresh; printf 'USERDATA_PREFIX=media/../../etc\n' > $R/etc/glassine/deploy.conf
run start
is "deploy.conf: '..' in USERDATA_PREFIX: no spill" "$(z backing_dev)" none
is "deploy.conf: '..' in USERDATA_PREFIX: nothing made" "$(ls $UD/media)" ""

fresh; echo SPILL_DIR=/srv/spill > $R/etc/glassine/zram; mkdir -p $R/srv; : > $S/st/mounted
run start
is "SPILL_DIR: no userdata mount needed, its parent exists" "$(mib $R/srv/spill/spill.btrfs)" 2048
fresh; echo SPILL_DIR=/srv/spill > $R/etc/glassine/zram
run start
is "SPILL_DIR without its parent: no spill" "$(z backing_dev)" none

echo "--- failures and usage"
fresh; rm -rf $Z
run start; is "no zram: exit 1" $? 1
has "no zram: module tried" $S/log "modprobe zram num_devices=1"
has "no zram: says so" $S/err "no zram in this kernel"
fresh; : > $R/proc/meminfo
run start; is "no MemTotal and no sizes: exit 1" $? 1
has "no MemTotal: says so" $S/err "no MemTotal"
fresh; : > $R/proc/meminfo; printf 'ZRAM_SIZE=1G\nZRAM_MEM_LIMIT=256M\n' > $R/etc/glassine/zram
run start; is "no MemTotal, sizes set: exit 0" $? 0
fresh
run status > $S/out; is "status when off: exit 0" $? 0
has "status when off" $S/out "zram0 off"
run; is "no command: usage (exit 2)" $? 2
run frobnicate; is "unknown command: usage (exit 2)" $? 2

echo "--- shellcheck"
if command -v shellcheck >/dev/null; then
    shellcheck -s busybox /o/usr/sbin/glassine-zram && echo "ok   shellcheck glassine-zram" || fail=1
    shellcheck -s busybox -e SC2034 /o/etc/init.d/glassine-zram && echo "ok   shellcheck the service" || fail=1
else
    echo "skipped: no shellcheck in the container"
fi
exit $fail
