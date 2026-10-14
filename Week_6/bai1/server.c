/*
 * server.c - Problem 1: TCP User Authentication Server
 * Course: IT4062 - Network Programming
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [REQUIREMENT 1] Each client window can only log in to 1 account.
 * - [REQUIREMENT 2] An account can log in simultaneously on multiple windows.
 * - [REQUIREMENT 3] If login fails > 3 times, account is locked (status 0).
 * - [REQUIREMENT 4] User accounts stored in account.txt (UserID Password Status).
 * - [REQUIREMENT 5] Command-line syntax: ./server Port_Number
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>

#define MAX_ACCOUNTS 256
#define MAX_STR_LEN 64
#define BUFFER_SIZE 1024
#define MAX_FAILED_ATTEMPTS 3
#define DEFAULT_ACCOUNT_FILE "account.txt"

/* [REQUIREMENT 4] In-memory account entry */
typedef struct {
    char user_id[MAX_STR_LEN];
    char password[MAX_STR_LEN];
    int status;           /* 1: active, 0: blocked/locked */
    int failed_attempts;  /* consecutive failed password attempts */
} Account;

static Account g_accounts[MAX_ACCOUNTS];
static int g_account_count = 0;
static char g_account_file[256] = DEFAULT_ACCOUNT_FILE;
static pthread_mutex_t g_acc_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Trim trailing whitespace, \r, and \n */
static void trim_line(char *str) {
    if (!str) return;
    int len = (int)strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' || isspace((unsigned char)str[len - 1]))) {
        str[len - 1] = '\0';
        len--;
    }
}

/* [REQUIREMENT 4] Load accounts from file (UserID Password Status) */
static int load_accounts(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        /* Fallback check for relative execution paths */
        fp = fopen("bai1/account.txt", "r");
        if (fp) {
            strncpy(g_account_file, "bai1/account.txt", sizeof(g_account_file) - 1);
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
        if (line[0] == '\0' || line[0] == '#') continue;

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

/* [REQUIREMENT 3 & 4] Atomically persist updated accounts to file */
static int save_accounts(void) {
    char tmp_file[300];
    snprintf(tmp_file, sizeof(tmp_file), "%s.tmp", g_account_file);

    FILE *fp = fopen(tmp_file, "w");
    if (!fp) {
        perror("fopen tmp_file");
        return -1;
    }

    for (int i = 0; i < g_account_count; i++) {
        fprintf(fp, "%s %s %d\n",
                g_accounts[i].user_id,
                g_accounts[i].password,
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

/* Helper to find account index by UserID */
static int find_account_index(const char *user_id) {
    for (int i = 0; i < g_account_count; i++) {
        if (strcmp(g_accounts[i].user_id, user_id) == 0) {
            return i;
        }
    }
    return -1;
}

/* Reliable send function for TCP byte stream */
static int send_all(int fd, const char *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = send(fd, buf + total, len - total, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        total += (size_t)n;
    }
    return 0;
}

/* Reliable line-based reader for TCP byte stream */
static int recv_line(int fd, char *buf, size_t max_len) {
    size_t idx = 0;
    while (idx + 1 < max_len) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1; /* Connection closed or error */
        }
        if (c == '\n') break;
        buf[idx++] = c;
    }
    buf[idx] = '\0';
    trim_line(buf);
    return (int)idx;
}

/* Send formatted text message with trailing \n */
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

/* Client connection thread context */
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} ClientContext;

/* Worker thread handling a single client connection */
static void *client_handler(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    int fd = ctx->client_fd;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ctx->client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(ctx->client_addr.sin_port);
    free(ctx);

    printf("[+] Client connected from %s:%d (Thread ID: %lu)\n",
           client_ip, client_port, (unsigned long)pthread_self());

    /* Send welcome message */
    send_msg(fd, "200 Welcome to TCP Authentication Server. Ready for commands.");

    /* [REQUIREMENT 1] Session state per client window */
    int is_logged_in = 0;
    char session_user[MAX_STR_LEN] = "";

    char line[BUFFER_SIZE];
    while (recv_line(fd, line, sizeof(line)) >= 0) {
        if (line[0] == '\0') continue;

        char cmd[MAX_STR_LEN] = "";
        char arg1[MAX_STR_LEN] = "";
        char arg2[MAX_STR_LEN] = "";
        int tokens = sscanf(line, "%63s %63s %63s", cmd, arg1, arg2);
        if (tokens < 1) continue;

        /* Convert command to uppercase */
        for (int i = 0; cmd[i]; i++) cmd[i] = (char)toupper((unsigned char)cmd[i]);

        if (strcmp(cmd, "LOGIN") == 0) {
            /* [REQUIREMENT 1] Check if this client window has already logged in */
            if (is_logged_in) {
                send_msg(fd, "401 Already logged in as '%s'. Please logout first.", session_user);
                continue;
            }

            if (tokens < 3) {
                send_msg(fd, "400 Syntax error. Usage: LOGIN <UserID> <Password>");
                continue;
            }

            pthread_mutex_lock(&g_acc_mutex);
            int idx = find_account_index(arg1);

            /* Check account existence */
            if (idx == -1) {
                pthread_mutex_unlock(&g_acc_mutex);
                send_msg(fd, "404 Account '%s' does not exist.", arg1);
                continue;
            }

            /* Check if account is already blocked/locked */
            if (g_accounts[idx].status == 0) {
                pthread_mutex_unlock(&g_acc_mutex);
                send_msg(fd, "403 Account '%s' is blocked.", arg1);
                continue;
            }

            /* Validate password */
            if (strcmp(g_accounts[idx].password, arg2) == 0) {
                /* Password matched: login successful */
                g_accounts[idx].failed_attempts = 0; /* Reset consecutive failed counter */

                /* [REQUIREMENT 1] Update client session state */
                is_logged_in = 1;
                strncpy(session_user, g_accounts[idx].user_id, sizeof(session_user) - 1);
                session_user[sizeof(session_user) - 1] = '\0';

                pthread_mutex_unlock(&g_acc_mutex);

                /* [REQUIREMENT 2] Notice: Multiple client windows can log in to this same account simultaneously */
                printf("[*] User '%s' logged in successfully from %s:%d\n",
                       session_user, client_ip, client_port);
                send_msg(fd, "200 Login successful. Welcome, %s!", session_user);
            } else {
                /* [REQUIREMENT 3] Password incorrect: increment failure counter */
                g_accounts[idx].failed_attempts++;
                int attempts = g_accounts[idx].failed_attempts;

                printf("[-] User '%s' wrong password (attempt %d/%d) from %s:%d\n",
                       arg1, attempts, MAX_FAILED_ATTEMPTS, client_ip, client_port);

                if (attempts >= MAX_FAILED_ATTEMPTS) {
                    /* Lock account immediately */
                    g_accounts[idx].status = 0;
                    save_accounts();
                    pthread_mutex_unlock(&g_acc_mutex);

                    printf("[!] Account '%s' is now BLOCKED due to %d consecutive failed attempts!\n",
                           arg1, attempts);
                    send_msg(fd, "405 Password incorrect (%d/%d). Account '%s' has been BLOCKED!",
                             attempts, MAX_FAILED_ATTEMPTS, arg1);
                } else {
                    pthread_mutex_unlock(&g_acc_mutex);
                    send_msg(fd, "402 Password incorrect. Attempt %d/%d. Please try again.",
                             attempts, MAX_FAILED_ATTEMPTS);
                }
            }
        }
        else if (strcmp(cmd, "LOGOUT") == 0) {
            /* [REQUIREMENT 1] Logout from current client window */
            if (!is_logged_in) {
                send_msg(fd, "400 You are not logged in.");
            } else {
                printf("[*] User '%s' logged out from %s:%d\n",
                       session_user, client_ip, client_port);
                is_logged_in = 0;
                session_user[0] = '\0';
                send_msg(fd, "200 Logged out successfully.");
            }
        }
        else if (strcmp(cmd, "WHOAMI") == 0 || strcmp(cmd, "STATUS") == 0) {
            if (is_logged_in) {
                send_msg(fd, "200 Logged in as: %s", session_user);
            } else {
                send_msg(fd, "200 Not logged in.");
            }
        }
        else if (strcmp(cmd, "QUIT") == 0 || strcmp(cmd, "EXIT") == 0 || strcmp(cmd, "BYE") == 0) {
            send_msg(fd, "200 Goodbye.");
            break;
        }
        else {
            send_msg(fd, "500 Unknown command. Available: LOGIN, LOGOUT, WHOAMI, QUIT");
        }
    }

    printf("[-] Client %s:%d disconnected.\n", client_ip, client_port);
    close(fd);
    return NULL;
}

/* [REQUIREMENT 5] Main entry point: ./server Port_Number */
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

    /* Load account data */
    int loaded = load_accounts(g_account_file);
    if (loaded < 0) {
        fprintf(stderr, "Error: Could not open account file '%s'.\n", g_account_file);
        return EXIT_FAILURE;
    }
    printf("[*] Loaded %d accounts from '%s'.\n", loaded, g_account_file);

    /* Initialize TCP listening socket */
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
    printf("[*] TCP Authentication Server listening on port %d...\n", port);
    printf("[*] Multi-threaded POSIX pthread model active.\n");
    printf("====================================================\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
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
