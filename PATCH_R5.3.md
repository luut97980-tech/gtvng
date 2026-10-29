# VTMenu iOS — bản sửa R5.3 (Fix Crash & Fix Lỗi Đăng Nhập)

## 1. Triệu chứng ban đầu (Bundle ID `ptg.vng`)

Gói game thực tế trên máy là `Payload/PLAYTOGETHERVNG.app` với bundle id
`ptg.vng` (bản VNG/repack), không phải `com.vng.playtogether`. Vì vậy:

- `tao_ipa_menu.py` dừng ở bước kiểm tra Info.plist: “Chi nhan PLAY TOGETHER VNG
  2.32.0 (227628)”.
- Nếu tiêm tay, guard trong game cũng từ chối metadata và backend không kết nối.

## 2. Triệu chứng văng game khi mở (`UIViewControllerHierarchyInconsistency`)

- Mở game khoảng 3,5 giây thì văng (`SIGABRT`). Crash log `.ips` báo ngoại lệ
  `UIViewControllerHierarchyInconsistency` tại `VTAttach()` khi gọi `addSubview:`.
- Nguyên nhân: Bản R5.2 gọi `[parent addChildViewController:VTController]` nhưng lại
  gắn `VTController.view` vào `UIWindow` (`container = game`), vi phạm kiểm tra
  phân cấp của UIKit.

## 3. Triệu chứng lỗi đăng nhập (“Đã xảy ra lỗi tạm thời khi đăng nhập”)

Khi bấm đăng nhập vào tài khoản, màn hình báo **“Đang kiểm tra danh tính.”** rồi hiện
thông báo **“Đã xảy ra lỗi tạm thời khi đăng nhập. Vui lòng thử lại sau.”** và bắt
đăng nhập lại liên tục.

### Nguyên nhân gốc rễ

1. **Lệch Bundle ID khi xác thực server (`ptg.vng` vs `com.vng.playtogether`):**
   IPA repack đã bị đổi `CFBundleIdentifier` trong `Info.plist` thành `ptg.vng`.
   Khi `PassSDK.framework`, `FBSDKLoginKit`, `GoogleSignIn`, `Firebase` và
   `UnityFramework` (`Application.identifier`) gửi gói tin xác thực danh tính lên
   máy chủ VNG/Haegin ở bước *“Đang kiểm tra danh tính.”*, máy chủ đối chiếu thấy
   Bundle ID là `ptg.vng` thay vì `com.vng.playtogether` nên từ chối phiên đăng nhập.
2. **Xung đột cửa sổ với SDK đăng nhập (`PassSDK`):**
   Việc tạo thêm một `UIWindow` phụ (`VTPassthroughWindow` ở mức `UIWindowLevelAlert`)
   khiến `PassSDK` / `ASWebAuthenticationSession` lấy nhầm cửa sổ trên cùng làm
   `presentationAnchor` hoặc làm mất trạng thái key window khi đóng popup đăng nhập.
3. **Quét `UnityFramework` quá sớm ngay màn hình đăng nhập:**
   `tryStartBackend:` chạy ngay từ khung hình đầu tiên lúc game đang kiểm tra danh tính.

### Giải pháp đã áp dụng trong `src/Plugin.mm`

1. **Spoof Bundle ID về `com.vng.playtogether` trong bộ nhớ (Runtime Bundle Spoof):**
   Ngay tại hàm khởi tạo `__attribute__((constructor))` (trước khi `main()`,
   `PassSDK`, `FBSDK` và `UnityFramework` chạy), `VTSpoofBundleIdentifier()` tự động
   swizzle `NSBundle` (`bundleIdentifier`, `objectForInfoDictionaryKey:`,
   `infoDictionary`) và cập nhật `CFBundleGetInfoDictionary` của `mainBundle` để luôn
   trả về `@"com.vng.playtogether"`. File `Info.plist` trên đĩa vẫn giữ nguyên để
   ESign ký và cài đặt bình thường, còn game và SDK xác thực luôn nhận đúng
   `com.vng.playtogether`.
2. **Dùng `UIView` thuần (`NSObject<MTKViewDelegate>`), bỏ `UIWindow` phụ và `UIViewController`:**
   - `VTMenuController` kế thừa trực tiếp `NSObject<MTKViewDelegate>` thay vì
     `UIViewController`. Nhờ đó `VTPassthroughView` là một `UIView` thuần (không có
     `_viewDelegate`), gắn trực tiếp vào view gốc mà **không bao giờ** gây lỗi
     `UIViewControllerHierarchyInconsistency`.
   - Không tạo thêm `UIWindow` phụ, giữ nguyên danh sách `windowScene.windows` cho
     `PassSDK`.
   - Trong `VTAttach()`, nếu `parent.presentedViewController != nil` hoặc cửa sổ
     đăng nhập của SDK đang active (`![game isKeyWindow]`), menu tạm dừng can thiệp
     vào phân cấp view.
3. **Hoãn kết nối Backend cho tới khi người dùng mở Menu:**
   - Khi đang ở màn hình đăng nhập (menu đang đóng), `tryStartBackend:` không chạy,
     hoàn toàn không chạm tới `UnityFramework` hay `IL2CPP`.
   - Ngay khi đăng nhập xong và bấm mở nút **Menu**, backend lập tức xác minh và kết nối.
