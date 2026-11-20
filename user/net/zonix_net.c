#include "minilib.h"
#include "netproto.h"
#include "zonix_net.h"

/* netd may still be starting when a service started next to it calls in. */
#define SERVICE_WAIT_MS 2000
#define RETRY_MS        20
#define REPLY_WAIT_MS   1000

static int read_full(int fd, void* output, long size) {
    char* bytes = (char*)output;
    long done = 0;
    while (done < size) {
        long count = sys_read(fd, bytes + done, size - done);
        if (count <= 0)
            return NET_ERROR_IO;
        done += count;
    }
    return 0;
}

static int write_full(int fd, const void* data, long size) {
    const char* bytes = (const char*)data;
    long done = 0;
    while (done < size) {
        long count = sys_write(fd, (void*)(bytes + done), size - done);
        if (count <= 0)
            return NET_ERROR_IO;
        done += count;
    }
    return 0;
}

/* Sleeps by waiting on a pipe that never becomes readable. */
static void pause_ms(long milliseconds) {
    int ends[2];
    if (sys_pipe2(ends, O_CLOEXEC) != 0) {
        sys_yield();
        return;
    }
    sys_wait_readable_for(1L << ends[0], milliseconds);
    sys_close(ends[0]);
    sys_close(ends[1]);
}

static int open_service(long wait_ms) {
    long deadline = sys_uptime_ms() + wait_ms;
    for (;;) {
        long fd = sys_channel_connect(NETD_CHANNEL, O_CLOEXEC);
        if (fd >= 0)
            return (int)fd;
        if (fd != SYS_ERROR_NOT_FOUND)
            return NET_ERROR_RESOURCES;
        if (sys_uptime_ms() >= deadline)
            return NET_ERROR_NO_SERVICE;
        pause_ms(RETRY_MS);
    }
}

/* Sends a request on a new channel. On success the channel stays open. */
static int transact_within(const struct NetRequest* request, struct NetReply* reply, long wait_ms) {
    int fd = open_service(wait_ms);
    if (fd < 0)
        return fd;
    int sent = write_full(fd, request, sizeof(*request)) == 0;
    /* A caller that does not wait for netd does not wait long for its answer either. */
    int answered = sent && (wait_ms != 0 || sys_wait_readable_for(1L << fd, REPLY_WAIT_MS) > 0);
    if (!answered || read_full(fd, reply, sizeof(*reply)) < 0) {
        sys_close(fd);
        return NET_ERROR_NO_SERVICE;
    }
    if (reply->status < 0) {
        sys_close(fd);
        return reply->status;
    }
    return fd;
}

static int transact(const struct NetRequest* request, struct NetReply* reply) {
    return transact_within(request, reply, SERVICE_WAIT_MS);
}

static int valid_port(int port) {
    return port >= 0 && port <= 65535;
}

int net_tcp_connect(unsigned int address, int port) {
    if (!valid_port(port) || port == 0)
        return NET_ERROR_INVALID;
    struct NetRequest request = {.op = NET_OP_TCP_CONNECT, .address = address, .port = (unsigned short)port};
    struct NetReply reply;
    return transact(&request, &reply);
}

int net_tcp_listen(int port) {
    if (!valid_port(port) || port == 0)
        return NET_ERROR_INVALID;
    struct NetRequest request = {.op = NET_OP_TCP_LISTEN, .port = (unsigned short)port};
    struct NetReply reply;
    return transact(&request, &reply);
}

int net_tcp_accept(int listener, unsigned int* address, int* port) {
    struct NetReply notice;
    if (read_full(listener, &notice, sizeof(notice)) < 0)
        return NET_ERROR_IO;
    struct NetRequest request = {.op = NET_OP_TCP_CLAIM, .value = notice.value};
    struct NetReply reply;
    int fd = transact(&request, &reply);
    if (fd >= 0) {
        if (address)
            *address = notice.address;
        if (port)
            *port = notice.port;
    }
    return fd;
}

int net_shutdown_write(int fd) {
    return sys_ioctl(fd, CHANNEL_SHUTDOWN_WRITE, 0) == 0 ? 0 : NET_ERROR_INVALID;
}

int net_udp_open(int port) {
    if (!valid_port(port))
        return NET_ERROR_INVALID;
    struct NetRequest request = {.op = NET_OP_UDP_OPEN, .port = (unsigned short)port};
    struct NetReply reply;
    return transact(&request, &reply);
}

long net_udp_send(int fd, unsigned int address, int port, const void* data, long size) {
    if (!valid_port(port) || port == 0 || size < 0 || size > NET_DATAGRAM_MAX)
        return NET_ERROR_INVALID;
    unsigned char frame[sizeof(struct NetDatagram) + NET_DATAGRAM_MAX];
    struct NetDatagram header = {.address = address, .port = (unsigned short)port, .length = (unsigned short)size};
    memcpy(frame, &header, sizeof(header));
    memcpy(frame + sizeof(header), (void*)data, size);
    /* One write keeps the header and its bytes together in the channel. */
    long total = (long)sizeof(header) + size;
    return sys_write(fd, frame, total) == total ? size : NET_ERROR_IO;
}

long net_udp_receive(int fd, unsigned int* address, int* port, void* data, long size) {
    struct NetDatagram header;
    if (read_full(fd, &header, sizeof(header)) < 0)
        return NET_ERROR_IO;
    unsigned char discard[64];
    long kept = header.length < size ? header.length : size;
    if (read_full(fd, data, kept) < 0)
        return NET_ERROR_IO;
    for (long left = header.length - kept; left > 0;) {
        long count = left < (long)sizeof(discard) ? left : (long)sizeof(discard);
        if (read_full(fd, discard, count) < 0)
            return NET_ERROR_IO;
        left -= count;
    }
    if (address)
        *address = header.address;
    if (port)
        *port = header.port;
    return kept;
}

int net_resolve(const char* name, unsigned int* address) {
    if (net_parse_ipv4(name, address) == 0)
        return 0;
    long length = strlen((char*)name);
    if (length == 0 || length > NET_NAME_MAX)
        return NET_ERROR_INVALID;
    struct NetRequest request = {.op = NET_OP_RESOLVE};
    memcpy(request.name, (void*)name, length);
    struct NetReply reply;
    int fd = transact(&request, &reply);
    if (fd < 0)
        return fd;
    sys_close(fd);
    *address = reply.address;
    return 0;
}

int net_ping(unsigned int address, int timeout_ms, int* rtt_ms) {
    if (timeout_ms <= 0)
        return NET_ERROR_INVALID;
    struct NetRequest request = {.op = NET_OP_PING, .address = address, .value = (unsigned int)timeout_ms};
    struct NetReply reply;
    int fd = transact(&request, &reply);
    if (fd < 0)
        return fd;
    sys_close(fd);
    if (rtt_ms)
        *rtt_ms = (int)reply.value;
    return 0;
}

int net_info(struct NetInfo* info) {
    struct NetRequest request = {.op = NET_OP_INFO};
    struct NetReply reply;
    int fd = transact_within(&request, &reply, 0);
    if (fd < 0)
        return fd;
    sys_close(fd);
    info->address = reply.address;
    info->netmask = reply.value;
    info->gateway = reply.gateway;
    info->dns = reply.dns;
    return 0;
}

int net_parse_ipv4(const char* text, unsigned int* address) {
    unsigned int value = 0;
    for (int part = 0; part < 4; ++part) {
        unsigned int number = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9' && digits < 3) {
            number = number * 10 + (unsigned int)(*text++ - '0');
            ++digits;
        }
        if (digits == 0 || number > 255 || *text != (part == 3 ? '\0' : '.'))
            return NET_ERROR_INVALID;
        if (part != 3)
            ++text;
        value = value << 8 | number;
    }
    *address = value;
    return 0;
}

void net_format_ipv4(unsigned int address, char* out) {
    for (int part = 3; part >= 0; --part) {
        unsigned int number = (address >> (part * 8)) & 0xFF;
        if (number >= 100)
            *out++ = (char)('0' + number / 100);
        if (number >= 10)
            *out++ = (char)('0' + number / 10 % 10);
        *out++ = (char)('0' + number % 10);
        if (part != 0)
            *out++ = '.';
    }
    *out = '\0';
}

const char* net_error_text(int error) {
    switch (error) {
        case NET_ERROR_NO_SERVICE: return "network service (netd) is not running";
        case NET_ERROR_REFUSED: return "connection refused";
        case NET_ERROR_UNREACHABLE: return "host unreachable";
        case NET_ERROR_TIMEOUT: return "timed out";
        case NET_ERROR_NOT_FOUND: return "host not found";
        case NET_ERROR_IN_USE: return "port in use";
        case NET_ERROR_RESOURCES: return "out of sockets";
        case NET_ERROR_INVALID: return "invalid argument";
        case NET_ERROR_NO_ADDRESS: return "no network address yet";
        default: return "network I/O error";
    }
}
