### Bản cập nhật crossB OS v0.2.4-dev (Kênh Thử nghiệm Dev)

Phiên bản cập nhật dành cho Xteink X4 Pro trên kênh phát triển (Dev Channel) tập trung vào việc chuẩn hóa toàn diện cơ chế ngủ của hệ thống: áp dụng chế độ Light Sleep cho màn hình ngủ đồng hồ giúp giữ nguyên vẹn nội dung bộ đệm màn hình không bị xóa trắng nửa dưới và chuyển sang Deep Sleep tuyệt đối cho các màn hình ngủ còn lại để tiết kiệm pin tối đa; tách biệt hoàn toàn màn hình ngủ mặc định và các màn hình chờ không còn nhầm lẫn với đồng hồ; khắc phục lỗi khởi động lại khi chuyển cỡ chữ 14pt và hỗ trợ vuốt ngón tay xem trọn vẹn nhật ký thay đổi trong Cập nhật OTA.

---

#### Điểm nổi bật & Cải tiến trong bản v0.2.4-dev:

1. **Chuẩn Hóa Cơ Chế Ngủ - Light Sleep Cho Đồng Hồ & Deep Sleep Vĩnh Viễn Cho Các Màn Hình Khác:**
   - **Màn hình ngủ đồng hồ sử dụng Light Sleep:** 
     - Toàn bộ 512KB SRAM nội và bộ nhớ đệm khung hình (Framebuffer) được duy trì trạng thái 100% trong suốt chu kỳ ngủ.
     - Triệt tiêu hoàn toàn hiện tượng xóa trắng nửa dưới màn hình (phần thẻ sách đang đọc và lịch tháng) sau mỗi chu kỳ cập nhật phút.
     - Hệ thống chỉ thực hiện vẽ đè (partial refresh) khu vực hiển thị thời gian lên nội dung màn hình cũ mà không cần nạp lại tài nguyên hay khởi động lại toàn bộ vi điều khiển.
     - Thời gian thức và phản hồi tức thì khi bấm nút nguồn hoặc khi timer 60 giây kích hoạt.
   - **Deep Sleep vĩnh viễn cho tất cả màn hình ngủ ngoài đồng hồ:**
     - Các màn hình ngủ tĩnh (Bìa sách, Ảnh tùy chỉnh, Màn hình trắng, Giữ nguyên trang sách, v.v.) được đưa vào trạng thái Deep Sleep tuyệt đối, tắt hoàn toàn RTC timer.
     - Dòng tiêu thụ duy trì ở mức tối thiểu cấp nano-ampe, kéo dài thời lượng pin chờ hàng tháng cho thiết bị.

2. **Tách Biệt Hoàn Toàn & Chuẩn Hóa Các Loại Màn Hình Ngủ:**
   - **Tách riêng màn hình ngủ mặc định (Default Sleep):** Thiết kế lại giao diện nghỉ mặc định sang trọng mang nhận diện hệ điều hành crossB OS với logo đồ họa lớn, hiển thị thời lượng pin và thông điệp nghỉ ngơi thanh lịch, tách biệt hoàn toàn khỏi bố cục đồng hồ Neo-brutalist.
   - **Chuẩn hóa màn hình ngủ tùy chỉnh (Custom Sleep):** Trong trường hợp chưa có ảnh trong thư mục `/sleep` hoặc thẻ nhớ chưa nạp tệp `sleep.bmp`, hệ thống hiển thị màn hình hướng dẫn trực quan với khung viền nét đứt chỉ dẫn người dùng vị trí chép ảnh, không còn bị nhảy nhầm về màn hình đồng hồ.
   - **Chuẩn hóa màn hình bìa sách (Cover Sleep):** Khi chưa mở bất kỳ cuốn sách nào, hệ thống hiển thị giao diện thư viện nhắc nhở "Chưa có sách nào đang đọc" kèm biểu tượng sách, tách biệt hoàn toàn khỏi màn hình đồng hồ.
   - **Chuẩn hóa màn hình ngủ trong suốt (Transparent Custom):** Khi không tìm thấy tệp ảnh phủ, hệ thống hiển thị huy hiệu "ĐÃ KHÓA" nhỏ gọn trên nền trang sách hiện tại.

3. **Khắc Phục Lỗi Khởi Động Lại Khi Chọn Cỡ Chữ 14pt Trong Trình Đọc Sách:**
   - **Đồng bộ bảng kích thước font chữ chuẩn:** Cập nhật mảng cấu hình `BUILTIN_READER_FONT_SIZES = {12, 14, 16, 18}` đồng bộ hoàn toàn với danh sách điểm kích thước `BUILTIN_READER_POINT_SIZES` và các font chữ được nạp sẵn trong firmware, loại bỏ triệt để hiện tượng lệch chỉ số bộ nhớ làm sập hệ thống.
   - **Chủ động giải phóng bộ nhớ đệm font trước khi tải font 14pt:** Thu hồi cache font cũ trước khi nạp bộ giải nén của font 14pt, ngăn ngừa hiện tượng đột biến phân mảnh bộ nhớ RAM.
   - **Bảo toàn đồng bộ cài đặt hiển thị:** Duy trì trạng thái cài đặt xem trước trong suốt quá trình preview và khôi phục an toàn vị trí đọc kèm khóa luồng RenderLock khi đóng thanh cài đặt.

4. **Hỗ Trợ Vuốt Ngón Tay Cuộn Xem Toàn Bộ Nhật Ký Thay Đổi Trong Cập Nhật OTA:**
   - **Nhận diện cử chỉ vuốt cảm ứng trên X4 Pro:** Bổ sung xử lý cử chỉ vuốt màn hình cảm ứng (vuốt lên, vuốt xuống, vuốt sang trang) để duyệt toàn bộ nội dung changelog của các bản phát hành có phần mô tả dài mà không bị kẹt ở các dòng đầu tiên.
   - **Bước cuộn tự thích ứng:** Tự động tính toán số dòng hiển thị thực tế trên màn hình để cuộn mượt mà và trực quan.

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.4-dev-x4pro.bin`: `30cce66919892b2f16a846c59fcf5ca09e9a27219de32a3c7385414baaa3ac91`
- `crossB-OS-v0.2.4-dev-x4pro-factory.bin`: `614a990bf81445e8b99e9e171f98b4e7725d21dd85df13236f8f8f20c9577a34`
