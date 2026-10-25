### Bản cập nhật crossB OS v0.2.7-dev (Kênh Thử nghiệm Dev)

Bản phát hành v0.2.7-dev giải quyết triệt để vấn đề đứng hình / treo cứng máy (Freeze) ngay cả khi đang đọc sách bình thường, bảo vệ an toàn phần cứng và tối ưu hóa hệ thống giao tiếp đa luồng trên phần cứng Xteink X4 Pro.

---

#### Chi tiết thay đổi & Cải tiến Cốt lõi:

1. **Khắc phục Triệt để Tranh Chấp Bus I2C Đa Luồng (I2C Contention Deadlock):**
   - **Cơ chế gây lỗi:** Trên Xteink X4 Pro, chip cảm ứng điện dung GT911, đồng hồ RTC BM8563 và IC đo dung lượng pin CW2017 cùng chia sẻ chung một đường bus I2C (SDA=GPIO39, SCL=GPIO38). Khi Core 0 quét cảm ứng liên tục còn Core 1 vẽ thanh trạng thái (đọc % pin & giờ RTC), việc truy cập đồng thời vào phần cứng I2C của ESP32-S3 làm biến dạng xung nhịp, dẫn đến việc thiết bị ngoại vi kéo chân SDA xuống mức LOW vĩnh viễn và đóng băng hệ điều hành.
   - **Giải pháp:** Bổ sung cơ chế khóa đệ quy thread-safe `freeink::i2c::ScopedLock` (sử dụng Recursive Mutex của FreeRTOS) với cơ chế timeout an toàn 200ms bảo vệ toàn bộ các thao tác đọc/ghi thanh ghi I2C trên cả 3 trình điều khiển (GT911, CW2017, BM8563).

2. **Bổ sung Timeout An toàn 15 giây cho Bộ điều khiển E-ink (EpdBus waitBusy):**
   - **Cơ chế gây lỗi:** Trình điều khiển màn hình E-ink của X4 Pro (UC8179 / UC8279) ở nhánh `BusyPolarity::UcIdleHigh` trước đây không có timeout giới hạn (vòng lặp `while (digitalRead(_pins.busy) == LOW)` vô hạn). Khi có sự sụt áp nhất thời hoặc xung nhiễu SPI, chân BUSY bị giữ ở mức LOW khiến Core 1 kẹt vô tận trong khi đang giữ khóa `renderingMutex`, gián tiếp làm tê liệt toàn bộ Core 0 và dừng hoàn toàn mọi tác vụ đọc sách.
   - **Giải pháp:** Thiết lập timeout cứng 15 giây (`if (millis() - start > 15000) break;`), bảo đảm nếu phần cứng E-ink phản hồi chậm hoặc gặp sự cố bất thường, hệ thống vẫn tự động thoát khóa an toàn, ghi nhận cảnh báo và duy trì hoạt động bình thường của firmware.

3. **Hỗ trợ Đánh Thức Vòng Lặp Chính Bằng Phím Digital & Cảm Ứng X4 Pro:**
   - **Cơ chế gây lỗi:** `HalGPIO::rawInputActive()` trước đây chỉ kiểm tra phím dạng thang điện áp ADC và nút nguồn, không hỗ trợ nhận diện các phím Digital (GPIO0, GPIO7) và đường ngắt cảm ứng Touch INT (GPIO10) của X4 Pro, khiến vòng lặp tiết kiệm điện 50ms bị trễ nhịp phản hồi khi người dùng thao tác.
   - **Giải pháp:** Nâng cấp `HalGPIO::rawInputActive()` để quét ngay lập tức trạng thái các phím Digital và tín hiệu ngắt cảm ứng GT911, cho phép phản hồi tức thì với mọi thao tác lật trang của người dùng.

4. **Ổn định Xung Nhịp Hệ Thống & Tránh Sụt Xung APB Khi Đang Đọc:**
   - Đảm bảo các giao tiếp phần cứng không bị ảnh hưởng bởi việc dao động tần số CPU đột ngột khi chuyển đổi trạng thái nguồn.

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.7-dev-x4pro.bin`: `c8bad40fa2c1d6ae357bf69cee969d2f280592dae18508a5fd4021c17ba6e937`
- `crossB-OS-v0.2.7-dev-x4pro-factory.bin`: `818ff59c7c24ccf2fc17624fc396d7dcaa709d358330cb07d3538e778c51776d`
