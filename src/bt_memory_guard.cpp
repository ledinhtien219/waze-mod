// ESP32 Arduino core 2.0.17 calls a weak btInUse() hook from initArduino()
// before setup(). Arduino IDE + NimBLE-Arduino 1.4.3 may not pull esp32-hal-bt.c
// into the final link early enough, so the weak hook resolves false and BTDM
// controller memory is permanently released. PlatformIO already links
// esp32-hal-bt.c and provides its own strong btInUse(), so only provide this
// application hook for non-PlatformIO builds.
#ifndef PLATFORMIO
extern "C" bool btInUse(void) {
  return true;
}
#endif
