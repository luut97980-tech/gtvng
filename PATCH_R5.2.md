# VTMenu iOS — bản sửa R5.2

Bản này gộp hai việc: sửa crash khi đăng nhập (R5.1) và làm menu khớp hướng
ngang/dọc của game (R5.2). `menu_revision` vẫn là **5**, `tao_ipa_menu.py` không
cần sửa.

## 1. Triệu chứng crash

Tiêm vào PLAY TOGETHER VNG, menu hiện bình thường. Bấm **đăng nhập** trong game
thì app văng.

### Nguyên nhân trong source R5

1. `GameBackend::tick()` chạy **mỗi khung hình ngay từ màn hình đăng nhập**, kể cả
   khi ESP và Spam nhảy đều đang tắt. Mỗi tick gọi vào runtime IL2CPP:
   `ActorSystem.Self` → `get_MyActorCharacter` → `UnityEngine.Object.op_Implicit`
   → đọc `<SUID>`. Lúc bấm đăng nhập, Unity đang đổi scene: actor, camera,
   transform bị hủy dở. Đọc đối tượng nửa sống đúng khoảnh khắc đó là crash.
2. Nếu người dùng bật ESP trước khi đăng nhập, `espTick()` gọi `Camera.get_main`,
   `Transform.get_position`, `WorldToViewportPoint` từ **thread vẽ Metal của UIKit**
   trong khi Unity chạy player loop ở thread riêng. Scene đang hủy thì đây là race
   chắc chắn nổ.
3. `VTAttach()` chạy mỗi 0,5 giây và có thể **dựng lại controller** khi tìm thấy
   metal view khác, kể cả view tạm của SDK login. Dựng/hủy overlay giữa transition
   là một nguồn crash UIKit nữa.
4. Không có cửa chặn nào cho lỗi ở tầng scene: một lần đọc hỏng vẫn tick tiếp ngay
   khung sau vào scene đang dở.

### Đã sửa (R5.1, giữ nguyên trong R5.2)

- `runtimeRequested()`: tick chỉ chạm runtime khi ESP hoặc Spam nhảy đang bật.
  Hai công tắc tắt → không một lời gọi game nào, chỉ ghi trạng thái.
- Cờ `needsSettle`: mất nhân vật/camera (login, loading, hồi sinh) hoặc đổi actor
  → xóa GC handle, chờ 0,75 s rồi mới đọc lại.
- `GameBackend::suspend()`: gọi khi xoay máy, đổi layout, keyboard, app quay lại
  foreground. Xóa handle + hoãn runtime 0,75 s.
- Lỗi scene/camera: backoff 0,75 s thay vì bắn lại mỗi khung; ESP bọc try/catch
  từng actor.
- `tick()` bọc toàn bộ trong try/catch — không để exception C++ thoát ra vòng vẽ.
- `Plugin.mm`: `_resumeAfter` chặn gọi game 1 s sau khi app active trở lại.
- `VTFindGameView()` bắt buộc view phải nằm trong window; `VTAttach()` giữ nguyên
  overlay đang sống thay vì dựng lại giữa chừng.

## 2. Triệu chứng lệch hướng

Menu hiện nhưng không khớp giao diện ngang/dọc của game: bảng bị xoay, lệch vùng
an toàn, hoặc kích thước chỉ đúng ở một hướng.

### Nguyên nhân

R5/R5.1 gắn menu vào chính `UnityView`/`UnityRenderingView`. Unity iOS dựng view
theo hướng render rồi xoay hoặc gán transform để khớp màn hình; view con thừa
hưởng transform đó. Khi game ở hướng khác lúc gắn — hoặc đổi hướng sau đó — bảng
menu bị vẽ trong hệ tọa độ đã xoay, còn kích thước cứng 380×280 chỉ hợp một hướng.

### Đã sửa (R5.2)

- **Container mới:** menu là child view controller trong **root view của window
  game**, không phải view Unity. Đây là không gian giao diện chưa bị transform,
  luôn khớp hướng ngang/dọc hiện tại. View Unity chỉ còn dùng để xác nhận đúng
  window của game; window của SDK login bị bỏ qua. Nếu root view lại chính là
  surface game thì fallback sang chính window.
- **Bố cục động:** `draw()` chọn ngang/dọc từ `io.DisplaySize` trừ safe area.
  - Dọc: rộng tối đa 380 pt, cao 300 pt (mở Chi tiết: 440 pt).
  - Ngang: rộng tối đa 420 pt hoặc 55% bề ngang, cao 260 pt (Chi tiết: 330 pt).
  - Nút Menu thu gọn: 132×48 ngang, 100×56 dọc.
  - Bảng luôn nằm trọn trong safe area; lề lấy từ container.
- **Đổi hướng giữa chừng:** `drawInMTKView` so kích thước Metal view mỗi khung
  hình; khác thì hủy chạm đang giữ, xóa `windowMin/Max`, suspend backend 0,75 s.
  `viewDidLayoutSubviews` và `viewWillTransitionToSize` vẫn giữ nguyên tác dụng.
- **Chẩn đoán:** ghi rõ container, bounds, safe-area inset, hướng scene và
  transform; nhãn menu đổi thành `PTG | R5.2`.

## File đã đổi

- `src/Plugin.mm` (container root view, `VTHostWindow`, reset khi đổi kích thước,
  nhãn R5.2)
- `src/mobile_menu.cpp` (bố cục ngang/dọc, nhãn R5.2)
- `src/mobile_menu.hpp` (chẩn đoán mặc định R5.2)
- `src/game_backend.hpp`
- `src/game_backend.cpp`
- `tests/backend_api_test.cpp` (kỳ vọng cửa sổ settle sau khi đổi actor)
- `README.md`
- `PATCH_R5.2.md` (thay cho `PATCH_R5.1.md`)

## Build lại

Giữ nguyên quy trình cũ: GitHub Actions → artifact → `tao_ipa_menu.py` → ký bằng
ESign. `menu_revision` vẫn là 5 nên tool ghép IPA không cần sửa.

## Kiểm tra sau khi cài

1. Mở game, chưa bật gì, bấm đăng nhập → phải vào được, không văng.
2. Vào trong game, mở menu, bật ESP → khung hiện sau ~0,75 s.
3. Bật Spam nhảy → nhảy theo nhịp; tắt app rồi mở lại thì cả hai tự tắt.
4. Xoay máy hoặc vào màn hình game đổi hướng: bảng menu phải đứng đúng chiều,
   nằm trong vùng an toàn, nút Menu không bị notch/home indicator che. Chạm ngoài
   bảng vẫn điều khiển được game.
5. Nếu vẫn văng khi **chưa bật tính năng nào**: lúc đó code không còn gọi runtime,
   nguyên nhân nằm ở check toàn vẹn của game (scan dyld/hash), không phải menu.
