# HUD Bridge Protocol

## Endpoint

`POST /hud`

Header:

`Content-Type: application/json`

## Payload

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

## Turn values

- straight
- left
- right
- slight_left
- slight_right
- uturn
- roundabout

## Alert values

- none
- police
- camera
- crash
- traffic
- roadworks
- pothole
- object
- car_on_shoulder
- broken_light
- closure
- bad_weather
- blocked_lane
- high_risk
- animal

## Partial updates

Các field có thể bỏ qua. ESP32 giữ giá trị cũ cho field không xuất hiện.

Ví dụ chỉ cập nhật tốc độ:

```json
{"speed": 55, "speed_limit": 60}
```

Ví dụ chỉ cập nhật cảnh báo:

```json
{"alert":{"type":"police","distance_m":800}}
```

## Timeout

Firmware tô xám tốc độ nếu không nhận packet mới trong 3 giây, và hiện `LINK LOST` (ẩn tốc độ, giới hạn, cảnh báo) sau 6 giây.

Lưu ý: frame HLP/1 `s` được coi là snapshot đầy đủ (field thiếu = không có giá trị). Quy tắc "giữ giá trị cũ" ở mục Partial updates chỉ áp dụng cho `POST /hud` dạng JSON.

## Recommended Android bridge policy

- Gửi khi dữ liệu thay đổi.
- Trong lúc navigation đang active, gửi heartbeat mỗi 2-3 giây.
- Không gửi nhanh hơn 5 Hz.
- Dùng cùng Wi-Fi/LAN với ESP32.
