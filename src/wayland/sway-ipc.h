/* sway-ipc.h — sway's IPC (i3-ipc framing) for glassine-shell-wl and glassine-swayd:
 * a connection to $SWAYSOCK, messages, JSON replies (json-c), field helpers. */
#ifndef GLASSINE_SWAY_IPC_H
#define GLASSINE_SWAY_IPC_H
#include <stdint.h>
#include <json-c/json.h>

enum { SIPC_COMMAND = 0, SIPC_GET_WORKSPACES = 1, SIPC_SUBSCRIBE = 2, SIPC_GET_OUTPUTS = 3, SIPC_GET_TREE = 4 };
#define SIPC_EV_WORKSPACE 0x80000000u
#define SIPC_EV_OUTPUT 0x80000001u
#define SIPC_EV_WINDOW 0x80000003u

int sipc_open(void);                                    /* -1: no $SWAYSOCK / no sway */
int sipc_send(int fd, uint32_t type, const char *payload);
char *sipc_recv(int fd, uint32_t *type);                /* malloc'd payload, NULL: connection lost */
json_object *sipc_call(int fd, uint32_t type, const char *payload);   /* NULL: lost or not JSON */
json_object *sj_parse(const char *s);                   /* any depth (sway's tree): NULL: not JSON */

const char *sj_str(json_object *o, const char *k);      /* NULL: missing / not a string */
long sj_int(json_object *o, const char *k, long dflt);
int sj_bool(json_object *o, const char *k);
void sj_rect(json_object *o, const char *k, int scale, int r[4]);   /* x y w h, times scale */
#endif
