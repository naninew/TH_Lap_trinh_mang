# IT4062 – BÀI TẬP TUẦN 5 (THỰC HÀNH LẬP TRÌNH MẠNG)
## THƯ MỤC NỘP BÀI: `Week_6`

- **Sinh viên:** Do Xuan Quy
- **MSSV:** 20235818
- **Môi trường chạy:** WSL (Kali Linux) / GCC (C11, POSIX-compliant, `pthread`)
- **Tên file nén nộp bài:** `DoXuanQuy_20235818_HW5.zip`
- **Nội dung:**
  - **Bài 1 (Bắt buộc):** Ứng dụng Xác thực người dùng (Đăng nhập / Đăng xuất) qua TCP Socket đa luồng (`pthread`), hỗ trợ nhiều client đồng thời, khóa tài khoản khi sai quá 3 lần, cập nhật tự động vào file `account.txt`.
  - **Bài 2 (Tùy chọn - Cộng 20% điểm):** Thiết kế giao thức & hiện thực hoàn chỉnh ứng dụng Upload / Download file bất kỳ qua UDP sử dụng cơ chế Stop-and-Wait ARQ tin cậy, Timeout & Retransmission, phát hiện gói tin trùng lặp và xác thực toàn vẹn bằng chuẩn CRC32 IEEE 802.3.

---

## 1. Cấu trúc thư mục (`Week_6/`)

```
Week_6/
├── Makefile                # Makefile chung (biên dịch bai1, bai2, clean, đóng gói zip)
├── README.md               # Sổ tay hướng dẫn chi tiết toàn bộ các lệnh terminal chạy thủ công
├── test_bai1.sh            # Script shell tham khảo kiểm thử tự động Bài 1
├── test_bai2.sh            # Script shell tham khảo kiểm thử tự động Bài 2
├── bai1/                   # BÀI 1: TCP User Authentication
│   ├── Makefile            # Makefile riêng của Bài 1
│   ├── server.c            # Source TCP Server (đa luồng pthread, mutex, atomic file write)
│   ├── client.c            # Source TCP Client (giao diện CLI trực quan, hỗ trợ command line)
│   └── account.txt         # Dữ liệu tài khoản (UserID Password Status)
└── bai2/                   # BÀI 2: Reliable UDP File Transfer Protocol (RUFTP)
    ├── Makefile            # Makefile riêng của Bài 2
    ├── protocol.h          # Định nghĩa cấu trúc gói tin UDP, mã lệnh, thuật toán CRC32
    ├── server.c            # Source UDP File Server (Stop-and-Wait ARQ, lưu trữ storage/)
    ├── client.c            # Source UDP File Client (Upload/Download file bất kỳ)
    └── storage/            # Thư mục lưu trữ file trên server (tự động tạo)
```

---

## 2. Hướng dẫn sử dụng Makefile

Tại thư mục gốc `Week_6/`, toàn bộ thao tác biên dịch, dọn dẹp và đóng gói được thực hiện qua các lệnh `make`:

| Lệnh Makefile | Tác dụng |
|---|---|
| `make` hoặc `make all` | Biên dịch toàn bộ cả `bai1` và `bai2`, sinh ra các file thực thi `server` và `client` ở cả hai thư mục |
| `make clean` | Dọn dẹp tất cả các file thực thi, file đối tượng (`*.o`), file tạm (`*.tmp`) |
| `make distclean` | Dọn dẹp sạch sẽ như `clean`, đồng thời xóa thư mục lưu trữ `bai2/storage/` |
| `make package` (hoặc `make zip`) | Tự động chạy `clean`, sau đó đóng gói **CHỈ CÁC FILE MÃ NGUỒN VÀ MAKEFILE** vào file nén **`DoXuanQuy_20235818_HW5.zip`** |

### Lệnh đóng gói và kiểm tra nội dung file nộp bài:
```bash
make package
unzip -l DoXuanQuy_20235818_HW5.zip
```

Kết quả hiển thị trên terminal:
```text
Archive:  DoXuanQuy_20235818_HW5.zip
  Length      Date    Time    Name
---------  ---------- -----   ----
     1522  2026-10-14 13:00   Makefile
      839  2026-10-14 13:00   bai1/Makefile
    13848  2026-10-14 13:00   bai1/server.c
     6513  2026-10-14 13:00   bai1/client.c
       81  2026-10-14 13:00   bai1/account.txt
      927  2026-10-14 13:00   bai2/Makefile
     2680  2026-10-14 13:00   bai2/protocol.h
    15192  2026-10-14 13:00   bai2/server.c
    18384  2026-10-14 13:00   bai2/client.c
---------                     -------
    59986                     9 files
```
*(Đúng tuyệt đối quy định: Đúng tên file `DoXuanQuy_20235818_HW5.zip`, chỉ gồm 9 tệp tin mã nguồn và Makefile, không chứa file `.o`, file nhị phân, file `.md` hay dữ liệu rác).*

---

## 3. Hướng dẫn chuẩn bị và tùy biến file dữ liệu Input

Hệ thống được thiết kế mở hoàn toàn, **bạn có toàn quyền chuẩn bị và sử dụng bất kỳ file dữ liệu nào của bạn** để kiểm thử:

### 3.1. Chuẩn bị file dữ liệu tài khoản (`account.txt`) cho Bài 1
Định dạng mỗi dòng trong file:
```text
UserID Password Status
```
- `UserID`: Tên tài khoản (không chứa dấu cách).
- `Password`: Mật khẩu tài khoản.
- `Status`: `1` là tài khoản hoạt động, `0` là tài khoản bị khóa.

**Khởi tạo lại file test mẫu bất kỳ lúc nào bằng lệnh terminal:**
```bash
cat << 'EOF' > /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_6/bai1/account.txt
admin 123456 1
user1 pass1 1
user2 pass2 0
quydx secret2023 1
student 20235818 1
EOF
```

**Bạn có thể tự chuẩn bị file của riêng bạn:**
- Dùng trình soạn thảo để sửa: `nano bai1/account.txt`
- Hoặc copy file tài khoản có sẵn của bạn vào: `cp /duong_dan_cua_ban/accounts.txt bai1/account.txt`
- Code Server hỗ trợ:
  * Tự động loại bỏ khoảng trắng thừa, xử lý đúng cả định dạng xuống dòng Windows (`\r\n`) và Linux (`\n`).
  * Bỏ qua các dòng trống hoặc dòng chú thích bắt đầu bằng `#`.

### 3.2. Chuẩn bị file dữ liệu Upload/Download cho Bài 2
Bạn có thể dùng **bất kỳ file nào** có sẵn trên máy Kali Linux của bạn:
- File văn bản thông thường (ví dụ: `~/test.txt`, `sample.c`,...)
- File ảnh có sẵn trong hệ thống Kali Linux: `/usr/share/backgrounds/kali/kali-light.png`
- File nhị phân bất kỳ do bạn tạo hoặc tải về: PDF, JPG, ZIP, MP4,...
- Có thể tự sinh file ngẫu nhiên dung lượng tùy ý bằng lệnh Linux:
  ```bash
  head -c 102400 /dev/urandom > ~/test_100k.bin   # File 100 KB
  head -c 1048576 /dev/urandom > ~/test_1mb.bin   # File 1 MB
  ```

---

## 4. QUY TRÌNH KIỂM THỬ THỦ CÔNG BÀI 1 (TCP USER AUTHENTICATION)

Tất cả các bước dưới đây được thực hiện trực tiếp bằng cách gõ lệnh trên Terminal của WSL Kali Linux.

### 4.1. Khởi động môi trường kiểm thử Bài 1

Mở **Terminal 1 (Dành cho Server)**:
```bash
# 1. Điều hướng vào thư mục bài 1
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_6/bai1

# 2. Khôi phục file account.txt về trạng thái ban đầu
cat << 'EOF' > account.txt
admin 123456 1
user1 pass1 1
user2 pass2 0
quydx secret2023 1
student 20235818 1
EOF

# 3. Biên dịch mã nguồn
make clean && make

# 4. Khởi động Server ở cổng 5500
./server 5500
```
Server sẽ in ra:
```text
[*] Loaded 5 accounts from 'account.txt'.
====================================================
[*] TCP Authentication Server listening on port 5500...
[*] Multi-threaded POSIX pthread model active.
====================================================
```

Mở **Terminal 2 (Dành cho Client 1)**:
```bash
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_6/bai1
```

---

### 4.2. Danh sách các ca kiểm thử thủ công Bài 1

#### TEST 1: Kiểm tra tính hợp lệ của tham số dòng lệnh (Command-line Arguments)
Tại **Terminal 2**, chạy lần lượt từng lệnh sau:

| Lệnh gõ trên Terminal | Kết quả in ra màn hình | Giải thích |
|---|---|---|
| `./server` | `Usage: ./server <Port_Number>` | Báo lỗi thiếu tham số cổng cho Server |
| `./server 70000` | `Error: Invalid port number '70000'. Must be between 1025 and 65535.` | Báo lỗi cổng vượt quá giới hạn 65535 |
| `./server abc` | `Error: Invalid port number 'abc'.` | Báo lỗi cổng không phải số |
| `./server 80` | `Error: Invalid port number '80'.` | Báo lỗi cổng đặc quyền <= 1024 |
| `./client` | `Usage: ./client <IP_Addr> <Port_Number>` | Báo lỗi thiếu cả IP và Port |
| `./client 127.0.0.1` | `Usage: ./client <IP_Addr> <Port_Number>` | Báo lỗi thiếu Port |
| `./client 999.1.1.1 5500` | `Error: Invalid IPv4 address '999.1.1.1'.` | Báo lỗi địa chỉ IP sai định dạng |
| `./client 127.0.0.1 70000` | `Error: Invalid port number '70000'.` | Báo lỗi cổng client sai khoảng giá trị |
| `./client 127.0.0.1 5501` | `connect: Connection refused` | Báo lỗi từ chối kết nối do server chưa bật ở cổng 5501 |

---

#### TEST 2: Đăng nhập thành công, Kiểm tra trạng thái và Đăng xuất (Happy Path)
Tại **Terminal 2**, khởi động Client kết nối tới Server:
```bash
./client 127.0.0.1 5500
```
Màn hình sẽ hiển thị:
```text
[Server] 200 Welcome to TCP Authentication Server. Ready for commands.

=========================================
     TCP USER AUTHENTICATION CLIENT     
=========================================
 1. Log in
 2. Log out
 3. Check current user status (WhoAmI)
 4. Exit
-----------------------------------------
Select option [1-4] or type command:
```

Thực hiện các thao tác sau:
1. Gõ `1` rồi nhấn `Enter`.
   - Client hỏi: `Enter UserID: ` -> Gõ `quydx` rồi nhấn `Enter`.
   - Client hỏi: `Enter Password: ` -> Gõ `secret2023` rồi nhấn `Enter`.
   - **Kết quả hiển thị:**
     ```text
     [Server] 200 Login successful. Welcome, quydx!
     ```
2. Gõ `3` rồi nhấn `Enter` (Kiểm tra trạng thái):
   - **Kết quả hiển thị:**
     ```text
     [Server] 200 Logged in as: quydx
     ```
3. Gõ `2` rồi nhấn `Enter` (Đăng xuất):
   - **Kết quả hiển thị:**
     ```text
     [Server] 200 Logged out successfully.
     ```
4. Gõ `3` rồi nhấn `Enter` (Kiểm tra lại sau khi đăng xuất):
   - **Kết quả hiển thị:**
     ```text
     [Server] 200 Not logged in.
     ```
5. Thử gõ `2` (Đăng xuất khi chưa đăng nhập):
   - **Kết quả hiển thị:**
     ```text
     [Server] 400 You are not logged in.
     ```

> **Cách chạy nhanh Test 2 bằng 1 dòng lệnh duy nhất trên terminal:**
> ```bash
> printf "1\nquydx\nsecret2023\n3\n2\n3\n2\n4\n" | ./client 127.0.0.1 5500
> ```

---

#### TEST 3: [YÊU CẦU 1] Mỗi cửa sổ client chỉ đăng nhập được 1 tài khoản
*Yêu cầu đề bài: Một cửa sổ client khi đã đăng nhập thì không được đăng nhập tài khoản khác khi chưa đăng xuất.*

Tại **Terminal 2**:
```bash
./client 127.0.0.1 5500
```
1. Chọn `1`, đăng nhập bằng tài khoản `quydx` / `secret2023`.
   - Kết quả: `[Server] 200 Login successful. Welcome, quydx!`
2. Khi đang ở trạng thái đã đăng nhập, chọn tiếp `1` để cố tình đăng nhập vào tài khoản `user1` / `pass1`:
   - Gõ `1` -> UserID: `user1` -> Password: `pass1`.
3. **Kết quả mong đợi:**
   ```text
   [Server] 401 Already logged in as 'quydx'. Please logout first.
   ```
   *(Server từ chối không cho phép đăng nhập đè, bảo vệ phiên làm việc của tài khoản hiện tại).*
4. Gõ `4` để thoát Client.

> **Cách chạy nhanh Test 3 bằng 1 dòng lệnh duy nhất:**
> ```bash
> printf "1\nquydx\nsecret2023\n1\nuser1\npass1\n4\n" | ./client 127.0.0.1 5500
> ```

---

#### TEST 4: [YÊU CẦU 2] Một tài khoản có thể đăng nhập đồng thời trên nhiều cửa sổ
*Yêu cầu đề bài: Mỗi tài khoản có thể đăng nhập đồng thời trên nhiều cửa sổ.*

Để kiểm chứng yêu cầu này, mở đồng thời **3 cửa sổ Terminal**:
- **Terminal 1**: Đang chạy `./server 5500`.
- **Terminal 2 (Client Cửa sổ 1)**:
  ```bash
  cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_6/bai1
  ./client 127.0.0.1 5500
  # Chọn 1 -> UserID: student -> Password: 20235818
  ```
  - Kết quả in ra: `[Server] 200 Login successful. Welcome, student!`
  - **Giữ nguyên cửa sổ này, KHÔNG đăng xuất.**
- **Terminal 3 (Client Cửa sổ 2)**:
  ```bash
  cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_6/bai1
  ./client 127.0.0.1 5500
  # Chọn 1 -> UserID: student -> Password: 20235818
  ```
  - Kết quả in ra: `[Server] 200 Login successful. Welcome, student!`
- **Quan sát log tại Terminal 1 (Server)**:
  Server sẽ in ra:
  ```text
  [+] Client connected from 127.0.0.1:43210 (Thread ID: ...)
  [*] User 'student' logged in successfully from 127.0.0.1:43210
  [+] Client connected from 127.0.0.1:43212 (Thread ID: ...)
  [*] User 'student' logged in successfully from 127.0.0.1:43212
  ```
  *Chứng minh:* Hai client kết nối độc lập từ 2 cổng khác nhau hoàn toàn có thể cùng lúc đăng nhập vào tài khoản `student` mà không hề bị xung đột hay từ chối.
- Tại cả 2 client, gõ `4` để thoát.

---

#### TEST 5: Kiểm tra tài khoản không tồn tại
Tại **Terminal 2**:
```bash
./client 127.0.0.1 5500
```
1. Chọn `1`.
2. Nhập `UserID`: `nguoi_dung_khong_ton_tai`
3. Nhập `Password`: `mat_khau_bat_ky`
4. **Kết quả mong đợi:**
   ```text
   [Server] 404 Account 'nguoi_dung_khong_ton_tai' does not exist.
   ```
5. Gõ `4` để thoát.

> **Cách chạy nhanh Test 5 bằng 1 dòng lệnh:**
> ```bash
> printf "1\nnguoi_dung_khong_ton_tai\nmat_khau_bat_ky\n4\n" | ./client 127.0.0.1 5500
> ```

---

#### TEST 6: Kiểm tra tài khoản đang bị khóa sẵn từ trước
Trong file `account.txt` ban đầu, tài khoản `user2` có `Status = 0`:
```bash
grep "^user2 " account.txt
# In ra: user2 pass2 0
```
Tại **Terminal 2**:
```bash
printf "1\nuser2\npass2\n4\n" | ./client 127.0.0.1 5500
```
**Kết quả mong đợi:**
```text
[Server] 403 Account 'user2' is blocked.
```
*(Server từ chối ngay lập tức, không cho phép truy cập tài khoản đang có trạng thái 0).*

---

#### TEST 7: Nhập sai mật khẩu 1 hoặc 2 lần rồi nhập đúng (Reset bộ đếm sai)
Tại **Terminal 2**, thử nghiệm với tài khoản `admin`:
1. Chọn `1` -> UserID: `admin` -> Mật khẩu sai lần 1: `sai_lan_1`
   - Kết quả: `[Server] 402 Password incorrect. Attempt 1/3. Please try again.`
2. Chọn `1` -> UserID: `admin` -> Mật khẩu sai lần 2: `sai_lan_2`
   - Kết quả: `[Server] 402 Password incorrect. Attempt 2/3. Please try again.`
3. Chọn `1` -> UserID: `admin` -> Mật khẩu ĐÚNG: `123456`
   - Kết quả: `[Server] 200 Login successful. Welcome, admin!`
   *(Bộ đếm sai của tài khoản `admin` đã được xóa sạch về 0).*
4. Đăng xuất: Chọn `2` -> `[Server] 200 Logged out successfully.`
5. Chọn `4` để thoát.

> **Cách chạy nhanh Test 7 bằng 1 dòng lệnh:**
> ```bash
> printf "1\nadmin\nsai_lan_1\n1\nadmin\nsai_lan_2\n1\nadmin\n123456\n2\n4\n" | ./client 127.0.0.1 5500
> ```

---

#### TEST 8: [YÊU CẦU 3] Nhập sai liên tiếp quá 3 lần -> Khóa tài khoản & Tự động ghi đè file đĩa
*Yêu cầu đề bài: Nếu đăng nhập sai quá 3 lần, tài khoản bị khóa. Trạng thái trong `account.txt` cập nhật thành 0.*

Tại **Terminal 2**, thực hiện kiểm thử với tài khoản `user1` (đang có `Status = 1`):

1. **Thực hiện nhập sai liên tiếp 3 lần:**
   ```bash
   printf "1\nuser1\nbad_pass_1\n1\nuser1\nbad_pass_2\n1\nuser1\nbad_pass_3\n4\n" | ./client 127.0.0.1 5500
   ```
   **Kết quả in ra lần lượt:**
   - Lần 1: `[Server] 402 Password incorrect. Attempt 1/3. Please try again.`
   - Lần 2: `[Server] 402 Password incorrect. Attempt 2/3. Please try again.`
   - Lần 3: `[Server] 405 Password incorrect (3/3). Account 'user1' has been BLOCKED!`

2. **Kiểm tra trực tiếp file `account.txt` trên ổ đĩa xem Server đã ghi đè chưa:**
   ```bash
   grep "^user1 " account.txt
   ```
   **Kết quả hiển thị trên terminal:**
   ```text
   user1 pass1 0
   ```
   *(Trạng thái của `user1` đã được Server tự động chuyển từ 1 sang 0 trên ổ đĩa).*

3. **Kiểm tra đăng nhập lại `user1` (dù nhập đúng mật khẩu `pass1`):**
   ```bash
   printf "1\nuser1\npass1\n4\n" | ./client 127.0.0.1 5500
   ```
   **Kết quả mong đợi:**
   ```text
   [Server] 403 Account 'user1' is blocked.
   ```
   *(Tài khoản đã bị khóa vĩnh viễn, không thể đăng nhập được nữa).*

---

#### TEST 9: Kiểm tra tính bền vững dữ liệu khi khởi động lại Server
1. Tại **Terminal 1**, tắt Server bằng tổ hợp phím `Ctrl + C`.
2. Khởi động lại Server:
   ```bash
   ./server 5500
   ```
3. Tại **Terminal 2**, thử đăng nhập lại tài khoản `user1`:
   ```bash
   printf "1\nuser1\npass1\n4\n" | ./client 127.0.0.1 5500
   ```
   **Kết quả mong đợi:**
   ```text
   [Server] 403 Account 'user1' is blocked.
   ```
   *(Chứng minh việc khóa tài khoản được lưu bền vững vào file đĩa, khởi động lại server vẫn nhận đúng trạng thái bị khóa).*

---

## 5. QUY TRÌNH KIỂM THỬ THỦ CÔNG BÀI 2 (RELIABLE UDP FILE TRANSFER)

### 5.1. Khởi động môi trường kiểm thử Bài 2

Mở **Terminal 1 (UDP Server)**:
```bash
# 1. Điều hướng vào thư mục bài 2
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_6/bai2

# 2. Dọn dẹp và biên dịch
make distclean && make

# 3. Khởi động UDP Server ở cổng 6000
./server 6000
```
Server sẽ in ra:
```text
====================================================
[*] Reliable UDP File Transfer (RUFTP) Server running on port 6000
[*] File repository directory: ./storage/
====================================================
```

Mở **Terminal 2 (UDP Client)**:
```bash
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_6/bai2
```

---

### 5.2. Danh sách các ca kiểm thử thủ công Bài 2

#### TEST 2.1: Upload file văn bản do bạn tự tạo và so sánh `diff`
Tại **Terminal 2**:
1. Tự tạo một file văn bản tùy ý tại bất cứ đâu (ví dụ thư mục `~`):
   ```bash
   echo "IT4062 - Lap trinh mang - Sinh vien: Do Xuan Quy - MSSV: 20235818" > ~/my_note.txt
   ```
2. Gõ lệnh upload file đó lên Server:
   ```bash
   ./client 127.0.0.1 6000 upload ~/my_note.txt
   ```
3. **Kết quả hiển thị trên Client:**
   ```text
   [UPLOAD] Preparing: 'my_note.txt' (67 bytes, CRC32: 0x...)
   [UPLOAD] Transferring data chunks...
   [SUCCESS] Upload of 'my_note.txt' completed! (67 bytes transferred, CRC verified)
   ```
4. **Kiểm tra file trên Server bằng lệnh `diff`:**
   ```bash
   diff ~/my_note.txt storage/my_note.txt
   ```
   *Lệnh không in ra bất kỳ sự khác biệt nào -> File trên server trùng khớp 100% từng ký tự.*

---

#### TEST 2.2: Upload file nhị phân / File ảnh thực tế và so sánh mã băm SHA-256
Tại **Terminal 2**:
1. Bạn có thể chọn bất kỳ file ảnh nào có sẵn trên Kali Linux (hoặc tự tạo một file nhị phân 50 KB):
   ```bash
   head -c 51200 /dev/urandom > ~/binary_50k.dat
   ```
2. Upload file nhị phân lên Server:
   ```bash
   ./client 127.0.0.1 6000 upload ~/binary_50k.dat
   ```
3. Client sẽ chia file thành 50 gói tin UDP (mỗi gói 1024 bytes), truyền Stop-and-Wait ARQ và in:
   ```text
   [SUCCESS] Upload of 'binary_50k.dat' completed! (51200 bytes transferred, CRC verified)
   ```
4. **So sánh mã băm SHA-256 giữa file gốc và file trong `storage/`:**
   ```bash
   sha256sum ~/binary_50k.dat storage/binary_50k.dat
   ```
   *Hai chuỗi hash in ra hoàn toàn giống hệt nhau từng ký tự -> Không hề bị sai lệch hay mất dữ liệu.*

---

#### TEST 2.3: Upload file lớn (> 1 MB, hơn 1000 gói tin UDP ARQ)
Tại **Terminal 2**:
1. Tạo một file dung lượng đúng 1.5 MB (1,572,864 bytes):
   ```bash
   head -c 1572864 /dev/urandom > ~/large_file.bin
   ```
2. Upload file lớn lên Server:
   ```bash
   ./client 127.0.0.1 6000 upload ~/large_file.bin
   ```
3. Quan sát quá trình truyền hơn 1500 block dữ liệu UDP liên tục.
4. **Kiểm tra tính toàn vẹn bằng lệnh `cmp` của Linux:**
   ```bash
   cmp ~/large_file.bin storage/large_file.bin && echo "FILE KHOP HOAN TOAN 100%!"
   ```

---

#### TEST 2.4: Download file từ Server về máy Client
Tại **Terminal 2**:
1. Tải file `my_note.txt` từ Server về lưu thành `~/downloaded_note.txt`:
   ```bash
   ./client 127.0.0.1 6000 download my_note.txt ~/downloaded_note.txt
   ```
2. **Kết quả hiển thị trên Client:**
   ```text
   [DOWNLOAD] Requesting file 'my_note.txt' from server...
   [DOWNLOAD] Receiving 'my_note.txt' (67 bytes, expected CRC: 0x...) -> saving as '~/downloaded_note.txt'
   [SUCCESS] Download of '~/downloaded_note.txt' completed! (67 bytes, CRC verified: 0x...)
   ```
3. So sánh file vừa tải về với file gốc trong `storage/`:
   ```bash
   diff ~/downloaded_note.txt storage/my_note.txt
   ```
   *(Hai file trùng khớp tuyệt đối).*

---

#### TEST 2.5: Yêu cầu Download file không tồn tại trên Server
Tại **Terminal 2**, thử yêu cầu tải một file không hề có trong `storage/`:
```bash
./client 127.0.0.1 6000 download file_khong_he_ton_tai.bin
```
**Kết quả hiển thị:**
```text
[DOWNLOAD] Requesting file 'file_khong_he_ton_tai.bin' from server...
Server error: File not found in storage
```
*(Server gửi gói tin `TYPE_ERR`, Client bắt lỗi và hiển thị rõ ràng, không bị treo socket).*

---

#### TEST 2.6: Sử dụng Menu tương tác (Interactive CLI Mode)
Tại **Terminal 2**, khởi động Client không kèm tham số lệnh để vào menu:
```bash
./client 127.0.0.1 6000
```
Màn hình sẽ hiển thị Menu:
```text
=========================================
   RELIABLE UDP FILE TRANSFER (RUFTP)   
=========================================
 1. Upload file   (Syntax: upload <filepath>)
 2. Download file (Syntax: download <remote_file> [save_path])
 3. Exit
-----------------------------------------
Select option [1-3] or type command:
```
- Bạn có thể gõ `1` để nhập đường dẫn file upload.
- Bạn có thể gõ `2` để nhập tên file download và nơi lưu.
- Bạn có thể gõ `3` để thoát Client.

---

## 6. Tổng kết Quy trình Kiểm thử

| STT | Bài | Mục tiêu kiểm thử | Lệnh terminal chính | Kết quả mong đợi |
|---|---|---|---|---|
| 1 | Bài 1 | Kiểm tra tham số dòng lệnh | `./server` và `./client` với port/IP sai | In hướng dẫn `Usage:` hoặc báo lỗi cổng/IP |
| 2 | Bài 1 | Đăng nhập hợp lệ & Đăng xuất | `printf "1\nquydx\nsecret2023\n2\n4\n" \| ./client 127.0.0.1 5500` | In `200 Login successful`, `200 Logged out` |
| 3 | Bài 1 | **[YC 1]** Mỗi cửa sổ chỉ đăng nhập 1 TK | Đăng nhập `quydx`, rồi cố đăng nhập tiếp `user1` | In `401 Already logged in... Please logout first.` |
| 4 | Bài 1 | **[YC 2]** Đăng nhập cùng 1 TK trên nhiều cửa sổ | Mở 2 terminal client cùng đăng nhập `student` | Cả 2 cửa sổ đều báo `200 Login successful` |
| 5 | Bài 1 | Đăng nhập tài khoản không tồn tại | Nhập `nonexistent_user` | In `404 Account does not exist` |
| 6 | Bài 1 | Đăng nhập tài khoản bị khóa sẵn (Status = 0) | Nhập `user2` / `pass2` | In `403 Account is blocked` |
| 7 | Bài 1 | Sai mật khẩu 1-2 lần rồi nhập đúng | Sai lần 1, sai lần 2, lần 3 nhập đúng `admin` | In `Attempt 1/3`, `Attempt 2/3`, rồi `Login successful` (reset đếm) |
| 8 | Bài 1 | **[YC 3]** Sai liên tiếp 3 lần -> Khóa TK & Ghi đĩa | Sai 3 lần `user1` -> kiểm tra `grep user1 account.txt` | In `Account 'user1' has been BLOCKED`, đĩa lưu `user1 pass1 0` |
| 9 | Bài 1 | Bền vững sau khi tắt/bật lại Server | `Ctrl+C` Server -> bật lại -> đăng nhập `user1` | Vẫn báo `403 Account is blocked` |
| 10 | Bài 2 | Upload file text | `./client 127.0.0.1 6000 upload file.txt` | `diff` giữa file gốc và `storage/file.txt` trùng khớp |
| 11 | Bài 2 | Upload file nhị phân & kiểm tra CRC32/SHA256 | `./client 127.0.0.1 6000 upload binary.dat` | `sha256sum` giữa 2 file trùng khớp từng ký tự |
| 12 | Bài 2 | Upload file lớn (> 1 MB) qua Stop-and-Wait ARQ | `./client 127.0.0.1 6000 upload large.bin` | Truyền hơn 1000 gói UDP tin cậy, `cmp` khớp 100% |
| 13 | Bài 2 | Download file từ server về máy | `./client 127.0.0.1 6000 download file.txt dl.txt` | `diff dl.txt storage/file.txt` trùng khớp |
| 14 | Bài 2 | Download file không tồn tại | `./client 127.0.0.1 6000 download no_file.xyz` | Báo `Server error: File not found in storage` |
| 15 | Chung | Đóng gói nộp bài chuẩn quy định | `make package && unzip -l DoXuanQuy_20235818_HW5.zip` | Tạo đúng file `DoXuanQuy_20235818_HW5.zip` chứa đúng 9 file code + Makefile |
