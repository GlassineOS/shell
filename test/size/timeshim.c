/* timeshim.c — LD_PRELOAD for the renders (test/size): the wall clock stands still at
 * GLASSINE_TEST_TIME (seconds since the epoch), so the bar's clock and the notifications'
 * times draw the same in every run. The monotonic clocks run on. */
#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static time_t fixed(void)
{
    const char *e = getenv("GLASSINE_TEST_TIME");
    return e ? (time_t)atoll(e) : 0;
}

time_t time(time_t *t)
{
    time_t v = fixed();
    if (t) *t = v;
    return v;
}

int clock_gettime(clockid_t c, struct timespec *ts)
{
    if (c == CLOCK_REALTIME || c == CLOCK_REALTIME_COARSE) { ts->tv_sec = fixed(); ts->tv_nsec = 0; return 0; }
    return (int)syscall(SYS_clock_gettime, c, ts);
}

int gettimeofday(struct timeval *restrict tv, void *restrict tz)
{
    (void)tz;
    if (tv) { tv->tv_sec = fixed(); tv->tv_usec = 0; }
    return 0;
}
