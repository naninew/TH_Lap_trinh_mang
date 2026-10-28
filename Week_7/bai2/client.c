/*
 * client.c - Problem 2: TCP User Authentication Client
 * Course: IT4062 - Network Programming
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [BAI 2 REQUIREMENT 1] Client window session handles single active login.
 * - [BAI 2 REQUIREMENT 2] Independent client instance connecting to server.
 * - [BAI 2 REQUIREMENT 5] Command-line syntax: ./client IP_Addr Port_Number
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

#define BUFFER_SIZE 1024
#define MAX_STR_LEN 64

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

static int send_all(int fd, const char *buf, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t n = send(fd, buf + total, len - total, 0);
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

static int recv_line(int fd, char *buf, size_t max_len) {
  size_t idx = 0;
  while (idx + 1 < max_len) {
    char c;
    ssize_t n = recv(fd, &c, 1, 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR)
        continue;
      return -1;
    }
    if (c == '\n')
      break;
    buf[idx++] = c;
  }
  buf[idx] = '\0';
  trim_line(buf);
  return (int)idx;
}

static int send_command(int fd, const char *cmd) {
  char out[BUFFER_SIZE];
  snprintf(out, sizeof(out), "%s\n", cmd);
  return send_all(fd, out, strlen(out));
}

static void print_menu(void) {
  printf("\n=========================================\n");
  printf("     TCP USER AUTHENTICATION CLIENT     \n");
  printf("=========================================\n");
  printf(" 1. Log in\n");
  printf(" 2. Log out\n");
  printf(" 3. Check current user status (WhoAmI)\n");
  printf(" 4. Exit\n");
  printf("-----------------------------------------\n");
  printf("Select option [1-4] or type command: ");
  fflush(stdout);
}

/* [BAI 2 REQUIREMENT 5] Main entry point: ./client IP_Addr Port_Number */
int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <IP_Addr> <Port_Number>\n", argv[0]);
    fprintf(stderr, "Example: %s 127.0.0.1 5500\n", argv[0]);
    return EXIT_FAILURE;
  }

  const char *server_ip = argv[1];
  int port = atoi(argv[2]);
  if (port <= 1024 || port > 65535) {
    fprintf(
        stderr,
        "Error: Invalid port number '%s'. Must be between 1025 and 65535.\n",
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

  char banner[BUFFER_SIZE];
  if (recv_line(sockfd, banner, sizeof(banner)) > 0) {
    printf("[Server] %s\n", banner);
  }

  char input[BUFFER_SIZE];
  char reply[BUFFER_SIZE];

  while (1) {
    print_menu();
    if (!fgets(input, sizeof(input), stdin)) {
      printf("\nExiting...\n");
      send_command(sockfd, "QUIT");
      break;
    }

    trim_line(input);
    if (input[0] == '\0')
      continue;

    if (strcmp(input, "1") == 0) {
      /* [BAI 2 REQUIREMENT 1] User login flow */
      char user_id[MAX_STR_LEN] = "";
      char password[MAX_STR_LEN] = "";

      printf("Enter UserID: ");
      fflush(stdout);
      if (!fgets(user_id, sizeof(user_id), stdin))
        break;
      trim_line(user_id);

      printf("Enter Password: ");
      fflush(stdout);
      if (!fgets(password, sizeof(password), stdin))
        break;
      trim_line(password);

      char login_cmd[BUFFER_SIZE];
      snprintf(login_cmd, sizeof(login_cmd), "LOGIN %s %s", user_id, password);
      if (send_command(sockfd, login_cmd) < 0) {
        printf("Error: Failed to send command to server.\n");
        break;
      }

      if (recv_line(sockfd, reply, sizeof(reply)) < 0) {
        printf("Error: Server disconnected.\n");
        break;
      }
      printf("[Server] %s\n", reply);
    } else if (strcmp(input, "2") == 0) {
      /* [BAI 2 REQUIREMENT 1] User logout flow */
      if (send_command(sockfd, "LOGOUT") < 0)
        break;
      if (recv_line(sockfd, reply, sizeof(reply)) < 0)
        break;
      printf("[Server] %s\n", reply);
    } else if (strcmp(input, "3") == 0) {
      if (send_command(sockfd, "WHOAMI") < 0)
        break;
      if (recv_line(sockfd, reply, sizeof(reply)) < 0)
        break;
      printf("[Server] %s\n", reply);
    } else if (strcmp(input, "4") == 0 || strcasecmp(input, "exit") == 0 ||
               strcasecmp(input, "quit") == 0) {
      send_command(sockfd, "QUIT");
      if (recv_line(sockfd, reply, sizeof(reply)) > 0) {
        printf("[Server] %s\n", reply);
      }
      break;
    } else {
      if (send_command(sockfd, input) < 0)
        break;
      if (recv_line(sockfd, reply, sizeof(reply)) < 0)
        break;
      printf("[Server] %s\n", reply);
    }
  }

  close(sockfd);
  return EXIT_SUCCESS;
}
