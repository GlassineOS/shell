/* glassine-rotd — screen autorotate from the accelerometer (sway session; spec 003).
 *
 * Reads gravity in the screen plane from an IIO accelerometer and turns the sway
 * output with the device. Lying flat (less than FLAT_G of g in the screen plane)
 * keeps the current turn; otherwise the dominant axis must beat the other by
 * DOMINANCE (~56 deg from the other axis: the 45 deg line plus hysteresis), and the
 * orientation must hold for HOLD_MS before the screen turns. The turn is how far the
 * content turns clockwise (sway's `transform` degrees): the device turned clockwise
 * (its right edge down) needs the content turned counter-clockwise = 270, turned
 * counter-clockwise = 90, upside down = 180.
 *
 * A turn: "rotate" to the e-ink daemon's control FIFO first (it keeps the old picture
 * until sway and the apps have redrawn, then shows the turned screen in one full
 * refresh), then `output OUTPUT transform N` on sway's IPC, then the turn into STATE.
 *
 * Power: the stream runs only in auto mode, at RATE_HZ. A power daemon may disable
 * the IIO buffer for a suspend and enable it again after: the open stream then just
 * has no samples for a while; glassine-rotd never turns a buffer on again while it
 * holds it open. A lost stream (read error: the driver went away) is looked up again.
 *
 * The rotation setting /etc/glassine/rotation (re-read when it changes; written by
 * glassine-settings-root rotation, from the quick settings and the settings app):
 * "auto" (default, also without the file), "lock N" (N = 0/90/180/270: stream off,
 * the screen turned to N) or "lock" (stream off, the screen stays as it is).
 * /run/glassine-rotation: the current turn (0/90/180/270), for the shell and the
 * window model; a restart of glassine-rotd starts from it. A new sway (a new IPC
 * socket: it starts at transform 0) gets the current turn again.
 *
 * The device: /etc/glassine/rotd.conf, KEY=value lines ('#' comments), all optional:
 *   SENSOR=NAME     the accelerometer's IIO name (sysfs `name`); default: the first
 *                   IIO device with an in_accel_x scan element
 *   OUTPUT=NAME     the sway output to turn (default `*`: all)
 *   AXES=R D        the sensor axes, each [+-]x|y|z, that read +g when the screen's
 *                   right edge (R) or bottom edge (D) points down; default "-x y"
 *                   (the Android axis convention: x right, y up)
 *   RATE_HZ=N       sampling rate while streaming (default 4)
 *   HOLD_MS=N       how long an orientation must hold (default 500)
 *
 * usage: glassine-rotd [-n] [-v] [-c CONF] [-m ROTATION] [-s STATE] [-r FILE [-z HZ]]
 *   -n        dry run: logs only, no sway command, no FIFO write, no state file
 *   -v        logs the screen-plane gravity once a second
 *   -c/-m/-s  other paths for rotd.conf, the rotation setting, the state file
 *   -r FILE   replay a capture (12-byte x/y/z s32 records at HZ, default RATE_HZ,
 *             times the sensor scale; SCALE_DEFAULT without a sensor) through the
 *             logic with the configured AXES, print the decisions, exit
 * GLASSINE_ROTD_SYSROOT: a prefix for /sys and /dev (tests).
 * SWAYSOCK, when set, is the sway socket; else the newest /run/user/(*)/sway-ipc.*.sock.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define CTL "/run/glassine-epdd-desktop.ctl"
#define G 9.80665
#define FLAT_G 0.5              /* below this share of g in the screen plane: flat */
#define DOMINANCE 1.5           /* |dominant axis| > 1.5 x |other| */
#define SCALE_DEFAULT 0.000015258

static const char *conf_path = "/etc/glassine/rotd.conf";
static const char *rot_path = "/etc/glassine/rotation";
static const char *state_path = "/run/glassine-rotation";
static char sysroot[128];
static int dry, verbose;

/* the device configuration */
static char sensor[64], output[64] = "*";
static int ax_r = -1, ax_d = 2;         /* axis index + 1, negative: inverted (x=1 y=2 z=3) */
static int rate_hz = 4, hold_ms = 500;

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void stamp(void)
{
    time_t t = time(NULL);
    char b[32];
    strftime(b, sizeof b, "%m-%d %H:%M:%S", localtime(&t));
    fprintf(stderr, "%s glassine-rotd: ", b);
}

static int wr(const char *path, const char *s)
{
    int fd = open(path, O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) return -1;
    int ok = write(fd, s, strlen(s)) == (ssize_t)strlen(s);
    close(fd);
    return ok ? 0 : -1;
}

static int rd(const char *path, char *b, size_t len)
{
    FILE *f = fopen(path, "r");
    b[0] = 0;
    if (!f) return -1;
    if (!fgets(b, len, f)) b[0] = 0;
    fclose(f);
    b[strcspn(b, "\n")] = 0;
    return 0;
}

/* "[+-]x|y|z" -> +-1..3, 0: invalid */
static int axis(const char *s)
{
    int sg = 1;
    if (*s == '-' || *s == '+') sg = *s++ == '-' ? -1 : 1;
    if (s[0] < 'x' || s[0] > 'z' || s[1]) return 0;
    return sg * (s[0] - 'x' + 1);
}

static void conf_load(void)
{
    FILE *f = fopen(conf_path, "r");
    char l[256];
    if (!f) return;
    while (fgets(l, sizeof l, f)) {
        char *k = l, *v;
        l[strcspn(l, "\n#")] = 0;
        while (*k == ' ' || *k == '\t') k++;
        if (!(v = strchr(k, '='))) continue;
        *v++ = 0;
        for (char *ke = v - 1; ke > k && (ke[-1] == ' ' || ke[-1] == '\t'); ke--) ke[-1] = 0;
        while (*v == ' ' || *v == '\t') v++;
        size_t n = strlen(v);
        while (n && (v[n - 1] == ' ' || v[n - 1] == '\t')) v[--n] = 0;
        if (n >= 2 && (*v == '"' || *v == '\'') && v[n - 1] == *v) { v[n - 1] = 0; v++; }
        if (!strcmp(k, "SENSOR")) snprintf(sensor, sizeof sensor, "%s", v);
        else if (!strcmp(k, "OUTPUT") && *v) snprintf(output, sizeof output, "%s", v);
        else if (!strcmp(k, "RATE_HZ") && atoi(v) > 0) rate_hz = atoi(v);
        else if (!strcmp(k, "HOLD_MS") && atoi(v) >= 0) hold_ms = atoi(v);
        else if (!strcmp(k, "AXES")) {
            char a[16] = "", b[16] = "";
            int r, d;
            if (sscanf(v, "%15s %15s", a, b) == 2 && (r = axis(a)) && (d = axis(b)) && abs(r) != abs(d)) ax_r = r, ax_d = d;
            else { stamp(); fprintf(stderr, "%s: AXES \"%s\" not understood, kept %s\n", conf_path, v, "the default"); }
        }
    }
    fclose(f);
}

/* the screen-plane gravity of one x/y/z sample: r (towards the right edge), d (bottom) */
static void plane(const double *v, double *r, double *d)
{
    *r = (ax_r < 0 ? -1 : 1) * v[abs(ax_r) - 1];
    *d = (ax_d < 0 ? -1 : 1) * v[abs(ax_d) - 1];
}

/* ---- the rotation logic (shared with the replay) ------------------------------ */
struct rot { int cur, cand; int64_t cand_since; };

/* one sample (m/s^2, screen plane) -> the transform to apply now, or -1 */
static int rot_step(struct rot *s, double r, double d, int64_t t)
{
    int o = -1;
    double ar = fabs(r), ad = fabs(d);
    if (hypot(r, d) >= FLAT_G * G) {
        if (ad > DOMINANCE * ar) o = d > 0 ? 0 : 180;
        else if (ar > DOMINANCE * ad) o = r > 0 ? 270 : 90;
    }
    if (o < 0 || o == s->cur) { s->cand = -1; return -1; }
    if (o != s->cand) { s->cand = o; s->cand_since = t; return -1; }
    if (t - s->cand_since < hold_ms) return -1;
    s->cur = o;
    s->cand = -1;
    return o;
}

/* ---- the sensor ---------------------------------------------------------------- */
struct accel { char sys[480]; int num; double scale; int fd; char rate_attr[48], rate0[32]; };

static int accel_find(struct accel *a)
{
    char dir[192];
    snprintf(dir, sizeof dir, "%s/sys/bus/iio/devices", sysroot);
    DIR *d = opendir(dir);
    struct dirent *e;
    a->num = -1;
    if (!d) return -1;
    while ((e = readdir(d))) {
        char p[512], name[64];
        int num;
        if (sscanf(e->d_name, "iio:device%d", &num) != 1) continue;
        snprintf(p, sizeof p, "%s/%s/name", dir, e->d_name);
        if (rd(p, name, sizeof name)) continue;
        if (*sensor ? strcmp(name, sensor) != 0 : 0) continue;
        if (!*sensor) {
            snprintf(p, sizeof p, "%s/%s/scan_elements/in_accel_x_en", dir, e->d_name);
            if (access(p, F_OK)) continue;
        }
        snprintf(a->sys, sizeof a->sys, "%s/%s", dir, e->d_name);
        a->num = num;
        break;
    }
    closedir(d);
    if (a->num < 0) return -1;
    char p[512], b[32];
    snprintf(p, sizeof p, "%s/in_accel_scale", a->sys);
    rd(p, b, sizeof b);
    a->scale = atof(b);
    if (a->scale <= 0) a->scale = SCALE_DEFAULT;
    return 0;
}

static int accel_attr(struct accel *a, const char *attr, const char *val)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", a->sys, attr);
    return wr(p, val);
}

/* x/y/z on, no timestamp: 12-byte records (s32 each) */
static int accel_start(struct accel *a)
{
    char rate[16], dev[192], p[512];
    accel_attr(a, "buffer/enable", "0");
    accel_attr(a, "scan_elements/in_timestamp_en", "0");
    if (accel_attr(a, "scan_elements/in_accel_x_en", "1") || accel_attr(a, "scan_elements/in_accel_y_en", "1") ||
        accel_attr(a, "scan_elements/in_accel_z_en", "1") || accel_attr(a, "buffer/length", "16"))
        return -1;
    /* the rate, kept to put back at the stop */
    a->rate_attr[0] = 0;
    static const char *const ra[] = { "in_accel_sampling_frequency", "sampling_frequency" };
    for (unsigned i = 0; i < sizeof ra / sizeof *ra; i++) {
        snprintf(p, sizeof p, "%s/%s", a->sys, ra[i]);
        if (!rd(p, a->rate0, sizeof a->rate0)) { snprintf(a->rate_attr, sizeof a->rate_attr, "%s", ra[i]); break; }
    }
    snprintf(rate, sizeof rate, "%d", rate_hz);
    if (*a->rate_attr && accel_attr(a, a->rate_attr, rate)) { stamp(); fprintf(stderr, "rate %d Hz not taken\n", rate_hz); }
    snprintf(dev, sizeof dev, "%s/dev/iio:device%d", sysroot, a->num);
    a->fd = open(dev, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (a->fd < 0) return -1;
    if (accel_attr(a, "buffer/enable", "1")) { close(a->fd); a->fd = -1; return -1; }
    return 0;
}

static void accel_stop(struct accel *a)
{
    if (a->fd < 0) return;
    accel_attr(a, "buffer/enable", "0");
    if (*a->rate_attr && *a->rate0) accel_attr(a, a->rate_attr, a->rate0);
    close(a->fd);
    a->fd = -1;
}

/* ---- sway + the e-ink daemon -------------------------------------------------- */
/* SWAYSOCK, else the newest /run/user/(*)/sway-ipc.*.sock into p; -1: none. Newest by
 * mtime, not the last name: a sway that died without cleaning up leaves its socket,
 * and the pid in the name sorts as text */
static int sway_sock(char *p, size_t len)
{
    const char *e = getenv("SWAYSOCK");
    if (e && *e) { snprintf(p, len, "%s", e); return access(p, F_OK) ? -1 : 0; }
    glob_t g;
    if (glob("/run/user/*/sway-ipc.*.sock", 0, NULL, &g) || !g.gl_pathc) return -1;
    size_t best = 0;
    time_t bt = 0;
    for (size_t i = 0; i < g.gl_pathc; i++) {
        struct stat st;
        if (!stat(g.gl_pathv[i], &st) && st.st_mtime >= bt) { best = i; bt = st.st_mtime; }
    }
    snprintf(p, len, "%s", g.gl_pathv[best]);
    globfree(&g);
    return 0;
}

/* one i3-ipc RUN_COMMAND; 0 = sway said success */
static int sway_cmd(const char *cmd)
{
    int ok = -1;
    struct sockaddr_un sa = { .sun_family = AF_UNIX };
    if (sway_sock(sa.sun_path, sizeof sa.sun_path)) return -1;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct timeval tv = { 2, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (!connect(fd, (struct sockaddr *)&sa, sizeof sa)) {
        uint32_t len = strlen(cmd), type = 0;
        char hdr[14], rep[512];
        memcpy(hdr, "i3-ipc", 6);
        memcpy(hdr + 6, &len, 4);
        memcpy(hdr + 10, &type, 4);
        if (write(fd, hdr, 14) == 14 && write(fd, cmd, len) == (ssize_t)len) {
            ssize_t n = 0, k;
            while (n < (ssize_t)sizeof rep - 1 && (k = read(fd, rep + n, sizeof rep - 1 - n)) > 0) {
                n += k;
                if (n > 14 && memchr(rep + 14, ']', n - 14)) break;
            }
            rep[n > 0 ? n : 0] = 0;
            /* sway pretty-prints its reply: "success": true */
            const char *sc = n > 14 ? strstr(rep + 14, "\"success\"") : NULL;
            if (sc) {
                sc += 9;
                while (*sc == ' ' || *sc == ':' || *sc == '\n' || *sc == '\t') sc++;
                if (!strncmp(sc, "true", 4)) ok = 0;
            }
            if (ok && n > 14) { stamp(); fprintf(stderr, "sway: %s\n", rep + 14); }
        }
    }
    close(fd);
    return ok;
}

static void epdd(const char *cmd)
{
    int fd = open(CTL, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;                 /* no e-ink daemon */
    if (write(fd, cmd, strlen(cmd)) < 0) { /* full or gone: the turn goes on without the hold */ }
    close(fd);
}

static void state_write(int t)
{
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", state_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "%d\n", t);
    if (fclose(f) == 0) rename(tmp, state_path);
}

static int state_read(void)
{
    int t = 0;
    FILE *f = fopen(state_path, "r");
    if (f) { if (fscanf(f, "%d", &t) != 1 || (t != 90 && t != 180 && t != 270)) t = 0; fclose(f); }
    return t;
}

/* the setting file's identity: a rename (the root helper's mv) or a rewrite changes it */
static int64_t mode_id(void)
{
    struct stat st;
    if (stat(rot_path, &st)) return 0;
    return ((int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec) ^ ((int64_t)st.st_ino << 40) ^ st.st_size;
}

/* "lock [N]" -> 0 (*lock = N, or -1: as it is), anything else (or no file) -> 1 */
static int mode_auto(int64_t *mt, int *lock)
{
    *lock = -1;
    *mt = mode_id();
    if (!*mt) return 1;
    char b[32];
    rd(rot_path, b, sizeof b);
    if (strncmp(b, "lock", 4)) return 1;
    int n;
    if (sscanf(b + 4, "%d", &n) == 1 && (n == 0 || n == 90 || n == 180 || n == 270)) *lock = n;
    return 0;
}

static int replay(const char *path, int hz)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    struct accel a = { .fd = -1 };
    double scale = accel_find(&a) ? SCALE_DEFAULT : a.scale;
    struct rot s = { 0, -1, 0 };
    int32_t v[3];
    long n = 0;
    while (fread(v, sizeof v, 1, f) == 1) {
        int64_t t = n * 1000 / hz;
        double g[3] = { v[0] * scale, v[1] * scale, v[2] * scale }, r, d;
        plane(g, &r, &d);
        int o = rot_step(&s, r, d, t);
        if (o >= 0) printf("%6.2f s  transform %d\n", t / 1000.0, o);
        n++;
    }
    fclose(f);
    return 0;
}

static volatile sig_atomic_t quit;
static void on_term(int s) { (void)s; quit = 1; }

int main(int argc, char **argv)
{
    int c, hz = 0;
    const char *rp = NULL, *e = getenv("GLASSINE_ROTD_SYSROOT");
    if (e) snprintf(sysroot, sizeof sysroot, "%s", e);
    while ((c = getopt(argc, argv, "nvc:m:s:r:z:")) != -1) {
        if (c == 'n') dry = 1;
        else if (c == 'v') verbose = 1;
        else if (c == 'c') conf_path = optarg;
        else if (c == 'm') rot_path = optarg;
        else if (c == 's') state_path = optarg;
        else if (c == 'r') rp = optarg;
        else if (c == 'z' && atoi(optarg) > 0) hz = atoi(optarg);
        else { fprintf(stderr, "usage: glassine-rotd [-n] [-v] [-c CONF] [-m ROTATION] [-s STATE] [-r FILE [-z HZ]]\n"); return 2; }
    }
    conf_load();
    if (rp) return replay(rp, hz ? hz : rate_hz);
    signal(SIGTERM, on_term);
    signal(SIGINT, on_term);
    signal(SIGPIPE, SIG_IGN);

    struct accel a = { .fd = -1 };
    struct rot s = { state_read(), -1, 0 };
    char sock[108] = "";
    ino_t sock_ino = 0;
    int64_t mt = -1;
    int lock, autorot = mode_auto(&mt, &lock), said_none = 0;
    int64_t conf_check = 0, retry_at = 0, quiet_until = 0, vlog = 0, turn_at = 0;
    int said_turn = -1;
    int pending = !autorot && lock >= 0 && lock != s.cur ? lock : -1;   /* a turn sway has not taken yet */
    if (pending >= 0) s.cur = lock;
    stamp(); fprintf(stderr, "start (%s), sensor %s, output %s, %d Hz, transform %d%s\n", autorot ? "auto" : "lock",
                     *sensor ? sensor : "(any accelerometer)", output, rate_hz, s.cur, dry ? ", dry run" : "");

    while (!quit) {
        int64_t t = now_ms();
        if (t >= conf_check) {
            if (mode_id() != mt) {
                autorot = mode_auto(&mt, &lock);
                stamp(); fprintf(stderr, autorot ? "mode auto\n" : lock >= 0 ? "mode lock %d\n" : "mode lock\n", lock);
                s.cand = -1;
                if (!autorot && lock >= 0 && lock != s.cur) { s.cur = lock; pending = lock; }
            }
            /* a new sway starts at transform 0 (a new socket: by name, or the same
             * name made again, as with SWAYSOCK set for the new sway) */
            char sk[108];
            struct stat st;
            if (!sway_sock(sk, sizeof sk) && !stat(sk, &st) && (strcmp(sk, sock) || st.st_ino != sock_ino)) {
                if (s.cur > 0) pending = s.cur;
                snprintf(sock, sizeof sock, "%s", sk);
                sock_ino = st.st_ino;
            }
            conf_check = t + 1000;
        }
        if (autorot && a.fd < 0 && t >= retry_at) {
            if (accel_find(&a) || accel_start(&a)) {
                if (!said_none) { stamp(); fprintf(stderr, "no accelerometer stream yet (%s), retrying\n", a.num < 0 ? "no such IIO device" : strerror(errno)); said_none = 1; }
                retry_at = t + 5000;
            } else {
                stamp(); fprintf(stderr, "streaming %s, scale %g\n", a.sys, a.scale);
                retry_at = 0;
                said_none = 0;
            }
        }
        if (!autorot && a.fd >= 0) { accel_stop(&a); stamp(); fprintf(stderr, "stream off\n"); }

        int use = a.fd >= 0 && t >= quiet_until;
        struct pollfd pf = { use ? a.fd : -1, POLLIN, 0 };
        int n = poll(&pf, 1, use ? 1000 : (int)(quiet_until > t && quiet_until - t < 1000 ? quiet_until - t : 1000));
        if (n < 0 && errno != EINTR) break;
        t = now_ms();

        if (n > 0 && pf.revents & POLLIN) {
            int32_t v[3 * 16];
            ssize_t k = read(a.fd, v, sizeof v);
            if (k < 0 && errno != EAGAIN && errno != EINTR) {
                /* the device went away (driver reload): find it again */
                stamp(); fprintf(stderr, "stream lost (%s), reopening\n", strerror(errno));
                close(a.fd); a.fd = -1; retry_at = t + 1000;
                continue;
            }
            /* nothing to read although poll said so: a paused buffer; never turn it on
             * again here (a power daemon may have paused it for a suspend), just wait */
            if (k <= 0) { quiet_until = t + 1000; continue; }
            for (ssize_t i = 0; i + 12 <= k; i += 12) {
                int32_t *sm = v + i / 4;
                double g[3] = { sm[0] * a.scale, sm[1] * a.scale, sm[2] * a.scale }, r, d;
                plane(g, &r, &d);
                if (verbose && t >= vlog) { stamp(); fprintf(stderr, "right %.2f down %.2f m/s2\n", r, d); vlog = t + 1000; }
                int o = rot_step(&s, r, d, t);
                if (o >= 0) pending = o;
            }
        } else if (n > 0 && pf.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            close(a.fd); a.fd = -1; retry_at = t + 1000;
        }

        if (pending >= 0 && t >= turn_at) {
            char cmd[128];
            snprintf(cmd, sizeof cmd, "output %s transform %d", output, pending);
            if (pending != said_turn) { stamp(); fprintf(stderr, "transform %d\n", pending); said_turn = pending; }
            if (dry) pending = -1;
            else {
                epdd("rotate\n");
                if (!sway_cmd(cmd)) {
                    state_write(pending);
                    pending = said_turn = -1;
                    turn_at = 0;
                } else {
                    /* no sway (restarting, another session): again in a second; a
                     * new orientation meanwhile replaces it */
                    turn_at = t + 1000;
                }
            }
        }
    }
    accel_stop(&a);
    stamp(); fprintf(stderr, "stop\n");
    return 0;
}
