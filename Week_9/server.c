/*
 * server.c - Problem 2: TCP User Authentication Server using select() I/O
 * Multiplexing Course: IT4062 - Network Programming (Week 7 - Problem 2 / Week
 * 9) Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [IO MULTIPLEXING - SELECT] Single-threaded server managing concurrent
 * clients using select().
 * - [REQUIREMENT 1] Each client window can only log in to 1 account.
 * - [REQUIREMENT 2] An account can log in simultaneously on multiple client
 * windows.
 * - [REQUIREMENT 3] Lock account after 3 consecutive failed password attempts &
 * sync to disk.
 * - [REQUIREMENT 4] Parse and persist user accounts in account.txt (UserID
 * Password Status).
 * - [REQUIREMENT 5] Command-line syntax: ./server Port_Number
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_ACCOUNTS 256
#define MAX_STR_LEN 64
#define BUFFER_SIZE 1024
#define MAX_FAILED_ATTEMPTS 3
#define DEFAULT_ACCOUNT_FILE "account.txt"

typedef struct {
  char user_id[MAX_STR_LEN];
  char password[MAX_STR_LEN];
  int status;
  int failed_attempts;
} Account;

typedef struct {
  int active;
  int is_logged_in;
  char session_user[MAX_STR_LEN];
  char client_ip[INET_ADDRSTRLEN];
  int client_port;
} ClientSession;

static Account g_accounts[MAX_ACCOUNTS];
static int g_account_count = 0;
static char g_account_file[256] = DEFAULT_ACCOUNT_FILE;
static ClientSession g_sessions[FD_SETSIZE];

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

static int load_accounts(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    fp = fopen("Week_9/account.txt", "r");
    if (fp) {
      strncpy(g_account_file, "Week_9/account.txt", sizeof(g_account_file) - 1);
    } else {
      return -1;
    }
  } else {
    strncpy(g_account_file, filename, sizeof(g_account_file) - 1);
  }

  g_account_count = 0;
  char line[BUFFER_SIZE];
  while (fgets(line, sizeof(line), fp)) {
    trim_line(line);
    if (line[0] == '\0' || line[0] == '#')
      continue;

    char u[MAX_STR_LEN], p[MAX_STR_LEN];
    int s = 1;
    if (sscanf(line, "%63s %63s %d", u, p, &s) == 3) {
      if (g_account_count < MAX_ACCOUNTS) {
        strncpy(g_accounts[g_account_count].user_id, u, MAX_STR_LEN - 1);
        g_accounts[g_account_count].user_id[MAX_STR_LEN - 1] = '\0';
        strncpy(g_accounts[g_account_count].password, p, MAX_STR_LEN - 1);
        g_accounts[g_account_count].password[MAX_STR_LEN - 1] = '\0';
        g_accounts[g_account_count].status = s;
        g_accounts[g_account_count].failed_attempts = 0;
        g_account_count++;
      }
    }
  }
  fclose(fp);
  return g_account_count;
}

static int save_accounts(void) {
  char tmp_file[300];
  snprintf(tmp_file, sizeof(tmp_file), "%s.tmp", g_account_file);

  FILE *fp = fopen(tmp_file, "w");
  if (!fp) {
    perror("fopen tmp_file");
    return -1;
  }

  for (int i = 0; i < g_account_count; i++) {
    fprintf(fp, "%s %s %d\n", g_accounts[i].user_id, g_accounts[i].password,
            g_accounts[i].status);
  }
  fflush(fp);
  fclose(fp);

  if (rename(tmp_file, g_account_file) != 0) {
    perror("rename account file");
    return -1;
  }
  return 0;
}

static int find_account_index(const char *user_id) {
  for (int i = 0; i < g_account_count; i++) {
    if (strcmp(g_accounts[i].user_id, user_id) == 0) {
      return i;
    }
  }
  return -1;
}

static int send_all(int fd, const char *buf, size_t len) {
  size_t total = 0;
  while (total < len) {
    ssize_t n = send(fd, buf + total, len - total, MSG_NOSIGNAL);
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

static int send_msg(int fd, const char *fmt, ...) {
  char out[BUFFER_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(out, sizeof(out), fmt, args);
  va_end(args);

  size_t len = strlen(out);
  if (len == 0 || out[len - 1] != '\n') {
    if (len + 2 < sizeof(out)) {
      out[len] = '\n';
      out[len + 1] = '\0';
      len++;
    }
  }
  return send_all(fd, out, len);
}

static void close_client(int fd, fd_set *master_set) {
  printf("[-] Client %s:%d (fd %d) disconnected.\n", g_sessions[fd].client_ip,
         g_sessions[fd].client_port, fd);
  close(fd);
  FD_CLR(fd, master_set);
  memset(&g_sessions[fd], 0, sizeof(ClientSession));
}

static void process_client_command(int fd, const char *line,
                                   fd_set *master_set) {
  char cmd[MAX_STR_LEN] = "";
  char arg1[MAX_STR_LEN] = "";
  char arg2[MAX_STR_LEN] = "";
  int tokens = sscanf(line, "%63s %63s %63s", cmd, arg1, arg2);
  if (tokens < 1)
    return;

  for (int i = 0; cmd[i]; i++)
    cmd[i] = (char)toupper((unsigned char)cmd[i]);

  ClientSession *session = &g_sessions[fd];

  if (strcmp(cmd, "LOGIN") == 0) {
    if (session->is_logged_in) {
      send_msg(fd, "401 Already logged in as '%s'. Please logout first.",
               session->session_user);
      return;
    }

    if (tokens < 3) {
      send_msg(fd, "400 Syntax error. Usage: LOGIN <UserID> <Password>");
      return;
    }

    int idx = find_account_index(arg1);

    if (idx == -1) {
      send_msg(fd, "404 Account '%s' does not exist.", arg1);
      return;
    }

    if (g_accounts[idx].status == 0) {
      send_msg(fd, "403 Account '%s' is blocked.", arg1);
      return;
    }

    if (strcmp(g_accounts[idx].password, arg2) == 0) {
      g_accounts[idx].failed_attempts = 0;

      session->is_logged_in = 1;
      strncpy(session->session_user, g_accounts[idx].user_id,
              sizeof(session->session_user) - 1);
      session->session_user[sizeof(session->session_user) - 1] = '\0';

      printf("[*] User '%s' logged in successfully from %s:%d (fd %d)\n",
             session->session_user, session->client_ip, session->client_port,
             fd);
      send_msg(fd, "200 Login successful. Welcome, %s!", session->session_user);
    } else {
      g_accounts[idx].failed_attempts++;
      int attempts = g_accounts[idx].failed_attempts;

      printf("[-] User '%s' wrong password (attempt %d/%d) from %s:%d\n", arg1,
             attempts, MAX_FAILED_ATTEMPTS, session->client_ip,
             session->client_port);

      if (attempts >= MAX_FAILED_ATTEMPTS) {
        g_accounts[idx].status = 0;
        save_accounts();

        printf("[!] Account '%s' is now BLOCKED due to %d consecutive failed "
               "attempts!\n",
               arg1, attempts);
        send_msg(
            fd,
            "405 Password incorrect (%d/%d). Account '%s' has been BLOCKED!",
            attempts, MAX_FAILED_ATTEMPTS, arg1);
      } else {
        send_msg(fd, "402 Password incorrect. Attempt %d/%d. Please try again.",
                 attempts, MAX_FAILED_ATTEMPTS);
      }
    }
  } else if (strcmp(cmd, "LOGOUT") == 0) {
    if (!session->is_logged_in) {
      send_msg(fd, "400 You are not logged in.");
    } else {
      printf("[*] User '%s' logged out from %s:%d (fd %d)\n",
             session->session_user, session->client_ip, session->client_port,
             fd);
      session->is_logged_in = 0;
      session->session_user[0] = '\0';
      send_msg(fd, "200 Logged out successfully.");
    }
  } else if (strcmp(cmd, "WHOAMI") == 0 || strcmp(cmd, "STATUS") == 0) {
    if (session->is_logged_in) {
      send_msg(fd, "200 Logged in as: %s", session->session_user);
    } else {
      send_msg(fd, "200 Not logged in.");
    }
  } else if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0 ||
             strcmp(cmd, "BYE") == 0) {
    send_msg(fd, "200 Goodbye.");
    close_client(fd, master_set);
  } else {
    send_msg(fd, "500 Unknown command. Available: LOGIN, LOGOUT, WHOAMI, QUIT");
  }
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

  signal(SIGPIPE, SIG_IGN);

  int loaded = load_accounts(g_account_file);
  if (loaded < 0) {
    fprintf(stderr, "Error: Could not open account file '%s'.\n",
            g_account_file);
    return EXIT_FAILURE;
  }
  printf("[*] Loaded %d accounts from '%s'.\n", loaded, g_account_file);

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
  printf("[*] TCP Authentication Server listening on port %d...\n", port);
  printf("[*] I/O Multiplexing active using select().\n");
  printf("====================================================\n");

  fd_set master_set;
  fd_set read_fds;
  FD_ZERO(&master_set);
  FD_ZERO(&read_fds);

  FD_SET(server_fd, &master_set);
  int max_fd = server_fd;

  memset(g_sessions, 0, sizeof(g_sessions));

  while (1) {
    read_fds = master_set;

    int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    if (activity < 0) {
      if (errno == EINTR)
        continue;
      perror("select");
      break;
    }

    if (FD_ISSET(server_fd, &read_fds)) {
      struct sockaddr_in client_addr;
      socklen_t client_len = sizeof(client_addr);
      int client_fd =
          accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

      if (client_fd >= 0) {
        if (client_fd < FD_SETSIZE) {
          char client_ip[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &client_addr.sin_addr, client_ip,
                    sizeof(client_ip));
          int client_port = ntohs(client_addr.sin_port);

          printf("[+] New client connected from %s:%d (Assigned fd %d)\n",
                 client_ip, client_port, client_fd);

          FD_SET(client_fd, &master_set);
          if (client_fd > max_fd) {
            max_fd = client_fd;
          }

          memset(&g_sessions[client_fd], 0, sizeof(ClientSession));
          g_sessions[client_fd].active = 1;
          g_sessions[client_fd].is_logged_in = 0;
          strncpy(g_sessions[client_fd].client_ip, client_ip,
                  sizeof(g_sessions[client_fd].client_ip) - 1);
          g_sessions[client_fd].client_port = client_port;

          send_msg(client_fd, "200 Welcome to TCP Authentication Server "
                              "(select-based). Ready.");
        } else {
          fprintf(stderr, "[-] Max socket descriptor reached (%d). Closing.\n",
                  FD_SETSIZE);
          close(client_fd);
        }
      }
    }

    for (int fd = 0; fd <= max_fd; fd++) {
      if (fd != server_fd && FD_ISSET(fd, &read_fds)) {
        char line[BUFFER_SIZE];
        int n = recv_line(fd, line, sizeof(line));

        if (n <= 0) {
          close_client(fd, &master_set);
        } else {
          process_client_command(fd, line, &master_set);
        }
      }
    }
  }

  close(server_fd);
  return EXIT_SUCCESS;
}
