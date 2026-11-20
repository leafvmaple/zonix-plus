#pragma once

/* Wire format between netd and its clients, private to user/net.
 *
 * A client connects to the channel NETD_CHANNEL and writes one NetRequest.
 * netd answers with one NetReply. What follows depends on the request:
 *   TCP_CONNECT, TCP_CLAIM  the channel carries the TCP byte stream; the client
 *                           ends its direction with CHANNEL_SHUTDOWN_WRITE.
 *   TCP_LISTEN              netd writes one NetReply per waiting connection
 *                           (peer address, port and a token); TCP_CLAIM with the
 *                           token on a new channel takes the connection.
 *   UDP_OPEN                each datagram is a NetDatagram header and its bytes,
 *                           in both directions.
 *   RESOLVE, PING, INFO     nothing; the client closes the channel.
 * Addresses are IPv4 values in host order, 10.0.2.15 being 0x0A00020F. */

#define NETD_CHANNEL      "net"
#define NET_NAME_MAX      63
#define NET_DATAGRAM_MAX  1472

enum NetOp {
    NET_OP_TCP_CONNECT = 1,
    NET_OP_TCP_LISTEN = 2,
    NET_OP_TCP_CLAIM = 3,
    NET_OP_UDP_OPEN = 4,
    NET_OP_RESOLVE = 5,
    NET_OP_PING = 6,
    NET_OP_INFO = 7,
};

struct NetRequest {
    unsigned int op;
    unsigned int address;
    unsigned short port;
    unsigned short reserved;
    unsigned int value; /* TCP_CLAIM token, PING timeout in milliseconds */
    char name[NET_NAME_MAX + 1];
};

struct NetReply {
    int status; /* 0 or a negative NET_ERROR_* value */
    unsigned int address;
    unsigned short port;
    unsigned short reserved;
    unsigned int value; /* listen token, ping round trip in milliseconds, INFO netmask */
    unsigned int gateway;
    unsigned int dns;
};

struct NetDatagram {
    unsigned int address;
    unsigned short port;
    unsigned short length;
};
