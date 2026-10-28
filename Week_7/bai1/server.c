/*
 * server.c - Problem 1: Caesar Cipher File Server (Multithreaded TCP)
 * Course: IT4062 - Network Programming
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [BAI 1 PROTOCOL] Opcode (1 byte), Length (2 bytes), Payload binary framing.
 * - [BAI 1 THREADING] Multithreaded TCP server with POSIX pthread.
 * - [BAI 1 STORAGE] Shared directory for temporary files with unique TID/FD
 * isolation.
 * - [BAI 1 CAESAR] Caesar cipher encryption and decryption logic.
 * - [BAI 1 CLEANUP] Temporary file cleanup on finish or error.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"

#define STORAGE_DIR "storage"

/* [BAI 1 STORAGE] Ensure shared storage directory exists */
static void ensure_storage_dir(void) {
  struct stat st;
  if (stat(STORAGE_DIR, &st) == -1) {
    mkdir(STORAGE_DIR, 0755);
  }
}

/* [BAI 1 PROTOCOL] Send exactly len bytes over TCP */
static int send_all(int fd, const void *buf, size_t len) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t total = 0;
  while (total < len) {
    ssize_t n = send(fd, p + total, len - total, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      return -1;
    total += (size_t)n;
  }
  return 0;
}

/* [BAI 1 PROTOCOL] Receive exactly len bytes over TCP */
static int recv_all(int fd, void *buf, size_t len) {
  uint8_t *p = (uint8_t *)buf;
  size_t total = 0;
  while (total < len) {
    ssize_t n = recv(fd, p + total, len - total, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      return -1;
    }
    total += (size_t)n;
  }
  return 0;
}

/* [BAI 1 PROTOCOL] Send framed message: [Opcode 1B][Length 2B][Payload] */
static int send_msg(int fd, uint8_t opcode, uint16_t len, const void *payload) {
  CaesarHeader hdr;
  hdr.opcode = opcode;
  hdr.length = htons(len);

  if (send_all(fd, &hdr, sizeof(hdr)) < 0)
    return -1;
  if (len > 0 && payload != NULL) {
    if (send_all(fd, payload, len) < 0)
      return -1;
  }
  return 0;
}

/* [BAI 1 PROTOCOL] Receive framed message header and payload */
static int recv_msg(int fd, uint8_t *opcode, uint16_t *len, void *payload_buf,
                    size_t max_payload_len) {
  CaesarHeader hdr;
  if (recv_all(fd, &hdr, sizeof(hdr)) < 0)
    return -1;

  *opcode = hdr.opcode;
  *len = ntohs(hdr.length);

  if (*len > 0) {
    if (*len > max_payload_len)
      return -1;
    if (recv_all(fd, payload_buf, *len) < 0)
      return -1;
  }
  return 0;
}

/* [BAI 1 CAESAR] Caesar character transformation */
static char caesar_transform(char c, int key, int is_decrypt) {
  int k = (key % 26 + 26) % 26;
  if (is_decrypt) {
    k = (26 - k) % 26;
  }

  if (c >= 'a' && c <= 'z') {
    return (char)('a' + (c - 'a' + k) % 26);
  }
  if (c >= 'A' && c <= 'Z') {
    return (char)('A' + (c - 'A' + k) % 26);
  }
  return c;
}

/* [BAI 1 CAESAR] Process file encryption/decryption block by block */
static int process_caesar_file(const char *in_path, const char *out_path,
                               int key, int is_decrypt) {
  FILE *fin = fopen(in_path, "rb");
  if (!fin)
    return -1;

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

/* Client connection thread context */
typedef struct {
  int client_fd;
  struct sockaddr_in client_addr;
} ClientContext;

/* [BAI 1 THREADING] Worker thread handling a single client */
static void *client_handler(void *arg) {
  ClientContext *ctx = (ClientContext *)arg;
  int fd = ctx->client_fd;
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &ctx->client_addr.sin_addr, client_ip, sizeof(client_ip));
  int client_port = ntohs(ctx->client_addr.sin_port);
  unsigned long tid = (unsigned long)pthread_self();
  free(ctx);

  printf("[+] [Thread %lu] Client connected from %s:%d\n", tid, client_ip,
         client_port);

  /* [BAI 1 STORAGE] Generate unique temp file paths per client thread */
  char temp_in[256];
  char temp_out[256];
  snprintf(temp_in, sizeof(temp_in), "%s/tmp_in_%lu_%d.dat", STORAGE_DIR, tid,
           fd);
  snprintf(temp_out, sizeof(temp_out), "%s/tmp_out_%lu_%d.dat", STORAGE_DIR,
           tid, fd);

  while (1) {
    /* B1: Wait for Encrypt/Decrypt request: Opcode = 0 or 1, Payload = Key */
    uint8_t opcode;
    uint16_t length;
    char key_str[128];
    memset(key_str, 0, sizeof(key_str));

    if (recv_msg(fd, &opcode, &length, key_str, sizeof(key_str) - 1) < 0) {
      break;
    }

    if (opcode != OP_ENCRYPT && opcode != OP_DECRYPT) {
      printf("[-] [Thread %lu] Invalid operation opcode: %u\n", tid, opcode);
      send_msg(fd, OP_ERROR, 0, NULL);
      continue;
    }

    int key = atoi(key_str);
    int is_decrypt = (opcode == OP_DECRYPT);
    printf("[*] [Thread %lu] Request: %s (Key: %d)\n", tid,
           is_decrypt ? "DECRYPT" : "ENCRYPT", key);

    /* B2: Receive file data chunks: Opcode = 2, Length > 0 until Length == 0 */
    FILE *fin = fopen(temp_in, "wb");
    if (!fin) {
      perror("fopen temp_in");
      send_msg(fd, OP_ERROR, 0, NULL);
      continue;
    }

    int upload_ok = 1;
    uint64_t total_recv = 0;
    char chunk_buf[MAX_CHUNK_SIZE];

    while (1) {
      uint8_t data_op;
      uint16_t data_len;
      if (recv_msg(fd, &data_op, &data_len, chunk_buf, sizeof(chunk_buf)) < 0) {
        upload_ok = 0;
        break;
      }

      if (data_op != OP_DATA) {
        printf("[-] [Thread %lu] Expected data opcode, got: %u\n", tid,
               data_op);
        upload_ok = 0;
        break;
      }

      if (data_len == 0) {
        break;
      }

      if (fwrite(chunk_buf, 1, data_len, fin) != data_len) {
        perror("fwrite temp_in");
        upload_ok = 0;
        break;
      }
      total_recv += data_len;
    }
    fclose(fin);

    if (!upload_ok) {
      send_msg(fd, OP_ERROR, 0, NULL);
      unlink(temp_in);
      continue;
    }

    printf("[*] [Thread %lu] Received %lu bytes of file data.\n", tid,
           (unsigned long)total_recv);

    /* B3: Process Caesar Cipher on temp file */
    if (process_caesar_file(temp_in, temp_out, key, is_decrypt) < 0) {
      printf("[-] [Thread %lu] Error processing Caesar cipher.\n", tid);
      send_msg(fd, OP_ERROR, 0, NULL);
      unlink(temp_in);
      unlink(temp_out);
      continue;
    }

    /* B4: Send result file back to client */
    FILE *fres = fopen(temp_out, "rb");
    if (!fres) {
      perror("fopen temp_out");
      send_msg(fd, OP_ERROR, 0, NULL);
      unlink(temp_in);
      unlink(temp_out);
      continue;
    }

    int send_ok = 1;
    uint64_t total_sent = 0;
    size_t r;
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
      printf("[+] [Thread %lu] Result sent successfully (%lu bytes).\n", tid,
             (unsigned long)total_sent);
    }

    /* [BAI 1 CLEANUP] B5: Delete temporary files */
    unlink(temp_in);
    unlink(temp_out);
  }

  unlink(temp_in);
  unlink(temp_out);

  printf("[-] [Thread %lu] Client %s:%d disconnected.\n", tid, client_ip,
         client_port);
  close(fd);
  return NULL;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <Port_Number>\n", argv[0]);
    fprintf(stderr, "Example: %s 5500\n", argv[0]);
    return EXIT_FAILURE;
  }

  int port = atoi(argv[1]);
  if (port <= 1024 || port > 65535) {
    fprintf(
        stderr,
        "Error: Invalid port number '%s'. Must be between 1025 and 65535.\n",
        argv[1]);
    return EXIT_FAILURE;
  }

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

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
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
  printf("[*] Multithreaded model active (POSIX pthread).\n");
  printf("[*] Shared temporary directory: ./%s/\n", STORAGE_DIR);
  printf("====================================================\n");

  while (1) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
      if (errno == EINTR)
        continue;
      perror("accept");
      continue;
    }

    ClientContext *ctx = (ClientContext *)malloc(sizeof(ClientContext));
    if (!ctx) {
      close(client_fd);
      continue;
    }
    ctx->client_fd = client_fd;
    ctx->client_addr = client_addr;

    pthread_t tid;
    if (pthread_create(&tid, NULL, client_handler, ctx) != 0) {
      perror("pthread_create");
      free(ctx);
      close(client_fd);
    } else {
      pthread_detach(tid);
    }
  }

  close(server_fd);
  return EXIT_SUCCESS;
}
