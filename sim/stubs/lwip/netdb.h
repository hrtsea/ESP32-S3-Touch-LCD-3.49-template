/* sim/stubs/lwip/netdb.h - lwip netdb stub（仅 snmp_client.c 使用，已 override） */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

static inline struct hostent *lwip_gethostbyname(const char *name) {
    (void)name; return NULL;
}

#define gethostbyname(n) lwip_gethostbyname(n)

#ifdef __cplusplus
}
#endif
