#pragma once

/* Socket interface of the Zonix network service (netd).
 *
 * Every socket is a descriptor. A connected TCP socket is a plain byte stream:
 * read, write, sys_wait_io and fcntl(O_NONBLOCK) work on it directly, and
 * net_shutdown_write ends the sending direction. Functions return a descriptor
 * or 0 on success and a negative NET_ERROR_* value on failure. Addresses are
 * IPv4 values in host order (10.0.2.15 is 0x0A00020F). */

#define NET_ERROR_NO_SERVICE  -1 /* netd is not running */
#define NET_ERROR_REFUSED     -2 /* the peer refused the connection */
#define NET_ERROR_UNREACHABLE -3 /* no route, or the peer did not answer */
#define NET_ERROR_TIMEOUT     -4
#define NET_ERROR_NOT_FOUND   -5 /* the name does not resolve */
#define NET_ERROR_IN_USE      -6 /* the local port is taken */
#define NET_ERROR_RESOURCES   -7 /* netd or the system ran out of sockets */
#define NET_ERROR_INVALID     -8
#define NET_ERROR_NO_ADDRESS  -9 /* the interface has no address yet */
#define NET_ERROR_IO          -10

struct NetInfo {
    unsigned int address;
    unsigned int netmask;
    unsigned int gateway;
    unsigned int dns;
};

int net_tcp_connect(unsigned int address, int port);
/* The listener is readable while a connection waits for net_tcp_accept. */
int net_tcp_listen(int port);
int net_tcp_accept(int listener, unsigned int* address, int* port);
int net_shutdown_write(int fd);

int net_udp_open(int port);
long net_udp_send(int fd, unsigned int address, int port, const void* data, long size);
/* Returns the datagram's length; a longer datagram is cut to size. */
long net_udp_receive(int fd, unsigned int* address, int* port, void* data, long size);

/* Accepts a dotted quad or a host name. */
int net_resolve(const char* name, unsigned int* address);
/* Sends one ICMP echo request; on success *rtt_ms holds the round trip. */
int net_ping(unsigned int address, int timeout_ms, int* rtt_ms);
int net_info(struct NetInfo* info);

int net_parse_ipv4(const char* text, unsigned int* address);
/* Writes "a.b.c.d" into out, which holds at least 16 bytes. */
void net_format_ipv4(unsigned int address, char* out);
const char* net_error_text(int error);
