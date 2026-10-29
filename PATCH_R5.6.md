# VTMenu iOS — Bản sửa R5.6 (Cơ chế Đè / Giữ nút Nhảy mới Spam)

## 1. Yêu cầu mới
Theo yêu cầu: **"Làm lại cái spam nhảy khi ấn hoặc đè nút nhảy mới spam"**.
Trước đây, khi bật công tắc "Spam nhảy", nhân vật nhảy liên tục không ngừng ngay cả khi người chơi không chạm vào nút Nhảy, khiến nhân vật khó di chuyển và không thể đứng yên.

## 2. Giải pháp kỹ thuật trong R5.6
1. **Theo dõi tương tác nút Nhảy qua UIKit (`VTJumpGestureRecognizer`):**
   - Bổ sung `VTJumpGestureRecognizer` gắn vào Game View (`UnityView`).
   - Cài đặt `cancelsTouchesInView = NO`, `delaysTouchesBegan = NO`, `delaysTouchesEnded = NO` và `shouldRecognizeSimultaneouslyWithGestureRecognizer: return YES`. Điều này đảm bảo **100% không làm trễ hay ảnh hưởng** đến cơ chế nhận diện cảm ứng gốc của game.
   - Vùng nhận diện nút Nhảy: góc dưới bên phải màn hình (vùng chứa nút Nhảy ảo của Play Together).
   - Hỗ trợ đa điểm (Multi-touch): ngón tay cái trái điều khiển joystick di chuyển, ngón tay cái phải ấn/đè nút Nhảy để spam mà không bị xung đột.
2. **Cơ chế nhảy (`jumpTick`):**
   - **Khi thả nút / không chạm:** Không gửi bất kỳ lệnh nhảy nào (`jumps = 0`), nhân vật đi lại hoặc đứng yên hoàn toàn bình thường. Trạng thái menu hiển thị: `"Spam nhảy: BẬT • Đè nút Nhảy để spam"`.
   - **Khi ấn 1 lần (Tap):** Nhân vật nhảy một lần duy nhất như bình thường.
   - **Khi giữ / đè nút (Hold):** Tự động spam nhảy liên tục ở tốc độ cao (nhịp cài đặt, ví dụ 20ms) với cơ chế mở khóa quyền `_canJump` tạm thời từ R5 của Windows. Trạng thái menu hiển thị: `"Spam nhảy: ĐANG NHẢY • Giữ nút để tiếp tục"`.
   - **Khi chạm vạch đích (`IsGoalIn` / State 51..54):** Tự động hủy trạng thái giữ và ngừng nhảy ngay lập tức kể cả khi người chơi vẫn đang đè nút.
