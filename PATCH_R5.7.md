# VTMenu iOS — Bản sửa R5.7 (Anti-Capture, Draggable UI & Web Key System)

## 1. Tính năng mới bổ sung theo yêu cầu

### A. Ẩn menu khi quay màn hình hoặc chụp ảnh (Anti-Capture Protection)
- **Tự động ẩn khi quay màn hình:** Lắng nghe thông báo hệ thống `UIScreenCapturedDidChangeNotification`. Ngay khi người dùng bật tính năng Quay màn hình từ Trung tâm điều khiển (Control Center), AirPlay hoặc QuickTime, menu lập tức tàng hình (xóa sạch lớp vẽ Metal).
- **Chống chụp ảnh màn hình:** Sử dụng cơ chế vùng bảo vệ hiển thị an toàn của iOS giúp menu không xuất hiện trong ảnh chụp màn hình (Screenshot) hoặc video lưu lại.
- **Tùy chọn linh hoạt:** Trong mục "Chi tiết" có nút chuyển trạng thái `Ẩn khi quay/chụp: BẬT / TẮT` (mặc định BẬT) và lưu vào cấu hình.

### B. Kéo di chuyển Menu và nút mở Menu (Draggable UI)
- **Nút Menu nổi:** Khi thu gọn, nút `Menu` có thể được bấm giữ và kéo rê đến bất kỳ vị trí nào trên màn hình để không che tầm nhìn chơi game.
- **Bảng Menu chính:** Khi mở menu, bạn có thể bấm giữ thanh tiêu đề hoặc khoảng trống của bảng menu để kéo di chuyển đến bất kỳ góc nào trên màn hình.
- **Tự động giới hạn:** Vị trí kéo luôn được khóa trong vùng an toàn (Safe Area) của thiết bị, không bị che bởi tai thỏ (notch), Dynamic Island hoặc thanh Home Bar.
- **Tự lưu vị trí:** Vị trí nút và vị trí menu được tự động ghi nhớ vào `config.json` để giữ nguyên vị trí ở các lần vào game sau.

### C. Gắn hệ thống Key vào Web `45.131.65.244:30019`
- **Giao diện xác thực Key tích hợp:**
  - Khi chưa kích hoạt key: Menu hiển thị bảng nhập key dạng `KCT-XXXX-XXXX-XXXX-XXXX`.
  - Có nút **"Dán từ bộ nhớ"** để dán nhanh key vừa copy từ web.
  - Nút **"Kích hoạt Key"** gửi yêu cầu xác thực trực tiếp đến API `http://45.131.65.244:30019/api/v1/activate`.
  - Các tính năng hack (`ESP`, `Line`, `Spam nhảy`) bị khóa cho tới khi key được xác thực thành công.
- **Quản lý thiết bị & Token:**
  - Tự sinh `device_id` duy nhất từ `identifierForVendor` của thiết bị iOS (SHA-256 64 hex characters) theo đúng chuẩn của hệ thống Web Key.
  - Lưu `token` và thông tin bản quyền vào `config.json`.
  - Tự động duy trì và kiểm tra trạng thái token qua `http://45.131.65.244:30019/api/v1/check`.
  - Hiển thị rõ ràng: `"Key Hợp Lệ • Còn X ngày Y giờ"`.
  - Có nút **"Đổi key"** để dễ dàng nhập key mới khi cần.
