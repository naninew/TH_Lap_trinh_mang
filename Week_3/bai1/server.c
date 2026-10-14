/*
 * Bai 1 - UDP Server
 * Receives a string from the client and splits it into two strings:
 *   - a string containing only alphabetic characters
 *   - a string containing only digit characters
 * If the received string contains any character that is neither a letter
 * nor a digit, the server replies with "Error".
 *
 * Usage: ./server PortNumber
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024

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

    printf("[Bai1] UDP classify server listening on port %d...\n", port);

    char buffer[BUF_SIZE];
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

        char digits[BUF_SIZE] = {0};
        char letters[BUF_SIZE] = {0};
        int di = 0, li = 0;
        int valid = 1;

        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buffer[i];
            if (isdigit(c)) {
                digits[di++] = (char)c;
            } else if (isalpha(c)) {
                letters[li++] = (char)c;
            } else {
                valid = 0;
                break;
            }
        }
        digits[di] = '\0';
        letters[li] = '\0';

        char response[BUF_SIZE];
        if (!valid) {
            snprintf(response, sizeof(response), "Error");
        } else {
            response[0] = '\0';
            /* digits string first, then letters string (see sample I/O table) */
            if (di > 0) {
                strcat(response, digits);
            }
            if (li > 0) {
                if (di > 0) strcat(response, "\n");
                strcat(response, letters);
            }
        }

        printf("Response: %s\n", response);

        sendto(sockfd, response, strlen(response), 0,
               (struct sockaddr *)&client_addr, addr_len);
    }

    close(sockfd);
    return 0;
}
