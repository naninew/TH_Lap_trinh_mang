/*
 * server.c - Problem 2 (IT4062 - Week 4)
 *
 * TCP file-receiving server. Clients upload files of any format (up to
 * MAX_FILE_SIZE bytes) which are stored in a single shared directory
 * ("storage/" next to the executable). If a file with the same name already
 * exists in that directory the upload is rejected.
 *
 * Usage: ./server PortNumber      (e.g. ./server 5500)
 *
 * Application protocol (all integers are big-endian / network byte order).
 * TCP is a byte stream, so every field is read with recv_all() until the
 * exact number of bytes has arrived.
 *
 *   Client -> Server (header):
 *       uint16  name_len
 *       bytes   file name (name_len bytes, no NUL, base name only)
 *       uint64  file_size
 *   Server -> Client:
 *       uint8   status  (ST_OK = ready to receive, otherwise an error code)
 *   Client -> Server (only if status == ST_OK):
 *       bytes   file content (exactly file_size bytes)
 *   Server -> Client:
 *       uint8   final status (ST_OK = file stored successfully)
 *
 * After a finished upload (success or a "rejected" header) the connection
 * stays open and the client may send another file.
 * If the connection breaks in the middle of a transfer, the partial file is
 * deleted so that no corrupted file is left on the server.
 *
 * The server is concurrent: one child process is forked per client.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG 16
#define STORAGE_DIR "storage"
#define MAX_NAME_LEN 255
#define MAX_FILE_SIZE ((uint64_t)1 << 31) /* 2 GiB (task needs 100 MB) */
#define CHUNK_SIZE 65536
#define TRANSFER_TIMEOUT_SEC 30 /* max silence allowed while receiving data */

/* Status codes shared with client.c. */
#define ST_OK 0
#define ST_EXISTS 1
#define ST_BAD_NAME 2
#define ST_TOO_LARGE 3
#define ST_IO_ERROR 4

#define RECV_OK 0
#define RECV_EOF 1
#define RECV_ERR -1

/*
 * Path of the file currently being received by THIS process (empty when
 * idle). If the server is stopped with Ctrl+C / SIGTERM in the middle of an
 * upload, the signal handler deletes this partial file. (SIGKILL cannot be
 * caught, so a partial file may remain in that case.)
 */
static char g_partial_path[512];

static void on_terminate(int sig)
{
    (void)sig;
    if (g_partial_path[0] != '\0')
        unlink(g_partial_path); /* unlink() is async-signal-safe */
    _exit(EXIT_FAILURE);
}

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

/* Receive exactly len bytes. RECV_EOF only if nothing was read at all. */
static int recv_all(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    size_t got = 0;

    while (got < len) {
        ssize_t n = recv(fd, p + got, len - got, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return RECV_ERR; /* includes timeout (EAGAIN) */
        }
        if (n == 0)
            return got == 0 ? RECV_EOF : RECV_ERR;
        got += (size_t)n;
    }
    return RECV_OK;
}

static int send_status(int fd, uint8_t status)
{
    return send_all(fd, &status, 1);
}

/* Decode a big-endian unsigned integer of n bytes. */
static uint64_t decode_be(const unsigned char *b, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v = (v << 8) | b[i];
    return v;
}

/*
 * A file name is acceptable only if it is a plain base name: not empty, not
 * too long, no path separators, no NUL bytes, and not "." or "..".
 * This prevents clients from writing outside the storage directory.
 */
static int is_valid_name(const char *name, size_t len)
{
    if (len == 0 || len > MAX_NAME_LEN)
        return 0;
    if (strlen(name) != len) /* embedded NUL byte */
        return 0;
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL)
        return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    return 1;
}

static void set_recv_timeout(int fd, int seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/*
 * Receive `size` bytes of file content from the socket into file `ofd`.
 * Returns 0 on success, -1 if the connection broke / timed out,
 * -2 on a local write error.
 */
static int receive_body(int fd, int ofd, uint64_t size)
{
    static unsigned char buf[CHUNK_SIZE]; /* static: one copy per process */
    uint64_t remaining = size;

    while (remaining > 0) {
        size_t want = remaining < CHUNK_SIZE ? (size_t)remaining : CHUNK_SIZE;
        ssize_t n = recv(fd, buf, want, 0);
        size_t off = 0;

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1; /* client vanished before sending everything */

        while (off < (size_t)n) { /* write() may also be partial */
            ssize_t w = write(ofd, buf + off, (size_t)n - off);
            if (w < 0) {
                if (errno == EINTR)
                    continue;
                return -2;
            }
            off += (size_t)w;
        }
        remaining -= (uint64_t)n;
    }
    return 0;
}

/*
 * Handle one upload request.
 * Returns 0 if the connection can be reused for another request, -1 if the
 * connection must be closed (client gone or stream out of sync).
 */
static int handle_upload(int fd, const char *tag)
{
    unsigned char len_buf[2], size_buf[8];
    char name[65536 + 1];
    char path[sizeof(STORAGE_DIR) + MAX_NAME_LEN + 2];
    uint16_t name_len;
    uint64_t file_size;
    int rc, ofd, body_rc;

    /* ---- Header: name length ---- */
    rc = recv_all(fd, len_buf, sizeof(len_buf));
    if (rc == RECV_EOF) {
        printf("%s client disconnected\n", tag);
        return -1;
    }
    if (rc != RECV_OK) {
        printf("%s connection error while reading header\n", tag);
        return -1;
    }
    name_len = (uint16_t)decode_be(len_buf, 2);

    /* ---- Header: name + size (read fully even if we will reject it, so
     *      that the byte stream stays in sync) ---- */
    if (name_len > 0 && recv_all(fd, name, name_len) != RECV_OK) {
        printf("%s connection error while reading file name\n", tag);
        return -1;
    }
    name[name_len] = '\0';
    if (recv_all(fd, size_buf, sizeof(size_buf)) != RECV_OK) {
        printf("%s connection error while reading file size\n", tag);
        return -1;
    }
    file_size = decode_be(size_buf, 8);

    /* ---- Validation ---- */
    if (!is_valid_name(name, name_len)) {
        printf("%s rejected: invalid file name\n", tag);
        return send_status(fd, ST_BAD_NAME) < 0 ? -1 : 0;
    }
    if (file_size > MAX_FILE_SIZE) {
        printf("%s rejected \"%s\": file too large (%llu bytes)\n", tag, name,
               (unsigned long long)file_size);
        return send_status(fd, ST_TOO_LARGE) < 0 ? -1 : 0;
    }

    if (snprintf(path, sizeof(path), "%s/%s", STORAGE_DIR, name) >=
        (int)sizeof(path)) { /* cannot happen after validation; be safe */
        printf("%s rejected: file name too long\n", tag);
        return send_status(fd, ST_BAD_NAME) < 0 ? -1 : 0;
    }

    /* O_EXCL makes "check if exists" + "create" one atomic step, so two
     * clients uploading the same name at once cannot both succeed. */
    ofd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (ofd < 0) {
        if (errno == EEXIST) {
            printf("%s rejected \"%s\": file already exists\n", tag, name);
            return send_status(fd, ST_EXISTS) < 0 ? -1 : 0;
        }
        printf("%s cannot create \"%s\": %s\n", tag, name, strerror(errno));
        return send_status(fd, ST_IO_ERROR) < 0 ? -1 : 0;
    }

    snprintf(g_partial_path, sizeof(g_partial_path), "%s", path);

    if (send_status(fd, ST_OK) < 0) {
        close(ofd);
        unlink(path);
        g_partial_path[0] = '\0';
        return -1;
    }

    /* ---- Body ---- */
    printf("%s receiving \"%s\" (%llu bytes)\n", tag, name,
           (unsigned long long)file_size);
    fflush(stdout);

    set_recv_timeout(fd, TRANSFER_TIMEOUT_SEC);
    body_rc = receive_body(fd, ofd, file_size);
    set_recv_timeout(fd, 0); /* back to blocking: user may idle between files */
    g_partial_path[0] = '\0';

    if (body_rc == 0 && close(ofd) == 0) {
        printf("%s stored \"%s\" successfully\n", tag, name);
        return send_status(fd, ST_OK) < 0 ? -1 : 0;
    }

    /* Failure: remove the partial file. */
    if (body_rc == 0)
        body_rc = -2; /* close() failed */
    else
        close(ofd);
    unlink(path);
    if (body_rc == -1) {
        printf("%s transfer of \"%s\" INTERRUPTED - partial file removed\n",
               tag, name);
    } else {
        printf("%s write error on \"%s\": %s - partial file removed\n", tag,
               name, strerror(errno));
        send_status(fd, ST_IO_ERROR); /* best effort */
    }
    return -1; /* stream is out of sync: close the connection */
}

static void handle_client(int fd, const char *tag)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_terminate;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    while (handle_upload(fd, tag) == 0)
        fflush(stdout);
    fflush(stdout);
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

    /* Shared storage directory (created on first start). */
    if (mkdir(STORAGE_DIR, 0755) < 0 && errno != EEXIST) {
        perror("mkdir " STORAGE_DIR);
        return EXIT_FAILURE;
    }

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

    printf("Server listening on port %ld, storing files in ./%s/\n", port,
           STORAGE_DIR);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int conn_fd = accept(listen_fd, (struct sockaddr *)&cli, &cli_len);
        char ip[INET_ADDRSTRLEN];
        char tag[64];
        pid_t pid;

        if (conn_fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            break;
        }
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        snprintf(tag, sizeof(tag), "[%s:%hu]", ip, ntohs(cli.sin_port));
        printf("%s client connected\n", tag);
        fflush(stdout);

        pid = fork();
        if (pid < 0) {
            perror("fork");
            close(conn_fd);
            continue;
        }
        if (pid == 0) { /* child */
            close(listen_fd);
            handle_client(conn_fd, tag);
            close(conn_fd);
            _exit(EXIT_SUCCESS);
        }
        close(conn_fd); /* parent */
    }

    close(listen_fd);
    return EXIT_FAILURE;
}
