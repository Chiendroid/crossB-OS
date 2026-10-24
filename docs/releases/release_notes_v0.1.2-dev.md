### 🚀 Bản cập nhật crossB OS v0.1.2-dev (Kênh Thử nghiệm Dev)

Phiên bản cập nhật dành cho Xteink X4 Pro trên kênh phát triển (Dev Channel) tập trung vào sửa triệt để các lỗi crash, hoàn thiện nhận diện thương hiệu **crossB OS**, và tăng cường độ mượt mà cho trải nghiệm đọc sách.

---

#### 🌟 Điểm nổi bật & Cải tiến mới:
1. **Thiết kế Logo Khởi động Mới crossB OS**:
   - Thay thế hoàn toàn logo CrossPoint cũ.
   - Biểu tượng mới thiết kế dạng khung Squircle bo tròn hiện đại kết hợp cuốn sách mở cách điệu hai cánh chữ **B** sang trọng, đi kèm thông tin phiên bản hệ thống rõ nét.

2. **Khắc phục Triệt để Lỗi Đồng hồ trên Status Bar**:
   - Xử lý vấn đề chip RTC PCF8563 bị khóa cờ điện áp thấp (`VL_FLAG`) khi chưa đồng bộ internet khiến đồng hồ biến mất ở tất cả giao diện.
   - Tự động kích hoạt dao động RTC, kết hợp bộ bù nội suy mili-giây và cơ chế đồng hồ dự phòng hệ thống để giờ luôn hiển thị chính xác trên mọi theme.

3. **Sửa lỗi Tràn chữ trong Ứng dụng Thư viện ảnh (Gallery)**:
   - Tách biệt rõ ràng vị trí hiển thị giữa tiêu đề *"Không tìm thấy hình ảnh"* và chú thích thư mục `"/wallpapers"`, căn chỉnh cân đối, không còn hiện tượng chữ đè lên nhau.

---

#### 🛠️ Sửa lỗi Trình đọc & Nâng cao Độ Ổn định:
1. **Khắc phục Crash khi chọn Kẻ trang (Underline)**:
   - Chuyển cơ chế lưu cài đặt kẻ trang sang dạng bản nháp (draft setting) trong Drawer Menu, không còn ghi I/O flash LittleFS đột ngột giữa lúc đang render và mở menu.
   - Thêm chốt bảo vệ `bodyPitch` tối thiểu ngăn chặn vòng lặp vô hạn gây tràn ngăn xếp (Stack Overflow) và Task Watchdog panic.

2. **Khắc phục Nháy màn hình khi Bật/Tắt Cảm ứng ở Quick Settings**:
   - Loại bỏ chu kỳ đảo màu toàn màn hình (clean refresh) khi nhấn nút chuyển đổi cảm ứng, thao tác tức thì và êm ái.

3. **Tích hợp 4 Tối ưu Kiến trúc Trọng điểm**:
   - **Trì hoãn ghi tiến độ (Debounced Progress Save)**: Gom tác vụ ghi vị trí đọc sách sau 1.5 giây nhàn rỗi hoặc khi thoát sách, loại bỏ hiện tượng khựng giật khi lật trang nhanh liên tục và bảo vệ độ bền chip Flash.
   - **Kiểm soát Vòng lặp Giao diện (Render Boundary Safety)**: Thiết lập giới hạn kích thước tối thiểu cho các thành phần dòng kẻ sách.
   - **Giảm tải Giao tiếp Ngoại vi I2C**: Tối ưu tần suất truy vấn chip thời gian thực RTC, tiết kiệm năng lượng.
   - **Tối ưu Hàng đợi Cập nhật E-ink**: Chỉ kích hoạt làm mới cục bộ khi thay đổi trạng thái các tile điều khiển nhanh.

---

#### ℹ️ Về Cơ chế Màn hình Khóa / Màn hình Ngủ:
- **Cơ chế Tiết kiệm Pin**: Khi thiết bị chuyển sang trạng thái ngủ, vi điều khiển ESP32-S3 sẽ kích hoạt chế độ **Deep Sleep**, ngắt hoàn toàn nguồn điện CPU và chỉ duy trì vài chục microampere. Do đặc tính của công nghệ màn hình E-ink giữ nguyên hình ảnh mà không cần điện năng, màn hình đồng hồ lúc ngủ sẽ hiển thị tĩnh thời điểm bắt đầu ngủ để đảm bảo thời lượng pin kéo dài hàng tuần.

---
**Checksum SHA-256**:
- `crossB-OS-v0.1.2-dev-x4pro.bin`: `8e2c05b7cf128d8765c04e5854e620e0a5e89dfd48a84ee328a267a6203121a3`
