### Bản cập nhật crossB OS v0.2.8-dev (Kênh Thử nghiệm Dev)

Bản phát hành v0.2.8-dev khôi phục thiết kế Logo khởi động (Boot Logo) bitmap sắc nét và trang nhã, khôi phục 100% nguyên bản an toàn phần cứng cho màn hình E-ink, đồng thời bổ sung cơ chế khóa I2C đa luồng và hỗ trợ chuyển tiếp cập nhật OTA mượt mà.

---

#### Chi tiết thay đổi & Cải tiến Cốt lõi:

1. **Khôi phục Logo Khởi Động Bitmap Sắc Nét & Tinh Tế (Boot Screen):**
   - Loại bỏ hoàn toàn phần vẽ vector thủ công thô kệch bằng hình khối nguyên thủy.
   - Nạp lại ảnh bitmap `Logo120` (120x120 pixels) chính hãng, căn chỉnh bố cục chính giữa màn hình với độ tương phản cao, sắc nét trên tấm nền E-ink.
   - Căn chỉnh typography tên hệ điều hành **crossB OS** (font Noto Sans đậm), dòng trạng thái khởi động và thông tin phiên bản ở cuối màn hình hài hòa, tinh tế.

2. **Khôi phục 100% Nguyên Bản An Toàn Phần Cứng Màn Hình E-ink:**
   - Hoàn tác (revert) hoàn toàn cơ chế timeout cưỡng bức 15 giây trong `EpdBus::waitBusy` theo đúng nguyên lý thiết kế và chỉ dẫn an toàn phần cứng của tác giả gốc FreeInk.
   - Đảm bảo các chu kỳ refresh phần cứng e-paper hoàn tất trọn vẹn điện áp waveform, chống hiện tượng burn-in và thoái hóa tấm nền màn hình.

3. **Bảo Vệ Đa Luồng Tuyệt Đối Cho Bus I2C (I2C ScopedLock):**
   - Áp dụng `freeink::i2c::ScopedLock` (sử dụng Recursive Mutex của FreeRTOS, timeout 200ms) trên toàn bộ các trình điều khiển dùng chung bus ngoại vi I2C (SDA=GPIO39, SCL=GPIO38):
     - Chip cảm ứng điện dung GT911 (`gt911ReadReg`, `gt911ClearStatus`).
     - IC đo dung lượng pin CW2017 (`readReg8`, `readReg16`, `writeReg8`, `writeReg16`).
     - Đồng hồ thời gian thực RTC BM8563 (`writeReg`, `readRegs`).
   - Loại bỏ nguy cơ xung đột dữ liệu đa nhân (Core 0 quét touch liên tục song song Core 1 đọc pin/RTC vẽ thanh trạng thái), ngăn chặn triệt để tình trạng treo cứng máy (freeze) khi đang đọc sách.

4. **Hỗ Trợ Chuyển Tiếp Cập Nhật OTA Linh Hoạt:**
   - Bổ sung cơ chế so khớp thông minh trong `OtaUpdater::isUpdateNewer()`, cho phép người dùng chuyển tiếp cập nhật OTA liền mạch từ các bản build cũ hoặc bản CrossInk mang số hiệu 1.x.x sang nhánh phát triển chính thức của **crossB-OS (0.2.x)**.

5. **Phản Hồi Thao Tác Tức Thì Trong Vòng Lặp Chính:**
   - Tối ưu `HalGPIO::rawInputActive()` để quét ngay lập tức trạng thái các phím Digital (GPIO0, GPIO7) và đường ngắt cảm ứng Touch INT (GPIO10) của X4 Pro, cho phép đánh thức vòng lặp nhàn rỗi và phản hồi tức thì với thao tác của người dùng.

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.8-dev-x4pro.bin`: `24cdf37b4214a53f19b47ddb7823a64ed7395dc7c602baa11217e6ecc7961230`
- `crossB-OS-v0.2.8-dev-x4pro-factory.bin`: `df2af50afb610a5262401a047e8f14ab958150611f3068a29f1cb8e99a23ab02`
