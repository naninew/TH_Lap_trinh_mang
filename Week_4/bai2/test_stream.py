#!/usr/bin/env python3
"""
Test script for Problem 2 (IT4062 - Week 4)
Tests TCP Byte Stream handling, protocol edge cases, path traversal protection,
and concurrent race conditions.
Usage:
    python3 test_stream.py [PORT]
    (Default PORT: 5500)
"""

import sys
import socket
import struct
import threading
import time
import os

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5500

def hdr(name: bytes, size: int) -> bytes:
    """Encode protocol header: uint16(name_len) + name + uint64(file_size)"""
    return struct.pack('>H', len(name)) + name + struct.pack('>Q', size)

def recv_exact(s: socket.socket, n: int) -> bytes:
    """Read exactly n bytes from socket."""
    buf = b''
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Server closed connection unexpectedly")
        buf += chunk
    return buf

def main():
    print(f"Connecting to 127.0.0.1:{PORT} ...")
    try:
        s = socket.create_connection(('127.0.0.1', PORT))
    except Exception as e:
        print(f"Error: Could not connect to server ({e}). Please ensure ./server {PORT} is running.")
        sys.exit(1)

    print("\n--- Test A: Security & Path Traversal Check (Expect status 2: ST_BAD_NAME) ---")
    bad_names = [b'../evil.txt', b'a/b.txt', b'..', b'a\\b', b'x' * 256]
    for bad in bad_names:
        s.sendall(hdr(bad, 10))
        st = recv_exact(s, 1)[0]
        print(f"Name '{bad[:16].decode('latin1', errors='ignore')}' -> Status {st} (Expected: 2)")
        assert st == 2, f"Expected status 2 for bad name, got {st}"
    print("[PASS] Test A Passed! Path traversal attempts correctly rejected.")

    print("\n--- Test B: Extreme Fragmentation (Header & Body byte-by-byte) ---")
    header_data = hdr(b'frag.txt', 5)
    for i in range(len(header_data)):
        s.send(header_data[i:i+1])
        time.sleep(0.002)
    header_status = recv_exact(s, 1)[0]
    print(f"Header status: {header_status} (Expected: 0)")
    assert header_status == 0, f"Expected header status 0, got {header_status}"

    for c in b'abcde':
        s.send(bytes([c]))
        time.sleep(0.002)
    final_status = recv_exact(s, 1)[0]
    print(f"Final status: {final_status} (Expected: 0)")
    assert final_status == 0, f"Expected final status 0, got {final_status}"
    print("[PASS] Test B Passed! Fragmented byte-by-byte transfer succeeded.")

    print("\n--- Test C: Duplicate File Check (Expect status 1: ST_EXISTS) ---")
    s.sendall(hdr(b'frag.txt', 1))
    dup_status = recv_exact(s, 1)[0]
    print(f"Duplicate upload status: {dup_status} (Expected: 1)")
    assert dup_status == 1, f"Expected duplicate status 1, got {dup_status}"
    print("[PASS] Test C Passed! Duplicate check working.")

    print("\n--- Test D: File Exceeding Max Size (1 TiB > 2 GiB limit, expect status 3) ---")
    s.sendall(hdr(b'huge.bin', 1 << 40))
    too_large_status = recv_exact(s, 1)[0]
    print(f"File too large status: {too_large_status} (Expected: 3)")
    assert too_large_status == 3, f"Expected status 3, got {too_large_status}"
    print("[PASS] Test D Passed! Max file size limit enforced.")

    s.close()

    print("\n--- Test E: Concurrent Uploads / Race Condition Test (4 Threads) ---")
    results = []
    def upload_worker():
        try:
            c = socket.create_connection(('127.0.0.1', PORT))
            payload_len = 100000
            c.sendall(hdr(b'race.bin', payload_len))
            st = recv_exact(c, 1)[0]
            if st == 0:
                time.sleep(0.1)
                c.sendall(b'x' * payload_len)
                fin_st = recv_exact(c, 1)[0]
                results.append(('SUCCESS', fin_st))
            else:
                results.append(('REJECTED', st))
            c.close()
        except Exception as e:
            results.append(('ERROR', str(e)))

    threads = [threading.Thread(target=upload_worker) for _ in range(4)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    print(f"Concurrent upload results: {results}")
    success_count = sum(1 for r in results if r == ('SUCCESS', 0))
    rejected_count = sum(1 for r in results if r == ('REJECTED', 1))
    print(f"Success count: {success_count} (Expected: 1)")
    print(f"Rejected count: {rejected_count} (Expected: 3)")
    assert success_count == 1 and rejected_count == 3, "Race condition check failed!"
    print("[PASS] Test E Passed! Exactly one client created the file atomically.")

    print("\n==========================================")
    print("ALL STREAM & SECURITY TESTS FOR BAI 2 PASSED (+2 PTS)!")
    print("==========================================")

if __name__ == '__main__':
    main()
