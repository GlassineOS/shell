/* sway-ipc.c — see sway-ipc.h */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "sway-ipc.h"

int sipc_open(void)
{
    const char *p = getenv("SWAYSOCK");
    if (!p || !*p) return -1;
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", p);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd >= 0 && connect(fd, (struct sockaddr *)&a, sizeof a)) { close(fd); fd = -1; }
    return fd;
}

static int full_io(int fd, void *b, size_t n, int wr)
{
    for (size_t o = 0; o < n;) {
        ssize_t r = wr ? write(fd, (char *)b + o, n - o) : read(fd, (char *)b + o, n - o);
        if (r <= 0) { if (r < 0 && errno == EINTR) continue; return 0; }
        o += r;
    }
    return 1;
}

int sipc_send(int fd, uint32_t type, const char *payload)
{
    uint32_t len = payload ? strlen(payload) : 0;
    char h[14];
    memcpy(h, "i3-ipc", 6); memcpy(h + 6, &len, 4); memcpy(h + 10, &type, 4);
    return full_io(fd, h, 14, 1) && full_io(fd, (void *)(payload ? payload : ""), len, 1);
}

char *sipc_recv(int fd, uint32_t *type)
{
    char h[14];
    if (!full_io(fd, h, 14, 0) || memcmp(h, "i3-ipc", 6)) return NULL;
    uint32_t len;
    memcpy(&len, h + 6, 4); memcpy(type, h + 10, 4);
    char *p = malloc(len + 1);
    if (!p || !full_io(fd, p, len, 0)) { free(p); return NULL; }
    p[len] = 0;
    return p;
}

/* json_tokener_parse stops at 32 levels: sway's tree nests deeper once
 * "move container to mark" / "split" have left single-child containers behind
 * (2 levels each: the object and its "nodes" array), and a NULL here made
 * glassine-swayd's handle taps and drags silently do nothing (A/B in
 * test/shell-sway DEPTH=12). glassine-swayd's build no longer nests it; this is
 * the guard for everything else that reads the tree (glassine-bgprio, the shell) */
json_object *sj_parse(const char *s)
{
    json_tokener *tok = json_tokener_new_ex(4096);
    if (!tok) return NULL;
    json_object *j = json_tokener_parse_ex(tok, s, -1);
    json_tokener_free(tok);
    return j;
}

json_object *sipc_call(int fd, uint32_t type, const char *payload)
{
    uint32_t t;
    char *r;
    if (fd < 0 || !sipc_send(fd, type, payload) || !(r = sipc_recv(fd, &t))) return NULL;
    json_object *j = sj_parse(r);
    free(r);
    return j;
}

const char *sj_str(json_object *o, const char *k)
{
    json_object *v;
    return json_object_object_get_ex(o, k, &v) && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

long sj_int(json_object *o, const char *k, long dflt)
{
    json_object *v;
    return json_object_object_get_ex(o, k, &v) && !json_object_is_type(v, json_type_null) ? json_object_get_int64(v) : dflt;
}

int sj_bool(json_object *o, const char *k)
{
    json_object *v;
    return json_object_object_get_ex(o, k, &v) && json_object_get_boolean(v);
}

void sj_rect(json_object *o, const char *k, int scale, int r[4])
{
    json_object *v;
    r[0] = r[1] = r[2] = r[3] = 0;
    if (!json_object_object_get_ex(o, k, &v)) return;
    r[0] = sj_int(v, "x", 0) * scale; r[1] = sj_int(v, "y", 0) * scale;
    r[2] = sj_int(v, "width", 0) * scale; r[3] = sj_int(v, "height", 0) * scale;
}
