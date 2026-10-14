#!/usr/bin/env python3
"""
Test script for Problem 1 (IT4062 - Week 4)
Tests TCP Byte Stream handling: fragmentation, coalescing, and large payloads.
Usage:
    python3 test_stream.py [PORT]
    (Default PORT: 5500)
"""

import sys
import socket
import struct
import time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 5500

def frame(b: bytes) -> bytes:
    """Frame payload with a 4-byte big-endian length prefix."""
    return struct.pack('>I', len(b)) + b

def recv_exact(s: socket.socket, n: int) -> bytes:
    """Read exactly n bytes from socket."""
    buf = b''
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("Server closed connection unexpectedly")
        buf += chunk
    return buf

def recv_frame(s: socket.socket) -> bytes:
    """Receive one framed reply."""
    raw_len = recv_exact(s, 4)
    length = struct.unpack('>I', raw_len)[0]
    return recv_exact(s, length)

def main():
    print(f"Connecting to 127.0.0.1:{PORT} ...")
    try:
        s = socket.create_connection(('127.0.0.1', PORT))
    except Exception as e:
        print(f"Error: Could not connect to server ({e}). Please ensure ./server {PORT} is running.")
        sys.exit(1)

    print("\n--- Test A: Extreme Fragmentation (Send 1 byte per 10ms) ---")
    data = frame(b'1a2b3cd')
    for i in range(len(data)):
        s.send(data[i:i+1])
        time.sleep(0.01)
    res_a = recv_frame(s)
    expected_a = b'123\nabcd'
    print(f"Received: {res_a}")
    print(f"Expected: {expected_a}")
    assert res_a == expected_a, f"Test A Failed: {res_a} != {expected_a}"
    print("[PASS] Test A Passed!")

    print("\n--- Test B: Coalescing (3 messages bundled into single send) ---")
    s.sendall(frame(b'abc') + frame(b'12$') + frame(b'x9'))
    res_b1 = recv_frame(s)
    res_b2 = recv_frame(s)
    res_b3 = recv_frame(s)
    print(f"Reply 1: {res_b1} (Expected: b'abc')")
    print(f"Reply 2: {res_b2} (Expected: b'Error')")
    print(f"Reply 3: {res_b3} (Expected: b'9\\nx')")
    assert res_b1 == b'abc', f"Test B1 Failed: {res_b1}"
    assert res_b2 == b'Error', f"Test B2 Failed: {res_b2}"
    assert res_b3 == b'9\nx', f"Test B3 Failed: {res_b3}"
    print("[PASS] Test B Passed!")

    print("\n--- Test C: Large message payload (60,000 bytes) ---")
    large_payload = b'a1' * 30000
    s.sendall(frame(large_payload))
    res_c = recv_frame(s)
    print(f"Received length: {len(res_c)} bytes (Expected: 60001 bytes, i.e. 30000 '1's + newline + 30000 'a's)")
    assert len(res_c) == 60001, f"Test C Failed: length {len(res_c)} != 60001"
    print("[PASS] Test C Passed!")

    s.close()
    print("\n==========================================")
    print("ALL STREAM TESTS FOR BAI 1 PASSED (+2 PTS)!")
    print("==========================================")

if __name__ == '__main__':
    main()
