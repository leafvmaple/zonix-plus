#include "../command.h"
#include "zonix_net.h"

/* Prints the interface configuration that netd uses, or resolves a host name. */

int main(int argc, char** argv) {
    if (argc > 2)
        return command_usage("Usage: netinfo [host]\n");
    char text[16];
    if (argc == 2) {
        unsigned int address;
        int error = net_resolve(argv[1], &address);
        if (error < 0) {
            printf("netinfo: %s: %s\n", argv[1], net_error_text(error));
            return 1;
        }
        net_format_ipv4(address, text);
        printf("%s is %s\n", argv[1], text);
        return 0;
    }
    struct NetInfo info;
    int error = net_info(&info);
    if (error < 0) {
        printf("netinfo: %s\n", net_error_text(error));
        return 1;
    }
    net_format_ipv4(info.address, text);
    printf("address %s\n", text);
    net_format_ipv4(info.netmask, text);
    printf("netmask %s\n", text);
    net_format_ipv4(info.gateway, text);
    printf("gateway %s\n", text);
    net_format_ipv4(info.dns, text);
    printf("dns     %s\n", text);
    return 0;
}
