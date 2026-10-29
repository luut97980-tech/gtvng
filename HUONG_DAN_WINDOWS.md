# Windows — menu iOS chỉ ESP + Spam nhảy

Đây là source R5; chưa có dylib/IPA build sẵn. Không dùng Qt/Visual Studio để
build target iOS này.

1. Giải nén. Đưa nội dung thư mục VTMenu_iOS_ESP_Spam_R5 vào root repository
   GitHub của bạn, bao gồm .github/workflows. Không đưa IPA/game lên repository.
2. Actions > **Build iOS ESP Spam Menu** > Run workflow. Máy runner macOS dùng
   Xcode để build. Chưa chạy workflow trong phiên tạo source; runner có thể dùng
   hạn mức/tính phí theo gói GitHub của bạn.
3. Tải artifact **VTMenu-iOS-ESP-Spam-R5**, giải nén. Cần VTMenu.dylib,
   menu-build.json và tao_ipa_menu.py cùng bản. Không dùng lại dylib/manifest R4.
4. Nhấp đúp tao_ipa_menu.py bằng Python 3.9+, chọn IPA gốc VNG 2.32.0 (227628),
   rồi chọn VTMenu.dylib. Tool tạo <ten-IPA>-VTMenu-UNSIGNED.ipa cạnh file gốc.
5. Ký lại app và framework bằng công cụ đang dùng (ví dụ ESign) cùng certificate/
   profile hợp lệ. Giữ bundle ID gốc (com.vng.playtogether hoặc ptg.vng). IPA chưa ký chưa cài được.
6. Vào app, chạm Menu: chỉ có ESP, Spam nhảy và thanh chỉnh nhịp. Bật spam để
   gửi yêu cầu nhảy lặp. Game quyết định có chấp nhận yêu cầu; 0 ms chỉ tối đa một
   yêu cầu/khung hình. Không tăng lực nhảy hay bật nhảy đôi.
7. Nếu còn lỗi, mở Chi tiết > Sao chép chẩn đoán. R5 hiện metadata, UUID/chữ ký, host view/kích thước/hướng để kiểm tra lỗi lệch. Native build/thiết bị thật của R5 chưa được thử ở đây.

Không cần gửi IPA 2 GB lên cuộc trò chuyện. Hướng dẫn kỹ thuật đầy đủ: README.md.
