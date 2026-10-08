# ESP32 Waze HUD Mod

HUD 320x240 dành cho **ESP32 DevKit V1 + ILI9341**, nhận dữ liệu điều hướng/cảnh báo từ điện thoại Android chạy Waze/Waze mod qua HTTP JSON.

## Mục tiêu

- Giao diện đúng tỉ lệ màn hình 320x240.
- Nền đen, chữ/biểu tượng tương phản cao.
- Mũi tên điều hướng lớn ở giữa.
- Hiển thị tốc độ hiện tại + biển giới hạn tốc độ.
- Cảnh báo gần nhất: police, camera, crash, traffic, roadworks, pothole, object, car on shoulder, broken light, closure, bad weather, blocked lane, high-risk area, animal.
- Hiển thị khoảng cách tới lần rẽ, tên đường, quãng đường còn lại, ETA và tên tuyến.
- Không dùng bitmap ngoài: icon được vẽ bằng primitive để nhẹ flash và dễ mirror HUD.

## Phần cứng

ESP32 DevKit V1 + ILI9341 SPI 320x240:

| ILI9341 | ESP32 |
|---|---:|
| CS | GPIO27 |
| RST | GPIO25 |
| DC | GPIO26 |
| MOSI | GPIO13 |
| SCK | GPIO14 |
| MISO | GPIO35 |

Nguồn logic 3.3V.

## Thư viện Arduino

- Adafruit GFX Library
- Adafruit ILI9341
- ArduinoJson 7.x

## API

ESP32 mở endpoint:

```
POST /hud
Content-Type: application/json
```

Ví dụ:

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

Các giá trị `turn`: `straight`, `left`, `right`, `slight_left`, `slight_right`, `uturn`, `roundabout`.

Các giá trị `alert.type`: `none`, `police`, `camera`, `crash`, `traffic`, `roadworks`, `pothole`, `object`, `car_on_shoulder`, `broken_light`, `closure`, `bad_weather`, `blocked_lane`, `high_risk`, `animal`.

## Test nhanh

Sau khi ESP32 kết nối Wi-Fi, mở:

```
http://<IP-ESP32>/
```

Trang test cho phép gửi dữ liệu HUD mẫu.

## Cấu trúc

- `ESP32_WAZE_HUD.ino`: firmware chính.
- `PROTOCOL.md`: giao thức bridge Android -> ESP32.

## Android bridge

Waze/Waze mod không có API công khai đơn giản để ESP32 lấy toàn bộ cảnh báo. Thiết kế dự án dùng điện thoại Android làm bridge:

1. NotificationListenerService lấy thông tin notification nếu có.
2. AccessibilityService bổ sung dữ liệu đang hiển thị khi cần.
3. Chuẩn hoá thành JSON.
4. POST sang `/hud` trên ESP32.

Bridge nên chỉ đọc dữ liệu người dùng đã chủ động cho phép và không cần can thiệp traffic mạng của Waze.
