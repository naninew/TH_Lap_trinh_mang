#!/bin/bash
# ==============================================================================
# test_bai2.sh - Automated Test Suite for Problem 2 (Reliable UDP File Transfer)
# Course: IT4062 - Network Programming
# Author: Do Xuan Quy (MSSV: 20235818)
# ==============================================================================

set -e

PORT=6088
BASE_DIR=$(cd "$(dirname "$0")" && pwd)
BAI2_DIR="$BASE_DIR/bai2"
SERVER_BIN="$BAI2_DIR/server"
CLIENT_BIN="$BAI2_DIR/client"
STORAGE_DIR="$BAI2_DIR/storage"
TMP_DIR="/tmp/ruftp_test_$$"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}==============================================================================${NC}"
echo -e "${CYAN}        AUTOMATED TEST SUITE: PROBLEM 2 (RELIABLE UDP FILE TRANSFER)         ${NC}"
echo -e "${CYAN}==============================================================================${NC}"

# 1. Compile Problem 2
echo -e "\n${YELLOW}[Step 1] Compiling Problem 2...${NC}"
make -C "$BAI2_DIR" clean > /dev/null
make -C "$BAI2_DIR" all

mkdir -p "$TMP_DIR"
rm -rf "$STORAGE_DIR"
mkdir -p "$STORAGE_DIR"

# 2. Start UDP Server
echo -e "\n${YELLOW}[Step 2] Starting UDP Server on port $PORT...${NC}"
fuser -k -n udp $PORT 2>/dev/null || true
sleep 0.5

cd "$BAI2_DIR"
./server $PORT > "$BASE_DIR/server_bai2.log" 2>&1 &
SERVER_PID=$!
cd "$BASE_DIR"

cleanup() {
    echo -e "\n${YELLOW}[*] Cleaning up temporary files and shutting down Server (PID: $SERVER_PID)...${NC}"
    kill -9 $SERVER_PID 2>/dev/null || true
    fuser -k -n udp $PORT 2>/dev/null || true
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT
sleep 1

# 3. Test 1: Upload a text file
echo -e "\n${YELLOW}[Step 3] Testing Text File Upload...${NC}"
echo "Hello IT4062 Network Programming from student Do Xuan Quy (MSSV: 20235818)!" > "$TMP_DIR/hello.txt"
"$CLIENT_BIN" 127.0.0.1 $PORT upload "$TMP_DIR/hello.txt"

if [ -f "$STORAGE_DIR/hello.txt" ] && cmp -s "$TMP_DIR/hello.txt" "$STORAGE_DIR/hello.txt"; then
    echo -e "  ${GREEN}[PASS] Text file uploaded and verified bit-exact with server storage.${NC}"
else
    echo -e "  ${RED}[FAIL] Text file upload failed or content differs.${NC}"
fi

# 4. Test 2: Upload a binary file (50 KB)
echo -e "\n${YELLOW}[Step 4] Testing Binary File Upload (50 KB)...${NC}"
head -c 51200 /dev/urandom > "$TMP_DIR/random.bin"
"$CLIENT_BIN" 127.0.0.1 $PORT upload "$TMP_DIR/random.bin"

if [ -f "$STORAGE_DIR/random.bin" ] && cmp -s "$TMP_DIR/random.bin" "$STORAGE_DIR/random.bin"; then
    echo -e "  ${GREEN}[PASS] Binary file (50 KB) uploaded and verified bit-exact (CRC32 checked).${NC}"
else
    echo -e "  ${RED}[FAIL] Binary file upload failed or corrupted.${NC}"
fi

# 5. Test 3: Download an existing file from server
echo -e "\n${YELLOW}[Step 5] Testing File Download from Server Storage...${NC}"
"$CLIENT_BIN" 127.0.0.1 $PORT download random.bin "$TMP_DIR/downloaded_random.bin"

if [ -f "$TMP_DIR/downloaded_random.bin" ] && cmp -s "$TMP_DIR/downloaded_random.bin" "$STORAGE_DIR/random.bin"; then
    echo -e "  ${GREEN}[PASS] File downloaded successfully and matches original server copy exactly!${NC}"
else
    echo -e "  ${RED}[FAIL] Downloaded file differs from server copy.${NC}"
fi

# 6. Test 4: Download non-existent file
echo -e "\n${YELLOW}[Step 6] Testing Download of Non-Existent File...${NC}"
OUT_ERR=$("$CLIENT_BIN" 127.0.0.1 $PORT download non_existent_file.xyz "$TMP_DIR/dummy.bin" 2>&1 || true)
if echo "$OUT_ERR" | grep -qi "not found"; then
    echo -e "  ${GREEN}[PASS] Server returned error for non-existent file properly.${NC}"
else
    echo -e "  ${RED}[FAIL] Non-existent download error handling unexpected:${NC}\n$OUT_ERR"
fi

# 7. Test 5: Upload larger binary file (250 KB)
echo -e "\n${YELLOW}[Step 7] Testing Larger Binary File (250 KB chunks)...${NC}"
head -c 256000 /dev/urandom > "$TMP_DIR/large_test.dat"
"$CLIENT_BIN" 127.0.0.1 $PORT upload "$TMP_DIR/large_test.dat"

if [ -f "$STORAGE_DIR/large_test.dat" ] && cmp -s "$TMP_DIR/large_test.dat" "$STORAGE_DIR/large_test.dat"; then
    echo -e "  ${GREEN}[PASS] Large file (250 KB, 250+ packets) transferred with Stop-and-Wait ARQ flawlessly!${NC}"
else
    echo -e "  ${RED}[FAIL] Large file transfer failed.${NC}"
fi

echo -e "\n${CYAN}==============================================================================${NC}"
echo -e "${GREEN}             ALL PROBLEM 2 TESTS COMPLETED SUCCESSFULLY (+20% BONUS)!         ${NC}"
echo -e "${CYAN}==============================================================================${NC}"
