### Bản cập nhật crossB OS v0.2.6-dev (Kênh Thử nghiệm Dev)

Bản phát hành v0.2.6-dev tập trung giải quyết triệt để vấn đề đứng hình / treo máy sau một thời gian sử dụng, khắc phục hoàn toàn lỗi crash khi thay đổi font/cỡ chữ nhiều lần trong trình đọc, và chuẩn hóa màn hình ngủ Sáng / Tối với bố cục 2 thẻ thanh lịch, loại bỏ số đồng hồ để tối ưu hóa độ bền màn hình E-ink và pin.

---

#### Chi tiết thay đổi & Cải tiến:

1. **Khắc phục Triệt để Lỗi Treo Máy / Đứng Im và Bật Nguồn Không Lên:**
   - **Thức dậy chuẩn xác từ Deep Sleep:** Sửa bỏ kiểm tra `wakeHoldVerified` không phù hợp trong `setup()` khiến máy nhận nhầm thao tác bấm nút nguồn nhanh là ghost-wake và tự động đưa máy vào lại chế độ ngủ ngay lập tức. Giờ đây chỉ cần một click nhẹ vào nút nguồn là thiết bị thức dậy và vào lại giao diện ngay.
   - **Xóa bỏ các điểm chặn loop:** Loại bỏ các tàn dư của RTC timer wake và kiểm tra thừa trong vòng lặp chính `main.cpp`, giúp hệ điều hành vận hành mượt mà, liên tục, không bị treo cứng sau một thời gian hoạt động.

2. **Khắc phục Lỗi Crash / Treo khi Đổi Cỡ Chữ và Font 2-3 Lần:**
   - **Bảo toàn Font Cache Manager:** Xóa bỏ lệnh `clearCache()` và `releaseSdFontCaches()` thô bạo trong `EpubReaderDrawerActivity.cpp` và `EpubReaderActivity.cpp`. Việc xóa cache font giữa chừng trước đây gây lỗi Use-After-Free và phá vỡ cấu trúc bộ nhớ FreeType sau 2-3 lần thao tác.
   - **Chuẩn hóa luồng `renderPreview` và `applyReaderTextSettings`:** Đồng bộ theo tiêu chuẩn của CrossInk và Upstream, khôi phục `liveSettings` đúng thời điểm và tách bạch các phạm vi khóa `RenderLock`, bảo đảm việc thay đổi kích cỡ chữ (12pt, 14pt, 16pt...) hoặc chuyển đổi font chữ diễn ra mượt mà 100%, không bị văng hay đứng màn hình.
   - **Làm sạch luồng dựng trang trong `renderBook`:** Xóa bỏ cờ gián đoạn popup và các lệnh `yield()` lạ được chèn vào các vòng lặp while, giữ cho tiến trình phân trang EPUB hoạt động ổn định và nhất quán.

3. **Thiết kế Màn hình Ngủ Sáng / Tối 2 Thẻ (Không Có Đồng Hồ):**
   - **Bố cục 2 Thẻ Trang Nhã:**
     - **Header:** Hiển thị trạng thái `ĐÃ KHÓA · % pin` cùng ngày tháng và lịch âm thanh lịch.
     - **Thẻ 1 (Sách đang đọc):** Hiển thị bìa sách (ảnh bìa thực tế hoặc hoa văn tinh tế), tên sách, tác giả, % tiến độ đọc và thanh tiến độ.
     - **Thẻ 2 (Thống kê & Heatmap):** Hiển thị tổng thời gian đọc sách trong tháng, số cuốn hoàn thành, khung chuỗi ngày đọc, cùng Lưới lịch Heatmap 7 cột trực quan.
     - **Footer:** Dòng chữ nhắc nhở `Bấm nút nguồn để tiếp tục`.
   - **Bảo vệ Màn hình & Pin Tuyệt Đối:** Loại bỏ hoàn toàn số đồng hồ thay đổi mỗi phút. Màn hình chỉ vẽ đúng 1 lần duy nhất khi ngủ rồi đưa vi điều khiển vào chế độ Deep Sleep tĩnh hoàn toàn (tiêu thụ ~12µA), loại bỏ hiện tượng nhấp nháy màn hình và tiết kiệm pin tối đa.
   - **Chế độ Sáng & Tối:** Tương thích cả 2 tùy chọn `Màn hình Sáng` (nền trắng chữ đen) và `Màn hình Tối` (nền đen tuyền, chữ trắng).

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.6-dev-x4pro.bin`: `98fd317bd144d00a8cbf4af162c6bf031643bf4e3a8f27b07f97a49f1b73a072`
- `crossB-OS-v0.2.6-dev-x4pro-factory.bin`: `58624d5c4265fd719e1040518c205084c0a3b2911d33071d780763664aabf004`

