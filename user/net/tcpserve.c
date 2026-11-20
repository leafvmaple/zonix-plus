#include "../command.h"
#include "net.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/ip4_addr.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#define MAX_SESSIONS          4
#define BRIDGE_BUFFER_BYTES   TCP_SND_BUF
#define ETHERNET_MIN_TX_BYTES 60
#define MAX_WAIT_MS           250
#define FRAMES_PER_WAKE       32
// sys_wait_io reports write readiness in the 24 bits above the read bits.
#define WRITE_READY_SHIFT 24

struct Session {
    struct tcp_pcb* pcb;
    int pid;
    int to_child;
    int from_child;
    int peer_eof;
    int child_eof;
    struct pbuf* inbound;
    unsigned char outbound[BRIDGE_BUFFER_BYTES];
    u16_t outbound_offset;
    u16_t outbound_size;
};

struct Service {
    struct Session sessions[MAX_SESSIONS];
    char* program;
};

_Noreturn void zonix_lwip_assert(const char* message) {
    command_text(STDERR_FD, "tcpserve: lwIP assertion: ");
    command_text(STDERR_FD, message);
    command_text(STDERR_FD, "\n");
    sys_exit(1);
    for (;;)
        sys_yield();
}

u32_t sys_now(void) {
    return (u32_t)sys_uptime_ms();
}

static void close_fd(int* fd) {
    if (*fd >= 0) {
        sys_close(*fd);
        *fd = -1;
    }
}

static int start_child(struct Session* session, char* program) {
    int input[2] = {-1, -1};
    int output[2] = {-1, -1};
    int saved_input = (int)sys_fcntl(STDIN_FD, F_DUPFD_CLOEXEC, 3);
    int saved_output = (int)sys_fcntl(STDOUT_FD, F_DUPFD_CLOEXEC, 3);
    int pid = -1;
    if (saved_input >= 0 && saved_output >= 0 && sys_pipe2(input, O_CLOEXEC) == 0 &&
        sys_pipe2(output, O_CLOEXEC) == 0 && sys_fcntl(input[1], F_SETFL, O_NONBLOCK) == 0 &&
        sys_fcntl(output[0], F_SETFL, O_NONBLOCK) == 0) {
        if (sys_dup2(input[0], STDIN_FD) >= 0 && sys_dup2(output[1], STDOUT_FD) >= 0) {
            char* argv[] = {program, 0};
            pid = (int)sys_spawn(program, 1, argv);
        }
        if (sys_dup2(saved_input, STDIN_FD) < 0 || sys_dup2(saved_output, STDOUT_FD) < 0)
            zonix_lwip_assert("could not restore server descriptors");
    }
    close_fd(&saved_input);
    close_fd(&saved_output);
    close_fd(&input[0]);
    close_fd(&output[1]);
    if (pid < 0) {
        close_fd(&input[1]);
        close_fd(&output[0]);
        return 0;
    }
    session->pid = pid;
    session->to_child = input[1];
    session->from_child = output[0];
    return 1;
}

static void finish_child(struct Session* session) {
    close_fd(&session->to_child);
    close_fd(&session->from_child);
    // The child is owned by this connection. Once its pipe output is drained,
    // or the connection is lost, request exit and reap it before reuse.
    sys_kill(session->pid);
    sys_wait(session->pid, 0);
    if (session->inbound)
        pbuf_free(session->inbound);
    memset(session, 0, sizeof(*session));
    session->to_child = -1;
    session->from_child = -1;
}

static err_t receive_tcp(void* arg, struct tcp_pcb* pcb, struct pbuf* packet, err_t error) {
    (void)pcb;
    struct Session* session = arg;
    if (error != ERR_OK)
        return error;
    if (!packet) {
        session->peer_eof = 1;
        return ERR_OK;
    }
    // Retained bytes count against the TCP receive window until written.
    if (session->inbound)
        pbuf_cat(session->inbound, packet);
    else
        session->inbound = packet;
    return ERR_OK;
}

static void connection_failed(void* arg, err_t error) {
    (void)error;
    struct Session* session = arg;
    session->pcb = 0;
}

static err_t accept_client(void* arg, struct tcp_pcb* pcb, err_t error) {
    if (!pcb || error != ERR_OK)
        return error;
    struct Service* service = arg;
    struct Session* session = 0;
    for (int i = 0; i < MAX_SESSIONS && !session; ++i)
        if (service->sessions[i].pid == 0)
            session = &service->sessions[i];
    if (!session || !start_child(session, service->program)) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    session->pcb = pcb;
    // Interactive sessions send small segments; do not hold them for an ACK.
    tcp_nagle_disable(pcb);
    tcp_arg(pcb, session);
    tcp_recv(pcb, receive_tcp);
    tcp_err(pcb, connection_failed);
    return ERR_OK;
}

// Each step returns whether it moved any bytes.
static int feed_child(struct Session* session) {
    unsigned char bytes[TCP_MSS];
    int moved = 0;
    while (session->inbound && session->to_child >= 0) {
        u16_t count = pbuf_copy_partial(session->inbound, bytes, sizeof(bytes), 0);
        // A non-blocking pipe write is all or nothing, while write readiness only
        // promises one free byte: shrink the write until it fits.
        long written;
        while ((written = sys_write(session->to_child, bytes, count)) < 0 && count > 1)
            count /= 2;
        if (written <= 0)
            break;
        session->inbound = pbuf_free_header(session->inbound, (u16_t)written);
        tcp_recved(session->pcb, (u16_t)written);
        moved = 1;
    }
    if (session->peer_eof && !session->inbound)
        close_fd(&session->to_child);
    return moved;
}

static int collect_child(struct Session* session) {
    if (session->from_child < 0)
        return 0;
    if (session->outbound_offset != 0) {
        session->outbound_size -= session->outbound_offset;
        memmove(session->outbound, session->outbound + session->outbound_offset, session->outbound_size);
        session->outbound_offset = 0;
    }
    if (session->outbound_size == sizeof(session->outbound))
        return 0;
    long count = sys_read(session->from_child, session->outbound + session->outbound_size,
                          (long)(sizeof(session->outbound) - session->outbound_size));
    if (count == 0) {
        session->child_eof = 1;
        close_fd(&session->from_child);
        return 0;
    }
    if (count < 0)
        return 0;
    session->outbound_size += (u16_t)count;
    return 1;
}

static int flush_outbound(struct Session* session) {
    u16_t count = session->outbound_size - session->outbound_offset;
    u16_t available = tcp_sndbuf(session->pcb);
    if (count > available)
        count = available;
    if (count == 0)
        return 0;
    err_t error = tcp_write(session->pcb, session->outbound + session->outbound_offset, count, TCP_WRITE_FLAG_COPY);
    if (error == ERR_OK) {
        session->outbound_offset += count;
        if (session->outbound_offset == session->outbound_size)
            session->outbound_offset = session->outbound_size = 0;
        return 1;
    }
    if (error != ERR_MEM) {
        tcp_abort(session->pcb);
        session->pcb = 0;
    }
    return 0;
}

static int output_pending(const struct Session* session) {
    return session->outbound_offset != session->outbound_size;
}

static void pump_session(struct Session* session) {
    if (session->pid == 0)
        return;
    if (session->pcb) {
        // Move bytes both ways until the pipes, the TCP window or the send buffer stop it.
        int moved;
        do {
            moved = feed_child(session);
            moved |= collect_child(session);
            if (session->pcb)
                moved |= flush_outbound(session);
        } while (moved && session->pcb);
        if (session->pcb)
            tcp_output(session->pcb);
    }
    if (session->pcb && session->child_eof && !output_pending(session)) {
        struct tcp_pcb* pcb = session->pcb;
        tcp_arg(pcb, 0);
        tcp_recv(pcb, 0);
        tcp_err(pcb, 0);
        if (tcp_close(pcb) == ERR_OK) {
            session->pcb = 0;
        } else {
            tcp_arg(pcb, session);
            tcp_recv(pcb, receive_tcp);
            tcp_err(pcb, connection_failed);
        }
    }
    if (!session->pcb)
        finish_child(session);
}

static err_t transmit_frame(struct netif* interface, struct pbuf* packet) {
    unsigned char frame[NET_FRAME_MAX];
    if (packet->tot_len > sizeof(frame))
        return ERR_BUF;
    if (pbuf_copy_partial(packet, frame, packet->tot_len, 0) != packet->tot_len)
        return ERR_BUF;
    int length = packet->tot_len;
    if (length < ETHERNET_MIN_TX_BYTES) {
        memset(frame + length, 0, ETHERNET_MIN_TX_BYTES - length);
        length = ETHERNET_MIN_TX_BYTES;
    }
    int fd = *(int*)interface->state;
    return sys_write(fd, frame, length) == length ? ERR_OK : ERR_IF;
}

static err_t ethernet_init(struct netif* interface) {
    int fd = *(int*)interface->state;
    if (sys_ioctl(fd, NET_GET_MAC, (long)interface->hwaddr) != 0)
        return ERR_IF;
    interface->name[0] = 'e';
    interface->name[1] = 'n';
    interface->hwaddr_len = NET_MAC_SIZE;
    interface->mtu = NET_FRAME_MAX - NET_FRAME_MIN;
    interface->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    interface->output = etharp_output;
    interface->linkoutput = transmit_frame;
    return ERR_OK;
}

static void receive_frames(struct netif* interface, int fd) {
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
        if (pbuf_take(packet, frame, (u16_t)length) != ERR_OK || interface->input(packet, interface) != ERR_OK)
            pbuf_free(packet);
    }
}

static int parse_port(const char* text) {
    unsigned int port = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9')
            return 0;
        port = port * 10U + (unsigned int)(*text - '0');
        if (port > 65535)
            return 0;
    }
    return (int)port;
}

struct Config {
    int port;
    char* program;
    int dynamic;
    ip4_addr_t address;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
};

// "ADDRESS/PREFIX", for example 192.168.1.20/24.
static int parse_address(char* text, struct Config* config) {
    char* slash = text;
    while (*slash && *slash != '/')
        ++slash;
    if (*slash == 0 || slash[1] == 0)
        return 0;
    *slash = 0;
    int ok = ip4addr_aton(text, &config->address);
    *slash = '/';
    unsigned int prefix = 0;
    for (char* digit = slash + 1; *digit; ++digit) {
        if (*digit < '0' || *digit > '9')
            return 0;
        prefix = prefix * 10U + (unsigned int)(*digit - '0');
        if (prefix > 32)
            return 0;
    }
    u32_t mask = prefix == 0 ? 0 : 0xFFFFFFFFUL << (32 - prefix);
    ip4_addr_set_u32(&config->netmask, lwip_htonl(mask));
    return ok && prefix != 0;
}

static void announce(const struct netif* interface, int port) {
    printf("tcpserve: listening on %s:%d\n", ip4addr_ntoa(netif_ip4_addr(interface)), port);
}

static void serve(int fd, const struct Config* config) {
    struct netif interface = {0};
    lwip_init();
    if (!netif_add(&interface, &config->address, &config->netmask, &config->gateway, &fd, ethernet_init,
                   ethernet_input))
        return;
    netif_set_default(&interface);
    netif_set_link_up(&interface);
    netif_set_up(&interface);
    if (config->dynamic && dhcp_start(&interface) != ERR_OK)
        return;

    struct tcp_pcb* socket = tcp_new();
    if (!socket)
        return;
    if (tcp_bind(socket, IP_ADDR_ANY, (u16_t)config->port) != ERR_OK) {
        tcp_close(socket);
        return;
    }
    struct tcp_pcb* listener = tcp_listen(socket);
    if (!listener) {
        tcp_close(socket);
        return;
    }

    // The bridge buffers are too large for the stack.
    static struct Service service;
    service.program = config->program;
    for (int i = 0; i < MAX_SESSIONS; ++i)
        service.sessions[i].to_child = service.sessions[i].from_child = -1;
    tcp_arg(listener, &service);
    tcp_accept(listener, accept_client);
    int announced = 0;
    if (!config->dynamic) {
        announce(&interface, config->port);
        announced = 1;
    }
    long network_mask = 1L << fd;
    for (;;) {
        long reading = network_mask;
        long writing = 0;
        for (int i = 0; i < MAX_SESSIONS; ++i) {
            struct Session* session = &service.sessions[i];
            if (session->pid == 0)
                continue;
            if (session->from_child >= 0 && session->outbound_size < sizeof(session->outbound))
                reading |= 1L << session->from_child;
            if (session->to_child >= 0 && session->inbound)
                writing |= 1L << session->to_child;
        }
        u32_t sleep = sys_timeouts_sleeptime();
        long ready = sys_wait_io(reading, writing, sleep < MAX_WAIT_MS ? (long)sleep : MAX_WAIT_MS);
        if (ready < 0)
            break;
        if ((ready & network_mask) != 0)
            receive_frames(&interface, fd);
        for (int i = 0; i < MAX_SESSIONS; ++i)
            pump_session(&service.sessions[i]);
        sys_check_timeouts();
        if (!announced && dhcp_supplied_address(&interface)) {
            announce(&interface, config->port);
            announced = 1;
        }
    }
    for (int i = 0; i < MAX_SESSIONS; ++i) {
        struct Session* session = &service.sessions[i];
        if (session->pcb)
            tcp_abort(session->pcb);
        if (session->pid != 0)
            finish_child(session);
    }
    tcp_close(listener);
    if (config->dynamic)
        dhcp_stop(&interface);
    netif_remove(&interface);
}

static int usage(void) {
    return command_usage("Usage: tcpserve [-a ADDRESS/PREFIX [-g GATEWAY]] <port> /absolute/worker-path\n"
                         "Without -a the address comes from DHCP.\n");
}

int main(int argc, char** argv) {
    struct Config config = {.dynamic = 1};
    int index = 1;
    while (index + 1 < argc && argv[index][0] == '-') {
        if (strcmp(argv[index], "-a") == 0 && parse_address(argv[index + 1], &config))
            config.dynamic = 0;
        else if (strcmp(argv[index], "-g") != 0 || !ip4addr_aton(argv[index + 1], &config.gateway))
            return usage();
        index += 2;
    }
    if (argc - index != 2 || (config.dynamic && !ip4_addr_isany_val(config.gateway)))
        return usage();
    config.port = parse_port(argv[index]);
    config.program = argv[index + 1];
    if (config.port == 0 || config.program[0] != '/')
        return usage();
    struct FileStat stat;
    if (sys_stat(config.program, &stat) < 0 || stat.type != FILE_TYPE_FILE)
        return command_error("tcpserve", config.program, SYS_ERROR_NOT_FOUND);
    int fd = sys_open("/dev/net0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        command_text(STDERR_FD, "tcpserve: cannot open /dev/net0\n");
        return 1;
    }
    serve(fd, &config);
    sys_close(fd);
    command_text(STDERR_FD, "tcpserve: could not start or continue the listener\n");
    return 1;
}
