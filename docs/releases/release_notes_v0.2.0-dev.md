### Bản cập nhật crossB OS v0.2.0-dev (Kênh Thử nghiệm Dev)

Phiên bản cập nhật dành cho Xteink X4 Pro trên kênh phát triển (Dev Channel) tập trung vào sửa triệt để lỗi crash khi chọn cỡ chữ trong trình đọc sách, nâng cấp trình duyệt Web Reader, khắc phục bộ nhớ đệm bìa sách, giao diện cập nhật OTA trong Cài đặt và tối ưu hóa hệ thống.

---

#### Điểm nổi bật & Cải tiến trong bản v0.2.0-dev:

1. **Khắc phục Triệt để Lỗi Crash khi Đổi Cỡ Chữ (Font Size) trong Trình đọc:**
   - **Bảo vệ Luồng Nạp Font:** Đưa toàn bộ tiến trình tải và thay đổi kích thước Font (cả Vector TTF lẫn Bitmap .cpfont) vào bên trong phạm vi đồng bộ của `RenderLock`, loại bỏ hoàn toàn hiện tượng Race Condition / Use-After-Free giữa Main Task và Render Task.
   - **Đồng bộ Tiến độ Đọc An toàn:** Bọc hàm gom lưu tiến độ đọc (`flushPendingProgressSave`) bằng `RenderLock(Mode::Try)`, bảo đảm không truy cập dữ liệu trang và bộ đệm khi chương sách đang được phân trang hoặc xây dựng lại.
   - **Chống Bấm Dồn Dập (Busy-Guard):** Bổ sung chốt bảo vệ `RenderLock::peek()` cho hai nút tăng/giảm cỡ chữ (`[+]` / `[-]`), ngăn chặn việc thao tác liên tục làm phân mảnh heap và tràn bộ nhớ RAM nội bộ của ESP32-S3.
   - **Giải Phóng Bộ Đệm Cache:** Tự động giải phóng cache glyphs SD cũ (`releaseSdFontCaches`) trước khi nạp cỡ chữ mới, duy trì vùng nhớ heap luôn thông thoáng.

2. **Khôi phục Giao Diện Menu Trình Đọc CrossInk Drawer:**
   - Khôi phục giao diện menu dạng ngăn kéo CrossInk Drawer trực quan, đầy đủ tính năng tra từ, chú thích, nhảy trang và tùy chỉnh hiển thị theo thói quen sử dụng.
   - Nút Chuyển tập tin trên Quick Settings đã được thay thế thành nút **Cài đặt** (biểu tượng bánh răng) tiện lợi.

3. **Khắc phục Triệt Để Lỗi Bộ Nhớ Đệm Bìa Sách (Cover Cache):**
   - Loại bỏ việc ghi file 0-byte rác `thumb_[HEIGHT].bmp` khi sinh cover thất bại, tránh ô nhiễm bộ nhớ đệm vĩnh viễn.
   - Bổ sung hàm kiểm tra tính hợp lệ của file BMP (`isFileValidBmp >= 54 bytes`), tự động phát hiện và tạo lại ảnh bìa chuẩn nếu gặp file rác.
   - Bổ sung lệnh lưu đường dẫn bìa bền vững vào `RecentBooksStore.json` khi tạo thumbnail thành công.

4. **Nâng Cấp Trình Duyệt Web & Sửa Lỗi ERR_EMPTY_RESPONSE:**
   - Khắc phục lỗi kẹt bộ lọc HTML khi gặp dấu so sánh `<, >` trong JavaScript/CSS bằng State Machine chuẩn HTML5, loại bỏ tình trạng nuốt mất nội dung gây lỗi rỗng trang (ERR_EMPTY_RESPONSE).
   - Bảo toàn trọn vẹn thẻ `<header>` chứa tiêu đề bài viết và các trang HTML5 lược bỏ thẻ đóng `</head>`.
   - Bổ sung cơ chế Fallback đọc thô từ file HTML tạm bảo vệ người dùng không bị mất nội dung.
   - Khắc phục crash khi lướt web: Xử lý cắt chuỗi tiếng Việt UTF-8 chuẩn xác (loại bỏ hoàn toàn lead byte mồ côi), kiểm tra kích thước file ảnh cache trước khi giải mã JPEG, thu hồi RAM trước khi nạp trang mới.
   - Tinh chỉnh padding-top và làm sạch bài viết ngoại tuyến (Offline articles).

5. **Nâng Cấp Giao Diện Cập Nhật OTA trong Cài Đặt:**
   - Hiển thị rõ ràng phiên bản hiện tại và phiên bản mới có sẵn.
   - Tích hợp khung xem nhật ký thay đổi (Changelog) cuộn được ngay trên màn hình.
   - Bổ sung nút Cập nhật và Đổi kênh trực quan, chỉ hiển thị hộp thoại xác nhận khi người dùng bấm Cập nhật, không tự ý thoát màn hình.
   - Loại bỏ toàn bộ emoji và ký tự đặc biệt trang trí.

6. **Tăng Chiều Cao Bottom Dock Bar & Tối Ưu Repository:**
   - Dock dưới chân màn hình chính được tinh chỉnh độ cao thoáng hơn, thao tác chạm chính xác và thẩm mỹ hơn trên màn hình 4.3 inch.
   - Chuẩn hóa `.gitignore` để bảo vệ mã nguồn, che giấu các tệp log, sao lưu và cấu hình cục bộ khi public repository.

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.0-dev-x4pro.bin`: `fb7b6daaf6a8fa24b3be9660b4bef0bd5ca59534baee8661895e7391d6de9484`
- `crossB-OS-v0.2.0-dev-x4pro-factory.bin`: `17184802070f4e8b037e9dbdf7a1246c9896d4ee0eaec8237e9d34b1b47b6bf1`
