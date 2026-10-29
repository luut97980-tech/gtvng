# VTMenu iOS — Bản sửa R5.5 (Fix lỗi ESP Minigame & Sảnh)

## 1. Hiện tượng ghi nhận
Từ bảng chẩn đoán người dùng:
```
Status: ESP/Line: chờ vào mini-game (120015, 120026, 120006, 120018, 120041, RealBoard)
Controller: 
MiniGame: none
ESP markers: 0 | Line segments: 0
```
- Khi bật ESP ở ngoài sảnh (Plaza) hoặc trong game: Màn hình không hiện gì do `ESP markers: 0`.
- Dòng `Controller:` bị trống.
- `MiniGame: none` kể cả khi người dùng đang chờ minigame.

## 2. Nguyên nhân gốc rễ
1. **Bộ lọc `findMiniGame()` bị chặn bởi lớp ảo `MiniGameBase`:**
   Mã nguồn R5.4 yêu cầu `inheritsFrom(cand, "MiniGameBase")`. Tuy nhiên trong IL2CPP của Play Together VNG, các map minigame kế thừa trực tiếp từ `MiniGameKMG` và `MiniGame` (không có lớp nào tên `MiniGameBase`), do đó hàm tìm minigame luôn trả về `nullptr`.
2. **Hàm `FindObjectOfType` phụ thuộc vào `il2cpp_type_get_object`:**
   Một số build `UnityFramework` trên iOS không export hàm nội bộ `il2cpp_type_get_object`, dẫn tới việc gọi `systemTypeFor()` bị trả về `nullptr` và không gọi được `FindObjectOfType`.
3. **Thiếu cơ chế ESP ở sảnh:**
   R5.4 thay thế toàn bộ ESP nhân vật bằng ESP Minigame. Khi người chơi ở ngoài sảnh (Plaza / Home / School), hệ thống xóa sạch marker (`markers: 0`) khiến người dùng tưởng ESP bị lỗi/hỏng.
4. **`controllerName` chỉ được cập nhật khi bật Spam nhảy:**
   Biến `controllerName` trước đây chỉ được gán trong `currentControl` khi `Spam nhảy` hoạt động, khiến mục `Controller:` trong chẩn đoán bị trống khi chỉ bật ESP.

## 3. Các thay đổi trong R5.5
1. **Phát hiện MiniGame đa tầng (4 cơ chế độc lập):**
   - **Tầng 1 (Actor Control):** Quét con trỏ từ `ActorKMGPlayer` của nhân vật.
   - **Tầng 2 (Static Fields):** Quét vùng nhớ tĩnh `static_fields` (+0xB8) của toàn bộ 17 lớp minigame (`MiniGameKMG`, `MiniGame`, `MiniGameKMGTwistRoad`, `MiniGameKMGFogMaze`, `MiniGameKMGRealBoard`, `MiniGameKMGDuel`, ...).
   - **Tầng 3 (Type.GetType Reflection):** Sử dụng `System.Type.GetType` thuần managed IL2CPP kết hợp `UnityEngine.Object.FindObjectOfType` mà không cần `il2cpp_type_get_object`.
   - **Tầng 4 (GameObject Hierarchy):** Tìm kiếm thông qua các GameObject gốc như `"MapRoot"`, `"RandomMaze_Root"`.
2. **Hỗ trợ ESP Sảnh tự động chuyển đổi sang ESP Minigame:**
   - Khi ở ngoài sảnh (`gameObj == nullptr`): ESP tự động kích hoạt `collectPlazaPlayers`, hiển thị vị trí, tên và khoảng cách của người chơi xung quanh từ `ActorSystem.get_OtherActorCharacter()`.
   - Khi bước vào bất kỳ minigame nào: Tự động chuyển mượt mà sang ESP Minigame (vòng tròn 12 điểm, `YES`/`NO`, `T1..T7`, `H1..H3`, `ĐÍCH`) và đường dẫn Line.
3. **Cập nhật `Controller:` liên tục:**
   - Mỗi tick đều làm mới trạng thái controller, phản ánh chính xác `ActorDefaultControlPlayer` (ở sảnh) hoặc `ActorKMGPlayer` (trong minigame).
