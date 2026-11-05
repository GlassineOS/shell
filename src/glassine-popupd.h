/* glassine-popupd.h — ask glassine-popupd (the resident panel popups, python + GTK
 * already loaded) to open a popup instead of starting its script, which cost
 * ~1.3 s per tap. */
#ifndef GLASSINE_POPUPD_H
#define GLASSINE_POPUPD_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* argv[0] is the popup's name (or, for glassine-shell, the request) (glassine-quick, glassine-tilemenu, ...). Returns 0 once
 * the request is queued, -1 when glassine-popupd doesn't run (then start the
 * script as before). */
static inline int glassine_sock_send(const char *sock, const char *const *argv, int argc)
{
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (snprintf(a.sun_path, sizeof a.sun_path, "%s/%s", dir && *dir ? dir : "/tmp", sock) >= (int)sizeof a.sun_path)
        return -1;
    char msg[1024];
    size_t n = 0;
    for (int i = 0; i < argc; i++) {
        size_t l = strlen(argv[i]) + 1;          /* NUL-separated */
        if (n + l > sizeof msg) return -1;
        memcpy(msg + n, argv[i], l);
        n += l;
    }
    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    int r = sendto(fd, msg, n - 1, MSG_DONTWAIT, (struct sockaddr *)&a, sizeof a) == (ssize_t)(n - 1) ? 0 : -1;
    close(fd);
    return r;
}

static inline int glassine_popupd(const char *const *argv, int argc) { return glassine_sock_send("glassine-popupd", argv, argc); }

/* glassine-shell (the bar + popups, glassine-shell.c): same protocol, its own socket */
static inline int glassine_shell(const char *const *argv, int argc) { return glassine_sock_send("glassine-shell", argv, argc); }
#endif
