# BÀI TẬP TUẦN 2 – IT4062 — Domain/IP Resolver

Chương trình dòng lệnh tra cứu thông tin tên miền / địa chỉ IP:

```
./resolver parameter
```

- Nếu `parameter` là **địa chỉ IPv4 hợp lệ** → hiển thị *tên miền chính thức*
  và các *tên miền phụ (alias)* trỏ tới địa chỉ đó (reverse DNS lookup).
- Nếu `parameter` là **tên miền** → hiển thị *địa chỉ IP chính thức* và các
  *địa chỉ IP phụ (alias)* mà tên miền đó phân giải tới (forward DNS lookup).
- Nếu không tìm thấy thông tin → in `Not found information`.

## 1. Cấu trúc thư mục nộp bài

```
DoXuanQuy_20235818_HW2.zip
├── resolver.c     # mã nguồn chính
└── Makefile       # để biên dịch ra file thực thi "resolver"
```

## 2. Biên dịch (build)

Yêu cầu: Linux, có `gcc` và `make` (thường có sẵn; nếu chưa có: `sudo apt install build-essential`).

```bash
make            # biên dịch, sinh ra file thực thi "resolver"
```

Muốn dọn file build:

```bash
make clean      # xoá file thực thi và file .o
```

Kiểm tra không có lỗi/cảnh báo:

```bash
gcc -Wall -Wextra -g -o resolver resolver.c
```

## 3. Cách chạy

```bash
./resolver <ten_mien_hoac_dia_chi_IP>
```

Ví dụ:

```bash
./resolver google.com
./resolver 216.58.197.110
```

Nếu không truyền đúng 1 tham số, chương trình in hướng dẫn dùng ra `stderr`
và thoát với mã lỗi 1.

## 4. Test chi tiết (khớp với đề bài)

Chạy lần lượt các lệnh sau và đối chiếu kết quả:

```bash
./resolver google.com
```
Kết quả mong đợi (địa chỉ IP thực tế của Google có thể thay đổi theo thời điểm/vị trí,
nhưng định dạng đầu ra sẽ là):
```
Official IP: <IP chính thức>
Alias IP:
<IP phụ 1>
<IP phụ 2>
...
```
(Lưu ý: Google hiện thường chỉ trả về 1 địa chỉ IP, nên phần "Alias IP" có thể
không xuất hiện — điều này là bình thường và đúng với domain hiện tại của Google,
khác với ví dụ minh hoạ trong đề bài dùng dữ liệu cũ.)

```bash
./resolver 8.8.8.8
```
Sẽ trả về tên miền chính thức của DNS server Google (ví dụ `dns.google`).

```bash
./resolver aznsc.test.com
```
→ `Not found information` (tên miền không tồn tại)

```bash
./resolver 259.12.34.12
```
→ `Not found information` (259 không phải octet IPv4 hợp lệ, 0–255)

```bash
./resolver 1.2.3
```
→ `Not found information` (thiếu octet, không phải IPv4 hợp lệ — chương trình
đã xử lý riêng để không bị `gethostbyname()` hiểu nhầm sang ký hiệu IP kiểu cũ)

```bash
./resolver
```
→ In hướng dẫn sử dụng, thoát mã lỗi 1 (không khớp với `argc != 2`)

### Test với domain có nhiều IP / nhiều alias name để thấy rõ phần "Alias"

Một số domain lớn có CDN thường trả nhiều IP:

```bash
./resolver facebook.com
./resolver microsoft.com
```

Một số IP khi tra ngược có thể có nhiều alias name (tuỳ nhà cung cấp DNS):

```bash
./resolver 8.8.4.4
```

> **Lưu ý:** Kết quả DNS phụ thuộc vào thời điểm chạy và server DNS đang dùng
> (khác với ví dụ cố định trong đề bài — đó là dữ liệu minh hoạ cũ của Google
> không còn đúng ở hiện tại). Điều quan trọng là **định dạng đầu ra** và
> **logic xử lý** (IP hợp lệ / tên miền / không hợp lệ / không tìm thấy) phải
> đúng như mô tả.

## 5. Giải thích kỹ thuật ngắn gọn

- Dùng `inet_pton()` để kiểm tra `parameter` có phải IPv4 hợp lệ không.
- Nếu là IP hợp lệ: dùng `gethostbyaddr()` để tra cứu ngược
  (`h_name` = tên chính thức, `h_aliases` = danh sách tên phụ).
- Nếu không phải IP: dùng `gethostbyname()` để tra cứu xuôi
  (`h_addr_list[0]` = IP chính thức, `h_addr_list[1..]` = IP phụ).
- Xử lý riêng các chuỗi "giống IP nhưng không hợp lệ" (chỉ gồm số và dấu chấm,
  ví dụ `1.2.3`, `259.12.34.12`): báo "Not found information" ngay, vì
  `gethostbyname()` trên Linux/glibc có thể hiểu nhầm các chuỗi dạng này theo
  ký hiệu địa chỉ kiểu cũ (legacy dotted notation) và trả về kết quả sai lệch
  thay vì báo lỗi.
- Không tìm thấy (`gethostbyname`/`gethostbyaddr` trả về `NULL`) → in
  `Not found information`.

## 6. Đóng gói nộp bài

Cách nhanh nhất (đã tích hợp trong Makefile):

```bash
make package    # hoặc: make zip
```

Lệnh này sẽ tự động `clean` các file build tạm, sau đó đóng gói `resolver.c` và `Makefile` vào file nén `DoXuanQuy_20235818_HW2.zip`.

Kiểm tra lại nội dung file zip trước khi nộp:

```bash
unzip -l DoXuanQuy_20235818_HW2.zip
```

