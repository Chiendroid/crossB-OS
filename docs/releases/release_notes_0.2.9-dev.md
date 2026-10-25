### Bản cập nhật crossB OS 0.2.9-dev (Kênh Thử nghiệm Dev)

Bản phát hành 0.2.9-dev giải quyết triệt để lỗi đứng hình / treo đơ cứng máy khi đang đọc sách trên thiết bị Xteink X4 Pro, loại bỏ hoàn toàn hiện tượng ép lệnh refresh khi chip EPD đang bận (overrun), ổn định xung nhịp CPU/PSRAM và trang bị cơ chế bảo vệ thoát khóa deadlock an toàn.

---

#### Chi tiết thay đổi & Cải tiến Cốt lõi:

1. **Khắc Phục Triệt Để Hiện Tượng Ép Refresh (EPD Overrun Prevention):**
   - **Cơ chế gây lỗi:** Khi lật trang sách, đổi trang nhanh hoặc mở menu reader, hệ thống gửi liên tiếp các lệnh refresh mới (`displayBuffer`, `displayWithRefreshCycle`) trong khi chip điều khiển E-ink (SSD1677 / UC8179 / UC8279) vẫn đang bận thực hiện chu kỳ quét waveform (đặc biệt ở chế độ khử răng cưa Grayscale / Anti-Aliasing). Việc bị "ép" nhận lệnh mới khi chân BUSY đang ở mức bận khiến bộ điều khiển phần cứng EPD rơi vào trạng thái lỗi và kéo chân BUSY ở mức LOW vĩnh viễn.
   - **Giải pháp:** Bổ sung cơ chế đồng bộ `renderer.waitRefreshComplete()` trước khi bắt đầu hiển thị trang mới trong `renderContents()`, trước khi mở Drawer Menu trong `openReaderMenu()`, và trước khi Drawer tiến hành làm mới màn hình. Mọi chu kỳ quét trước đó đều được đợi hoàn tất 100% trước khi phát lệnh mới.

2. **Khôi Phục Timeout An Toàn 15 Giây Cho Chân BUSY (EpdBus waitBusy):**
   - **Cơ chế gây lỗi:** Ở nhánh `BusyPolarity::UcIdleHigh` của bộ điều khiển UC8179/UC8279, vòng lặp `while (digitalRead(_pins.busy) == LOW)` không có điểm dừng nếu xảy ra lỗi phần cứng hoặc sụt áp đột ngột. Việc Core 1 kẹt vô tận trong khi giữ khóa `renderingMutex` làm tê liệt Core 0 và khiến toàn bộ thiết bị đơ cứng.
   - **Giải pháp:** Thiết lập timeout cứng an toàn 15 giây (`if (millis() - start > 15000) break;`). Nếu EPD phản hồi chậm hoặc gặp sự cố bất thường, hệ thống vẫn tự động giải phóng khóa, xuất log cảnh báo và duy trì sự ổn định của hệ điều hành.

3. **Cố Định Tần Số Hoạt Động CPU Khi Đang Đọc Sách (Power Stability):**
   - **Cơ chế gây lỗi:** Sau 3 giây không chạm (`IDLE_POWER_SAVING_MS = 3000`), vòng lặp chính chuyển tần số CPU từ 240MHz xuống 80MHz (`setCpuFrequencyMhz(80)`). Trên ESP32-S3 sử dụng Octal SPI PSRAM, việc nhảy tần số liên tục mỗi khi người dùng dừng đọc rồi lật trang làm biến động xung nhịp APB/MSPI, gây lỗi tranh chấp với tác vụ đọc trước ngầm (`idlePrewarm`) và phân trang nền (`backgroundBuild`) từ thẻ nhớ SDMMC vào PSRAM.
   - **Giải pháp:** Khóa xung nhịp CPU ở mức tối đa 240MHz ổn định trong suốt phiên đọc sách (`activityManager.isReaderActivity()`), loại bỏ hoàn toàn rung giật xung clock ngoại vi SPI/I2C/PSRAM.

4. **Chuẩn Hóa Định Danh Phiên Bản Không Có Tiền Tố 'v':**
   - Kể từ bản phát hành này, tên phiên bản, tên file binary và git tag được chuẩn hóa hoàn toàn theo định dạng `0.2.9-dev`, đồng bộ trên toàn bộ hệ thống OTA và giao diện hiển thị.

---

#### Checksum SHA-256:
- `crossB-OS-0.2.9-dev-x4pro.bin`: `54f1b132f757fa9995b554e8dce12837e13a0ce452e4bd33ea5cb23c2536b342`
- `crossB-OS-0.2.9-dev-x4pro-factory.bin`: `70a0a36991157133223d5e3de5961104b235089355781baf0e77628809b8e316`
