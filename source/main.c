#include <stdio.h>
#include <switch.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>
#include "main.h"

static const char *statusNames[] = {
    "blocked", "unblocked", "unresolved", "timeout", "offline", "error", "timeout"
};

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
    u32 version = hosversionGet();
    unsigned int legacyBlocked = 0, asyncBlocked = 0, asyncUnblocked = 0;
    unsigned int total = hostnameCount();
    FILE *log = fopen("sdmc:/90dns-test-log.txt", "a");
    if (log) {
        time_t now = time(NULL);
        struct tm *timestamp = localtime(&now);
        char date[32];
        fprintf(log, "\n=== 90DNS Testing Utility v1.0.6 ===\n");
        if (timestamp && strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", timestamp))
            fprintf(log, "Time: %s (console clock)\n", date);
        fprintf(log, "Firmware: %u.%u.%u\n", HOSVER_MAJOR(version), HOSVER_MINOR(version), HOSVER_MICRO(version));
        fprintf(log, "Legacy: gethostbyname | c-ares: FW 23 Query + Fetch\nDNS results do not prove that all traffic is blocked.\n");
        if (!asyncSupported)
            fprintf(log, "c-ares: N/A (requires FW 23 or newer)\n");
    }
    u32 ip = 0;
    Result net_rc = nifmGetCurrentIpAddress(&ip);
    bool offline = R_FAILED(net_rc) || ip == 0;
    if (log) {
        if (offline)
            fprintf(log, "Network unavailable; blocking cannot be verified.\n");
        fprintf(log, "%-38s %-11s %-11s\n", "Domain", "Legacy", "c-ares");
    }
    unsigned int completed = 0;
    for (unsigned int i = 0; i < total; i++) {
        if (!progress(context, i, DNS_PENDING, DNS_PENDING))
            break;
        RESOLVER_STATUS legacy = offline ? DNS_OFFLINE : resolveHostname(hostnames[i]);
        if (!progress(context, i, legacy, DNS_PENDING))
            break;
        RESOLVER_STATUS async = DNS_ERROR;
        if (asyncSupported)
            async = offline ? DNS_OFFLINE : resolveHostnameAsync(hostnames[i], log);
        legacyBlocked += legacy == DNS_BLOCKED;
        if (asyncSupported) {
            asyncBlocked += async == DNS_BLOCKED;
            asyncUnblocked += async == DNS_RESOLVED;
        }
        completed++;
        if (log) {
            fprintf(log, "%-38s %-11s %-11s\n", hostnames[i], statusNames[legacy], asyncSupported ? statusNames[async] : "N/A");
            fflush(log);
        }
        if (!progress(context, i, legacy, async))
            break;
    }
    if (log) {
        fprintf(log, "Completed: %u/%u domains\nSummary: Legacy %u/%u blocked\n", completed, total, legacyBlocked, total);
        if (asyncSupported)
            fprintf(log, "c-ares: %u blocked, %u unblocked, %u unverified (including unfinished queries)\n", asyncBlocked, asyncUnblocked, total - asyncBlocked - asyncUnblocked);
        bool failed = ferror(log) != 0;
        if (fclose(log) != 0)
            failed = true;
        progress(context, total, failed ? DNS_ERROR : DNS_BLOCKED, DNS_PENDING);
    } else {
        progress(context, total, DNS_ERROR, DNS_PENDING);
    }
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
