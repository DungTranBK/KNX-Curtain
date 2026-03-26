# BÁO CÁO PHÂN TÍCH SỰ CỐ HỆ THỐNG HOME CONTROLLER

## 1. Thông tin chung
* **Tên sự cố:** Lỗi mất dữ liệu thiết bị tại Bridge Bluetooth sau khi khởi động lại.
* **Thiết bị ảnh hưởng:** Home Controller (HC).
* **Mốc thời gian ghi nhận:** * 16h44 ngày 28/01
    * 17h09 ngày 29/01 (Sau khi mất nguồn đột ngột).
* **Mức độ nghiêm trọng:** **Critical** (Ảnh hưởng trực tiếp đến khả năng điều khiển thiết bị của người dùng).

---

## 2. Mô tả hiện tượng & Vấn đề (Problem Statement)
Hệ thống lưu trữ trên HC được chia làm 2 phân vùng: **DB HC-G1** (Logic) và **DB Bluetooth** (Thông tin thiết bị mạng). 

**Sự cố xảy ra:**
1.  Sau khi khởi động lại (Reboot/Power Cycle), toàn bộ dữ liệu trong **DB Bluetooth** bị mất sạch.
2.  `HC-coor` truy vấn thông tin nhưng Bridge trả về lỗi: `[nullptr] Null object at [Elements.hpp]`.
3.  **Hệ quả:** Người dùng mất hoàn toàn quyền điều khiển thiết bị từ App/Cloud. Hệ thống chỉ có thể tự phục hồi từng thiết bị khi thiết bị đó gửi bản tin trạng thái định kỳ (khoảng 30 phút/lần).

---

## 3. Kiểm tra & Tái hiện lỗi tại công trình

**Ngày kiểm tra:** 30/01/2026

### Case 1: Ngắt nguồn HC trong điều kiện sử dụng bình thường
| Hạng mục | Kết quả |
|----------|---------|
| Điều khiển nhóm | ✅ Phản hồi nhanh, không gặp lỗi |
| Thiết bị đơn lẻ | ✅ Không offline, phản hồi nhanh |
| Dữ liệu `bluetooth.db` | ✅ Không bị mất |
| Đồng bộ lại thông tin | ✅ Không có hiện tượng đồng bộ lại |

> [!NOTE]
> **Kết luận Case 1:** Hệ thống hoạt động ổn định khi ngắt nguồn trong điều kiện bình thường (không có tác vụ ghi DB).

---

### Case 2: Ngắt nguồn HC trong lúc gia nhập/cấu hình thiết bị
| Hạng mục | Kết quả |
|----------|---------|
| Điều khiển nhóm | ⚠️ Gửi lệnh bình thường nhưng thiết bị thực tế không phản hồi |
| Thiết bị đơn lẻ | ❌ Hiển thị **offline** hoặc phản hồi chậm |
| Dữ liệu `bluetooth.db` | ❌ **Bị mất** |
| Đồng bộ lại thông tin | ❌ Có hiện tượng đồng bộ lại lần lượt khi thiết bị gửi bản tin trạng thái |
| Logic backup DB | ⚠️ **Nghi ngờ** cơ chế backup `bluetooth.db` không hoạt động đúng *(Cần tái hiện & kiểm tra logic tại R&D)* |

> [!CAUTION]
> **Kết luận Case 2:** Lỗi chỉ xảy ra khi ngắt nguồn **đúng thời điểm** hệ thống đang ghi dữ liệu (gia nhập/cấu hình). Điều này khẳng định nguyên nhân gốc rễ nằm ở **cơ chế ghi file không an toàn (Non-atomic Write)**.

---

## 4. Phân tích nguyên nhân gốc rễ (Root Cause Analysis)

### 4.1. Lỗi lưu trữ tệp tin (File System Persistence)
* **Write Caching:** Hệ thống có thể đang sử dụng cơ chế lưu trữ không đồng bộ. Khi mất điện đột ngột, dữ liệu còn nằm trên RAM Cache chưa kịp ghi xuống chip Flash, dẫn đến file DB bị trống hoặc hư hỏng.
* **Non-atomic Write:** Quy trình ghi đè trực tiếp lên file DB chính khiến file dễ bị lỗi `nullptr` nếu tiến trình bị ngắt quãng giữa chừng.

### 4.2. Rào cản phục hồi chủ động
* Việc chủ động gửi bản tin `Get` thông tin toàn mạng ngay khi boot là **không khả thi** do:
    * Gây quá tải băng thông mạng (Network Congestion).
    * Gây nghẽn cổ chai tại Bridge Bluetooth, có thể dẫn đến treo hệ thống.

---

## 5. Phương án đề xuất xử lý

Để khắc phục triệt để mà không gây quá tải hệ thống, cần tập trung vào việc bảo vệ tính toàn vẹn của dữ liệu tại nguồn.

### 5.1. Giải pháp kỹ thuật ưu tiên (Dành cho đội Dev)
* **Cơ chế Ghi nguyên tử (Atomic Write):** 1. Ghi dữ liệu vào một file tạm (`.tmp`).
    2. Gọi lệnh `fsync()` để ép dữ liệu từ RAM xuống Flash.
    3. Dùng lệnh `rename()` để thay thế file chính bằng file tạm (thao tác này đảm bảo tính toàn vẹn).
* **Cơ chế Shadow Backup:** Luôn duy trì một bản sao dự phòng (`db_bluetooth.bak`). Nếu file chính bị lỗi khi khởi động, HC sẽ tự động khôi phục từ bản backup này để có dữ liệu điều khiển ngay lập tức.

### 5.2. Tối ưu hóa tại tầng Firmware
* **Xử lý ngoại lệ (Exception Handling):** Bổ sung kiểm tra điều kiện `if (object == nullptr)` tại `Elements.hpp` để hệ thống không bị crash và có thể đưa ra phản hồi phù hợp cho người dùng thay vì trạng thái treo.
* **On-demand Sync:** Thay vì đợi 30 phút, nếu có lệnh điều khiển từ người dùng cho một thiết bị chưa có thông tin, HC sẽ ưu tiên "Get" thông tin thiết bị đó ngay tại thời điểm đó.

---

## 6. Kết luận
Nguyên nhân cốt lõi không nằm ở giao thức Bluetooth mà nằm ở **cơ chế quản lý tệp tin trên HC**. Việc triển khai **Atomic Write** và **Backup DB** là giải pháp tối ưu nhất để đảm bảo hệ thống luôn sẵn sàng điều khiển ngay sau khi khởi động lại mà không gây rủi ro nghẽn mạng.

---
**Người lập báo cáo:** [Tên của bạn]  
**Ngày lập:** 30/01/2026