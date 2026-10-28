/*
 * server.c - Problem 2: TCP User Authentication Server (Multiprocess with
 * fork()) Course: IT4062 - Network Programming Author: Do Xuan Quy (MSSV:
 * 20235818)
 *
 * Requirements mapped:
 * - [BAI 2 FORK] Multiprocess server model using fork() and SIGCHLD handler (no
 * zombies).
 * - [BAI 2 IPC] POSIX shared memory (mmap MAP_SHARED) with process-shared
 * mutex.
 * - [BAI 2 REQUIREMENT 1] Each client window can only log in to 1 account.
 * - [BAI 2 REQUIREMENT 2] An account can log in simultaneously on multiple
 * windows.
 * - [BAI 2 REQUIREMENT 3] If login fails > 3 times, account is locked (status
 * 0) and synced to disk.
 * - [BAI 2 REQUIREMENT 4] User accounts stored in account.txt (UserID Password
 * Status).
 * - [BAI 2 REQUIREMENT 5] Command-line syntax: ./server Port_Number
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_ACCOUNTS 256
#define MAX_STR_LEN 64
#define BUFFER_SIZE 1024
#define MAX_FAILED_ATTEMPTS 3
#define DEFAULT_ACCOUNT_FILE "account.txt"

/* [BAI 2 REQUIREMENT 4] Account data structure */
typedef struct {
  char user_id[MAX_STR_LEN];
  char password[MAX_STR_LEN];
  int status;          /* 1: active, 0: blocked/locked */
  int failed_attempts; /* consecutive failed password attempts */
} Account;

/* [BAI 2 IPC] Shared memory data structure across forked child processes */
typedef struct {
  Account accounts[MAX_ACCOUNTS];
  int account_count;
  char account_file[256];
  pthread_mutex_t mutex;
} SharedData;

static SharedData *g_shared = NULL;

/* [BAI 2 FORK] SIGCHLD handler to reap terminated child processes and prevent
 * zombies */
static void sigchld_handler(int sig) {
  (void)sig;
  while (waitpid(-1, NULL, WNOHANG) > 0)
    ;
}

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

/* [BAI 2 REQUIREMENT 4] Load accounts from file into shared memory */
static int load_accounts(const char *filename) {
  FILE *fp = fopen(filename, "r");
  if (!fp) {
    fp = fopen("bai2/account.txt", "r");
    if (fp) {
      strncpy(g_shared->account_file, "bai2/account.txt",
              sizeof(g_shared->account_file) - 1);
    } else {
      return -1;
    }
  } else {
    strncpy(g_shared->account_file, filename,
            sizeof(g_shared->account_file) - 1);
  }

  g_shared->account_count = 0;
  char line[BUFFER_SIZE];
  while (fgets(line, sizeof(line), fp)) {
    trim_line(line);
    if (line[0] == '\0' || line[0] == '#')
      continue;

    char u[MAX_STR_LEN], p[MAX_STR_LEN];
    int s = 1;
    if (sscanf(line, "%63s %63s %d", u, p, &s) == 3) {
      if (g_shared->account_count < MAX_ACCOUNTS) {
        strncpy(g_shared->accounts[g_shared->account_count].user_id, u,
                MAX_STR_LEN - 1);
        g_shared->accounts[g_shared->account_count].user_id[MAX_STR_LEN - 1] =
            '\0';
        strncpy(g_shared->accounts[g_shared->account_count].password, p,
                MAX_STR_LEN - 1);
        g_shared->accounts[g_shared->account_count].password[MAX_STR_LEN - 1] =
            '\0';
        g_shared->accounts[g_shared->account_count].status = s;
        g_shared->accounts[g_shared->account_count].failed_attempts = 0;
        g_shared->account_count++;
      }
    }
  }
  fclose(fp);
  return g_shared->account_count;
}

/* [BAI 2 REQUIREMENT 3] Atomically write accounts back to file on disk */
static int save_accounts(void) {
  char tmp_file[300];
  snprintf(tmp_file, sizeof(tmp_file), "%s.tmp", g_shared->account_file);

  FILE *fp = fopen(tmp_file, "w");
  if (!fp) {
    perror("fopen tmp_file");
    return -1;
  }

  for (int i = 0; i < g_shared->account_count; i++) {
    fprintf(fp, "%s %s %d\n", g_shared->accounts[i].user_id,
            g_shared->accounts[i].password, g_shared->accounts[i].status);
  }
  fflush(fp);
  fclose(fp);

  if (rename(tmp_file, g_shared->account_file) != 0) {
    perror("rename account file");
    return -1;
  }
  return 0;
}

static int find_account_index(const char *user_id) {
  for (int i = 0; i < g_shared->account_count; i++) {
    if (strcmp(g_shared->accounts[i].user_id, user_id) == 0) {
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

/* [BAI 2 FORK] Worker process routine for a single client */
static void handle_client(int fd, struct sockaddr_in client_addr) {
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
  int client_port = ntohs(client_addr.sin_port);
  pid_t pid = getpid();

  printf("[+] [Child PID %d] Client connected from %s:%d\n", pid, client_ip,
         client_port);

  send_msg(fd, "200 Welcome to Multiprocess TCP Authentication Server. Ready.");

  /* [BAI 2 REQUIREMENT 1] Session state per client window */
  int is_logged_in = 0;
  char session_user[MAX_STR_LEN] = "";

  char line[BUFFER_SIZE];
  while (recv_line(fd, line, sizeof(line)) >= 0) {
    if (line[0] == '\0')
      continue;

    char cmd[MAX_STR_LEN] = "";
    char arg1[MAX_STR_LEN] = "";
    char arg2[MAX_STR_LEN] = "";
    int tokens = sscanf(line, "%63s %63s %63s", cmd, arg1, arg2);
    if (tokens < 1)
      continue;

    for (int i = 0; cmd[i]; i++)
      cmd[i] = (char)toupper((unsigned char)cmd[i]);

    if (strcmp(cmd, "LOGIN") == 0) {
      /* [BAI 2 REQUIREMENT 1] Check if this client window has already logged in
       */
      if (is_logged_in) {
        send_msg(fd, "401 Already logged in as '%s'. Please logout first.",
                 session_user);
        continue;
      }

      if (tokens < 3) {
        send_msg(fd, "400 Syntax error. Usage: LOGIN <UserID> <Password>");
        continue;
      }

      /* [BAI 2 IPC] Lock process-shared mutex across forked children */
      pthread_mutex_lock(&g_shared->mutex);
      int idx = find_account_index(arg1);

      if (idx == -1) {
        pthread_mutex_unlock(&g_shared->mutex);
        send_msg(fd, "404 Account '%s' does not exist.", arg1);
        continue;
      }

      if (g_shared->accounts[idx].status == 0) {
        pthread_mutex_unlock(&g_shared->mutex);
        send_msg(fd, "403 Account '%s' is blocked.", arg1);
        continue;
      }

      if (strcmp(g_shared->accounts[idx].password, arg2) == 0) {
        g_shared->accounts[idx].failed_attempts = 0;

        is_logged_in = 1;
        strncpy(session_user, g_shared->accounts[idx].user_id,
                sizeof(session_user) - 1);
        session_user[sizeof(session_user) - 1] = '\0';

        pthread_mutex_unlock(&g_shared->mutex);

        /* [BAI 2 REQUIREMENT 2] Concurrent logins for the same account allowed
         */
        printf("[*] [Child PID %d] User '%s' logged in from %s:%d\n", pid,
               session_user, client_ip, client_port);
        send_msg(fd, "200 Login successful. Welcome, %s!", session_user);
      } else {
        /* [BAI 2 REQUIREMENT 3] Wrong password: increment failure counter */
        g_shared->accounts[idx].failed_attempts++;
        int attempts = g_shared->accounts[idx].failed_attempts;

        printf("[-] [Child PID %d] User '%s' wrong password (attempt %d/%d) "
               "from %s:%d\n",
               pid, arg1, attempts, MAX_FAILED_ATTEMPTS, client_ip,
               client_port);

        if (attempts >= MAX_FAILED_ATTEMPTS) {
          g_shared->accounts[idx].status = 0;
          save_accounts();
          pthread_mutex_unlock(&g_shared->mutex);

          printf("[!] [Child PID %d] Account '%s' is now BLOCKED after %d "
                 "failed attempts!\n",
                 pid, arg1, attempts);
          send_msg(
              fd,
              "405 Password incorrect (%d/%d). Account '%s' has been BLOCKED!",
              attempts, MAX_FAILED_ATTEMPTS, arg1);
        } else {
          pthread_mutex_unlock(&g_shared->mutex);
          send_msg(fd,
                   "402 Password incorrect. Attempt %d/%d. Please try again.",
                   attempts, MAX_FAILED_ATTEMPTS);
        }
      }
    } else if (strcmp(cmd, "LOGOUT") == 0) {
      /* [BAI 2 REQUIREMENT 1] Logout from current client window */
      if (!is_logged_in) {
        send_msg(fd, "400 You are not logged in.");
      } else {
        printf("[*] [Child PID %d] User '%s' logged out from %s:%d\n", pid,
               session_user, client_ip, client_port);
        is_logged_in = 0;
        session_user[0] = '\0';
        send_msg(fd, "200 Logged out successfully.");
      }
    } else if (strcmp(cmd, "WHOAMI") == 0 || strcmp(cmd, "STATUS") == 0) {
      if (is_logged_in) {
        send_msg(fd, "200 Logged in as: %s", session_user);
      } else {
        send_msg(fd, "200 Not logged in.");
      }
    } else if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0 ||
               strcmp(cmd, "BYE") == 0) {
      send_msg(fd, "200 Goodbye.");
      break;
    } else {
      send_msg(fd,
               "500 Unknown command. Available: LOGIN, LOGOUT, WHOAMI, QUIT");
    }
  }

  printf("[-] [Child PID %d] Client %s:%d disconnected.\n", pid, client_ip,
         client_port);
  close(fd);
}

/* [BAI 2 REQUIREMENT 5] Main entry point: ./server Port_Number */
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

  /* [BAI 2 IPC] Allocate shared memory for multi-process synchronization */
  g_shared =
      (SharedData *)mmap(NULL, sizeof(SharedData), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (g_shared == MAP_FAILED) {
    perror("mmap");
    return EXIT_FAILURE;
  }

  pthread_mutexattr_t mattr;
  pthread_mutexattr_init(&mattr);
  pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&g_shared->mutex, &mattr);
  pthread_mutexattr_destroy(&mattr);

  int loaded = load_accounts(DEFAULT_ACCOUNT_FILE);
  if (loaded < 0) {
    fprintf(stderr, "Error: Could not open account file '%s'.\n",
            DEFAULT_ACCOUNT_FILE);
    return EXIT_FAILURE;
  }
  printf("[*] Loaded %d accounts from '%s'.\n", loaded, g_shared->account_file);

  /* [BAI 2 FORK] Setup SIGCHLD handler to prevent zombie processes */
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  if (sigaction(SIGCHLD, &sa, NULL) < 0) {
    perror("sigaction");
    return EXIT_FAILURE;
  }

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
  printf("[*] Multiprocess model active (fork() + shared memory).\n");
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

    /* [BAI 2 FORK] Fork a child process to handle this client connection */
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      close(client_fd);
      continue;
    }

    if (pid == 0) {
      close(server_fd);
      handle_client(client_fd, client_addr);
      exit(EXIT_SUCCESS);
    } else {
      close(client_fd);
    }
  }

  close(server_fd);
  munmap(g_shared, sizeof(SharedData));
  return EXIT_SUCCESS;
}
