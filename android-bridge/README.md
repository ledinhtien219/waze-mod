# Android Waze HUD Bridge

Ứng dụng bridge tối giản cho Android.

## Chức năng

- NotificationListenerService: đọc notification từ package có tên chứa `waze`.
- AccessibilityService: đọc text/contentDescription trên màn hình Waze/Waze mod khi người dùng chủ động cấp quyền.
- Parser heuristic: nhận diện turn, distance, ETA và nhiều nhóm cảnh báo.
- HTTP POST JSON sang ESP32 `/hud`.

## Build

Mở thư mục `android-bridge` bằng Android Studio mới, sync Gradle và build APK.

## Cài đặt

1. Cùng Wi-Fi với ESP32.
2. Mở app, nhập IP ESP32.
3. Bật Notification Access.
4. Bật Accessibility cho Waze HUD Bridge nếu muốn lấy thêm dữ liệu đang hiển thị.
5. Bấm Send test HUD để kiểm tra.

## Lưu ý

Waze/Waze mod có thể thay đổi text/layout theo phiên bản, ngôn ngữ và khu vực. Parser hiện tại là best-effort và được thiết kế để dễ bổ sung keyword/selector sau khi test trên đúng bản app đang dùng.
