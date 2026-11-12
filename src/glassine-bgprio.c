/* glassine-bgprio: apps on other workspaces get the CPU only when the apps in
 * front don't want it, are frozen when hidden for long, and their memory leaves
 * RAM.
 *
 * Follows the sway session over its IPC (the desktop user's socket,
 * /run/user/(*)/sway-ipc.*.sock, waited for up to 60 s; exits when sway does).
 * A view's owner is its "pid" (the client's host pid from the socket's peer
 * credentials; for an Xwayland window wlroots 0.18 asks the X server's XRes for
 * the X client's pid and ignores _NET_WM_PID). A process with a view on a
 * visible workspace (or a sticky view) is in front; one whose views are all
 * elsewhere (the scratchpad counts as elsewhere) is in the background. Processes
 * without a view of their own go with the nearest view owner among their
 * ancestors (a browser's content processes, a build started in a terminal).
 *
 * Only the session's processes are touched: a process is demoted, frozen or
 * boosted only when its real, effective, saved and file uids and its view
 * owner's are all sway's user (the peer of sway's IPC socket), and that user is
 * not root. Anything else under a view owner (a root `apk` the settings app
 * started through doas, a `doas` in a terminal, a pid an X client named) stays
 * where it is, and goes back home if it was ours before (it inherited the group).
 *
 * Background processes move to the cgroup v2 group /sys/fs/cgroup/glassine-bg
 * (cpu.weight 1: almost nothing against the rest when the CPU is contended, all
 * of it when it isn't; cpu.uclamp.max bg_uclamp_max %, see below), their threads
 * to idle I/O priority and a 50 ms timer slack, so their timers coalesce. All
 * three are inherited by new threads and children. Back in front = the cgroup
 * this daemon started in (the session's, where the whole desktop runs), I/O
 * priority and timer slack back to the defaults. A cgroup and not nice or
 * SCHED_IDLE: autogroups put terminal-started apps in groups of their own, and
 * nice only counts within a group.
 *
 * bg_uclamp_max: schedutil adds its 25 % headroom first and clamps after, so
 * with 79 % background work alone never asks for the top frequency of a cluster
 * (79 x 1.25 < 100); how far below the top it lands depends on the CPU's
 * operating points. A foreground task on the same CPU lifts the clamp.
 *
 * Coming to the front is immediate (on sway's event, before the app repaints);
 * going to the background waits DEMOTE_S, so flipping between workspaces costs
 * nothing. Rescan every RESCAN_S (new processes and windows, the conf).
 *
 * Freeze when hidden: a process in the background for freeze_after seconds
 * (120) moves on into glassine-bg/frozen, whose cgroup.freeze is 1: no CPU, no
 * timer wakeups at all. Coming to the front moves it back home, which thaws it
 * (leaving a frozen cgroup thaws). A frozen app answers nothing (Wayland events
 * queue, D-Bus calls wait), so the shell asks for a thaw before it closes a
 * hidden window: a datagram "thaw CON_ID" (sway's container id) on
 * /run/glassine-bgprio.sock (sway's user's, mode 0600; at most THAW_BURST per
 * wakeup, the rest waits for the next) moves that view's app out of the freezer
 * and restarts its freeze timer. An output change thaws every frozen app (sway
 * sends every client a burst of events at once).
 *
 * Parked memory: after a pass that froze something, the processes it froze get
 * process_madvise(MADV_PAGEOUT) over their anonymous mappings (in a thread: it
 * takes a while), so the kernel swaps them into zram at once instead of under
 * the next app start's memory pressure; a thaw pages them back in as they are
 * touched. The page-out goes through a pidfd taken at the freeze, and only while
 * the process is still in the frozen group (checked before each batch of
 * mappings): one thawed meanwhile, or a pid that is now another process's, is
 * left alone. Not memory.reclaim on the frozen group: cgroup v2 leaves a moved
 * process's pages charged where they were allocated (its home group), so the
 * frozen group holds next to nothing. Only while a swap device is on (reclaim
 * swap, the default). park_after seconds after an app froze (600),
 * `glassine-zram park PID...` moves that app's zram slots on to the disk spill
 * (without one: recompresses them in RAM), one app at a time, on the RESCAN_S
 * pass. Thawing an app while its park runs stops the park; an app frozen again
 * is parked again. Optional instead (writeback_after, off by default: it also
 * writes the front app's idle pages): writeback_after seconds after the first
 * freeze of a cluster, one
 * `glassine-zram writeback 1800` moves every page idle that long; freezes after
 * it was scheduled get one more. Frozen processes get oom_score_adj 1000 (the
 * OOM killer takes a parked app before the one in front), their own value back
 * when they thaw.
 *
 * Never frozen (only deprioritized), checked when an app is due to freeze:
 *   - its process tree owns an MPRIS name (org.mpris.MediaPlayer2.*) on its
 *     session bus: a media player, which media keys must reach even when paused;
 *   - its process tree has a PipeWire output stream that plays (pactl: an
 *     uncorked sink input; pipewire-pulse lists native streams too);
 *   - it is a Flatpak whose app id has "yes" in the portal's background
 *     permission table (the app asked to run in the background);
 *   - it is part of the desktop (never_freeze[]: the desktop blocks on those)
 *     or a "nofreeze" name in the conf.
 * The queries run as the app's user with its session's DBUS_SESSION_BUS_ADDRESS
 * and XDG_RUNTIME_DIR (from /proc/PID/environ of the app, else a parent, else
 * any process of that user: some browsers overwrite their environ, a Flatpak's
 * bus is the sandbox proxy), in their own process group killed after a 6 s read
 * deadline, at most once per full pass and only while something is due to
 * freeze.
 *
 * Flatpaks run in their own pid namespace: a pulse client's
 * application.process.id and an MPRIS name's owner (xdg-dbus-proxy) are not the
 * app's host pids. So a PipeWire stream counts by host pid (pipewire.sec.pid for
 * native clients) or by its Flatpak app id (pipewire.access.portal.app_id), and
 * any process whose /.flatpak-info names a view owner's app id belongs to that
 * owner's app (a browser's renderers, spawned through the Flatpak portal, are
 * not under the browser).
 *
 * Foreground boost (opt-in): with /etc/glassine/fg-boost holding a number N
 * (0-100), the processes in front (a view on a visible workspace, and their
 * children) move to /sys/fs/cgroup/glassine-fg with cpu.uclamp.min N %. The
 * scheduler then treats their busy threads as at least that big: above a small
 * cluster's capacity they are placed on the big cores, and schedutil runs their
 * CPU at least that high. glassine-fg is a sibling of the session's cgroup, so N
 * replaces any floor the session's group has for those apps. One rule for every
 * app, no per-app list. Off in the power saver mode (/etc/glassine/ddr starts
 * with "saver"). Never boosted: the desktop itself (no_boost[]). No file, an
 * empty one or 0 = off (everything back home). Read on every RESCAN_S pass.
 *
 * /etc/glassine/bgprio.conf ('#' comments; re-read on every RESCAN_S pass):
 *   NAME...                process names (comm, as in ps) never demoted, e.g. a
 *                          music player (audio would stutter)
 *   nofreeze NAME...       demoted but never frozen (downloads, chat)
 *   freeze NAME...         frozen as soon as demoted (DEMOTE_S hidden), even
 *                          while it plays audio or owns an MPRIS name; the
 *                          desktop's never_freeze[] still wins
 *                          (nofreeze/freeze: a process name or, with a dot, a
 *                          Flatpak app id; glassine-settings > Apps "Freeze in
 *                          background" Off/Always writes them, Auto = neither)
 *   freeze_after SECONDS   hidden time before freezing (120); 0 = never freeze
 *   reclaim swap|always|off  frozen apps' anon memory paged out after a
 *                          freeze (swap: only while a swap device is on; default)
 *   writeback_after SECONDS  first freeze -> `glassine-zram writeback 1800`
 *                          (0 = never, the default)
 *   park_after SECONDS     an app frozen this long -> `glassine-zram park` of
 *                          its processes (600; 0 = never)
 *   bg_uclamp_max PERCENT  the background group's cpu.uclamp.max (79; 100 = no cap)
 * Process names compare as the kernel keeps them: the first 15 characters.
 *
 * Runs as root (cgroups, other users' processes), started by
 * glassine-session-sway; `-sway` is accepted for older callers and changes
 * nothing. Needs cgroup v2 with the cpu controller (the freezer and uclamp are
 * optional: without them it logs what it can't do). Log: stderr.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <grp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "wayland/sway-ipc.h"

#define CONF "/etc/glassine/bgprio.conf"
#define FG_BOOST_CONF "/etc/glassine/fg-boost"
#define POWER_MODE "/etc/glassine/ddr"
#define THAW_SOCK "/run/glassine-bgprio.sock"
#define ZRAM_CMD "/usr/sbin/glassine-zram"
#define CG_ROOT "/sys/fs/cgroup"
#define BG_GROUP "/glassine-bg"
#define FROZEN_GROUP "/glassine-bg/frozen"
#define FG_GROUP "/glassine-fg"
#define BG_UCLAMP_MAX 79
#define FREEZE_S 120
#define WRITEBACK_AFTER_S 0             /* off: only parked apps reach the disk */
#define WRITEBACK_IDLE "1800"
#define PARK_AFTER_S 600
#define OOM_NONE (-32768)               /* oomsave: nothing saved */
#define BG_SLACK_NS "50000000"          /* 50 ms */
#define DEMOTE_S 3
#define RESCAN_S 30
#define THAW_BURST 16                   /* thaw datagrams handled per wakeup */
#define COMM_LEN 15                     /* the kernel's comm, without the NUL */
#define IOPRIO_WHO_PROCESS 1
#define IOPRIO_CLASS_IDLE 3
#define IOPRIO_CLASS_SHIFT 13

static int sw_cmd = -1, sw_ev = -1, thaw_fd = -1;   /* IPC connections, the thaw socket */
static char home[256];                /* our own cgroup, relative to CG_ROOT ("" = the root) */
static int pid_max;
static int *ppid;                     /* per pid, this pass; -1 = no process */
static char (*comm)[16];
static signed char *win;              /* per pid: 0 no view, 1 front, 2 background */
static char *inbg;                    /* per pid: 1 in glassine-bg, 2 in glassine-bg/frozen */
static double *bgsince;               /* per pid: when it was demoted (0 = unknown: now) */
static char *exempt_logged;           /* per pid: "not freezing" already logged */
static uid_t session_uid = (uid_t)-1; /* sway's user, from its IPC socket */
static signed char *uidok;            /* per pid, this pass: 1 the session user's, -1 not, 0 not read */
static char nofrz[64][128], alwfrz[64][128];   /* nofreeze / freeze names: comm or a Flatpak app id */
static int nalwfrz, nnofrz, freeze_s = FREEZE_S, can_freeze;
static int bg_clamp = BG_UCLAMP_MAX, bg_clamp_set = -1;   /* conf; what the group has (-1: not written) */
static int fg_boost = -1;             /* fg-boost: cpu.uclamp.min %; 0 = off, -1 = not read yet */
static char *infg;                    /* per pid: 1 in glassine-fg */
enum { RECLAIM_OFF, RECLAIM_SWAP, RECLAIM_ALWAYS };
static int reclaim_mode = RECLAIM_SWAP, can_reclaim = 1, writeback_s = WRITEBACK_AFTER_S;
#define MAXFROZE 512
static int froze[MAXFROZE], froze_fd[MAXFROZE], nfroze;   /* frozen in this pass, their pidfds */
static double wb_at, last_freeze;               /* the pending writeback; the latest freeze */
static pid_t wb_pid;
static int park_s = PARK_AFTER_S;
static double *frozat;                /* per pid: when it froze (0 = not frozen by us) */
static int *frzown;                   /* per pid: the view owner it froze with */
static char *parkst;                  /* per pid: 1 = parked (or its park runs) since it froze */
static short *oomsave;                /* per pid: oom_score_adj before the freeze (OOM_NONE) */
static pid_t pk_pid;                  /* the running `glassine-zram park` */
static int pk_owner;                  /* ... and the app it parks */
static char excl[64][16];
static int nexcl;
static volatile sig_atomic_t quit;
static double now(void);
static int pidfd_of(int pid);

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...)
{
    char ts[32];
    time_t t = time(NULL);
    strftime(ts, sizeof ts, "%m-%d %H:%M:%S", localtime(&t));
    fprintf(stderr, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int write_str(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY), r = -1;
    if (fd < 0) return -1;
    if (write(fd, s, strlen(s)) == (ssize_t)strlen(s)) r = 0;
    close(fd);
    return r;
}

/* a process name as the kernel keeps it (COMM_LEN characters) against a full
 * program name: "glassine-epdd-desktop" is "glassine-epdd-d" in comm */
static int same_comm(const char *c, const char *name)
{
    size_t n = strlen(name);
    if (n > COMM_LEN) n = COMM_LEN;
    return strlen(c) == n && !strncmp(c, name, n);
}

/* ---- cgroup ------------------------------------------------------------- */

/* the background group's cap (bg_uclamp_max), when it changed */
static void apply_bg_clamp(void)
{
    char b[16];
    if (bg_clamp == bg_clamp_set) return;
    snprintf(b, sizeof b, "%d", bg_clamp);
    if (write_str(CG_ROOT BG_GROUP "/cpu.uclamp.max", bg_clamp >= 100 ? "max" : b))
        logf_("no cpu.uclamp.max (%s): background work may still use the top frequencies", strerror(errno));
    else if (bg_clamp_set >= 0)
        logf_("background cap now %d %%", bg_clamp);
    bg_clamp_set = bg_clamp;
}

static int setup_cgroup(void)
{
    char line[256];
    if (access(CG_ROOT "/cgroup.controllers", F_OK)) { errno = ENOTSUP; return -1; }   /* not cgroup v2 */
    FILE *f = fopen("/proc/self/cgroup", "r");
    home[0] = 0;
    while (f && fgets(line, sizeof line, f))
        if (!strncmp(line, "0::", 3)) {
            line[strcspn(line, "\n")] = 0;
            snprintf(home, sizeof home, "%s", strcmp(line + 3, "/") ? line + 3 : "");
        }
    if (f) fclose(f);
    if (mkdir(CG_ROOT BG_GROUP, 0755) && errno != EEXIST) return -1;
    if (write_str(CG_ROOT BG_GROUP "/cpu.weight", "1")) return -1;     /* no cpu controller */
    if (mkdir(CG_ROOT FG_GROUP, 0755) && errno != EEXIST)
        logf_("no glassine-fg cgroup (%s): no foreground boost", strerror(errno));
    /* a child without controllers of its own: allowed next to glassine-bg's own
     * processes, and it stays under glassine-bg's weight and cap */
    if ((mkdir(CG_ROOT FROZEN_GROUP, 0755) && errno != EEXIST) ||
        write_str(CG_ROOT FROZEN_GROUP "/cgroup.freeze", "1"))
        logf_("no freezer (%s): hidden apps are deprioritized, not frozen", strerror(errno));
    else
        can_freeze = 1;
    return 0;
}

static void set_ioprio(int pid, int idle)
{
    char p[64];
    snprintf(p, sizeof p, "/proc/%d/task", pid);
    DIR *d = opendir(p);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        int tid = atoi(e->d_name);
        if (tid <= 0) continue;
        if (idle) {
            syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, tid, IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT);
        } else {
            long cur = syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, tid);
            if (cur >= 0 && (cur >> IOPRIO_CLASS_SHIFT) == IOPRIO_CLASS_IDLE)
                syscall(SYS_ioprio_set, IOPRIO_WHO_PROCESS, tid, 0);
        }
    }
    closedir(d);
}

/* move a whole process ("" = back to our own cgroup, where the desktop runs) */
static int move_to(int pid, const char *group)
{
    char p[544], s[16];
    if (!*group) group = home;
    snprintf(s, sizeof s, "%d", pid);
    snprintf(p, sizeof p, CG_ROOT "%s/cgroup.procs", group);
    return write_str(p, s);
}

/* timer slack of every thread: BG_SLACK_NS, or "0" = back to the default */
static void set_slack(int pid, const char *ns)
{
    char p[64];
    snprintf(p, sizeof p, "/proc/%d/task", pid);
    DIR *d = opendir(p);
    struct dirent *e;
    if (!d) return;
    while ((e = readdir(d))) {
        char f[96];
        int tid = atoi(e->d_name);
        if (tid <= 0) continue;
        /* timerslack_ns exists only at the top level; /proc/<tid>/ is that thread */
        snprintf(f, sizeof f, "/proc/%d/timerslack_ns", tid);
        write_str(f, ns);
    }
    closedir(d);
}

/* log the view owners only (a browser alone has a dozen processes) */
static void demote(int pid, int loud)
{
    if (move_to(pid, BG_GROUP)) { logf_("can't demote %s (%d): %s", comm[pid], pid, strerror(errno)); return; }
    set_ioprio(pid, 1);
    set_slack(pid, BG_SLACK_NS);
    inbg[pid] = 1;
    infg[pid] = 0;
    bgsince[pid] = now();
    if (loud) logf_("background %s (%d)", comm[pid], pid);
}

static int oom_adj(int pid)
{
    char p[64];
    snprintf(p, sizeof p, "/proc/%d/oom_score_adj", pid);
    FILE *f = fopen(p, "r");
    int v = OOM_NONE;
    if (f && fscanf(f, "%d", &v) != 1) v = OOM_NONE;
    if (f) fclose(f);
    return v;
}
static void set_oom_adj(int pid, int v)
{
    char p[64], b[16];
    snprintf(p, sizeof p, "/proc/%d/oom_score_adj", pid);
    snprintf(b, sizeof b, "%d", v);
    write_str(p, b);
}

/* a frozen process leaves the freezer (thaw, front, exit of bgprio): its own
 * oom_score_adj back; a park of its app now is pointless (it pages back in) */
static void unfrozen(int pid)
{
    if (oomsave[pid] != OOM_NONE) set_oom_adj(pid, oomsave[pid]);
    else if (inbg[pid] == 2 && oom_adj(pid) == 1000) set_oom_adj(pid, 0);   /* a previous instance's */
    oomsave[pid] = OOM_NONE;
    if (pk_pid > 0 && frozat[pid] && frzown[pid] == pk_owner) {
        kill(pk_pid, SIGTERM);
        logf_("park of %s (%d) stopped: it thawed", comm[pk_owner], pk_owner);
        pk_owner = -1;
    }
    frozat[pid] = 0;
    parkst[pid] = 0;
}

static void freeze(int pid, int owner, int loud)
{
    if (move_to(pid, FROZEN_GROUP)) { logf_("can't freeze %s (%d): %s", comm[pid], pid, strerror(errno)); return; }
    inbg[pid] = 2;
    frozat[pid] = now();
    frzown[pid] = owner;
    parkst[pid] = 0;
    int v = oom_adj(pid);
    if (v != OOM_NONE && v != 1000) { oomsave[pid] = (short)v; set_oom_adj(pid, 1000); }
    /* the pidfd now: the page-out comes later, when the pid may be someone else's */
    if (nfroze < MAXFROZE) { froze[nfroze] = pid; froze_fd[nfroze++] = pidfd_of(pid); }
    if (loud) logf_("frozen %s (%d)", comm[pid], pid);
}

static void promote(int pid, int loud)
{
    if (inbg[pid] == 2 || frozat[pid]) unfrozen(pid);
    move_to(pid, "");                 /* out of a frozen group = thawed */
    set_ioprio(pid, 0);
    set_slack(pid, "0");
    if (loud) logf_("front %s (%d)%s", comm[pid], pid, inbg[pid] == 2 ? " (thawed)" : "");
    inbg[pid] = 0;
    bgsince[pid] = 0;
    exempt_logged[pid] = 0;
}

/* ---- parked memory (see the header) ------------------------------------- */

static int swap_on(void)
{
    FILE *f = fopen("/proc/swaps", "r");
    char l[256];
    int n = 0;
    while (f && fgets(l, sizeof l, f)) n++;   /* a header line + one per device */
    if (f) fclose(f);
    return n > 1;
}

#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_process_madvise
#define SYS_process_madvise 440
#endif
#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif
#ifndef MADV_PAGEOUT
#define MADV_PAGEOUT 21
#endif

/* RssAnon of a process, KiB (-1: gone) */
static long rss_anon(int pid)
{
    char p[64], l[128];
    snprintf(p, sizeof p, "/proc/%d/status", pid);
    FILE *f = fopen(p, "r");
    long v = -1;
    while (f && fgets(l, sizeof l, f))
        if (sscanf(l, "RssAnon: %ld", &v) == 1) break;
    if (f) fclose(f);
    return v;
}

static int pidfd_of(int pid) { return (int)syscall(SYS_pidfd_open, pid, 0); }

/* pid is still the process pidfd was taken for, and still in the frozen group.
 * The pidfd check comes last: while that process lives its pid can't be
 * reused, so what was read by pid before it (cgroup, maps) was its own. */
static int still_frozen(int pid, int pidfd)
{
    char p[64], l[320];
    int in = 0;
    snprintf(p, sizeof p, "/proc/%d/cgroup", pid);
    FILE *f = fopen(p, "r");
    while (f && fgets(l, sizeof l, f))
        if (!strncmp(l, "0::", 3)) { l[strcspn(l, "\n")] = 0; in = !strcmp(l + 3, FROZEN_GROUP); break; }
    if (f) fclose(f);
    return in && !syscall(SYS_pidfd_send_signal, pidfd, 0, NULL, 0);
}

/* MADV_PAGEOUT over pid's anonymous mappings (no file behind them: heap,
 * stacks, anonymous mmaps; not [vdso]/[vvar]), checked to be still frozen
 * before each batch. 0 = advised, -1 = refused, 1 = not frozen (any more) */
static int page_out(int pid, int fd)
{
    char p[64], l[512];
    if (fd < 0) return -1;            /* no pidfd: no process_madvise either */
    snprintf(p, sizeof p, "/proc/%d/maps", pid);
    FILE *f = fopen(p, "r");
    if (!f) return 1;
    if (!still_frozen(pid, fd)) { fclose(f); return 1; }
    int r = 0;
    struct iovec iov[256];
    int n = 0;
    while (fgets(l, sizeof l, f)) {
        unsigned long a, b;
        char perm[8] = "", path[256] = "";
        if (sscanf(l, "%lx-%lx %7s %*s %*s %*s %255s", &a, &b, perm, path) < 3) continue;
        if (perm[0] != 'r' || (path[0] && strcmp(path, "[heap]") && strncmp(path, "[stack", 6) && strncmp(path, "[anon:", 6)))
            continue;
        iov[n++] = (struct iovec){ (void *)a, b - a };
        if (n == 256) {
            if (syscall(SYS_process_madvise, fd, iov, n, MADV_PAGEOUT, 0) < 0 && errno != ENOMEM) r = -1;
            n = 0;
            if (!still_frozen(pid, fd)) { fclose(f); return 1; }   /* thawed meanwhile: stop */
        }
    }
    if (n && syscall(SYS_process_madvise, fd, iov, n, MADV_PAGEOUT, 0) < 0 && errno != ENOMEM) r = -1;
    fclose(f);
    return r;
}

static pthread_mutex_t rc_mu = PTHREAD_MUTEX_INITIALIZER;
static int rc_pids[MAXFROZE], rc_fds[MAXFROZE], rc_n, rc_running;

/* page out what the passes froze (more may come while it runs); a process
 * thawed, gone or replaced by another with its pid by now is left alone */
static void *reclaim_thread(void *arg)
{
    (void)arg;
    for (;;) {
        int pids[MAXFROZE], fds[MAXFROZE], n;
        pthread_mutex_lock(&rc_mu);
        n = rc_n;
        memcpy(pids, rc_pids, sizeof(int) * n);
        memcpy(fds, rc_fds, sizeof(int) * n);
        rc_n = 0;
        if (!n) { rc_running = 0; pthread_mutex_unlock(&rc_mu); return NULL; }
        pthread_mutex_unlock(&rc_mu);
        long before = 0, after = 0;
        int refused = 0, tried = 0, skipped = 0;
        double t0 = now();
        for (int i = 0; i < n; i++) {
            long b = rss_anon(pids[i]), a;
            int r;
            if (b < 0 || (fds[i] >= 0 && !still_frozen(pids[i], fds[i]))) { skipped++; goto next; }
            r = page_out(pids[i], fds[i]);
            if (r > 0) { skipped++; goto next; }
            tried++;
            if (r < 0) refused++;
            before += b;
            a = rss_anon(pids[i]);
            after += a < 0 ? 0 : a;
        next:
            if (fds[i] >= 0) close(fds[i]);
        }
        logf_("paged out %d frozen process(es): anon %ld -> %ld MiB in %.1f s%s%s", tried, before >> 10, after >> 10,
              now() - t0, refused ? " (some refused: no process_madvise?)" : "",
              skipped ? " (others thawed or gone first: left alone)" : "");
        if (tried && refused == tried) can_reclaim = 0;  /* the kernel can't: don't try again */
    }
}

static void reclaim_parked(void)
{
    int on = can_reclaim && reclaim_mode != RECLAIM_OFF && (reclaim_mode != RECLAIM_SWAP || swap_on());
    pthread_mutex_lock(&rc_mu);
    for (int i = 0; i < nfroze; i++) {
        if (on && rc_n < MAXFROZE) { rc_pids[rc_n] = froze[i]; rc_fds[rc_n++] = froze_fd[i]; }
        else if (froze_fd[i] >= 0) close(froze_fd[i]);
    }
    if (on && rc_n && !rc_running) {
        pthread_t th;
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        if (!pthread_create(&th, &a, reclaim_thread, NULL)) rc_running = 1;
        pthread_attr_destroy(&a);
    }
    pthread_mutex_unlock(&rc_mu);
}

static const char *reclaim_name(void)
{
    if (!can_reclaim) return "unavailable";
    return reclaim_mode == RECLAIM_OFF ? "off" : reclaim_mode == RECLAIM_ALWAYS ? "always" :
           swap_on() ? "swap (on)" : "swap (no swap now)";
}

/* after a pass: what it froze leaves RAM; the writeback is scheduled */
static void parked(void)
{
    if (!nfroze) return;
    reclaim_parked();
    nfroze = 0;
    last_freeze = now();
    if (writeback_s > 0 && !wb_at) wb_at = last_freeze + writeback_s;
}

/* on the RESCAN_S pass: the writeback, when due (one at a time) */
static void writeback_due(void)
{
    if (wb_pid > 0) {
        int st;
        pid_t r = waitpid(wb_pid, &st, WNOHANG);
        if (r == wb_pid && !(WIFEXITED(st) && !WEXITSTATUS(st)))
            logf_("zram writeback failed (%s %d; an idle age needs CONFIG_ZRAM_TRACK_ENTRY_ACTIME)",
                  WIFEXITED(st) ? "exit" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
        if (r != 0) wb_pid = 0;
    }
    if (!wb_at || now() < wb_at || wb_pid > 0) return;
    double was = wb_at;
    /* freezes after this one was scheduled: their pages are not idle long enough yet */
    wb_at = last_freeze > was - writeback_s ? last_freeze + writeback_s : 0;
    if (writeback_s <= 0 || access(ZRAM_CMD, X_OK)) return;     /* (no service: nothing to write to) */
    pid_t c = fork();
    if (c == 0) {
        execl(ZRAM_CMD, "glassine-zram", "writeback", WRITEBACK_IDLE, (char *)NULL);
        _exit(127);
    }
    if (c > 0) { wb_pid = c; logf_("zram writeback of pages idle >= " WRITEBACK_IDLE " s started"); }
}

/* on the RESCAN_S pass: one app frozen park_after seconds ago -> glassine-zram park */
static void park_due(void)
{
    if (pk_pid > 0) {
        int st;
        if (waitpid(pk_pid, &st, WNOHANG) == 0) return;          /* one at a time */
        if (pk_owner >= 0 && !(WIFEXITED(st) && !WEXITSTATUS(st)))
            logf_("park of %s (%d) failed (%s %d)", comm[pk_owner], pk_owner,
                  WIFEXITED(st) ? "exit" : "signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st));
        pk_pid = 0;
    }
    if (park_s <= 0 || !can_freeze || access(ZRAM_CMD, X_OK)) return;
    double t = now();
    int owner = -1;
    for (int pid = 2; pid < pid_max && owner < 0; pid++)
        if (inbg[pid] == 2 && frozat[pid] && !parkst[pid] && t - frozat[pid] >= park_s) owner = frzown[pid];
    if (owner < 0) return;
    char *argv[3 + 256], num[256][12];
    int n = 0;
    argv[n++] = "glassine-zram";
    argv[n++] = "park";
    for (int pid = 2; pid < pid_max; pid++) {
        if (inbg[pid] != 2 || !frozat[pid] || parkst[pid] || frzown[pid] != owner) continue;
        parkst[pid] = 1;                    /* (more than 256: the next pass takes the rest) */
        if (n - 2 >= 256) { parkst[pid] = 0; continue; }
        snprintf(num[n - 2], sizeof num[0], "%d", pid);
        argv[n] = num[n - 2];
        n++;
    }
    argv[n] = NULL;
    pid_t c = fork();
    if (c == 0) {
        execv(ZRAM_CMD, argv);
        _exit(127);
    }
    if (c < 0) return;
    pk_pid = c;
    pk_owner = owner;
    logf_("parking %s (%d): %d process(es) -> glassine-zram park", comm[owner], owner, n - 2);
}

static void read_group(const char *group, char mark)
{
    char p[320], line[32];
    snprintf(p, sizeof p, CG_ROOT "%s/cgroup.procs", group);
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int pid = atoi(line);
        if (pid > 0 && pid < pid_max) {
            inbg[pid] = mark;
            if (bgsince[pid] == 0) bgsince[pid] = now();
        }
    }
    fclose(f);
}

static void read_fg(void)
{
    char line[32];
    memset(infg, 0, pid_max);
    FILE *f = fopen(CG_ROOT FG_GROUP "/cgroup.procs", "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int pid = atoi(line);
        if (pid > 0 && pid < pid_max) infg[pid] = 1;
    }
    fclose(f);
}

static void read_bg(void)
{
    memset(inbg, 0, pid_max);
    read_group(BG_GROUP, 1);
    if (can_freeze) read_group(FROZEN_GROUP, 2);
    read_fg();
}

/* everything back in front (at start: what a previous instance left; at exit) */
static void release_all(void)
{
    read_bg();
    for (int pid = 1; pid < pid_max; pid++) {
        if (inbg[pid]) promote(pid, 0);
        if (infg[pid] && !move_to(pid, "")) infg[pid] = 0;
    }
}

/* ---- the desktop's own processes ---------------------------------------- */

/* never in glassine-fg: the desktop itself (never clamp or boost the
 * compositor; the panel daemon places its own threads) */
static const char *const no_boost[] = {
    "sway", "sway-glassine", "Xwayland", "glassine-epdd", "glassine-epdd-desktop", "glassine-shell",
    "glassine-session", "glassine-session-sway", "glassine-bgprio", "glassine-swayd", "dbus-daemon", NULL
};

/* never frozen, whatever the conf says: the desktop blocks on these (a frozen
 * audio client can stall the PipeWire graph) */
static const char *const never_freeze[] = {
    "sway", "sway-glassine", "Xwayland", "glassine-epdd", "glassine-epdd-desktop", "glassine-shell",
    "glassine-session", "glassine-session-sway", "glassine-bgprio", "glassine-swayd",
    "pipewire", "pipewire-pulse", "wireplumber", "dbus-daemon", NULL
};

static int in_names(int pid, const char *const *names)
{
    for (int i = 0; names[i]; i++)
        if (same_comm(comm[pid], names[i])) return 1;
    return 0;
}

static int never_boost(int pid) { return in_names(pid, no_boost); }
static int never(int pid) { return in_names(pid, never_freeze); }

/* ---- foreground boost (see the header) ---------------------------------- */

/* fg-boost -> fg_boost (0 = off); a change rewrites the group's floor */
static void read_fg_boost(void)
{
    int v = 0;
    FILE *f = fopen(FG_BOOST_CONF, "r");
    if (f) {
        if (fscanf(f, "%d", &v) != 1) v = 0;
        fclose(f);
    }
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    /* the power saver mode: no floors at all */
    char lvl[32] = "";
    if ((f = fopen(POWER_MODE, "r"))) {
        if (!fgets(lvl, sizeof lvl, f)) lvl[0] = 0;
        fclose(f);
    }
    if (!strncmp(lvl, "saver", 5)) v = 0;
    if (v == fg_boost) return;
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    if (v && write_str(CG_ROOT FG_GROUP "/cpu.uclamp.min", b)) {
        logf_("foreground boost %d %%: can't write " CG_ROOT FG_GROUP "/cpu.uclamp.min (%s), off", v, strerror(errno));
        v = 0;
    }
    if (fg_boost >= 0 || v) logf_("foreground boost %s%d%s", v ? "uclamp.min " : "", v, v ? " %" : " (off)");
    fg_boost = v;
}

/* ---- processes ---------------------------------------------------------- */

static void read_procs(void)
{
    DIR *d = opendir("/proc");
    struct dirent *e;
    for (int i = 0; i < pid_max; i++) ppid[i] = -1;
    memset(uidok, 0, pid_max);
    if (!d) return;
    while ((e = readdir(d))) {
        int pid = atoi(e->d_name);
        char p[64], buf[512];
        if (pid <= 0 || pid >= pid_max) continue;
        snprintf(p, sizeof p, "/proc/%d/stat", pid);
        int fd = open(p, O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof buf - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        char *l = strchr(buf, '('), *r = strrchr(buf, ')');
        if (!l || !r) continue;
        int len = r - l - 1;
        if (len > COMM_LEN) len = COMM_LEN;
        memcpy(comm[pid], l + 1, len);
        comm[pid][len] = 0;
        /* after ") ": state ppid ... */
        char *f = r + 2;
        int field = 3, pp = -1;
        for (char *save, *tok = strtok_r(f, " ", &save); tok; tok = strtok_r(NULL, " ", &save), field++)
            if (field == 4) { pp = atoi(tok); break; }
        ppid[pid] = pp;
    }
    closedir(d);
}

/* every uid of pid (real, effective, saved, file) is the session user's, who
 * isn't root: only those processes are ours to demote, freeze or boost */
static int session_proc(int pid)
{
    if (pid <= 0 || pid >= pid_max) return 0;
    if (uidok[pid]) return uidok[pid] > 0;
    char p[64], l[256];
    unsigned u[4];
    int ok = 0;
    snprintf(p, sizeof p, "/proc/%d/status", pid);
    FILE *f = fopen(p, "r");
    while (f && fgets(l, sizeof l, f))
        if (sscanf(l, "Uid: %u %u %u %u", &u[0], &u[1], &u[2], &u[3]) == 4) {
            ok = session_uid != 0 && session_uid != (uid_t)-1;
            for (int i = 0; i < 4; i++) if (u[i] != (unsigned)session_uid) ok = 0;
            break;
        }
    if (f) fclose(f);
    uidok[pid] = ok ? 1 : -1;
    return ok;
}

static int excluded(int pid)
{
    for (int i = 0; i < nexcl; i++)
        if (!strcmp(comm[pid], excl[i])) return 1;
    return 0;
}

/* ---- "runs in the background on purpose": MPRIS, PipeWire, Flatpak ------- */

#define EXEMPT_SH \
    "B='dbus-send --session --print-reply --reply-timeout=1000 --dest=org.freedesktop.DBus /org/freedesktop/DBus';" \
    "for n in $($B org.freedesktop.DBus.ListNames 2>/dev/null | sed -n 's/.*string \"\\(org\\.mpris\\.MediaPlayer2\\.[^\"]*\\)\".*/\\1/p'); do" \
    "  p=$($B org.freedesktop.DBus.GetConnectionUnixProcessID string:$n 2>/dev/null | sed -n 's/.*uint32 //p');" \
    "  [ -n \"$p\" ] && echo \"mpris $p $n\";" \
    "done;" \
    "{ timeout 3 pactl list clients | sed 's/^/C /'; timeout 3 pactl list sink-inputs | sed 's/^/S /'; } 2>/dev/null |" \
    "awk '$1==\"C\" && $2==\"Client\" {c=substr($3,2)}" \
    "  $1==\"C\" && $3==\"=\" {v=$4; gsub(/\"/,\"\",v); k[c,$2]=v}" \
    "  $1==\"S\" && $2==\"Client:\" {cl=$3}" \
    "  $1==\"S\" && $2==\"Corked:\" && $3==\"no\" {play[cl]=1}" \
    "  END {for (c in play) {" \
    "    if (k[c,\"pipewire.access.portal.app_id\"] != \"\") print \"pipewire 0\", k[c,\"pipewire.access.portal.app_id\"];" \
    "    else if (k[c,\"client.api\"] != \"pipewire-pulse\" && k[c,\"pipewire.sec.pid\"] != \"\") print \"pipewire\", k[c,\"pipewire.sec.pid\"];" \
    "    else if (k[c,\"application.process.id\"] != \"\") print \"pipewire\", k[c,\"application.process.id\"] }}';" \
    "command -v flatpak >/dev/null && timeout 3 flatpak permissions background background 2>/dev/null |" \
    "  awk '$1==\"background\" && $2==\"background\" && $4 ~ /yes/ {print \"flatpak 0\", $3}'"

static struct { char why[12]; int pid; char name[128]; } ex[128];
static int nex, ex_valid;             /* ex_valid: queried in this pass */

static void env_of(int pid, const char *key, char *out, size_t n)
{
    char p[64], buf[16384];
    out[0] = 0;
    snprintf(p, sizeof p, "/proc/%d/environ", pid);
    int fd = open(p, O_RDONLY);
    if (fd < 0) return;
    ssize_t len = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (len <= 0) return;
    buf[len] = 0;
    size_t kl = strlen(key);
    for (char *e = buf; e < buf + len; e += strlen(e) + 1)
        if (!strncmp(e, key, kl) && e[kl] == '=') { snprintf(out, n, "%s", e + kl + 1); return; }
}

/* the owner's session bus + runtime dir. Its own environ first, then its parents',
 * then any process of the same user: some browsers overwrite their environ with
 * their process title, and inside a Flatpak the bus is the sandbox's proxy
 * (/run/flatpak/bus), which the host can't use. */
static int host_bus(int pid, char *bus, size_t nb, char *run, size_t nr)
{
    env_of(pid, "DBUS_SESSION_BUS_ADDRESS", bus, nb);
    if (!*bus || strstr(bus, "/run/flatpak/")) { *bus = 0; return 0; }
    env_of(pid, "XDG_RUNTIME_DIR", run, nr);
    return 1;
}

static void session_env(int owner, uid_t uid, char *bus, size_t nb, char *run, size_t nr)
{
    int pid = owner;
    *run = 0;
    for (int d = 0; pid > 1 && pid < pid_max && d < 64; d++, pid = ppid[pid])
        if (host_bus(pid, bus, nb, run, nr)) goto out;
    for (pid = 2; pid < pid_max; pid++) {
        char p[32];
        struct stat st;
        if (ppid[pid] < 0 || ppid[pid] == 2) continue;
        snprintf(p, sizeof p, "/proc/%d", pid);
        if (!stat(p, &st) && st.st_uid == uid && host_bus(pid, bus, nb, run, nr)) goto out;
    }
out:
    if (!*run) snprintf(run, nr, "/run/user/%u", (unsigned)uid);
}

static void query_exempt(int owner)
{
    char bus[256], run[256], p[64];
    struct stat st;
    int fd[2];
    nex = 0;
    ex_valid = 1;
    snprintf(p, sizeof p, "/proc/%d", owner);
    if (stat(p, &st)) return;
    session_env(owner, st.st_uid, bus, sizeof bus, run, sizeof run);
    if (pipe(fd)) return;
    pid_t c = fork();
    if (c < 0) { close(fd[0]); close(fd[1]); return; }
    if (c == 0) {
        setpgid(0, 0);                /* one group: dbus-send/pactl die with it */
        dup2(fd[1], 1);
        close(fd[0]); close(fd[1]);
        int nul = open("/dev/null", O_RDWR);
        if (nul >= 0) { dup2(nul, 0); dup2(nul, 2); }
        clearenv();
        setenv("PATH", "/usr/local/bin:/usr/bin:/bin", 1);
        if (*bus) setenv("DBUS_SESSION_BUS_ADDRESS", bus, 1);
        if (*run) setenv("XDG_RUNTIME_DIR", run, 1);
        if (setgroups(0, NULL) || setgid(st.st_gid) || setuid(st.st_uid)) _exit(1);
        execl("/usr/bin/timeout", "timeout", "5", "/bin/sh", "-c", EXEMPT_SH, (char *)NULL);
        _exit(1);
    }
    close(fd[1]);
    setpgid(c, c);
    /* read with a deadline: a stuck grandchild keeps the pipe open */
    char buf[8192];
    size_t len = 0;
    double end = now() + 6.0;
    for (;;) {
        int ms = (int)((end - now()) * 1000);
        struct pollfd pf = { fd[0], POLLIN, 0 };
        if (ms <= 0 || poll(&pf, 1, ms) <= 0) break;
        ssize_t n = read(fd[0], buf + len, sizeof buf - 1 - len);
        if (n <= 0) break;
        len += n;
        if (len >= sizeof buf - 1) break;
    }
    close(fd[0]);
    kill(-c, SIGKILL);
    waitpid(c, NULL, 0);
    buf[len] = 0;
    for (char *save, *l = strtok_r(buf, "\n", &save); l && nex < 128; l = strtok_r(NULL, "\n", &save)) {
        char why[12] = "", name[128] = "";
        int pid = 0;
        if (sscanf(l, "%11s %d %127s", why, &pid, name) >= 2) {
            snprintf(ex[nex].why, sizeof ex[nex].why, "%s", why);
            ex[nex].pid = pid;
            snprintf(ex[nex].name, sizeof ex[nex].name, "%s", name);
            nex++;
        }
    }
}

static int in_tree(int pid, int owner)  /* owner is pid or one of its ancestors */
{
    for (int d = 0; pid > 1 && pid < pid_max && d < 64; d++, pid = ppid[pid])
        if (pid == owner) return 1;
    return 0;
}

static void flatpak_id(int pid, char *out, size_t n)
{
    char p[64], line[256];
    out[0] = 0;
    snprintf(p, sizeof p, "/proc/%d/root/.flatpak-info", pid);
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "name=", 5)) { line[strcspn(line, "\n")] = 0; snprintf(out, n, "%.127s", line + 5); break; }
    fclose(f);
}

/* why the view owner's tree must keep running, or NULL */
static const char *exempt(int owner)
{
    static char why[160];
    char app[128];
    if (!ex_valid) query_exempt(owner);
    flatpak_id(owner, app, sizeof app);
    for (int i = 0; i < nex; i++) {
        char other[128];
        int hit = 0;
        if (ex[i].pid > 0) {          /* a host pid: in the owner's tree, or the same Flatpak app */
            hit = in_tree(ex[i].pid, owner);
            if (!hit && *app) { flatpak_id(ex[i].pid, other, sizeof other); hit = !strcmp(other, app); }
        } else {                      /* a Flatpak app id */
            hit = *app && !strcmp(ex[i].name, app);
        }
        if (hit) {
            snprintf(why, sizeof why, "%s%s%s", !strcmp(ex[i].why, "flatpak") ? "flatpak background permission" : ex[i].why,
                     *ex[i].name ? " " : "", ex[i].name);
            return why;
        }
    }
    return NULL;
}

/* pid's name or Flatpak app id in a nofreeze/freeze list */
static int listed(int pid, char (*l)[128], int n)
{
    char app[128];
    int dotted = 0;
    for (int i = 0; i < n; i++) {
        if (!strcmp(comm[pid], l[i])) return 1;
        if (strchr(l[i], '.')) dotted = 1;
    }
    if (!dotted) return 0;
    flatpak_id(pid, app, sizeof app);
    for (int i = 0; *app && i < n; i++)
        if (!strcmp(app, l[i])) return 1;
    return 0;
}

static int nofreeze(int pid) { return never(pid) || listed(pid, nofrz, nnofrz); }
static int always_freeze(int owner) { return nalwfrz && listed(owner, alwfrz, nalwfrz); }

static void read_conf(void)
{
    read_fg_boost();
    FILE *f = fopen(CONF, "r");
    char line[256];
    nexcl = nnofrz = nalwfrz = 0;
    freeze_s = FREEZE_S;
    reclaim_mode = RECLAIM_SWAP;
    writeback_s = WRITEBACK_AFTER_S;
    park_s = PARK_AFTER_S;
    bg_clamp = BG_UCLAMP_MAX;
    while (f && fgets(line, sizeof line, f)) {
        char *h = strchr(line, '#'), w[256], *save, *t;
        if (h) *h = 0;
        if (!(t = strtok_r(line, " \t\n", &save))) continue;
        if (!strcmp(t, "freeze_after")) {
            if ((t = strtok_r(NULL, " \t\n", &save))) freeze_s = atoi(t);
            continue;
        }
        if (!strcmp(t, "reclaim")) {
            if ((t = strtok_r(NULL, " \t\n", &save)))
                reclaim_mode = !strcmp(t, "off") ? RECLAIM_OFF : !strcmp(t, "always") ? RECLAIM_ALWAYS : RECLAIM_SWAP;
            continue;
        }
        if (!strcmp(t, "writeback_after")) {
            if ((t = strtok_r(NULL, " \t\n", &save))) writeback_s = atoi(t);
            continue;
        }
        if (!strcmp(t, "park_after")) {
            if ((t = strtok_r(NULL, " \t\n", &save))) park_s = atoi(t);
            continue;
        }
        if (!strcmp(t, "bg_uclamp_max")) {
            if ((t = strtok_r(NULL, " \t\n", &save))) {
                int v = atoi(t);
                bg_clamp = v < 1 ? 1 : v > 100 ? 100 : v;
            }
            continue;
        }
        int nf = !strcmp(t, "nofreeze"), af = !strcmp(t, "freeze");
        if (nf || af) t = strtok_r(NULL, " \t\n", &save);
        for (; t; t = strtok_r(NULL, " \t\n", &save)) {
            snprintf(w, sizeof w, "%s", t);
            /* a process name: the kernel's comm keeps COMM_LEN characters */
            if (!strchr(w, '.')) w[COMM_LEN] = 0;
            if (nf && nnofrz < 64) snprintf(nofrz[nnofrz++], 128, "%.127s", w);
            else if (af && nalwfrz < 64) snprintf(alwfrz[nalwfrz++], 128, "%.127s", w);
            else if (!nf && !af && nexcl < 64) snprintf(excl[nexcl++], 16, "%.15s", w);
        }
    }
    if (f) fclose(f);
    apply_bg_clamp();
}

/* ---- the windows, from sway ---------------------------------------------- */

static struct { char name[64]; int visible; } sws[64];
static int nsws;

/* an app out of the freezer now (it stays in the background; the freeze timer restarts) */
static void thaw_owner(long owner)
{
    if (owner <= 1 || owner >= pid_max) return;
    read_procs();
    read_bg();
    int n = 0;
    char app[128], id[128];
    flatpak_id((int)owner, app, sizeof app);
    for (int pid = 2; pid < pid_max; pid++)
        if (inbg[pid] == 2 &&
            (in_tree(pid, owner) || (app[0] && (flatpak_id(pid, id, sizeof id), !strcmp(id, app)))) &&
            !move_to(pid, BG_GROUP)) {
            unfrozen(pid);
            inbg[pid] = 1;
            bgsince[pid] = now();
            n++;
        }
    if (n) logf_("thawed %s (%ld) on request, %d process(es)", comm[owner], owner, n);
}

/* win[] from the tree: views on visible workspaces are in front */
static void sway_walk(json_object *n, int front, long want_id, long *found_pid)
{
    const char *type = sj_str(n, "type");
    if (type && !strcmp(type, "workspace")) {
        const char *nm = sj_str(n, "name");
        front = 0;
        for (int i = 0; nm && i < nsws; i++) if (!strcmp(sws[i].name, nm)) front = sws[i].visible;
        if (nm && !strcmp(nm, "__i3_scratch")) front = 0;
    }
    if (sj_str(n, "shell")) {           /* a view */
        long pid = sj_int(n, "pid", -1);
        if (want_id && sj_int(n, "id", 0) == want_id) *found_pid = pid;
        if (sj_bool(n, "sticky")) front = 1;
        if (pid > 0 && pid < pid_max && !want_id) {
            if (front) win[pid] = 1;
            else if (!win[pid]) win[pid] = 2;
        }
        return;
    }
    json_object *a;
    if (json_object_object_get_ex(n, "nodes", &a))
        for (size_t i = 0; i < json_object_array_length(a); i++) sway_walk(json_object_array_get_idx(a, i), front, want_id, found_pid);
    if (json_object_object_get_ex(n, "floating_nodes", &a))
        for (size_t i = 0; i < json_object_array_length(a); i++) sway_walk(json_object_array_get_idx(a, i), front, want_id, found_pid);
}

static void sway_gone(void)
{
    release_all();
    if (thaw_fd >= 0) unlink(THAW_SOCK);
    logf_("sway gone, exiting");
    exit(0);
}

/* want_id 0: win[] for every view; else the pid of the view with that id */
static long sway_scan(long want_id)
{
    json_object *w = sipc_call(sw_cmd, SIPC_GET_WORKSPACES, NULL);
    if (!w) sway_gone();
    nsws = 0;
    for (size_t i = 0; i < json_object_array_length(w) && nsws < 64; i++) {
        json_object *o = json_object_array_get_idx(w, i);
        snprintf(sws[nsws].name, sizeof sws[0].name, "%s", sj_str(o, "name") ? sj_str(o, "name") : "");
        sws[nsws++].visible = sj_bool(o, "visible");
    }
    json_object_put(w);
    json_object *t = sipc_call(sw_cmd, SIPC_GET_TREE, NULL);
    if (!t) sway_gone();
    long pid = -1;
    sway_walk(t, 0, want_id, &pid);
    json_object_put(t);
    return pid;
}

/* the desktop user's sway: $SWAYSOCK, else the newest /run/user/(*)/sway-ipc.*.sock */
static int sway_connect(void)
{
    for (int tries = 0; tries < 60 && !quit; tries++) {
        if (!getenv("SWAYSOCK") || tries) {
            /* newest by mtime, not the last name: dead sways leave their
             * sockets, and the pids in the names sort as text */
            glob_t g;
            if (!glob("/run/user/*/sway-ipc.*.sock", 0, NULL, &g) && g.gl_pathc) {
                size_t best = 0;
                time_t bt = 0;
                for (size_t i = 0; i < g.gl_pathc; i++) {
                    struct stat st;
                    if (!stat(g.gl_pathv[i], &st) && st.st_mtime >= bt) { best = i; bt = st.st_mtime; }
                }
                setenv("SWAYSOCK", g.gl_pathv[best], 1);
                globfree(&g);
            }
        }
        if (getenv("SWAYSOCK") && (sw_cmd = sipc_open()) >= 0 && (sw_ev = sipc_open()) >= 0) {
            uint32_t t;
            char *ok;
            if (sipc_send(sw_ev, SIPC_SUBSCRIBE, "[\"window\",\"workspace\",\"output\"]") && (ok = sipc_recv(sw_ev, &t))) {
                free(ok);
                return 0;
            }
        }
        if (sw_cmd >= 0) close(sw_cmd);
        if (sw_ev >= 0) close(sw_ev);
        sw_cmd = sw_ev = -1;
        sleep(1);
    }
    return -1;
}

/* every frozen app back to plain background, timers restarted */
static void thaw_all(const char *why)
{
    read_procs();
    read_bg();
    int n = 0;
    for (int pid = 2; pid < pid_max; pid++)
        if (inbg[pid] == 2 && !move_to(pid, BG_GROUP)) { unfrozen(pid); inbg[pid] = 1; bgsince[pid] = now(); n++; }
    if (n) logf_("thawed %d process(es): %s", n, why);
}

static void thaw_sock_open(void)
{
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", THAW_SOCK);
    unlink(a.sun_path);
    thaw_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (thaw_fd >= 0 && bind(thaw_fd, (struct sockaddr *)&a, sizeof a)) { close(thaw_fd); thaw_fd = -1; }
    /* only sway's user (the shell, which asks) and root may send: every datagram
     * costs a /proc scan and a get_tree */
    if (thaw_fd >= 0 && ((session_uid != (uid_t)-1 && chown(a.sun_path, session_uid, (gid_t)-1)) || chmod(a.sun_path, 0600)))
        logf_("thaw socket " THAW_SOCK ": can't give it to uid %d: %s", (int)session_uid, strerror(errno));
    if (thaw_fd < 0) logf_("no thaw socket " THAW_SOCK ": %s", strerror(errno));
}

/* one pass; demote_ok = 0: only bring processes to the front */
static int pass(int demote_ok)
{
    int pending = 0;

    read_procs();
    read_bg();
    ex_valid = 0;                     /* MPRIS/PipeWire/Flatpak: asked again when needed */
    memset(win, 0, pid_max);
    sway_scan(0);

    /* Flatpak view owners by app id: a sandbox's other processes may hang
     * elsewhere (a browser's renderers, spawned through the Flatpak portal) */
    static int fo_pid[64];
    static char fo_id[64][128];
    int nfo = 0;
    for (int pid = 2; pid < pid_max && nfo < 64; pid++)
        if (win[pid] && ppid[pid] >= 0) {
            flatpak_id(pid, fo_id[nfo], sizeof fo_id[0]);
            if (fo_id[nfo][0]) fo_pid[nfo++] = pid;
        }

    for (int pid = 2; pid < pid_max; pid++) {
        if (ppid[pid] < 0 || ppid[pid] == 2) continue;          /* no process / kernel thread */
        int q = pid, state = 0, owner = 0;
        for (int depth = 0; q > 1 && q < pid_max && depth < 64; depth++, q = ppid[q]) {
            if (ppid[q] < 0) break;
            if (win[q]) { state = win[q]; owner = q; break; }
        }
        if (!state && nfo) {
            char id[128];
            flatpak_id(pid, id, sizeof id);
            /* the app's front view wins over a hidden one */
            for (int i = 0; id[0] && i < nfo; i++)
                if (!strcmp(id, fo_id[i]) && (!state || win[fo_pid[i]] == 1)) { state = win[fo_pid[i]]; owner = fo_pid[i]; }
        }
        /* not the session user's (root, another user): left alone, back home if ours */
        if (state && (!session_proc(owner) || !session_proc(pid))) state = 0;
        int bg = state == 2 && !excluded(owner) && !excluded(pid);
        if (bg && !inbg[pid]) {
            if (!demote_ok) pending = 1;
            else {
                demote(pid, pid == owner);
                if (can_freeze && always_freeze(owner) && !never(owner) && !never(pid)) freeze(pid, owner, pid == owner);
            }
        } else if (bg && inbg[pid] == 1 && demote_ok && can_freeze && always_freeze(owner) && !never(owner) && !never(pid)) {
            freeze(pid, owner, pid == owner);          /* "freeze" in the conf: at once, no exemptions */
        } else if (bg && inbg[pid] == 1 && demote_ok && can_freeze && freeze_s > 0 &&
                   /* the view owner's hidden time: its whole tree freezes together */
                   now() - (bgsince[owner] ? bgsince[owner] : bgsince[pid]) >= freeze_s &&
                   !nofreeze(owner) && !nofreeze(pid)) {
            const char *why = exempt(owner);
            if (why) {
                if (!exempt_logged[owner]) logf_("not freezing %s (%d): %s", comm[owner], owner, why);
                exempt_logged[owner] = 1;
            } else {
                freeze(pid, owner, pid == owner);
            }
        } else if (!bg && inbg[pid]) {
            promote(pid, pid == owner);
        }
        /* foreground boost: in front (not merely kept there by the conf) */
        int boost = fg_boost > 0 && state == 1 && !inbg[pid] && !never_boost(pid) && !never_boost(owner);
        if (boost && !infg[pid]) {
            if (!move_to(pid, FG_GROUP)) {
                infg[pid] = 1;
                if (pid == owner) logf_("boosted %s (%d)", comm[pid], pid);
            }
        } else if (!boost && infg[pid] && !inbg[pid]) {
            if (!move_to(pid, "")) infg[pid] = 0;
        }
    }
    parked();
    return pending;
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void on_signal(int s) { (void)s; quit = 1; }

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-sway")) {           /* -sway: what older callers pass; the only mode */
            fprintf(stderr, "usage: glassine-bgprio (runs as root in the sway session; see the source header)\n");
            return 2;
        }
    FILE *pm = fopen("/proc/sys/kernel/pid_max", "r");
    if (!pm || fscanf(pm, "%d", &pid_max) != 1) pid_max = 32768;
    if (pm) fclose(pm);
    pid_max++;
    ppid = calloc(pid_max, sizeof *ppid);
    comm = calloc(pid_max, sizeof *comm);
    win = calloc(pid_max, 1);
    inbg = calloc(pid_max, 1);
    infg = calloc(pid_max, 1);
    bgsince = calloc(pid_max, sizeof *bgsince);
    exempt_logged = calloc(pid_max, 1);
    uidok = calloc(pid_max, 1);
    frozat = calloc(pid_max, sizeof *frozat);
    frzown = calloc(pid_max, sizeof *frzown);
    parkst = calloc(pid_max, 1);
    oomsave = malloc(pid_max * sizeof *oomsave);
    if (!ppid || !comm || !win || !inbg || !infg || !bgsince || !exempt_logged || !uidok || !frozat || !frzown || !parkst || !oomsave)
        return 1;
    for (int i = 0; i < pid_max; i++) oomsave[i] = OOM_NONE;

    if (setup_cgroup()) { logf_("no cgroup v2 cpu controller at " CG_ROOT ": %s", strerror(errno)); return 1; }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGPIPE, SIG_IGN);
    if (sway_connect()) { logf_("no sway IPC socket"); return 1; }
    struct ucred cr;
    socklen_t crl = sizeof cr;
    if (!getsockopt(sw_cmd, SOL_SOCKET, SO_PEERCRED, &cr, &crl)) session_uid = cr.uid;
    if (session_uid == 0 || session_uid == (uid_t)-1)
        logf_("sway runs as %s: no process is ever demoted, frozen or boosted", session_uid ? "an unknown user" : "root");
    thaw_sock_open();
    read_conf();
    release_all();
    logf_("started (sway %s), cgroup " CG_ROOT BG_GROUP " (uclamp.max %d %%), front = %s, %d excluded name(s), "
          "freeze after %s%d s (%d nofreeze), reclaim %s, writeback after %d s, park after %d s",
          getenv("SWAYSOCK"), bg_clamp, *home ? home : "/", nexcl, can_freeze ? "" : "never: no freezer, ", freeze_s,
          nnofrz, reclaim_name(), writeback_s, park_s);
    double next = now() + DEMOTE_S;       /* first full pass after the delay */
    pass(0);
    while (!quit) {
        int ms = (int)((next - now()) * 1000);
        struct pollfd pf[2] = { { sw_ev, POLLIN, 0 }, { thaw_fd, POLLIN, 0 } };
        if (poll(pf, 2, ms < 0 ? 0 : ms) < 0 && errno != EINTR) break;
        int changed = 0;
        if (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            do {                        /* this event and whatever else is queued */
                uint32_t t;
                char *m = sipc_recv(sw_ev, &t);
                if (!m) sway_gone();
                if (t == SIPC_EV_OUTPUT) thaw_all("an output changed");
                else {
                    json_object *j = sj_parse(m);
                    const char *ch = j ? sj_str(j, "change") : NULL;
                    if (!ch || (strcmp(ch, "title") && strcmp(ch, "mark") && strcmp(ch, "urgent"))) changed = 1;
                    json_object_put(j);
                }
                free(m);
                pf[0].revents = 0;
            } while (poll(pf, 1, 0) > 0 && (pf[0].revents & POLLIN));
        }
        if (pf[1].revents & POLLIN) {
            char m[64];
            ssize_t l;
            long id;
            /* a few per wakeup: sway's events come between (thaw_owner reads /proc itself) */
            for (int k = 0; k < THAW_BURST && (l = recv(thaw_fd, m, sizeof m - 1, 0)) > 0; k++) {
                m[l] = 0;
                if (sscanf(m, "thaw %ld", &id) == 1 && id > 0) thaw_owner(sway_scan(id));
            }
        }
        if (changed) {
            if (pass(0)) next = now() + DEMOTE_S;
        } else if (now() >= next) {
            read_conf();
            pass(1);
            writeback_due();
            park_due();
            next = now() + RESCAN_S;
        }
    }
    release_all();
    if (thaw_fd >= 0) unlink(THAW_SOCK);
    logf_("stopped, everything back in front");
    return 0;
}
