/*
 * resolver.c - DNS Lookup Tool (Forward and Reverse Resolution)
 * Assignment: Week 2 - IT4062 (Network Programming)
 *
 * Usage:
 *   ./resolver <parameter>
 *
 * Parameters:
 *   - Valid IPv4 address: Performs reverse lookup; displays official hostname
 *     and any aliases.
 *   - Domain name: Performs forward lookup; displays official IP
 *     and any alias IPs.
 *   - Invalid parameter / lookup failure: Displays "Not found information".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

/*
 * Validates whether the input string is a valid IPv4 address in dotted-decimal format.
 * Returns 1 on success (stores converted address in out_addr), or 0 on failure.
 */
static int is_valid_ip(const char *str, struct in_addr *out_addr) {
    return inet_pton(AF_INET, str, out_addr) == 1;
}

/*
 * Checks if a string consists strictly of digits and dots, containing at least one digit.
 *
 * Purpose: gethostbyname() in legacy glibc mode treats incomplete dotted-numeric
 * strings (e.g. "1.2.3") or invalid octets as legacy numeric addresses instead
 * of reporting a lookup error. Any input matching this pattern that failed
 * is_valid_ip() is directly treated as invalid.
 */
static int looks_like_ip(const char *str) {
    int has_digit = 0;
    for (const char *p = str; *p != '\0'; p++) {
        if (*p >= '0' && *p <= '9') {
            has_digit = 1;
        } else if (*p != '.') {
            return 0;
        }
    }
    return has_digit;
}

/*
 * Performs reverse DNS lookup for an IPv4 address.
 * Displays official hostname and any alias names.
 */
static void resolve_by_ip(const char *ip_str) {
    struct in_addr addr;
    inet_pton(AF_INET, ip_str, &addr);

    struct hostent *he = gethostbyaddr(&addr, sizeof(addr), AF_INET);
    if (he == NULL) {
        printf("Not found information\n");
        return;
    }

    printf("Official name: %s\n", he->h_name);

    /* Print alias hostnames if any exist */
    if (he->h_aliases != NULL && he->h_aliases[0] != NULL) {
        printf("Alias name:\n");
        for (int i = 0; he->h_aliases[i] != NULL; i++) {
            printf("%s\n", he->h_aliases[i]);
        }
    }
}

/*
 * Performs forward DNS lookup for a domain name.
 * Displays official IPv4 address and any alias IPs.
 */
static void resolve_by_name(const char *domain) {
    struct hostent *he = gethostbyname(domain);
    if (he == NULL) {
        printf("Not found information\n");
        return;
    }

    char ip_str[INET_ADDRSTRLEN];
    struct in_addr **addr_list = (struct in_addr **) he->h_addr_list;

    if (addr_list == NULL || addr_list[0] == NULL) {
        printf("Not found information\n");
        return;
    }

    /* First address in list is the primary/official IP */
    inet_ntop(AF_INET, addr_list[0], ip_str, INET_ADDRSTRLEN);
    printf("Official IP: %s\n", ip_str);

    /* Subsequent addresses are aliases */
    if (addr_list[1] != NULL) {
        printf("Alias IP:\n");
        for (int i = 1; addr_list[i] != NULL; i++) {
            inet_ntop(AF_INET, addr_list[i], ip_str, INET_ADDRSTRLEN);
            printf("%s\n", ip_str);
        }
    }
}

int main(int argc, char *argv[]) {
    /* Validate number of command-line arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <domain_name | IP_address>\n", argv[0]);
        return 1;
    }

    struct in_addr addr;
    if (is_valid_ip(argv[1], &addr)) {
        /* Input is a valid IPv4 address -> Reverse DNS lookup */
        resolve_by_ip(argv[1]);
    } else if (looks_like_ip(argv[1])) {
        /* Input is a malformed numeric IP string -> Reject immediately */
        printf("Not found information\n");
    } else {
        /* Input is treated as a domain name -> Forward DNS lookup */
        resolve_by_name(argv[1]);
    }

    return 0;
}
