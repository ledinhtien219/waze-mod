# Waze HUD Studio BLE Config Protocol

Firmware v1.7.0 adds a Web Bluetooth configuration service without changing the official HLP/1 service used by WazeMod.

## Service

- Service: `8a7e1001-4d6e-4c48-9a9d-484c504c0001`
- Device info (READ): `8a7e1002-4d6e-4c48-9a9d-484c504c0001`
- Settings (READ/WRITE): `8a7e1003-4d6e-4c48-9a9d-484c504c0001`
- Wi-Fi config (WRITE): `8a7e1004-4d6e-4c48-9a9d-484c504c0001`
- Command (WRITE): `8a7e1005-4d6e-4c48-9a9d-484c504c0001`
- Status (READ/NOTIFY): `8a7e1006-4d6e-4c48-9a9d-484c504c0001`
- Live HUD (READ/NOTIFY, firmware v1.7.1+): `8a7e1007-4d6e-4c48-9a9d-484c504c0001`

All payloads are UTF-8 JSON.

Settings uses compact keys to remain small enough for BLE writes. Wi-Fi is `{"ssid":"...","pass":"..."}`.

Commands: `refresh`, `check_update`, `ota`, `reboot`, `defaults`.

Heavy operations are queued and executed from Arduino `loop()`; NimBLE callbacks only copy incoming bytes.


## Live HUD payload

Compact JSON mirrored from the active Waze/HLP/1 state: speed, limit, maneuver, maneuver distance, road, ETA, remaining distance, nearest alert code/distance/value/severity, alert count and next speed limit. Studio keeps v1.7.0 backward compatibility; configuration works without this characteristic, but realtime preview requires v1.7.1+.
