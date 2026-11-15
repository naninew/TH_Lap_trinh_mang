/*
 * server.c - Problem 1: Caesar Cipher File Server using poll() I/O Multiplexing
 * Course: IT4062 - Network Programming (Week 7 / Week 8)
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [POLL MULTIPLEXING] Single-threaded TCP server using poll() to handle concurrent clients.
 * - [PROTOCOL FRAMING] Message format: [Opcode 1B][Length 2B (Big-Endian)][Payload].
 * - [STEP 1 - REQUEST] Opcode 0 (Encrypt) / 1 (Decrypt) with key in Payload.
 * - [STEP 2 - UPLOAD] Opcode 2 with file data chunks, ending with Length = 0.
 * - [STEP 3 - PROCESS CAESAR] Save to temporary file in storage/, execute Caesar cipher.
 * - [STEP 4 - SEND RESULT] Send result file chunks with Opcode 2, ending with Length = 0.
 * - [STEP 5 - CLEANUP] Automated deletion (unlink) of temporary and result files.
 * - [STORAGE ISOLATION] Shared storage/ directory with unique filenames (tmp_in_<fd>.dat).
 * - [CLI ARGUMENTS] Command-line syntax: ./server Port_Number
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>

#include "protocol.h"

#define MAX_CLIENTS 128
#define STORAGE_DIR "storage"

/* Client protocol session state */
typedef enum {
    STATE_WAIT_REQUEST,
    STATE_RECEIVING_DATA
} ClientState;

typedef struct {
    int active;
    ClientState state;
    int is_decrypt;
    int key;
    char temp_in[256];
    char temp_out[256];
    FILE *fp_in;
    uint64_t total_received;
} ClientSession;

static ClientSession g_sessions[MAX_CLIENTS];
static struct pollfd g_fds[MAX_CLIENTS];
static int g_nfds = 0;

/* [STORAGE ISOLATION] Ensure shared storage directory exists */
static void ensure_storage_dir(void) {
    struct stat st;
    if (stat(STORAGE_DIR, &st) == -1) {
        mkdir(STORAGE_DIR, 0755);
    }
}

/* [PROTOCOL FRAMING] Send exactly len bytes over TCP */
static int send_all(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, p + total, len - total, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

/* [PROTOCOL FRAMING] Receive exactly len bytes over TCP */
static int recv_all(int fd, void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        total += (size_t)n;
    }
    return 0;
}

/* [PROTOCOL FRAMING] Send framed message: [Opcode 1B][Length 2B][Payload] */
static int send_msg(int fd, uint8_t opcode, uint16_t len, const void *payload) {
    CaesarHeader hdr;
    hdr.opcode = opcode;
    hdr.length = htons(len);

    if (send_all(fd, &hdr, sizeof(hdr)) < 0) return -1;
    if (len > 0 && payload != NULL) {
        if (send_all(fd, payload, len) < 0) return -1;
    }
    return 0;
}

/* [PROTOCOL FRAMING] Receive framed message header and payload */
static int recv_msg(int fd, uint8_t *opcode, uint16_t *len, void *payload_buf, size_t max_payload_len) {
    CaesarHeader hdr;
    if (recv_all(fd, &hdr, sizeof(hdr)) < 0) return -1;

    *opcode = hdr.opcode;
    *len = ntohs(hdr.length);

    if (*len > 0) {
        if (*len > max_payload_len) return -1;
        if (recv_all(fd, payload_buf, *len) < 0) return -1;
    }
    return 0;
}

/* [CAESAR CIPHER] In-place character shift modulo 26 */
static char caesar_transform(char c, int key, int is_decrypt) {
    int norm_k = (key % 26 + 26) % 26;
    if (is_decrypt) {
        norm_k = (26 - norm_k) % 26;
    }

    if (c >= 'a' && c <= 'z') {
        return (char)('a' + (c - 'a' + norm_k) % 26);
    }
    if (c >= 'A' && c <= 'Z') {
        return (char)('A' + (c - 'A' + norm_k) % 26);
    }
    return c; /* Digits, spaces, punctuation, newlines remain unchanged */
}

/* [STEP 3 - PROCESS CAESAR] Process temporary input file and generate result file */
static int process_caesar_file(const char *in_path, const char *out_path, int key, int is_decrypt) {
    FILE *fin = fopen(in_path, "rb");
    if (!fin) return -1;

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        fclose(fin);
        return -1;
    }

    char buf[MAX_CHUNK_SIZE];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), fin)) > 0) {
        for (size_t i = 0; i < r; i++) {
            buf[i] = caesar_transform(buf[i], key, is_decrypt);
        }
        if (fwrite(buf, 1, r, fout) != r) {
            fclose(fin);
            fclose(fout);
            return -1;
        }
    }

    fclose(fin);
    fclose(fout);
    return 0;
}

/* [STEP 5 - CLEANUP] Clean up client session and delete temporary files */
static void cleanup_client(int index) {
    int fd = g_fds[index].fd;
    printf("[-] Client on fd %d disconnected. Cleaning up resources.\n", fd);

    if (g_sessions[index].fp_in != NULL) {
        fclose(g_sessions[index].fp_in);
        g_sessions[index].fp_in = NULL;
    }

    /* Unlink temporary files */
    if (g_sessions[index].temp_in[0] != '\0') {
        unlink(g_sessions[index].temp_in);
        g_sessions[index].temp_in[0] = '\0';
    }
    if (g_sessions[index].temp_out[0] != '\0') {
        unlink(g_sessions[index].temp_out);
        g_sessions[index].temp_out[0] = '\0';
    }

    close(fd);
    g_sessions[index].active = 0;
    g_sessions[index].state = STATE_WAIT_REQUEST;

    /* Compact pollfd and session arrays */
    for (int i = index; i < g_nfds - 1; i++) {
        g_fds[i] = g_fds[i + 1];
        g_sessions[i] = g_sessions[i + 1];
    }
    g_nfds--;
}

/* [POLL MULTIPLEXING] Handle I/O activity on an active client socket */
static void handle_client_data(int index) {
    int fd = g_fds[index].fd;
    ClientSession *session = &g_sessions[index];

    uint8_t opcode;
    uint16_t length;
    char payload[MAX_CHUNK_SIZE + 1];

    if (recv_msg(fd, &opcode, &length, payload, sizeof(payload) - 1) < 0) {
        cleanup_client(index);
        return;
    }

    if (session->state == STATE_WAIT_REQUEST) {
        /* [STEP 1 - REQUEST] Waiting for Encrypt/Decrypt request (Opcode = 0 or 1) */
        if (opcode != OP_ENCRYPT && opcode != OP_DECRYPT) {
            printf("[-] [fd %d] Invalid opcode %u in STATE_WAIT_REQUEST. Sending error.\n", fd, opcode);
            send_msg(fd, OP_ERROR, 0, NULL);
            cleanup_client(index);
            return;
        }

        payload[length] = '\0';
        session->key = atoi(payload);
        session->is_decrypt = (opcode == OP_DECRYPT);

        /* [STORAGE ISOLATION] Generate unique temporary filenames based on socket fd */
        snprintf(session->temp_in, sizeof(session->temp_in), "%s/tmp_in_%d.dat", STORAGE_DIR, fd);
        snprintf(session->temp_out, sizeof(session->temp_out), "%s/tmp_out_%d.dat", STORAGE_DIR, fd);

        session->fp_in = fopen(session->temp_in, "wb");
        if (!session->fp_in) {
            perror("fopen temp_in");
            send_msg(fd, OP_ERROR, 0, NULL);
            cleanup_client(index);
            return;
        }

        session->total_received = 0;
        session->state = STATE_RECEIVING_DATA;
        printf("[*] [fd %d] Request accepted: %s (Key: %d). Awaiting file data.\n",
               fd, session->is_decrypt ? "DECRYPT" : "ENCRYPT", session->key);
    }
    else if (session->state == STATE_RECEIVING_DATA) {
        /* [STEP 2 - UPLOAD] Receiving file data chunks (Opcode = 2) */
        if (opcode != OP_DATA) {
            printf("[-] [fd %d] Expected OP_DATA (2), got %u. Sending error.\n", fd, opcode);
            send_msg(fd, OP_ERROR, 0, NULL);
            cleanup_client(index);
            return;
        }

        if (length > 0) {
            /* Chunk data: write to temporary input file */
            if (fwrite(payload, 1, length, session->fp_in) != length) {
                perror("fwrite temp_in");
                send_msg(fd, OP_ERROR, 0, NULL);
                cleanup_client(index);
                return;
            }
            session->total_received += length;
        } else {
            /* EOF marker (Length == 0): upload complete */
            fclose(session->fp_in);
            session->fp_in = NULL;
            printf("[*] [fd %d] File upload complete (%lu bytes). Processing Caesar cipher...\n",
                   fd, (unsigned long)session->total_received);

            /* [STEP 3 - PROCESS CAESAR] Execute transformation */
            if (process_caesar_file(session->temp_in, session->temp_out, session->key, session->is_decrypt) < 0) {
                printf("[-] [fd %d] Failed to process Caesar cipher.\n", fd);
                send_msg(fd, OP_ERROR, 0, NULL);
                cleanup_client(index);
                return;
            }

            /* [STEP 4 - SEND RESULT] Send result file chunks back to client */
            FILE *fres = fopen(session->temp_out, "rb");
            if (!fres) {
                perror("fopen temp_out");
                send_msg(fd, OP_ERROR, 0, NULL);
                cleanup_client(index);
                return;
            }

            char chunk_buf[MAX_CHUNK_SIZE];
            size_t r;
            uint64_t total_sent = 0;
            int send_ok = 1;

            while ((r = fread(chunk_buf, 1, sizeof(chunk_buf), fres)) > 0) {
                if (send_msg(fd, OP_DATA, (uint16_t)r, chunk_buf) < 0) {
                    send_ok = 0;
                    break;
                }
                total_sent += r;
            }
            fclose(fres);

            if (send_ok) {
                /* Send final EOF marker (Opcode = 2, Length = 0) */
                send_msg(fd, OP_DATA, 0, NULL);
                printf("[+] [fd %d] Result transmitted successfully (%lu bytes).\n",
                       fd, (unsigned long)total_sent);
            }

            /* [STEP 5 - CLEANUP] Remove temporary files */
            unlink(session->temp_in);
            unlink(session->temp_out);
            session->temp_in[0] = '\0';
            session->temp_out[0] = '\0';

            /* Reset session state for subsequent requests on this connection */
            session->state = STATE_WAIT_REQUEST;
        }
    }
}

/* [CLI ARGUMENTS] Main entry point: ./server Port_Number */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port_Number>\n", argv[0]);
        fprintf(stderr, "Example: %s 5500\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 1024 || port > 65535) {
        fprintf(stderr, "Error: Invalid port number '%s'. Must be between 1025 and 65535.\n", argv[1]);
        return EXIT_FAILURE;
    }

    signal(SIGPIPE, SIG_IGN);
    ensure_storage_dir();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("====================================================\n");
    printf("[*] Caesar Cipher TCP Server listening on port %d...\n", port);
    printf("[*] I/O Multiplexing active using poll().\n");
    printf("[*] Shared temporary directory: ./%s/\n", STORAGE_DIR);
    printf("====================================================\n");

    /* [POLL MULTIPLEXING] Initialize pollfd array */
    memset(g_sessions, 0, sizeof(g_sessions));
    g_fds[0].fd = server_fd;
    g_fds[0].events = POLLIN;
    g_nfds = 1;

    while (1) {
        int poll_count = poll(g_fds, g_nfds, -1);
        if (poll_count < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        /* Check listening socket for new connections */
        if (g_fds[0].revents & POLLIN) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

            if (client_fd >= 0) {
                if (g_nfds < MAX_CLIENTS) {
                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                    int client_port = ntohs(client_addr.sin_port);

                    printf("[+] New client connected from %s:%d (Assigned fd %d)\n",
                           client_ip, client_port, client_fd);

                    g_fds[g_nfds].fd = client_fd;
                    g_fds[g_nfds].events = POLLIN | POLLHUP | POLLERR;
                    g_fds[g_nfds].revents = 0;

                    memset(&g_sessions[g_nfds], 0, sizeof(ClientSession));
                    g_sessions[g_nfds].active = 1;
                    g_sessions[g_nfds].state = STATE_WAIT_REQUEST;
                    g_nfds++;
                } else {
                    fprintf(stderr, "[-] Max clients reached (%d). Rejecting connection.\n", MAX_CLIENTS);
                    close(client_fd);
                }
            }
        }

        /* Check active client sockets for events */
        for (int i = 1; i < g_nfds; i++) {
            if (g_fds[i].revents & (POLLHUP | POLLERR | POLLNVAL)) {
                cleanup_client(i);
                i--; /* Adjust index after compaction */
                continue;
            }

            if (g_fds[i].revents & POLLIN) {
                handle_client_data(i);
            }
        }
    }

    close(server_fd);
    return EXIT_SUCCESS;
}

