/*
 * client.c - Problem 2: Reliable UDP File Transfer Client (RUFTP)
 * Course: IT4062 - Network Programming
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [BAI 2 PROTOCOL] Reliable UDP transfer using RUFTP protocol.
 * - [BAI 2 ARQ] Stop-and-Wait ARQ with timeouts, retries, and CRC32 verification.
 * - [BAI 2 UPLOAD] Uploads arbitrary local files to server storage/.
 * - [BAI 2 DOWNLOAD] Downloads files from server storage/ to client.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>
#include <ctype.h>

#include "protocol.h"

/* Helper to wait for an incoming UDP packet with timeout */
static int recv_packet_timeout(int sockfd, RuftpPacket *pkt, struct sockaddr_in *from, socklen_t *from_len, int timeout_ms) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(sockfd + 1, &readfds, NULL, NULL, &tv);
    if (ret > 0 && FD_ISSET(sockfd, &readfds)) {
        ssize_t n = recvfrom(sockfd, pkt, sizeof(RuftpPacket), 0, (struct sockaddr *)from, from_len);
        if (n >= (ssize_t)(sizeof(RuftpPacket) - RUFTP_CHUNK_SIZE)) {
            pkt->magic = ntohl(pkt->magic);
            pkt->seq_num = ntohl(pkt->seq_num);
            pkt->ack_num = ntohl(pkt->ack_num);
            pkt->payload_len = ntohs(pkt->payload_len);
            pkt->checksum = ntohl(pkt->checksum);
            if (pkt->magic == RUFTP_MAGIC) {
                return (int)n;
            }
        }
    }
    return -1;
}

/* Helper to send a RUFTP packet */
static int send_packet(int sockfd, const RuftpPacket *pkt, const struct sockaddr_in *to, socklen_t to_len) {
    RuftpPacket wire_pkt = *pkt;
    wire_pkt.magic = htonl(RUFTP_MAGIC);
    wire_pkt.seq_num = htonl(pkt->seq_num);
    wire_pkt.ack_num = htonl(pkt->ack_num);
    wire_pkt.payload_len = htons(pkt->payload_len);
    wire_pkt.checksum = htonl(pkt->checksum);

    size_t total_size = sizeof(RuftpPacket) - RUFTP_CHUNK_SIZE + pkt->payload_len;
    ssize_t n = sendto(sockfd, &wire_pkt, total_size, 0, (const struct sockaddr *)to, to_len);
    return (n > 0) ? 0 : -1;
}

/* Send an ACK packet */
static void send_ack(int sockfd, uint32_t ack_num, const struct sockaddr_in *to, socklen_t to_len) {
    RuftpPacket ack_pkt;
    memset(&ack_pkt, 0, sizeof(ack_pkt));
    ack_pkt.type = RUFTP_TYPE_ACK;
    ack_pkt.ack_num = ack_num;
    send_packet(sockfd, &ack_pkt, to, to_len);
}

/* Trim whitespace, \r, and \n */
static void trim_line(char *str) {
    if (!str) return;
    int len = (int)strlen(str);
    while (len > 0 && (str[len - 1] == '\r' || str[len - 1] == '\n' || isspace((unsigned char)str[len - 1]))) {
        str[len - 1] = '\0';
        len--;
    }
}

/* [BAI 2 UPLOAD] Upload local file to UDP server */
static int do_upload(int sockfd, const char *local_path, const struct sockaddr_in *server_addr, socklen_t addr_len) {
    FILE *fp = fopen(local_path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open local file '%s': %s\n", local_path, strerror(errno));
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    uint64_t file_size = (uint64_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* Compute CRC32 of local file */
    uint32_t actual_crc = ~0U;
    char chunk[RUFTP_CHUNK_SIZE];
    size_t r;
    while ((r = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        const uint8_t *p = (const uint8_t *)chunk;
        while (r--) {
            actual_crc ^= *p++;
            for (int k = 0; k < 8; k++) {
                actual_crc = (actual_crc >> 1) ^ (0xEDB88320U & -(actual_crc & 1U));
            }
        }
    }
    actual_crc = ~actual_crc;
    fseek(fp, 0, SEEK_SET);

    char path_copy[512];
    strncpy(path_copy, local_path, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    char *base = basename(path_copy);

    printf("[UPLOAD] Preparing: '%s' (%lu bytes, CRC32: 0x%08X)\n",
           base, (unsigned long)file_size, actual_crc);

    /* Prepare upload request */
    RuftpFileInfo info;
    memset(&info, 0, sizeof(info));
    strncpy(info.filename, base, sizeof(info.filename) - 1);
    info.file_size = file_size;
    info.file_crc32 = actual_crc;

    RuftpPacket req_pkt;
    memset(&req_pkt, 0, sizeof(req_pkt));
    req_pkt.type = RUFTP_TYPE_REQ_UPLOAD;
    req_pkt.payload_len = sizeof(RuftpFileInfo);
    memcpy(req_pkt.payload, &info, sizeof(RuftpFileInfo));

    /* Send upload request and wait for server ACK */
    int start_acked = 0;
    for (int retry = 0; retry < RUFTP_MAX_RETRIES && !start_acked; retry++) {
        send_packet(sockfd, &req_pkt, server_addr, addr_len);

        RuftpPacket ack_pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recv_packet_timeout(sockfd, &ack_pkt, &from_addr, &from_len, RUFTP_TIMEOUT_MS) > 0) {
            if (ack_pkt.type == RUFTP_TYPE_ACK) {
                start_acked = 1;
            } else if (ack_pkt.type == RUFTP_TYPE_ERR) {
                fprintf(stderr, "Server rejected upload: %s\n", ack_pkt.payload);
                fclose(fp);
                return -1;
            }
        }
    }

    if (!start_acked) {
        fprintf(stderr, "Error: Server did not respond to upload request.\n");
        fclose(fp);
        return -1;
    }

    /* [BAI 2 ARQ] Send data chunks with Stop-and-Wait ARQ */
    uint32_t seq_num = 0;
    uint64_t bytes_sent = 0;

    printf("[UPLOAD] Transferring data chunks...\n");

    while (bytes_sent < file_size) {
        size_t bytes_to_read = RUFTP_CHUNK_SIZE;
        if (file_size - bytes_sent < bytes_to_read) {
            bytes_to_read = (size_t)(file_size - bytes_sent);
        }

        RuftpPacket data_pkt;
        memset(&data_pkt, 0, sizeof(data_pkt));
        data_pkt.type = RUFTP_TYPE_DATA;
        data_pkt.seq_num = seq_num;
        data_pkt.payload_len = (uint16_t)fread(data_pkt.payload, 1, bytes_to_read, fp);
        data_pkt.checksum = ruftp_crc32(data_pkt.payload, data_pkt.payload_len);

        int chunk_acked = 0;
        for (int retry = 0; retry < RUFTP_MAX_RETRIES && !chunk_acked; retry++) {
            send_packet(sockfd, &data_pkt, server_addr, addr_len);

            RuftpPacket ack_pkt;
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            if (recv_packet_timeout(sockfd, &ack_pkt, &from_addr, &from_len, RUFTP_TIMEOUT_MS) > 0) {
                if (ack_pkt.type == RUFTP_TYPE_ACK && ack_pkt.ack_num == seq_num) {
                    chunk_acked = 1;
                } else if (ack_pkt.type == RUFTP_TYPE_ERR) {
                    fprintf(stderr, "Server error: %s\n", ack_pkt.payload);
                    fclose(fp);
                    return -1;
                }
            }
        }

        if (!chunk_acked) {
            fprintf(stderr, "Error: Timeout retransmitting chunk %u. Aborted.\n", seq_num);
            fclose(fp);
            return -1;
        }

        bytes_sent += data_pkt.payload_len;
        seq_num++;
    }

    fclose(fp);

    /* Wait for final FIN confirmation from server */
    RuftpPacket fin_pkt;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int finished = 0;
    for (int retry = 0; retry < RUFTP_MAX_RETRIES && !finished; retry++) {
        if (recv_packet_timeout(sockfd, &fin_pkt, &from_addr, &from_len, RUFTP_TIMEOUT_MS * 2) > 0) {
            if (fin_pkt.type == RUFTP_TYPE_FIN) {
                finished = 1;
            } else if (fin_pkt.type == RUFTP_TYPE_ERR) {
                fprintf(stderr, "Server verification failed: %s\n", fin_pkt.payload);
                return -1;
            }
        }
    }

    if (finished) {
        printf("[SUCCESS] Upload of '%s' completed! (%lu bytes transferred, CRC verified)\n",
               base, (unsigned long)bytes_sent);
        return 0;
    } else {
        fprintf(stderr, "Warning: Transfer ended without FIN confirmation.\n");
        return -1;
    }
}

/* [BAI 2 DOWNLOAD] Download file from UDP server to local path */
static int do_download(int sockfd, const char *remote_file, const char *save_path, const struct sockaddr_in *server_addr, socklen_t addr_len) {
    RuftpPacket req_pkt;
    memset(&req_pkt, 0, sizeof(req_pkt));
    req_pkt.type = RUFTP_TYPE_REQ_DOWNLOAD;
    req_pkt.payload_len = (uint16_t)strlen(remote_file);
    strncpy(req_pkt.payload, remote_file, RUFTP_CHUNK_SIZE - 1);

    printf("[DOWNLOAD] Requesting file '%s' from server...\n", remote_file);

    RuftpFileInfo info;
    int got_res = 0;

    for (int retry = 0; retry < RUFTP_MAX_RETRIES && !got_res; retry++) {
        send_packet(sockfd, &req_pkt, server_addr, addr_len);

        RuftpPacket res_pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recv_packet_timeout(sockfd, &res_pkt, &from_addr, &from_len, RUFTP_TIMEOUT_MS) > 0) {
            if (res_pkt.type == RUFTP_TYPE_RES_DOWNLOAD) {
                memcpy(&info, res_pkt.payload, sizeof(RuftpFileInfo));
                got_res = 1;
            } else if (res_pkt.type == RUFTP_TYPE_ERR) {
                fprintf(stderr, "Server error: %s\n", res_pkt.payload);
                return -1;
            }
        }
    }

    if (!got_res) {
        fprintf(stderr, "Error: Server did not respond to download request.\n");
        return -1;
    }

    const char *out_name = (save_path && save_path[0] != '\0') ? save_path : info.filename;
    FILE *fp = fopen(out_name, "wb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open local destination file '%s': %s\n", out_name, strerror(errno));
        return -1;
    }

    printf("[DOWNLOAD] Receiving '%s' (%lu bytes, expected CRC: 0x%08X) -> saving as '%s'\n",
           info.filename, (unsigned long)info.file_size, info.file_crc32, out_name);

    /* Send ACK to start data chunks */
    send_ack(sockfd, 0, server_addr, addr_len);

    /* [BAI 2 ARQ] Receive data chunks with Stop-and-Wait ARQ */
    uint32_t expected_seq = 0;
    uint64_t bytes_received = 0;
    int retries = 0;

    while (bytes_received < info.file_size) {
        RuftpPacket data_pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        int recvd = recv_packet_timeout(sockfd, &data_pkt, &from_addr, &from_len, RUFTP_TIMEOUT_MS * 2);
        if (recvd <= 0) {
            retries++;
            if (retries > RUFTP_MAX_RETRIES) {
                fprintf(stderr, "[-] Download timed out waiting for chunk %u.\n", expected_seq);
                break;
            }
            send_ack(sockfd, expected_seq > 0 ? expected_seq - 1 : 0, server_addr, addr_len);
            continue;
        }

        retries = 0;

        if (data_pkt.type == RUFTP_TYPE_DATA) {
            uint32_t calc_crc = ruftp_crc32(data_pkt.payload, data_pkt.payload_len);
            if (calc_crc != data_pkt.checksum) {
                send_ack(sockfd, expected_seq > 0 ? expected_seq - 1 : 0, server_addr, addr_len);
                continue;
            }

            if (data_pkt.seq_num == expected_seq) {
                size_t written = fwrite(data_pkt.payload, 1, data_pkt.payload_len, fp);
                if (written != data_pkt.payload_len) {
                    perror("fwrite");
                    break;
                }
                bytes_received += written;
                send_ack(sockfd, expected_seq, server_addr, addr_len);
                expected_seq++;
            } else if (data_pkt.seq_num < expected_seq) {
                send_ack(sockfd, data_pkt.seq_num, server_addr, addr_len);
            }
        } else if (data_pkt.type == RUFTP_TYPE_FIN) {
            break;
        }
    }

    fclose(fp);

    if (bytes_received == info.file_size) {
        /* Verify total file CRC32 */
        FILE *verify_fp = fopen(out_name, "rb");
        uint32_t actual_crc = ~0U;
        if (verify_fp) {
            char chunk[RUFTP_CHUNK_SIZE];
            size_t r;
            while ((r = fread(chunk, 1, sizeof(chunk), verify_fp)) > 0) {
                const uint8_t *p = (const uint8_t *)chunk;
                while (r--) {
                    actual_crc ^= *p++;
                    for (int k = 0; k < 8; k++) {
                        actual_crc = (actual_crc >> 1) ^ (0xEDB88320U & -(actual_crc & 1U));
                    }
                }
            }
            actual_crc = ~actual_crc;
            fclose(verify_fp);
        }

        if (actual_crc == info.file_crc32) {
            printf("[SUCCESS] Download of '%s' completed! (%lu bytes, CRC verified: 0x%08X)\n",
                   out_name, (unsigned long)bytes_received, actual_crc);
            return 0;
        } else {
            fprintf(stderr, "[!] Corruption: expected CRC 0x%08X, got 0x%08X\n", info.file_crc32, actual_crc);
            return -1;
        }
    } else {
        fprintf(stderr, "[-] Download aborted: received %lu / %lu bytes\n",
                (unsigned long)bytes_received, (unsigned long)info.file_size);
        unlink(out_name);
        return -1;
    }
}

/* Display interactive CLI menu */
static void print_menu(void) {
    printf("\n=========================================\n");
    printf("   RELIABLE UDP FILE TRANSFER (RUFTP)   \n");
    printf("=========================================\n");
    printf(" 1. Upload file   (Syntax: upload <filepath>)\n");
    printf(" 2. Download file (Syntax: download <remote_file> [save_path])\n");
    printf(" 3. Exit\n");
    printf("-----------------------------------------\n");
    printf("Select option [1-3] or type command: ");
    fflush(stdout);
}

/* Main entry point for UDP File Client */
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <IP_Addr> <Port_Number> [command] [args...]\n", argv[0]);
        fprintf(stderr, "Interactive mode: %s 127.0.0.1 6000\n", argv[0]);
        fprintf(stderr, "Direct upload:    %s 127.0.0.1 6000 upload <filepath>\n", argv[0]);
        fprintf(stderr, "Direct download:  %s 127.0.0.1 6000 download <remote_file> [save_path]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    if (port <= 1024 || port > 65535) {
        fprintf(stderr, "Error: Invalid port '%s'. Must be 1025-65535.\n", argv[2]);
        return EXIT_FAILURE;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        fprintf(stderr, "Error: Invalid IPv4 address '%s'.\n", server_ip);
        close(sockfd);
        return EXIT_FAILURE;
    }

    /* Support direct command line execution */
    if (argc >= 4) {
        if (strcasecmp(argv[3], "upload") == 0) {
            if (argc < 5) {
                fprintf(stderr, "Usage: %s <IP> <Port> upload <filepath>\n", argv[0]);
                close(sockfd);
                return EXIT_FAILURE;
            }
            int res = do_upload(sockfd, argv[4], &server_addr, sizeof(server_addr));
            close(sockfd);
            return (res == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
        } else if (strcasecmp(argv[3], "download") == 0) {
            if (argc < 5) {
                fprintf(stderr, "Usage: %s <IP> <Port> download <remote_file> [save_path]\n", argv[0]);
                close(sockfd);
                return EXIT_FAILURE;
            }
            const char *save_path = (argc >= 6) ? argv[5] : argv[4];
            int res = do_download(sockfd, argv[4], save_path, &server_addr, sizeof(server_addr));
            close(sockfd);
            return (res == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
        }
    }

    /* Interactive Menu Loop */
    char line[512];
    while (1) {
        print_menu();
        if (!fgets(line, sizeof(line), stdin)) break;
        trim_line(line);
        if (line[0] == '\0') continue;

        if (strcmp(line, "1") == 0) {
            char path[512] = "";
            printf("Enter local file path to upload: ");
            fflush(stdout);
            if (!fgets(path, sizeof(path), stdin)) break;
            trim_line(path);
            if (path[0] != '\0') {
                do_upload(sockfd, path, &server_addr, sizeof(server_addr));
            }
        }
        else if (strcmp(line, "2") == 0) {
            char remote[RUFTP_FILENAME_MAX] = "";
            char local[512] = "";
            printf("Enter remote file name on server: ");
            fflush(stdout);
            if (!fgets(remote, sizeof(remote), stdin)) break;
            trim_line(remote);

            printf("Enter local destination path (press Enter for default): ");
            fflush(stdout);
            if (!fgets(local, sizeof(local), stdin)) break;
            trim_line(local);

            if (remote[0] != '\0') {
                do_download(sockfd, remote, local[0] != '\0' ? local : remote, &server_addr, sizeof(server_addr));
            }
        }
        else if (strcmp(line, "3") == 0 || strcasecmp(line, "exit") == 0 || strcasecmp(line, "quit") == 0) {
            printf("Goodbye.\n");
            break;
        }
        else {
            char cmd[64] = "";
            char arg1[512] = "";
            char arg2[512] = "";
            int cnt = sscanf(line, "%63s %511s %511s", cmd, arg1, arg2);
            if (cnt >= 2 && strcasecmp(cmd, "upload") == 0) {
                do_upload(sockfd, arg1, &server_addr, sizeof(server_addr));
            } else if (cnt >= 2 && strcasecmp(cmd, "download") == 0) {
                const char *save_path = (cnt >= 3) ? arg2 : arg1;
                do_download(sockfd, arg1, save_path, &server_addr, sizeof(server_addr));
            } else {
                printf("Unknown command. Available: upload <path>, download <file> [dest], exit\n");
            }
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
