# IT4062 – BÀI TẬP TUẦN 4: TCP SOCKET

- **Sinh viên:** DoXuanQuy
- **MSSV:** 20235818
- **Ngôn ngữ:** C (chuẩn C11, chuẩn POSIX), chạy trên Linux / WSL Kali.
- **Điểm cộng:** Đã xử lý trọn vẹn vấn đề **truyền theo dòng byte (byte stream) trên TCP Socket** cho cả Bài 1 và Bài 2 (+2 điểm mỗi bài).
- **Mô hình kết nối:** Concurrent Server đa tiến trình sử dụng `fork()` cho cả 2 bài, tự động thu dọn tiến trình con, không bị zombie process.

---

## 1. Cấu trúc thư mục

```
Week_4/
├── Makefile                # Makefile chung (biên dịch cả 2 bài, đóng gói nộp bài)
├── README.md               # Hướng dẫn chi tiết chạy toàn bộ các bộ test trên terminal WSL
├── setup_test_data.sh      # Script tự động tạo thư mục và các file dữ liệu test mẫu
├── testfile.jpg            # File ảnh mẫu (JPEG hợp lệ) dùng để test Bài 2
├── test_data/              # Thư mục chứa các file dữ liệu test mẫu
│   ├── testfile.jpg
│   ├── testfile.txt
│   └── file with spaces.txt
├── bai1/                   # BÀI 1: Tách chữ cái / chữ số qua TCP Socket
│   ├── Makefile            # Makefile riêng của Bài 1
│   ├── server.c            # Mã nguồn Server Bài 1 (Forking TCP Server)
│   ├── client.c            # Mã nguồn Client Bài 1 (Interactive TCP Client)
│   └── test_stream.py      # Script terminal kiểm tra xử lý dòng byte TCP (+2 điểm)
└── bai2/                   # BÀI 2: Truyền file nhị phân bất kỳ lên tới 100MB qua TCP Socket
    ├── Makefile            # Makefile riêng của Bài 2
    ├── server.c            # Mã nguồn Server Bài 2 (Quản lý lưu trữ storage/, atomic create)
    ├── client.c            # Mã nguồn Client Bài 2 (Upload file, chunking, auto-reconnect)
    └── test_stream.py      # Script terminal kiểm tra dòng byte, bảo mật & race condition (+2 điểm)
```

---

## 2. Chuẩn bị môi trường trên WSL Kali

Mở terminal trên **WSL Kali Linux**, cài đặt các gói công cụ cần thiết (nếu chưa có):

```bash
sudo apt update && sudo apt install -y build-essential zip python3
```

Điều hướng vào thư mục làm việc của Tuần 4:

```bash
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_4
```

*(Lưu ý: Thay đổi đường dẫn `/mnt/d/...` cho phù hợp nếu bạn đặt thư mục ở vị trí khác).*

---

## 3. Makefile chung (Root Makefile) & Makefile từng bài

Tại thư mục gốc `Week_4/`, Makefile chung hỗ trợ các lệnh:

| Lệnh | Tác dụng |
|---|---|
| `make` / `make all` | Tự động gọi Makefile ở cả `bai1/` và `bai2/` để biên dịch ra các file thực thi `server` và `client` |
| `make clean` | Dọn dẹp các file thực thi, file đối tượng `*.o` và file nén ở cả 2 bài |
| `make package` (hoặc `make zip`) | Tự động chạy `clean`, sau đó đóng gói mã nguồn và Makefiles vào file nén **`DoXuanQuy_20235818_HW4.zip`** (chỉ chứa file mã nguồn `.c` và `Makefile`, **không** chứa file `.md`, file `.o`, file nhị phân hay dữ liệu test) |
| `make distclean` | Chạy `clean` đồng thời xóa thư mục lưu trữ `bai2/storage/` |

Kiểm tra nội dung file zip đã đóng gói:
```bash
unzip -l DoXuanQuy_20235818_HW4.zip
```
*(Đảm bảo chỉ có `Makefile`, `bai1/Makefile`, `bai1/server.c`, `bai1/client.c`, `bai2/Makefile`, `bai2/server.c`, `bai2/client.c`).*

---

## 4. Chuẩn bị dữ liệu test (File ảnh mẫu và File 100MB)

Trong thư mục `Week_4/` đã có sẵn file ảnh mẫu hợp lệ `testfile.jpg`.

Để chuẩn bị đầy đủ toàn bộ các loại file theo kịch bản test (file văn bản, file ảnh, file rỗng, file có khoảng trắng, file 100MB, thư mục con), chạy script tự động:

```bash
chmod +x setup_test_data.sh
./setup_test_data.sh
```

Mặc định script sẽ tạo thư mục `~/test` chứa:
- `testfile.jpg`: File ảnh JPEG mẫu hợp lệ
- `testfile.txt`: File văn bản nội dung `"hello world"`
- `empty.bin`: File rỗng 0 byte
- `file with spaces.txt`: File có dấu cách trong tên
- `folder1`: Thư mục con (dùng để test từ chối upload thư mục)
- `big100.bin`: File nhị phân dung lượng đúng **100 MB (104,857,600 bytes)**

---

## 5. HƯỚNG DẪN CHẠY BỘ TEST BÀI 1 (TÁCH CHỮ VÀ SỐ)

### 5.1 Thiết kế xử lý dòng byte TCP (+2 điểm)
- TCP không duy trì ranh giới thông điệp giữa các lần `send()`.
- Bài 1 giải quyết triệt để bằng kỹ thuật **Length-Prefixed Framing**:
  - Mỗi message gửi đi đều có 4 byte đầu tiên là độ dài payload theo chuẩn mạng (big-endian 32-bit):
    `[uint32_t length (4 bytes)][payload (length bytes)]`
  - Hàm `send_all()` và `recv_all()` đảm bảo gửi/nhận đủ từng byte, xử lý tín hiệu ngắt `EINTR` và phân mảnh/dính gói hoàn hảo.

---

### 5.2 Khởi động Server và Client
Mở **2 cửa sổ Terminal** trên WSL Kali:

#### Terminal 1 (Server):
```bash
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_4/bai1
make
./server 5500
```
Server sẽ in: `Server listening on port 5500 ...`

#### Terminal 2 (Client):
```bash
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_4/bai1
./client 127.0.0.1 5500
```
Client sẽ in: `Connected to 127.0.0.1:5500. Enter an empty string to quit.`

---

### 5.3 Chạy lần lượt từng bộ test thủ công (Gõ trực tiếp ở Terminal 2)

Nhập từng chuỗi sau vào dấu nhắc `Enter string: `:

| STT | Chuỗi nhập vào Client | Kết quả hiển thị mong đợi | Giải thích |
|---|---|---|---|
| **Test 1** | `1a2b3cd` | `123`<br>`abcd` | Dòng 1: các chữ số, Dòng 2: các chữ cái |
| **Test 2** | `123` | `123` | Chỉ có chữ số |
| **Test 3** | `abcd` | `abcd` | Chỉ có chữ cái |
| **Test 4** | `Ab15CD$` | `Error` | Chứa ký tự đặc biệt `$` |
| **Test 5** | `ab cd` | `Error` | Chứa ký tự dấu cách |
| **Test 6** | `$` | `Error` | Chỉ chứa ký tự đặc biệt |
| **Test 7** | `Z9` | `9`<br>`Z` | Hỗ trợ cả chữ in hoa |
| **Test 8** | `0` | `0` | Số 0 |
| **Test 9** | `a` | `a` | Chữ đơn lẻ |
| **Test 10** | *(Chỉ nhấn Enter)* | `Bye.` | Nhập xâu rỗng -> Client ngắt kết nối an toàn |

> **Cách chạy nhanh tự động toàn bộ 10 test trên chỉ bằng một dòng lệnh:**
> ```bash
> printf '1a2b3cd\n123\nabcd\nAb15CD$\nab cd\n$\nZ9\n0\na\n\n' | ./client 127.0.0.1 5500
> ```

---

### 5.4 Các test nâng cao cho Bài 1

#### A. Test chuỗi siêu dài (vượt ngưỡng buffer 4095 ký tự)
Tại Terminal 2:
```bash
# Chuỗi 6000 ký tự -> client báo lỗi và bỏ qua phần thừa, không crash
python3 -c "print('a1'*3000)" | ./client 127.0.0.1 5500

# Chuỗi 2000 ký tự (hợp lệ) -> server xử lý trả về 1000 số 1 và 1000 chữ a
python3 -c "print('a1'*1000)" | ./client 127.0.0.1 5500
```

#### B. Test kiểm tra tính hợp lệ của tham số dòng lệnh
```bash
./server                 # Báo lỗi: Usage: ./server PortNumber
./server abc             # Báo lỗi: Error: invalid port number 'abc'
./server 70000           # Báo lỗi: Error: invalid port number '70000'
./client 127.0.0.1       # Báo lỗi: Usage: ./client IPAddress PortNumber
./client 999.1.1.1 5500  # Báo lỗi: Error: invalid IPv4 address
./client 127.0.0.1 5501  # Báo lỗi: connect: Connection refused (khi chưa bật server ở 5501)
```

#### C. Test kết nối nhiều Client đồng thời (Mô hình đa tiến trình Fork)
Mở thêm **Terminal 3**, cùng chạy `./client 127.0.0.1 5500`. Nhập dữ liệu ở cả 2 terminal client:
- Ở log Server (Terminal 1) sẽ thấy server fork ra các tiến trình con phục vụ với port ngẫu nhiên khác nhau cho từng client (ví dụ `[127.0.0.1:42134]`, `[127.0.0.1:42136]`).
- Các client hoạt động song song độc lập, không block lẫn nhau.

---

### 5.5 Test xử lý dòng byte TCP chuyên sâu (+2 điểm)

Khi Server đang chạy ở cổng `5500`, tại một terminal khác (ở thư mục `bai1`), chạy script Python kiểm tra trực tiếp qua terminal:

```bash
python3 test_stream.py
```
*(Hoặc chạy lệnh `python3 test_stream.py 5500` nếu bạn dùng cổng khác).*

Script này thực hiện 3 kịch bản khắt khe mà các bài socket thông thường sẽ thất bại:
1. **Phân mảnh cực đoan (Extreme Fragmentation):** Gửi từng byte một kèm trễ `10ms` -> Server vẫn lắp ghép đúng chuỗi `1a2b3cd` thành `123\nabcd`.
2. **Dính gói (Coalescing):** Gộp 3 thông điệp `abc`, `12$`, `x9` trong duy nhất **1 lần send** -> Server bóc tách chính xác từng message, trả về lần lượt `abc`, `Error`, `9\nx`.
3. **Payload lớn:** Gửi gói tin lớn 60,000 bytes -> Server đọc trọn vẹn và trả về đủ 60,001 bytes.

Nếu màn hình in ra:
```
==========================================
ALL STREAM TESTS FOR BAI 1 PASSED (+2 PTS)!
==========================================
```
tức là bài 1 đạt trọn vẹn điểm cộng!

---

## 6. HƯỚNG DẪN CHẠY BỘ TEST BÀI 2 (TRUYỀN FILE TCP SOCKET ĐẾN 100MB)

### 6.1 Thiết kế giao thức & xử lý dòng byte TCP (+2 điểm)
- Giao thức định nghĩa chuẩn nhị phân (Network byte order / Big-endian):
  ```
  1. Client -> Server : [uint16_t name_len][filename (name_len bytes)][uint64_t file_size]
  2. Server -> Client : [uint8_t status] (0: OK, 1: File đã tồn tại, 2: Tên lỗi, 3: File quá lớn, 4: Lỗi I/O)
  3. Client -> Server : [file_size bytes nội dung] (chỉ gửi khi status == 0, truyền từng block 64KB)
  4. Server -> Client : [uint8_t final_status] (0: Lưu thành công)
  ```
- **Nguyên tử hóa (Atomic Check-and-Create):** Server dùng cờ `open(path, O_WRONLY | O_CREAT | O_EXCL, 0644)` giúp thao tác kiểm tra trùng tên và tạo file là duy nhất; ngăn chặn hoàn toàn Race Condition khi nhiều client gửi cùng lúc.
- **Bảo mật:** Loại bỏ nguy cơ Directory Traversal (từ chối mọi tên chứa `/`, `\`, `..`, hoặc dài hơn 255 ký tự).
- **Xóa file dở dang:** Nếu mất kết nối hoặc server/client bị ngắt giữa chừng, file tạm sẽ được xóa ngay lập tức (`unlink`), không để lại file lỗi trên server.
- **Tự động kết nối lại (Auto-reconnect):** Khi đường truyền bị đứt, client tự khởi tạo socket kết nối lại server để người dùng có thể tiếp tục thao tác mà không phải bật lại chương trình.

---

### 6.2 Khởi động Server và Client
Mở **2 cửa sổ Terminal** trên WSL Kali:

#### Terminal 1 (Server):
```bash
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_4/bai2
make distclean && make
./server 5500
```
Server sẽ in: `Server listening on port 5500, storing files in ./storage/`

#### Terminal 2 (Client):
```bash
cd /mnt/d/HUST_Study/2026.1/TH_lap_trinh_mang/Do_Xuan_Quy_20235818/TH_Lap_trinh_mang/Week_4/bai2
./client 127.0.0.1 5500
```
Client sẽ in: `Connected to 127.0.0.1:5500. Enter an empty path to quit.`

---

### 6.3 Chạy lần lượt từng bộ test thủ công (Gõ trực tiếp ở Terminal 2)

Nhập lần lượt các đường dẫn sau (theo giả định bạn đã chạy `./setup_test_data.sh` tạo thư mục `~/test`):

| STT | Đường dẫn nhập vào Client | Kết quả hiển thị mong đợi | Cách kiểm tra thêm trên hệ thống |
|---|---|---|---|
| **Test 1** | `/home/kali/test/testfile.jpg`<br>*(hoặc đường dẫn tuyệt đối tới `testfile.jpg`)* | `Successful transfering` | File được lưu tại `storage/testfile.jpg`.<br>Kiểm tra tính toàn vẹn: `sha256sum ~/test/testfile.jpg storage/testfile.jpg` (2 mã hash phải giống hệt nhau). |
| **Test 2** | `/home/kali/test/testfile.rar` | `Error: File not found` | Client kiểm tra file không tồn tại ở local, không gửi gì lên server. |
| **Test 3** | `/home/kali/test/testfile.jpg` *(nhập lại lần nữa)* | `Error: File is existent on server` | Server phát hiện file đã tồn tại trong `storage/`, từ chối nhận lại và không ghi đè. |
| **Test 4** | `/home/kali/test/testfile.txt` | `Successful transfering` | Nội dung file `storage/testfile.txt` là `"hello world"`. |
| **Test 5** | `/home/kali/test/empty.bin` | `Successful transfering` | Truyền thành công file 0 byte (`ls -l storage/empty.bin`). |
| **Test 6** | `/home/kali/test/file with spaces.txt` | `Successful transfering` | Truyền thành công file có dấu cách trong tên. |
| **Test 7** | `/home/kali/test/folder1` | `Error: Not a regular file` | Client từ chối đường dẫn là thư mục. |
| **Test 8** | `/home/kali/test/big100.bin` | `Successful transfering` | Truyền thành công file dung lượng **100MB**.<br>Kiểm tra: `sha256sum ~/test/big100.bin storage/big100.bin` (khớp 100%). |
| **Test 9** | `/home/kali/test/big100.bin` *(nhập lại)* | `Error: File is existent on server` | File 100MB đã có trong `storage/`, bị từ chối. |
| **Test 10** | Đường dẫn tương đối: `testfile.txt` *(nếu copy file vào thư mục hiện tại)* | `Error: File is existent on server` | Nhận diện đúng tên base name kể cả khi nhập đường dẫn tương đối. |
| **Test 11** | *(Chỉ nhấn Enter)* | `Bye.` | Nhập xâu rỗng -> Client thoát sạch sẽ. Server ghi log `client disconnected`. |

---

### 6.4 Test các tình huống truyền dở dang (Error: File tranfering is interupted)

File 100MB truyền qua mạng nội bộ loopback (127.0.0.1) chỉ mất chưa tới 0.5 giây. Để người dùng kịp thao tác ngắt thủ công bằng tay, Client hỗ trợ biến môi trường **`SEND_DELAY_MS`** (chèn độ trễ vài mili-giây sau mỗi block 64KB).

#### Kịch bản A: Server bị kill tiến trình giữa chừng (Client phát hiện mất kết nối)
1. Tại Terminal 1: Chạy `./server 5500`
2. Tại Terminal 2: Bật client có độ trễ:
   ```bash
   SEND_DELAY_MS=5 ./client 127.0.0.1 5500
   ```
   Nhập: `/home/kali/test/big100.bin`
3. Trong khoảng ~8 giây file đang truyền, mở **Terminal 3** chạy lệnh:
   ```bash
   pkill -9 -x server
   ```
4. Kết quả mong đợi tại Terminal 2 (Client):
   ```
   Error: File tranfering is interupted
   Error: cannot reconnect to server. Exiting.
   ```

#### Kịch bản B: Server dừng bằng Ctrl+C giữa chừng (Tự động xóa sạch file dở dang)
1. Bật lại `./server 5500` ở Terminal 1. Đảm bảo `rm -f storage/big100.bin`.
2. Tại Terminal 2: `SEND_DELAY_MS=5 ./client 127.0.0.1 5500`, nhập `/home/kali/test/big100.bin`.
3. Khi đang truyền, nhấn ngay **Ctrl+C** tại Terminal 1 (Server).
4. Kiểm tra tại thư mục `storage/`:
   ```bash
   ls storage/big100.bin
   ```
   -> File **không** tồn tại vì Signal Handler của server đã kịp gọi `unlink()` xóa file dở dang!

#### Kịch bản C: Client bị ngắt Ctrl+C giữa chừng (Server tự thu dọn)
1. Bật `./server 5500` ở Terminal 1.
2. Tại Terminal 2: `SEND_DELAY_MS=5 ./client 127.0.0.1 5500`, nhập `/home/kali/test/big100.bin`.
3. Khi đang truyền, nhấn **Ctrl+C** ngay tại Terminal 2 (Client).
4. Xem Terminal 1 (Server):
   In thông báo: `transfer of "big100.bin" INTERRUPTED - partial file removed`.
   `storage/big100.bin` được dọn sạch, không bị rác ổ cứng.

#### Kịch bản D: Server Worker bị chết & Client tự động kết nối lại (Auto-reconnect)
1. Bật `./server 5500` ở Terminal 1.
2. Tại Terminal 2: `SEND_DELAY_MS=5 ./client 127.0.0.1 5500`, nhập `/home/kali/test/big100.bin`.
3. Khi đang truyền, tại Terminal 3 chỉ tiêu diệt tiến trình con (tiến trình cha vẫn sống):
   ```bash
   pkill -9 -P $(pgrep -ox server)
   ```
4. Xem Terminal 2 (Client):
   ```
   Error: File tranfering is interupted
   (reconnected to server)
   Enter file path: 
   ```
   Client tự động kết nối lại socket mới, cho phép bạn tiếp tục gõ file khác để truyền tiếp mà không cần khởi động lại client!

---

### 6.5 Test xử lý dòng byte TCP, bảo mật & Race Condition (+2 điểm)

Khi Server Bài 2 đang chạy ở cổng 5500, tại thư mục `bai2`, chạy script Python kiểm tra:

```bash
python3 test_stream.py
```
*(Hoặc chạy lệnh `python3 test_stream.py 5500` nếu bạn dùng cổng khác).*

Script thực hiện 5 bài test nghiêm ngặt:
1. **Chống Path Traversal:** Thử gửi các đường dẫn nguy hiểm `../evil.txt`, `a/b.txt`, `..`, tên dài hơn 255 ký tự -> Server từ chối ngay lập tức (Status code = 2).
2. **Phân mảnh cực đoan:** Gửi header từng byte một, sau đó gửi body từng byte một -> Server vẫn nhận đầy đủ và lưu file `frag.txt` hoàn chỉnh (Status code = 0).
3. **Phát hiện trùng tên:** Gửi lại `frag.txt` -> Server trả về Status code = 1.
4. **Giới hạn kích thước file:** Khai báo file 1 TiB (vượt giới hạn an toàn 2 GiB) -> Server từ chối ngay (Status code = 3).
5. **Race Condition (Upload đồng thời 4 luồng cùng tên file `race.bin`):** Nhờ cơ chế `O_CREAT | O_EXCL` nguyên tử, đúng 1 luồng upload thành công và 3 luồng còn lại nhận thông báo lỗi trùng file (không xảy ra lỗi tranh chấp dữ liệu hay ghi đè lộn xộn).

Nếu màn hình in:
```
==========================================
ALL STREAM & SECURITY TESTS FOR BAI 2 PASSED (+2 PTS)!
==========================================
```
tức là bài 2 hoàn thành xuất sắc toàn bộ tiêu chí!

---

## 7. Quy trình đóng gói nộp bài

Sau khi hoàn thành tất cả các bài test, đứng tại thư mục gốc `Week_4/` và chạy lệnh:

```bash
make package
```

Lệnh này sẽ tự động:
1. Gọi `make clean` dọn dẹp sạch sẽ các file `.o` và file thực thi ở cả 2 bài.
2. Nén toàn bộ mã nguồn và Makefile vào file duy nhất: **`DoXuanQuy_20235818_HW4.zip`**.

Kiểm tra lại cấu trúc file zip để đảm bảo chuẩn quy định nộp bài:

```bash
unzip -l DoXuanQuy_20235818_HW4.zip
```

Kết quả hiển thị đúng chuẩn sẽ gồm:
```
Makefile
bai1/Makefile
bai1/server.c
bai1/client.c
bai2/Makefile
bai2/server.c
bai2/client.c
```
*(Tuyệt đối không chứa file thực thi, file rác `.o`, file `.md`, thư mục `storage/` hay file ảnh test).*
