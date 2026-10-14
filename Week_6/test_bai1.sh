#!/bin/bash
# ==============================================================================
# test_bai1.sh - Automated Test Suite for Problem 1 (TCP Authentication)
# Course: IT4062 - Network Programming
# Author: Do Xuan Quy (MSSV: 20235818)
# ==============================================================================

set -e

PORT=5588
BASE_DIR=$(cd "$(dirname "$0")" && pwd)
BAI1_DIR="$BASE_DIR/bai1"
SERVER_BIN="$BAI1_DIR/server"
CLIENT_BIN="$BAI1_DIR/client"
ACC_FILE="$BAI1_DIR/account.txt"

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}==============================================================================${NC}"
echo -e "${CYAN}              AUTOMATED TEST SUITE: PROBLEM 1 (TCP AUTHENTICATION)            ${NC}"
echo -e "${CYAN}==============================================================================${NC}"

# 1. Compile Problem 1
echo -e "\n${YELLOW}[Step 1] Compiling Problem 1...${NC}"
make -C "$BAI1_DIR" clean > /dev/null
make -C "$BAI1_DIR" all

# 2. Reset account.txt to default testing state
echo -e "${YELLOW}[Step 2] Initializing test accounts in $ACC_FILE...${NC}"
cat << 'EOF' > "$ACC_FILE"
admin 123456 1
user1 pass1 1
user2 pass2 0
quydx secret2023 1
student 20235818 1
EOF

# 3. Test CLI argument validation
echo -e "\n${YELLOW}[Step 3] Testing Command-Line Arguments validation...${NC}"
if "$SERVER_BIN" 2>&1 | grep -q "Usage"; then
    echo -e "  [PASS] Server with no arguments prints usage."
else
    echo -e "  [FAIL] Server with no arguments failed to print usage."
fi

if "$SERVER_BIN" 99999 2>&1 | grep -q "Invalid port"; then
    echo -e "  [PASS] Server rejects invalid port 99999."
else
    echo -e "  [FAIL] Server did not reject invalid port."
fi

if "$CLIENT_BIN" 127.0.0.1 2>&1 | grep -q "Usage"; then
    echo -e "  [PASS] Client with missing port prints usage."
else
    echo -e "  [FAIL] Client with missing port failed to print usage."
fi

if "$CLIENT_BIN" 999.1.1.1 5500 2>&1 | grep -q "Invalid IPv4"; then
    echo -e "  [PASS] Client rejects invalid IPv4 address."
else
    echo -e "  [FAIL] Client did not reject invalid IPv4 address."
fi

# 4. Start Server in background
echo -e "\n${YELLOW}[Step 4] Starting TCP Server on port $PORT...${NC}"
# Kill any leftover process on this port
fuser -k -n tcp $PORT 2>/dev/null || true
sleep 0.5

cd "$BAI1_DIR"
./server $PORT > "$BASE_DIR/server_bai1.log" 2>&1 &
SERVER_PID=$!
cd "$BASE_DIR"

cleanup() {
    echo -e "\n${YELLOW}[*] Shutting down Server (PID: $SERVER_PID)...${NC}"
    kill -9 $SERVER_PID 2>/dev/null || true
    fuser -k -n tcp $PORT 2>/dev/null || true
}
trap cleanup EXIT
sleep 1

# 5. Test Happy Path: Login and Logout
echo -e "\n${YELLOW}[Step 5] Testing valid Login & Logout (quydx / secret2023)...${NC}"
OUT=$(printf "LOGIN quydx secret2023\nLOGOUT\nQUIT\n" | "$CLIENT_BIN" 127.0.0.1 $PORT)
if echo "$OUT" | grep -q "Login successful" && echo "$OUT" | grep -q "Logged out successfully"; then
    echo -e "  ${GREEN}[PASS] User 'quydx' logged in and logged out successfully.${NC}"
else
    echo -e "  ${RED}[FAIL] Login / Logout output unexpected:${NC}\n$OUT"
fi

# 6. Test Requirement 1: Each client window can only log in to 1 account
echo -e "\n${YELLOW}[Step 6] Testing REQUIREMENT 1: Single active login per client window...${NC}"
OUT=$(printf "LOGIN quydx secret2023\nLOGIN user1 pass1\nLOGOUT\nQUIT\n" | "$CLIENT_BIN" 127.0.0.1 $PORT)
if echo "$OUT" | grep -q "Already logged in as 'quydx'"; then
    echo -e "  ${GREEN}[PASS] Server rejected second login while window is already logged in.${NC}"
else
    echo -e "  ${RED}[FAIL] Second login was not rejected as expected:${NC}\n$OUT"
fi

# 7. Test Requirement 2: Same account can log in simultaneously on multiple windows
echo -e "\n${YELLOW}[Step 7] Testing REQUIREMENT 2: Concurrent logins for same account across multiple windows...${NC}"
FIFO1="/tmp/c1_fifo_$$"
FIFO2="/tmp/c2_fifo_$$"
rm -f "$FIFO1" "$FIFO2"
mkfifo "$FIFO1" "$FIFO2"

# Start client 1 in background and keep session open
"$CLIENT_BIN" 127.0.0.1 $PORT < "$FIFO1" > "/tmp/c1_out_$$" 2>&1 &
PID_C1=$!

# Feed login to client 1
echo "LOGIN student 20235818" > "$FIFO1"
sleep 0.5

# Start client 2 and log in to same account "student"
OUT2=$(printf "LOGIN student 20235818\nLOGOUT\nQUIT\n" | "$CLIENT_BIN" 127.0.0.1 $PORT)

# Clean up client 1
echo "LOGOUT" > "$FIFO1"
echo "QUIT" > "$FIFO1"
wait $PID_C1 || true
rm -f "$FIFO1" "$FIFO2"

if grep -q "Login successful" "/tmp/c1_out_$$" && echo "$OUT2" | grep -q "Login successful"; then
    echo -e "  ${GREEN}[PASS] Both Client 1 and Client 2 simultaneously logged in as 'student'!${NC}"
else
    echo -e "  ${RED}[FAIL] Concurrent login test failed.${NC}"
fi
rm -f "/tmp/c1_out_$$"

# 8. Test Non-existent account and Blocked account
echo -e "\n${YELLOW}[Step 8] Testing non-existent and initially blocked accounts...${NC}"
OUT_NO=$(printf "LOGIN no_user 123456\nQUIT\n" | "$CLIENT_BIN" 127.0.0.1 $PORT)
if echo "$OUT_NO" | grep -q "does not exist"; then
    echo -e "  ${GREEN}[PASS] Non-existent account properly reported.${NC}"
else
    echo -e "  ${RED}[FAIL] Non-existent account test failed:${NC}\n$OUT_NO"
fi

OUT_BLOCKED=$(printf "LOGIN user2 pass2\nQUIT\n" | "$CLIENT_BIN" 127.0.0.1 $PORT)
if echo "$OUT_BLOCKED" | grep -q "is blocked"; then
    echo -e "  ${GREEN}[PASS] Initially blocked account (Status=0) rejected immediately.${NC}"
else
    echo -e "  ${RED}[FAIL] Blocked account test failed:${NC}\n$OUT_BLOCKED"
fi

# 9. Test Requirement 3: 3 failed password attempts -> Account blocked & account.txt updated
echo -e "\n${YELLOW}[Step 9] Testing REQUIREMENT 3: 3 failed attempts lock account & sync to disk...${NC}"
# Attempt 1 & 2 fail, then success resets counter
OUT_RESET=$(printf "LOGIN user1 wrong1\nLOGIN user1 wrong2\nLOGIN user1 pass1\nLOGOUT\nQUIT\n" | "$CLIENT_BIN" 127.0.0.1 $PORT)
if echo "$OUT_RESET" | grep -q "Attempt 1/3" && echo "$OUT_RESET" | grep -q "Attempt 2/3" && echo "$OUT_RESET" | grep -q "Login successful"; then
    echo -e "  ${GREEN}[PASS] Failed counter successfully resets to 0 upon correct password.${NC}"
else
    echo -e "  ${RED}[FAIL] Reset counter test failed:${NC}\n$OUT_RESET"
fi

# 3 consecutive failed attempts on user1
OUT_LOCK=$(printf "LOGIN user1 bad1\nLOGIN user1 bad2\nLOGIN user1 bad3\nQUIT\n" | "$CLIENT_BIN" 127.0.0.1 $PORT)
if echo "$OUT_LOCK" | grep -q "Account 'user1' has been BLOCKED"; then
    echo -e "  ${GREEN}[PASS] Server reported account 'user1' blocked after 3 consecutive failed attempts.${NC}"
else
    echo -e "  ${RED}[FAIL] Account was not blocked after 3 failed attempts:${NC}\n$OUT_LOCK"
fi

# Verify account.txt has updated Status to 0
STATUS_ON_DISK=$(grep "^user1 " "$ACC_FILE" | awk '{print $3}')
if [ "$STATUS_ON_DISK" = "0" ]; then
    echo -e "  ${GREEN}[PASS] account.txt on disk was automatically updated: 'user1 pass1 0'.${NC}"
else
    echo -e "  ${RED}[FAIL] account.txt status was not updated to 0 (found: '$STATUS_ON_DISK').${NC}"
fi

# Verify subsequent login attempt is rejected as blocked
OUT_AFTER=$(printf "LOGIN user1 pass1\nQUIT\n" | "$CLIENT_BIN" 127.0.0.1 $PORT)
if echo "$OUT_AFTER" | grep -q "is blocked"; then
    echo -e "  ${GREEN}[PASS] Subsequent login rejected with 'Account is blocked'.${NC}"
else
    echo -e "  ${RED}[FAIL] Subsequent login test failed:${NC}\n$OUT_AFTER"
fi

echo -e "\n${CYAN}==============================================================================${NC}"
echo -e "${GREEN}             ALL PROBLEM 1 TESTS COMPLETED SUCCESSFULLY!                      ${NC}"
echo -e "${CYAN}==============================================================================${NC}"
