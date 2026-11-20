#include "../command.h"
#include "zonix_net.h"

/* Downloads a file over HTTP/1.1 through netd. */

#define HEADER_MAX     8192
#define BLOCK_BYTES    4096
#define URL_MAX        512
#define MAX_REDIRECTS  5
#define READ_TIMEOUT_MS 30000
#define HOST_MAX        64

struct Url {
    char host[HOST_MAX];
    int port;
    char path[URL_MAX];
};

struct Response {
    int status;
    long content_length; /* -1 when the response does not say */
    int chunked;
    char location[URL_MAX];
};

/* Connection state: bytes already read past the headers are served first. */
struct Stream {
    int fd;
    char buffer[HEADER_MAX];
    long start;
    long end;
};

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static int starts_with_nocase(const char* text, const char* prefix) {
    for (; *prefix; ++text, ++prefix)
        if (lower(*text) != *prefix)
            return 0;
    return 1;
}

static void copy_text(char* out, long capacity, const char* begin, long length) {
    if (length >= capacity)
        length = capacity - 1;
    memcpy(out, (void*)begin, length);
    out[length] = 0;
}

/* Accepts http://host[:port][/path]. */
static int parse_url(const char* text, struct Url* url) {
    if (!starts_with_nocase(text, "http://")) {
        if (starts_with_nocase(text, "https://"))
            printf("fetch: HTTPS is not supported yet\n");
        else
            printf("fetch: %s: only http:// URLs are supported\n", text);
        return 0;
    }
    const char* host = text + 7;
    const char* end = host;
    while (*end && *end != ':' && *end != '/')
        ++end;
    if (end == host || end - host >= (long)sizeof(url->host))
        return 0;
    copy_text(url->host, sizeof(url->host), host, end - host);
    url->port = 80;
    if (*end == ':') {
        int port = 0;
        for (++end; *end >= '0' && *end <= '9' && port <= 65535; ++end)
            port = port * 10 + (*end - '0');
        if (port <= 0 || port > 65535 || (*end && *end != '/'))
            return 0;
        url->port = port;
    }
    copy_text(url->path, sizeof(url->path), *end ? end : "/", *end ? (long)strlen((char*)end) : 1);
    return 1;
}

static int send_all(int fd, const char* text) {
    return command_write_all(fd, text, strlen((char*)text)) == 0;
}

/* Reads more bytes into the stream buffer; 0 at end of stream, -1 on error or timeout. */
static long fill(struct Stream* stream) {
    if (stream->start == stream->end)
        stream->start = stream->end = 0;
    if (stream->end == (long)sizeof(stream->buffer))
        return -1;
    long ready = sys_wait_readable_for(1L << stream->fd, READ_TIMEOUT_MS);
    if (ready <= 0) {
        printf("fetch: %s\n", ready == 0 ? "the server stopped answering" : "connection error");
        return -1;
    }
    long count = sys_read(stream->fd, stream->buffer + stream->end, (long)sizeof(stream->buffer) - stream->end);
    if (count < 0)
        return -1;
    stream->end += count;
    return count;
}

/* Returns a pointer to the next line ending in CRLF, terminated in place. */
static char* next_line(struct Stream* stream) {
    for (;;) {
        for (long i = stream->start; i + 1 < stream->end; ++i) {
            if (stream->buffer[i] == '\r' && stream->buffer[i + 1] == '\n') {
                char* line = stream->buffer + stream->start;
                stream->buffer[i] = 0;
                stream->start = i + 2;
                return line;
            }
        }
        if (stream->start != 0) {
            memmove(stream->buffer, stream->buffer + stream->start, stream->end - stream->start);
            stream->end -= stream->start;
            stream->start = 0;
        }
        if (fill(stream) <= 0)
            return 0;
    }
}

static long parse_number(const char* text, int base) {
    long value = 0;
    int digits = 0;
    for (;; ++text, ++digits) {
        char c = lower(*text);
        int digit = c >= '0' && c <= '9' ? c - '0' : base == 16 && c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0)
            break;
        value = value * base + digit;
    }
    return digits == 0 ? -1 : value;
}

static int read_response(struct Stream* stream, struct Response* response) {
    char* line = next_line(stream);
    if (!line || !starts_with_nocase(line, "http/1.") || strlen(line) < 12) {
        printf("fetch: the server did not answer with HTTP\n");
        return 0;
    }
    response->status = (int)parse_number(line + 9, 10);
    response->content_length = -1;
    response->chunked = 0;
    response->location[0] = 0;
    if (response->status < 100) {
        printf("fetch: malformed status line\n");
        return 0;
    }
    if (response->status != 200 && (response->status < 300 || response->status > 308))
        printf("fetch: server answered: %s\n", line + 9);
    while ((line = next_line(stream)) && *line) {
        if (starts_with_nocase(line, "content-length:"))
            response->content_length = parse_number(line + 15 + (line[15] == ' '), 10);
        else if (starts_with_nocase(line, "transfer-encoding:"))
            response->chunked = 1;
        else if (starts_with_nocase(line, "location:")) {
            const char* value = line + 9;
            while (*value == ' ')
                ++value;
            copy_text(response->location, sizeof(response->location), value, strlen((char*)value));
        }
    }
    if (!line) {
        printf("fetch: the response headers were cut short\n");
        return 0;
    }
    return 1;
}

/* Copies up to `limit` body bytes (all of them when negative) to the output. */
static long copy_body(struct Stream* stream, int output, long limit, long* total) {
    long copied = 0;
    while (limit < 0 || copied < limit) {
        if (stream->start == stream->end) {
            long count = fill(stream);
            if (count < 0)
                return -1;
            if (count == 0)
                return limit < 0 ? copied : -1;
        }
        long count = stream->end - stream->start;
        if (limit >= 0 && count > limit - copied)
            count = limit - copied;
        if (command_write_all(output, stream->buffer + stream->start, count) < 0) {
            printf("fetch: writing the output failed\n");
            return -1;
        }
        stream->start += count;
        copied += count;
        *total += count;
    }
    return copied;
}

static int copy_chunked(struct Stream* stream, int output, long* total) {
    for (;;) {
        char* line = next_line(stream);
        long size = line ? parse_number(line, 16) : -1;
        if (size < 0)
            return 0;
        if (size == 0)
            break;
        if (copy_body(stream, output, size, total) != size || !(line = next_line(stream)) || *line)
            return 0;
    }
    /* Trailer fields end with an empty line. */
    char* line;
    while ((line = next_line(stream)) && *line) {
    }
    return line != 0;
}

/* Resolves a Location header against the current URL. */
static int follow(const struct Url* current, const char* location, struct Url* next) {
    if (location[0] == '/') {
        *next = *current;
        copy_text(next->path, sizeof(next->path), location, strlen((char*)location));
        return 1;
    }
    return parse_url(location, next);
}

static const char* default_name(const struct Url* url) {
    const char* name = url->path;
    for (const char* c = url->path; *c && *c != '?'; ++c)
        if (*c == '/')
            name = c + 1;
    return *name && *name != '?' ? name : "index.html";
}

int main(int argc, char** argv) {
    const char* output_path = 0;
    if (argc == 4 && strcmp(argv[1], "-o") == 0) {
        output_path = argv[2];
        argv += 2;
        argc -= 2;
    }
    struct Url url;
    if (argc != 2)
        return command_usage("Usage: fetch [-o FILE|-] http://host[:port]/path\n");
    if (!parse_url(argv[1], &url))
        return 1;

    static struct Stream stream;
    struct Response response;
    for (int redirects = 0;; ++redirects) {
        unsigned int address;
        int error = net_resolve(url.host, &address);
        if (error < 0) {
            printf("fetch: %s: %s\n", url.host, net_error_text(error));
            return 1;
        }
        stream.fd = net_tcp_connect(address, url.port);
        stream.start = stream.end = 0;
        if (stream.fd < 0) {
            printf("fetch: %s:%d: %s\n", url.host, url.port, net_error_text(stream.fd));
            return 1;
        }
        char port[8] = "";
        if (url.port != 80) {
            port[0] = ':';
            char digits[6];
            int count = 0;
            for (int value = url.port; value > 0; value /= 10)
                digits[count++] = (char)('0' + value % 10);
            for (int i = 0; i < count; ++i)
                port[1 + i] = digits[count - 1 - i];
            port[1 + count] = 0;
        }
        if (!send_all(stream.fd, "GET ") || !send_all(stream.fd, url.path) ||
            !send_all(stream.fd, " HTTP/1.1\r\nHost: ") || !send_all(stream.fd, url.host) || !send_all(stream.fd, port) ||
            !send_all(stream.fd, "\r\nUser-Agent: zonix-fetch/1\r\nAccept: */*\r\nConnection: close\r\n\r\n")) {
            printf("fetch: sending the request failed\n");
            return 1;
        }
        if (!read_response(&stream, &response))
            return 1;
        int redirect = response.status >= 301 && response.status <= 308 && response.status != 304 &&
                       response.status != 305 && response.status != 306;
        if (!redirect)
            break;
        sys_close(stream.fd);
        struct Url next;
        if (redirects == MAX_REDIRECTS || !response.location[0] || !follow(&url, response.location, &next)) {
            printf("fetch: cannot follow the redirect\n");
            return 1;
        }
        url = next;
    }
    if (response.status != 200)
        return 1;

    int to_stdout = output_path && strcmp((char*)output_path, "-") == 0;
    if (!output_path)
        output_path = default_name(&url);
    int output = to_stdout ? STDOUT_FD : sys_open((char*)output_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (output < 0)
        return command_error("fetch", output_path, output);
    long started = sys_uptime_ms();
    long total = 0;
    int ok = response.chunked ? copy_chunked(&stream, output, &total)
                              : copy_body(&stream, output, response.content_length, &total) >= 0;
    if (!to_stdout)
        sys_close(output);
    sys_close(stream.fd);
    if (!ok) {
        printf("fetch: the download was cut short after %d bytes\n", (int)total);
        return 1;
    }
    if (!to_stdout) {
        long elapsed = sys_uptime_ms() - started;
        printf("fetch: saved %d bytes to %s (%d KiB/s)\n", (int)total, output_path,
               (int)(elapsed > 0 ? total * 1000 / 1024 / elapsed : total / 1024));
    }
    return 0;
}
