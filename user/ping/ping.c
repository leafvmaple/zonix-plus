#include "../command.h"
#include "net.h"

#define IPV4_BYTES        4
#define IPV4_HEADER_BYTES 20
#define ICMP_HEADER_BYTES 8
#define ETH_MIN_TX_BYTES  60
#define ETH_TYPE_ARP      0x0806
#define ETH_TYPE_IPV4     0x0800
#define ARP_REQUEST       1
#define ARP_REPLY         2
#define ICMP_PROTOCOL     1
#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY   0
#define MAX_RX_FRAMES     16

struct EthernetFrame {
    unsigned char destination[NET_MAC_SIZE];
    unsigned char source[NET_MAC_SIZE];
    unsigned char type[2];
    unsigned char payload[NET_FRAME_MAX - NET_FRAME_MIN];
};

struct ArpPacket {
    unsigned char hardware_type[2];
    unsigned char protocol_type[2];
    unsigned char hardware_size;
    unsigned char protocol_size;
    unsigned char operation[2];
    unsigned char sender_mac[NET_MAC_SIZE];
    unsigned char sender_ip[IPV4_BYTES];
    unsigned char target_mac[NET_MAC_SIZE];
    unsigned char target_ip[IPV4_BYTES];
};

_Static_assert(sizeof(struct EthernetFrame) == NET_FRAME_MAX, "Ethernet frame layout");
_Static_assert(sizeof(struct ArpPacket) == 28, "Ethernet IPv4 ARP layout");

static const unsigned char LOCAL_IP[IPV4_BYTES] = {10, 0, 2, 15};
static const unsigned char BROADCAST[NET_MAC_SIZE] = {255, 255, 255, 255, 255, 255};
static const unsigned char ECHO_DATA[] = "Zonix ping";

static unsigned int read_be16(const unsigned char* bytes) {
    return ((unsigned int)bytes[0] << 8) | bytes[1];
}

static void write_be16(unsigned char* bytes, unsigned int value) {
    bytes[0] = (unsigned char)(value >> 8);
    bytes[1] = (unsigned char)value;
}

static unsigned int checksum(const unsigned char* bytes, int length) {
    unsigned int sum = 0;
    while (length >= 2) {
        sum += read_be16(bytes);
        bytes += 2;
        length -= 2;
    }
    if (length)
        sum += (unsigned int)bytes[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xffffU) + (sum >> 16);
    return ~sum & 0xffffU;
}

static int parse_ipv4(const char* text, unsigned char* address) {
    for (int index = 0; index < IPV4_BYTES; ++index) {
        if (*text < '0' || *text > '9')
            return 0;
        unsigned int value = 0;
        int digits = 0;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (unsigned int)(*text++ - '0');
            if (++digits > 3 || value > 255)
                return 0;
        }
        address[index] = (unsigned char)value;
        if (index < IPV4_BYTES - 1 && *text++ != '.')
            return 0;
    }
    return *text == '\0';
}

static void ethernet_header(struct EthernetFrame* frame, const unsigned char* destination, const unsigned char* source,
                            unsigned int type) {
    memcpy(frame->destination, destination, NET_MAC_SIZE);
    memcpy(frame->source, source, NET_MAC_SIZE);
    write_be16(frame->type, type);
}

static int send_arp(int fd, const unsigned char* local_mac, const unsigned char* target_ip,
                    const unsigned char* target_mac, unsigned int operation) {
    struct EthernetFrame frame = {0};
    struct ArpPacket arp = {0};
    ethernet_header(&frame, target_mac ? target_mac : BROADCAST, local_mac, ETH_TYPE_ARP);
    write_be16(arp.hardware_type, 1);
    write_be16(arp.protocol_type, ETH_TYPE_IPV4);
    arp.hardware_size = NET_MAC_SIZE;
    arp.protocol_size = IPV4_BYTES;
    write_be16(arp.operation, operation);
    memcpy(arp.sender_mac, local_mac, NET_MAC_SIZE);
    memcpy(arp.sender_ip, LOCAL_IP, IPV4_BYTES);
    if (target_mac)
        memcpy(arp.target_mac, target_mac, NET_MAC_SIZE);
    memcpy(arp.target_ip, target_ip, IPV4_BYTES);
    memcpy(frame.payload, &arp, sizeof(arp));
    return sys_write(fd, &frame, ETH_MIN_TX_BYTES) == ETH_MIN_TX_BYTES;
}

static int parse_arp(const struct EthernetFrame* frame, long length, struct ArpPacket* arp) {
    if (length < NET_FRAME_MIN + (long)sizeof(*arp) || read_be16(frame->type) != ETH_TYPE_ARP)
        return 0;
    memcpy(arp, frame->payload, sizeof(*arp));
    return read_be16(arp->hardware_type) == 1 && read_be16(arp->protocol_type) == ETH_TYPE_IPV4 &&
           arp->hardware_size == NET_MAC_SIZE && arp->protocol_size == IPV4_BYTES && (arp->sender_mac[0] & 1) == 0 &&
           memcmp(frame->source, arp->sender_mac, NET_MAC_SIZE) == 0;
}

static void answer_arp(int fd, const unsigned char* local_mac, const struct ArpPacket* arp) {
    if (read_be16(arp->operation) == ARP_REQUEST && memcmp(arp->target_ip, LOCAL_IP, IPV4_BYTES) == 0)
        send_arp(fd, local_mac, arp->sender_ip, arp->sender_mac, ARP_REPLY);
}

static int resolve_peer(int fd, const unsigned char* local_mac, const unsigned char* peer_ip, unsigned char* peer_mac) {
    if (!send_arp(fd, local_mac, peer_ip, 0, ARP_REQUEST))
        return 0;
    struct EthernetFrame frame;
    for (int i = 0; i < MAX_RX_FRAMES; ++i) {
        long length = sys_read(fd, &frame, sizeof(frame));
        if (length < 0)
            return 0;
        struct ArpPacket arp;
        if (!parse_arp(&frame, length, &arp))
            continue;
        if (read_be16(arp.operation) == ARP_REPLY && memcmp(arp.sender_ip, peer_ip, IPV4_BYTES) == 0 &&
            memcmp(arp.target_ip, LOCAL_IP, IPV4_BYTES) == 0 && memcmp(arp.target_mac, local_mac, NET_MAC_SIZE) == 0 &&
            memcmp(frame.destination, local_mac, NET_MAC_SIZE) == 0) {
            memcpy(peer_mac, arp.sender_mac, NET_MAC_SIZE);
            return 1;
        }
        answer_arp(fd, local_mac, &arp);
    }
    return 0;
}

static int send_echo(int fd, const unsigned char* local_mac, const unsigned char* peer_mac,
                     const unsigned char* peer_ip, unsigned int identifier) {
    struct EthernetFrame frame = {0};
    ethernet_header(&frame, peer_mac, local_mac, ETH_TYPE_IPV4);
    unsigned char* ip = frame.payload;
    unsigned char* icmp = ip + IPV4_HEADER_BYTES;
    int icmp_bytes = ICMP_HEADER_BYTES + (int)sizeof(ECHO_DATA);
    int ip_bytes = IPV4_HEADER_BYTES + icmp_bytes;
    ip[0] = 0x45;
    write_be16(ip + 2, (unsigned int)ip_bytes);
    write_be16(ip + 4, identifier);
    ip[8] = 64;
    ip[9] = ICMP_PROTOCOL;
    memcpy(ip + 12, LOCAL_IP, IPV4_BYTES);
    memcpy(ip + 16, peer_ip, IPV4_BYTES);
    write_be16(ip + 10, checksum(ip, IPV4_HEADER_BYTES));

    icmp[0] = ICMP_ECHO_REQUEST;
    write_be16(icmp + 4, identifier);
    write_be16(icmp + 6, 1);
    memcpy(icmp + ICMP_HEADER_BYTES, ECHO_DATA, sizeof(ECHO_DATA));
    write_be16(icmp + 2, checksum(icmp, icmp_bytes));
    int frame_bytes = NET_FRAME_MIN + ip_bytes;
    if (frame_bytes < ETH_MIN_TX_BYTES)
        frame_bytes = ETH_MIN_TX_BYTES;
    return sys_write(fd, &frame, frame_bytes) == frame_bytes;
}

static int matches_echo(const struct EthernetFrame* frame, long length, const unsigned char* local_mac,
                        const unsigned char* peer_ip, unsigned int identifier) {
    if (length < NET_FRAME_MIN + IPV4_HEADER_BYTES || read_be16(frame->type) != ETH_TYPE_IPV4 ||
        memcmp(frame->destination, local_mac, NET_MAC_SIZE) != 0)
        return 0;
    const unsigned char* ip = frame->payload;
    int header_bytes = (ip[0] & 15) * 4;
    int ip_bytes = (int)read_be16(ip + 2);
    int icmp_bytes = ip_bytes - header_bytes;
    if ((ip[0] >> 4) != 4 || header_bytes < IPV4_HEADER_BYTES || ip_bytes > length - NET_FRAME_MIN ||
        icmp_bytes != ICMP_HEADER_BYTES + (int)sizeof(ECHO_DATA) || ip[9] != ICMP_PROTOCOL ||
        (read_be16(ip + 6) & 0x3fffU) != 0 || memcmp(ip + 12, peer_ip, IPV4_BYTES) != 0 ||
        memcmp(ip + 16, LOCAL_IP, IPV4_BYTES) != 0 || checksum(ip, header_bytes) != 0)
        return 0;
    const unsigned char* icmp = ip + header_bytes;
    return icmp[0] == ICMP_ECHO_REPLY && icmp[1] == 0 && read_be16(icmp + 4) == identifier &&
           read_be16(icmp + 6) == 1 && checksum(icmp, icmp_bytes) == 0 &&
           memcmp(icmp + ICMP_HEADER_BYTES, ECHO_DATA, sizeof(ECHO_DATA)) == 0;
}

static int receive_echo(int fd, const unsigned char* local_mac, const unsigned char* peer_ip, unsigned int identifier) {
    struct EthernetFrame frame;
    for (int i = 0; i < MAX_RX_FRAMES; ++i) {
        long length = sys_read(fd, &frame, sizeof(frame));
        if (length < 0)
            return 0;
        if (matches_echo(&frame, length, local_mac, peer_ip, identifier))
            return 1;
        struct ArpPacket arp;
        if (parse_arp(&frame, length, &arp))
            answer_arp(fd, local_mac, &arp);
    }
    return 0;
}

int main(int argc, char** argv) {
    unsigned char peer_ip[IPV4_BYTES];
    if (argc != 2 || !parse_ipv4(argv[1], peer_ip))
        return command_usage("Usage: ping <IPv4 address>\n");
    if (memcmp(LOCAL_IP, peer_ip, 3) != 0 || peer_ip[3] == 0 || peer_ip[3] == 255 || peer_ip[3] == LOCAL_IP[3])
        return command_usage("ping: use another host on 10.0.2.0/24\n");

    int fd = sys_open("/dev/net0", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        command_text(STDERR_FD, "ping: cannot open /dev/net0\n");
        return 1;
    }
    unsigned char local_mac[NET_MAC_SIZE];
    unsigned char peer_mac[NET_MAC_SIZE];
    unsigned int identifier = (unsigned int)sys_getpid() & 0xffffU;
    int status = 1;
    long error = sys_ioctl(fd, NET_GET_MAC, (long)local_mac);
    if (error < 0)
        command_error("ping", "/dev/net0", error);
    else if (!resolve_peer(fd, local_mac, peer_ip, peer_mac))
        command_text(STDERR_FD, "ping: ARP resolution failed\n");
    else if (!send_echo(fd, local_mac, peer_mac, peer_ip, identifier))
        command_text(STDERR_FD, "ping: could not send echo request\n");
    else if (!receive_echo(fd, local_mac, peer_ip, identifier))
        command_text(STDERR_FD, "ping: no matching echo reply\n");
    else {
        command_text(STDOUT_FD, "Reply from ");
        command_text(STDOUT_FD, argv[1]);
        command_text(STDOUT_FD, "\n");
        status = 0;
    }
    sys_close(fd);
    return status;
}
