#include "zonix_port.h"
#include "../command.h"
#include "buf.h"
#include "channel.h"
#include "die.h"
#include "e.h"
#include "global.h"
#include "log.h"
#include "packet.h"
#include "purge.h"
#include "ssh.h"
#include "sshcrypto.h"
#include "str.h"
#include "subprocess.h"
#include <poll.h>

#define CONNECT_TIMEOUT_MS 60000L
#define SESSION_TIMEOUT_MS 3600000L
#define KEY_DIR            "/opt/ssh"
#define BASE64_KEY_BYTES   68

static char authorized_key[BASE64_KEY_BYTES + 1];

int subprocess_auth(const char* user, const char* algorithm, const char* public_key) {
    return str_equaln(user, str_len(user), "zonix") && str_equaln(algorithm, str_len(algorithm), "ssh-ed25519") &&
                   str_equaln(public_key, str_len(public_key), authorized_key)
               ? 0
               : -1;
}

int subprocess_sign(unsigned char* output, long long output_size, const char* directory, unsigned char* hash,
                    long long hash_size) {
    (void)directory;
    if (output_size != sshcrypto_sign_bytes || hash_size != sshcrypto_hash_bytes)
        return -1;

    unsigned char secret[crypto_sign_ed25519_SECRETKEYBYTES];
    unsigned char signed_hash[sshcrypto_sign_MAX + sshcrypto_hash_MAX];
    unsigned long long signed_size = 0;
    int ok = zonix_load_key(KEY_DIR "/HOST.SK", secret, sizeof(secret)) &&
             memcmp(secret + crypto_sign_ed25519_PUBLICKEYBYTES, sshcrypto_sign_publickey,
                    crypto_sign_ed25519_PUBLICKEYBYTES) == 0 &&
             sshcrypto_sign(signed_hash, &signed_size, hash, (unsigned long long)hash_size, secret) == 0 &&
             signed_size == (unsigned long long)(output_size + hash_size);
    if (ok)
        memcpy(output, signed_hash, (size_t)output_size);
    purge(secret, sizeof(secret));
    purge(signed_hash, sizeof(signed_hash));
    return ok ? 0 : -1;
}

static int prepare_protocol(void) {
    if (!zonix_load_key(KEY_DIR "/LOGIN.PUB", authorized_key, BASE64_KEY_BYTES))
        return 0;
    struct sshcrypto_key* key = &sshcrypto_keys[0];
    if (!zonix_load_key(KEY_DIR "/HOST.PK", key->sign_publickey, key->sign_publickeybytes))
        return 0;
    key->sign_flagserver = key->sign_flagclient = 1;
    for (int i = 0; sshcrypto_kexs[i].name; ++i)
        sshcrypto_kexs[i].flagenabled = 1;
    for (int i = 0; sshcrypto_ciphers[i].name; ++i)
        sshcrypto_ciphers[i].flagenabled = 1;
    return 1;
}

static int rekey(struct buf* packet_in, struct buf* work) {
    zonix_deadline(CONNECT_TIMEOUT_MS);
    if (!packet_kex_receive_rekey(packet_in) || !packet_kex_send() || !packet_kexdh(KEY_DIR, packet_in, work))
        return 0;
    zonix_deadline(SESSION_TIMEOUT_MS);
    return 1;
}

static int process_packet(struct buf* packet_in, struct buf* work) {
    switch (packet_in->buf[0]) {
        case SSH_MSG_GLOBAL_REQUEST: return packet_global_request(packet_in);
        case SSH_MSG_CHANNEL_OPEN: return packet_channel_open(packet_in, work);
        case SSH_MSG_CHANNEL_REQUEST: return packet_channel_request(packet_in, work, 0);
        case SSH_MSG_CHANNEL_DATA: return packet_channel_recv_data(packet_in);
        case SSH_MSG_CHANNEL_EXTENDED_DATA: return packet_channel_recv_extendeddata(packet_in);
        case SSH_MSG_CHANNEL_WINDOW_ADJUST: return packet_channel_recv_windowadjust(packet_in);
        case SSH_MSG_CHANNEL_EOF: return packet_channel_recv_eof(packet_in);
        case SSH_MSG_CHANNEL_CLOSE: return packet_channel_recv_close(packet_in);
        case SSH_MSG_KEXINIT: return rekey(packet_in, work);
        default: return packet_unimplemented(packet_in);
    }
}

static int serve(struct buf* packet_in, struct buf* work) {
    for (;;) {
        int signal = 0;
        int status = 0;
        if (channel_iseof() && packet_putisready() && channel_waitnohang(&signal, &status)) {
            packet_channel_send_eof(work);
            if (!packet_channel_send_close(work, signal, status))
                return 0;
        }
        if (packet.flagclosesent && packet.flagchanneleofreceived && !packet_sendisready())
            return 1;
        if (zonix_pty_read_pending() && channel_readisready() && packet_putisready())
            packet_channel_send_data(work);
        if (channel_writeisready() && channel.buf0[0] == 3 && !channel_write())
            return 0;

        struct pollfd io[3];
        unsigned long count = 0;
        if (packet_recvisready() && packet_putisready())
            io[count++] = (struct pollfd){STDIN_FD, POLLIN, 0};
        if (packet_sendisready())
            io[count++] = (struct pollfd){STDOUT_FD, POLLOUT, 0};
        short tty_events = 0;
        if (channel_readisready() && packet_putisready())
            tty_events |= POLLIN;
        if (channel_writeisready())
            tty_events |= POLLOUT;
        if (tty_events != 0)
            io[count++] = (struct pollfd){channel.master, tty_events, 0};
        if (count == 0 || poll(io, count, -1) < 0)
            return 0;

        for (unsigned long i = 0; i < count; ++i) {
            if (io[i].revents == 0)
                continue;
            if (io[i].fd == STDIN_FD) {
                if (!packet_recv())
                    return errno == ECONNRESET;
                zonix_deadline(SESSION_TIMEOUT_MS);
            } else if (io[i].fd == STDOUT_FD) {
                if (!packet_send())
                    return 0;
            } else {
                if ((io[i].revents & POLLOUT) != 0 && !channel_write())
                    return 0;
                if ((io[i].revents & POLLIN) != 0 && packet_putisready())
                    packet_channel_send_data(work);
            }
        }

        while (packet_putisready()) {
            if (!packet_get(packet_in, 0))
                return errno == 0;
            if (packet_in->len == 0)
                break;
            if (!process_packet(packet_in, work))
                return 0;
        }
        if (channel.maxpacket != 0 && packet_putisready() && !packet_channel_send_windowadjust(work))
            return 0;
    }
}

int main(int argc, char** argv) {
    (void)argv;
    if (argc != 1)
        return command_usage("Usage: tcpserve 22 /opt/bin/sshd\n");
    if (sys_fcntl(STDIN_FD, F_SETFL, O_NONBLOCK) < 0 || sys_fcntl(STDOUT_FD, F_SETFL, O_NONBLOCK) < 0)
        return command_error("sshd", "tcpserve pipes", SYS_ERROR_IO);
    zonix_deadline(CONNECT_TIMEOUT_MS);
    global_init();
    log_init(2, "sshd", 0);
    if (!prepare_protocol())
        die_fatal("unable to load SSH keys", KEY_DIR, 0);

    struct buf packet_in = {global_bspace1, 0, sizeof(global_bspace1)};
    struct buf work = {global_bspace2, 0, sizeof(global_bspace2)};
    if (!packet_hello_send() || !packet_hello_receive() || !packet_kex_send() || !packet_kex_receive() ||
        !packet_kexdh(KEY_DIR, &packet_in, &work))
        die_fatal("SSH key exchange failed", 0, 0);
    packet.flagkeys = 1;
    if (!packet_auth(&packet_in, &work))
        die_fatal("SSH authentication failed", 0, 0);
    packet.flagauthorized = 1;
    zonix_deadline(SESSION_TIMEOUT_MS);
    global_die(serve(&packet_in, &work) ? 0 : 111);
    return 111;
}
