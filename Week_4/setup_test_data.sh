#!/bin/bash
# ==============================================================================
# setup_test_data.sh - Setup test files for Week 4 (IT4062)
# Usage on WSL / Kali Linux:
#   chmod +x setup_test_data.sh
#   ./setup_test_data.sh              # creates files in ~/test
#   ./setup_test_data.sh /tmp/mytest  # creates files in /tmp/mytest
# ==============================================================================

TARGET_DIR="${1:-$HOME/test}"

echo "==> Creating test directory: $TARGET_DIR"
mkdir -p "$TARGET_DIR"

# 1. Text file
echo "hello world" > "$TARGET_DIR/testfile.txt"

# 2. Sample image file (testfile.jpg)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/testfile.jpg" ]; then
    cp "$SCRIPT_DIR/testfile.jpg" "$TARGET_DIR/testfile.jpg"
elif [ -f "$SCRIPT_DIR/test_data/testfile.jpg" ]; then
    cp "$SCRIPT_DIR/test_data/testfile.jpg" "$TARGET_DIR/testfile.jpg"
else
    # Fallback minimal valid 1x1 JPEG
    echo '/9j/4AAQSkZJRgABAQEASABIAAD/2wBDAP//////////////////////////////////////////////////////////////////////////////////////wgALCAABAAEBAREA/8QAFBABAAAAAAAAAAAAAAAAAAAAAP/aAAgBAQABPxA=' | base64 -d > "$TARGET_DIR/testfile.jpg"
fi

# 3. Empty file (0 bytes)
: > "$TARGET_DIR/empty.bin"

# 4. File name with spaces
echo "File content with spaces" > "$TARGET_DIR/file with spaces.txt"

# 5. Directory (to test "Error: Not a regular file")
mkdir -p "$TARGET_DIR/folder1"

# 6. Large 100MB file for Bai 2 test
echo "==> Generating 100MB test file: $TARGET_DIR/big100.bin ..."
if command -v fallocate >/dev/null 2>&1; then
    fallocate -l 100M "$TARGET_DIR/big100.bin" 2>/dev/null || head -c 100M /dev/urandom > "$TARGET_DIR/big100.bin"
else
    head -c 100M /dev/urandom > "$TARGET_DIR/big100.bin"
fi

echo ""
echo "==> Test files created in $TARGET_DIR:"
ls -lh "$TARGET_DIR"
echo ""
echo "==> SHA256 checksums:"
sha256sum "$TARGET_DIR/testfile.jpg" "$TARGET_DIR/big100.bin"
echo ""
echo "==> Setup complete! You are ready to run tests."
