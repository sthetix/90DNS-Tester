#include <stdio.h>
#include <switch.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <time.h>
#include "main.h"

PrintConsole *console;
static const char *statusNames[] = {
    "blocked", "unblocked", "unresolved", "timeout", "offline", "error", "event-wait"
};

static void printResolverStatus(RESOLVER_STATUS status)
{
    const char *color = status == DNS_BLOCKED ? CONSOLE_GREEN : (status == DNS_RESOLVED ? CONSOLE_RED : CONSOLE_YELLOW);
    printf("%s%-11s" CONSOLE_RESET, color, statusNames[status]);
}

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

int main(int argc, char **argv)
{
    // Initialize default console
    console = consoleGetDefault();
    consoleInit(console);

    // Configure pad for player 1 accepting input from any controller
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadFullCtrl);
    padInitializeAny(&pad);

    // Initialize sockets
    socketInitializeDefault();
    // Initialize nifm
    nifmInitialize(NifmServiceType_User);

    checkHostnames();

    // Main loop
    while(appletMainLoop())
    {
        // Update the pad inputs
        padUpdate(&pad);

        // Get key pressed on the pad
        u64 kDown = padGetButtonsDown(&pad);

        // Exit on B
        if (kDown & HidNpadButton_B) break;
        // Retry on X
        if (kDown & HidNpadButton_X)
        {
            consoleClear();
            checkHostnames();
        }
    }

    // stop sockets before quit
    socketExit();
    // Quit nifm
    nifmExit();
    consoleExit(console);
    return 0;
}

void checkHostnames() 
{
    const char *logPath = "sdmc:/90dns-test-log.txt";
    bool asyncSupported = hosversionAtLeast(23, 0, 0);
    u32 version = hosversionGet();
    unsigned int legacyBlocked = 0, asyncBlocked = 0, asyncUnblocked = 0;
    unsigned int total = sizeof(hostnames) / sizeof(hostnames[0]);
    FILE *log = fopen(logPath, "a");
    if (log) {
        time_t now = time(NULL);
        struct tm *timestamp = localtime(&now);
        char date[32];
        fprintf(log, "\n=== 90DNS Testing Utility v1.0.5 ===\n");
        if (timestamp && strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", timestamp))
            fprintf(log, "Time: %s (console clock)\n", date);
        fprintf(log, "Firmware: %u.%u.%u\n", HOSVER_MAJOR(version), HOSVER_MINOR(version), HOSVER_MICRO(version));
        fprintf(log, "Legacy: gethostbyname | Async: FW 23 Query + Fetch\nDNS results do not prove that all traffic is blocked.\n");
        if (!asyncSupported)
            fprintf(log, "Async: N/A (requires FW 23 or newer)\n");
    }
    printf(CONSOLE_CYAN "90DNS Testing Utility v1.0.5" CONSOLE_RESET "\n");
    printf("FW %u.%u.%u  |  Legacy + Async%s  |  B: Exit  X: Retry\n", HOSVER_MAJOR(version), HOSVER_MINOR(version), HOSVER_MICRO(version), asyncSupported ? "" : " (N/A)");
    printf("DNS results do not verify all traffic is blocked.\n");
    printf("event-wait = no event within 2s; blocking unverified.\n");

    // Check if system says we're connected to a network (wifi or ethernet)
    u32 ip = 0;
    Result net_rc = nifmGetCurrentIpAddress(&ip);
    bool offline = R_FAILED(net_rc) || ip == 0;
    if (offline) {
        printf(CONSOLE_YELLOW "Network unavailable; blocking cannot be verified.\n" CONSOLE_RESET);
    }
    if (log) {
        if (offline)
            fprintf(log, "Network unavailable; blocking cannot be verified.\n");
        fprintf(log, "%-38s %-11s %-11s\n", "Domain", "Legacy", "Async");
    }

    printf("\n" CONSOLE_CYAN "%-38s %-11s %-11s\n" CONSOLE_RESET, "Domain", "Legacy", "Async");

    // Iterate through hostnames array
    for (int i = 0; i < sizeof(hostnames)/sizeof(hostnames[0]); i++)
    {
        printf("%-38s %-11s %-11s\r", hostnames[i], "...", asyncSupported ? "pending" : "N/A");
        consoleUpdate(console);

        RESOLVER_STATUS legacy = offline ? DNS_OFFLINE : resolveHostname(hostnames[i]);
        printf("%-38s ", hostnames[i]);
        printResolverStatus(legacy);
        printf(" %-11s\r", asyncSupported ? "..." : "N/A");
        consoleUpdate(console);

        RESOLVER_STATUS async = DNS_ERROR;
        if (asyncSupported)
            async = offline ? DNS_OFFLINE : resolveHostnameAsync(hostnames[i], log);
        legacyBlocked += legacy == DNS_BLOCKED;
        if (asyncSupported) {
            asyncBlocked += async == DNS_BLOCKED;
            asyncUnblocked += async == DNS_RESOLVED;
        }
        if (log) {
            fprintf(log, "%-38s %-11s %-11s\n", hostnames[i], statusNames[legacy], asyncSupported ? statusNames[async] : "N/A");
            fflush(log);
        }

        printf("%-38s ", hostnames[i]);
        printResolverStatus(legacy);
        printf(" ");
        if (asyncSupported)
            printResolverStatus(async);
        else
            printf(CONSOLE_YELLOW "%-11s" CONSOLE_RESET, "N/A");
        printf("\n");
        consoleUpdate(console);
    }

    if (log) {
        fprintf(log, "Summary: Legacy %u/%u blocked\n", legacyBlocked, total);
        if (asyncSupported)
            fprintf(log, "Async: %u blocked, %u unblocked, %u unverified\n", asyncBlocked, asyncUnblocked, total - asyncBlocked - asyncUnblocked);
        fprintf(log, "Press B to exit. Press X to retry.\n");
        bool logFailed = ferror(log) != 0;
        if (fclose(log) != 0)
            logFailed = true;
        if (logFailed)
            printf(CONSOLE_YELLOW "Log write failed: %s\n" CONSOLE_RESET, logPath);
        else
            printf("Log saved: %s\n", logPath);
    }
    else {
        printf(CONSOLE_YELLOW "Cannot open log: %s\n" CONSOLE_RESET, logPath);
    }
    printf("Legacy: %u/%u blocked", legacyBlocked, total);
    if (asyncSupported)
        printf(" | Async: %u blocked, %u unblocked, %u unknown", asyncBlocked, asyncUnblocked, total - asyncBlocked - asyncUnblocked);
    printf("\n");
    printf("B: Exit  |  X: Retry both tests");
    consoleUpdate(console);
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
