#include "../command.h"
#include "zonix_net.h"

/* Accepts TCP connections through netd and runs a worker for each one with
 * the connection as its standard input and output. */

#define MAX_SESSIONS 4
#define REAP_INTERVAL_MS 1000

static int spawn_worker(int stream, char* program) {
    int saved_input = (int)sys_fcntl(STDIN_FD, F_DUPFD_CLOEXEC, 3);
    int saved_output = (int)sys_fcntl(STDOUT_FD, F_DUPFD_CLOEXEC, 3);
    long pid = -1;
    if (saved_input >= 0 && saved_output >= 0 && sys_dup2(stream, STDIN_FD) >= 0 && sys_dup2(stream, STDOUT_FD) >= 0) {
        char* argv[] = {program, 0};
        pid = sys_spawn(program, 1, argv);
    }
    if (saved_input >= 0)
        sys_dup2(saved_input, STDIN_FD);
    if (saved_output >= 0)
        sys_dup2(saved_output, STDOUT_FD);
    if (saved_input >= 0)
        sys_close(saved_input);
    if (saved_output >= 0)
        sys_close(saved_output);
    return pid > 0 ? (int)pid : 0;
}

/* There is no nonblocking wait: reap only workers that are already zombies. */
static void reap_finished(int* workers, int* count) {
    for (int i = 0; i < *count;) {
        struct ProcessInfo info;
        int finished = sys_process_next(workers[i] - 1, &info) <= 0 || (int)info.pid != workers[i] ||
                       info.state == PROCESS_STATE_ZOMBIE;
        if (finished) {
            sys_wait(workers[i], 0);
            workers[i] = workers[--*count];
        } else {
            ++i;
        }
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

int main(int argc, char** argv) {
    int port = argc == 3 ? parse_port(argv[1]) : 0;
    if (port == 0 || argv[2][0] != '/')
        return command_usage("Usage: tcpserve <port> /absolute/worker-path\n");
    struct FileStat stat;
    if (sys_stat(argv[2], &stat) < 0 || stat.type != FILE_TYPE_FILE)
        return command_error("tcpserve", argv[2], SYS_ERROR_NOT_FOUND);
    int listener = net_tcp_listen(port);
    if (listener < 0) {
        printf("tcpserve: cannot listen on port %d: %s\n", port, net_error_text(listener));
        return 1;
    }
    printf("tcpserve: listening on port %d\n", port);
    int workers[MAX_SESSIONS];
    int count = 0;
    for (;;) {
        long ready = sys_wait_readable_for(1L << listener, REAP_INTERVAL_MS);
        reap_finished(workers, &count);
        if (ready < 0)
            break;
        if (ready == 0)
            continue;
        int stream = net_tcp_accept(listener, 0, 0);
        if (stream == NET_ERROR_IO)
            break;
        if (stream < 0)
            continue;
        int pid = count < MAX_SESSIONS ? spawn_worker(stream, argv[2]) : 0;
        if (pid > 0)
            workers[count++] = pid;
        /* The worker holds its own copies; closing ours leaves the connection to it. */
        sys_close(stream);
    }
    command_text(STDERR_FD, "tcpserve: the network service went away\n");
    return 1;
}
