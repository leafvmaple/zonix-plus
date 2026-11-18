#include "../command.h"
#include "net.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/sys.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#define BRIDGE_BUFFER_BYTES   1024
#define ETHERNET_MIN_TX_BYTES 60
#define POLL_INTERVAL_MS      50

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
    struct Session session;
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
    struct Session* session = &service->session;
    if (session->pid != 0 || !start_child(session, service->program)) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    session->pcb = pcb;
    tcp_arg(pcb, session);
    tcp_recv(pcb, receive_tcp);
    tcp_err(pcb, connection_failed);
    return ERR_OK;
}

static void feed_child(struct Session* session) {
    unsigned char bytes[BRIDGE_BUFFER_BYTES];
    while (session->inbound) {
        u16_t count = pbuf_copy_partial(session->inbound, bytes, sizeof(bytes), 0);
        long written = sys_write(session->to_child, bytes, count);
        if (written <= 0)
            break;
        session->inbound = pbuf_free_header(session->inbound, (u16_t)written);
        tcp_recved(session->pcb, (u16_t)written);
    }
    if (session->peer_eof && !session->inbound)
        close_fd(&session->to_child);
}

static void collect_child(struct Session* session) {
    long count = sys_read(session->from_child, session->outbound, sizeof(session->outbound));
    if (count == 0) {
        session->child_eof = 1;
        close_fd(&session->from_child);
    } else if (count > 0) {
        session->outbound_offset = 0;
        session->outbound_size = (u16_t)count;
    }
}

static void flush_outbound(struct Session* session) {
    u16_t count = session->outbound_size - session->outbound_offset;
    u16_t available = tcp_sndbuf(session->pcb);
    if (count > available)
        count = available;
    if (count == 0)
        return;
    err_t error = tcp_write(session->pcb, session->outbound + session->outbound_offset, count, TCP_WRITE_FLAG_COPY);
    if (error == ERR_OK) {
        session->outbound_offset += count;
        tcp_output(session->pcb);
    } else if (error != ERR_MEM) {
        tcp_abort(session->pcb);
    }
}

static int output_pending(const struct Session* session) {
    return session->outbound_offset != session->outbound_size;
}

static void pump_session(struct Session* session, unsigned int ready) {
    if (session->pid == 0)
        return;
    if (session->pcb) {
        feed_child(session);
        if (session->from_child >= 0 && !output_pending(session) && (ready & (1U << session->from_child)) != 0)
            collect_child(session);
        flush_outbound(session);
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

static void receive_frame(struct netif* interface, int fd) {
    unsigned char frame[NET_FRAME_MAX];
    long length = sys_read(fd, frame, sizeof(frame));
    if (length < NET_FRAME_MIN)
        return;
    struct pbuf* packet = pbuf_alloc(PBUF_RAW, (u16_t)length, PBUF_POOL);
    if (!packet)
        return;
    if (pbuf_take(packet, frame, (u16_t)length) != ERR_OK || interface->input(packet, interface) != ERR_OK)
        pbuf_free(packet);
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

static void serve(int fd, int port, char* program) {
    struct netif interface = {0};
    ip4_addr_t address;
    ip4_addr_t prefix;
    ip4_addr_t gateway;
    IP4_ADDR(&address, 10, 0, 2, 15);
    IP4_ADDR(&prefix, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 10, 0, 2, 2);
    lwip_init();
    if (!netif_add(&interface, &address, &prefix, &gateway, &fd, ethernet_init, ethernet_input))
        return;
    netif_set_default(&interface);
    netif_set_link_up(&interface);
    netif_set_up(&interface);

    struct tcp_pcb* socket = tcp_new();
    if (!socket)
        return;
    if (tcp_bind(socket, IP_ADDR_ANY, (u16_t)port) != ERR_OK) {
        tcp_close(socket);
        return;
    }
    struct tcp_pcb* listener = tcp_listen(socket);
    if (!listener) {
        tcp_close(socket);
        return;
    }

    struct Service service = {.program = program};
    struct Session* session = &service.session;
    tcp_arg(listener, &service);
    tcp_accept(listener, accept_client);
    printf("tcpserve: listening on 10.0.2.15:%d\n", port);
    unsigned int network_mask = 1U << fd;
    for (;;) {
        unsigned int mask = network_mask;
        if (session->pid != 0 && session->from_child >= 0 && !output_pending(session))
            mask |= 1U << session->from_child;
        long ready = sys_wait_readable_for(mask, POLL_INTERVAL_MS);
        if (ready < 0)
            break;
        if (((unsigned long)ready & network_mask) != 0)
            receive_frame(&interface, fd);
        pump_session(session, (unsigned int)ready);
        sys_check_timeouts();
    }
    if (session->pcb)
        tcp_abort(session->pcb);
    if (session->pid != 0)
        finish_child(session);
    tcp_close(listener);
    netif_remove(&interface);
}

int main(int argc, char** argv) {
    int port = argc == 3 ? parse_port(argv[1]) : 0;
    if (port == 0 || argv[2][0] != '/')
        return command_usage("Usage: tcpserve <port> /absolute/worker-path\n");
    struct FileStat stat;
    if (sys_stat(argv[2], &stat) < 0 || stat.type != FILE_TYPE_FILE)
        return command_error("tcpserve", argv[2], SYS_ERROR_NOT_FOUND);
    int fd = sys_open("/dev/net0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        command_text(STDERR_FD, "tcpserve: cannot open /dev/net0\n");
        return 1;
    }
    serve(fd, port, argv[2]);
    sys_close(fd);
    command_text(STDERR_FD, "tcpserve: could not start or continue the listener\n");
    return 1;
}
