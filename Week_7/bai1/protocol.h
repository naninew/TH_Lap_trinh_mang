/*
 * protocol.h - Problem 1: Caesar Cipher Application Protocol
 * Course: IT4062 - Network Programming
 * Author: Do Xuan Quy (MSSV: 20235818)
 *
 * Requirements mapped:
 * - [BAI 1 PROTOCOL] Opcode (1 byte), Length (2 bytes), Payload framing
 *   Opcode 0: Encrypt (Payload = Key)
 *   Opcode 1: Decrypt (Payload = Key)
 *   Opcode 2: File Data (Length > 0: chunk, Length = 0: EOF)
 *   Opcode 3: Error (Length = 0)
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
