---
description: Quy trình phân tích Zephyr Coredump từ serial log (dump.log)
---

Quy trình này hướng dẫn cách chuyển đổi một file log thô chứa dữ liệu coredump thành thông tin debug có giá trị để tìm ra nguyên nhân crash (NULL pointer, Stack overflow, Race condition).

### Bước 0: Kiểm tra cấu hình Coredump (Prerequisites)
Trước khi phân tích, cần đảm bảo firmware đã được build với các flag coredump. Kiểm tra file `prj.conf` xem đã có các dòng sau chưa:
```conf
#DEBUG CONFIG
CONFIG_DEBUG_COREDUMP=y
CONFIG_DEBUG_COREDUMP_BACKEND_LOGGING=y
```
- Nếu chưa có: Thêm vào cuối file `prj.conf`, build lại và nạp firmware mới để có thể thu được dữ liệu coredump khi crash.

### Bước 1: Tiền xử lý Log (Cleaning)
Log thu thập từ serial thường lẫn các ký tự điều khiển (ANSI), timestamp, hoặc prefix của logger. Cần tách riêng khối dữ liệu hex.
1. Tìm khối dữ liệu bắt đầu bằng `--- BEGIN CPPLOAD ---` và kết thúc bằng `--- END CPPLOAD ---`.
2. Sử dụng script `clean_new_log.py` (hoặc tương đương) để lọc bỏ các ký tự phi hex và đảm bảo định dạng sạch.
// turbo
3. `python3 clean_new_log.py dump.log dump_clean.log`

### Bước 2: Chuyển đổi sang Binary
Sử dụng công cụ của Zephyr SDK để parse file hex sạch thành file binary coredump.
// turbo
1. `python3 $ZEPHYR_SDK_PATH/zephyr/scripts/coredump/coredump_serial_log_parser.py dump_clean.log dump.bin`

### Bước 3: Khởi chạy GDB Server cho Coredump
Cần file `zephyr.elf` tương ứng với firmware đã crash để map địa chỉ vùng nhớ.
// turbo
1. `python3 $ZEPHYR_SDK_PATH/zephyr/scripts/coredump/coredump_gdbserver.py zephyr.elf dump.bin`
*(Lưu ý: Mặc định server sẽ chờ ở port 1234)*

### Bước 4: Phân tích bằng GDB
Mở GDB tương ứng với kiến trúc chip (ví dụ: `arm-zephyr-eabi-gdb`) và kết nối tới server.
1. `arm-zephyr-eabi-gdb zephyr.elf`
2. Trong GDB: `(gdb) target remote :1234`
3. Xem các thanh ghi: `(gdb) info registers` -> Tìm `pc` (Program Counter) và `sp` (Stack Pointer).
4. Xem call stack: `(gdb) bt` (Backtrace).

### Bước 5: Nhận diện các Pattern lỗi phổ biến
- **PC trỏ vào địa chỉ hợp lệ nhưng `this` hoặc register tham số rất thấp (0x0...0x500)**: Lỗi NULL pointer dereference với offset (truy cập member của struct/class khi con trỏ base bị NULL).
- **SP (Stack Pointer) = 0x0**: Stack bị phá hủy hoàn toàn (Severe Stack Corruption hoặc Stack Overflow).
- **Crash xảy ra ở các lệnh liên quan đến con trỏ nhưng giá trị con trỏ thay đổi bất ngờ**: Race condition (nhiều thread cùng ghi đè một vùng nhớ).
- **Crash trong ISR**: Lỗi xử lý ngắt, lưu ý việc sử dụng các function không an toàn trong interrupt context.

### Bước 6: Đối chiếu Source Code và Fix
1. Dùng `list *<địa chỉ PC>` trong GDB để tìm dòng code gây crash.
2. Kiểm tra trạng thái khởi tạo của Object tại thời điểm đó (ví dụ: `knx->configured()`).
3. Áp dụng các kỹ thuật bảo vệ: **Mutex** cho Race condition, **Check NULL/Config** cho Memory safety.
