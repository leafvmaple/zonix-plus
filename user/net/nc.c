#include "../command.h"
#include "netproto.h"
#include "zonix_net.h"

/* Connects standard input and output to a TCP connection or UDP socket.
 * Standard output carries only the peer's data, so that it can be redirected
 * to a file; messages go to standard error. */

static int fail(const char* what, int error) {
    command_text(STDERR_FD, "nc: ");
    command_text(STDERR_FD, what);
    command_text(STDERR_FD, ": ");
    command_text(STDERR_FD, net_error_text(error));
    command_text(STDERR_FD, "\n");
    return 1;
}

static int parse_port(const char* text) {
    int port = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || port > 6553)
            return 0;
        port = port * 10 + (*text - '0');
    }
    return port <= 65535 ? port : 0;
}

/* A terminal read waits for the whole count, so take bytes one at a time while
 * input is ready and stop after a line. */
static long read_input(char* buffer, long size) {
    struct TerminalSize terminal;
    int is_terminal = sys_ioctl(STDIN_FD, TERMINAL_GET_SIZE, (long)&terminal) == 0;
    long count = 0;
    do {
        long got = sys_read(STDIN_FD, buffer + count, 1);
        if (got <= 0)
            return count > 0 ? count : got;
        /* A terminal passes Ctrl-D through as a byte; treat it as end of input. */
        if (is_terminal && buffer[count] == 4)
            return count;
        ++count;
    } while (count < size && buffer[count - 1] != '\n' && sys_wait_readable_for(1L << STDIN_FD, 0) > 0);
    return count;
}

/* Copies stdin to the stream and the stream to stdout until the peer closes. */
static int bridge_stream(int stream) {
    char buffer[1024];
    int input_open = 1;
    /* A regular file cannot be waited on: send all of it first, then end the direction. */
    if (sys_wait_readable_for(1L << STDIN_FD, 0) < 0) {
        long count;
        while ((count = sys_read(STDIN_FD, buffer, sizeof(buffer))) > 0)
            if (command_write_all(stream, buffer, count) < 0)
                return 1;
        net_shutdown_write(stream);
        input_open = 0;
    }
    for (;;) {
        long ready = sys_wait_readable((input_open ? 1L << STDIN_FD : 0) | 1L << stream);
        if (ready < 0)
            return 1;
        if ((ready & (1L << stream)) != 0) {
            long count = sys_read(stream, buffer, sizeof(buffer));
            if (count <= 0)
                return count == 0 ? 0 : 1;
            if (command_write_all(STDOUT_FD, buffer, count) < 0)
                return 1;
        }
        if (input_open && (ready & (1L << STDIN_FD)) != 0) {
            long count = read_input(buffer, sizeof(buffer));
            if (count <= 0) {
                input_open = 0;
                net_shutdown_write(stream);
            } else if (command_write_all(stream, buffer, count) < 0) {
                return 1;
            }
        }
    }
}

/* Sends each line of stdin as a datagram and prints datagrams as they arrive.
 * Without a fixed peer, replies go to whoever sent the last datagram. */
static int bridge_datagrams(int socket, unsigned int peer, int peer_port) {
    char buffer[NET_DATAGRAM_MAX];
    char line[NET_DATAGRAM_MAX];
    long line_size = 0;
    int input_open = 1;
    for (;;) {
        long ready = sys_wait_readable((input_open ? 1L << STDIN_FD : 0) | 1L << socket);
        if (ready < 0)
            return 1;
        if ((ready & (1L << socket)) != 0) {
            unsigned int from;
            int from_port;
            long count = net_udp_receive(socket, &from, &from_port, buffer, sizeof(buffer));
            if (count < 0)
                return 1;
            if (peer_port == 0 || peer == 0) {
                peer = from;
                peer_port = from_port;
            }
            command_write_all(STDOUT_FD, buffer, count);
        }
        if (input_open && (ready & (1L << STDIN_FD)) != 0) {
            /* Keys arrive one at a time; a datagram carries a whole line. */
            long count = read_input(line + line_size, (long)sizeof(line) - line_size);
            if (count <= 0) {
                input_open = 0;
                if (peer_port == 0)
                    return 0;
                continue;
            }
            line_size += count;
            if (line[line_size - 1] != '\n' && line_size != (long)sizeof(line))
                continue;
            if (peer_port != 0 && net_udp_send(socket, peer, peer_port, line, line_size) < 0)
                return 1;
            line_size = 0;
        }
    }
}

int main(int argc, char** argv) {
    int listen = 0;
    int udp = 0;
    int index = 1;
    for (; index < argc && argv[index][0] == '-'; ++index) {
        if (strcmp(argv[index], "-l") == 0)
            listen = 1;
        else if (strcmp(argv[index], "-u") == 0)
            udp = 1;
        else
            break;
    }
    int arguments = argc - index;
    if ((listen && arguments != 1) || (!listen && arguments != 2))
        return command_usage("Usage: nc [-u] <host> <port>\n       nc -l [-u] <port>\n");
    int port = parse_port(argv[argc - 1]);
    if (port == 0)
        return command_usage("nc: invalid port\n");

    if (listen) {
        if (udp) {
            int socket = net_udp_open(port);
            return socket < 0 ? fail("listen", socket) : bridge_datagrams(socket, 0, 0);
        }
        int listener = net_tcp_listen(port);
        if (listener < 0)
            return fail("listen", listener);
        unsigned int peer;
        int peer_port;
        int stream = net_tcp_accept(listener, &peer, &peer_port);
        sys_close(listener);
        if (stream < 0)
            return fail("accept", stream);
        char text[16];
        net_format_ipv4(peer, text);
        char port[6];
        int length = 0;
        for (int rest = peer_port; length == 0 || rest > 0; rest /= 10)
            ++length;
        port[length] = 0;
        for (int rest = peer_port, at = length; at > 0; rest /= 10)
            port[--at] = (char)('0' + rest % 10);
        command_text(STDERR_FD, "nc: connection from ");
        command_text(STDERR_FD, text);
        command_text(STDERR_FD, ":");
        command_text(STDERR_FD, port);
        command_text(STDERR_FD, "\n");
        return bridge_stream(stream);
    }

    unsigned int address;
    int error = net_resolve(argv[index], &address);
    if (error < 0)
        return fail(argv[index], error);
    if (udp) {
        int socket = net_udp_open(0);
        return socket < 0 ? fail("socket", socket) : bridge_datagrams(socket, address, port);
    }
    int stream = net_tcp_connect(address, port);
    return stream < 0 ? fail(argv[index], stream) : bridge_stream(stream);
}
