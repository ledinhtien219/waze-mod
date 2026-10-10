# Kiểm thử

## Kiểm thử tự động (không cần board)

```bash
bash tests/run_tests.sh
```

Chạy trên máy tính, cần `g++` và `python3`:

- **Âm lịch:** các mốc đã biết (Tết 2024/2025/2026, tháng nhuận 2023 và 2025, Trung thu 2026) và các bất biến trên 25.933 ngày liên tiếp 1990–2060: ngày âm chỉ tăng thêm 1 hoặc quay về 1 sau ngày 29/30, mỗi năm dương có đúng một ngày Tết.
- **Logic firmware mới:** tự giảm sáng (khung giờ qua nửa đêm, không dùng khi chưa có giờ), nhận giờ từ điện thoại, nút BOOT (bấm ngắn, giữ, giữ 10 giây), chuỗi bíp của còi.

GitHub Actions chạy cùng bộ test này và build firmware cho mỗi lần push (`.github/workflows/ci.yml`).

## Kiểm thử trên board thật

Chưa thể tự động hóa phần này. Sau mỗi bản phát hành, chạy qua danh sách:

**Khởi động và màn hình chờ**
- [ ] Logo khởi động chạy, không treo.
- [ ] Màn hình chờ hiện đồng hồ, ngày dương lịch, thứ trong tuần.
- [ ] Sau khi có Wi-Fi vài giây, giờ đúng múi giờ Việt Nam (không còn `--:--`).
- [ ] Âm lịch khớp lịch trên điện thoại (thử đúng ngày mùng 1 và ngày rằm).
- [ ] Nhiệt độ hiện sau khi có Wi-Fi có internet; đổi khu vực trong Web Setting thì nhiệt độ đổi.
- [ ] Tắt Wi-Fi, mở app Android bridge: giờ vẫn đúng sau khi kết nối BLE.
- [ ] Số giờ nét liền, dấu `:` nháy, thanh giây chạy hết một vòng mỗi phút.

**Điều hướng**
- [ ] Gửi dữ liệu Waze: HUD tự thay thế màn hình chờ trong 1 giây.
- [ ] Ngắt app: sau 30 giây màn hình chờ quay lại; kết nối lại thì HUD trở lại, màu tốc độ đúng.

**Độ sáng**
- [ ] Đặt khung giờ giảm sáng bao gồm giờ hiện tại: màn hình tối đi (đo bằng mắt), ngoài khung thì sáng lại.
- [ ] Nút BOOT giữ 2 giây: độ sáng đổi 100 → 70 → 40 → 100.

**Web Setting**
- [ ] Mở `http://wazehud.local` (hoặc IP) trên điện thoại cùng Wi-Fi.
- [ ] Lưu cài đặt, khởi động lại thiết bị: cài đặt còn nguyên.
- [ ] Đặt PIN: trình duyệt hỏi đăng nhập (admin); thử tải trang bằng tab ẩn danh phải bị yêu cầu PIN. Tắt PIN lại bằng cách để trống.
- [ ] Sao lưu, đổi vài cài đặt, khôi phục từ file: giá trị quay về như lúc sao lưu.
- [ ] Đặt lại mặc định: HUD về cấu hình mặc định, Wi-Fi vẫn giữ.

**Còi (nếu có buzzer)**
- [ ] Bật còi, gửi cảnh báo camera: bíp đôi một lần; cùng cảnh báo đó không bíp lặp lại.
- [ ] Vượt tốc độ: bíp ba tiếng khi bắt đầu vượt.

**Độ bền**
- [ ] Để thiết bị chạy 24 giờ liên tục: không tự khởi động lại bất thường (xem log Serial: dòng `Watchdog: loop stalled` nghĩa là loop từng bị treo quá 45 giây).
- [ ] Cập nhật OTA online và OTA thủ công vẫn thành công (không bị watchdog cắt giữa chừng).
- [ ] Giữ nút BOOT 10 giây: thiết bị về trạng thái ban đầu (hiện AP `WAZE-HUD`).
