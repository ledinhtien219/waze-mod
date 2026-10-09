# Arduino IDE build guide

The firmware is tested with the same dependency versions pinned by PlatformIO.

## Required versions

- ESP32 by Espressif Systems: **2.0.17**
- LVGL: **8.3.11** (do not use LVGL 9.x)
- NimBLE-Arduino by h2zero: **1.4.3** (do not use NimBLE-Arduino 2.x)
- ArduinoJson: **7.x**
- Adafruit GFX Library
- Adafruit ILI9341

## LVGL configuration for Arduino IDE

PlatformIO passes `LV_CONF_INCLUDE_SIMPLE` and includes `src/lv_conf.h` automatically. Arduino IDE does not.

Copy this repository's `lv_conf.h` to the Arduino libraries directory **next to** the `lvgl` folder.

Typical Windows path:

```text
C:\Users\<YOU>\Documents\Arduino\libraries\lv_conf.h
C:\Users\<YOU>\Documents\Arduino\libraries\lvgl\
```

Then restart Arduino IDE.

The supplied `lv_conf.h` enables the Montserrat 12/14/18/24/40 fonts and LVGL canvas APIs required by the HUD.

## Common version mismatch symptoms

If errors mention `lv_draw_buf_t` instead of `lv_disp_draw_buf_t`, Arduino IDE is compiling against LVGL 9.x.

If errors say NimBLE callbacks marked `override` do not override, or `setScanResponse` is missing, Arduino IDE is compiling against NimBLE-Arduino 2.x.

Use the exact versions above.
