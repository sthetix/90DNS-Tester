#include <switch.h>
#include <switch/services/bsd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include "main.h"

static u32 readNetworkU32(const u8 *data)
{
    u32 value;
    memcpy(&value, data, sizeof(value));
    return ntohl(value);
}

static RESOLVER_STATUS checkAsyncAddresses(const u8 *data, size_t size)
{
    size_t offset = 0;
    bool found = false;
    bool unblocked = false;
    while (size - offset >= 4)
    {
        u32 magic = readNetworkU32(data + offset);
        if (magic == 0)
            return found ? (unblocked ? DNS_RESOLVED : DNS_BLOCKED) : DNS_UNRESOLVED;
        if (magic != 0xBEEFCAFE || size - offset < 24)
            return DNS_ERROR;

        u32 family = readNetworkU32(data + offset + 8);
        u32 length = readNetworkU32(data + offset + 20);
        offset += 24;
        size_t addressSize = length ? length : 4;
        if (addressSize > size - offset)
            return DNS_ERROR;
        if (family == AF_INET && length == 16)
        {
            // The serializer converts the stored s_addr integer to network order.
            u32 address = readNetworkU32(data + offset + 4);
            unblocked |= address != htonl(INADDR_LOOPBACK) && address != htonl(INADDR_ANY);
            found = true;
        }
        else if (family == AF_INET6 && length == 28)
        {
            static const u8 zero[16] = {0};
            static const u8 loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
            const u8 *address = data + offset + 8;
            unblocked |= memcmp(address, zero, 16) != 0 && memcmp(address, loopback, 16) != 0;
            found = true;
        }
        else
            return DNS_ERROR;
        offset += addressSize;
        const u8 *end = memchr(data + offset, 0, size - offset);
        if (!end)
            return DNS_ERROR;
        offset = (size_t)(end - data) + 1;
    }
    return DNS_ERROR;
}

RESOLVER_STATUS resolveHostnameAsync(const char* hostname, FILE *log)
{
    if (!hosversionAtLeast(23, 0, 0) || !serviceIsActive(bsdGetServiceSession()))
        return DNS_ERROR;

    Service resolver = {0};
    if (R_FAILED(smGetService(&resolver, "sfdnsres")))
        return DNS_ERROR;

    const struct { u64 initial; u32 flags; u32 padding; } eventIn = {0, 0, 0};
    struct { s32 fd; s32 error; } eventOut = {-1, 0};
    Result rc = serviceDispatchInOut(bsdGetServiceSession(), 31, eventIn, eventOut);
    if (log)
        fprintf(log, "[async %s] EventFd: rc=0x%08X fd=%d errno=%d\n", hostname, rc, eventOut.fd, eventOut.error);
    if (R_FAILED(rc) || eventOut.fd < 0)
    {
        serviceClose(&resolver);
        return DNS_ERROR;
    }

    const struct {
        u32 fd, version, count, padding;
        u64 pid;
    } queryIn = {(u32)eventOut.fd, 0, 0, 0, 0};
    struct { u32 id; s32 retval; s32 error; } queryOut = {0};
    rc = serviceDispatchInOut(&resolver, 100, queryIn, queryOut,
        .in_send_pid = true,
        .buffer_attrs = {
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
            SfBufferAttr_HipcAutoSelect | SfBufferAttr_In,
        },
        .buffers = {{hostname, strlen(hostname) + 1}, {NULL, 0}, {NULL, 0}, {NULL, 0}},
    );
    if (log)
        fprintf(log, "[async %s] Query: rc=0x%08X id=%u retval=%d errno=%d\n", hostname, rc, queryOut.id, queryOut.retval, queryOut.error);

    RESOLVER_STATUS status = DNS_ERROR;
    if (R_SUCCEEDED(rc) && queryOut.retval == 0)
    {
        struct pollfd fd = {eventOut.fd, POLLIN, 0};
        int ready = bsdPoll(&fd, 1, 2000);
        if (log)
            fprintf(log, "[async %s] Poll: ready=%d revents=0x%04X rc=0x%08X errno=%d wait=2000ms\n", hostname, ready, (unsigned short)fd.revents, g_bsdResult, g_bsdErrno);
        if (ready > 0 && (fd.revents & POLLIN))
        {
            u64 signal = 0;
            ssize_t readSize = bsdRead(eventOut.fd, &signal, sizeof(signal));
            if (log)
                fprintf(log, "[async %s] EventRead: bytes=%ld signal=%llu rc=0x%08X errno=%d\n", hostname, (long)readSize, (unsigned long long)signal, g_bsdResult, g_bsdErrno);
            if (readSize != sizeof(signal))
                goto cancel_query;
            u8 buffer[0x1000] = {0};
            const struct { u32 id; u32 version; u64 pid; } fetchIn = {queryOut.id, 0, 0};
            struct { u32 size; s32 retval; s32 error; } fetchOut = {0};
            rc = serviceDispatchInOut(&resolver, 102, fetchIn, fetchOut,
                .in_send_pid = true,
                .buffer_attrs = {SfBufferAttr_HipcAutoSelect | SfBufferAttr_Out},
                .buffers = {{buffer, sizeof(buffer)}},
            );
            if (log)
                fprintf(log, "[async %s] Fetch: rc=0x%08X size=%u retval=%d errno=%d\n", hostname, rc, fetchOut.size, fetchOut.retval, fetchOut.error);
            if (R_SUCCEEDED(rc))
            {
                if (fetchOut.error == ETIMEDOUT)
                    status = DNS_TIMEOUT;
                else if (fetchOut.retval != 0)
                    status = DNS_UNRESOLVED;
                else if (fetchOut.size <= sizeof(buffer))
                    status = checkAsyncAddresses(buffer, fetchOut.size);
            }
        }
        else
        {
            status = ready == 0 ? DNS_EVENT_WAIT : DNS_ERROR;
            if (log)
                fprintf(log, "[async %s] Fetch not attempted: event not ready; blocking is unverified.\n", hostname);
        }

cancel_query:;
        // Release any queued query, including after a timeout or failed fetch.
        const struct { u32 id; u32 padding; u64 pid; } cancelIn = {queryOut.id, 0, 0};
        rc = serviceDispatchIn(&resolver, 104, cancelIn, .in_send_pid = true);
        if (log)
            fprintf(log, "[async %s] Cancel: rc=0x%08X\n", hostname, rc);
    }
    else if (R_SUCCEEDED(rc))
    {
        status = queryOut.error == ETIMEDOUT ? DNS_TIMEOUT : DNS_UNRESOLVED;
    }

    bsdClose(eventOut.fd);
    serviceClose(&resolver);
    return status;
}
