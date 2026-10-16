#ifndef __DNSTEST_MAIN_H
#define __DNSTEST_MAIN_H
#include <stdio.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RESOLVER_STATUS {
    DNS_BLOCKED,
    DNS_RESOLVED,
    DNS_UNRESOLVED,
    DNS_TIMEOUT,
    DNS_OFFLINE,
    DNS_ERROR,
    DNS_EVENT_WAIT,
    DNS_PENDING
} RESOLVER_STATUS;

RESOLVER_STATUS resolveHostname(const char* hostname);
RESOLVER_STATUS resolveHostnameAsync(const char* hostname, FILE *log);
typedef bool (*DnsProgress)(void *context, unsigned int index, RESOLVER_STATUS legacy, RESOLVER_STATUS async);
unsigned int hostnameCount(void);
const char *hostnameAt(unsigned int index);
void checkHostnames(DnsProgress progress, void *context);

#ifdef __cplusplus
}
#endif

#endif
