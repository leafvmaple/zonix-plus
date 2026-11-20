#include "../command.h"
#include "zonix_net.h"

/* Sends ICMP echo requests through netd. */

#define DEFAULT_COUNT 4
#define TIMEOUT_MS    2000

int main(int argc, char** argv) {
    int count = DEFAULT_COUNT;
    if (argc == 4 && strcmp(argv[1], "-c") == 0) {
        count = 0;
        for (char* digit = argv[2]; *digit >= '0' && *digit <= '9' && count < 1000; ++digit)
            count = count * 10 + (*digit - '0');
        argv += 2;
        argc -= 2;
    }
    if (argc != 2 || count <= 0)
        return command_usage("Usage: ping [-c COUNT] <host>\n");
    unsigned int address;
    int error = net_resolve(argv[1], &address);
    if (error < 0) {
        printf("ping: %s: %s\n", argv[1], net_error_text(error));
        return 1;
    }
    char text[16];
    net_format_ipv4(address, text);
    int received = 0;
    for (int i = 0; i < count; ++i) {
        int rtt = 0;
        error = net_ping(address, TIMEOUT_MS, &rtt);
        if (error == 0) {
            printf("Reply from %s: time=%d ms\n", text, rtt);
            ++received;
        } else {
            printf("No reply from %s: %s\n", text, net_error_text(error));
            if (error != NET_ERROR_TIMEOUT)
                break;
        }
    }
    return received > 0 ? 0 : 1;
}
