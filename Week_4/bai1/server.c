/*
 * server.c - Problem 1 (IT4062 - Week 4)
 *
 * TCP server that receives a string from a client and replies with:
 *   - one string containing only the digits of the input, and
 *   - one string containing only the letters of the input.
 * If the input contains any character that is neither a letter nor a digit,
 * the server replies with "Error".
 *
 * Usage: ./server PortNumber      (e.g. ./server 5500)
 *
 * TCP is a byte stream (message boundaries are NOT preserved), so every
 * message is framed with a 4-byte length prefix in network byte order:
 *
 *      +----------------------+---------------------+
 *      | length (uint32, BE)  | payload (length B)  |
 *      +----------------------+---------------------+
 *
 * Both sides use send_all()/recv_all() so partial send()/recv() calls and
 * several messages arriving in one segment are handled correctly.
 *
 * The server is concurrent: one child process is forked per client.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG 16
#define MAX_FRAME 65536u /* maximum accepted payload size in bytes */

/* Return codes for recv_all(). */
#define RECV_OK 0
#define RECV_EOF 1  /* peer closed before ANY byte of this read arrived */
#define RECV_ERR -1 /* error, or peer closed in the middle of the read   */

/* Send exactly len bytes. Returns 0 on success, -1 on error. */
static int send_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    size_t sent = 0;

    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        sent += (size_t)n;
    }
    return 0;
}

/* Receive exactly len bytes (loops until all bytes have arrived). */
static int recv_all(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    size_t got = 0;

    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return RECV_ERR;
        }
        if (n == 0)
            return got == 0 ? RECV_EOF : RECV_ERR;
        got += (size_t)n;
    }
    return RECV_OK;
}

/* Send one framed message (4-byte big-endian length + payload). */
static int send_frame(int fd, const char *data, uint32_t len)
{
    uint32_t net_len = htonl(len);

    if (send_all(fd, &net_len, sizeof(net_len)) < 0)
        return -1;
    if (len > 0 && send_all(fd, data, len) < 0)
        return -1;
    return 0;
}

/*
 * Receive one framed message. On success *out is a malloc'ed buffer
 * (NUL-terminated for convenience) that the caller must free.
 * Returns RECV_OK, RECV_EOF (clean close between messages) or RECV_ERR.
 */
static int recv_frame(int fd, char **out, uint32_t *out_len)
{
    uint32_t net_len, len;
    char *buf;
    int rc;

    rc = recv_all(fd, &net_len, sizeof(net_len));
    if (rc != RECV_OK)
        return rc;

    len = ntohl(net_len);
    if (len > MAX_FRAME)
        return RECV_ERR; /* protocol violation */

    buf = malloc((size_t)len + 1);
    if (buf == NULL)
        return RECV_ERR;

    if (len > 0 && recv_all(fd, buf, len) != RECV_OK) {
        free(buf);
        return RECV_ERR;
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    return RECV_OK;
}

/*
 * Build the reply for one input string.
 * Returns a malloc'ed, NUL-terminated string and stores its length in
 * *reply_len. Reply format:
 *   - "Error" if the input has a character that is not a letter/digit
 *   - otherwise the digits string, then the letters string, each on its own
 *     line (a line is omitted if the corresponding string is empty)
 */
static char *build_reply(const char *s, uint32_t len, size_t *reply_len)
{
    char *digits = malloc((size_t)len + 1);
    char *letters = malloc((size_t)len + 1);
    char *reply = malloc((size_t)len + 8); /* digits + letters + '\n' + slack */
    size_t nd = 0, nl = 0;
    int valid = (len > 0);

    if (digits == NULL || letters == NULL || reply == NULL) {
        free(digits);
        free(letters);
        free(reply);
        return NULL;
    }

    for (uint32_t i = 0; i < len && valid; i++) {
        unsigned char c = (unsigned char)s[i];
        if (isdigit(c))
            digits[nd++] = (char)c;
        else if (isalpha(c))
            letters[nl++] = (char)c;
        else
            valid = 0; /* includes spaces, symbols and NUL bytes */
    }

    if (!valid) {
        strcpy(reply, "Error");
        *reply_len = strlen(reply);
    } else {
        size_t pos = 0;
        if (nd > 0) {
            memcpy(reply + pos, digits, nd);
            pos += nd;
        }
        if (nd > 0 && nl > 0)
            reply[pos++] = '\n';
        if (nl > 0) {
            memcpy(reply + pos, letters, nl);
            pos += nl;
        }
        reply[pos] = '\0';
        *reply_len = pos;
    }

    free(digits);
    free(letters);
    return reply;
}

/* Serve one connected client until it disconnects. */
static void handle_client(int fd, const char *ip, unsigned short port)
{
    for (;;) {
        char *msg = NULL;
        uint32_t len = 0;
        int rc = recv_frame(fd, &msg, &len);

        if (rc == RECV_EOF) {
            printf("[%s:%hu] client disconnected\n", ip, port);
            break;
        }
        if (rc == RECV_ERR) {
            printf("[%s:%hu] connection error / protocol violation\n", ip, port);
            break;
        }

        size_t reply_len = 0;
        char *reply = build_reply(msg, len, &reply_len);
        /* Log at most 60 characters of the message to keep the log readable. */
        printf("[%s:%hu] received \"%.60s%s\" (%u bytes) -> %s\n", ip, port,
               msg, len > 60 ? "..." : "", len,
               reply != NULL && strcmp(reply, "Error") == 0 ? "Error" : "OK");
        fflush(stdout);
        free(msg);

        if (reply == NULL) {
            fprintf(stderr, "out of memory\n");
            break;
        }
        int ok = send_frame(fd, reply, (uint32_t)reply_len);
        free(reply);
        if (ok < 0) {
            printf("[%s:%hu] failed to send reply\n", ip, port);
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    int listen_fd;
    int opt = 1;
    struct sockaddr_in addr;
    char *end = NULL;
    long port;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s PortNumber\n", argv[0]);
        return EXIT_FAILURE;
    }

    port = strtol(argv[1], &end, 10);
    if (*argv[1] == '\0' || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Error: invalid port number '%s' (must be 1-65535)\n",
                argv[1]);
        return EXIT_FAILURE;
    }

    /* Do not die on SIGPIPE; do not leave zombie children. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return EXIT_FAILURE;
    }
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        return EXIT_FAILURE;
    }

    printf("Server listening on port %ld ...\n", port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int conn_fd = accept(listen_fd, (struct sockaddr *)&cli, &cli_len);
        char ip[INET_ADDRSTRLEN];
        pid_t pid;

        if (conn_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        printf("[%s:%hu] client connected\n", ip, ntohs(cli.sin_port));
        fflush(stdout);

        pid = fork();
        if (pid < 0) {
            perror("fork");
            close(conn_fd);
            continue;
        }
        if (pid == 0) { /* child: serve this client */
            close(listen_fd);
            handle_client(conn_fd, ip, ntohs(cli.sin_port));
            close(conn_fd);
            _exit(EXIT_SUCCESS);
        }
        close(conn_fd); /* parent: keep listening */
    }

    close(listen_fd);
    return EXIT_FAILURE;
}
