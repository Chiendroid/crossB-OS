### Bản cập nhật crossB OS v0.2.1-dev (Kênh Thử nghiệm Dev)

Phiên bản cập nhật dành cho Xteink X4 Pro trên kênh phát triển (Dev Channel) tập trung sửa lỗi RTC làm mới màn hình ngủ làm mất thông tin cuốn sách đang đọc, nâng cao tính phản hồi khi bấm nút nguồn thức dậy, cập nhật trình duyệt Web Reader, bộ nhớ đệm bìa sách và giao diện cập nhật OTA trong Cài đặt.

---

#### Điểm nổi bật & Cải tiến trong bản v0.2.1-dev:

1. **Khắc phục Triệt Để Lỗi RTC Màn Hình Ngủ Làm Mất Sách Đang Đọc:**
   - **Đồng Bộ Trạng Thái Khi Đánh Thức Bằng RTC Timer:** Khi RTC Timer thức dậy mỗi 60 giây để cập nhật đồng hồ số lớn, hệ thống tự động nạp đầy đủ cấu hình phiên đọc và danh sách sách gần đây (APP_STATE, RECENT_BOOKS, I18N, UITheme). Nhờ đó, thẻ thông tin sách đang đọc (gồm ảnh bìa, tiêu đề, tác giả, thanh phần trăm tiến độ) luôn được bảo toàn trọn vẹn, không còn bị chuyển thành trạng thái trống sau 1 phút như trước.
   - **Bảo Vệ Đánh Thức Nút Nguồn Tức Thì:** Bổ sung cơ chế phát hiện nút nguồn ngay trong chu kỳ thức dậy của RTC Timer. Nếu người dùng nhấn nút nguồn để sử dụng thiết bị, hệ thống lập tức hủy lệnh ngủ tiếp và khởi động thẳng vào giao diện đọc sách hiện tại.
   - **Xóa Cờ Báo Lỗi Crash Khi Trang Đầu Render Thành Công:** Đặt lại chỉ số tải sách (readerActivityLoadCount) về 0 ngay khi trang đầu tiên được dựng thành công, ngăn ngừa hiện tượng máy hiểu lầm là trình đọc bị sập để văng ra màn hình chính.
   - **Tự Động Bù Đắp Đường Dẫn Sách Trước Khi Vào Giấc Ngủ:** Hàm chuyển vào giấc ngủ sâu (enterDeepSleep) tự động đối chiếu cuốn sách đang mở gần nhất trong danh sách lịch sử đọc để không bao giờ bị mất dấu sách.
   - **Tinh Chỉnh Hiển Thị Tiến Độ Đọc:** Loại bỏ hoàn toàn chuỗi thời gian cố định, thay bằng thông tin tiến độ thực tế chính xác.

2. **Khôi phục Giao Diện Menu Trình Đọc CrossInk Drawer:**
   - Khôi phục giao diện menu dạng ngăn kéo CrossInk Drawer trực quan, đầy đủ tính năng tra từ, chú thích, nhảy trang và tùy chỉnh hiển thị theo thói quen sử dụng.
   - Nút Chuyển tập tin trên Quick Settings đã được thay thế thành nút Cài đặt (biểu tượng bánh răng) tiện lợi.

3. **Nâng Cấp Trình Duyệt Web & Sửa Lỗi ERR_EMPTY_RESPONSE:**
   - Khắc phục lỗi kẹt bộ lọc HTML khi gặp dấu so sánh trong JavaScript và CSS bằng State Machine chuẩn HTML5, loại bỏ tình trạng nuốt mất nội dung gây lỗi rỗng trang (ERR_EMPTY_RESPONSE).
   - Bảo toàn trọn vẹn thẻ header chứa tiêu đề bài viết và các trang HTML5 lược bỏ thẻ đóng head.
   - Bổ sung cơ chế Fallback đọc thô từ file HTML tạm bảo vệ người dùng không bị mất nội dung.
   - Khắc phục lỗi crash khi lướt web: Xử lý cắt chuỗi tiếng Việt UTF-8 chuẩn xác, kiểm tra kích thước file ảnh cache trước khi giải mã JPEG, thu hồi RAM trước khi nạp trang mới.
   - Tinh chỉnh khoảng cách lề trên và làm sạch bài viết ngoại tuyến (Offline articles).

4. **Khắc phục Triệt Để Lỗi Bộ Nhớ Đệm Bìa Sách (Cover Cache):**
   - Loại bỏ việc ghi file 0-byte rác khi sinh thumbnail thất bại, tránh ô nhiễm bộ nhớ đệm vĩnh viễn.
   - Bổ sung hàm kiểm tra tính hợp lệ của file BMP (kích thước tối thiểu 54 bytes), tự động phát hiện và tạo lại ảnh bìa chuẩn nếu gặp file rác.
   - Bổ sung lệnh lưu đường dẫn bìa bền vững vào recent.json khi tạo thumbnail thành công.

5. **Nâng Cấp Giao Diện Cập Nhật OTA trong Cài Đặt:**
   - Hiển thị rõ ràng phiên bản hiện tại và phiên bản mới có sẵn.
   - Tích hợp khung xem nhật ký thay đổi (Changelog) cuộn được ngay trên màn hình.
   - Bổ sung nút Cập nhật và Đổi kênh trực quan, chỉ hiển thị hộp thoại xác nhận khi người dùng bấm Cập nhật, không tự ý thoát màn hình.
   - Loại bỏ toàn bộ emoji và ký tự đặc biệt trang trí.

6. **Tăng Chiều Cao Bottom Dock Bar & Tối Ưu Repository:**
   - Dock dưới chân màn hình chính được tinh chỉnh độ cao thoáng hơn, thao tác chạm chính xác và thẩm mỹ hơn trên màn hình 4.3 inch.
   - Chuẩn hóa .gitignore để bảo vệ mã nguồn, che giấu các tệp log, sao lưu và cấu hình cục bộ khi public repository.

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.1-dev-x4pro.bin`: `d520f8bf13ee486c6882106800cae3eeede95b87a910d6958fbdce43036c12d4`
- `crossB-OS-v0.2.1-dev-x4pro-factory.bin`: `5bb73263d27593323d29d0efe829daf37ad5154fdf11bc24d6018b197b00f23f`
