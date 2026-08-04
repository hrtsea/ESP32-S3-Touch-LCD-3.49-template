/* sim/stubs/lwip/sockets.h - lwip socket stub（仅 snmp_client.c 使用，已 override） */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define AF_INET       2
#define SOCK_DGRAM    2
#define SOCK_STREAM   1
#define IPPROTO_UDP   17
#define IPPROTO_TCP   6
#define SOL_SOCKET     0xffff
#define SO_REUSEADDR   0x0004
#define SO_BROADCAST   0x0020
#define SO_RCVTIMEO    0x1006
#define SO_SNDTIMEO    0x1005

typedef uint32_t socklen_t;
struct timeval;

static inline int lwip_socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol; return -1;
}
static inline int lwip_close(int s) { (void)s; return -1; }
static inline int lwip_setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen) {
    (void)s; (void)level; (void)optname; (void)optval; (void)optlen; return -1;
}
static inline int lwip_send(int s, const void *dataptr, size_t size, int flags) {
    (void)s; (void)dataptr; (void)size; (void)flags; return -1;
}
static inline int lwip_recv(int s, void *mem, size_t len, int flags) {
    (void)s; (void)mem; (void)len; (void)flags; return -1;
}

#define socket(a,b,c) lwip_socket(a,b,c)
#define close(s) lwip_close(s)
#define setsockopt(a,b,c,d,e) lwip_setsockopt(a,b,c,d,e)
#define send(a,b,c,d) lwip_send(a,b,c,d)
#define recv(a,b,c,d) lwip_recv(a,b,c,d)

#ifdef __cplusplus
}
#endif
