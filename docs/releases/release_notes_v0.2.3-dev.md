### Bản cập nhật crossB OS v0.2.3-dev (Kênh Thử nghiệm Dev)

Phiên bản cập nhật dành cho Xteink X4 Pro trên kênh phát triển (Dev Channel) tập trung vào việc tối ưu hóa pin triệt để cho màn hình ngủ đồng hồ (loại bỏ hoàn toàn việc bật thẻ nhớ SD và hạ xung nhịp CPU trong chu kỳ cập nhật mỗi phút), bổ sung cơ chế bảo vệ pin thông minh, đồng thời khắc phục triệt để lỗi không hiển thị nhật ký thay đổi của bản cập nhật mới trên giao diện Cập nhật OTA.

---

#### Điểm nổi bật & Cải tiến trong bản v0.2.3-dev:

1. **Tối Ưu Hóa Năng Lượng Đột Phá Cho Màn Hình Ngủ Đồng Hồ (Ultra Low-Power Sleep Clock):**
   - **Loại bỏ kích hoạt thẻ nhớ SD mỗi phút:** Cấu hình màn hình ngủ (chế độ nền tối/sáng, định dạng 12h/24h, ngôn ngữ) được lưu trữ trực tiếp trên bộ nhớ RTC SRAM lưu điện (`RTC_DATA_ATTR`). Trong chu kỳ thức dậy mỗi phút của đồng hồ ngủ, hệ thống hoàn toàn không kích hoạt hay cấp nguồn cho thẻ nhớ SD, triệt tiêu dòng tiêu thụ đỉnh (50-100mA) và rút ngắn thời gian thức từ gần 1 giây xuống dưới 80 mili-giây (tiết kiệm hơn 85-90% năng lượng).
   - **Hạ xung nhịp CPU xuống 80MHz:** Trong chu kỳ cập nhật đồng hồ ngủ, tần số CPU được giảm từ 240MHz xuống 80MHz, giảm tối đa mức tiêu thụ điện của nhân vi xử lý ESP32-S3 mà vẫn đảm bảo tính toán thời gian và vẽ đồng hồ cực nhanh.
   - **Loại bỏ thời gian trễ chờ USB Serial:** Loại bỏ hoàn toàn độ trễ 250ms chờ cổng USB CDC khi máy thức dậy từ RTC timer, đưa chip vào trạng thái deep sleep trở lại gần như tức thì.
   - **Cơ chế bảo vệ pin thông minh nhiều tầng:**
     - Khi pin còn dưới hoặc bằng 20%: Hệ thống tự động giãn chu kỳ cập nhật đồng hồ từ 1 phút lên 5 phút một lần để bảo toàn dung lượng pin.
     - Khi pin còn dưới hoặc bằng 10% (mức nguy cấp): Tự động tắt hoàn toàn RTC timer, giữ màn hình tĩnh và đưa máy vào chế độ ngủ sâu tuyệt đối để tránh làm cạn kiệt pin thiết bị.

2. **Khắc Phục Triệt Để Lỗi Không Hiển Thị Nhật Ký Thay Đổi Của Bản Cập Nhật Mới:**
   - **Giải mã stream trực tiếp không giới hạn bộ đệm:** Xây dựng bộ giải mã trực tiếp luồng stream trường `body` trong `ReleaseJsonParser`, hỗ trợ dung lượng mô tả phiên bản lên đến 4096 bytes mà không bị giới hạn bởi bộ đệm token 2048 bytes cũ của thư viện JSON, loại bỏ hiện tượng tràn bộ đệm làm rơi mất nội dung changelog khi bản phát hành có mô tả chi tiết.
   - **Hỗ trợ đầy đủ ký tự tiếng Việt và giải mã Unicode:** Bộ giải mã tự động xử lý các ký tự xuống dòng, trích xuất chuỗi thoát (`\n`, `\r`, `\t`, `\"`) và giải mã các mã Unicode escape (`\uXXXX`) sang UTF-8 tiếng Việt chuẩn xác.
   - **Ngăn chặn hiển thị nhầm changelog bản cũ:** Trong trường hợp có phiên bản mới trên máy chủ nhưng chưa tải được mô tả hoặc bản phát hành không kèm ghi chú, hệ thống sẽ thông báo rõ ràng "Bản cập nhật mới không kèm nội dung nhật ký chi tiết", tuyệt đối không dùng lại nhật ký của bản firmware cũ đang chạy trên máy.

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.3-dev-x4pro.bin`: `ab6cb7d6410bf3df78353a034d8bc1aacdf77fff62a0188a79a24d846ddf444c`
- `crossB-OS-v0.2.3-dev-x4pro-factory.bin`: `358b9e59cfed3e21cbca036d19a98d8a8264b571dd836375cc8c164d54c7d4f6`
