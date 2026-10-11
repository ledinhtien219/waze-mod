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
├── partitions.csv
├── vn_font_14.c
├── vn_font_18.c
└── vn_arrows_22.c
```

The sketch includes its local `lv_conf.h` automatically, so you no longer need to copy
`lv_conf.h` into `Documents\Arduino\libraries`.

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
```

The included `partitions.csv` is the project partition table and gives the firmware enough
OTA/application space.

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
the supplied `ESP32_WAZE_HUD\ESP32_WAZE_HUD.ino` and that `lv_conf.h` is still in that
same sketch folder.
