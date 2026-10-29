# Menu iOS R5.3 — chỉ ESP và Spam nhảy

Bản này có đúng **hai chức năng game**: ESP và Spam nhảy. Các chức năng khác
đã bị xóa khỏi cả giao diện, enum tính năng và backend; không còn hàng bị khóa.
Mục tiêu vẫn là PLAY TOGETHER **2.32.0, build 227628**, ARM64, iOS/iPadOS 15+,
cả nhánh VNG và nhánh quốc tế (Haegin).

## R6.0 — lớp chống phát hiện

`src/stealth.hpp` là lớp chống phát hiện ba đường, đi cả ba:

1. ghi `MethodInfo->methodPointer` — bắt delegate, reflection, `runtime_invoke`;
2. inline detour ARM64 (`ldr x16, #8` + `br x16` + địa chỉ stub) ghi thẳng 16 byte
   đầu thân hàm — bắt lệnh `bl` trực tiếp và gọi qua vtable, hai đường mà R5.9
   không đụng tới;
3. quét object sống mỗi 0,25 s (0,05 s ở nhịp kế nếu vừa có rác) — dọn hàng đợi
   báo cáo, hashset phát hiện, `CodeHashDic` và cờ của G-Presto.

48 điểm vào managed trên 18 class; arity quét `0..5` nên overload tự khớp, không
có bảng arity cứng để hết hạn sau mỗi bản patch. Watchdog 0,10 s ghi lại pointer
và detour nếu runtime ghi đè, rescan class mỗi 20 s.

### Cùng một bản cho VNG và quốc tế

Hai dump 2.32.0 (VNG và Haegin) đã được so **từng type**: 37.320 type, thân hàm
chuẩn hoá bỏ dòng `RVA` trước khi hash. Kết quả:

- Toàn bộ bề mặt chống gian lận bên dưới **giống hệt nhau** ở cả hai bản.
- `PID_CHEAT_HackDetection` vẫn là `19999`.
- `GPrestoDetectType` giống hệt, `WebClient.GPrestoCrossCheckResult(bool)` giống hệt.
- Khác biệt cấu trúc duy nhất mà lớp chống phát hiện có thể vướng: `ModuleSystem`
  bản VNG có thêm `_msgQueue` ở `0x248`, bản quốc tế không có, nên mọi field của
  `ModuleSystem` **phía trên** `0x248` thấp hơn 8 byte. Hai offset lớp này đụng vào
  là `0x40` và `0x48` — nằm dưới mốc đó.
- Bản quốc tế **không có** VNGSDK / PassSDK / `VNGIAPMessageCB` / `ModuleSystem.Update`;
  image `VNGGamesSDKAssembly.dll` chỉ có ở bản VNG. Không liên quan đến phát hiện.

Vì resolve theo **tên và slot vtable**, không theo layout bản build, `stealth.hpp`
không rẽ nhánh theo nhánh game. Chẩn đoán in thêm dòng `Client: vng` /
`Client: global`, dò bằng chính field `_msgQueue` — chỉ là nhãn, không phải điều
kiện rẽ nhánh.

Nhịp spam nhảy và Tự búa dùng **đúng** giá trị người dùng đặt: không jitter, không
nhịp nghỉ, không kẹp ngầm. Bảng offset, danh sách cố ý không vá, lý do không kèm
pacing và giới hạn đã biết: `PATCH_R6_STEALTH.md`.

## R6.1c — sửa lỗi văng khi mở Menu (`tryField` trên `ModuleSystem`)

R6.0–R6.1b truyền nhầm con trỏ instance `modSys` (`Obj`) thay vì `a.object_get_class(modSys)` (`Klass`) vào `tryField(modSys, "_msgQueue")` trong `sanitizeLiveAntiCheat()`. Khi mở menu, `il2cpp_class_get_field_from_name` đọc nhầm vùng nhớ instance của `ModuleSystem` như một `Il2CppClass*` và gây `SIGSEGV`. R6.1c sửa lại thành `tryField(a.object_get_class(modSys), "_msgQueue")`, đồng thời attach thread IL2CPP trước khi `stealth.arm()` chạy và bảo vệ trường hợp detour 16 byte nằm vắt qua ranh giới trang nhớ.

## R6.1b — nhật ký văng

Bản quốc tế văng khi mở menu. Không có `.ips` thì không chỉ được chỗ văng, nên
R6.1b gắn một vòng đệm dấu vết vào mọi bước không thể hoàn tác (ghi `methodPointer`,
ghi `__TEXT`, sửa header collection, quét assembly) và một handler `SIGSEGV / SIGBUS /
SIGILL / SIGFPE / SIGTRAP / SIGABRT`.

- Văng lần này → lần mở app sau hiện bảng **"VTMenu — lần chạy trước bị văng"** với
  đúng dòng cuối cùng trước cú văng: class nào, method nào, bước thứ mấy.
- File đầy đủ: `Documents/vtmenu_crash.txt` (sau khi xem đổi tên thành
  `vtmenu_crash_last.txt`).
- Handler chỉ `open` / `write` / `close` / `raise` — async-signal-safe, chạy trên
  stack phụ 64 KiB, và **trả `SIG_DFL` rồi `raise()` lại** nên iOS vẫn ghi `.ips`.
- Handler được **cài lại trong `VTAttach()`**: Unity đăng ký crash handler của nó
  sau constructor của dylib, nên nếu chỉ đặt một lần ở constructor thì nó sẽ bị đè.
- Không đổi hành vi: journal chỉ ghi, không nhánh nào đọc lại nó; chuỗi
  `Stealth: armed N stubs / M code detours` giữ nguyên định dạng.

Bảng tra dòng cuối → chỗ văng nằm ở `PATCH_R6_STEALTH.md` §10.5.

## R6.1a — `mach_vm.h` trên iPhoneOS

`<mach/mach_vm.h>` là `#error mach_vm.h unsupported.` trong SDK iPhoneOS; cả họ
`mach_vm_*` chỉ có trên macOS. `protectPage()` đổi sang `vm_protect()` khai báo
trong `<mach/vm_map.h>` (đã được `<mach/mach.h>` kéo vào). Trên mọi target 64-bit
`vm_address_t` chính là `mach_vm_address_t`, nên cùng trap, cùng `VM_PROT_COPY`,
cùng `KERN_PROTECTION_FAILURE` khi thiếu entitlement.


## R5.3 — nhận cả bundle `ptg.vng`

Cùng một build 2.32.0 (227628) phát hành dưới hai bundle id: bản App Store
`com.vng.playtogether` và bản VNG/repack `ptg.vng`. Hai gói có cùng framework
UUID (`1c1d8aea…39465d`), cùng `global-metadata.dat` (SHA-256 `ddefd9…cc5fb`)
và cùng RVA method. R5.3:

- `matchesExpected()` và `tao_ipa_menu.py` nhận **một trong hai** bundle id.
- Version 2.32.0, build 227628, framework UUID, metadata hash, chữ ký hàm và
  `menu_revision=5` vẫn kiểm tra y như cũ; bundle khác vẫn bị từ chối.
- Chẩn đoán in `bundle=com.vng.playtogether or ptg.vng` để thấy ngay gói đang cài.

Không có thay đổi nào ở phần ESP/Spam nhảy hay bố cục menu so với R5.2.

## R5.2 — menu khớp hướng ngang/dọc của game

R5.1 gắn menu vào chính `UnityView`. Unity iOS dựng view theo hướng render rồi
xoay/transform để khớp màn hình, nên view con thừa hưởng transform đó: bảng menu
bị xoay 90° hoặc lệch khỏi vùng an toàn khi game ở hướng khác lúc gắn. R5.2:

- **Gắn vào root view của window**, không phải Unity view. Đây là không gian giao
  diện chưa bị transform, luôn khớp hướng ngang/dọc hiện tại của game. Unity view
  chỉ còn dùng để xác nhận đúng window của game; window của SDK login bị bỏ qua.
- **Bố cục theo tỉ lệ sống:** ngang/dọc chọn từ kích thước interface trừ safe
  area. Dọc: bảng tối đa 380 pt (chi tiết 440). Ngang: rộng tối đa 420 pt hoặc
  55% bề ngang, cao 260/330 pt. Nút Menu thu gọn 132×48 ngang, 100×56 dọc. Bảng
  luôn nằm trong safe area, không đè notch hay home indicator.
- **Đổi hướng giữa chừng:** kích thước Metal view đổi → hủy chạm đang giữ, xóa
  windowMin/Max và suspend backend 0,75 s trước khi nhận chạm mới.
- Chẩn đoán ghi rõ container, bounds, safe-area inset, hướng scene và transform.

Toàn bộ sửa lỗi văng khi đăng nhập của R5.1 bên dưới được giữ nguyên.

## R5.1 — sửa lỗi văng game khi đăng nhập

R5 gọi vào runtime game **ngay từ màn hình đăng nhập**, mỗi khung hình, kể cả khi
chưa bật tính năng nào: `ActorSystem.Self`, `get_MyActorCharacter`, rồi
`UnityEngine.Object.op_Implicit` và đọc `<SUID>`. Lúc bấm đăng nhập, Unity đang
dựng/đổi scene, actor và camera đang bị hủy; đọc đối tượng nửa sống từ thread vẽ
của UIKit là chỗ app đi xuống. R5.1:

- **Không gọi runtime khi cả hai công tắc đang tắt.** Tick chỉ còn ghi trạng thái.
  Bật ESP/Spam nhảy mới chạm vào game, và chỉ sau khi nhân vật local tồn tại.
- **Giữ nhịp sau khi đổi cảnh:** mất nhân vật (login, loading, hồi sinh) đặt cờ
  chờ; nhân vật mới xuất hiện thì chờ thêm 0,75 s trước khi đọc camera/transform.
- **Suspend theo vòng đời:** xoay máy, đổi layout, keyboard, app quay lại foreground
  đều xóa GC handle và hoãn gọi game 0,75–1 s.
- **Lỗi ở tầng scene/camera** (không phải lỗi riêng của Spam nhảy) → xóa toàn bộ
  handle, nghỉ 0,75 s rồi mới thử lại, không bắn liên tục vào scene đang hủy.
- **ESP chịu lỗi từng actor:** một actor đã hủy không làm hỏng cả khung hình.
- **Overlay không dựng lại giữa chừng:** timer 0,5 s chỉ gắn lại khi container
  hoặc window đã đổi; không bám vào view tạm của SDK login. (R5.2 chuyển hẳn
  overlay ra root view của window, xem mục trên.)
- `GameBackend::tick` bọc toàn bộ trong try/catch, không để exception C++ thoát ra
  vòng vẽ Metal.

Gói là mã nguồn thử nghiệm. Chưa có dylib/IPA đã build trong gói. Chưa có Xcode
ở môi trường tạo source nên chưa xác nhận compile native, ký hoặc chạy iPhone.

## Sửa nhận diện app R5

R3 so sánh trực tiếp ba trường trong `NSBundle.mainBundle.infoDictionary` một lần,
chỉ hiện lỗi chung và không thử lại nếu UnityFramework chưa tải. `Info.plist` đã
cung cấp có cả ba trường khớp, nên dữ liệu này chưa xác định nguyên nhân runtime.

R5 ưu tiên đọc `Info.plist` thật cạnh executable đang chạy, đối chiếu thêm bản đọc
từ NSBundle để chẩn đoán. Chỉ fallback sang NSBundle khi không đọc được plist trên
đĩa; nếu plist đọc được nhưng sai bản thì vẫn từ chối. Build dạng NSNumber được
chuyển sang chuỗi và bỏ khoảng trắng ngoài chuỗi trước khi so sánh.

Nút **Sao chép chẩn đoán** hiện Bundle ID/version/build của cả hai nguồn, nguồn
được chọn, UUID thực/đích và RVA nếu một chữ ký hàm sai. Tiêu đề R5 và chẩn đoán ghi
rõ phiên bản mục tiêu; không khẳng định đó là phiên bản runtime vừa đọc.

Khi metadata hoặc UnityFramework chưa có, kiểm tra được thử lại tối đa mỗi giây
khi renderer đang hoạt động. Lỗi phiên bản/UUID/chữ ký hàm thì dừng backend, không
bật tính năng. Bấm Sao chép chẩn đoán có thể yêu cầu kiểm tra lại. Không bỏ kiểm
tra UUID hay bảy chữ ký hàm.

Đã thêm liên kết **CoreGraphics** để giữ sửa lỗi `_CGRectZero` khi build.
Patcher và build manifest yêu cầu đúng R5, tránh ghép nhầm dylib R4.

### Cập nhật trên Windows / GitHub

1. Thay nội dung repo bằng nguồn R5 này, gồm thư mục `.github/workflows`.
2. Actions → **Build iOS ESP Spam Menu** → Run workflow trên nhánh main.
3. Tải artifact **VTMenu-iOS-ESP-Spam-R5** của lượt mới đã thành công.
4. Giải nén, chạy `py tao_ipa_menu.py`; chọn IPA gốc và dylib R5.
5. Dùng ESign ký file IPA chưa ký mới tạo rồi cài cập nhật. Không dùng lại IPA
   ghép bằng dylib R4. Nếu còn khóa backend, gửi nội dung Sao chép chẩn đoán của R5.

Chưa có native build hoặc kết quả chạy thiết bị của R5.3 trong môi trường hiện tại.
R3 đã build bằng GitHub Actions trong phiên trước; kết quả đó không xác nhận R5.3.

Tài liệu Apple đã đối chiếu:
- https://developer.apple.com/documentation/foundation/bundle/infodictionary
- https://developer.apple.com/library/archive/documentation/CoreFoundation/Conceptual/CFBundles/AccessingaBundlesContents/AccessingaBundlesContents.html

## ESP

- Khung 2D, UID, khoảng cách; đọc actor khác và camera qua API IL2CPP.
- Cập nhật 10 Hz, tối đa 128 actor, chiều cao khung xấp xỉ 1,65 m.
- Mất nhân vật/chuyển owner/lỗi đọc thì xóa khung thay vì giữ ảnh cũ.
- ESP độc lập với controller nhảy, không đòi controller thuộc 9 loại cũ.

## Spam nhảy

- Bật nút để lặp yêu cầu **OnJump()** trên controller local thực tế của game.
- Lấy method từ metadata của lớp controller hiện tại, có kiểm tra ActorControl,
  liên kết actorBase và cờ _terminate. Dùng runtime_invoke, không gọi offset x64.
- Thanh nhịp 0–1000 ms, mặc định 20 ms. **0 ms nghĩa là tối đa một yêu cầu mỗi
  khung hình**, không phải vòng lặp vô hạn hay bảo đảm số lần nhân vật nhảy.
- Không bù hàng loạt lệnh sau khi đứng khung hình/đổi đồng hồ. Gửi lại khi tới
  nhịp tiếp theo; exception managed có khoảng chờ thử lại.
- Mất nhân vật hoặc controller dừng: chờ; hồi sinh/đổi controller thì lấy owner
  mới và tiếp tục nếu công tắc vẫn bật. Không dùng lại con trỏ cũ.
- Đây là input nhảy gốc. Game vẫn quyết định lúc nào được nhảy, bao gồm cooldown,
  trạng thái sống/về đích; không sửa quyền nhảy, lực nhảy, cờ nhảy đôi hay ready.
- Chẩn đoán đếm **yêu cầu gửi thành công**, không giả là số lần nhảy thành công.

## Giao diện đơn giản và hướng hiển thị R5.2

- Bảng chính tối giản, chữ nằm ngang: ESP, Spam nhảy, nhịp nhảy, Lưu, Tắt hết,
  Chi tiết và Ẩn. Khi thu gọn chỉ có nút Menu.
- Bỏ FPS, UID và mô tả kỹ thuật khỏi mặt chính. Chi tiết mở phần trạng thái,
  UID và nút Sao chép chẩn đoán; phần dài có thể cuộn bằng kéo ngón tay.
- Menu là child view controller đặt trong **root view của window game**, nằm trên
  Unity view nhưng không nằm trong transform xoay của nó. Nhờ vậy bảng luôn đứng
  theo hướng ngang/dọc thật của giao diện, kể cả khi Unity xoay view nội bộ.
- Bố cục tính lại mỗi khung hình từ kích thước Metal view: dọc dùng bảng 380 pt,
  ngang dùng tối đa 420 pt hoặc 55% bề ngang; chiều cao và nút thu gọn cũng đổi
  theo hướng. Safe area của chính container quyết định lề trên/dưới/trái/phải.
- Khung vẽ, safe area và tọa độ UITouch đều lấy từ cùng Metal view. Chạm ngoài
  menu trả lại game. Khi đổi kích thước/xoay, hủy chạm cũ trước khi nhận chạm mới.
- Kiểm tra host mỗi 0.5 giây khi foreground, nhưng **chỉ gắn lại khi container
  hoặc window đổi**; cùng host thì không hủy chạm hay reset input đang kéo. Khi
  app xuống nền vẫn tắt cả hai tính năng và ẩn bảng.
- Chẩn đoán R5.2 có thêm tên container, kích thước, safe-area inset, hướng scene,
  transform và cổng runtime (`idle` = chưa gọi game lần nào).

Ảnh `preview/menu_simple.png` được dựng từ draw data thật của ImGui với dữ liệu
mẫu; không phải ảnh chụp chạy trong game hay bằng chứng xoay UIKit trên iPhone.

Config vẫn ở Application Support/VTMenu/config.json, schema 3; lưu nhịp và style,
không tự bật lại chức năng. Các sửa metadata/UUID/signature/CoreGraphics của R4
được giữ. Chỉ giao diện, cách gắn view và nhãn phiên bản được cập nhật ở R5.2.

Tài liệu UIKit đã đối chiếu:
- https://developer.apple.com/library/archive/featuredarticles/ViewControllerPGforiPhoneOS/ImplementingaContainerViewController.html
- https://developer.apple.com/documentation/uikit/uicoordinatespace
- https://developer.apple.com/documentation/uikit/uiview/hittest(_:with:)

Chưa build native hoặc thử xoay/cảm ứng UIKit trên thiết bị trong môi trường này.

## Build/cài

1. Có Mac/Xcode: chạy `bash build_dylib.sh`.
2. Chỉ có Windows: làm theo **HUONG_DAN_WINDOWS.md**, chạy workflow
   **Build iOS ESP Spam Menu** trên GitHub Actions macOS. Chưa chạy workflow ở đây.
3. Artifact phải là **VTMenu-iOS-ESP-Spam-R5**, gồm VTMenu.dylib và menu-build.json.
4. Chạy tao_ipa_menu.py trên máy bạn, chọn IPA gốc và dylib. IPA 2 GB không cần
   tải lên cuộc trò chuyện. Tool từ chối manifest R4 hoặc dylib sai hash.
5. Đầu ra IPA chưa ký; cần ký lại app/framework bằng công cụ và profile hợp lệ
   rồi cài trên thiết bị. Giữ bundle ID gốc (com.vng.playtogether hoặc ptg.vng); bản game vẫn phải đúng framework đã xác minh.

Patcher giữ nguyên IPA gốc, kiểm tra version/UUID/metadata hash và header padding.
Không giải mã IPA, không bỏ qua chữ ký Apple. Gói không có web key iOS hoặc ẩn
khi quay/chụp màn hình. Không kèm binary game, SDK, khóa ký hoặc tài khoản Apple.

## Kiểm chứng

- UI C++/ImGui: đúng hai công tắc; click/cancel, vùng nhận chạm, bố cục phone/iPad
  và cả ngang/dọc (safe area, chiều rộng/cao theo hướng), slider hợp lệ; không còn
  mục game cũ.
- Backend dùng IL2CPP giả: giới hạn nhịp, 0 ms không lặp vô hạn, không catch-up,
  bật/tắt, chết/hồi sinh, đổi controller, termination, retry, ESP độc lập, GC handle.
- Metadata C++: dữ liệu khớp, lệch cache, fallback, từ chối sai phiên bản/build và báo giá trị thực.
- Foundation trên macOS: test đọc XML/binary plist và NSNumber trong workflow; chưa chạy ở môi trường Linux này.
- 14 test Python: kiểm tra ghép IPA, hash/version/UUID, mã hóa, padding, bảo toàn
  code/assets, trùng injection, manifest R4 bị từ chối, không ghi đè file nguồn,
  và chấp nhận bundle `ptg.vng` bên cạnh `com.vng.playtogether`.
- Kiểm tra dump/byte mã ARM64 và các API export của framework đã gửi.

```sh
cmake -S . -B build-core -DVT_UI_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-core --parallel 2
ctest --test-dir build-core --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
```

Xem VALIDATION.json và analysis/ios_build.json. Kiểm tra offline không thay thế
thử UIKit/Metal/gameplay trên thiết bị. Chưa hứa ESP/spam nhảy hoạt động trong game.

ImGui MIT: third_party/imgui/LICENSE.txt. Lumin: third_party/lumin/NOTICE.md.
Font Noto Sans: Resources/FONT_LICENSE.txt.
