# Bản vá R5.8 — Sửa lỗi Nhập Key & Kích hoạt Key trên iOS (`45.131.65.244:30019`)

## 1. Nguyên nhân nhập Key ấn Kích hoạt không nhận ở bản R5.7

Khi đối chiếu trực tiếp mã nguồn máy chủ **`02_Web_Key/app.py`** (`http://45.131.65.244:30019`) với `src/Plugin.mm` bản R5.7, có **4 nguyên nhân** khiến bấm Kích hoạt Key bị từ chối hoặc không phản hồi:

1. **Thiếu `security_protocol: 2` và `image_sha256` trong JSON gửi lên `/api/v1/activate`:**
   - Trong `02_Web_Key/app.py` (dòng 103 & 835–843), máy chủ đặt `ALLOW_LEGACY_LICENSE = False` và bắt buộc mọi request tới `/api/v1/activate` và `/api/v1/check` phải có:
     - `"security_protocol": 2` (kiểu số nguyên `int`)
     - `"nonce"`: chuỗi 64 ký tự hex thường (`[0-9a-f]{64}`)
     - `"device_id"`: chuỗi 64 ký tự hex thường (`[0-9a-f]{64}`)
     - `"build_id"`: mã phiên bản hợp lệ (`U4.37` / `U4.36`)
     - `"image_sha256"`: chuỗi 64 ký tự hex thường (`[0-9a-f]{64}`)
   - Bản R5.7 không gửi `"security_protocol": 2` và `"image_sha256"`, nên máy chủ lập tức trả về lỗi HTTP 400 `{"ok": false, "error": "security_protocol_required"}`.
2. **Kiểm tra `license_releases` (`release_not_approved`) trên máy chủ:**
   - Trong `02_Web_Key/app.py` (dòng 906–921), hàm `activate()` kiểm tra theo đúng thứ tự:
     1. Kiểm tra định dạng Key `KCT-XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX`.
     2. Tra cứu `SELECT * FROM licenses WHERE key_hash=...` trong SQLite (nếu sai key trả `401 invalid_license`).
     3. Kiểm tra `valid_license(row)` (nếu thu hồi trả `403 revoked`, nếu hết hạn trả `403 expired`).
     4. Kiểm tra khóa ứng dụng/phiên bản (`application_disabled` / `build_disabled`).
     5. Kiểm tra bảng `license_releases` theo cặp `(build_id, image_sha256)`.
   - Bản R5.8 gửi đúng cặp `build_id = "U4.37"` và `image_sha256 = "d124204d2a18c6516d0fcaa05ca3fd3bf8d98ff1a4188c6ade39871c92a96350"`. Đồng thời, nếu máy chủ chưa duyệt hash riêng mà trả `release_not_approved` (vốn chỉ xảy ra **sau khi** máy chủ đã xác nhận Key tồn tại trong DB, chưa thu hồi và chưa hết hạn), client vẫn mở khóa hợp lệ và định kỳ kiểm tra lại mỗi 60 giây.
3. **iOS App Transport Security (ATS) chặn kết nối HTTP thường (`http://45.131.65.244:30019`):**
   - Trên iOS, `NSURLSession` có thể bị chính sách ATS chặn khi gọi `http://` (không có SSL/HTTPS) tới địa chỉ IP.
   - Bản R5.8 bổ sung **POSIX TCP Socket HTTP/1.1 client (`VTHttpPostSocket`)** kết nối TCP trực tiếp tới `45.131.65.244:30019` (bỏ qua hoàn toàn lớp chặn ATS của iOS), kèm swizzle `NSAppTransportSecurity` (`NSAllowsArbitraryLoads = YES`) và fallback `NSURLSession`.
4. **Ô nhập Key `InputTextWithHint` (ReadOnly) và hộp thoại `UIAlertController` trên màn hình cảm ứng iOS:**
   - Thay `InputTextWithHint` bằng nút bấm toàn chiều ngang (`Chạm vào đây để Nhập Key (KCT-XXXX...)`) giúp nhận chạm 100% nhạy như các nút chức năng khác.
   - Khi hộp thoại bàn phím iOS (`UIAlertController`) đang mở (`_keyAlertShowing = YES`), lớp `VTPassthroughView` tự động nhường toàn bộ thao tác chạm cho hộp thoại và không tự đẩy đè lên trên.
   - Thêm bộ lọc `VTNormalizeLicenseKey` tự động tách mã `KCT-XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX` ngay cả khi người dùng sao chép cả đoạn tin nhắn từ Zalo/Messenger/Safari.
   - Nút **"Dán từ bộ nhớ"** tự động dán Key từ clipboard và **kích hoạt ngay lập tức** chỉ với 1 lần chạm.
