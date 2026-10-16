#include <stdio.h>
#include <switch.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include "main.h"

const char *hostnames[] = {
    "nintendo.com",
    "nintendo.com",
    "nintendo.net",
    "nintendo.jp",
    "nintendo.co.jp",
    "nintendo.co.uk",
    "nintendo-europe.com",
    "nintendowifi.net",
    "nintendo.es",
    "nintendo.co.kr",
    "nintendo.tw",
    "nintendo.com.hk",
    "nintendo.com.au",
    "nintendo.co.nz",
    "nintendo.at",
    "nintendo.be",
    "nintendods.cz",
    "nintendo.dk",
    "nintendo.de",
    "nintendo.fi",
    "nintendo.fr",
    "nintendo.gr",
    "nintendo.hu",
    "nintendo.it",
    "nintendo.nl",
    "nintendo.no",
    "nintendo.pt",
    "nintendo.ru",
    "nintendo.co.za",
    "nintendo.se",
    "nintendo.ch",
    "nintendoswitch.cn",
    "nintendoswitch.com.cn",
    "nintendoswitch.com",
    "sun.hac.lp1.d4c.nintendo.net"
};

unsigned int hostnameCount(void)
{
    return sizeof(hostnames) / sizeof(hostnames[0]);
}

const char *hostnameAt(unsigned int index)
{
    return index < hostnameCount() ? hostnames[index] : "";
}

void checkHostnames(DnsProgress progress, void *context)
{
    bool asyncSupported = hosversionAtLeast(23, 0, 0);
    unsigned int total = hostnameCount();
    u32 ip = 0;
    Result net_rc = nifmGetCurrentIpAddress(&ip);
    bool offline = R_FAILED(net_rc) || ip == 0;
    for (unsigned int i = 0; i < total; i++) {
        if (!progress(context, i, DNS_PENDING, DNS_PENDING))
            break;
        RESOLVER_STATUS legacy = offline ? DNS_OFFLINE : resolveHostname(hostnames[i]);
        if (!progress(context, i, legacy, DNS_PENDING))
            break;
        RESOLVER_STATUS async = DNS_ERROR;
        if (asyncSupported)
            async = offline ? DNS_OFFLINE : resolveHostnameAsync(hostnames[i]);
        if (!progress(context, i, legacy, async))
            break;
    }
    progress(context, total, DNS_BLOCKED, DNS_PENDING);
}

RESOLVER_STATUS resolveHostname(const char* hostname)
{
    struct hostent *he;
    struct in_addr a;
    // Resolve through the legacy API; this does not attempt a connection.
    errno = 0;
    he = gethostbyname(hostname);
    if (he)
    {
        RESOLVER_STATUS result = DNS_UNRESOLVED;
        if (he->h_addrtype != AF_INET || he->h_length != sizeof(a) || !he->h_addr_list)
        {
            freehostent(he);
            return DNS_UNRESOLVED;
        }
        // Every returned address must be blocked.
        for (char **address = he->h_addr_list; *address; address++)
        {
            memcpy(&a, *address, sizeof(a));
            if (a.s_addr != htonl(INADDR_LOOPBACK) && a.s_addr != htonl(INADDR_ANY))
            {
                result = DNS_RESOLVED;
                break;
            }
            result = DNS_BLOCKED;
        }
        freehostent(he);
        return result;
    }
    return errno == ETIMEDOUT ? DNS_TIMEOUT : DNS_UNRESOLVED;
}
