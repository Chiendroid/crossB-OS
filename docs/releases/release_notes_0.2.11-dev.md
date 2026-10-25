### Bản cập nhật crossB OS 0.2.11-dev (Kênh Thử nghiệm Dev)

Bản phát hành 0.2.11-dev tập trung giải quyết triệt để vấn đề mất điều khiển trên thiết bị Xteink X4 Pro — trang bị hệ thống phím cứng dự phòng toàn diện, tối ưu hoá độ nhạy cảm ứng màn hình và nâng cấp độ ổn định cho chip cảm ứng Goodix GT911.

---

#### Chi tiết thay đổi & Cải tiến Cốt lõi:

1. **Hệ Thống Phím Cứng Dự Phòng Toàn Diện (Emergency Key Fallback Navigation):**
   - **Duyệt Toàn Bộ Giao Diện Màn Hình Chính Bằng Phím Trái/Phải (Up/Down):** Trước đây, trên X4 Pro với giao diện Cover Grid Home, phím Trái và Phải chỉ có thể cuộn qua lại giữa các cuốn sách gần đây mà không thể nhảy xuống thanh Dock Bar phía dưới. Trong bản 0.2.11-dev, khi bấm phím Phải (Down) qua hết danh mục sách, con trỏ sẽ tiếp tục di chuyển xuống 4 tab của Dock Bar (Gần đây, Thư viện, Ứng dụng, Cài đặt) và tự động bung mở Dock Bar. Người dùng có thể tiếp cận menu Cài đặt và cập nhật OTA 100% bằng phím bấm vật lý mà không cần màn hình cảm ứng.
   - **Nút Nguồn (Power Button) Hoạt Động Như Nút Confirm Mặc Định:** Nhấn nhả nhanh nút Nguồn sẽ kích hoạt lệnh Chọn / Mở (Confirm) ngay từ khi xuất xưởng. Nhấn giữ nút Nguồn > 800ms đưa máy vào chế độ Ngủ (Sleep). Bấm đúp nút Nguồn bật/tắt đèn nền (Frontlight).
   - **Nhấn Giữ Nút Trái (Up) Làm Nút Quay Lại (BACK):** Giữ nút Trái > 450ms hoạt động như nút BACK trên thiết bị X4 Pro, cho phép quay lại màn hình trước ở bất kỳ đâu.
   - **Nhấn Giữ Nút Phải (Down) Làm Nút Chọn (CONFIRM):** Giữ nút Phải > 450ms hoạt động như nút CONFIRM, giúp xác nhận lựa chọn dễ dàng chỉ bằng một tay.
   - **Tổ Hợp Phím Khẩn Cấp Bấm Cùng Lúc Trái + Phải (UP + DOWN):** Hoạt động như phao cứu sinh (Emergency Escape Hatch) — Khi đang đọc sách sẽ thoát về Trang chủ; khi ở Trang chủ sẽ mở thẳng vào màn hình Cài đặt (Settings); khi ở các menu con sẽ lập tức quay lại màn hình trước.

2. **Tối Ưu Độ Nhạy Cảm Ứng & Bung Mở Dock Bar:**
   - Mở rộng vùng chạm ở đáy màn hình từ `y >= 755` lên `y >= 720` (toàn bộ 80px dải đáy màn hình) và vùng vuốt mở từ `startY >= 680`. Bất kỳ thao tác chạm hoặc vuốt nhẹ nào ở đáy màn hình đều lập tức bung mở Dock Bar nhạy bén, không còn bị hụt hoặc trượt cử chỉ.

3. **Củng Cố Độ Ổn Định Chip Cảm Ứng Goodix GT911 & Bus I2C:**
   - Bật nguồn sớm cho đường nguồn cảm ứng (GPIO 2 Active-LOW) ngay từ `holdPowerRails()` ở micro-giây đầu tiên khi máy khởi động. Giúp điện áp cảm ứng ổn định hoàn toàn trước khi reset.
   - Tối ưu chuỗi xung reset và tăng thời gian chờ lên 100ms theo đúng datasheet Goodix để chip nạp cấu hình nội bộ ổn định.
   - Cơ chế thử lại (retry) khi xóa thanh ghi trạng thái `0x814E` và tự động khôi phục bus I2C khi phát hiện lỗi đọc liên tiếp, ngăn chặn 100% hiện tượng kẹt cảm ứng.

4. **Chuẩn Hóa Phiên Bản `0.2.11-dev` Không Tiền Tố 'v':**
   - Đảm bảo tính tương thích tuyệt đối cho hệ thống cập nhật OTA tự động.

---

#### Checksum SHA-256:

- `crossB-OS-0.2.11-dev-x4pro.bin`: `9597200f38b5b4164cf81df216f6505a434b6bb6cbdfb9f746d4b0c64280798d`
- `crossB-OS-0.2.11-dev-x4pro-factory.bin`: `b347a0cc9e68d22cd4bed0420584278190e42955fb18ed777465b8cd32ba7c44`
