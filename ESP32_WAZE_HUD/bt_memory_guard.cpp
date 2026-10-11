// ESP32 Arduino core 2.0.17 calls a weak btInUse() hook from initArduino()
// before setup(). Returning false permanently releases BTDM controller memory,
// after which NimBLE-Arduino 1.4.3 aborts in esp_bt_controller_init() with
// ESP_ERR_INVALID_STATE. Keep this in a separate .cpp file so Arduino's .ino
// prototype generator cannot reorder application types/functions.
extern "C" bool btInUse(void) {
  return true;
}
