/*
 * server.c - Problem 2: Reliable UDP File Transfer Server (RUFTP)
 * Course: IT4062 - Network Programming
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [BAI 2 PROTOCOL] Reliable transport over UDP using RUFTP protocol.
 * - [BAI 2 ARQ] Stop-and-Wait ARQ with sequence numbers, timeouts, and CRC32 verification.
 * - [BAI 2 UPLOAD] Handles file uploads and stores files safely into storage/ directory.
 * - [BAI 2 DOWNLOAD] Handles file downloads from storage/ directory to clients.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <libgen.h>

#include "protocol.h"

#define STORAGE_DIR "storage"

/* Ensure storage directory exists */
static void ensure_storage_dir(void) {
    struct stat st;
    if (stat(STORAGE_DIR, &st) == -1) {
        mkdir(STORAGE_DIR, 0755);
    }
}

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

/* Send an error packet */
static void send_error(int sockfd, const char *msg, const struct sockaddr_in *to, socklen_t to_len) {
    RuftpPacket err_pkt;
    memset(&err_pkt, 0, sizeof(err_pkt));
    err_pkt.type = RUFTP_TYPE_ERR;
    err_pkt.payload_len = (uint16_t)strlen(msg);
    strncpy(err_pkt.payload, msg, RUFTP_CHUNK_SIZE - 1);
    send_packet(sockfd, &err_pkt, to, to_len);
}

/* Send an ACK packet */
static void send_ack(int sockfd, uint32_t ack_num, const struct sockaddr_in *to, socklen_t to_len) {
    RuftpPacket ack_pkt;
    memset(&ack_pkt, 0, sizeof(ack_pkt));
    ack_pkt.type = RUFTP_TYPE_ACK;
    ack_pkt.ack_num = ack_num;
    send_packet(sockfd, &ack_pkt, to, to_len);
}

/* [BAI 2 UPLOAD] Process file upload from client */
static void handle_upload(int sockfd, const RuftpFileInfo *info, const struct sockaddr_in *client_addr, socklen_t addr_len) {
    char safe_name[RUFTP_FILENAME_MAX];
    /* Sanitize filename: extract basename */
    char temp[RUFTP_FILENAME_MAX];
    strncpy(temp, info->filename, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char *base = basename(temp);
    strncpy(safe_name, base, sizeof(safe_name) - 1);
    safe_name[sizeof(safe_name) - 1] = '\0';

    char target_path[512];
    snprintf(target_path, sizeof(target_path), "%s/%s", STORAGE_DIR, safe_name);

    printf("[UPLOAD] Incoming file: '%s' (Size: %lu bytes, CRC: 0x%08X)\n",
           safe_name, (unsigned long)info->file_size, info->file_crc32);

    FILE *fp = fopen(target_path, "wb");
    if (!fp) {
        perror("fopen upload target");
        send_error(sockfd, "Server failed to create target file", client_addr, addr_len);
        return;
    }

    /* Send initial ACK for the upload request */
    send_ack(sockfd, 0, client_addr, addr_len);

    uint32_t expected_seq = 0;
    uint64_t bytes_received = 0;
    int retries = 0;

    while (bytes_received < info->file_size) {
        RuftpPacket data_pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        int recvd = recv_packet_timeout(sockfd, &data_pkt, &from_addr, &from_len, RUFTP_TIMEOUT_MS * 2);
        if (recvd <= 0) {
            retries++;
            if (retries > RUFTP_MAX_RETRIES) {
                printf("[-] Upload timed out waiting for chunk %u.\n", expected_seq);
                break;
            }
            /* Re-send ACK for previous sequence */
            send_ack(sockfd, expected_seq > 0 ? expected_seq - 1 : 0, client_addr, addr_len);
            continue;
        }

        retries = 0;

        if (data_pkt.type != RUFTP_TYPE_DATA) {
            continue;
        }

        /* Check data chunk integrity */
        uint32_t calc_crc = ruftp_crc32(data_pkt.payload, data_pkt.payload_len);
        if (calc_crc != data_pkt.checksum) {
            printf("[!] Checksum mismatch on chunk %u. Requesting retransmission.\n", data_pkt.seq_num);
            send_ack(sockfd, expected_seq > 0 ? expected_seq - 1 : 0, client_addr, addr_len);
            continue;
        }

        if (data_pkt.seq_num == expected_seq) {
            /* [BAI 2 ARQ] In-order chunk received: write to file */
            size_t written = fwrite(data_pkt.payload, 1, data_pkt.payload_len, fp);
            if (written != data_pkt.payload_len) {
                perror("fwrite");
                send_error(sockfd, "Disk write error", client_addr, addr_len);
                break;
            }
            bytes_received += written;
            send_ack(sockfd, expected_seq, client_addr, addr_len);
            expected_seq++;
        } else if (data_pkt.seq_num < expected_seq) {
            /* Duplicate chunk: resend ACK */
            send_ack(sockfd, data_pkt.seq_num, client_addr, addr_len);
        }
    }

    fclose(fp);

    if (bytes_received == info->file_size) {
        /* Verify total file CRC32 */
        FILE *verify_fp = fopen(target_path, "rb");
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

        if (actual_crc == info->file_crc32) {
            printf("[+] File '%s' uploaded successfully! (CRC verified: 0x%08X)\n", safe_name, actual_crc);
            RuftpPacket fin_pkt;
            memset(&fin_pkt, 0, sizeof(fin_pkt));
            fin_pkt.type = RUFTP_TYPE_FIN;
            send_packet(sockfd, &fin_pkt, client_addr, addr_len);
        } else {
            printf("[!] Upload corrupted: expected CRC 0x%08X but got 0x%08X\n", info->file_crc32, actual_crc);
            send_error(sockfd, "File corruption detected (CRC mismatch)", client_addr, addr_len);
        }
    } else {
        printf("[-] Upload aborted: received %lu / %lu bytes\n", (unsigned long)bytes_received, (unsigned long)info->file_size);
        unlink(target_path); /* Clean up partial file */
    }
}

/* [BAI 2 DOWNLOAD] Process file download to client */
static void handle_download(int sockfd, const char *req_filename, const struct sockaddr_in *client_addr, socklen_t addr_len) {
    char safe_name[RUFTP_FILENAME_MAX];
    char temp[RUFTP_FILENAME_MAX];
    strncpy(temp, req_filename, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    char *base = basename(temp);
    strncpy(safe_name, base, sizeof(safe_name) - 1);
    safe_name[sizeof(safe_name) - 1] = '\0';

    char target_path[512];
    snprintf(target_path, sizeof(target_path), "%s/%s", STORAGE_DIR, safe_name);

    FILE *fp = fopen(target_path, "rb");
    if (!fp) {
        printf("[-] Download requested file '%s' not found.\n", safe_name);
        send_error(sockfd, "File not found in storage", client_addr, addr_len);
        return;
    }

    /* Compute file size and total CRC32 */
    fseek(fp, 0, SEEK_END);
    uint64_t file_size = (uint64_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

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

    printf("[DOWNLOAD] Sending file: '%s' (Size: %lu bytes, CRC: 0x%08X)\n",
           safe_name, (unsigned long)file_size, actual_crc);

    /* Send metadata response */
    RuftpFileInfo info;
    memset(&info, 0, sizeof(info));
    strncpy(info.filename, safe_name, sizeof(info.filename) - 1);
    info.file_size = file_size;
    info.file_crc32 = actual_crc;

    RuftpPacket res_pkt;
    memset(&res_pkt, 0, sizeof(res_pkt));
    res_pkt.type = RUFTP_TYPE_RES_DOWNLOAD;
    res_pkt.payload_len = sizeof(RuftpFileInfo);
    memcpy(res_pkt.payload, &info, sizeof(RuftpFileInfo));

    /* Wait for client ACK for download metadata */
    int acked = 0;
    for (int retry = 0; retry < RUFTP_MAX_RETRIES && !acked; retry++) {
        send_packet(sockfd, &res_pkt, client_addr, addr_len);

        RuftpPacket ack_pkt;
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        if (recv_packet_timeout(sockfd, &ack_pkt, &from_addr, &from_len, RUFTP_TIMEOUT_MS) > 0) {
            if (ack_pkt.type == RUFTP_TYPE_ACK) {
                acked = 1;
            }
        }
    }

    if (!acked) {
        printf("[-] Client failed to ACK download start.\n");
        fclose(fp);
        return;
    }

    /* [BAI 2 ARQ] Send data chunks with Stop-and-Wait ARQ */
    uint32_t seq_num = 0;
    uint64_t bytes_sent = 0;

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
            send_packet(sockfd, &data_pkt, client_addr, addr_len);

            RuftpPacket ack_pkt;
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            if (recv_packet_timeout(sockfd, &ack_pkt, &from_addr, &from_len, RUFTP_TIMEOUT_MS) > 0) {
                if (ack_pkt.type == RUFTP_TYPE_ACK && ack_pkt.ack_num == seq_num) {
                    chunk_acked = 1;
                }
            }
        }

        if (!chunk_acked) {
            printf("[-] Timeout retransmitting chunk %u. Aborting.\n", seq_num);
            break;
        }

        bytes_sent += data_pkt.payload_len;
        seq_num++;
    }

    fclose(fp);

    if (bytes_sent == file_size) {
        printf("[+] Download of '%s' completed successfully (%lu bytes).\n", safe_name, (unsigned long)bytes_sent);
        RuftpPacket fin_pkt;
        memset(&fin_pkt, 0, sizeof(fin_pkt));
        fin_pkt.type = RUFTP_TYPE_FIN;
        for (int i = 0; i < 3; i++) {
            send_packet(sockfd, &fin_pkt, client_addr, addr_len);
            usleep(10000);
        }
    }
}

/* Main entry point for UDP File Server */
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <Port_Number>\n", argv[0]);
        fprintf(stderr, "Example: %s 6000\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    if (port <= 1024 || port > 65535) {
        fprintf(stderr, "Error: Invalid port '%s'. Must be 1025-65535.\n", argv[1]);
        return EXIT_FAILURE;
    }

    ensure_storage_dir();

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons((uint16_t)port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("====================================================\n");
    printf("[*] Reliable UDP File Transfer (RUFTP) Server running on port %d\n", port);
    printf("[*] File repository directory: ./%s/\n", STORAGE_DIR);
    printf("====================================================\n");

    while (1) {
        RuftpPacket pkt;
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        ssize_t n = recvfrom(sockfd, &pkt, sizeof(RuftpPacket), 0, (struct sockaddr *)&client_addr, &client_len);
        if (n < (ssize_t)(sizeof(RuftpPacket) - RUFTP_CHUNK_SIZE)) continue;

        pkt.magic = ntohl(pkt.magic);
        pkt.seq_num = ntohl(pkt.seq_num);
        pkt.ack_num = ntohl(pkt.ack_num);
        pkt.payload_len = ntohs(pkt.payload_len);
        pkt.checksum = ntohl(pkt.checksum);

        if (pkt.magic != RUFTP_MAGIC) continue;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        if (pkt.type == RUFTP_TYPE_REQ_UPLOAD) {
            RuftpFileInfo info;
            memcpy(&info, pkt.payload, sizeof(RuftpFileInfo));
            printf("[*] [%s:%d] Requested Upload: %s\n", client_ip, client_port, info.filename);
            handle_upload(sockfd, &info, &client_addr, client_len);
        }
        else if (pkt.type == RUFTP_TYPE_REQ_DOWNLOAD) {
            printf("[*] [%s:%d] Requested Download: %s\n", client_ip, client_port, pkt.payload);
            handle_download(sockfd, pkt.payload, &client_addr, client_len);
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
