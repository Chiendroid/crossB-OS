### 🚀 Bản cập nhật crossB OS v0.2.0-dev (Kênh Thử nghiệm Dev)

Phiên bản cập nhật dành cho Xteink X4 Pro trên kênh phát triển (Dev Channel) tập trung vào sửa triệt để lỗi crash khi chọn cỡ chữ trong trình đọc sách, tái cấu trúc menu đọc tinh gọn, nâng cấp độ cao Dock và tối ưu trình duyệt Web Reader, đồng thời dọn dẹp và tối ưu hóa repository sạch sẽ chuẩn mã nguồn mở.

---

#### 🌟 Điểm nổi bật & Cải tiến trong bản v0.2.0-dev:

1. **Khắc phục Triệt để Lỗi Crash khi Đổi Cỡ Chữ (Font Size) trong Trình đọc:**
   - **Bảo vệ Luồng Nạp Font:** Đưa toàn bộ tiến trình tải và thay đổi kích thước Font (cả Vector TTF lẫn Bitmap .cpfont) vào bên trong phạm vi đồng bộ của `RenderLock`, loại bỏ hoàn toàn hiện tượng Race Condition / Use-After-Free giữa Main Task và Render Task.
   - **Đồng bộ Tiến độ Đọc An toàn:** Bọc hàm gom lưu tiến độ đọc (`flushPendingProgressSave`) bằng `RenderLock(Mode::Try)`, bảo đảm không truy cập dữ liệu trang và bộ đệm khi chương sách đang được phân trang hoặc xây dựng lại.
   - **Chống Bấm Dồn Dập (Busy-Guard):** Bổ sung chốt bảo vệ `RenderLock::peek()` cho hai nút tăng/giảm cỡ chữ (`[+]` / `[-]`), ngăn chặn việc thao tác liên tục làm phân mảnh heap và tràn bộ nhớ RAM nội bộ của ESP32-S3.
   - **Giải Phóng Bộ Đệm Cache:** Tự động giải phóng cache glyphs SD cũ (`releaseSdFontCaches`) trước khi nạp cỡ chữ mới, duy trì vùng nhớ heap luôn thông thoáng.

2. **Tối ưu Hóa Giao Diện & Menu Trình Đọc Sách:**
   - Tinh giản mã nguồn: Loại bỏ hoàn toàn menu drawer cũ cồng kềnh, chuyển sang sử dụng thanh công cụ `ReaderToolbarUi` hiện đại, mượt mà và trực quan.
   - Nút Chuyển tập tin trên Quick Settings đã được thay thế thành nút **Cài đặt** (biểu tượng bánh răng) tiện lợi.

3. **Cải Tiến Trình Duyệt Web (Web Reader):**
   - Tăng padding-top để các thành phần trang web không còn bị chạm mép màn hình E-ink.
   - Làm sạch bài viết ngoại tuyến (Offline article): Tự động lược bỏ toàn bộ chỉ dẫn dư thừa và dấu ngoặc đơn `( )` gây rối mắt.
   - Tối ưu hóa bộ nhớ khi tải trang web nặng, hạn chế lỗi hết bộ nhớ (OOM).

4. **Tăng Chiều Cao Bottom Dock Bar:**
   - Dock dưới chân màn hình chính được tinh chỉnh độ cao thoáng hơn, thao tác chạm chính xác và thẩm mỹ hơn trên màn hình 4.3 inch.

5. **Tối Ưu Hóa & Dọn Dẹp Repository:**
   - Loại bỏ các thư mục nháp/tạm (`disabled_src`) khỏi git tracking.
   - Chuẩn hóa quy tắc `.gitignore` để bảo vệ mã nguồn, che giấu các tệp log, sao lưu và cấu hình cục bộ không cần thiết khi public repository.

---

#### 🔐 Checksum SHA-256:
- `crossB-OS-v0.2.0-dev-x4pro.bin`: `75e23d56808d35f63dfce4d63d5d6a345fdf0f3555c280a92bdf27cdab067b27`
