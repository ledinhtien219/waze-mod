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


## Giao diện đề xuất

Dự án dùng mô hình **màn TFT chỉ hiển thị HUD**, không cần cảm ứng. Toàn bộ cài đặt được thao tác trên điện thoại bằng trình duyệt web.

### TFT 320×240

Màn hình ILI9341 chỉ dùng cho các trạng thái:

- HUD điều hướng khi đang chạy.
- Waiting / chưa nhận dữ liệu từ điện thoại.
- Mất kết nối bridge.
- Đang cập nhật firmware OTA.

Bố cục HUD chính:

- Cột trái: tốc độ hiện tại, biển giới hạn tốc độ, cảnh báo camera gần.
- Khu vực giữa: khoảng cách tới lần rẽ tiếp theo, mũi tên điều hướng lớn, tên đường.
- Cột phải: cảnh báo Waze/WAZE mod gần nhất.
- Thanh dưới: quãng đường còn lại, ETA và tên tuyến.

### Web Setting trên điện thoại

Mở địa chỉ IP ESP32 bằng trình duyệt để cấu hình:

- Hiển thị HUD.
- Chế độ ban đêm.
- Phản chiếu HUD.
- Hiển thị tên đường.
- Hiển thị tuyến đường.
- Hiển thị ETA.
- Hiển thị biển giới hạn tốc độ.
- Bật/tắt cảnh báo Police.
- Bật/tắt Camera.
- Bật/tắt Crash.
- Bật/tắt Traffic.
- Bật/tắt Roadworks.
- Bật/tắt nhóm cảnh báo khác.
- Cấu hình Wi-Fi.
- Kiểm tra trạng thái Android Bridge.
- Gửi dữ liệu HUD mẫu.
- Kiểm tra và cài đặt firmware online.
- Xem thông tin thiết bị.

### Luồng sử dụng

```text
Waze / WAZE mod trên Android
        ↓
Android Bridge
NotificationListener + Accessibility
        ↓ HTTP JSON
ESP32
        ↓
ILI9341 320×240 HUD

Điện thoại
        ↓ trình duyệt
Web Setting trên ESP32
```

## Cập nhật online

Trang Web Setting có mục **Cập nhật online** gồm:

- Firmware hiện tại.
- Phiên bản mới nhất trên GitHub.
- Kiểm tra cập nhật.
- Tải về và cập nhật.
- Tự động kiểm tra khi khởi động.

Quy trình:

```text
ESP32
  ↓
GitHub Releases API
  ↓
release mới nhất
  ↓
firmware.bin
  ↓ HTTPS
Update.writeStream()
  ↓
ESP32 restart
```

Trong lúc cập nhật TFT sẽ hiển thị:

```text
UPDATING
DO NOT POWER OFF
```

Không tắt nguồn trong quá trình ghi firmware.

## Mockup giao diện

Mockup dự kiến gồm hai phần:

1. **HUD 320×240 trên ILI9341**: tối giản, dễ đọc khi lái xe.
2. **Web Setting trên điện thoại**: giao diện tối, các nhóm cài đặt rõ ràng và có mục cập nhật OTA online.

Ảnh mockup nên được lưu trong repo ở `docs/` nếu muốn hiển thị trực tiếp trong README.
