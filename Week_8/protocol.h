/*
 * protocol.h - Caesar Cipher Application Protocol
 * Course: IT4062 - Network Programming (Week 7 / Week 8)
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [PROTOCOL FRAMING] Opcode (1 byte), Length (2 bytes, big-endian), Payload
 *   Opcode 0: Encrypt (Payload = Key string)
 *   Opcode 1: Decrypt (Payload = Key string)
 *   Opcode 2: File Data (Length > 0: chunk data, Length = 0: EOF marker)
 *   Opcode 3: Error Notification (Length = 0, no payload)
 */

#ifndef CAESAR_PROTOCOL_H
#define CAESAR_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define OP_ENCRYPT   0
#define OP_DECRYPT   1
#define OP_DATA      2
#define OP_ERROR     3

#define MAX_CHUNK_SIZE 2048

#pragma pack(push, 1)
typedef struct {
    uint8_t  opcode;  /* 0: Encrypt, 1: Decrypt, 2: File Data, 3: Error */
    uint16_t length;  /* Payload length in bytes (Network Byte Order)   */
} CaesarHeader;
#pragma pack(pop)

#endif /* CAESAR_PROTOCOL_H */

