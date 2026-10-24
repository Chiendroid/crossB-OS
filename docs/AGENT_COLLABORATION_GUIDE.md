# Hướng Dẫn Dành Cho AI Agent & Lập Trình Viên Tham Gia Dự Án CrossPoint Reader (Xteink X4 Pro)

Tài liệu này cung cấp toàn bộ bối cảnh kiến trúc, hiện trạng mã nguồn, các đặc thù phần cứng của **Xteink X4 Pro**, danh sách tính năng đã triển khai, và các nguyên tắc bắt buộc khi tham gia phát triển.

---

## 1. Tổng Quan Dự Án & Mục Tiêu

* **Tên dự án:** CrossPoint Reader (Nhánh `develop`).
* **Mục tiêu:** Hệ điều hành/firmware mã nguồn mở tối ưu cao dành cho máy đọc sách E-ink nhỏ gọn, trọng tâm là dòng thiết bị **Xteink X4 Pro** (bên cạnh X3, X4 Classic và hệ sinh thái FreeInk).
* **Ngăn xếp công nghệ (Tech Stack):**
  * Ngôn ngữ: **C++20** (`-std=c++2a`), không dùng C++ exceptions (`-fno-exceptions`), không dùng RTTI (`-fno-rtti`).
  * Nền tảng: ESP-IDF / Arduino core trên vi điều khiển **ESP32-S3** (cho X4 Pro) và ESP32-C3 (cho X3/X4).
  * Công cụ build: **PlatformIO**.
  * SDK phần cứng: [FreeInk SDK](file:///Users/chien/workspaces/xteink%20x4%20pro/freeink-sdk).

---

## 2. Kiến Trúc Hệ Thống (System Architecture)

Dự án được phân tầng nghiêm ngặt nhằm đảm bảo tính ổn định và tính đa nền tảng:

```
+-------------------------------------------------------+
|  UI & Activities Layer (src/activities/, components/) |
|  (Home, Reader, Chess, Calendar, Settings, Drawer...) |
+-------------------------------------------------------+
                           |
                           v
+-------------------------------------------------------+
|          Core Logic & Utils (src/util/, lib/)         |
|  (VietnameseTelex, LunarCalendar, I18N, ReadingStats) |
+-------------------------------------------------------+
                           |
                           v
+-------------------------------------------------------+
|             CrossPoint HAL (lib/hal/)                 |
|  (HalDisplay, HalFrontlight, HalPowerManager, Storage)|
+-------------------------------------------------------+
                           |
                           v
+-------------------------------------------------------+
|            FreeInk SDK (freeink-sdk/libs/)            |
|       (BoardConfig, EInkDisplay, Hardware drivers)    |
+-------------------------------------------------------+
```

### Nguyên Tắc HAL Bắt Buộc:
1. **Không gọi trực tiếp FreeInk SDK hoặc SdFat ở tầng UI**: Mọi thao tác bộ nhớ, màn hình, nguồn, đồng hồ, lưu trữ và đèn nền bắt buộc thông qua các lớp wrapper trong `lib/hal/`:
   * Màn hình: [`HalDisplay`](file:///Users/chien/workspaces/xteink%20x4%20pro/lib/hal/HalDisplay.h)
   * Nguồn & Ngủ sâu: [`HalPowerManager`](file:///Users/chien/workspaces/xteink%20x4%20pro/lib/hal/HalPowerManager.h)
   * Đèn nền: [`HalFrontlight`](file:///Users/chien/workspaces/xteink%20x4%20pro/lib/hal/HalFrontlight.h)
   * Đọc ghi file: [`HalStorage`](file:///Users/chien/workspaces/xteink%20x4%20pro/lib/hal/HalStorage.h) và wrapper [`HalFile`](file:///Users/chien/workspaces/xteink%20x4%20pro/lib/hal/HalStorage.h).
2. **An toàn luồng SPI & Mutex Storage**:
   * Tuyệt đối không dùng trực tiếp các con trỏ SdFat (`FsFile`, `SdFat`, `SdSpiCard`) ngoài HAL.
   * Mọi thao tác tệp phải giữ `storageMutex` thông qua `HalFile` để tránh xung đột trên bus SPI với màn hình E-ink gây ra lỗi FreeRTOS `xTaskPriorityDisinherit`.

---

## 3. Đặc Thù Phần Cứng Xteink X4 Pro & Các "Bẫy" Cần Tránh

Khi làm việc với Xteink X4 Pro, Agent phải nắm vững các chi tiết sau:

### 3.1. Sơ đồ Chân Phần Cứng (Hardware Pinout)
* **ESP32-S3**: 16MB Flash, 8MB Octal PSRAM.
* **Màn hình E-ink (SPI Bus)**:
  * `SCLK = GPIO 12`
  * `MOSI = GPIO 11`
  * `CS = GPIO 13`
  * `DC = GPIO 18`
  * `RST = GPIO 14`
  * `BUSY = GPIO 6`
* **Đèn nền kép (Dual Frontlight - Warm / Cold)**:
  * `Cold Light (Trắng lạnh) = GPIO 8`
  * `Warm Light (Vàng ấm) = GPIO 9`
  * Tần số PWM: **25 kHz** (tránh rít cuộn cảm boost converter và chống chớp).
* **Quản lý Nguồn**:
  * `Master Peripheral Latch = GPIO 1`
* **Màn hình cảm ứng (Touch Panel GT911)**:
  * `SDA = GPIO 4`, `SCL = GPIO 5`, `INT = GPIO 7`, `RST = GPIO 10`.

### 3.2. Cơ Chế Nhận Diện Tự Động 3 Lô Controller Màn Hình (Hardware Probing)
Nhà sản xuất Xteink xuất xưởng X4 Pro qua nhiều lô phần cứng khác nhau với 3 loại controller E-ink:
1. `SSD1677` (Lô đầu)
2. `UC8179` (Lô giữa)
3. `UC8279_X4` (Lô mới, độ phân giải 800x480)

* **Quy tắc an toàn**: Không bao giờ hardcode duy nhất một loại driver màn hình. Tại [`src/main.cpp`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/main.cpp), hàm `freeink::applyXteinkDisplayController()` sẽ tự động probe chân BUSY và SPI ID để switch sang controller tương ứng lúc boot, ngăn chặn triệt để tình trạng trắng màn hoặc cháy phân cực DC bias trên màng E-ink.

### 3.3. Cơ Chế Chống Rò Điện & Cháy Đèn Khi Ngủ Sâu (Deep Sleep Leakage Fix)
* **Vấn đề lịch sử**: Issue GitHub `#2857` và PR `#3215` chỉ ra lỗi rò dòng nghiêm trọng khi sleep do chân PWM GPIO8/9 bị thả nổi (floating gate) và rail nguồn ngoại vi `GPIO 1` được giữ HIGH để fast-wake.
* **Giải pháp đã áp dụng**:
  * Trước khi vào Deep Sleep, [`HalPowerManager::startDeepSleep()`](file:///Users/chien/workspaces/xteink%20x4%20pro/lib/hal/HalPowerManager.cpp) tắt đèn nền qua `Frontlight.setOn(false)`, ép chân `GPIO 8` và `GPIO 9` về mức LOW cố định và gọi `gpio_hold_en()`.
  * Khi thức dậy, [`holdPowerRails()`](file:///Users/chien/workspaces/xteink%20x4%20pro/freeink-sdk/libs/hardware/BoardConfig/include/BoardConfig.h) gọi `gpio_hold_dis()` để giải phóng pad hold trước khi gắn kênh LEDC PWM trở lại.

---

## 4. Các Tính Năng Đã Hoàn Thiện & Cấu Trúc Mã Nguồn

| Tính năng | Đường dẫn file chính | Mô tả |
| :--- | :--- | :--- |
| **Ứng dụng Cờ vua (Chess)** | [`src/activities/chess/`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/activities/chess/) | Game cờ vua tích hợp AI Micro-Max, hỗ trợ lưu/load ván đấu, vẽ quân cờ E-ink độ nét cao. |
| **Lịch & Lịch Âm (Calendar)** | [`src/activities/calendar/`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/activities/calendar/), [`LunarCalendar.cpp`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/util/LunarCalendar.cpp) | Lịch tháng, hỗ trợ tính toán thuật toán âm lịch Việt Nam chính xác, hiển thị ngày hoàng đạo/sự kiện. |
| **Bộ gõ Tiếng Việt Telex** | [`src/util/VietnameseTelex.cpp`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/util/VietnameseTelex.cpp) | Xử lý bỏ dấu tiếng Việt chuẩn trên bàn phím ảo E-ink. |
| **Trình đọc Web (Web Reader)** | [`src/activities/browser/`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/activities/browser/) | Tải bài viết qua HTTP và chuyển đổi DOM sang định dạng trang đọc E-ink thân thiện. |
| **Truyền sách P2P** | [`src/activities/network/NearbyBookTransferActivity.cpp`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/activities/network/NearbyBookTransferActivity.cpp) | Nhận sách trực tiếp qua mạng nội bộ hoặc hotspot. |
| **Đo lường & Thống kê Đọc** | [`src/activities/reader/BookReadingStats.cpp`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/activities/reader/BookReadingStats.cpp) | Theo dõi thời gian đọc, tốc độ đọc trang/phút và phiên đọc. |
| **Bảng điều khiển đèn & Icon pin** | [`battery-indicator.h`](file:///Users/chien/workspaces/xteink%20x4%20pro/freeink-sdk/libs/ui/FreeInkUI/include/components/bars/battery-indicator.h), [`FrontlightPanelActivity.cpp`](file:///Users/chien/workspaces/xteink%20x4%20pro/src/activities/util/FrontlightPanelActivity.cpp) | Vuốt từ mép trên xuống để mở bảng chỉnh đèn ấm/lạnh; Icon pin bo tròn thẩm mỹ cao. |

---

## 5. Quy Trình Biên Dịch, Kiểm Thử & Chạy Giả Lập

Agent tham gia phải luôn kiểm tra mã nguồn bằng các lệnh sau:

### 5.1. Biên dịch Firmware thật cho X4 Pro
```bash
pio run -e x4pro
```
* Tạo ra các file nhị phân: `.pio/build/x4pro/firmware.bin` và `firmware.factory.bin`.
* Bộ nhớ RAM và Flash phải nằm trong ngưỡng cho phép (16MB Flash, 8MB PSRAM).

### 5.2. Chạy Bộ Kiểm Thử Tự Động (Host Unit Tests - GTest)
```bash
cmake -S test -B build/test
cmake --build build/test
ctest --test-dir build/test --output-on-failure -j
```
* Toàn bộ các suite kiểm thử gtest bao gồm kiểm tra font, parser XML/HTML, giải mã hình ảnh, xử lý tệp, và logic định dạng trang phải luôn vượt qua.

### 5.3. Khởi Chạy Giả Lập Desktop (Simulator)
```bash
pio run -e simulator_x4_pro
./.pio/build/simulator_x4_pro/program
```
* Cho phép kiểm tra giao diện trực quan qua cửa sổ SDL trên máy tính mà không cần nạp vào máy thật.

---

## 6. Quy Tắc Ứng Xử Dành Cho Agent (Agent Collaboration Rules)

1. **Tuân thủ Quy định Git & Review**:
   * Mọi công việc tích hợp đều thực hiện trên nhánh `develop`.
   * **Không bao giờ tự ý chạy `git push`** hoặc tạo/đóng Pull Request nếu không có yêu cầu trực tiếp từ người dùng.
   * Tạo commit cục bộ phải có sự xác nhận của người dùng.
2. **Bảo tồn tài liệu và chú thích (Documentation Integrity)**:
   * Giữ nguyên các comment và docstring hiện có không thuộc phạm vi chỉnh sửa.
3. **Phong cách mã nguồn (Coding Standards)**:
   * Luôn dùng hàm ngắn gọn (< 20 dòng nếu có thể).
   * Dùng `LOG_INF`, `LOG_DBG`, `LOG_ERR` từ [`Logging.h`](file:///Users/chien/workspaces/xteink%20x4%20pro/lib/hal/Logging.h), không dùng `printf` hay `Serial.print`.
   * Xử lý an toàn biến rỗng / null safety, không dùng con trỏ sống tự do khi có singleton hoặc smart lifetime management.
4. **Ngôn ngữ giao tiếp**:
   * Tất cả giải thích và trao đổi với người dùng **bắt buộc viết bằng Tiếng Việt**.
