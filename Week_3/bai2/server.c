/*
 * Bai 2 - UDP DNS Resolver Server
 * Receives a domain name or an IPv4 address from the client:
 *   - If a domain name is received, resolve it to its official IP address
 *     and (if any) alias IP addresses.
 *   - If an IPv4 address is received, resolve it (reverse lookup) to its
 *     official host name and (if any) alias names.
 *   - If the input looks like an IPv4 address but is not a valid one
 *     (e.g. an octet > 255), reply "IP Address is invalid".
 *   - If nothing can be resolved, reply "Not found information".
 *
 * Usage: ./server PortNumber
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define BUF_SIZE 2048

/* Returns 1 if the string looks like someone's attempt at typing an IPv4
 * address (only digits and dots, at least one dot). This lets us tell
 * "259.12.34.12" (an invalid IP) apart from "aznsc.test.com" (a domain
 * name), since both fail a strict IP parse but must be reported
 * differently. */
static int looks_like_ip(const char *s) {
    int dot_count = 0;
    if (*s == '\0') return 0;
    for (int i = 0; s[i] != '\0'; i++) {
        if (s[i] == '.') {
            dot_count++;
        } else if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return dot_count > 0;
}

static void resolve_domain(const char *name, char *response, size_t resp_size) {
    struct hostent *he = gethostbyname(name);
    if (he == NULL || he->h_addr_list[0] == NULL) {
        snprintf(response, resp_size, "Not found information");
        return;
    }

    char line[256];
    struct in_addr addr;

    response[0] = '\0';
    memcpy(&addr, he->h_addr_list[0], sizeof(struct in_addr));
    snprintf(line, sizeof(line), "Official IP: %s\n", inet_ntoa(addr));
    strncat(response, line, resp_size - strlen(response) - 1);

    int alias_count = 0;
    for (int i = 1; he->h_addr_list[i] != NULL; i++) alias_count++;

    if (alias_count > 0) {
        strncat(response, "Alias IP:\n", resp_size - strlen(response) - 1);
        for (int i = 1; he->h_addr_list[i] != NULL; i++) {
            memcpy(&addr, he->h_addr_list[i], sizeof(struct in_addr));
            snprintf(line, sizeof(line), "%s\n", inet_ntoa(addr));
            strncat(response, line, resp_size - strlen(response) - 1);
        }
    }

    size_t len = strlen(response);
    if (len > 0 && response[len - 1] == '\n') response[len - 1] = '\0';
}

static void resolve_ip(const char *ip_str, char *response, size_t resp_size) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip_str, &addr) != 1) {
        snprintf(response, resp_size, "IP Address is invalid");
        return;
    }

    struct hostent *he = gethostbyaddr(&addr, sizeof(addr), AF_INET);
    if (he == NULL) {
        snprintf(response, resp_size, "Not found information");
        return;
    }

    char line[256];
    response[0] = '\0';
    snprintf(line, sizeof(line), "Official name: %s\n", he->h_name);
    strncat(response, line, resp_size - strlen(response) - 1);

    int alias_count = 0;
    for (int i = 0; he->h_aliases[i] != NULL; i++) alias_count++;

    if (alias_count > 0) {
        strncat(response, "Alias name:\n", resp_size - strlen(response) - 1);
        for (int i = 0; he->h_aliases[i] != NULL; i++) {
            snprintf(line, sizeof(line), "%s\n", he->h_aliases[i]);
            strncat(response, line, resp_size - strlen(response) - 1);
        }
    }

    size_t len = strlen(response);
    if (len > 0 && response[len - 1] == '\n') response[len - 1] = '\0';
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <PortNumber>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr, client_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("[Bai2] DNS resolver UDP server listening on port %d...\n", port);

    char buffer[BUF_SIZE];
    char response[BUF_SIZE];
    socklen_t addr_len = sizeof(client_addr);

    while (1) {
        memset(buffer, 0, BUF_SIZE);
        addr_len = sizeof(client_addr);
        ssize_t n = recvfrom(sockfd, buffer, BUF_SIZE - 1, 0,
                              (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }
        buffer[n] = '\0';

        printf("Received from %s:%d -> \"%s\"\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port), buffer);

        memset(response, 0, BUF_SIZE);
        if (looks_like_ip(buffer)) {
            resolve_ip(buffer, response, BUF_SIZE);
        } else {
            resolve_domain(buffer, response, BUF_SIZE);
        }

        printf("Response:\n%s\n", response);

        sendto(sockfd, response, strlen(response), 0,
               (struct sockaddr *)&client_addr, addr_len);
    }

    close(sockfd);
    return 0;
}
