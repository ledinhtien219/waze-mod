# Arduino IDE build guide

This repository now includes a **ready-to-open Arduino sketch**.

Open:

```text
ESP32_WAZE_HUD\ESP32_WAZE_HUD.ino
```

Do **not** move only the `.ino` file to another folder. Keep these files together:

```text
ESP32_WAZE_HUD\
├── ESP32_WAZE_HUD.ino
├── lv_conf.h
├── build_opt.h
├── bt_memory_guard.cpp
├── partitions.csv
├── vn_font_14.c
├── vn_font_18.c
└── vn_arrows_22.c
```

`build_opt.h` carries the LVGL configuration as compiler defines and applies it to the
entire Arduino build, including LVGL's own source files. This enables the required fonts
and canvas APIs without relying on a global `lv_conf.h`.

The included `lv_conf.h` remains as the matching PlatformIO/reference configuration, but
Arduino IDE does not require you to copy it into `Documents\Arduino\libraries`.

The sketch also provides a strong `btInUse()` hook in `bt_memory_guard.cpp`. ESP32 Arduino core 2.0.17 calls this hook before `setup()`; returning `true` prevents the core from permanently releasing Bluetooth controller memory. This fixes the reboot loop at **34% / STARTING BLUETOOTH** where NimBLE reported `ESP_ERR_INVALID_STATE`.

## Required versions

Install these exact major versions in Arduino IDE:

- **ESP32 by Espressif Systems: 2.0.17**
- **LVGL: 8.3.11** — do not use LVGL 9.x
- **NimBLE-Arduino by h2zero: 1.4.3** — do not use NimBLE-Arduino 2.x
- **ArduinoJson: 7.x**
- Adafruit GFX Library
- Adafruit ILI9341

Restart Arduino IDE after changing LVGL or NimBLE versions.

## Board

Use:

```text
Board: ESP32 Dev Module
Tools > Partition Scheme > Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)
```

The **Partition Scheme** selection is needed so Arduino IDE uses the correct maximum sketch
size during its final size check. The included local `partitions.csv` still overrides the
actual flash layout and provides the project OTA layout:

- app0: `0x1E0000`
- app1: `0x1E0000`
- SPIFFS: `0x30000`

Do not use the default 1.2MB APP scheme; it will report **Sketch too big** even though the
project's custom partition table has enough space.

## What the local LVGL config enables

The included `lv_conf.h` enables:

- Montserrat 12
- Montserrat 14
- Montserrat 18
- Montserrat 24
- Montserrat 40
- LVGL Canvas

These are required by the HUD, standby clock, boot screen and OTA screen.

## Version mismatch symptoms

If the compiler suggests `lv_draw_buf_t` instead of `lv_disp_draw_buf_t`, Arduino IDE is
using **LVGL 9.x**. Install **8.3.11**.

If NimBLE callbacks marked `override` fail, or `setScanResponse` is missing, Arduino IDE is
using **NimBLE-Arduino 2.x**. Install **1.4.3**.

If only `lv_font_montserrat_14` exists but 12/18/24/40 are missing, make sure you opened
the supplied `ESP32_WAZE_HUD\ESP32_WAZE_HUD.ino` and that `build_opt.h` is still in the
same sketch folder.
