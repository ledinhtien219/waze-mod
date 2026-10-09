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
- BLE GATT: điện thoại quét thiết bị `WAZE-HUD` và truyền dữ liệu HUD trực tiếp qua BLE; Wi-Fi chỉ còn dùng cho Web Setting, test HTTP và OTA.
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

### Bắt buộc khi build bằng Arduino IDE

Firmware BLE v1.2.x lớn hơn partition mặc định 1.31 MB của ESP32. Trong Arduino IDE chọn:

- **Board:** ESP32 Dev Module
- **Flash Size:** 4MB (32Mb)
- **Partition Scheme:** **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**  
  (một số bản ESP32 core mới có thể ghi **130KB SPIFFS** thay vì 190KB)
- Lần nạp đầu sau khi đổi partition: có thể bật **Erase All Flash Before Sketch Upload**

Sau khi chọn đúng, lúc Verify dòng dung lượng phải báo gần:

`Maximum is 1966080 bytes`

Nếu vẫn hiện:

`Maximum is 1310720 bytes`

thì Arduino IDE vẫn đang dùng partition mặc định.

Repo cũng có sẵn `partitions.csv`. Nếu dùng custom partition tự động, file này phải nằm **cùng thư mục sketch** với `ESP32_WAZE_HUD.ino` và đúng tên `partitions.csv` (không phải `partitions.csv.txt`).

Firmware BLE lớn hơn giới hạn app mặc định của ESP32. Repo có sẵn `partitions.csv` với 2 OTA slot ~1.9 MB. Khi dùng Arduino IDE, hãy giữ `partitions.csv` cùng thư mục với `ESP32_WAZE_HUD.ino` để build và OTA đúng phân vùng.

## Sử dụng

### Kết nối BLE

1. Nạp firmware v1.2.0 trở lên cho ESP32.
2. Mở app Android Bridge và cấp quyền Bluetooth/BLE.
3. Bấm **Quét & kết nối WAZE-HUD**.
4. Khi trạng thái báo **Đã kết nối WAZE-HUD**, dữ liệu điều hướng được gửi qua BLE.

ESP32 advertise BLE với tên `WAZE-HUD`. 
### Wi-Fi / Web Setting

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


## BLE HLP/1 tương thích WazeMod

Firmware v1.2.1 dùng đúng transport BLE HLP/1 của WazeMod:

- Tên BLE: `WazeHUD`
- Service: `8a7e0001-4d6e-4c48-9a9d-484c504c0001`
- Android → HUD TX: `8a7e0002-4d6e-4c48-9a9d-484c504c0001`
- HUD → Android RX notify: `8a7e0003-4d6e-4c48-9a9d-484c504c0001`
- Capabilities: `8a7e0004-4d6e-4c48-9a9d-484c504c0001`

Trong WazeMod hãy vào HUD Link → Chọn thiết bị và chọn `WazeHUD` dạng BLE. Không chọn Bluetooth Classic cho firmware BLE này.


## Firmware v1.2.2 — ổn định BLE và chống nháy TFT

- BLE GATT callback chỉ copy ATT chunk vào FreeRTOS queue; parse HLP/1 và render chạy ngoài callback.
- Frame được ghép theo LF và giới hạn 512 byte.
- TFT dùng dirty-region rendering: không còn `fillScreen()` mỗi state/heartbeat.
- HLP state giống hệt nhau được WazeMod gửi mỗi giây sẽ không làm màn hình redraw.
- Trạng thái LINK LOST chỉ redraw khi thực sự chuyển trạng thái.
- Wi-Fi connect timeout khởi động giảm còn 6 giây; BLE chỉ advertise sau khi bước Wi-Fi setup hoàn tất để tránh nhận kết nối khi main loop chưa chạy.


## Firmware v1.2.3 — BLE discovery diagnostics

- HLP service UUID được đặt trực tiếp trong BLE advertising packet để WazeMod picker lọc chắc chắn.
- Tên `WazeHUD` nằm trong scan response.
- Serial Monitor in BLE address thực tế của ESP32.
- Web `/state` trả thêm `ble_name` và `ble_address`.
- Khi dùng WazeMod built-in HUD Link, hãy chọn transport **BLE GATT** và chọn lại thiết bị sau khi flash; bản ghi Classic cũ không tự đổi transport chỉ vì chọn "Tự động".


## Firmware v1.2.4 — Arduino IDE compatibility

- Thêm prototype thủ công cho `parseHlpTurn(JsonDocument &doc)` để tránh lỗi auto-prototype của Arduino IDE: `TurnType does not name a type`.
- Chuẩn hoá `BLECharacteristic::getValue()` qua `.c_str()` sang Arduino `String`, tương thích cả ESP32 core trả `std::string` lẫn core mới trả `String`.


## Firmware v1.3.0 — HUD UI redesign

- Bố cục mới tối giản kiểu ô tô: road/distance ở header, speed card bên trái, maneuver lớn ở giữa, alert/link card bên phải, ETA/remaining/route ở footer.
- Bỏ các box `NAV ACTIVE`, `NEXT` thừa và giảm chữ nhỏ.
- Màu nền đen, cyan làm accent, vàng chỉ dùng cho distance/cảnh báo.
- Waiting screen được làm lại gọn hơn.
- Vẫn giữ dirty-region rendering để không nháy toàn màn hình.


## Firmware v1.3.1 — HLP/1 data fix, alerts, Wi-Fi IP & smoother TFT

- Parse đúng state HLP/1: `trn`, `dst`, `st/st2`, `rm/rkm`, `alr/alrD/alrV/alrS/alrM`.
- Khai báo `dev.want.fields` đúng chuẩn và opt-in `alrs` để nhận tối đa 4 cảnh báo phía trước.
- Hỗ trợ mã cảnh báo HLP/1 0..75, gồm camera, police, traffic jam, roadwork, biển cấm, đèn giao thông và các cảnh báo mở rộng.
- Alert gần nhất hiển thị đúng label, khoảng cách, giá trị tốc độ hoặc mức ùn tắc/phút chậm khi có.
- Web `/state` có thêm alert diagnostics.
- Boot screen hiển thị trạng thái Wi-Fi và IP DHCP/AP mới ngay khi kết nối.
- TFT SPI tăng từ 20 MHz lên 40 MHz để giảm thời gian redraw.


## Firmware v1.3.6 — balanced HUD, clearer NOW/NEXT and live IP

- Cân lại layout 320x240: SPEED 88px, NEXT maneuver 124px, alert 84px.
- NOW/NEXT speed limits tách rõ hai cột; NEXT hiện khoảng cách đến giới hạn tốc độ kế tiếp.
- Mũi tên nhỏ hơn và phân biệt straight/left/right/slight/sharp/keep/exit/U-turn/roundabout/arrive.
- Map HLP `trn` sang đúng nhóm maneuver thay vì ép sharp/keep/exit thành left/right.
- Header hiển thị tên đường đã bỏ dấu Unicode an toàn và IP Wi-Fi/AP hiện tại.
- IP trên màn chính tự đổi sau khi DHCP cấp địa chỉ mới.
- Giữ hệ thống alert HLP và icon cảnh báo chuyên biệt của v1.3.5.


## Firmware v1.3.7 — Wi-Fi reconnect fix

- Loại bỏ việc gọi lại `WiFi.begin()` khi STA vẫn đang kết nối; đây là nguyên nhân log `sta is connecting, cannot set config`.
- Chỉ nạp SSID/password một lần lúc boot.
- Fallback AP `WAZE-HUD` không ghi đè station config.
- Background retry dùng `WiFi.reconnect()` mỗi 15 giây.
- Tắt Arduino auto-reconnect để tránh hai cơ chế reconnect chạy chồng nhau.
- Serial log thêm Wi-Fi disconnect reason và `/state` có `wifi_disconnect_reason`.


## Firmware v1.4.0 — 3 HUD layouts + smooth fonts

- Thêm 3 bố cục chọn trong Web Setting: Balanced, Navigation và Minimal.
- Lưu bố cục bằng Preferences, reboot vẫn giữ.
- Chuyển các chữ chính sang FreeSans/FreeSansBold GFX fonts để nét mượt hơn trên ILI9341.
- Balanced: cân bằng tốc độ, maneuver, alert.
- Navigation: ưu tiên mũi tên và khoảng cách đến lần rẽ.
- Minimal: giao diện thoáng, ít khung, tốc độ + maneuver nổi bật.
- Giữ IP Wi-Fi/AP trên header và toàn bộ HLP alert / NOW / NEXT logic.


## Firmware v1.4.1 — robust online OTA

- OTA không còn tải/ghi firmware ngay bên trong WebServer request.
- Nút update trả HTTP 202 trước, firmware thực hiện download/write sau trong loop.
- Thêm `/update-status` và trạng thái queued/downloading/writing/success/failed.
- Kiểm tra `ESP.getFreeSketchSpace()` trước khi ghi.
- Timeout HTTPS dài hơn cho GitHub Release.
- Serial log HTTP code, Content-Length, số byte đã ghi và mã lỗi `Update`.
- Web hiển thị nguyên nhân lỗi OTA thay vì chỉ báo chung chung.


## Firmware v1.4.2 — Waze alert icon mapping 0..75

- Đối chiếu enum/mapping HLP alert 0..75 với WazeHUD-CYD-2.8 release 1.1.2.
- Không chép bitmap GPL; icon được vẽ lại bằng primitive Adafruit_GFX để giữ firmware nhẹ và độc lập.
- Thêm icon riêng cho police, các loại camera, red-light camera, accident, jam, closure, roadwork, pothole, railway, toll, restriction signs, flood/fog/hail/snow/ice, cyclist, emergency, traffic light và các mã HLP mở rộng.
- SPEED_DROP / END_SPEED_RESTRICTION dùng trực tiếp giá trị `alrV` để vẽ biển tốc độ.
- Traffic jam dùng `alrS` để hiện mức độ.
- Bỏ IP khỏi màn HUD chính; IP vẫn còn ở boot/settings để cấu hình OTA.
- Bỏ ONLINE / NO ALERT khỏi vùng alert khi đang lái.


## Firmware v1.5.0 — Final Full HUD

- Thêm style 3 **Full HUD (Chốt)** và migrate một lần để thiết bị hiện style này mặc định.
- Bố cục bám theo mockup cuối: instruction trên cùng, maneuver + khoảng cách bên trái, tốc độ cực lớn giữa, biển giới hạn tốc độ lớn, alert stack bên phải, ETA/lane/road/clock ở dưới.
- Bỏ hoàn toàn mục ắc quy / 14.0V.
- Lane guidance có 4 hướng: hướng maneuver hiện tại sáng cyan; các hướng chưa chọn giảm sáng mạnh bằng dark gray.
- Sử dụng FreeSansBold24pt cho tốc độ chính và gom redraw trong một SPI transaction để nét/nhanh hơn trên ILI9341.
- HUD chính không hiện IP / ONLINE / NO ALERT.
- IP chỉ còn ở màn boot/settings.
- Clock dùng NTP UTC+7 khi Wi-Fi có Internet.
- Giữ mapping alert HLP 0..75 và OTA robust từ v1.4.x.


## Firmware v1.5.1 — branded boot + real OTA progress

- Thêm logo nhận diện WazeHUD nguyên bản bằng vector Adafruit_GFX, không dùng bitmap nặng.
- Màn khởi động có logo, tên sản phẩm, version và progress theo các bước SETTINGS / NETWORK / BLE / WEB / UPDATE CHECK / READY.
- OTA chuyển từ `Update.writeStream()` sang stream 1 KB có đo byte thực tế.
- Màn TFT hiển thị thanh tiến trình OTA thật 0–99%, bước VERIFYING và 100% UPDATE COMPLETE.
- Trang Web Setting có progress bar và phần trăm OTA đồng bộ qua `/update-status`.
- Trong khi OTA, firmware vẫn phục vụ `/update-status` để trình duyệt cập nhật tiến trình.
- Giữ nguyên Full HUD v1.5.0 và toàn bộ HLP alert / Wi-Fi / BLE.


## Firmware v1.6.0 — LVGL renderer

- Thay renderer Adafruit_GFX thủ công bằng LVGL 8.3.11 cho Full HUD.
- Partial display buffer 320×16, không cần PSRAM/full framebuffer.
- Montserrat anti-aliased cho toàn bộ text chính: title, speed, ETA, alert, road, clock.
- HUD chỉ giữ một layout Full HUD để giảm flash và tránh hai renderer chạy song song.
- Maneuver + lane guidance được vẽ trên canvas LVGL: route active cyan sáng, lane khác giảm sáng mạnh.
- Boot logo, waiting screen và OTA progress chuyển sang LVGL.
- BLE HLP/1, Wi-Fi, Web Setting và OTA logic giữ nguyên.

- LVGL branch chuyển Bluetooth stack từ Bluedroid BLE Arduino sang NimBLE-Arduino 1.4.3 để giảm flash nhưng giữ nguyên UUID/protocol HLP/1.

- LVGL chỉ chạy từ Arduino loop; NimBLE callback không render trực tiếp để tránh cross-task UI access.

- v1.7.1: Studio chỉ còn một giao diện Full HUD; thêm BLE Live HUD characteristic để preview tốc độ/chỉ hướng/cảnh báo trực tiếp từ Waze, sửa HUD mirror và software brightness hoạt động thật, đồng thời giữ tương thích cấu hình với firmware v1.7.0.


## Firmware v1.7.2 — ESP32 Wi-Fi/BLE coexistence boot fix

- Sửa boot loop tại `NimBLEDevice::init()` / `coex_core_enable()`.
- Không còn gọi `WiFi.setSleep(false)`; Wi-Fi modem sleep được giữ bật để ESP32 Wi-Fi + BLE coexistence hoạt động đúng.
- Khởi tạo BLE/Studio trước Wi-Fi để Web Bluetooth vẫn sẵn sàng ngay cả khi Wi-Fi lỗi hoặc timeout.
- Giảm thời gian chờ Wi-Fi ban đầu từ 20 giây xuống 8 giây.


## Firmware v1.7.3 — Studio BLE JSON framing fix

- Studio characteristics now call NimBLE `setValue(data, length)` explicitly.
- Prevents trailing buffer bytes from being exposed to Web Bluetooth JSON reads.
- Studio parser also strips NUL/trailing bytes for compatibility with older 1.7.x firmware.
