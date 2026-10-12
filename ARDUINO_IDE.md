# Arduino IDE build guide

This repository includes a ready-to-open Arduino sketch:

```text
ESP32_WAZE_HUD\ESP32_WAZE_HUD.ino
```

Keep the entire `ESP32_WAZE_HUD` folder together. Do not move only the `.ino` file.

## Required versions

- ESP32 by Espressif Systems: **2.0.17**
- LVGL: **8.3.11**
- ArduinoJson: **7.x**
- Adafruit GFX Library
- Adafruit ILI9341

**Do not install NimBLE-Arduino for this firmware.** The HUD now uses the BLE stack bundled
with ESP32 Arduino core 2.0.17 (`BLEDevice` / Bluedroid). This avoids the
`ESP_ERR_INVALID_STATE` crash that occurred inside NimBLE-Arduino 1.4.3 at
`esp_bt_controller_init()` during the 34% STARTING BLUETOOTH stage.

## Board settings

```text
Board: ESP32 Dev Module
Tools > Partition Scheme > Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)
```

The local `partitions.csv` provides the project OTA layout. The larger partition scheme is
also needed so Arduino IDE's final size check accepts the firmware.

## LVGL configuration

`build_opt.h` carries the LVGL compile defines for the whole Arduino build. It enables the
Montserrat 12/14/18/24/40 fonts and canvas support required by the HUD.

You do not need to copy `lv_conf.h` into the global Arduino libraries directory.

## Expected Bluetooth boot log

A successful BLE startup now prints:

```text
BLE stack: Arduino Bluedroid, controller before init=0
BLE controller after init=2
Arduino BLE HLP/1 + Studio advertising as WazeHUD, address: ...
```

Status 0 is IDLE and status 2 is ENABLED.

If Arduino IDE still mentions `NimBLEDevice.cpp`, it is compiling an older copy of the
sketch. Open the supplied `ESP32_WAZE_HUD\ESP32_WAZE_HUD.ino` from this package.
