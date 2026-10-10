# Mẫu vỏ in 3D (khởi đầu)

`case.scad` là mẫu OpenSCAD tham số hóa: hộp có ngăn cho ESP32 DevKit V1 và mặt trước có ô cửa sổ cho màn ILI9341 2.4".

> **Chưa được in thử.** Kích thước module ILI9341 và ESP32 khác nhau giữa các nhà sản xuất. Đo module thật bằng thước kẹp, sửa các biến ở đầu file rồi xem trước (`F5`) trước khi in. Nên in thử một bản nhỏ để kiểm tra độ vừa.

Cách dùng:

1. Cài OpenSCAD (https://openscad.org).
2. Mở `docs/case.scad`, đổi `part = "base"` hoặc `part = "lid"`.
3. `F6` để render, `File → Export → STL`.

Ghi chú in: PLA/PETG, lớp 0.2 mm, không cần support nếu in mặt đáy xuống. Vỏ để ô khoét cổng micro-USB; không dùng vật liệu dễ chảy mềm (PLA thường) nếu đặt trong xe dưới nắng, nên dùng PETG hoặc ASA.
