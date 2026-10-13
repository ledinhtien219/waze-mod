# Arduino IDE build guide

Open:

```text
ESP32_WAZE_HUD\ESP32_WAZE_HUD.ino
```

Keep the whole `ESP32_WAZE_HUD` folder together.

## Required versions

Install these exact versions:

- **ESP32 by Espressif Systems: 3.3.11**
- **NimBLE-Arduino by h2zero: 2.5.1**
- **LVGL: 8.3.11**
- **ArduinoJson: 7.x**
- Adafruit GFX Library
- Adafruit ILI9341

Remove old NimBLE-Arduino 1.x copies from your Arduino libraries folder before compiling.
The firmware has been ported to the NimBLE 2.x callback and advertising APIs.

## Board settings

```text
Board: ESP32 Dev Module
Tools > Partition Scheme > Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)
```

The sketch also includes its own `partitions.csv` for the Waze HUD OTA layout.

## Why the Bluetooth stack changed

The previous combination of ESP32 core 2.0.17 + NimBLE-Arduino 1.4.3 could enter a
permanent Bluetooth-controller-memory state where `esp_bt_controller_init()` returned
`ESP_ERR_INVALID_STATE` at boot. NimBLE 1.x used `ESP_ERROR_CHECK` there, causing an
abort/reboot loop at 34% STARTING BLUETOOTH.

This build uses ESP32 core 3.3.11 + NimBLE-Arduino 2.5.1. NimBLE 2.x returns a normal
failure from `NimBLEDevice::init()` instead of aborting the ESP32, and the firmware
continues booting without BLE if initialization ever fails.

## Expected boot log

```text
NimBLE-Arduino 2.5.1
NimBLE HLP/1 + Studio advertising as WazeHUD, address: ...
```

There must be no reference to `NimBLEDevice.cpp line 880` from NimBLE-Arduino 1.4.3.


## If Arduino still reports NimBLE 1.x API errors

If errors say that `NimBLEDevice::init()` returns `void`, `getVersion()` does not
exist, callbacks with `NimBLEConnInfo&` do not override, or advertising only has
`setScanResponse()`, Arduino is still compiling NimBLE-Arduino 1.x.

On the affected Windows setup the old library has previously been loaded from:

```text
C:\Users\ledin\1\libraries\NimBLE-Arduino
```

Move or delete that whole folder **outside the `libraries` directory**, restart Arduino
IDE, then install exactly **NimBLE-Arduino 2.5.1** from Library Manager.

The sketch now has a compile-time version gate, so it will produce one clear version
error if the wrong NimBLE library is selected.


## Wi-Fi internal-heap hardening

The firmware now keeps setup fallback in **AP-only** mode and tears the AP down before
each station reconnect. It no longer keeps AP+STA, NimBLE and LVGL allocated together
during reconnect attempts.

It also pre-initializes the newlib stdout/UART locks at boot. This addresses a decoded
crash path where the ESP Wi-Fi task reached `wifi_log -> uart_write -> lock_init_generic`
and aborted because a mutex could not be allocated from the remaining internal heap.

Wi-Fi event callbacks only set flags; Serial/UI work runs from `loop()`. Automatic OTA
and weather HTTPS calls are also gated on internal free heap and largest contiguous block.

Useful diagnostics:

```text
[heap] boot: free=..., largest=...
[heap] STA attempt: free=..., largest=...
[heap] STA connected: free=..., largest=...
```


## AHT10 local sensor

Firmware v1.8.0 supports the AHT10 directly over I2C without any extra Arduino library.

Wiring used by this project:

```text
AHT10 VCC -> 3.3V
AHT10 GND -> GND
AHT10 SDA -> D32 / GPIO32
AHT10 SCL -> D33 / GPIO33
I2C address: 0x38
```

At boot Serial should print:

```text
AHT10 ready: SDA=D32, SCL=D33, address=0x38
AHT10: 27.1 C, 58.0 %RH
```

AHT10 is the preferred temperature source for the standby screen and Web Settings.
Open-Meteo is used only as a fallback if the sensor is absent or stops responding.
