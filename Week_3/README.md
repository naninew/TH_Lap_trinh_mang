# IT4062 - Bài tập tuần 3 (HW3)

UDP socket programming in C — two exercises:

- **Bài 1** (`bai1/`): a server that classifies a string into its letters and digits.
- **Bài 2** (`bai2/`): a server that resolves a domain name to an IP address, or an IP address back to a domain name (DNS-style lookup).

## 1. Folder structure

```
.
├── Makefile              # top-level: builds both bai1 and bai2
├── README.md
├── bai1/
│   ├── Makefile
│   ├── server.c
│   └── client.c
└── bai2/
    ├── Makefile
    ├── server.c
    └── client.c
```

Each `bai*/Makefile` produces two executables in that folder: `server` and `client`, exactly as required by the assignment.

## 2. Build

Requires `gcc` and `make` (any Linux/WSL/macOS box works).

**Build everything at once (from the project root):**
```bash
make
```

**Or build one exercise at a time:**
```bash
cd bai1 && make
cd ../bai2 && make
```

**Clean:**
```bash
make clean          # from root, cleans both
# or, inside bai1/ or bai2/
make clean
```

## 3. Run

Open two terminals per exercise: one for `server`, one for `client`.

### Bài 1 — Letter/digit classifier

```bash
# Terminal 1
cd bai1
./server 5500

# Terminal 2
cd bai1
./client 127.0.0.1 5500
```

The client will prompt repeatedly:
```
Enter a string (empty to quit):
```
Type a string and press Enter to send it. Press Enter on an empty line to stop the client.

### Bài 2 — Domain/IP resolver

```bash
# Terminal 1
cd bai2
./server 5500

# Terminal 2
cd bai2
./client 127.0.0.1 5500
```

The client prompts:
```
Enter a domain name or IP address (empty to quit):
```
Note: this exercise needs real DNS resolution, so the machine running `server` must have working internet/DNS access to get real results for public domains such as `google.com`.

You can run server and client on different machines too — just use the server machine's real IP instead of `127.0.0.1` when starting the client.

## 4. How the logic works

### Bài 1 (`bai1/server.c`)
For each received string:
- Walk through every character.
- If it's a digit → append to the "digits" string.
- If it's a letter → append to the "letters" string.
- If it's anything else (symbol, space, punctuation, etc.) → reply `Error` immediately.
- If valid, reply with the digits string, then (on a new line) the letters string — whichever of the two is non-empty is included; an all-digit or all-letter input therefore returns just one line.

### Bài 2 (`bai2/server.c`)
- First the server checks whether the input *looks like* an IPv4 address attempt (only digits and dots, e.g. `259.12.34.12` or `126.58.99.199`).
  - If yes, it tries `inet_pton()`. If parsing fails (any octet out of `0–255`, wrong number of parts, etc.) → reply `IP Address is invalid`.
  - If it is a valid IPv4 address, it performs a reverse lookup (`gethostbyaddr`) → reply with `Official name:` (+ `Alias name:` list if there are aliases), or `Not found information` if the address doesn't reverse-resolve.
- Otherwise, it's treated as a domain name and forward-resolved (`gethostbyname`) → reply with `Official IP:` (first address returned) + `Alias IP:` list if the domain has more than one A record, or `Not found information` if the domain doesn't resolve at all.

## 5. Full test suite (manual)

### Bài 1 test cases (from the assignment's sample I/O table)

Start `./server 5500` in one terminal, then in another run `./client 127.0.0.1 5500` and try each input below in turn (press Enter after each, then finally press Enter on an empty line to quit):

| # | Input      | Expected output           | Notes                              |
|---|------------|----------------------------|-------------------------------------|
| 1 | `1a2b3cd`  | `123` then `abcd` (2 lines)| mixed digits + letters              |
| 2 | `123`      | `123`                      | digits only                         |
| 3 | `abcd`     | `abcd`                     | letters only                        |
| 4 | `Ab15CD$`  | `Error`                    | contains `$`, an invalid character  |

Extra edge cases worth trying manually:

| # | Input        | Expected output | Why                                          |
|---|--------------|------------------|-----------------------------------------------|
| 5 | `Hello123`   | `123` / `Hello`  | mixed case letters still classified as letters |
| 6 | `!!!`        | `Error`          | all invalid characters                        |
| 7 | `a b`        | `Error`          | space is not a letter/digit                   |
| 8 | (empty line) | — (client exits) | client stops the loop, nothing is sent        |

### Bài 2 test cases (from the assignment's sample I/O table)

Start `./server 5500`, then run `./client 127.0.0.1 5500` (server machine needs internet access):

| # | Input             | Expected output                                                    | Notes                                   |
|---|-------------------|----------------------------------------------------------------------|-------------------------------------------|
| 1 | `google.com`      | `Official IP: <ip>` (+ `Alias IP:` list if multiple A records)      | real-world results vary by DNS/location   |
| 2 | `126.58.99.199`   | `Official name: ...` (+ `Alias name:` list) or `Not found information` if that address has no PTR record | example IP in the assignment may not have a real reverse record; use a real IP you know resolves (e.g. `8.8.8.8`) to see a populated result |
| 3 | `aznsc.test.com`  | `Not found information`                                              | non-existent domain                       |
| 4 | `259.12.34.12`    | `IP Address is invalid`                                              | octet `259` is out of the `0-255` range   |

Extra edge cases worth trying manually:

| # | Input             | Expected output          | Why                                             |
|---|-------------------|---------------------------|---------------------------------------------------|
| 5 | `8.8.8.8`         | `Official name: dns.google` (or similar) | a real, well-known reverse-resolvable IP        |
| 6 | `1.2.3`           | `IP Address is invalid`  | only 3 octets, not a full IPv4 address            |
| 7 | `999.1.1.1`       | `IP Address is invalid`  | first octet out of range                          |
| 8 | `example.com`     | `Official IP: 93.184.x.x` (or similar) | a real domain with a single A record             |
| 9 | (empty line)      | — (client exits)         | client stops the loop, nothing is sent            |

> Tip: since exercise 2 depends on live DNS, results for real domains/IPs will reflect whatever the server machine's resolver currently returns — this is expected and not a bug.

## 6. Packaging for submission

Run the automated package command from the root of `Week_3/`:

```bash
make zip
# or
make package
```

This automatically cleans build artifacts and creates `DoXuanQuy_20235818_HW3.zip` containing only source code, Makefiles, and README.md, ready for submission.

