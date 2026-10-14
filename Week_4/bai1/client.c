/*
 * client.c - Problem 1 (IT4062 - Week 4)
 *
 * TCP client: reads a string from the keyboard, sends it to the server and
 * prints the reply (a digits string and a letters string, or "Error").
 * The loop ends when the user enters an empty string (or on EOF / Ctrl+D).
 *
 * Usage: ./client IPAddress PortNumber   (e.g. ./client 127.0.0.1 5500)
 *
 * Framing: every message is a 4-byte big-endian length followed by the
 * payload, so the program is correct even though TCP is a byte stream
 * (see server.c for details).
 */

#include <arpa/inet.h>
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

#define INPUT_MAX 4096u    /* max characters per input line (incl. NUL) */
#define MAX_FRAME 65536u   /* max accepted reply size in bytes          */

#define RECV_OK 0
#define RECV_ERR -1

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

/* Receive exactly len bytes. Returns RECV_OK or RECV_ERR. */
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
            return RECV_ERR; /* server closed the connection */
        got += (size_t)n;
    }
    return RECV_OK;
}

/* Send one framed message. */
static int send_frame(int fd, const char *data, uint32_t len)
{
    uint32_t net_len = htonl(len);

    if (send_all(fd, &net_len, sizeof(net_len)) < 0)
        return -1;
    if (len > 0 && send_all(fd, data, len) < 0)
        return -1;
    return 0;
}

/* Receive one framed message; *out must be freed by the caller. */
static int recv_frame(int fd, char **out, uint32_t *out_len)
{
    uint32_t net_len, len;
    char *buf;

    if (recv_all(fd, &net_len, sizeof(net_len)) != RECV_OK)
        return RECV_ERR;
    len = ntohl(net_len);
    if (len > MAX_FRAME)
        return RECV_ERR;

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

int main(int argc, char *argv[])
{
    struct sockaddr_in serv;
    char *end = NULL;
    long port;
    int fd;
    char line[INPUT_MAX];

    if (argc != 3) {
        fprintf(stderr, "Usage: %s IPAddress PortNumber\n", argv[0]);
        return EXIT_FAILURE;
    }

    port = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || *end != '\0' || port < 1 || port > 65535) {
        fprintf(stderr, "Error: invalid port number '%s' (must be 1-65535)\n",
                argv[2]);
        return EXIT_FAILURE;
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, argv[1], &serv.sin_addr) != 1) {
        fprintf(stderr, "Error: invalid IPv4 address '%s'\n", argv[1]);
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }
    if (connect(fd, (struct sockaddr *)&serv, sizeof(serv)) < 0) {
        perror("connect");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("Connected to %s:%ld. Enter an empty string to quit.\n", argv[1],
           port);

    for (;;) {
        size_t len;
        char *reply = NULL;
        uint32_t reply_len = 0;

        printf("Enter string: ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
            break; /* EOF (Ctrl+D) behaves like an empty string */

        len = strlen(line);
        if (len > 0 && line[len - 1] != '\n' && !feof(stdin)) {
            /* Line longer than the buffer: discard the rest of it. */
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF)
                ;
            fprintf(stderr, "Error: input too long (max %u characters)\n",
                    INPUT_MAX - 1);
            continue;
        }

        /* Strip trailing newline / carriage return. */
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0)
            break; /* empty string -> quit */

        if (send_frame(fd, line, (uint32_t)len) < 0) {
            fprintf(stderr, "Error: failed to send data to server\n");
            break;
        }
        if (recv_frame(fd, &reply, &reply_len) != RECV_OK) {
            fprintf(stderr, "Error: connection to server lost\n");
            break;
        }

        printf("%s\n", reply);
        free(reply);
    }

    close(fd);
    printf("Bye.\n");
    return EXIT_SUCCESS;
}
