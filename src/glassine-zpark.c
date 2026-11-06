/* glassine-zpark: move one parked app's swapped memory from zram to disk,
 * compressed hard. glassine-bgprio has already paged the frozen app out
 * (MADV_PAGEOUT), so its anonymous memory sits in zram, compressed with the fast
 * first algorithm. This finds exactly that app's zram slots and
 *   1. recompresses them with zram's second algorithm (priority 1, registered by
 *      glassine-zram; needs a kernel whose zram recompress takes page_index= /
 *      page_indexes=, as writeback does; a refusal is reported and the writeback
 *      still runs),
 *   2. writes them to the backing device (glassine-zram's spill: a loop over a
 *      file on a btrfs mounted compress-force=zstd), then
 *   3. flushes the files given with -f and drops their page cache: the loop is
 *      buffered, so until then the written-back pages would still sit in RAM.
 * Only this app's memory reaches the disk; other apps' cold pages stay in RAM.
 *
 *   glassine-zpark [-n] [-R] [-W] [-d zram0] [-f FILE]... (-c CGROUP_DIR | PID...)
 *     -n  dry run: find the slots, print the counts, change nothing
 *     -R  no recompression      -W  no writeback
 *     -f  flush + drop the page cache of FILE after writeback (up to 8)
 *     -c  all processes of a cgroup (e.g. /sys/fs/cgroup/glassine-bg/frozen)
 * Usually run through `glassine-zram park`, which adds the spill's -f files.
 *
 * How the slots are found: /proc/PID/pagemap gives, for a swapped-out page, the
 * swap type and offset (root only: CAP_SYS_ADMIN, else the field reads 0). On a
 * swap device that is a raw block device the offset is the device page, i.e. the
 * zram slot index. Only mappings whose smaps "Swap:" is non-zero are read, so a
 * browser's huge reserved regions cost nothing. The swap type has to be zram's:
 * with one active swap device (ours) all swapped entries carry its type; when
 * the pages sit on more than one (another swap file switched on, e.g. for a
 * hibernate) it refuses. The app must stay frozen while this runs (bgprio's
 * job): a page it touched in between would simply lose its slot, which zram
 * skips.
 * Exit: 0 done (or nothing to do), 1 error, 2 usage. */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PM_SWAP (1ULL << 62)
#define PM_FILE (1ULL << 61)
#define SWP_TYPE(e) ((unsigned)((e) & 0x1f))
#define SWP_OFF(e) (((e) >> 5) & ((1ULL << 50) - 1))
#define ARGMAX 3500                     /* sysfs store gets one page; leave room */

static unsigned long *slots;
static size_t nslots, capslots;
static int seen_type = -1, mixed_types;

static void add_slot(unsigned long s)
{
    if (nslots == capslots) {
        capslots = capslots ? capslots * 2 : 4096;
        slots = realloc(slots, capslots * sizeof *slots);
        if (!slots) { perror("glassine-zpark: realloc"); exit(1); }
    }
    slots[nslots++] = s;
}

/* the swapped pages of [lo, hi) in one process */
static int scan_range(int pm, unsigned long lo, unsigned long hi)
{
    static uint64_t buf[4096];
    long psz = sysconf(_SC_PAGESIZE);
    for (unsigned long a = lo; a < hi;) {
        size_t n = (hi - a) / psz;
        if (n > 4096) n = 4096;
        ssize_t r = pread(pm, buf, n * 8, (off_t)(a / psz) * 8);
        if (r <= 0) return r < 0 ? -1 : 0;
        for (size_t i = 0; i < (size_t)r / 8; i++) {
            uint64_t e = buf[i];
            if (!(e & PM_SWAP) || (e & PM_FILE)) continue;
            unsigned t = SWP_TYPE(e);
            if (seen_type < 0) seen_type = t;
            else if ((int)t != seen_type) { mixed_types = 1; continue; }
            if (SWP_OFF(e)) add_slot(SWP_OFF(e));
        }
        a += (r / 8) * psz;
    }
    return 0;
}

static int scan_pid(int pid)
{
    char p[64], line[512];
    snprintf(p, sizeof p, "/proc/%d/smaps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    snprintf(p, sizeof p, "/proc/%d/pagemap", pid);
    int pm = open(p, O_RDONLY);
    if (pm < 0) { fclose(f); return -1; }
    unsigned long lo = 0, hi = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long a, b, kb;
        if (sscanf(line, "%lx-%lx ", &a, &b) == 2 && strchr(line, '-') < strchr(line, ' ')) {
            lo = a; hi = b;
        } else if (sscanf(line, "Swap: %lu kB", &kb) == 1 && kb && hi > lo) {
            scan_range(pm, lo, hi);
        }
    }
    close(pm);
    fclose(f);
    return 0;
}

static int cmp_ul(const void *a, const void *b)
{
    unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;
    return x < y ? -1 : x > y;
}

/* write "PREFIX page_indexes=a-b ..." in sysfs-sized pieces */
static int store_ranges(const char *path, const char *prefix)
{
    char arg[ARGMAX + 64];
    size_t len = 0;
    int fails = 0;
    for (size_t i = 0; i < nslots;) {
        size_t j = i;
        while (j + 1 < nslots && slots[j + 1] == slots[j] + 1) j++;
        char r[48];
        int rl = i == j ? snprintf(r, sizeof r, " page_index=%lu", slots[i])
                        : snprintf(r, sizeof r, " page_indexes=%lu-%lu", slots[i], slots[j]);
        if (len && len + rl > ARGMAX) {
            int fd = open(path, O_WRONLY);
            if (fd < 0 || write(fd, arg, len) < 0) { fprintf(stderr, "glassine-zpark: %s: %s\n", path, strerror(errno)); fails++; }
            if (fd >= 0) close(fd);
            len = 0;
        }
        if (!len) len = snprintf(arg, sizeof arg, "%s", prefix);
        memcpy(arg + len, r, rl);
        len += rl;
        i = j + 1;
    }
    if (len) {
        int fd = open(path, O_WRONLY);
        if (fd < 0 || write(fd, arg, len) < 0) { fprintf(stderr, "glassine-zpark: %s: %s\n", path, strerror(errno)); fails++; }
        if (fd >= 0) close(fd);
    }
    return fails ? -1 : 0;
}

static void stat_line(const char *dev, const char *f, unsigned long long *v, int n)
{
    char p[128];
    snprintf(p, sizeof p, "/sys/block/%s/%s", dev, f);
    FILE *fp = fopen(p, "r");
    for (int i = 0; i < n; i++) v[i] = 0;
    if (!fp) return;
    for (int i = 0; i < n && fscanf(fp, "%llu", &v[i]) == 1; i++) ;
    fclose(fp);
}

/* the written-back pages out of the page cache: to disk, then dropped */
static void flush_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "glassine-zpark: %s: %s\n", path, strerror(errno)); return; }
    if (fdatasync(fd)) fprintf(stderr, "glassine-zpark: fdatasync %s: %s\n", path, strerror(errno));
    posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
    close(fd);
}

int main(int argc, char **argv)
{
    const char *files[8];
    int nfiles = 0;
    const char *dev = "zram0", *cg = NULL;
    int dry = 0, norecomp = 0, nowb = 0, c;
    while ((c = getopt(argc, argv, "nRWd:c:f:")) != -1) {
        switch (c) {
        case 'n': dry = 1; break;
        case 'R': norecomp = 1; break;
        case 'W': nowb = 1; break;
        case 'd': dev = optarg; break;
        case 'c': cg = optarg; break;
        case 'f': if (nfiles < 8) files[nfiles++] = optarg; break;
        default:
            fprintf(stderr, "usage: glassine-zpark [-n] [-R] [-W] [-d zram0] [-f FILE]... (-c CGROUP_DIR | PID...)\n");
            return 2;
        }
    }
    if (!cg && optind >= argc) { fprintf(stderr, "glassine-zpark: no PID and no -c\n"); return 2; }

    int npid = 0;
    if (cg) {
        char p[512];
        snprintf(p, sizeof p, "%s/cgroup.procs", cg);
        FILE *f = fopen(p, "r");
        if (!f) { fprintf(stderr, "glassine-zpark: %s: %s\n", p, strerror(errno)); return 1; }
        int pid;
        while (fscanf(f, "%d", &pid) == 1) if (!scan_pid(pid)) npid++;
        fclose(f);
    }
    for (int i = optind; i < argc; i++) if (!scan_pid(atoi(argv[i]))) npid++;
    if (mixed_types) {
        fprintf(stderr, "glassine-zpark: swapped pages on more than one swap device: refusing\n");
        return 1;
    }
    qsort(slots, nslots, sizeof *slots, cmp_ul);
    size_t u = 0;
    for (size_t i = 0; i < nslots; i++) if (!u || slots[i] != slots[u - 1]) slots[u++] = slots[i];
    nslots = u;
    printf("glassine-zpark: %d process(es), %zu swapped slot(s) on swap type %d\n", npid, nslots, seen_type);
    if (!nslots || dry) return 0;

    char p[128];
    unsigned long long mm0[8], mm1[8], bd0[3], bd1[3];
    stat_line(dev, "mm_stat", mm0, 8);
    stat_line(dev, "bd_stat", bd0, 3);
    int err = 0;
    if (!norecomp) {
        snprintf(p, sizeof p, "/sys/block/%s/recompress", dev);
        if (store_ranges(p, "priority=1")) err = 1;
    }
    stat_line(dev, "mm_stat", mm1, 8);
    if (!norecomp)
        printf("glassine-zpark: recompressed: zram RAM %llu -> %llu KiB\n", mm0[1] >> 10, mm1[1] >> 10);
    if (!nowb) {
        snprintf(p, sizeof p, "/sys/block/%s/writeback", dev);
        if (store_ranges(p, "")) err = 1;
        for (int i = 0; i < nfiles; i++) flush_file(files[i]);
        stat_line(dev, "bd_stat", bd1, 3);
        stat_line(dev, "mm_stat", mm1, 8);
        printf("glassine-zpark: written back %llu page(s) (%llu KiB on disk now), zram RAM %llu KiB\n",
               bd1[2] - bd0[2], bd1[0] * 4, mm1[1] >> 10);
    }
    return err;
}
