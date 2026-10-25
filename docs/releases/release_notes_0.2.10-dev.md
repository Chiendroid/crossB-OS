### Bản cập nhật crossB OS 0.2.10-dev (Kênh Thử nghiệm Dev)

Bản phát hành 0.2.10-dev giải quyết dứt điểm nguyên nhân gốc rễ gây ra hiện tượng **treo đơ cứng máy (freeze)** trên thiết bị Xteink X4 Pro — bao gồm cả khi đang đọc sách và **kể cả khi không đọc sách** (ở màn hình chính, menu cài đặt hoặc để yên máy một lúc).

---

#### Chi tiết thay đổi & Cải tiến Cốt lõi:

1. **Khắc Phục Hiện Tượng Giật Xung Nhịp CPU/PSRAM (Vô Hiệu Hóa Frequency Scaling Khi Thức):**
   - **Cơ chế gây lỗi:** Trước đây, sau mỗi 3 giây không có thao tác cảm ứng/nút bấm (`IDLE_POWER_SAVING_MS`), hệ thống tự động ép xung nhịp CPU từ 240MHz xuống 80MHz (`setCpuFrequencyMhz(80)`), và khi có thao tác lại kéo lên 240MHz. Trên chip ESP32-S3 sử dụng bộ nhớ Octal SPI PSRAM (`dio_opi`) và chia sẻ chung đường I2C (cảm ứng GT911, mạch đo pin CW2017, đồng hồ RTC), việc đổi xung nhịp CPU/APB liên tục trong lúc máy đang thức làm rung giật xung clock ngoại vi MSPI/I2C. Khi xung nhịp I2C bị đứt gãy giữa chừng, chip cảm ứng GT911 bị mất đồng bộ và giữ chân SDA ở mức LOW, khiến toàn bộ giao thức `Wire` bị khóa cứng (I2C Bus Lockup), dẫn đến máy đơ hoàn toàn kể cả khi không đọc sách.
   - **Giải pháp:** Vô hiệu hóa việc gọi `setCpuFrequencyMhz` trong runtime trên X4 Pro và các bo mạch có PSRAM trong `HalPowerManager::setPowerSaving()`. Máy luôn duy trì mức 240MHz ổn định tuyệt đối trong suốt thời gian thức. Khi không có tác vụ, vòng lặp `delay(10)` của FreeRTOS sẽ tự động đưa CPU vào trạng thái ngắt xung nhịp phần cứng (`waiti 0`), tiết kiệm điện tối ưu mà không gây rủi ro đứt gãy xung nhịp. Chế độ ngủ sâu (Deep Sleep) vẫn được kích hoạt hoàn toàn khi hết thời gian chờ hoặc bấm nút nguồn.

2. **Trang Bị Cơ Chế Tự Phục Hồi Bus I2C (Hardware I2C Bus Recovery):**
   - **Cơ chế:** Khi chip slave (GT911 hoặc CW2017) gặp xung nhiễu hoặc sụt áp đường truyền FPC và kéo chân dữ liệu SDA xuống mức LOW, bộ điều khiển I2C của ESP32 không thể khởi tạo xung START Condition và sẽ bị treo hoặc liên tục báo lỗi.
   - **Giải pháp:** Bổ sung hàm phục hồi `freeink::i2c::recoverBus()` trong `I2cBusLock.h` và `InputManager.cpp`. Khi phát hiện chân SDA bị giữ mức LOW bất thường sau lỗi truyền nhận, hệ thống tự động phát 9 xung nhịp SCL để ép chip slave nhả đường SDA về mức HIGH, tạo tín hiệu STOP Condition và khởi tạo lại bus I2C trong tích tắc, ngăn chặn 100% tình trạng khóa cứng bus.

3. **Loại Bỏ Nguy Cơ Deadlock Trong Đồng Bộ Hiển Thị (Safe `requestUpdateAndWait`):**
   - **Cơ chế gây lỗi:** Hàm đồng bộ hiển thị `ActivityManager::requestUpdateAndWait()` trước đây sử dụng các lệnh kiểm tra có thể dẫn đến việc gọi `ulTaskNotifyTake(pdTRUE, portMAX_DELAY)` khi task hiện tại đang nắm giữ khóa `renderingMutex`, gây ra hiện tượng khóa lẫn nhau (deadlock) vĩnh viễn giữa Core 0 và Core 1.
   - **Giải pháp:** Bổ sung các bước kiểm tra an toàn theo chuẩn CrossInk. Nếu phát hiện lệnh gọi xuất phát từ render task, từ task đang chờ khác hoặc khi đang nắm giữ `RenderLock`, hàm sẽ ghi log cảnh báo và thoát ngay lập tức thay vì block vô tận.

4. **Chuẩn Hóa Phiên Bản `0.2.10-dev` Không Tiền Tố 'v':**
   - Thống nhất quy ước phiên bản không dùng tiền tố `v`, đảm bảo trải nghiệm cập nhật OTA liền mạch và đồng bộ.

---

#### Checksum SHA-256:
- `crossB-OS-0.2.10-dev-x4pro.bin`: `1edb6beb6928a671bb443102e133169cb8e934154dd1cfcb6faacedc8d11cd7b`
- `crossB-OS-0.2.10-dev-x4pro-factory.bin`: `aa34a24edbc26cb85cd185db8836f34b03575308fd865bd01f923a592aafdb7e`
