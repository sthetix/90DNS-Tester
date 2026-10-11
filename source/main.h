#ifndef __DNSTEST_MAIN_H
#define __DNSTEST_MAIN_H
#include <stdio.h>

typedef enum RESOLVER_STATUS {
    DNS_BLOCKED,
    DNS_RESOLVED,
    DNS_UNRESOLVED,
    DNS_TIMEOUT,
    DNS_OFFLINE,
    DNS_ERROR,
    DNS_EVENT_WAIT
} RESOLVER_STATUS;

RESOLVER_STATUS resolveHostname(const char* hostname);
RESOLVER_STATUS resolveHostnameAsync(const char* hostname, FILE *log);
void checkHostnames();

#endif
