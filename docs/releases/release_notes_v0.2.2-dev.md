### Bản cập nhật crossB OS v0.2.2-dev (Kênh Thử nghiệm Dev)

Phiên bản cập nhật dành cho Xteink X4 Pro trên kênh phát triển (Dev Channel) tập trung vào tối ưu hóa màn hình ngủ đồng hồ với cơ chế làm mới cục bộ siêu tiết kiệm pin, sửa triệt để lỗi nút bấm và thông báo tàng hình trên màn hình Cập nhật OTA, đồng thời chuẩn hóa toàn bộ giao diện cập nhật sang Tiếng Việt có dấu đầy đủ.

---

#### Điểm nổi bật & Cải tiến trong bản v0.2.2-dev:

1. **Làm Mới Cục Bộ Màn Hình Ngủ Đồng Hồ (Windowed Partial Refresh):**
   - **Cập nhật đồng hồ và pin không nhấp nháy toàn màn hình:** Thay vì kích hoạt chu kỳ quét điện thế toàn màn hình mỗi phút, hệ thống sử dụng cơ chế làm mới cửa sổ cục bộ (Windowed Partial Refresh qua `displayWindow`) chỉ áp dụng riêng cho vùng header (từ 0 đến 224 px) chứa đồng hồ số lớn, mức pin, thứ ngày tháng và lịch âm.
   - **Bảo toàn tĩnh tuyệt đối hai thẻ phía dưới:** Thẻ thông tin cuốn sách đang đọc (ảnh bìa, tiêu đề, tiến độ) và Thẻ thống kê đọc sách kèm lưới Heatmap lịch tháng hoàn toàn đứng yên, không bị phát xung điện thế e-ink.
   - **Tiết kiệm điện năng vượt trội:** Loại bỏ hoàn toàn các thao tác nạp lại ảnh bìa từ thẻ nhớ SD và tính toán lại dữ liệu thống kê mỗi phút khi máy đang ngủ, giúp kéo dài thời lượng pin đáng kể khi người dùng bật chế độ hiển thị đồng hồ liên tục.
   - **Làm mới toàn phần lúc nửa đêm (00:00):** Chỉ vào thời khắc 00:00 hàng ngày, hệ thống mới tự động kích hoạt một chu kỳ Full Refresh để chuyển ngày mới và cập nhật trạng thái ô lịch tháng sạch sẽ, triệt tiêu mọi bóng mờ tích tụ.

2. **Khắc Phục Triệt Để Lỗi Nút Bấm và Dòng Trạng Thái Tàng Hình trên Màn Hình OTA:**
   - **Sửa lỗi chữ trắng trên nền trắng trong các nút bấm:** Điều chỉnh tham số màu vẽ chữ trong hàm dựng nút bấm `drawSimpleButton` sang màu đen tương phản, giúp toàn bộ các nút chức năng ("CẬP NHẬT NGAY", "Đổi kênh", "Kiểm tra lại", "Quay lại") hiển thị chữ sắc nét, đậm và rõ ràng.
   - **Hiển thị rõ ràng trạng thái phiên bản mới:** Khắc phục lỗi ẩn chữ ở dòng thông báo trạng thái cập nhật ("Có bản cập nhật mới: ..." hoặc "Bạn đang ở phiên bản mới nhất.") và dòng tiêu đề hộp nhật ký thay đổi. Người dùng lập tức biết được tình trạng cập nhật của máy ngay khi vừa mở trang.
   - **Chuẩn hóa hộp thoại xác nhận cập nhật:** Hộp thoại hỏi người dùng xác nhận tải và cài đặt phiên bản mới hiển thị đầy đủ tiêu đề và nội dung rõ ràng, trực quan.

3. **Bản Địa Hóa Tiếng Việt Có Dấu Đầy Đủ cho Màn Hình Cập Nhật & Changelog:**
   - **Giao diện Cập nhật chuẩn Tiếng Việt:** Toàn bộ các chuỗi giao diện hiển thị trên màn hình Cập nhật OTA đã được chuyển sang Tiếng Việt có dấu đầy đủ, chuẩn chính tả (Phiên bản hiện tại, Kênh cập nhật, Phát triển, Ổn định, Có bản cập nhật mới, Bạn đang ở phiên bản mới nhất, Nhật ký thay đổi, Cuộn lên, Cuộn xuống).
   - **Nhật ký thay đổi (Changelog) có dấu:** Nội dung nhật ký thay đổi mặc định hiển thị tiếng Việt có dấu chuẩn xác, không còn hiện tượng hiển thị chữ không dấu.

---

#### Checksum SHA-256:
- `crossB-OS-v0.2.2-dev-x4pro.bin`: `fbfa305f5cd9719b198f4ff4d57e5e09e3f81168ba85d8a99e295198d6c1b1c2`
- `crossB-OS-v0.2.2-dev-x4pro-factory.bin`: `a8ab01b705e511e3d4fc87f6a1530bb4f5a14c5c63a1f0e5dba5abccd6d024e2`
