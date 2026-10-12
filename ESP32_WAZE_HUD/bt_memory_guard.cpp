#include <stdint.h>
#include <esp_err.h>
#include <esp_bt.h>

// Arduino-ESP32 2.0.17 may release BT controller RAM from initArduino()
// before setup() if the Bluetooth HAL usage probe resolves false. Once that
// memory is released, esp_bt_controller_init() can never succeed and returns
// ESP_ERR_INVALID_STATE even though esp_bt_controller_get_status() says IDLE.
//
// build_opt.h renames every source-level esp_bt_controller_mem_release() call
// (core + libraries + sketch) to this function for Arduino IDE builds.
// Waze HUD requires BLE for its whole lifetime, so releasing controller RAM is
// never useful here. Keep the memory reserved and report how many release
// attempts were blocked so the serial log proves the guard ran before setup().

extern "C" {
volatile uint32_t wazeBtMemReleaseBlocked = 0;
volatile int32_t wazeBtMemReleaseLastMode = -1;

esp_err_t waze_bt_controller_mem_release(esp_bt_mode_t mode) {
  wazeBtMemReleaseBlocked++;
  wazeBtMemReleaseLastMode = (int32_t)mode;
  return ESP_OK;
}
}
