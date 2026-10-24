# Tài Liệu Kỹ Thuật Phần Cứng Xteink X4 Pro

Tài liệu này tổng hợp toàn bộ thông số phần cứng, sơ đồ chân GPIO, kiến trúc mạch nguồn, mạch lái đèn nền và giải pháp bảo vệ phần cứng cho thiết bị **Xteink X4 Pro** khi chạy firmware CrossPoint Reader.

---

## 1. Thông Số Tổng Thể

* **Vi điều khiển trung tâm (SoC):** Espressif ESP32-S3 (Dual-core Xtensa LX7 lên tới 240 MHz).
* **Bộ nhớ:** 16 MB SPI Flash, 8 MB Octal PSRAM.
* **Màn hình hiển thị:** E-ink kích thước 4.3 inch, độ phân giải 800 x 480 pixels (hoặc panel tương thích).
* **Màn hình cảm ứng (Touch):** Goodix GT911 Capacitive Touch Controller (I2C).
* **Hệ thống đèn nền (Frontlight):** Đèn nền kép 2 kênh màu (Cool White & Warm Amber).
* **Nguồn điện:** Pin Li-Po, tích hợp IC quản lý sạc và chân chốt nguồn (Peripheral Power Latch).

---

## 2. Bảng Phân Bổ Chân GPIO (Pin Assignment)

| Chân GPIO | Chức năng phần cứng | Chế độ / Cấu hình | Ghi chú an toàn |
| :--- | :--- | :--- | :--- |
| **GPIO 1** | Peripheral Power Latch | OUTPUT (Active HIGH) | Giữ mức HIGH để cấp nguồn cho màn hình & đèn nền; ngắt LOW khi tắt hẳn nguồn. |
| **GPIO 4** | Touch I2C SDA | OPEN-DRAIN / Pull-up | Bus I2C kết nối IC cảm ứng GT911. |
| **GPIO 5** | Touch I2C SCL | OPEN-DRAIN / Pull-up | Xung nhịp I2C cho GT911. |
| **GPIO 6** | E-ink BUSY | INPUT (Pull-up) | Mức LOW = bận refresh; HIGH = sẵn sàng nhận lệnh SPI tiếp theo. |
| **GPIO 7** | Touch INT (Interrupt) | INPUT (Pull-up) | Đánh thức MCU khi có thao tác chạm cảm ứng. |
| **GPIO 8** | Frontlight Cool PWM | OUTPUT / LEDC Channel 0 | Tần số PWM 25 kHz, độ phân giải 10-bit (0 - 1023). |
| **GPIO 9** | Frontlight Warm PWM | OUTPUT / LEDC Channel 1 | Tần số PWM 25 kHz, độ phân giải 10-bit (0 - 1023). |
| **GPIO 10** | Touch RST (Reset) | OUTPUT | Kéo LOW để reset IC GT911 khi khởi động. |
| **GPIO 11** | E-ink SPI MOSI | SPI OUTPUT | Dữ liệu lệnh và framebuffer gửi đến E-ink controller. |
| **GPIO 12** | E-ink SPI SCLK | SPI OUTPUT | Xung nhịp SPI (thường chạy 10 MHz - 20 MHz). |
| **GPIO 13** | E-ink SPI CS (Chip Select)| OUTPUT (Active LOW) | Chọn chip E-ink. Kéo HIGH khi nhả bus. |
| **GPIO 14** | E-ink RST (Reset) | OUTPUT (Active LOW) | Xung reset cứng cho panel E-ink trước khi nạp lệnh khởi tạo. |
| **GPIO 18** | E-ink DC (Data / Command) | OUTPUT | LOW = Command byte; HIGH = Data byte. |

---

## 3. Kiến Trúc Mạch Đèn Nền & Vấn Đề Bảo Vệ Chống Cháy

### 3.1. Nguyên lý hoạt động
Mạch đèn nền trên Xteink X4 Pro sử dụng IC Boost Converter để nâng điện áp pin (3.7V - 4.2V) lên khoảng 9V - 12V cấp cho chuỗi LED nối tiếp. Dòng điện qua LED được điều chế bằng MOSFET lái ở chân cực máng (drain), nhận xung điều khiển PWM từ **GPIO 8** (Cool) và **GPIO 9** (Warm).

### 3.2. Nguy cơ cháy cuộn cảm & rò dòng khi ngủ (Deep Sleep)
* Khi ESP32-S3 vào Deep Sleep, các chân GPIO thông thường sẽ bị ngắt kết nối với mạch logic nội (pad isolation) và chuyển sang trạng thái trở kháng cao (High-Z / floating).
* Nếu cổng (gate) của MOSFET lái LED bị thả nổi khi cuộn cảm boost converter vẫn có điện tích hoặc đường nguồn peripheral latch (`GPIO 1`) chưa ngắt, MOSFET có thể dẫn một phần (partial conduction), tạo ra dòng rò liên tục (quiescent leakage current) từ 15mA đến hơn 50mA.
* Hậu quả thực tế:
  * Pin tụt cạn chỉ sau 1 đêm (GitHub Issue `#2857`).
  * Cuộn cảm bị quá nhiệt cục bộ, làm biến dạng hoặc cháy đứt chuỗi LED sau thời gian dài sử dụng.

### 3.3. Giải pháp cấp phần mềm bắt buộc (Hardware Protection Protocol)
Trước khi gọi hàm `esp_deep_sleep_start()`:
1. Gọi `Frontlight.setOn(false)` để ngừng phát xung LEDC PWM.
2. Cấu hình `pinMode(pin, OUTPUT)` và kéo chân xuống mức **LOW** tuyệt đối.
3. Kích hoạt tính năng giữ trạng thái pad độc lập qua **RTC I/O Hold**:
   ```cpp
   gpio_hold_en(GPIO_NUM_8);
   gpio_hold_en(GPIO_NUM_9);
   ```
4. Khi MCU khởi động lại từ sleep, hàm `holdPowerRails()` sẽ giải phóng pad hold bằng `gpio_hold_dis()` trước khi khởi tạo lại driver PWM.

---

## 4. Cơ Chế Nhận Diện 3 Lô Controller Màn Hình (E-ink Controller Probing)

Khác với các dòng máy đọc sách đơn bản, Xteink X4 Pro có ít nhất 3 biến thể mạch điều khiển màn hình E-ink trên thị trường:

1. **Solomon Systech SSD1677**: Controller phổ biến trên các lô đầu, hỗ trợ 4 cấp xám phần cứng, kích thước bộ đệm chuẩn.
2. **UltraChip UC8179**: Controller xuất hiện ở các lô trung kỳ.
3. **UltraChip UC8279_X4**: Biến thể chuyên biệt cho màn hình 800x480 mới nhất của Xteink.

### Quy trình nhận diện lúc khởi động (`src/main.cpp`):
```
[Khởi động nguồn] 
       │
       ▼
Gửi xung Reset (GPIO 14) & Đọc trạng thái chân BUSY (GPIO 6)
       │
       ▼
Gửi mã lệnh thăm dò SPI (Status / Rev Read)
       │
       ├──► Nếu nhận phản hồi SSD1677  ──► Nạp waveform & LUT của SSD1677
       │
       ├──► Nếu nhận phản hồi UC8179   ──► Cấu hình driver UC8179
       │
       └──► Mặc định/Khớp lệnh X4 Pro  ──► Khởi tạo UC8279_X4 (800x480 LUT tối ưu)
```

> **CẢNH BÁO QUAN TRỌNG:** Tuyệt đối không xóa bỏ cơ chế thăm dò runtime này. Nếu ép cứng firmware chạy driver SSD1677 trên panel UC8279, màn hình sẽ bị hiện tượng trắng xóa hoàn toàn và có thể bị lưu vết điện tích một chiều (DC bias), làm hỏng màng hiển thị vi hạt (Micro-capsules).
