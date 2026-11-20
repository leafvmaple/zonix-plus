#include "../command.h"
#include "net.h"
#include "netproto.h"
#include "zonix_net.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/inet_chksum.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/prot/icmp.h"
#include "lwip/raw.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/ethernet.h"

/* netd owns /dev/net0 and the lwIP stack and serves sockets to other programs
 * over the channel NETD_CHANNEL; netproto.h describes the protocol. */

#define MAX_CLIENTS           16
#define MAX_PENDING           16
#define MAX_WAIT_MS           250
#define FRAMES_PER_WAKE       32
#define STAGING_BYTES         2048
#define ETHERNET_MIN_TX_BYTES 60
#define PING_ID               0x5A4E
#define PING_PAYLOAD_BYTES    32
/* sys_wait_io reports write readiness in the 24 bits above the read bits. */
#define WRITE_READY_SHIFT 24

enum State { FREE, REQUEST, CONNECTING, STREAM, LISTENING, DATAGRAM, RESOLVING, PINGING };

struct Client;

/* A TCP connection. One accepted by a listener waits, unclaimed, until a
 * TCP_CLAIM with its token attaches it to a client. */
struct Connection {
    struct tcp_pcb* pcb;
    struct Client* owner;
    unsigned int token;
    int claimed;
    struct pbuf* inbound; /* received, not yet written to the channel */
    int peer_eof;         /* the peer sent FIN */
    int peer_eof_sent;    /* the channel's write direction is shut down */
    int local_eof;        /* the client shut down its direction */
    int client_gone;      /* the client closed its descriptor */
    unsigned char staging[STAGING_BYTES]; /* read from the channel, not yet accepted by TCP */
    u16_t staged;
};

struct Client {
    int fd;
    enum State state;
    unsigned int generation;
    unsigned char request[sizeof(struct NetRequest)];
    unsigned int request_size;
    struct Connection* connection;
    struct tcp_pcb* listener;
    struct udp_pcb* udp;
    unsigned char frame[sizeof(struct NetDatagram) + NET_DATAGRAM_MAX];
    unsigned int frame_size;
    u16_t ping_sequence;
    u32_t ping_sent;
    u32_t deadline;
};

static struct Client clients[MAX_CLIENTS];
static struct Connection connections[MAX_CLIENTS + MAX_PENDING];
static struct netif interface;
static struct raw_pcb* icmp;
static unsigned int next_token = 1;
static u16_t next_ping = 1;

_Noreturn void zonix_lwip_assert(const char* message) {
    command_text(STDERR_FD, "netd: lwIP assertion: ");
    command_text(STDERR_FD, message);
    command_text(STDERR_FD, "\n");
    sys_exit(1);
    for (;;)
        sys_yield();
}

u32_t sys_now(void) {
    return (u32_t)sys_uptime_ms();
}

static unsigned int host_order(const ip4_addr_t* address) {
    return lwip_ntohl(ip4_addr_get_u32(address));
}

static void to_ip(unsigned int address, ip_addr_t* out) {
    ip_addr_set_ip4_u32(out, lwip_htonl(address));
}

static int write_all(int fd, const void* data, long size) {
    return sys_write(fd, (void*)data, size) == size;
}

static void reply(struct Client* client, int status, unsigned int address, unsigned int port, unsigned int value) {
    struct NetReply answer = {.status = status, .address = address, .port = (unsigned short)port, .value = value};
    write_all(client->fd, &answer, sizeof(answer));
}

static int lwip_error(err_t error) {
    switch (error) {
        case ERR_RST: return NET_ERROR_REFUSED;
        case ERR_USE: return NET_ERROR_IN_USE;
        case ERR_MEM:
        case ERR_BUF: return NET_ERROR_RESOURCES;
        case ERR_RTE: return NET_ERROR_UNREACHABLE;
        case ERR_TIMEOUT: return NET_ERROR_TIMEOUT;
        case ERR_ARG:
        case ERR_VAL: return NET_ERROR_INVALID;
        default: return NET_ERROR_UNREACHABLE;
    }
}

static struct Connection* new_connection(struct tcp_pcb* pcb, struct Client* owner) {
    for (int i = 0; i < MAX_CLIENTS + MAX_PENDING; ++i) {
        struct Connection* connection = &connections[i];
        if (!connection->pcb && !connection->owner) {
            memset(connection, 0, sizeof(*connection));
            connection->pcb = pcb;
            connection->owner = owner;
            return connection;
        }
    }
    return 0;
}

static void detach_pcb(struct tcp_pcb* pcb) {
    tcp_arg(pcb, 0);
    tcp_recv(pcb, 0);
    tcp_err(pcb, 0);
}

static void free_connection(struct Connection* connection, int graceful) {
    if (connection->pcb) {
        detach_pcb(connection->pcb);
        if (!graceful || tcp_close(connection->pcb) != ERR_OK)
            tcp_abort(connection->pcb);
    }
    if (connection->inbound)
        pbuf_free(connection->inbound);
    memset(connection, 0, sizeof(*connection));
}

static void free_client(struct Client* client) {
    if (client->connection)
        free_connection(client->connection, 1);
    if (client->listener) {
        /* A listening pcb takes only an argument and an accept callback. */
        tcp_arg(client->listener, 0);
        tcp_accept(client->listener, 0);
        tcp_close(client->listener);
        for (int i = 0; i < MAX_CLIENTS + MAX_PENDING; ++i)
            if (connections[i].owner == client && !connections[i].claimed)
                free_connection(&connections[i], 0);
    }
    if (client->udp)
        udp_remove(client->udp);
    sys_close(client->fd);
    unsigned int generation = client->generation + 1;
    memset(client, 0, sizeof(*client));
    client->generation = generation;
    client->fd = -1;
}

/* ---- lwIP callbacks ---- */

static err_t stream_received(void* arg, struct tcp_pcb* pcb, struct pbuf* packet, err_t error) {
    (void)pcb;
    struct Connection* connection = arg;
    if (error != ERR_OK)
        return error;
    if (!packet) {
        connection->peer_eof = 1;
        return ERR_OK;
    }
    /* Retained bytes count against the TCP receive window until delivered. */
    if (connection->inbound)
        pbuf_cat(connection->inbound, packet);
    else
        connection->inbound = packet;
    return ERR_OK;
}

static void stream_failed(void* arg, err_t error) {
    struct Connection* connection = arg;
    connection->pcb = 0; /* lwIP has freed it */
    struct Client* client = connection->owner;
    if (!connection->claimed) {
        free_connection(connection, 0);
    } else if (client->state == CONNECTING) {
        reply(client, lwip_error(error), 0, 0, 0);
        free_client(client);
    } else {
        /* Deliver what arrived, then end the stream. */
        connection->peer_eof = 1;
    }
}

static err_t stream_connected(void* arg, struct tcp_pcb* pcb, err_t error) {
    struct Connection* connection = arg;
    struct Client* client = connection->owner;
    if (error != ERR_OK)
        return error;
    tcp_nagle_disable(pcb);
    reply(client, 0, host_order(ip_2_ip4(&pcb->local_ip)), pcb->local_port, 0);
    client->state = STREAM;
    return ERR_OK;
}

static err_t stream_accepted(void* arg, struct tcp_pcb* pcb, err_t error) {
    struct Client* listener = arg;
    if (!pcb || error != ERR_OK)
        return ERR_VAL;
    struct Connection* connection = new_connection(pcb, listener);
    if (!connection) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    connection->token = next_token++;
    tcp_nagle_disable(pcb);
    tcp_arg(pcb, connection);
    tcp_recv(pcb, stream_received);
    tcp_err(pcb, stream_failed);
    struct NetReply notice = {.address = host_order(ip_2_ip4(&pcb->remote_ip)),
                              .port = pcb->remote_port,
                              .value = connection->token};
    if (!write_all(listener->fd, &notice, sizeof(notice))) {
        free_connection(connection, 0);
        return ERR_ABRT;
    }
    return ERR_OK;
}

static void udp_received(void* arg, struct udp_pcb* pcb, struct pbuf* packet, const ip_addr_t* address, u16_t port) {
    (void)pcb;
    struct Client* client = arg;
    unsigned char frame[sizeof(struct NetDatagram) + NET_DATAGRAM_MAX];
    u16_t length = packet->tot_len < NET_DATAGRAM_MAX ? packet->tot_len : NET_DATAGRAM_MAX;
    struct NetDatagram header = {.address = host_order(ip_2_ip4(address)), .port = port, .length = length};
    memcpy(frame, &header, sizeof(header));
    pbuf_copy_partial(packet, frame + sizeof(header), length, 0);
    /* A datagram that does not fit in the channel is dropped, as on a full socket. */
    write_all(client->fd, frame, (long)(sizeof(header) + length));
    pbuf_free(packet);
}

static void name_found(const char* name, const ip_addr_t* address, void* arg) {
    (void)name;
    unsigned long cookie = (unsigned long)arg;
    struct Client* client = &clients[cookie % MAX_CLIENTS];
    if (client->state != RESOLVING || client->generation != cookie / MAX_CLIENTS)
        return;
    if (address)
        reply(client, 0, host_order(ip_2_ip4(address)), 0, 0);
    else
        reply(client, NET_ERROR_NOT_FOUND, 0, 0, 0);
    free_client(client);
}

static u8_t icmp_received(void* arg, struct raw_pcb* pcb, struct pbuf* packet, const ip_addr_t* address) {
    (void)arg;
    (void)pcb;
    (void)address;
    struct icmp_echo_hdr echo;
    u8_t version_length;
    if (pbuf_copy_partial(packet, &version_length, 1, 0) != 1)
        return 0;
    u16_t header = (u16_t)((version_length & 0x0F) * 4);
    if (pbuf_copy_partial(packet, &echo, sizeof(echo), header) != sizeof(echo) || echo.type != ICMP_ER ||
        lwip_ntohs(echo.id) != PING_ID)
        return 0;
    u16_t sequence = lwip_ntohs(echo.seqno);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        struct Client* client = &clients[i];
        if (client->state == PINGING && client->ping_sequence == sequence) {
            reply(client, 0, 0, 0, sys_now() - client->ping_sent);
            free_client(client);
        }
    }
    pbuf_free(packet);
    return 1;
}

/* ---- requests ---- */

static void start_ping(struct Client* client, const struct NetRequest* request) {
    struct pbuf* packet = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr) + PING_PAYLOAD_BYTES, PBUF_RAM);
    if (!packet) {
        reply(client, NET_ERROR_RESOURCES, 0, 0, 0);
        free_client(client);
        return;
    }
    struct icmp_echo_hdr* echo = packet->payload;
    ICMPH_TYPE_SET(echo, ICMP_ECHO);
    ICMPH_CODE_SET(echo, 0);
    echo->id = lwip_htons(PING_ID);
    client->ping_sequence = next_ping++;
    echo->seqno = lwip_htons(client->ping_sequence);
    memset((unsigned char*)echo + sizeof(*echo), 'z', PING_PAYLOAD_BYTES);
    echo->chksum = 0;
    echo->chksum = inet_chksum(echo, packet->len);
    ip_addr_t target;
    to_ip(request->address, &target);
    client->ping_sent = sys_now();
    client->deadline = client->ping_sent + request->value;
    err_t error = raw_sendto(icmp, packet, &target);
    pbuf_free(packet);
    if (error != ERR_OK) {
        reply(client, lwip_error(error), 0, 0, 0);
        free_client(client);
        return;
    }
    client->state = PINGING;
}

static void handle_request(struct Client* client) {
    struct NetRequest* request = (struct NetRequest*)client->request;
    request->name[NET_NAME_MAX] = 0;
    ip_addr_t address;
    switch (request->op) {
        case NET_OP_TCP_CONNECT:
        case NET_OP_TCP_LISTEN: {
            struct tcp_pcb* pcb = tcp_new();
            if (!pcb)
                break;
            if (request->op == NET_OP_TCP_CONNECT) {
                struct Connection* connection = new_connection(pcb, client);
                if (!connection) {
                    tcp_abort(pcb);
                    break;
                }
                connection->claimed = 1;
                client->connection = connection;
                tcp_arg(pcb, connection);
                tcp_recv(pcb, stream_received);
                tcp_err(pcb, stream_failed);
                to_ip(request->address, &address);
                err_t error = tcp_connect(pcb, &address, request->port, stream_connected);
                if (error != ERR_OK) {
                    reply(client, lwip_error(error), 0, 0, 0);
                    free_client(client);
                    return;
                }
                client->state = CONNECTING;
                return;
            }
            err_t error = tcp_bind(pcb, IP_ADDR_ANY, request->port);
            struct tcp_pcb* listener = error == ERR_OK ? tcp_listen(pcb) : 0;
            if (!listener) {
                tcp_close(pcb);
                reply(client, error == ERR_OK ? NET_ERROR_RESOURCES : lwip_error(error), 0, 0, 0);
                free_client(client);
                return;
            }
            client->listener = listener;
            tcp_arg(listener, client);
            tcp_accept(listener, stream_accepted);
            reply(client, 0, 0, request->port, 0);
            client->state = LISTENING;
            return;
        }
        case NET_OP_TCP_CLAIM:
            for (int i = 0; i < MAX_CLIENTS + MAX_PENDING; ++i) {
                struct Connection* connection = &connections[i];
                if (connection->owner && !connection->claimed && connection->token == request->value) {
                    connection->claimed = 1;
                    connection->owner = client;
                    client->connection = connection;
                    struct tcp_pcb* pcb = connection->pcb;
                    reply(client, 0, pcb ? host_order(ip_2_ip4(&pcb->remote_ip)) : 0, pcb ? pcb->remote_port : 0, 0);
                    client->state = STREAM;
                    return;
                }
            }
            reply(client, NET_ERROR_INVALID, 0, 0, 0);
            free_client(client);
            return;
        case NET_OP_UDP_OPEN: {
            struct udp_pcb* pcb = udp_new();
            if (!pcb)
                break;
            err_t error = udp_bind(pcb, IP_ADDR_ANY, request->port);
            if (error != ERR_OK) {
                udp_remove(pcb);
                reply(client, lwip_error(error), 0, 0, 0);
                free_client(client);
                return;
            }
            client->udp = pcb;
            udp_recv(pcb, udp_received, client);
            reply(client, 0, 0, pcb->local_port, 0);
            client->state = DATAGRAM;
            return;
        }
        case NET_OP_RESOLVE: {
            if (ip_addr_isany(dns_getserver(0))) {
                reply(client, NET_ERROR_NO_ADDRESS, 0, 0, 0);
                free_client(client);
                return;
            }
            void* cookie = (void*)(unsigned long)(client->generation * MAX_CLIENTS + (unsigned long)(client - clients));
            err_t error = dns_gethostbyname(request->name, &address, name_found, cookie);
            if (error == ERR_INPROGRESS) {
                client->state = RESOLVING;
                return;
            }
            reply(client, error == ERR_OK ? 0 : NET_ERROR_NOT_FOUND, error == ERR_OK ? host_order(ip_2_ip4(&address)) : 0,
                  0, 0);
            free_client(client);
            return;
        }
        case NET_OP_PING: start_ping(client, request); return;
        case NET_OP_INFO: {
            struct NetReply answer = {.status = NET_ERROR_NO_ADDRESS};
            if (!ip4_addr_isany_val(*netif_ip4_addr(&interface))) {
                answer.status = 0;
                answer.address = host_order(netif_ip4_addr(&interface));
                answer.value = host_order(netif_ip4_netmask(&interface));
                answer.gateway = host_order(netif_ip4_gw(&interface));
                answer.dns = host_order(ip_2_ip4(dns_getserver(0)));
            }
            write_all(client->fd, &answer, sizeof(answer));
            free_client(client);
            return;
        }
        default:
            reply(client, NET_ERROR_INVALID, 0, 0, 0);
            free_client(client);
            return;
    }
    reply(client, NET_ERROR_RESOURCES, 0, 0, 0);
    free_client(client);
}

/* ---- channel I/O ---- */

static void read_request(struct Client* client) {
    long count = sys_read(client->fd, client->request + client->request_size,
                          (long)(sizeof(client->request) - client->request_size));
    if (count == 0) {
        free_client(client);
        return;
    }
    if (count < 0)
        return;
    client->request_size += (unsigned int)count;
    if (client->request_size == sizeof(client->request))
        handle_request(client);
}

static void read_datagrams(struct Client* client) {
    for (;;) {
        unsigned int need = sizeof(struct NetDatagram);
        if (client->frame_size >= need)
            need += ((struct NetDatagram*)client->frame)->length;
        if (need > sizeof(client->frame)) {
            free_client(client);
            return;
        }
        long count = sys_read(client->fd, client->frame + client->frame_size, (long)(need - client->frame_size));
        if (count == 0) {
            free_client(client);
            return;
        }
        if (count < 0)
            return;
        client->frame_size += (unsigned int)count;
        struct NetDatagram* header = (struct NetDatagram*)client->frame;
        if (client->frame_size < sizeof(*header) || client->frame_size < sizeof(*header) + header->length)
            continue;
        struct pbuf* packet = pbuf_alloc(PBUF_TRANSPORT, header->length, PBUF_RAM);
        if (packet) {
            ip_addr_t target;
            to_ip(header->address, &target);
            pbuf_take(packet, client->frame + sizeof(*header), header->length);
            udp_sendto(client->udp, packet, &target, header->port);
            pbuf_free(packet);
        }
        client->frame_size = 0;
    }
}

/* Reads a closed or misbehaving client: end of file frees it, bytes are ignored. */
static void watch_client(struct Client* client) {
    unsigned char ignored[64];
    if (sys_read(client->fd, ignored, sizeof(ignored)) == 0)
        free_client(client);
}

/* Moves bytes between the channel and the TCP connection in both directions. */
static void pump_stream(struct Client* client) {
    struct Connection* connection = client->connection;
    unsigned char bytes[TCP_MSS];
    while (connection->inbound && !connection->client_gone) {
        u16_t count = pbuf_copy_partial(connection->inbound, bytes, sizeof(bytes), 0);
        /* A nonblocking channel write is all or nothing; shrink it until it fits. */
        long written;
        while ((written = sys_write(client->fd, bytes, count)) < 0 && count > 1)
            count /= 2;
        if (written <= 0) {
            /* One byte failing while the channel reports space means the reader is gone. */
            if (sys_wait_io(0, 1L << client->fd, 0) > 0)
                connection->client_gone = 1;
            break;
        }
        connection->inbound = pbuf_free_header(connection->inbound, (u16_t)written);
        if (connection->pcb)
            tcp_recved(connection->pcb, (u16_t)written);
    }
    if (connection->client_gone && connection->inbound) {
        pbuf_free(connection->inbound);
        connection->inbound = 0;
    }
    if (connection->peer_eof && !connection->inbound && !connection->peer_eof_sent) {
        sys_ioctl(client->fd, CHANNEL_SHUTDOWN_WRITE, 0);
        connection->peer_eof_sent = 1;
    }

    while (connection->pcb && !connection->local_eof) {
        if (connection->staged == 0) {
            long room = tcp_sndbuf(connection->pcb);
            if (room > STAGING_BYTES)
                room = STAGING_BYTES;
            if (room == 0)
                break;
            long count = sys_read(client->fd, connection->staging, room);
            if (count == 0) {
                connection->local_eof = 1;
                tcp_shutdown(connection->pcb, 0, 1);
                break;
            }
            if (count < 0)
                break;
            connection->staged = (u16_t)count;
        }
        err_t error = tcp_write(connection->pcb, connection->staging, connection->staged, TCP_WRITE_FLAG_COPY);
        if (error != ERR_OK) {
            if (error != ERR_MEM) {
                tcp_abort(connection->pcb);
                connection->pcb = 0;
                connection->peer_eof = 1;
            }
            break;
        }
        connection->staged = 0;
    }
    if (connection->pcb)
        tcp_output(connection->pcb);

    int incoming_done = connection->peer_eof_sent || connection->client_gone;
    int outgoing_done = connection->local_eof || !connection->pcb;
    if (incoming_done && outgoing_done && !connection->inbound)
        free_client(client);
}

/* ---- interface ---- */

static err_t transmit_frame(struct netif* netif, struct pbuf* packet) {
    unsigned char frame[NET_FRAME_MAX];
    if (packet->tot_len > sizeof(frame) || pbuf_copy_partial(packet, frame, packet->tot_len, 0) != packet->tot_len)
        return ERR_BUF;
    int length = packet->tot_len;
    if (length < ETHERNET_MIN_TX_BYTES) {
        memset(frame + length, 0, ETHERNET_MIN_TX_BYTES - length);
        length = ETHERNET_MIN_TX_BYTES;
    }
    int fd = *(int*)netif->state;
    return sys_write(fd, frame, length) == length ? ERR_OK : ERR_IF;
}

static err_t ethernet_init(struct netif* netif) {
    int fd = *(int*)netif->state;
    if (sys_ioctl(fd, NET_GET_MAC, (long)netif->hwaddr) != 0)
        return ERR_IF;
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->hwaddr_len = NET_MAC_SIZE;
    netif->mtu = NET_FRAME_MAX - NET_FRAME_MIN;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    netif->output = etharp_output;
    netif->linkoutput = transmit_frame;
    return ERR_OK;
}

static void receive_frames(int fd) {
    unsigned char frame[NET_FRAME_MAX];
    for (int i = 0; i < FRAMES_PER_WAKE; ++i) {
        if (i != 0 && sys_wait_io(1L << fd, 0, 0) <= 0)
            return;
        long length = sys_read(fd, frame, sizeof(frame));
        if (length < NET_FRAME_MIN)
            return;
        struct pbuf* packet = pbuf_alloc(PBUF_RAW, (u16_t)length, PBUF_POOL);
        if (!packet)
            return;
        if (pbuf_take(packet, frame, (u16_t)length) != ERR_OK || interface.input(packet, &interface) != ERR_OK)
            pbuf_free(packet);
    }
}

static void accept_clients(int service) {
    while (sys_wait_io(1L << service, 0, 0) > 0) {
        long fd = sys_channel_accept(service, O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
            return;
        struct Client* client = 0;
        for (int i = 0; i < MAX_CLIENTS && !client; ++i)
            if (clients[i].state == FREE)
                client = &clients[i];
        if (!client) {
            /* Closing the stream tells the client that netd is out of sockets. */
            sys_close((int)fd);
            continue;
        }
        client->fd = (int)fd;
        client->state = REQUEST;
    }
}

static void expire_pings(void) {
    u32_t now = sys_now();
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        struct Client* client = &clients[i];
        if (client->state == PINGING && (s32_t)(now - client->deadline) >= 0) {
            reply(client, NET_ERROR_TIMEOUT, 0, 0, 0);
            free_client(client);
        }
    }
}

static void announce(void) {
    char address[16];
    char dns[16];
    net_format_ipv4(host_order(netif_ip4_addr(&interface)), address);
    net_format_ipv4(host_order(ip_2_ip4(dns_getserver(0))), dns);
    printf("netd: address %s, DNS %s\n", address, dns);
}

static void serve(int network, int service, int dynamic) {
    int announced = !dynamic;
    if (!dynamic)
        announce();
    for (;;) {
        long reading = (1L << network) | (1L << service);
        long writing = 0;
        long timeout = MAX_WAIT_MS;
        u32_t now = sys_now();
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            struct Client* client = &clients[i];
            if (client->state == FREE)
                continue;
            if (client->state == STREAM) {
                struct Connection* connection = client->connection;
                if (connection->pcb && !connection->local_eof && connection->staged == 0 &&
                    tcp_sndbuf(connection->pcb) > 0)
                    reading |= 1L << client->fd;
                if (connection->inbound && !connection->client_gone)
                    writing |= 1L << client->fd;
            } else {
                reading |= 1L << client->fd;
            }
            if (client->state == PINGING) {
                long left = (s32_t)(client->deadline - now);
                if (left < timeout)
                    timeout = left < 0 ? 0 : left;
            }
        }
        u32_t sleep = sys_timeouts_sleeptime();
        if (sleep < (u32_t)timeout)
            timeout = (long)sleep;
        long ready = sys_wait_io(reading, writing, timeout);
        if (ready < 0)
            return;
        if ((ready & (1L << network)) != 0)
            receive_frames(network);
        if ((ready & (1L << service)) != 0)
            accept_clients(service);
        for (int i = 0; i < MAX_CLIENTS; ++i) {
            struct Client* client = &clients[i];
            int readable = client->state != FREE && (ready & (1L << client->fd)) != 0;
            switch (client->state) {
                case REQUEST:
                    if (readable)
                        read_request(client);
                    break;
                case STREAM: pump_stream(client); break;
                case DATAGRAM:
                    if (readable)
                        read_datagrams(client);
                    break;
                case CONNECTING:
                case LISTENING:
                case RESOLVING:
                case PINGING:
                    if (readable)
                        watch_client(client);
                    break;
                case FREE: break;
            }
        }
        sys_check_timeouts();
        expire_pings();
        if (!announced && dhcp_supplied_address(&interface)) {
            announce();
            announced = 1;
        }
    }
}

/* "ADDRESS/PREFIX", for example 192.168.1.20/24. */
static int parse_network(char* text, unsigned int* address, unsigned int* netmask) {
    char* slash = text;
    while (*slash && *slash != '/')
        ++slash;
    if (*slash == 0)
        return 0;
    *slash = 0;
    int ok = net_parse_ipv4(text, address) == 0;
    *slash = '/';
    unsigned int prefix = 0;
    int digits = 0;
    for (char* digit = slash + 1; *digit; ++digit, ++digits) {
        if (*digit < '0' || *digit > '9' || digits == 2)
            return 0;
        prefix = prefix * 10 + (unsigned int)(*digit - '0');
    }
    if (digits == 0 || prefix == 0 || prefix > 32)
        return 0;
    *netmask = prefix == 32 ? 0xFFFFFFFFU : ~(0xFFFFFFFFU >> prefix);
    return ok;
}

static int usage(void) {
    return command_usage("Usage: netd [-a ADDRESS/PREFIX [-g GATEWAY] [-d DNS]]\n"
                         "Without -a the address, gateway and DNS server come from DHCP.\n");
}

int main(int argc, char** argv) {
    unsigned int address = 0, netmask = 0, gateway = 0, dns = 0;
    int dynamic = 1;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc)
            return usage();
        if (strcmp(argv[i], "-a") == 0 && parse_network(argv[i + 1], &address, &netmask))
            dynamic = 0;
        else if ((strcmp(argv[i], "-g") != 0 || net_parse_ipv4(argv[i + 1], &gateway) != 0) &&
                 (strcmp(argv[i], "-d") != 0 || net_parse_ipv4(argv[i + 1], &dns) != 0))
            return usage();
    }
    if (dynamic && (gateway != 0 || dns != 0))
        return usage();

    int network = sys_open("/dev/net0", O_RDWR | O_CLOEXEC);
    if (network < 0) {
        command_text(STDERR_FD, "netd: cannot open /dev/net0\n");
        return 1;
    }
    long service = sys_channel_listen(NETD_CHANNEL, O_CLOEXEC | O_NONBLOCK);
    if (service < 0) {
        command_text(STDERR_FD, service == SYS_ERROR_EXISTS ? "netd: already running\n"
                                                            : "netd: cannot register the network service\n");
        return 1;
    }
    for (int i = 0; i < MAX_CLIENTS; ++i)
        clients[i].fd = -1;

    lwip_init();
    ip4_addr_t ip, mask, gw;
    ip4_addr_set_u32(&ip, lwip_htonl(address));
    ip4_addr_set_u32(&mask, lwip_htonl(netmask));
    ip4_addr_set_u32(&gw, lwip_htonl(gateway));
    if (!netif_add(&interface, &ip, &mask, &gw, &network, ethernet_init, ethernet_input)) {
        command_text(STDERR_FD, "netd: cannot start the interface\n");
        return 1;
    }
    netif_set_default(&interface);
    netif_set_link_up(&interface);
    netif_set_up(&interface);
    if (dns != 0) {
        ip_addr_t server;
        to_ip(dns, &server);
        dns_setserver(0, &server);
    }
    icmp = raw_new(IP_PROTO_ICMP);
    if (!icmp || (dynamic && dhcp_start(&interface) != ERR_OK)) {
        command_text(STDERR_FD, "netd: cannot start the network stack\n");
        return 1;
    }
    raw_recv(icmp, icmp_received, 0);
    serve(network, (int)service, dynamic);
    command_text(STDERR_FD, "netd: waiting for events failed\n");
    return 1;
}
