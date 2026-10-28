/*
 * client.c - Problem 1: Caesar Cipher File Client (TCP)
 * Course: IT4062 - Network Programming
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [BAI 1 PROTOCOL] Opcode (1 byte), Length (2 bytes), Payload framing.
 * - [BAI 1 CLIENT] Interactive CLI menu or direct command-line arguments.
 * - [BAI 1 TRANSFER] Upload local file in chunks, receive transformed file.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "protocol.h"

static void trim_line(char *str) {
  if (!str)
    return;
  int len = (int)strlen(str);
  while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' ||
                     isspace((unsigned char)str[len - 1]))) {
    str[len - 1] = '\0';
    len--;
  }
}

/* [BAI 1 PROTOCOL] Send exactly len bytes over TCP */
static int send_all(int fd, const void *buf, size_t len) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t total = 0;
  while (total < len) {
    ssize_t n = send(fd, p + total, len - total, 0);
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

/* [BAI 1 TRANSFER] Perform Caesar file operation */
static int do_caesar_operation(int sockfd, int is_decrypt, int key,
                               const char *in_path, const char *out_path) {
  FILE *fin = fopen(in_path, "rb");
  if (!fin) {
    fprintf(stderr, "Error: Cannot open input file '%s': %s\n", in_path,
            strerror(errno));
    return -1;
  }

  FILE *fout = fopen(out_path, "wb");
  if (!fout) {
    fprintf(stderr, "Error: Cannot open destination file '%s': %s\n", out_path,
            strerror(errno));
    fclose(fin);
    return -1;
  }

  /* B1: Send request (Opcode = 0 for encrypt, 1 for decrypt) with key */
  char key_str[32];
  snprintf(key_str, sizeof(key_str), "%d", key);
  uint8_t req_op = is_decrypt ? OP_DECRYPT : OP_ENCRYPT;

  printf("[*] Sending %s request with key %d...\n",
         is_decrypt ? "DECRYPTION" : "ENCRYPTION", key);
  if (send_msg(sockfd, req_op, (uint16_t)strlen(key_str), key_str) < 0) {
    fprintf(stderr, "Error: Failed to send request header.\n");
    fclose(fin);
    fclose(fout);
    unlink(out_path);
    return -1;
  }

  /* B2: Send input file data chunks (Opcode = 2, Length > 0), ending with
   * Length = 0 */
  printf("[*] Uploading input file '%s'...\n", in_path);
  char chunk[MAX_CHUNK_SIZE];
  size_t r;
  uint64_t total_sent = 0;
  while ((r = fread(chunk, 1, sizeof(chunk), fin)) > 0) {
    if (send_msg(sockfd, OP_DATA, (uint16_t)r, chunk) < 0) {
      fprintf(stderr, "Error: Connection lost while sending file.\n");
      fclose(fin);
      fclose(fout);
      unlink(out_path);
      return -1;
    }
    total_sent += r;
  }
  fclose(fin);

  /* Send EOF marker */
  if (send_msg(sockfd, OP_DATA, 0, NULL) < 0) {
    fprintf(stderr, "Error: Failed to send EOF marker.\n");
    fclose(fout);
    unlink(out_path);
    return -1;
  }

  printf("[*] Sent %lu bytes. Waiting for server result...\n",
         (unsigned long)total_sent);

  /* B4: Receive result file from server (Opcode = 2, Length > 0) until Length =
   * 0 */
  uint64_t total_recv = 0;
  int success = 1;
  while (1) {
    uint8_t res_op;
    uint16_t res_len;
    if (recv_msg(sockfd, &res_op, &res_len, chunk, sizeof(chunk)) < 0) {
      fprintf(stderr, "Error: Connection lost while receiving result.\n");
      success = 0;
      break;
    }

    if (res_op == OP_ERROR) {
      fprintf(stderr, "[-] Server returned an error (Opcode = 3).\n");
      success = 0;
      break;
    }

    if (res_op != OP_DATA) {
      fprintf(stderr, "[-] Unexpected opcode from server: %u\n", res_op);
      success = 0;
      break;
    }

    if (res_len == 0) {
      break;
    }

    if (fwrite(chunk, 1, res_len, fout) != res_len) {
      perror("fwrite out_path");
      success = 0;
      break;
    }
    total_recv += res_len;
  }
  fclose(fout);

  if (success) {
    printf(
        "[SUCCESS] File processed successfully! Saved as: '%s' (%lu bytes)\n",
        out_path, (unsigned long)total_recv);
    return 0;
  } else {
    unlink(out_path);
    return -1;
  }
}

static void print_menu(void) {
  printf("\n=========================================\n");
  printf("     CAESAR CIPHER TCP CLIENT (BAI 1)    \n");
  printf("=========================================\n");
  printf(" 1. Encrypt a file (Mã hóa file)\n");
  printf(" 2. Decrypt a file (Giải mã file)\n");
  printf(" 3. Exit\n");
  printf("-----------------------------------------\n");
  printf("Select option [1-3]: ");
  fflush(stdout);
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(stderr,
            "Usage: %s <IP_Addr> <Port_Number> [encrypt|decrypt] [key] "
            "[input_file] [output_file]\n",
            argv[0]);
    fprintf(stderr, "Interactive mode: %s 127.0.0.1 5500\n", argv[0]);
    fprintf(
        stderr,
        "Direct command:   %s 127.0.0.1 5500 encrypt 3 plain.txt cipher.txt\n",
        argv[0]);
    fprintf(stderr,
            "Direct command:   %s 127.0.0.1 5500 decrypt 3 cipher.txt "
            "restored.txt\n",
            argv[0]);
    return EXIT_FAILURE;
  }

  const char *server_ip = argv[1];
  int port = atoi(argv[2]);
  if (port <= 1024 || port > 65535) {
    fprintf(stderr,
            "Error: Invalid port '%s'. Must be between 1025 and 65535.\n",
            argv[2]);
    return EXIT_FAILURE;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons((uint16_t)port);

  if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
    fprintf(stderr, "Error: Invalid IPv4 address '%s'.\n", server_ip);
    return EXIT_FAILURE;
  }

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("socket");
    return EXIT_FAILURE;
  }

  if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("connect");
    close(sockfd);
    return EXIT_FAILURE;
  }

  if (argc >= 7) {
    const char *op_str = argv[3];
    int key = atoi(argv[4]);
    const char *in_file = argv[5];
    const char *out_file = argv[6];

    int is_decrypt =
        (strcasecmp(op_str, "decrypt") == 0 || strcmp(op_str, "1") == 0);
    int res = do_caesar_operation(sockfd, is_decrypt, key, in_file, out_file);
    close(sockfd);
    return (res == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  char line[256];
  while (1) {
    print_menu();
    if (!fgets(line, sizeof(line), stdin))
      break;
    trim_line(line);
    if (line[0] == '\0')
      continue;

    if (strcmp(line, "1") == 0 || strcmp(line, "2") == 0) {
      int is_decrypt = (strcmp(line, "2") == 0);

      char key_input[64] = "";
      char in_path[256] = "";
      char out_path[256] = "";

      printf("Enter Caesar key (integer, e.g. 3): ");
      fflush(stdout);
      if (!fgets(key_input, sizeof(key_input), stdin))
        break;
      trim_line(key_input);
      int key = atoi(key_input);

      printf("Enter input file path: ");
      fflush(stdout);
      if (!fgets(in_path, sizeof(in_path), stdin))
        break;
      trim_line(in_path);

      printf("Enter output destination file path: ");
      fflush(stdout);
      if (!fgets(out_path, sizeof(out_path), stdin))
        break;
      trim_line(out_path);

      if (in_path[0] != '\0' && out_path[0] != '\0') {
        do_caesar_operation(sockfd, is_decrypt, key, in_path, out_path);
      }
    } else if (strcmp(line, "3") == 0 || strcasecmp(line, "exit") == 0 ||
               strcasecmp(line, "quit") == 0) {
      printf("Goodbye.\n");
      break;
    } else {
      printf("Invalid selection. Please choose 1, 2, or 3.\n");
    }
  }

  close(sockfd);
  return EXIT_SUCCESS;
}
