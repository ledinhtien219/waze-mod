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

## Đồng bộ giờ từ điện thoại (HLP/1 `time`)

ESP32 không có pin RTC. Khi không có Wi-Fi/NTP, app có thể gửi giờ điện thoại qua BLE để đồng hồ chờ và chế độ giảm sáng ban đêm hoạt động:

```json
{"v":1,"t":"time","ts":1791000000}
```

- `ts`: Unix time (giây, UTC). Firmware luôn hiển thị theo múi giờ Việt Nam (UTC+7).
- Firmware chỉ nhận khi chưa có giờ hợp lệ hoặc Wi-Fi chưa kết nối; khi Wi-Fi/NTP đã có giờ thì bỏ qua để tránh lệch.
- Nên gửi sau khi kết nối BLE và lặp lại mỗi 10 phút. App Android bridge trong repo đã làm việc này.

Có thể kèm thời tiết (tùy chọn). Khi có, firmware dùng giá trị này thay vì tự gọi Open-Meteo trong 30 phút:

```json
{"v":1,"t":"time","ts":1791000000,"temp":28.5,"wx":3}
```

- `temp`: nhiệt độ °C. `wx`: mã thời tiết WMO (0 quang, 1–2 ít mây, 3 nhiều mây, 45/48 sương mù, 51–57 mưa phùn, 61–67 mưa, 80–82 mưa rào, 95+ dông).

## Cài đặt Studio (BLE settings characteristic)

Ngoài các khóa cũ, firmware 1.8.0 đọc/ghi thêm: `sb` (đồng hồ chờ), `sl` (âm lịch), `st` (nhiệt độ), `wc` (khu vực thời tiết 0–11), `ad` (tự giảm sáng), `al` (độ sáng ban đêm 20–80), `af`/`at` (giờ bắt đầu/kết thúc, 0–23), `bz` (còi). Mỗi lần ghi tối đa 240 byte nên Studio gửi các khóa mới thành một payload riêng; khóa nào vắng mặt thì giữ nguyên.

## Web Setting HTTP

| Endpoint | Mô tả |
|---|---|
| `GET /` | Trang cài đặt |
| `POST /settings` | Lưu cài đặt (form) |
| `GET /backup` | Tải JSON cài đặt (không gồm Wi-Fi và PIN) |
| `POST /restore` | Khôi phục từ JSON (body là JSON) |
| `POST /reset-settings` | Đặt lại cài đặt HUD về mặc định (giữ Wi-Fi và PIN) |
| `POST /security` | Đặt/tắt PIN (`pin=`; trống = tắt; 4–16 ký tự) |
| `POST /reboot` | Khởi động lại |
| `GET /state` | Trạng thái JSON (luôn mở, không cần PIN) |
| `POST /hud` | Nhận dữ liệu HUD (luôn mở để Android bridge dùng được) |

Khi đã đặt PIN, tất cả endpoint trên trừ `/state`, `/hud`, `/update-status` yêu cầu HTTP Basic (user `admin`, mật khẩu là PIN).
