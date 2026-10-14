/*
 * protocol.h - Problem 2: Reliable UDP File Transfer Protocol (RUFTP)
 * Course: IT4062 - Network Programming
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Protocol Overview (RFC-style design):
 * - Transport layer: UDP (SOCK_DGRAM)
 * - Framing: Fixed header (19 bytes) + Payload (up to 1024 bytes) = max 1043 bytes (under MTU 1500)
 * - Reliability: Stop-and-Wait ARQ with sequence numbers, timeouts, and retransmissions.
 * - Integrity: IEEE 802.3 CRC32 checksum over both data chunks and entire files.
 */

#ifndef RUFTP_PROTOCOL_H
#define RUFTP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define RUFTP_MAGIC           0x52554654  /* "RUFT" in ASCII */
#define RUFTP_CHUNK_SIZE      1024
#define RUFTP_FILENAME_MAX    255
#define RUFTP_TIMEOUT_MS      500         /* 500 milliseconds RTO */
#define RUFTP_MAX_RETRIES     6           /* Maximum retransmission attempts */

/* Message Types */
typedef enum {
    RUFTP_TYPE_REQ_UPLOAD   = 1, /* Client -> Server: Request upload (filename, size, file CRC32) */
    RUFTP_TYPE_REQ_DOWNLOAD = 2, /* Client -> Server: Request download (filename) */
    RUFTP_TYPE_RES_DOWNLOAD = 3, /* Server -> Client: Download metadata (size, file CRC32) */
    RUFTP_TYPE_DATA         = 4, /* Data chunk with sequence number */
    RUFTP_TYPE_ACK          = 5, /* Acknowledgment packet */
    RUFTP_TYPE_ERR          = 6, /* Error packet */
    RUFTP_TYPE_FIN          = 7  /* Final packet acknowledging transfer complete */
} RuftpType;

/* Packet Structure (Network Wire Format) */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;                    /* Magic header (RUFTP_MAGIC) */
    uint8_t  type;                     /* Message type (RuftpType) */
    uint32_t seq_num;                  /* Packet sequence number (0, 1, 2, ...) */
    uint32_t ack_num;                  /* Acknowledged sequence number */
    uint16_t payload_len;              /* Actual length of data in payload */
    uint32_t checksum;                 /* CRC32 of payload data */
    char     payload[RUFTP_CHUNK_SIZE];/* Data payload or control message */
} RuftpPacket;

/* Upload / Download control payload */
typedef struct {
    char     filename[RUFTP_FILENAME_MAX];
    uint64_t file_size;
    uint32_t file_crc32;
} RuftpFileInfo;
#pragma pack(pop)

/* Standard IEEE 802.3 CRC32 implementation */
static inline uint32_t ruftp_crc32(const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t crc = ~0U;
    while (len--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
        }
    }
    return ~crc;
}

#endif /* RUFTP_PROTOCOL_H */
