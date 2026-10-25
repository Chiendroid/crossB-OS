### Bản cập nhật crossB OS v0.2.5-dev (Kênh Thử nghiệm Dev)

Bản cập nhật v0.2.5-dev dành cho Xteink X4 Pro trên kênh phát triển (Dev Channel) tập trung vào việc khôi phục và tối ưu hóa triệt để độ ổn định phần cứng, bảo vệ tuổi thọ màn hình E-ink, khắc phục hoàn toàn lỗi treo máy trong túi khi ngủ, đưa hệ thống về chế độ Deep Sleep tĩnh nguyên bản (12µA), chuyển đổi màn hình ngủ sang hai chế độ chuẩn E-reader **"Màn hình Sáng"** và **"Màn hình Tối"**, loại bỏ màn hình Boot khi mở máy và sửa dứt điểm lỗi crash khi đổi cỡ chữ 14pt.

---

#### Điểm nổi bật & Cải tiến trong bản v0.2.5-dev:

1. **Khôi Phục Deep Sleep Tĩnh Tuyệt Đối - Triệt Tiêu Lỗi Treo Cứng Máy Trong Túi:**
   - **Xóa bỏ hoàn toàn cơ chế Light Sleep và vòng lặp ngầm:** Loại bỏ vòng lặp thức dậy mỗi 60 giây và hàm vẽ ngầm trên màn hình E-ink khi đang ngủ.
   - **Loại bỏ nguy cơ Deadlock chân BUSY của màn hình E-ink:** Bản cũ do gọi lệnh làm tươi màn hình khi chip E-ink đã vào trạng thái ngắt nguồn khiến chân `BUSY` bị treo vĩnh viễn ở vòng lặp `waitBusy()`, làm CPU ESP32-S3 bị khóa cứng và mất phản ứng nút nguồn. Việc đưa về Deep Sleep tĩnh giúp loại bỏ 100% hiện tượng này.
   - **Dòng tiêu thụ cực thấp (12µA):** Thiết bị ngủ sâu hoàn toàn, không tiêu hao pin, không sinh nhiệt trong túi và có thời lượng pin chờ kéo dài hàng tháng.

2. **Chuyển Đổi Màn Hình Ngủ Thành "Màn hình Sáng" & "Màn hình Tối" Chuẩn E-Reader:**
   - **Loại bỏ đồng hồ số tự làm mới:** Nhằm bảo vệ sức khỏe hạt mực điện tử (E-ink particles), tránh hiện tượng lưu ảnh và làm giảm tuổi thọ màn hình do làm tươi liên tục mỗi phút.
   - **Bổ sung hai chế độ ngủ chuẩn:**
     - **Màn hình Sáng (`LIGHT`):** Hiển thị màn hình ngủ nền trắng trang nhã, logo đồ họa CrossBasic OS lớn, phần trăm pin và thông điệp hướng dẫn rõ ràng.
     - **Màn hình Tối (`DARK`):** Đảo màu tuyền toàn bộ giao diện thành nền đen huyền bí, chữ và logo trắng sắc nét, phù hợp cho người dùng thích phong cách Dark Mode hoặc đọc đêm.
   - Cả hai chế độ chỉ vẽ đúng 1 lần duy nhất khi bấm nút nguồn tắt máy với chế độ quét sạch mực `kSleepClean`, hoàn toàn không quét lại trong suốt thời gian ngủ.

3. **Khôi Phục Cơ Chế Khởi Động Nhanh Tức Thì (Splashless Wake):**
   - Loại bỏ việc khởi động lại mềm (`esp_restart()`), khôi phục việc thức dậy chuẩn từ Deep Sleep bằng ngắt nút nguồn phần cứng (`HalGPIO::WakeupReason::PowerButton`).
   - Kích hoạt cơ chế `SplashlessWake`, mở thẳng vào trang sách đang đọc hoặc màn hình chính chỉ trong khoảng 300ms, không còn xuất hiện màn hình logo Boot gây khó chịu.

4. **Khắc Phục Triệt Để Lỗi Crash Khi Đổi Cỡ Chữ (Đặc Biệt Cỡ 14pt):**
   - **Loại bỏ Live Preview dồn dập trong thanh cài đặt:** Khi chọn cỡ chữ trong Drawer, hệ thống không còn ép nạp font từ thẻ SD và render preview liên tục ở nền, loại bỏ hiện tượng tràn bộ nhớ đệm và xung đột phân trang dồn dập.
   - **Hiển thị thông báo "Đang đánh chỉ mục..." ngay lập tức:** Khi người dùng đóng menu sau khi đổi cỡ chữ, hệ thống lập tức hiển thị popup `Đang đánh chỉ mục...` để người dùng an tâm chờ hệ thống tính toán lại số trang sách.
   - **Chống Task Watchdog Timeout (TWDT):** Bổ sung lệnh nhường CPU (`yield()`) vào tất cả các vòng lặp tính toán và phân trang chương sách (`while (!section->isBuildComplete())`), đảm bảo quá trình đánh chỉ mục cho các chương dài với cỡ chữ nhỏ diễn ra mượt mà 100%, không bị Watchdog reset hệ thống.

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.5-dev-x4pro.bin`: `d728c7de00bc03ba3c60e1b2530748d229df3b669fcbd189b6c8c998de9980c6`
- `crossB-OS-v0.2.5-dev-x4pro-factory.bin`: `230c4a5e8a06c3a49c6a5aa6e214c5b62988be45b07b5161180d3c273c565f07`
