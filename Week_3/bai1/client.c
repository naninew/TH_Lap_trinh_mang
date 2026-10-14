/*
 * Bai 1 - UDP Client
 * Reads a string from the keyboard, sends it to the server, and prints the
 * server's reply. Repeats until the user enters an empty string.
 *
 * Usage: ./client IPAddress PortNumber
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define BUF_SIZE 1024

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IPAddress> <PortNumber>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid server IP address: %s\n", server_ip);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    char input[BUF_SIZE];
    char response[BUF_SIZE];
    socklen_t addr_len = sizeof(server_addr);

    while (1) {
        printf("Enter a string (empty to quit): ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) break;
        input[strcspn(input, "\n")] = '\0';

        if (strlen(input) == 0) {
            printf("Empty input entered, exiting client.\n");
            break;
        }

        if (sendto(sockfd, input, strlen(input), 0,
                   (struct sockaddr *)&server_addr, addr_len) < 0) {
            perror("sendto");
            continue;
        }

        memset(response, 0, BUF_SIZE);
        ssize_t n = recvfrom(sockfd, response, BUF_SIZE - 1, 0, NULL, NULL);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }
        response[n] = '\0';

        printf("Server response:\n%s\n", response);
    }

    close(sockfd);
    return 0;
}
