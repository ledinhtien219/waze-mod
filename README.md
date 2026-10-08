# ESP32 Waze HUD Mod

HUD 320x240 dành cho **ESP32 DevKit V1 + ILI9341**. TFT không cần cảm ứng: màn hình chỉ hiển thị HUD, toàn bộ cấu hình thực hiện bằng web trên điện thoại.

## Tính năng

- HUD điều hướng 320x240 nền đen, tương phản cao.
- Tốc độ hiện tại và biển giới hạn tốc độ.
- Mũi tên rẽ, khoảng cách, tên đường, ETA, quãng đường còn lại và tên tuyến.
- Cảnh báo Waze/Waze mod: police, camera, crash, traffic, roadworks, pothole, object, car on shoulder, broken light, closure, bad weather, blocked lane, high-risk area, animal.
- Web Setting trên điện thoại:
  - hiển thị HUD;
  - cảnh báo;
  - Wi-Fi;
  - trạng thái Android bridge;
  - kiểm tra và cập nhật firmware online;
  - thông tin thiết bị.
- Cấu hình lưu bằng ESP32 Preferences.
- Android bridge gồm NotificationListenerService + AccessibilityService.
- GitHub Actions build firmware.
- GitHub Release tự đính kèm `firmware.bin` khi push tag `v*`.

## Phần cứng

| ILI9341 | ESP32 DevKit V1 |
|---|---:|
| CS | GPIO27 |
| RST | GPIO25 |
| DC | GPIO26 |
| MOSI | GPIO13 |
| SCK | GPIO14 |
| MISO | GPIO35 |

Logic 3.3V.

## Cài thư viện Arduino

- Adafruit GFX Library
- Adafruit ILI9341
- ArduinoJson 7.x

## Sử dụng

Lần đầu nếu chưa có Wi-Fi, ESP32 tạo AP:

- SSID: `WAZE-HUD`
- Password: `12345678`
- Web: `http://192.168.4.1`

Sau khi cấu hình Wi-Fi, mở địa chỉ IP ESP32 bằng trình duyệt trên điện thoại. TFT không cần cảm ứng.

## API HUD

`POST /hud`

```json
{
  "turn": "right",
  "distance_m": 350,
  "road": "Vo Nguyen Giap",
  "speed": 62,
  "speed_limit": 60,
  "remaining_km": 8.6,
  "eta": "10:42",
  "route": "QL1A",
  "alert": {
    "type": "camera",
    "distance_m": 500
  }
}
```

Xem thêm `PROTOCOL.md`.

## OTA online

Firmware gọi GitHub Releases API để kiểm tra release mới nhất của:

`ledinhtien219/waze-mod`

Release phải có asset tên `firmware.bin`.

Quy trình phát hành:

```bash
git tag v1.1.0
git push origin v1.1.0
```

Workflow `Release Firmware` sẽ compile PlatformIO và tạo GitHub Release có `firmware.bin`. Sau đó thiết bị có thể vào web Setting > Cập nhật online > Kiểm tra cập nhật > Tải về & cập nhật.

Không tắt nguồn khi ESP32 đang ghi firmware.

## Android bridge

Mở thư mục `android-bridge` bằng Android Studio, build APK, nhập IP ESP32 rồi bật quyền Notification Access và Accessibility nếu muốn đọc thêm dữ liệu đang hiển thị trong Waze/Waze mod.

Parser hiện tại là best-effort vì text/layout của từng Waze mod có thể khác nhau theo phiên bản và ngôn ngữ.
