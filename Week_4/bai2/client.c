/*
 * client.c - Problem 2 (IT4062 - Week 4)
 *
 * TCP file-upload client. Repeatedly asks for a file path, uploads the file
 * to the server and prints the result. Ends when an empty path is entered.
 *
 * Usage: ./client IPAddress PortNumber   (e.g. ./client 127.0.0.1 5500)
 *
 * Protocol (see server.c for the full description, integers are big-endian):
 *   -> uint16 name_len | name bytes | uint64 file_size
 *   <- uint8 status (0 = OK, otherwise error code)
 *   -> file content (file_size bytes)
 *   <- uint8 final status (0 = stored successfully)
 *
 * Optional testing hook: set the environment variable SEND_DELAY_MS to sleep
 * that many milliseconds after every 64 KB chunk. This slows the upload down
 * so that an interrupted transfer can be reproduced by hand (kill the server
 * or press Ctrl+C while the transfer is running).
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
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define INPUT_MAX 4096
#define MAX_NAME_LEN 255
#define CHUNK_SIZE 65536

/* Status codes shared with server.c. */
#define ST_OK 0
#define ST_EXISTS 1
#define ST_BAD_NAME 2
#define ST_TOO_LARGE 3
#define ST_IO_ERROR 4

/* Result of one upload attempt. */
enum upload_result {
    UP_OK,          /* file stored, connection still usable            */
    UP_REJECTED,    /* server refused / local error, connection usable */
    UP_INTERRUPTED  /* connection broken, must reconnect               */
};

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

static int recv_all(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    size_t got = 0;

    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1; /* server closed the connection */
        got += (size_t)n;
    }
    return 0;
}

/* Encode v as n big-endian bytes. */
static void encode_be(unsigned char *b, uint64_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        b[i] = (unsigned char)(v & 0xFF);
        v >>= 8;
    }
}

/* Connect to the server; returns a socket or -1. */
static int connect_to_server(const struct sockaddr_in *serv)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;
    if (connect(fd, (const struct sockaddr *)serv, sizeof(*serv)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Return the part of path after the last '/' or '\' (handles D:\a\b.txt). */
static const char *base_name(const char *path)
{
    const char *p = path, *last = path;

    for (; *p; p++)
        if (*p == '/' || *p == '\\')
            last = p + 1;
    return last;
}

static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Upload one file over the already connected socket. */
static enum upload_result upload_file(int fd, const char *path)
{
    static unsigned char buf[CHUNK_SIZE];
    unsigned char header[2 + MAX_NAME_LEN + 8];
    unsigned char status;
    struct stat st;
    const char *name;
    size_t name_len;
    uint64_t size, remaining;
    FILE *fp;
    long delay_ms = 0;
    const char *env = getenv("SEND_DELAY_MS");

    if (env != NULL)
        delay_ms = strtol(env, NULL, 10);

    /* ---- Local checks ---- */
    if (stat(path, &st) < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            printf("Error: File not found\n");
        else
            printf("Error: Cannot access file (%s)\n", strerror(errno));
        return UP_REJECTED;
    }
    if (!S_ISREG(st.st_mode)) {
        printf("Error: Not a regular file\n");
        return UP_REJECTED;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        if (errno == ENOENT)
            printf("Error: File not found\n");
        else
            printf("Error: Cannot open file (%s)\n", strerror(errno));
        return UP_REJECTED;
    }

    name = base_name(path);
    name_len = strlen(name);
    if (name_len == 0 || name_len > MAX_NAME_LEN) {
        printf("Error: Invalid file name (must be 1-%d characters)\n",
               MAX_NAME_LEN);
        fclose(fp);
        return UP_REJECTED;
    }
    size = (uint64_t)st.st_size;

    /* ---- Header ---- */
    encode_be(header, name_len, 2);
    memcpy(header + 2, name, name_len);
    encode_be(header + 2 + name_len, size, 8);
    if (send_all(fd, header, 2 + name_len + 8) < 0 ||
        recv_all(fd, &status, 1) < 0) {
        printf("Error: File tranfering is interupted\n");
        fclose(fp);
        return UP_INTERRUPTED;
    }

    switch (status) {
    case ST_OK:
        break;
    case ST_EXISTS:
        printf("Error: File is existent on server\n");
        fclose(fp);
        return UP_REJECTED;
    case ST_BAD_NAME:
        printf("Error: Invalid file name\n");
        fclose(fp);
        return UP_REJECTED;
    case ST_TOO_LARGE:
        printf("Error: File is too large for the server\n");
        fclose(fp);
        return UP_REJECTED;
    default:
        printf("Error: Server could not store the file\n");
        fclose(fp);
        return UP_REJECTED;
    }

    /* ---- Body ---- */
    remaining = size;
    while (remaining > 0) {
        size_t want = remaining < CHUNK_SIZE ? (size_t)remaining : CHUNK_SIZE;
        size_t n = fread(buf, 1, want, fp);

        if (n == 0) { /* file shrank or read error while sending */
            printf("Error: File tranfering is interupted\n");
            fclose(fp);
            return UP_INTERRUPTED; /* server expects more bytes: reconnect */
        }
        if (send_all(fd, buf, n) < 0) {
            printf("Error: File tranfering is interupted\n");
            fclose(fp);
            return UP_INTERRUPTED;
        }
        remaining -= n;
        if (delay_ms > 0)
            sleep_ms(delay_ms);
    }
    fclose(fp);

    /* ---- Final status ---- */
    if (recv_all(fd, &status, 1) < 0) {
        printf("Error: File tranfering is interupted\n");
        return UP_INTERRUPTED;
    }
    if (status != ST_OK) {
        printf("Error: Server could not store the file\n");
        return UP_INTERRUPTED; /* server closes the connection after this */
    }
    printf("Successful transfering\n");
    return UP_OK;
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

    fd = connect_to_server(&serv);
    if (fd < 0) {
        perror("connect");
        return EXIT_FAILURE;
    }
    printf("Connected to %s:%ld. Enter an empty path to quit.\n", argv[1],
           port);

    for (;;) {
        size_t len;

        printf("Enter file path: ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL)
            break; /* EOF behaves like an empty path */

        len = strlen(line);
        if (len > 0 && line[len - 1] != '\n' && !feof(stdin)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF)
                ;
            printf("Error: path too long (max %d characters)\n", INPUT_MAX - 1);
            continue;
        }
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';

        if (len == 0)
            break; /* empty path -> quit */

        if (upload_file(fd, line) == UP_INTERRUPTED) {
            /* The old connection is unusable: open a fresh one. */
            close(fd);
            fd = connect_to_server(&serv);
            if (fd < 0) {
                printf("Error: cannot reconnect to server. Exiting.\n");
                return EXIT_FAILURE;
            }
            printf("(reconnected to server)\n");
        }
    }

    close(fd);
    printf("Bye.\n");
    return EXIT_SUCCESS;
}
