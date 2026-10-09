#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <lvgl.h>
#include <time.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ===== ESP32 DevKit V1 + ILI9341 320x240 =====
#define TFT_CS   27
#define TFT_RST  25
#define TFT_DC   26
#define TFT_MOSI 13
#define TFT_SCK  14
#define TFT_MISO 35

static const char *AP_NAME = "WAZE-HUD";
static const char *AP_PASS = "12345678";
static const char *BLE_DEVICE_NAME = "WazeHUD";
static const char *BLE_SERVICE_UUID = "8a7e0001-4d6e-4c48-9a9d-484c504c0001";
static const char *BLE_TX_UUID      = "8a7e0002-4d6e-4c48-9a9d-484c504c0001";
static const char *BLE_RX_UUID      = "8a7e0003-4d6e-4c48-9a9d-484c504c0001";
static const char *BLE_CAPS_UUID    = "8a7e0004-4d6e-4c48-9a9d-484c504c0001";
static const uint32_t HUD_TIMEOUT_MS = 10000;
static const char *FW_VERSION = "1.6.0";
static const char *GITHUB_REPO = "ledinhtien219/waze-mod";

SPIClass displaySPI(HSPI);
Adafruit_ILI9341 tft(&displaySPI, TFT_DC, TFT_CS, TFT_RST);

// LVGL uses a 16-line partial draw buffer: smooth UI without a full framebuffer.
static lv_disp_draw_buf_t lvDrawBuf;
static lv_color_t *lvBuf1 = nullptr;
static lv_disp_drv_t lvDispDrv;

enum LvUiMode : uint8_t { LVUI_NONE, LVUI_BOOT, LVUI_WAITING, LVUI_HUD, LVUI_OTA };
static LvUiMode lvUiMode = LVUI_NONE;

// Shared canvases keep memory bounded. They are reused by boot/HUD/OTA screens.
static lv_color_t *lvMainCanvasBuf = nullptr;
static lv_color_t *lvLaneCanvasBuf = nullptr;
static lv_color_t *lvAlertCanvasBuf = nullptr;

static lv_obj_t *uiTitle = nullptr;
static lv_obj_t *uiSpeed = nullptr;
static lv_obj_t *uiSpeedUnit = nullptr;
static lv_obj_t *uiLimitCircle = nullptr;
static lv_obj_t *uiLimitText = nullptr;
static lv_obj_t *uiManeuverCanvas = nullptr;
static lv_obj_t *uiDistance = nullptr;
static lv_obj_t *uiEtaCaption = nullptr;
static lv_obj_t *uiEta = nullptr;
static lv_obj_t *uiLaneCanvas = nullptr;
static lv_obj_t *uiRoad = nullptr;
static lv_obj_t *uiAlertCanvas = nullptr;
static lv_obj_t *uiAlertLabel = nullptr;
static lv_obj_t *uiAlertDistance = nullptr;
static lv_obj_t *uiRemainCaption = nullptr;
static lv_obj_t *uiRemain = nullptr;
static lv_obj_t *uiNextCaption = nullptr;
static lv_obj_t *uiNextLimitCircle = nullptr;
static lv_obj_t *uiNextLimitText = nullptr;
static lv_obj_t *uiNextDistance = nullptr;
static lv_obj_t *uiClock = nullptr;
static lv_obj_t *uiBleDot = nullptr;
static lv_obj_t *uiWifiBars[4] = {nullptr, nullptr, nullptr, nullptr};

static lv_obj_t *uiBootStage = nullptr;
static lv_obj_t *uiBootPercent = nullptr;
static lv_obj_t *uiBootBar = nullptr;
static lv_obj_t *uiWaitStatus = nullptr;
static lv_obj_t *uiWaitIp = nullptr;
static lv_obj_t *uiOtaStage = nullptr;
static lv_obj_t *uiOtaPercent = nullptr;
static lv_obj_t *uiOtaBar = nullptr;

WebServer server(80);
Preferences prefs;

String wifiSSID;
String wifiPASS;
String shownIp = "";
volatile bool wifiUiDirty = false;
uint32_t lastWifiRetry = 0;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;
volatile uint8_t lastWifiDisconnectReason = 0;
bool apMode = false;
bool bleConnected = false;
bool bleHlpReady = false;
String bleRxBuffer;
NimBLECharacteristic *bleNotifyCharacteristic = nullptr;
uint32_t lastBleDevNotify = 0;
String bleLocalAddress = "";

struct BleRxChunk {
  uint16_t length;
  uint8_t bytes[256];
};

QueueHandle_t bleRxQueue = nullptr;
volatile uint32_t bleRxDropped = 0;

struct AppSettings {
  bool mirrorHud = false;
  bool nightMode = true;
  bool showRoad = true;
  bool showRoute = true;
  bool showEta = true;
  bool showSpeedLimit = true;
  bool alertPolice = true;
  bool alertCamera = true;
  bool alertCrash = true;
  bool alertTraffic = true;
  bool alertRoadworks = true;
  bool alertHazard = true;
  bool autoUpdateCheck = true;
  uint8_t brightness = 100;
  uint8_t hudStyle = 3; // 0 Balanced, 1 Navigation, 2 Minimal, 3 Full HUD
} settings;

String latestVersion = "";
String latestFirmwareUrl = "";
size_t latestFirmwareSize = 0;
String updateMessage = "Chưa kiểm tra";
bool updateAvailable = false;

volatile bool otaRequested = false;
bool otaInProgress = false;
uint32_t otaRequestedAt = 0;
String otaStatus = "idle";
size_t otaBytesWritten = 0;
size_t otaBytesTotal = 0;
volatile uint8_t otaPercent = 0;
int otaRenderedPercent = -1;
String otaRenderedStage = "";

void loadAppSettings() {
  prefs.begin("wazehud", true);
  settings.mirrorHud = prefs.getBool("mirror", false);
  settings.nightMode = prefs.getBool("night", true);
  settings.showRoad = prefs.getBool("road", true);
  settings.showRoute = prefs.getBool("route", true);
  settings.showEta = prefs.getBool("eta", true);
  settings.showSpeedLimit = prefs.getBool("limit", true);
  settings.alertPolice = prefs.getBool("a_police", true);
  settings.alertCamera = prefs.getBool("a_camera", true);
  settings.alertCrash = prefs.getBool("a_crash", true);
  settings.alertTraffic = prefs.getBool("a_traffic", true);
  settings.alertRoadworks = prefs.getBool("a_work", true);
  settings.alertHazard = prefs.getBool("a_hazard", true);
  settings.autoUpdateCheck = prefs.getBool("autoupdate", true);
  settings.brightness = constrain((int)prefs.getUChar("bright", 100), 20, 100);
  settings.hudStyle = constrain((int)prefs.getUChar("layout", 3), 0, 3);
  bool fullHudMigrated = prefs.getBool("full150", false);
  prefs.end();

  // v1.5.0 switches existing devices to the finalized Full HUD exactly once.
  if (!fullHudMigrated) {
    settings.hudStyle = 3;
    prefs.begin("wazehud", false);
    prefs.putUChar("layout", 3);
    prefs.putBool("full150", true);
    prefs.end();
  }
}


enum TurnType {
  TURN_STRAIGHT,
  TURN_LEFT,
  TURN_RIGHT,
  TURN_SLIGHT_LEFT,
  TURN_SLIGHT_RIGHT,
  TURN_SHARP_LEFT,
  TURN_SHARP_RIGHT,
  TURN_KEEP_LEFT,
  TURN_KEEP_RIGHT,
  TURN_EXIT_LEFT,
  TURN_EXIT_RIGHT,
  TURN_UTURN,
  TURN_ROUNDABOUT,
  TURN_ARRIVE
};

enum AlertType {
  ALERT_NONE,
  ALERT_POLICE,
  ALERT_CAMERA,
  ALERT_CRASH,
  ALERT_TRAFFIC,
  ALERT_ROADWORKS,
  ALERT_POTHOLE,
  ALERT_OBJECT,
  ALERT_CAR_ON_SHOULDER,
  ALERT_BROKEN_LIGHT,
  ALERT_CLOSURE,
  ALERT_BAD_WEATHER,
  ALERT_BLOCKED_LANE,
  ALERT_HIGH_RISK,
  ALERT_ANIMAL
};

// Explicit prototypes keep Arduino IDE's .ino preprocessor from generating
// prototypes that reference TurnType / AlertType before these enums exist.
char foldVietnameseCodepoint(uint32_t cp) {
  switch (cp) {
case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0x103: case 0x1EA1: case 0x1EA3: case 0x1EA5: case 0x1EA7: case 0x1EA9: case 0x1EAB: case 0x1EAD: case 0x1EAF: case 0x1EB1: case 0x1EB3: case 0x1EB5: case 0x1EB7: return 'a';
case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0x102: case 0x1EA0: case 0x1EA2: case 0x1EA4: case 0x1EA6: case 0x1EA8: case 0x1EAA: case 0x1EAC: case 0x1EAE: case 0x1EB0: case 0x1EB2: case 0x1EB4: case 0x1EB6: return 'A';
case 0xE8: case 0xE9: case 0xEA: case 0x1EB9: case 0x1EBB: case 0x1EBD: case 0x1EBF: case 0x1EC1: case 0x1EC3: case 0x1EC5: case 0x1EC7: return 'e';
case 0xC8: case 0xC9: case 0xCA: case 0x1EB8: case 0x1EBA: case 0x1EBC: case 0x1EBE: case 0x1EC0: case 0x1EC2: case 0x1EC4: case 0x1EC6: return 'E';
case 0xEC: case 0xED: case 0x129: case 0x1EC9: case 0x1ECB: return 'i';
case 0xCC: case 0xCD: case 0x128: case 0x1EC8: case 0x1ECA: return 'I';
case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0x1A1: case 0x1ECD: case 0x1ECF: case 0x1ED1: case 0x1ED3: case 0x1ED5: case 0x1ED7: case 0x1ED9: case 0x1EDB: case 0x1EDD: case 0x1EDF: case 0x1EE1: case 0x1EE3: return 'o';
case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0x1A0: case 0x1ECC: case 0x1ECE: case 0x1ED0: case 0x1ED2: case 0x1ED4: case 0x1ED6: case 0x1ED8: case 0x1EDA: case 0x1EDC: case 0x1EDE: case 0x1EE0: case 0x1EE2: return 'O';
case 0xF9: case 0xFA: case 0x169: case 0x1B0: case 0x1EE5: case 0x1EE7: case 0x1EE9: case 0x1EEB: case 0x1EED: case 0x1EEF: case 0x1EF1: return 'u';
case 0xD9: case 0xDA: case 0x168: case 0x1AF: case 0x1EE4: case 0x1EE6: case 0x1EE8: case 0x1EEA: case 0x1EEC: case 0x1EEE: case 0x1EF0: return 'U';
case 0xFD: case 0x1EF3: case 0x1EF5: case 0x1EF7: case 0x1EF9: return 'y';
case 0xDD: case 0x1EF2: case 0x1EF4: case 0x1EF6: case 0x1EF8: return 'Y';
case 0x111: return 'd';
case 0x110: return 'D';
    default: return 0;
  }
}

String normalizeRoadName(const String &input) {
  String out;
  out.reserve(input.length());
  for (size_t i = 0; i < input.length();) {
    uint8_t c = (uint8_t)input[i];
    uint32_t cp = 0;
    size_t used = 1;
    if (c < 0x80) cp = c;
    else if ((c & 0xE0) == 0xC0 && i + 1 < input.length()) {
      cp = ((uint32_t)(c & 0x1F) << 6) | ((uint8_t)input[i + 1] & 0x3F);
      used = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < input.length()) {
      cp = ((uint32_t)(c & 0x0F) << 12) | (((uint8_t)input[i + 1] & 0x3F) << 6) | ((uint8_t)input[i + 2] & 0x3F);
      used = 3;
    }
    if (cp >= 32 && cp <= 126) out += (char)cp;
    else {
      char folded = foldVietnameseCodepoint(cp);
      if (folded) out += folded;
    }
    i += used;
  }
  out.trim();
  return out;
}

TurnType parseTurn(String s);
AlertType parseAlert(String s);
TurnType parseHlpTurn(JsonDocument &doc);
AlertType mapHlpAlert(uint8_t code);
const char* hlpAlertLabel(uint8_t code);
String currentIpString();
bool alertEnabled(AlertType a);
const char* alertLabel(AlertType a);
void drawAlertGlyph(AlertType a, int cx, int cy);
void drawMiniSpeedLimit(int cx, int cy, int limit, int radius);
void drawArrow(TurnType turn, int cx, int cy);
void drawLaneGuidance();
String currentClockText();
void drawBrandMark(int cx, int cy, int r);
void drawBootSplash();
void updateBootProgress(uint8_t percent, const String &stage);
void drawOtaProgressScreen(uint8_t percent, const String &stage, bool reset = false);
void drawHud();
void drawWaiting();
void processBleInput();

struct HudState {
  TurnType turn = TURN_STRAIGHT;
  uint16_t distanceM = 0;
  String road = "";
  int speed = 0;
  int speedLimit = 0;
  bool overSpeed = false;
  int nextSpeedLimit = 0;
  int nextSpeedDistanceM = -1;
  float remainingKm = 0;
  String eta = "--:--";
  String route = "";

  // HLP/1 nearest-alert mirror fields.
  uint8_t alertCode = 0;       // alr
  AlertType alert = ALERT_NONE;
  int alertDistanceM = -1;     // alrD, -1 = none
  int alertValue = -1;         // alrV when applicable
  uint8_t alertSeverity = 0;   // alrS for traffic jam
  int alertDelayMin = -1;      // alrM when Waze provides explicit delay
  uint8_t alertCount = 0;      // alrs[] count when negotiated

  uint32_t updatedAt = 0;
  bool valid = false;
} hud;

HudState renderedHud;
AppSettings renderedSettings;
bool hudRenderValid = false;
bool renderedLinkLost = false;
String renderedMainIp = "";
String renderedClock = "";
bool renderedBleState = false;
bool renderedWifiState = false;
bool ntpConfigured = false;
bool overspeedBorderVisible = false;
uint32_t lastOverspeedBlink = 0;

const uint16_t C_BG      = ILI9341_BLACK;
const uint16_t C_WHITE   = ILI9341_WHITE;
const uint16_t C_BLUE    = 0x05FF;
const uint16_t C_BLUE2   = 0x051F;
const uint16_t C_YELLOW  = ILI9341_YELLOW;
const uint16_t C_RED     = ILI9341_RED;
const uint16_t C_DARK    = 0x0841;
const uint16_t C_PANEL   = 0x0863;
const uint16_t C_GREY    = 0x8410;
const uint16_t C_GREEN   = 0x07E0;
const uint16_t C_CYAN    = 0x07FF;
const uint16_t C_LANE_DIM = 0x2104;
const uint16_t C_LINE_DIM = 0x18E3;

String cleanText(String s) {
  return normalizeRoadName(s);
}

TurnType parseTurn(String s) {
  s.toLowerCase();
  s.replace("-", "_");
  s.replace(" ", "_");

  if (s == "left") return TURN_LEFT;
  if (s == "right") return TURN_RIGHT;
  if (s == "slight_left") return TURN_SLIGHT_LEFT;
  if (s == "slight_right") return TURN_SLIGHT_RIGHT;
  if (s == "sharp_left") return TURN_SHARP_LEFT;
  if (s == "sharp_right") return TURN_SHARP_RIGHT;
  if (s == "keep_left") return TURN_KEEP_LEFT;
  if (s == "keep_right") return TURN_KEEP_RIGHT;
  if (s == "exit_left") return TURN_EXIT_LEFT;
  if (s == "exit_right") return TURN_EXIT_RIGHT;
  if (s == "uturn" || s == "u_turn") return TURN_UTURN;
  if (s == "roundabout") return TURN_ROUNDABOUT;
  if (s == "arrive" || s == "destination") return TURN_ARRIVE;
  return TURN_STRAIGHT;
}

AlertType parseAlert(String s) {
  s.toLowerCase();
  if (s == "police") return ALERT_POLICE;
  if (s == "camera") return ALERT_CAMERA;
  if (s == "crash") return ALERT_CRASH;
  if (s == "traffic") return ALERT_TRAFFIC;
  if (s == "roadworks") return ALERT_ROADWORKS;
  if (s == "pothole") return ALERT_POTHOLE;
  if (s == "object") return ALERT_OBJECT;
  if (s == "car_on_shoulder") return ALERT_CAR_ON_SHOULDER;
  if (s == "broken_light") return ALERT_BROKEN_LIGHT;
  if (s == "closure") return ALERT_CLOSURE;
  if (s == "bad_weather") return ALERT_BAD_WEATHER;
  if (s == "blocked_lane") return ALERT_BLOCKED_LANE;
  if (s == "high_risk") return ALERT_HIGH_RISK;
  if (s == "animal") return ALERT_ANIMAL;
  return ALERT_NONE;
}

void sendHlpLine(const String &line) {
  if (!bleConnected || bleNotifyCharacteristic == nullptr) return;

  String frame = line;
  if (!frame.endsWith("\n")) frame += "\n";

  // 20-byte chunks are valid even when the peer keeps the default ATT MTU 23.
  const size_t chunkSize = 20;
  for (size_t offset = 0; offset < frame.length(); offset += chunkSize) {
    size_t count = min(chunkSize, frame.length() - offset);
    bleNotifyCharacteristic->setValue(
      (uint8_t *)(frame.c_str() + offset),
      count
    );
    bleNotifyCharacteristic->notify();
    delay(2);
  }
}

void sendHlpDev() {
  // Proper HLP/1 receiver declaration. alrs is opt-in; without this field
  // Android only sends the nearest alert mirror (alr/alrD/alrV).
  String dev =
    String("{\"v\":1,\"t\":\"dev\",\"name\":\"WazeHUD-ESP32\",\"fw\":\"") +
    FW_VERSION +
    "\",\"proto\":[1],\"disp\":{\"w\":320,\"h\":240,\"color\":1},"
    "\"can\":[\"speed\",\"limit\",\"turn\",\"street\",\"eta\",\"alerts\"],"
    "\"want\":{\"rate\":4,\"fields\":["
      "\"nav\",\"spd\",\"lim\",\"over\","
      "\"trn\",\"trn2\",\"dst\",\"exit\","
      "\"st\",\"st2\",\"eta\",\"rmin\",\"rm\",\"rkm\","
      "\"alr\",\"alrD\",\"alrV\",\"alrS\",\"alrM\",\"alrs\""
    "]}}";
  sendHlpLine(dev);
}

TurnType parseHlpTurn(JsonDocument &doc) {
  if (!doc["trn"].isNull()) {
    int code = (int)doc["trn"];
    switch (code) {
      case 1:  return TURN_STRAIGHT;
      case 2:  return TURN_LEFT;
      case 3:  return TURN_RIGHT;
      case 4:  return TURN_SLIGHT_LEFT;
      case 5:  return TURN_SLIGHT_RIGHT;
      case 6:  return TURN_SHARP_LEFT;
      case 7:  return TURN_SHARP_RIGHT;
      case 8:
      case 9:  return TURN_UTURN;
      case 10:
      case 11:
      case 12:
      case 19:
      case 20: return TURN_ROUNDABOUT;
      case 13: return TURN_KEEP_LEFT;
      case 14: return TURN_KEEP_RIGHT;
      case 15: return TURN_EXIT_LEFT;
      case 16: return TURN_EXIT_RIGHT;
      case 17: return TURN_ARRIVE;
      default: return hud.turn;
    }
  }

  // Fallback for test payloads / protocol variants.
  const char *keys[] = {"turn", "maneuver", "man", "dir"};
  for (const char *key : keys) {
    if (!doc[key].isNull() && doc[key].is<const char*>()) {
      return parseTurn(String((const char*)doc[key]));
    }
  }
  return hud.turn;
}

AlertType mapHlpAlert(uint8_t code) {
  switch (code) {
    case 0: return ALERT_NONE;
    case 1: return ALERT_POLICE;

    // All fixed/mobile enforcement camera variants.
    case 2: case 3:
    case 40: case 41: case 42: case 43: case 44: case 45: case 46:
      return ALERT_CAMERA;

    case 5: return ALERT_CRASH;
    case 6: return ALERT_TRAFFIC;
    case 7: case 38: return ALERT_CLOSURE;
    case 13: return ALERT_CAR_ON_SHOULDER;
    case 14: return ALERT_ROADWORKS;
    case 15: return ALERT_POTHOLE;
    case 16: case 50: case 51: case 52: case 53: case 54: case 55:
      return ALERT_BAD_WEATHER;
    case 17: return ALERT_BLOCKED_LANE;
    case 47: case 49: return ALERT_ANIMAL;
    case 48: return ALERT_OBJECT;
    case 61: return ALERT_BROKEN_LIGHT;

    // Generic road hazards / restrictions / signs not having a dedicated glyph.
    default: return ALERT_HIGH_RISK;
  }
}

const char* hlpAlertLabel(uint8_t code) {
  switch (code) {
    case 0: return "";
    case 1: return "POLICE";
    case 2: return "SPEED CAM";
    case 3: return "RED LIGHT";
    case 4: return "HAZARD";
    case 5: return "ACCIDENT";
    case 6: return "TRAFFIC";
    case 7: return "CLOSED";
    case 8: return "SPEED DROP";
    case 9: return "NO PASS";
    case 10: return "PASS OK";
    case 11: return "RAILWAY";
    case 12: return "TOLL";
    case 13: return "VEHICLE";
    case 14: return "ROADWORK";
    case 15: return "POTHOLE";
    case 16: return "WEATHER";
    case 17: return "LANE";
    case 18: return "DANGER";
    case 19: return "EXIT";
    case 20: return "REST AREA";
    case 21: return "REST STOP";
    case 22: return "END LIMIT";
    case 23: return "RESIDENTIAL";
    case 24: return "END RESID";
    case 25: return "END BAN";
    case 26: return "NO CAR";
    case 27: return "NO MOTO";
    case 28: return "NO LEFT";
    case 29: return "NO RIGHT";
    case 30: return "NO UTURN";
    case 31: return "NO STRAIGHT";
    case 32: return "STRAIGHT";
    case 33: return "RIGHT ONLY";
    case 34: return "LEFT ONLY";
    case 35: return "CAR LANE";
    case 36: return "MOTO LANE";
    case 37: return "ONE WAY";
    case 38: return "NO ENTRY";
    case 39: return "RESTRICT";
    case 40: return "CAMERA";
    case 41: return "DUMMY CAM";
    case 42: return "SEATBELT";
    case 43: return "DIST CAM";
    case 44: return "BUS CAM";
    case 45: return "NOISE CAM";
    case 46: return "STOP CAM";
    case 47: return "ANIMAL";
    case 48: return "OBJECT";
    case 49: return "ROADKILL";
    case 50: return "FLOOD";
    case 51: return "FOG";
    case 52: return "HAIL";
    case 53: return "SNOW";
    case 54: return "ICE";
    case 55: return "SLIPPERY";
    case 56: return "SPEED BUMP";
    case 57: return "SCHOOL";
    case 58: return "MERGING";
    case 59: return "CURVE";
    case 60: return "FORK";
    case 61: return "BAD LIGHT";
    case 62: return "CYCLIST";
    case 63: return "EMERGENCY";
    case 64: return "SAFETY";
    case 65: return "NO STR/R";
    case 66: return "NO L/U";
    case 67: return "NO STR/L";
    case 68: return "NO L/R";
    case 69: return "CAR NO L/U";
    case 70: return "CAR NO R/U";
    case 71: return "NO R/U";
    case 72: return "CAR NO LEFT";
    case 73: return "CAR NO RIGHT";
    case 74: return "CAR NO UTURN";
    case 75: return "TRAFFIC LIGHT";
    default: return "WARNING";
  }
}

bool applyHudPayload(const String &payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;

  String type = String((const char*)(doc["t"] | ""));

  // HLP/1 keepalive.
  if (type == "ping") {
    sendHlpLine("{\"v\":1,\"t\":\"pong\"}");
    return true;
  }

  // Android acknowledgement after the device declaration.
  if (type == "hi") {
    bleHlpReady = true;
    Serial.println("HLP/1 handshake ready");
    return true;
  }

  // Native HLP/1 state frame from WazeMod.
  if (type == "s") {
    if (!doc["spd"].isNull()) hud.speed = constrain((int)doc["spd"], 0, 299);
    if (!doc["lim"].isNull()) hud.speedLimit = constrain((int)doc["lim"], 0, 199);

    if (!doc["over"].isNull()) {
      hud.overSpeed = ((int)doc["over"]) != 0;
    } else {
      hud.overSpeed = hud.speedLimit > 0 && hud.speed > hud.speedLimit;
    }

    // Next speed limit is encoded as a SPEED_DROP/END_SPEED_RESTRICTION
    // alert in alrs[], not as a standalone state field.
    hud.nextSpeedLimit = 0;
    hud.nextSpeedDistanceM = -1;

    int dst = doc["dst"].isNull() ? -1 : (int)doc["dst"];
    hud.distanceM = dst >= 0 ? constrain(dst, 0, 65000) : 0;

    // st2 is the road after the maneuver; fall back to current street st.
    String nextStreet = String((const char*)(doc["st2"] | ""));
    String currentStreet = String((const char*)(doc["st"] | ""));
    hud.road = nextStreet.length() ? nextStreet : currentStreet;

    hud.eta = String((const char*)(doc["eta"] | ""));

    // rm (metres) is authoritative; rkm is legacy/fallback.
    if (!doc["rm"].isNull()) {
      hud.remainingKm = max(0.0f, (float)((int)doc["rm"]) / 1000.0f);
    } else if (!doc["rkm"].isNull()) {
      hud.remainingKm = max(0.0f, (float)doc["rkm"]);
    }

    hud.turn = parseHlpTurn(doc);

    // Nearest alert mirror fields are baseline HLP/1 fields.
    uint8_t code = doc["alr"].isNull() ? 0 : constrain((int)doc["alr"], 0, 255);
    hud.alertCode = code;
    hud.alert = mapHlpAlert(code);

    int alertDistance = doc["alrD"].isNull() ? -1 : (int)doc["alrD"];
    hud.alertDistanceM = alertDistance >= 0 ? constrain(alertDistance, 0, 65000) : -1;
    hud.alertValue = doc["alrV"].isNull() ? -1 : (int)doc["alrV"];
    hud.alertSeverity = doc["alrS"].isNull() ? 0 : constrain((int)doc["alrS"], 0, 5);
    hud.alertDelayMin = doc["alrM"].isNull() ? -1 : max(-1, (int)doc["alrM"]);

    // Full alert list is opt-in. The nearest mirror above remains authoritative
    // for this compact 320x240 renderer, but expose the count for diagnostics.
    if (doc["alrs"].is<JsonArray>()) {
      JsonArray alerts = doc["alrs"].as<JsonArray>();
      hud.alertCount = min((size_t)255, alerts.size());

      // Find the nearest future speed-limit change anywhere in alrs[].
      // It may be the second/third alert while a camera is nearer.
      for (JsonObject item : alerts) {
        int kind = (int)(item["k"] | 0);
        int value = item["v"].isNull() ? 0 : (int)item["v"];
        if ((kind == 8 || kind == 22) && value > 0 && value != hud.speedLimit) {
          hud.nextSpeedLimit = constrain(value, 1, 199);
          int d = (int)(item["d"] | -1);
          hud.nextSpeedDistanceM = d >= 0 ? constrain(d, 0, 65000) : -1;
          break;
        }
      }

      // Be tolerant of producers where only alrs is present.
      if (hud.alertCode == 0 && !alerts.isNull() && alerts.size() > 0) {
        JsonObject first = alerts[0].as<JsonObject>();
        if (!first.isNull()) {
          hud.alertCode = constrain((int)(first["k"] | 0), 0, 255);
          hud.alert = mapHlpAlert(hud.alertCode);
          int d = (int)(first["d"] | -1);
          hud.alertDistanceM = d >= 0 ? constrain(d, 0, 65000) : -1;
          hud.alertValue = first["v"].isNull() ? -1 : (int)first["v"];
          hud.alertSeverity = first["s"].isNull() ? 0 : constrain((int)first["s"], 0, 5);
          hud.alertDelayMin = first["m"].isNull() ? -1 : (int)first["m"];
        }
      }
    } else {
      hud.alertCount = hud.alertCode ? 1 : 0;
    }

    if (hud.nextSpeedLimit == 0 &&
        (hud.alertCode == 8 || hud.alertCode == 22) &&
        hud.alertValue > 0 && hud.alertValue != hud.speedLimit) {
      hud.nextSpeedLimit = constrain(hud.alertValue, 1, 199);
      hud.nextSpeedDistanceM = hud.alertDistanceM;
    }

    hud.updatedAt = millis();
    hud.valid = true;
    return true;
  }

  // Existing HTTP/test JSON format.
  if (!doc["turn"].isNull()) hud.turn = parseTurn(String((const char*)doc["turn"]));
  if (!doc["distance_m"].isNull()) hud.distanceM = constrain((int)doc["distance_m"], 0, 65000);
  if (!doc["road"].isNull()) hud.road = String((const char*)doc["road"]);
  if (!doc["speed"].isNull()) hud.speed = constrain((int)doc["speed"], 0, 299);
  if (!doc["speed_limit"].isNull()) hud.speedLimit = constrain((int)doc["speed_limit"], 0, 199);
  if (!doc["over"].isNull()) hud.overSpeed = (bool)doc["over"];
  else hud.overSpeed = hud.speedLimit > 0 && hud.speed > hud.speedLimit;
  if (!doc["next_speed_limit"].isNull()) hud.nextSpeedLimit = constrain((int)doc["next_speed_limit"], 0, 199);
  if (!doc["next_speed_distance_m"].isNull()) hud.nextSpeedDistanceM = constrain((int)doc["next_speed_distance_m"], 0, 65000);
  if (!doc["remaining_km"].isNull()) hud.remainingKm = max(0.0f, (float)doc["remaining_km"]);
  if (!doc["eta"].isNull()) hud.eta = String((const char*)doc["eta"]);
  if (!doc["route"].isNull()) hud.route = String((const char*)doc["route"]);

  JsonObject alert = doc["alert"];
  if (!alert.isNull()) {
    if (!alert["type"].isNull()) {
      hud.alert = parseAlert(String((const char*)alert["type"]));
      hud.alertCode = hud.alert == ALERT_NONE ? 0 : 4;
    }
    if (!alert["distance_m"].isNull()) hud.alertDistanceM = constrain((int)alert["distance_m"], 0, 65000);
    hud.alertCount = hud.alert == ALERT_NONE ? 0 : 1;
  }

  hud.updatedAt = millis();
  hud.valid = true;
  return true;
}

bool alertEnabled(AlertType a) {
  switch (a) {
    case ALERT_POLICE: return settings.alertPolice;
    case ALERT_CAMERA: return settings.alertCamera;
    case ALERT_CRASH: return settings.alertCrash;
    case ALERT_TRAFFIC: return settings.alertTraffic;
    case ALERT_ROADWORKS: return settings.alertRoadworks;
    case ALERT_NONE: return false;
    default: return settings.alertHazard;
  }
}

const char* alertLabel(AlertType a) {
  switch (a) {
    case ALERT_POLICE: return "POLICE";
    case ALERT_CAMERA: return "CAMERA";
    case ALERT_CRASH: return "CRASH";
    case ALERT_TRAFFIC: return "TRAFFIC";
    case ALERT_ROADWORKS: return "ROADWORK";
    case ALERT_POTHOLE: return "POTHOLE";
    case ALERT_OBJECT: return "OBJECT";
    case ALERT_CAR_ON_SHOULDER: return "SHOULDER";
    case ALERT_BROKEN_LIGHT: return "BAD LIGHT";
    case ALERT_CLOSURE: return "CLOSED";
    case ALERT_BAD_WEATHER: return "WEATHER";
    case ALERT_BLOCKED_LANE: return "LANE";
    case ALERT_HIGH_RISK: return "HIGH RISK";
    case ALERT_ANIMAL: return "ANIMAL";
    default: return "";
  }
}


String currentIpString() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (apMode) return WiFi.softAPIP().toString();
  return "--";
}

String formatDistance(int m) {
  if (m < 0) return "--";
  if (m < 1000) return String(m) + " m";
  return String(m / 1000.0f, 1) + " km";
}

String compactDistance(int m) {
  if (m < 0) return "";
  if (m < 1000) return String(m) + "m";
  if (m < 10000) return String(m / 1000.0f, 1) + "km";
  return String(m / 1000) + "km";
}

const char* maneuverInstruction(TurnType turn) {
  switch (turn) {
    case TURN_LEFT: return "Re trai";
    case TURN_RIGHT: return "Re phai";
    case TURN_SLIGHT_LEFT: return "Chech trai";
    case TURN_SLIGHT_RIGHT: return "Chech phai";
    case TURN_SHARP_LEFT: return "Cua gap trai";
    case TURN_SHARP_RIGHT: return "Cua gap phai";
    case TURN_KEEP_LEFT: return "Giu trai";
    case TURN_KEEP_RIGHT: return "Giu phai";
    case TURN_EXIT_LEFT: return "Ra loi trai";
    case TURN_EXIT_RIGHT: return "Ra loi phai";
    case TURN_UTURN: return "Quay dau";
    case TURN_ROUNDABOUT: return "Vao vong xuyen";
    case TURN_ARRIVE: return "Den noi";
    default: return "Tiep tuc di thang";
  }
}

String currentClockText() {
  struct tm ti;
  if (getLocalTime(&ti, 5)) {
    char buf[6];
    strftime(buf, sizeof(buf), "%H:%M", &ti);
    return String(buf);
  }
  return "--:--";
}

bool turnIsLeft(TurnType t) {
  return t == TURN_LEFT || t == TURN_SLIGHT_LEFT || t == TURN_SHARP_LEFT ||
         t == TURN_KEEP_LEFT || t == TURN_EXIT_LEFT || t == TURN_UTURN;
}

bool turnIsRight(TurnType t) {
  return t == TURN_RIGHT || t == TURN_SLIGHT_RIGHT || t == TURN_SHARP_RIGHT ||
         t == TURN_KEEP_RIGHT || t == TURN_EXIT_RIGHT;
}

// ---------- LVGL display bridge ----------
static inline lv_color_t lc(uint32_t rgb) {
  return lv_color_hex(rgb);
}

void lvDisplayFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *colorP) {
  uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.writePixels(reinterpret_cast<uint16_t *>(colorP), w * h, true, false);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

void lvUiInit() {
  lvBuf1 = (lv_color_t*)malloc(sizeof(lv_color_t) * 320 * 16);
  lvMainCanvasBuf = (lv_color_t*)malloc(sizeof(lv_color_t) * 72 * 72);
  lvLaneCanvasBuf = (lv_color_t*)malloc(sizeof(lv_color_t) * 140 * 58);
  lvAlertCanvasBuf = (lv_color_t*)malloc(sizeof(lv_color_t) * 44 * 44);

  if (!lvBuf1 || !lvMainCanvasBuf || !lvLaneCanvasBuf || !lvAlertCanvasBuf) {
    Serial.println("FATAL: LVGL buffer allocation failed");
    delay(1000);
    ESP.restart();
  }

  lv_init();

  lv_disp_draw_buf_init(&lvDrawBuf, lvBuf1, nullptr, 320 * 16);
  lv_disp_drv_init(&lvDispDrv);
  lvDispDrv.hor_res = 320;
  lvDispDrv.ver_res = 240;
  lvDispDrv.flush_cb = lvDisplayFlush;
  lvDispDrv.draw_buf = &lvDrawBuf;
  lvDispDrv.antialiasing = 1;
  lv_disp_drv_register(&lvDispDrv);

  lv_obj_set_style_bg_color(lv_scr_act(), lc(0x050B16), 0);
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
  lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
}

void lvUiPump() {
  lv_timer_handler();
}

void lvUiClear(LvUiMode mode) {
  lv_obj_clean(lv_scr_act());
  lv_obj_set_style_bg_color(lv_scr_act(), lc(0x050B16), 0);
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(lv_scr_act(), 0, 0);
  lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
  lvUiMode = mode;

  uiTitle = uiSpeed = uiSpeedUnit = nullptr;
  uiLimitCircle = uiLimitText = nullptr;
  uiManeuverCanvas = uiDistance = nullptr;
  uiEtaCaption = uiEta = nullptr;
  uiLaneCanvas = uiRoad = nullptr;
  uiAlertCanvas = uiAlertLabel = uiAlertDistance = nullptr;
  uiRemainCaption = uiRemain = nullptr;
  uiNextCaption = uiNextLimitCircle = uiNextLimitText = uiNextDistance = nullptr;
  uiClock = uiBleDot = nullptr;
  for (int i = 0; i < 4; ++i) uiWifiBars[i] = nullptr;

  uiBootStage = uiBootPercent = uiBootBar = nullptr;
  uiWaitStatus = uiWaitIp = nullptr;
  uiOtaStage = uiOtaPercent = uiOtaBar = nullptr;
}

lv_obj_t* makeLabel(lv_obj_t *parent, int x, int y, int w, int h,
                    const lv_font_t *font, lv_color_t color,
                    lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
  lv_obj_t *o = lv_label_create(parent);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_text_font(o, font, 0);
  lv_obj_set_style_text_color(o, color, 0);
  lv_obj_set_style_text_align(o, align, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
  lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
  return o;
}

void setHidden(lv_obj_t *o, bool hidden) {
  if (!o) return;
  if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* makeLineRect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, color, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

void canvasLine(lv_obj_t *canvas, int x1, int y1, int x2, int y2,
                lv_color_t color, uint8_t width) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = color;
  d.width = width;
  d.round_start = 1;
  d.round_end = 1;
  lv_point_t p[2] = {{(lv_coord_t)x1,(lv_coord_t)y1},{(lv_coord_t)x2,(lv_coord_t)y2}};
  lv_canvas_draw_line(canvas, p, 2, &d);
}

void canvasCircle(lv_obj_t *canvas, int cx, int cy, int r,
                  lv_color_t color, uint8_t width) {
  lv_draw_arc_dsc_t d;
  lv_draw_arc_dsc_init(&d);
  d.color = color;
  d.width = width;
  d.rounded = 1;
  lv_canvas_draw_arc(canvas, cx, cy, r, 0, 360, &d);
}

void canvasRect(lv_obj_t *canvas, int x, int y, int w, int h,
                lv_color_t bg, lv_color_t border, uint8_t borderW, uint8_t radius) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = bg;
  d.bg_opa = LV_OPA_COVER;
  d.border_color = border;
  d.border_width = borderW;
  d.radius = radius;
  lv_canvas_draw_rect(canvas, x, y, w, h, &d);
}

void drawBrandOnCanvas(lv_obj_t *canvas, int w, int h) {
  lv_canvas_fill_bg(canvas, lc(0x050B16), LV_OPA_COVER);
  int cx = w / 2;
  int cy = h / 2;

  canvasRect(canvas, 5, 5, w - 10, h - 10, lc(0x0B1622), lc(0x14D9FF), 2, 14);
  canvasLine(canvas, cx - 17, cy + 17, cx - 7, cy - 12, lc(0xF4F8FF), 3);
  canvasLine(canvas, cx + 17, cy + 17, cx + 7, cy - 12, lc(0xF4F8FF), 3);
  canvasLine(canvas, cx, cy + 15, cx, cy - 10, lc(0x29D9FF), 6);
  canvasLine(canvas, cx, cy - 12, cx - 9, cy - 2, lc(0x29D9FF), 5);
  canvasLine(canvas, cx, cy - 12, cx + 9, cy - 2, lc(0x29D9FF), 5);
}

void drawTurnOnCanvas(lv_obj_t *canvas, TurnType turn, bool small = false) {
  int w = small ? 34 : 72;
  int h = small ? 48 : 72;
  lv_canvas_fill_bg(canvas, lc(0x050B16), LV_OPA_COVER);

  lv_color_t glow = lc(0x083D66);
  lv_color_t cyan = lc(0x38DFFF);
  int cx = w / 2;
  int bottom = h - 9;
  int top = 10;
  uint8_t wide = small ? 5 : 10;
  uint8_t crisp = small ? 3 : 6;

  auto line2 = [&](int x1,int y1,int x2,int y2) {
    canvasLine(canvas, x1,y1,x2,y2,glow,wide);
    canvasLine(canvas, x1,y1,x2,y2,cyan,crisp);
  };
  auto arrowHead = [&](int tx,int ty,int bx1,int by1,int bx2,int by2) {
    line2(tx,ty,bx1,by1);
    line2(tx,ty,bx2,by2);
  };

  if (turn == TURN_STRAIGHT || turn == TURN_ARRIVE) {
    line2(cx,bottom,cx,top+10);
    arrowHead(cx,top,cx-9,top+11,cx+9,top+11);
    return;
  }

  if (turn == TURN_LEFT || turn == TURN_RIGHT ||
      turn == TURN_SHARP_LEFT || turn == TURN_SHARP_RIGHT) {
    int dir = (turn == TURN_RIGHT || turn == TURN_SHARP_RIGHT) ? 1 : -1;
    int elbowY = small ? 25 : 36;
    int tipX = cx + dir * (small ? 13 : 25);
    line2(cx,bottom,cx,elbowY);
    line2(cx,elbowY,tipX,elbowY);
    arrowHead(tipX,elbowY,tipX-dir*8,elbowY-8,tipX-dir*8,elbowY+8);
    return;
  }

  if (turn == TURN_SLIGHT_LEFT || turn == TURN_SLIGHT_RIGHT ||
      turn == TURN_KEEP_LEFT || turn == TURN_KEEP_RIGHT ||
      turn == TURN_EXIT_LEFT || turn == TURN_EXIT_RIGHT) {
    int dir = (turn == TURN_SLIGHT_RIGHT || turn == TURN_KEEP_RIGHT || turn == TURN_EXIT_RIGHT) ? 1 : -1;
    int tipX = cx + dir * (small ? 13 : 23);
    int tipY = top + 3;
    line2(cx,bottom,cx,bottom-18);
    line2(cx,bottom-18,tipX,tipY);
    arrowHead(tipX,tipY,tipX-dir*8,tipY+2,tipX-dir*2,tipY+9);
    return;
  }

  if (turn == TURN_UTURN) {
    int r = small ? 9 : 16;
    int ccy = small ? 24 : 34;
    canvasCircle(canvas, cx, ccy, r, glow, wide);
    canvasCircle(canvas, cx, ccy, r, cyan, crisp);
    line2(cx+r,bottom,cx+r,ccy);
    int tx = cx-r-2;
    arrowHead(tx,ccy,tx+7,ccy-7,tx+7,ccy+7);
    return;
  }

  if (turn == TURN_ROUNDABOUT) {
    int r = small ? 10 : 18;
    canvasCircle(canvas, cx, h/2, r, glow, wide);
    canvasCircle(canvas, cx, h/2, r, cyan, crisp);
    int tx = cx+r+5, ty = h/2;
    arrowHead(tx,ty,tx-7,ty-7,tx-7,ty+7);
    return;
  }

  line2(cx,bottom,cx,top+10);
  arrowHead(cx,top,cx-9,top+11,cx+9,top+11);
}

void drawLaneOnCanvas() {
  lv_canvas_fill_bg(uiLaneCanvas, lc(0x050B16), LV_OPA_COVER);

  TurnType lanes[4] = {TURN_STRAIGHT, TURN_STRAIGHT, TURN_STRAIGHT, TURN_RIGHT};
  int active = 1;
  if (turnIsLeft(hud.turn)) {
    lanes[0] = hud.turn;
    active = 0;
  } else if (turnIsRight(hud.turn)) {
    lanes[3] = hud.turn;
    active = 3;
  } else {
    lanes[1] = hud.turn;
    active = 1;
  }

  const int laneCx[4] = {16, 52, 88, 124};
  lv_color_t dim = lc(0x273348);
  lv_color_t divider = lc(0x1A2838);
  lv_color_t cyan = lc(0x38DFFF);
  lv_color_t glow = lc(0x0B5078);

  for (int i = 0; i < 3; ++i) {
    int x = 34 + i * 36;
    for (int y = 7; y < 55; y += 11) canvasLine(uiLaneCanvas, x,y,x,y+5,divider,1);
  }

  auto laneArrow = [&](int idx, TurnType turn, bool on) {
    int cx = laneCx[idx];
    int bottom = 51;
    int top = 7;
    lv_color_t c = on ? cyan : dim;
    lv_color_t g = on ? glow : dim;
    uint8_t w1 = on ? 7 : 4;
    uint8_t w2 = on ? 4 : 3;

    auto ln = [&](int x1,int y1,int x2,int y2) {
      canvasLine(uiLaneCanvas,x1,y1,x2,y2,g,w1);
      canvasLine(uiLaneCanvas,x1,y1,x2,y2,c,w2);
    };
    auto head = [&](int tx,int ty,int bx1,int by1,int bx2,int by2) {
      ln(tx,ty,bx1,by1); ln(tx,ty,bx2,by2);
    };

    if (turnIsLeft(turn) && turn != TURN_UTURN) {
      ln(cx+4,bottom,cx+4,28);
      ln(cx+4,28,cx-10,28);
      head(cx-13,28,cx-6,21,cx-6,35);
    } else if (turnIsRight(turn)) {
      ln(cx-4,bottom,cx-4,28);
      ln(cx-4,28,cx+10,28);
      head(cx+13,28,cx+6,21,cx+6,35);
    } else if (turn == TURN_UTURN) {
      canvasCircle(uiLaneCanvas,cx,27,9,g,w1);
      canvasCircle(uiLaneCanvas,cx,27,9,c,w2);
      ln(cx+9,bottom,cx+9,27);
      head(cx-11,27,cx-4,20,cx-4,34);
    } else {
      ln(cx,bottom,cx,top+10);
      head(cx,top,cx-7,top+9,cx+7,top+9);
    }
  };

  for (int i = 0; i < 4; ++i) if (i != active) laneArrow(i, lanes[i], false);
  laneArrow(active, lanes[active], true);
}

void drawAlertOnCanvas() {
  lv_canvas_fill_bg(uiAlertCanvas, lc(0x050B16), LV_OPA_COVER);
  uint8_t code = hud.alertCode;
  if (code == 0 || !alertEnabled(hud.alert)) return;

  lv_color_t white = lc(0xF4F8FF);
  lv_color_t cyan = lc(0x39DFFF);
  lv_color_t red = lc(0xFF3B30);
  lv_color_t yellow = lc(0xFFD54A);
  lv_color_t blue = lc(0x268DFF);
  lv_color_t orange = lc(0xFF8A30);

  int cx = 22, cy = 22;

  // Camera family.
  if (code == 2 || (code >= 40 && code <= 46)) {
    canvasRect(uiAlertCanvas, 5, 12, 31, 21, blue, white, 2, 5);
    canvasRect(uiAlertCanvas, 11, 8, 11, 5, blue, white, 1, 2);
    canvasCircle(uiAlertCanvas, cx, cy+1, 7, white, 2);
    canvasCircle(uiAlertCanvas, cx, cy+1, 3, cyan, 2);
    return;
  }

  // Red-light camera / traffic light.
  if (code == 3 || code == 75 || code == 61) {
    canvasRect(uiAlertCanvas, 12, 4, 20, 36, lc(0x18212D), white, 2, 5);
    canvasCircle(uiAlertCanvas, 22, 12, 4, red, 4);
    canvasCircle(uiAlertCanvas, 22, 22, 4, yellow, 4);
    canvasCircle(uiAlertCanvas, 22, 32, 4, lc(0x35E68A), 4);
    if (code == 61) {
      canvasLine(uiAlertCanvas, 8,7,36,37,red,3);
      canvasLine(uiAlertCanvas, 36,7,8,37,red,3);
    }
    return;
  }

  // Police.
  if (code == 1) {
    canvasRect(uiAlertCanvas, 6, 7, 32, 30, blue, white, 2, 12);
    canvasLine(uiAlertCanvas, 11,14,33,14,white,4);
    canvasCircle(uiAlertCanvas, 22, 24, 7, white, 2);
    canvasLine(uiAlertCanvas, 16,34,28,34,white,4);
    return;
  }

  // Crash.
  if (code == 5) {
    canvasRect(uiAlertCanvas, 3, 19, 18, 12, red, white, 1, 4);
    canvasRect(uiAlertCanvas, 23, 13, 18, 12, orange, white, 1, 4);
    canvasLine(uiAlertCanvas, 18,10,24,17,yellow,3);
    canvasLine(uiAlertCanvas, 24,10,18,17,yellow,3);
    return;
  }

  // Traffic jam.
  if (code == 6) {
    for (int i=0;i<3;i++) canvasRect(uiAlertCanvas, 7, 8+i*10, 30, 6, orange, orange, 0, 3);
    return;
  }

  // Roadwork.
  if (code == 14) {
    canvasLine(uiAlertCanvas, 6,36,22,7,yellow,4);
    canvasLine(uiAlertCanvas, 22,7,38,36,yellow,4);
    canvasLine(uiAlertCanvas, 6,36,38,36,yellow,4);
    canvasCircle(uiAlertCanvas, 21,17,3,white,3);
    canvasLine(uiAlertCanvas, 21,20,17,31,white,3);
    canvasLine(uiAlertCanvas, 21,22,30,28,white,3);
    return;
  }

  // Pothole / object / animal / weather / lane / generic hazard.
  if (code == 15) {
    canvasLine(uiAlertCanvas, 5,13,39,13,white,2);
    canvasLine(uiAlertCanvas, 5,13,12,34,orange,3);
    canvasLine(uiAlertCanvas, 12,34,22,25,orange,3);
    canvasLine(uiAlertCanvas, 22,25,31,35,orange,3);
    canvasLine(uiAlertCanvas, 31,35,39,13,orange,3);
    return;
  }
  if (code == 47 || code == 49) {
    canvasCircle(uiAlertCanvas, 15,22,6,orange,4);
    canvasCircle(uiAlertCanvas, 28,22,6,orange,4);
    canvasLine(uiAlertCanvas, 12,14,8,6,orange,3);
    canvasLine(uiAlertCanvas, 31,14,35,6,orange,3);
    return;
  }
  if (code == 16 || (code >= 50 && code <= 55)) {
    canvasCircle(uiAlertCanvas, 14,18,6,white,5);
    canvasCircle(uiAlertCanvas, 23,14,8,white,5);
    canvasCircle(uiAlertCanvas, 31,19,5,white,5);
    canvasLine(uiAlertCanvas, 10,31,7,38,cyan,2);
    canvasLine(uiAlertCanvas, 21,31,18,38,cyan,2);
    canvasLine(uiAlertCanvas, 32,31,29,38,cyan,2);
    return;
  }

  // Restrictions / closure.
  if (code == 7 || code == 38 || (code >= 25 && code <= 39) || (code >= 65 && code <= 74)) {
    canvasCircle(uiAlertCanvas, cx, cy, 17, red, 4);
    canvasLine(uiAlertCanvas, 10,34,34,10,red,4);
    return;
  }

  // Generic warning triangle.
  canvasLine(uiAlertCanvas, 22,5,5,37,yellow,4);
  canvasLine(uiAlertCanvas, 5,37,39,37,yellow,4);
  canvasLine(uiAlertCanvas, 39,37,22,5,yellow,4);
  canvasLine(uiAlertCanvas, 22,16,22,27,white,3);
  canvasCircle(uiAlertCanvas,22,32,1,white,2);
}

void buildStatusDots(lv_obj_t *parent) {
  uiBleDot = lv_obj_create(parent);
  lv_obj_remove_style_all(uiBleDot);
  lv_obj_set_pos(uiBleDot, 276, 10);
  lv_obj_set_size(uiBleDot, 7, 7);
  lv_obj_set_style_radius(uiBleDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(uiBleDot, LV_OPA_COVER, 0);

  for (int i=0;i<4;i++) {
    uiWifiBars[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(uiWifiBars[i]);
    lv_obj_set_pos(uiWifiBars[i], 292 + i*6, 18 - (5+i*3));
    lv_obj_set_size(uiWifiBars[i], 4, 5+i*3);
    lv_obj_set_style_bg_opa(uiWifiBars[i], LV_OPA_COVER, 0);
  }
}

void refreshStatusDots() {
  lv_color_t bleC = bleConnected ? lc(0x39E889) : lc(0x273348);
  lv_color_t wifiC = WiFi.status() == WL_CONNECTED ? lc(0x39E889) : lc(0x273348);
  if (uiBleDot) lv_obj_set_style_bg_color(uiBleDot, bleC, 0);
  for (int i=0;i<4;i++) if (uiWifiBars[i]) lv_obj_set_style_bg_color(uiWifiBars[i], wifiC, 0);
}

void ensureHudScreen() {
  if (lvUiMode == LVUI_HUD) return;
  lvUiClear(LVUI_HUD);
  lv_obj_t *root = lv_scr_act();

  makeLineRect(root, 6, 35, 308, 1, lc(0x1A2838));
  makeLineRect(root, 94, 42, 1, 174, lc(0x1A2838));
  makeLineRect(root, 242, 42, 1, 174, lc(0x1A2838));
  makeLineRect(root, 6, 216, 308, 1, lc(0x1A2838));
  makeLineRect(root, 246, 108, 70, 1, lc(0x1A2838));
  makeLineRect(root, 246, 153, 70, 1, lc(0x1A2838));

  uiTitle = makeLabel(root, 8, 4, 258, 28, &lv_font_montserrat_18, lc(0xF5F8FF));

  buildStatusDots(root);

  uiManeuverCanvas = lv_canvas_create(root);
  lv_canvas_set_buffer(uiManeuverCanvas, lvMainCanvasBuf, 72, 72, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(uiManeuverCanvas, 11, 42);

  uiDistance = makeLabel(root, 4, 115, 86, 24, &lv_font_montserrat_18, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);
  uiEtaCaption = makeLabel(root, 8, 145, 78, 18, &lv_font_montserrat_12, lc(0x5FE9FF));
  uiEta = makeLabel(root, 8, 164, 82, 26, &lv_font_montserrat_18, lc(0xF5F8FF));

  uiSpeed = makeLabel(root, 100, 43, 76, 53, &lv_font_montserrat_40, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);
  uiSpeedUnit = makeLabel(root, 105, 99, 64, 18, &lv_font_montserrat_12, lc(0x8C9CB0), LV_TEXT_ALIGN_CENTER);

  uiLimitCircle = lv_obj_create(root);
  lv_obj_remove_style_all(uiLimitCircle);
  lv_obj_set_pos(uiLimitCircle, 176, 47);
  lv_obj_set_size(uiLimitCircle, 60, 60);
  lv_obj_set_style_radius(uiLimitCircle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(uiLimitCircle, lc(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(uiLimitCircle, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(uiLimitCircle, lc(0xFF3B30), 0);
  lv_obj_set_style_border_width(uiLimitCircle, 6, 0);
  uiLimitText = makeLabel(uiLimitCircle, 0, 14, 60, 32, &lv_font_montserrat_24, lc(0x101820), LV_TEXT_ALIGN_CENTER);

  uiLaneCanvas = lv_canvas_create(root);
  lv_canvas_set_buffer(uiLaneCanvas, lvLaneCanvasBuf, 140, 58, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(uiLaneCanvas, 98, 147);

  uiRoad = makeLabel(root, 96, 215, 145, 23, &lv_font_montserrat_14, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);

  uiAlertCanvas = lv_canvas_create(root);
  lv_canvas_set_buffer(uiAlertCanvas, lvAlertCanvasBuf, 44, 44, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(uiAlertCanvas, 259, 40);
  uiAlertLabel = makeLabel(root, 246, 84, 70, 17, &lv_font_montserrat_12, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);
  uiAlertDistance = makeLabel(root, 246, 99, 70, 20, &lv_font_montserrat_14, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);

  uiRemainCaption = makeLabel(root, 248, 114, 64, 16, &lv_font_montserrat_12, lc(0x8C9CB0), LV_TEXT_ALIGN_CENTER);
  uiRemain = makeLabel(root, 246, 132, 70, 20, &lv_font_montserrat_14, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);

  uiNextCaption = makeLabel(root, 248, 157, 64, 15, &lv_font_montserrat_12, lc(0x8C9CB0), LV_TEXT_ALIGN_CENTER);
  uiNextLimitCircle = lv_obj_create(root);
  lv_obj_remove_style_all(uiNextLimitCircle);
  lv_obj_set_pos(uiNextLimitCircle, 270, 170);
  lv_obj_set_size(uiNextLimitCircle, 42, 42);
  lv_obj_set_style_radius(uiNextLimitCircle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(uiNextLimitCircle, lc(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(uiNextLimitCircle, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(uiNextLimitCircle, lc(0xFF3B30), 0);
  lv_obj_set_style_border_width(uiNextLimitCircle, 4, 0);
  uiNextLimitText = makeLabel(uiNextLimitCircle, 0, 10, 42, 22, &lv_font_montserrat_14, lc(0x101820), LV_TEXT_ALIGN_CENTER);
  uiNextDistance = makeLabel(root, 246, 211, 70, 18, &lv_font_montserrat_12, lc(0x8C9CB0), LV_TEXT_ALIGN_CENTER);

  uiClock = makeLabel(root, 245, 219, 71, 20, &lv_font_montserrat_18, lc(0xF5F8FF), LV_TEXT_ALIGN_RIGHT);

  lv_label_set_text(uiEtaCaption, "ETA");
  lv_label_set_text(uiSpeedUnit, "km/h");
  lv_label_set_text(uiRemainCaption, "LEFT");
  lv_label_set_text(uiNextCaption, "NEXT");
}

void drawHud() {
  ensureHudScreen();

  lv_label_set_text(uiTitle, maneuverInstruction(hud.turn));

  String spd = String(max(0, hud.speed));
  lv_label_set_text(uiSpeed, spd.c_str());
  lv_obj_set_style_text_color(uiSpeed, hud.overSpeed ? lc(0xFF453A) : lc(0xF5F8FF), 0);

  String limit = hud.speedLimit > 0 ? String(hud.speedLimit) : "--";
  lv_label_set_text(uiLimitText, limit.c_str());
  setHidden(uiLimitCircle, !(settings.showSpeedLimit && hud.speedLimit > 0));

  drawTurnOnCanvas(uiManeuverCanvas, hud.turn, false);
  String distance = formatDistance(hud.distanceM);
  lv_label_set_text(uiDistance, distance.c_str());

  String eta = settings.showEta && hud.eta.length() ? hud.eta : "--:--";
  lv_label_set_text(uiEta, eta.c_str());

  drawLaneOnCanvas();

  String road = settings.showRoad ? normalizeRoadName(hud.road) : "";
  if (!road.length() && settings.showRoute) road = cleanText(hud.route);
  if (road.length() > 20) road = road.substring(0, 20);
  lv_label_set_text(uiRoad, road.c_str());

  drawAlertOnCanvas();
  bool showAlert = hud.alertCode != 0 && alertEnabled(hud.alert);
  setHidden(uiAlertCanvas, !showAlert);
  setHidden(uiAlertLabel, !showAlert);
  setHidden(uiAlertDistance, !showAlert);
  if (showAlert) {
    String al = String(hlpAlertLabel(hud.alertCode));
    if (al.length() > 10) al = al.substring(0, 10);
    lv_label_set_text(uiAlertLabel, al.c_str());
    String ad = formatDistance(hud.alertDistanceM);
    lv_label_set_text(uiAlertDistance, ad.c_str());
  }

  lv_label_set_text(uiRemain, (String(hud.remainingKm, 1) + " km").c_str());

  int nextLimit = hud.nextSpeedLimit > 0 ? hud.nextSpeedLimit : 0;
  bool showNext = settings.showSpeedLimit && nextLimit > 0 && nextLimit != hud.speedLimit;
  setHidden(uiNextCaption, !showNext);
  setHidden(uiNextLimitCircle, !showNext);
  setHidden(uiNextDistance, !showNext);
  if (showNext) {
    String ns = String(nextLimit);
    lv_label_set_text(uiNextLimitText, ns.c_str());
    String nd = compactDistance(hud.nextSpeedDistanceM);
    lv_label_set_text(uiNextDistance, nd.c_str());
  }

  String clk = currentClockText();
  lv_label_set_text(uiClock, clk.c_str());

  refreshStatusDots();
  lvUiPump();
}

void buildBrandScreen(LvUiMode mode) {
  lvUiClear(mode);
  lv_obj_t *root = lv_scr_act();

  lv_obj_t *logo = lv_canvas_create(root);
  lv_canvas_set_buffer(logo, lvMainCanvasBuf, 72, 72, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(logo, 124, 24);
  drawBrandOnCanvas(logo, 72, 72);

  lv_obj_t *name = makeLabel(root, 0, 101, 320, 33, &lv_font_montserrat_24, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(name, "WAZE HUD");

  lv_obj_t *sub = makeLabel(root, 0, 134, 320, 20, &lv_font_montserrat_12, lc(0x7B8DA3), LV_TEXT_ALIGN_CENTER);
  lv_label_set_text(sub, "SMART NAV DISPLAY");
}

void drawBootSplash() {
  buildBrandScreen(LVUI_BOOT);

  uiBootStage = makeLabel(lv_scr_act(), 0, 160, 320, 20, &lv_font_montserrat_12, lc(0x8C9CB0), LV_TEXT_ALIGN_CENTER);
  uiBootBar = lv_bar_create(lv_scr_act());
  lv_obj_set_pos(uiBootBar, 38, 190);
  lv_obj_set_size(uiBootBar, 244, 12);
  lv_obj_set_style_bg_color(uiBootBar, lc(0x172332), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(uiBootBar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(uiBootBar, 6, LV_PART_MAIN);
  lv_obj_set_style_bg_color(uiBootBar, lc(0x27DFFF), LV_PART_INDICATOR);
  lv_obj_set_style_radius(uiBootBar, 6, LV_PART_INDICATOR);
  lv_bar_set_range(uiBootBar, 0, 100);

  uiBootPercent = makeLabel(lv_scr_act(), 0, 207, 320, 22, &lv_font_montserrat_14, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);

  lv_obj_t *ver = makeLabel(lv_scr_act(), 10, 224, 100, 15, &lv_font_montserrat_12, lc(0x5F7187));
  lv_label_set_text(ver, ("v" + String(FW_VERSION)).c_str());

  updateBootProgress(5, "POWERING UP");
}

void updateBootProgress(uint8_t percent, const String &stage) {
  if (lvUiMode != LVUI_BOOT || !uiBootBar) drawBootSplash();
  percent = constrain((int)percent, 0, 100);
  lv_bar_set_value(uiBootBar, percent, LV_ANIM_OFF);
  lv_label_set_text(uiBootStage, stage.c_str());
  lv_label_set_text(uiBootPercent, (String(percent) + "%").c_str());
  lv_obj_set_style_text_color(uiBootStage, percent >= 100 ? lc(0x39E889) : lc(0x8C9CB0), 0);
  lv_obj_set_style_bg_color(uiBootBar, percent >= 100 ? lc(0x39E889) : lc(0x27DFFF), LV_PART_INDICATOR);
  lvUiPump();
}

void drawWaiting() {
  if (lvUiMode != LVUI_WAITING) {
    buildBrandScreen(LVUI_WAITING);
    uiWaitStatus = makeLabel(lv_scr_act(), 0, 163, 320, 22, &lv_font_montserrat_14, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);
    uiWaitIp = makeLabel(lv_scr_act(), 0, 190, 320, 24, &lv_font_montserrat_18, lc(0x28DFFF), LV_TEXT_ALIGN_CENTER);
    lv_obj_t *hint = makeLabel(lv_scr_act(), 0, 217, 320, 18, &lv_font_montserrat_12, lc(0x7B8DA3), LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(hint, "BLE: WazeHUD  |  Web setup");
  }

  String status;
  if (bleConnected) status = "BLE CONNECTED";
  else if (WiFi.status() == WL_CONNECTED) status = "WAITING FOR WAZEMOD";
  else if (apMode) status = "SETUP ACCESS POINT";
  else status = "CONNECTING";

  lv_label_set_text(uiWaitStatus, status.c_str());
  String ip = currentIpString();
  lv_label_set_text(uiWaitIp, ip.c_str());
  lvUiPump();
  wifiUiDirty = false;
}

void drawOtaProgressScreen(uint8_t percent, const String &stage, bool reset) {
  if (reset || lvUiMode != LVUI_OTA) {
    lvUiClear(LVUI_OTA);

    lv_obj_t *logo = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(logo, lvMainCanvasBuf, 72, 72, LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(logo, 16, 20);
    drawBrandOnCanvas(logo, 72, 72);

    lv_obj_t *title = makeLabel(lv_scr_act(), 96, 25, 210, 28, &lv_font_montserrat_18, lc(0xF5F8FF));
    lv_label_set_text(title, "SYSTEM UPDATE");

    lv_obj_t *ver = makeLabel(lv_scr_act(), 96, 58, 210, 20, &lv_font_montserrat_12, lc(0x7B8DA3));
    lv_label_set_text(ver, ("v" + String(FW_VERSION) + "  >  v" + latestVersion).c_str());

    uiOtaStage = makeLabel(lv_scr_act(), 0, 101, 320, 22, &lv_font_montserrat_14, lc(0x27DFFF), LV_TEXT_ALIGN_CENTER);

    uiOtaBar = lv_bar_create(lv_scr_act());
    lv_obj_set_pos(uiOtaBar, 28, 137);
    lv_obj_set_size(uiOtaBar, 264, 18);
    lv_obj_set_style_bg_color(uiOtaBar, lc(0x172332), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(uiOtaBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(uiOtaBar, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(uiOtaBar, lc(0x27DFFF), LV_PART_INDICATOR);
    lv_obj_set_style_radius(uiOtaBar, 9, LV_PART_INDICATOR);
    lv_bar_set_range(uiOtaBar, 0, 100);

    uiOtaPercent = makeLabel(lv_scr_act(), 0, 164, 320, 34, &lv_font_montserrat_24, lc(0xF5F8FF), LV_TEXT_ALIGN_CENTER);

    lv_obj_t *warn = makeLabel(lv_scr_act(), 0, 214, 320, 18, &lv_font_montserrat_12, lc(0xFFD54A), LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(warn, "DO NOT POWER OFF");
  }

  percent = constrain((int)percent, 0, 100);
  lv_bar_set_value(uiOtaBar, percent, LV_ANIM_OFF);
  lv_label_set_text(uiOtaStage, stage.c_str());
  lv_label_set_text(uiOtaPercent, (String(percent) + "%").c_str());

  if (percent >= 100) {
    lv_obj_set_style_bg_color(uiOtaBar, lc(0x39E889), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(uiOtaStage, lc(0x39E889), 0);
    lv_obj_set_style_text_color(uiOtaPercent, lc(0x39E889), 0);
  }
  lvUiPump();
}

class HudBleServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *server) override {
    bleConnected = true;
    bleHlpReady = false;
    bleRxBuffer = "";
    if (bleRxQueue != nullptr) xQueueReset(bleRxQueue);
    lastBleDevNotify = 0;
    if (!hud.valid) drawWaiting();
    Serial.println("BLE HLP client connected");
  }

  void onDisconnect(NimBLEServer *server) override {
    bleConnected = false;
    bleHlpReady = false;
    bleRxBuffer = "";
    if (bleRxQueue != nullptr) xQueueReset(bleRxQueue);
    if (!hud.valid) drawWaiting();
    NimBLEDevice::getAdvertising()->start();
    Serial.println("BLE HLP client disconnected; advertising restarted");
  }
};

class HudBleTxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic) override {
    // HLP/1 requires the GATT callback to return quickly. Copy the ATT chunk
    // into a bounded FreeRTOS queue; framing, JSON parsing and TFT rendering
    // happen later from loop().
    auto value = characteristic->getValue();
    String raw(value.c_str());

    if (raw.length() == 0 || raw.length() > sizeof(BleRxChunk::bytes) || bleRxQueue == nullptr) {
      bleRxDropped++;
      return;
    }

    BleRxChunk chunk;
    chunk.length = (uint16_t)raw.length();
    memcpy(chunk.bytes, raw.c_str(), chunk.length);

    if (xQueueSend(bleRxQueue, &chunk, 0) != pdTRUE) {
      bleRxDropped++;
    }
  }
};

void processBleLine(String payload) {
  payload.trim();
  if (!payload.length()) return;

  // applyHudPayload replies to ping before any TFT rendering.
  if (applyHudPayload(payload) && hud.valid) {
    drawHud();
  }
}

void processBleInput() {
  if (bleRxQueue == nullptr) return;

  BleRxChunk chunk;
  while (xQueueReceive(bleRxQueue, &chunk, 0) == pdTRUE) {
    for (uint16_t i = 0; i < chunk.length; i++) {
      char c = (char)chunk.bytes[i];

      if (c == '\n') {
        String payload = bleRxBuffer;
        bleRxBuffer = "";
        processBleLine(payload);
      } else if (c != '\r') {
        // HLP/1 frames are limited to 512 bytes.
        if (bleRxBuffer.length() < 512) {
          bleRxBuffer += c;
        } else {
          bleRxBuffer = "";
          bleRxDropped++;
        }
      }
    }
  }
}

void setupBLE() {
  bleRxQueue = xQueueCreate(16, sizeof(BleRxChunk));
  if (bleRxQueue == nullptr) {
    Serial.println("ERROR: cannot create BLE RX queue");
  }

  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setMTU(185);

  NimBLEServer *bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new HudBleServerCallbacks());

  NimBLEService *service = bleServer->createService(BLE_SERVICE_UUID);

  // Android -> HUD. HLP/1 uses acknowledged writes.
  NimBLECharacteristic *tx = service->createCharacteristic(
    BLE_TX_UUID,
    NIMBLE_PROPERTY::WRITE
  );
  tx->setCallbacks(new HudBleTxCallbacks());

  // HUD -> Android. NimBLE automatically exposes CCCD for NOTIFY.
  bleNotifyCharacteristic = service->createCharacteristic(
    BLE_RX_UUID,
    NIMBLE_PROPERTY::NOTIFY
  );

  NimBLECharacteristic *caps = service->createCharacteristic(
    BLE_CAPS_UUID,
    NIMBLE_PROPERTY::READ
  );
  caps->setValue("{\"v\":1,\"caps\":{\"transport\":\"ble\",\"maxFrame\":512}}\n");

  service->start();

  bleLocalAddress = String(NimBLEDevice::getAddress().toString().c_str());

  // Keep the official HLP service UUID in the advertisement so WazeMod can
  // filter this device directly. Scan response carries the configured name.
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->start();

  Serial.print("NimBLE HLP/1 advertising as WazeHUD, address: ");
  Serial.println(bleLocalAddress);
}

bool checkForUpdate() {
  updateAvailable = false;
  latestVersion = "";
  latestFirmwareUrl = "";
  latestFirmwareSize = 0;

  if (WiFi.status() != WL_CONNECTED) {
    updateMessage = "Wi-Fi chưa kết nối Internet";
    Serial.println("OTA check: Wi-Fi not connected");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  http.setUserAgent("ESP32-Waze-HUD/" + String(FW_VERSION));
  http.setConnectTimeout(15000);
  http.setTimeout(20000);

  String api = String("https://api.github.com/repos/") + GITHUB_REPO + "/releases/latest";
  if (!http.begin(client, api)) {
    updateMessage = "Không mở được GitHub API";
    Serial.println("OTA check: http.begin failed");
    return false;
  }

  int code = http.GET();
  Serial.print("OTA check HTTP: ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    updateMessage = "GitHub HTTP " + String(code);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    updateMessage = "Lỗi dữ liệu phiên bản";
    Serial.print("OTA check JSON error: ");
    Serial.println(err.c_str());
    return false;
  }

  latestVersion = String((const char*)(doc["tag_name"] | ""));
  if (latestVersion.startsWith("v")) latestVersion.remove(0, 1);

  JsonArray assets = doc["assets"].as<JsonArray>();
  for (JsonObject asset : assets) {
    String name = String((const char*)(asset["name"] | ""));
    if (name == "firmware.bin" || name.endsWith("-firmware.bin")) {
      latestFirmwareUrl = String((const char*)(asset["browser_download_url"] | ""));
      latestFirmwareSize = (size_t)(asset["size"] | 0);
      break;
    }
  }

  if (!latestVersion.length()) {
    updateMessage = "Release không có version";
    return false;
  }

  if (!latestFirmwareUrl.length()) {
    updateMessage = "Release v" + latestVersion + " chưa có firmware.bin";
    return false;
  }

  updateAvailable = latestVersion != FW_VERSION;

  if (updateAvailable) {
    updateMessage = "Có bản v" + latestVersion;
  } else {
    updateMessage = "Đang dùng bản mới nhất v" + String(FW_VERSION);
  }

  Serial.print("OTA latest: v");
  Serial.print(latestVersion);
  Serial.print(", asset bytes=");
  Serial.println((unsigned long)latestFirmwareSize);
  return true;
}

bool installOnlineUpdate() {
  otaBytesWritten = 0;
  otaBytesTotal = 0;
  otaPercent = 0;

  if (WiFi.status() != WL_CONNECTED) {
    updateMessage = "OTA lỗi: Wi-Fi đã mất kết nối";
    otaStatus = "failed";
    return false;
  }

  if (!latestFirmwareUrl.length()) {
    if (!checkForUpdate() || !latestFirmwareUrl.length()) {
      otaStatus = "failed";
      return false;
    }
  }

  size_t freeSketch = ESP.getFreeSketchSpace();
  if (latestFirmwareSize > 0 && latestFirmwareSize > freeSketch) {
    updateMessage = "Firmware quá lớn cho OTA partition";
    otaStatus = "failed";
    Serial.print("OTA size too large. asset=");
    Serial.print((unsigned long)latestFirmwareSize);
    Serial.print(" free=");
    Serial.println((unsigned long)freeSketch);
    return false;
  }

  otaStatus = "downloading";
  updateMessage = "Đang tải firmware v" + latestVersion;
  drawOtaProgressScreen(0, "PREPARING UPDATE", true);

  Serial.print("OTA free sketch space: ");
  Serial.println((unsigned long)freeSketch);
  Serial.print("OTA URL: ");
  Serial.println(latestFirmwareUrl);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000);

  HTTPClient http;
  http.setUserAgent("ESP32-Waze-HUD/" + String(FW_VERSION));
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(20000);
  http.setTimeout(30000);

  if (!http.begin(client, latestFirmwareUrl)) {
    updateMessage = "OTA lỗi: không mở được firmware URL";
    otaStatus = "failed";
    Serial.println("OTA download: http.begin failed");
    return false;
  }

  http.addHeader("Accept-Encoding", "identity");

  int code = http.GET();
  Serial.print("OTA download HTTP: ");
  Serial.println(code);

  if (code != HTTP_CODE_OK) {
    updateMessage = "OTA tải lỗi HTTP " + String(code);
    otaStatus = "failed";
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  size_t expected = contentLength > 0 ? (size_t)contentLength : latestFirmwareSize;
  otaBytesTotal = expected;

  Serial.print("OTA content length: ");
  Serial.println(contentLength);

  if (expected > 0 && expected > freeSketch) {
    updateMessage = "Firmware vượt quá OTA partition";
    otaStatus = "failed";
    http.end();
    return false;
  }

  size_t beginSize = expected > 0 ? expected : UPDATE_SIZE_UNKNOWN;
  if (!Update.begin(beginSize)) {
    int errCode = Update.getError();
    updateMessage = "Update.begin lỗi " + String(errCode);
    otaStatus = "failed";
    Serial.print("Update.begin failed, code=");
    Serial.println(errCode);
    Update.printError(Serial);
    http.end();
    return false;
  }

  otaStatus = "writing";
  updateMessage = "Đang tải và cài đặt...";
  drawOtaProgressScreen(1, "DOWNLOADING + INSTALLING");

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t written = 0;
  bool streamOK = true;
  uint32_t lastDataAt = millis();
  uint32_t lastWebAt = 0;

  while ((expected > 0 && written < expected) ||
         (expected == 0 && http.connected())) {
    size_t available = stream->available();

    if (available > 0) {
      size_t want = min(available, sizeof(buffer));
      if (expected > 0) want = min(want, expected - written);

      size_t got = stream->readBytes(buffer, want);
      if (got == 0) {
        delay(1);
        continue;
      }

      size_t flashed = Update.write(buffer, got);
      if (flashed != got) {
        streamOK = false;
        Serial.print("OTA Update.write mismatch: ");
        Serial.print((unsigned long)flashed);
        Serial.print("/");
        Serial.println((unsigned long)got);
        break;
      }

      written += flashed;
      otaBytesWritten = written;
      lastDataAt = millis();

      uint8_t pct = 1;
      if (expected > 0) {
        // Reserve 100% for successful verification.
        size_t calcPct = (written * (size_t)99) / expected;
        if (calcPct > 99) calcPct = 99;
        pct = (uint8_t)calcPct;
      }
      otaPercent = pct;
      updateMessage = "Đang cập nhật " + String(pct) + "%";
      drawOtaProgressScreen(pct, "DOWNLOADING + INSTALLING");
    } else {
      if (expected > 0 && written >= expected) break;
      if (!http.connected() && expected == 0) break;

      if (millis() - lastDataAt > 30000) {
        streamOK = false;
        updateMessage = "OTA timeout khi tải firmware";
        Serial.println("OTA stream timeout");
        break;
      }

      delay(1);
    }

    // Keep /update-status responsive during the otherwise blocking OTA stream.
    if (millis() - lastWebAt >= 120) {
      lastWebAt = millis();
      server.handleClient();
    }
    yield();
  }

  otaBytesWritten = written;

  Serial.print("OTA written: ");
  Serial.print((unsigned long)written);
  Serial.print("/");
  Serial.println((unsigned long)expected);

  bool lengthOK = expected == 0 || written == expected;

  if (!streamOK || !lengthOK) {
    int errCode = Update.getError();
    Update.abort();
    http.end();

    otaStatus = "failed";
    otaPercent = 0;
    updateMessage = "OTA ghi lỗi " + String(errCode) + " (" +
                    String((unsigned long)written) + "/" +
                    String((unsigned long)expected) + " bytes)";
    Serial.println(updateMessage);
    return false;
  }

  drawOtaProgressScreen(99, "VERIFYING FIRMWARE");
  otaPercent = 99;
  updateMessage = "Đang xác minh firmware...";

  bool endOK = Update.end(true);
  bool finished = Update.isFinished();
  http.end();

  if (!endOK || !finished) {
    int errCode = Update.getError();
    otaStatus = "failed";
    updateMessage = "OTA xác minh lỗi " + String(errCode);
    Serial.print("OTA verify failed, error=");
    Serial.println(errCode);
    Update.printError(Serial);
    return false;
  }

  otaPercent = 100;
  otaStatus = "success";
  updateMessage = "Cập nhật v" + latestVersion + " thành công";
  drawOtaProgressScreen(100, "UPDATE COMPLETE");

  Serial.println("OTA success; rebooting");
  return true;
}


String checked(bool v) { return v ? "checked" : ""; }

String pageHtml() {
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  String html = R"HTML(
<!doctype html><html lang="vi"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Waze HUD Settings</title>
<style>
:root{color-scheme:dark;--bg:#080b10;--card:#101722;--line:#1d2a38;--blue:#1597ff;--cyan:#28d7ff;--muted:#8ca0b8;--green:#22d67a;--red:#ff4949}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:#fff;font:15px system-ui,-apple-system,sans-serif}main{max-width:760px;margin:auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}.brand{font-size:24px;font-weight:800}.pill{padding:6px 10px;border-radius:999px;background:#0e2634;color:var(--cyan);font-size:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:18px;padding:16px;margin:12px 0;box-shadow:0 10px 28px #0005}.card h2{font-size:17px;margin:0 0 12px}
.row{display:flex;justify-content:space-between;align-items:center;gap:16px;padding:11px 0;border-bottom:1px solid #1c2733}.row:last-child{border-bottom:0}.sub{color:var(--muted);font-size:12px}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:11px;border-radius:10px;border:1px solid #304154;background:#09111a;color:#fff;margin:5px 0}
input[type=range]{width:150px}input[type=checkbox]{width:22px;height:22px;accent-color:var(--blue)}
button{border:0;border-radius:11px;padding:12px 14px;font-weight:750;background:var(--blue);color:white;width:100%;margin-top:8px}.secondary{background:#1a2837}.danger{background:var(--red)}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.status{padding:12px;border-radius:12px;background:#09141e;color:var(--muted);margin-top:8px}.ok{color:var(--green)}.warn{color:#ffd34d}.progress{height:10px;background:#09141e;border:1px solid #203142;border-radius:999px;overflow:hidden;margin:10px 0 4px}.progress>div{height:100%;width:0;background:linear-gradient(90deg,var(--blue),var(--cyan));transition:width .25s ease}.pct{text-align:right;color:var(--muted);font-size:12px}
@media(max-width:560px){.grid{grid-template-columns:1fr}}
</style></head><body><main>
<header><div><div class="brand">WAZE <span style="color:var(--cyan)">HUD</span></div><div class="sub">ESP32 DevKit V1 · ILI9341 320×240</div></div><div class="pill">%IP%</div></header>

<form method="post" action="/settings">
<div class="card"><h2>Hiển thị HUD</h2>
<div class="row"><div><b>Kiểu hiển thị</b><div class="sub">LVGL anti-aliased, tối ưu riêng cho ILI9341 320×240</div></div><b>Full HUD LVGL</b></div>
<div class="row"><div><b>Phản chiếu HUD</b><div class="sub">Dành cho hiển thị phản xạ lên kính lái</div></div><input type="checkbox" name="mirror" %MIRROR%></div>
<div class="row"><div><b>Chế độ ban đêm</b><div class="sub">Nền đen, độ tương phản cao</div></div><input type="checkbox" name="night" %NIGHT%></div>
<div class="row"><div><b>Độ sáng giao diện</b><div class="sub">Lưu cấu hình mức sáng HUD</div></div><input type="range" name="bright" min="20" max="100" value="%BRIGHT%"></div>
<div class="row"><div><b>Tên đường</b></div><input type="checkbox" name="road" %ROAD%></div>
<div class="row"><div><b>Tên tuyến</b></div><input type="checkbox" name="route" %ROUTE%></div>
<div class="row"><div><b>ETA</b></div><input type="checkbox" name="eta" %ETA%></div>
<div class="row"><div><b>Biển giới hạn tốc độ</b></div><input type="checkbox" name="limit" %LIMIT%></div>
</div>

<div class="card"><h2>Cảnh báo Waze/WAZE mod</h2>
<div class="row"><div><b>Cảnh sát</b></div><input type="checkbox" name="a_police" %APOLICE%></div>
<div class="row"><div><b>Camera tốc độ</b></div><input type="checkbox" name="a_camera" %ACAMERA%></div>
<div class="row"><div><b>Tai nạn</b></div><input type="checkbox" name="a_crash" %ACRASH%></div>
<div class="row"><div><b>Ùn tắc</b></div><input type="checkbox" name="a_traffic" %ATRAFFIC%></div>
<div class="row"><div><b>Công trường</b></div><input type="checkbox" name="a_work" %AWORK%></div>
<div class="row"><div><b>Cảnh báo khác</b><div class="sub">Ổ gà, vật cản, đóng đường, thời tiết, làn bị chặn...</div></div><input type="checkbox" name="a_hazard" %AHAZARD%></div>
<button type="submit">Lưu cài đặt</button>
</div>
</form>

<div class="card"><h2>Kết nối Wi-Fi</h2>
<form method="post" action="/wifi"><input name="ssid" type="text" placeholder="Tên Wi-Fi (SSID)"><input name="pass" type="password" placeholder="Mật khẩu"><button>Lưu Wi-Fi & khởi động lại</button></form>
<div class="status">Trạng thái: <span class="%WIFICLASS%">%WIFISTATUS%</span><br>IP: %IP%</div></div>

<div class="card"><h2>Kết nối điện thoại / WAZE mod</h2>
<div class="status"><b>BLE HLP/1:</b> WazeHUD · <span>%BLEADDR%</span><br><b>Service:</b> 8a7e0001-4d6e-4c48-9a9d-484c504c0001<br>Dữ liệu HUD: %HUDSTATUS%</div>
<button class="secondary" onclick="sendTest()">Gửi dữ liệu HUD mẫu</button><div id="testmsg" class="sub"></div></div>

<div class="card"><h2>Cập nhật online</h2>
<div class="row"><div><b>Firmware hiện tại</b></div><b>v%VERSION%</b></div>
<div class="row"><div><b>Phiên bản online</b></div><b id="latest">%LATEST%</b></div>
<div class="row"><div><b>Tự kiểm tra khi khởi động</b></div><input id="autoupdate" type="checkbox" %AUTOUPDATE% onchange="saveAuto()"></div>
<div id="updatemsg" class="status">%UPDATEMSG%</div>
<div class="progress"><div id="updatebar"></div></div><div id="updatepct" class="pct">0%</div>
<div class="grid"><button onclick="checkUpdate()">Kiểm tra cập nhật</button><button id="installbtn" class="secondary" onclick="installUpdate()">Tải về & cập nhật</button></div>
<div class="sub" style="margin-top:8px">Firmware được tải trực tiếp từ GitHub Release của dự án. Không tắt nguồn trong lúc cập nhật.</div>
</div>

<div class="card"><h2>Thông tin thiết bị</h2>
<div class="row"><div>Thiết bị</div><b>ESP32 DevKit V1</b></div><div class="row"><div>Màn hình</div><b>ILI9341 320×240</b></div>
<div class="row"><div>Firmware</div><b>v%VERSION%</b></div><div class="row"><div>Uptime</div><b>%UPTIME%s</b></div>
</div>

<script>
async function sendTest(){let b={turn:"right",distance_m:350,road:"Vo Nguyen Giap",speed:62,speed_limit:60,remaining_km:8.6,eta:"10:42",route:"QL1A",alert:{type:"camera",distance_m:500}};let r=await fetch("/hud",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(b)});document.getElementById("testmsg").textContent=await r.text()}
async function checkUpdate(){let e=document.getElementById("updatemsg");e.textContent="Đang kiểm tra GitHub...";let r=await fetch("/update-check",{method:"POST"});let j=await r.json();e.textContent=j.message;document.getElementById("latest").textContent=j.latest?("v"+j.latest):"--"}
async function installUpdate(){if(!confirm("Cập nhật firmware ngay? Không tắt nguồn trong quá trình cập nhật."))return;let e=document.getElementById("updatemsg");document.getElementById("updatebar").style.width="0%";document.getElementById("updatepct").textContent="0%";e.textContent="Đang gửi lệnh cập nhật...";try{let r=await fetch("/update-online",{method:"POST"});let t=await r.text();e.textContent=t;if(r.ok)setTimeout(pollUpdate,700)}catch(_){e.textContent="Không gửi được lệnh cập nhật."}}
async function pollUpdate(){let e=document.getElementById("updatemsg");try{let r=await fetch("/update-status",{cache:"no-store"});let j=await r.json();let p=Math.max(0,Math.min(100,Number(j.percent||0)));document.getElementById("updatebar").style.width=p+"%";document.getElementById("updatepct").textContent=p+"%";e.textContent=j.message||j.status;if(j.status==="failed")return;if(j.status==="success"){document.getElementById("updatebar").style.width="100%";document.getElementById("updatepct").textContent="100%";e.textContent="Cập nhật thành công, ESP32 đang khởi động lại...";return}setTimeout(pollUpdate,650)}catch(_){e.textContent="ESP32 đang cập nhật hoặc khởi động lại...";setTimeout(pollUpdate,1200)}}
async function saveAuto(){await fetch("/update-auto?enabled="+(document.getElementById("autoupdate").checked?1:0),{method:"POST"})}
</script></main></body></html>)HTML";

  html.replace("%IP%", ip); html.replace("%IP%", ip); html.replace("%IP%", ip);
  html.replace("%VERSION%", FW_VERSION); html.replace("%VERSION%", FW_VERSION);
  html.replace("%LATEST%", latestVersion.length() ? ("v"+latestVersion) : "--");
  html.replace("%UPDATEMSG%", updateMessage);
  html.replace("%UPTIME%", String(millis()/1000));
  html.replace("%HUDSTATUS%", hud.valid ? "Đã nhận dữ liệu" : "Đang chờ điện thoại");
  html.replace("%BLEADDR%", bleLocalAddress.length() ? bleLocalAddress : "--");
  html.replace("%WIFISTATUS%", WiFi.status()==WL_CONNECTED ? "Đã kết nối" : (apMode ? "AP cài đặt" : "Mất kết nối"));
  html.replace("%WIFICLASS%", WiFi.status()==WL_CONNECTED ? "ok" : "warn");
  html.replace("%MIRROR%", checked(settings.mirrorHud));
  html.replace("%NIGHT%", checked(settings.nightMode));
  html.replace("%ROAD%", checked(settings.showRoad));
  html.replace("%ROUTE%", checked(settings.showRoute));
  html.replace("%ETA%", checked(settings.showEta));
  html.replace("%LIMIT%", checked(settings.showSpeedLimit));
  html.replace("%APOLICE%", checked(settings.alertPolice));
  html.replace("%ACAMERA%", checked(settings.alertCamera));
  html.replace("%ACRASH%", checked(settings.alertCrash));
  html.replace("%ATRAFFIC%", checked(settings.alertTraffic));
  html.replace("%AWORK%", checked(settings.alertRoadworks));
  html.replace("%AHAZARD%", checked(settings.alertHazard));
  html.replace("%AUTOUPDATE%", checked(settings.autoUpdateCheck));
  html.replace("%BRIGHT%", String(settings.brightness));
  html.replace("%LAYOUT0%", settings.hudStyle == 0 ? "selected" : "");
  html.replace("%LAYOUT1%", settings.hudStyle == 1 ? "selected" : "");
  html.replace("%LAYOUT2%", settings.hudStyle == 2 ? "selected" : "");
  html.replace("%LAYOUT3%", settings.hudStyle == 3 ? "selected" : "");
  return html;
}

void setupServer() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control","no-store");
    server.send(200, "text/html; charset=utf-8", pageHtml());
  });

  server.on("/hud", HTTP_POST, []() {
    if (!applyHudPayload(server.arg("plain"))) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"json\"}");
      return;
    }
    drawHud();
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/settings", HTTP_POST, []() {
    settings.hudStyle = 3;
    settings.mirrorHud = server.hasArg("mirror");
    settings.nightMode = server.hasArg("night");
    settings.showRoad = server.hasArg("road");
    settings.showRoute = server.hasArg("route");
    settings.showEta = server.hasArg("eta");
    settings.showSpeedLimit = server.hasArg("limit");
    settings.alertPolice = server.hasArg("a_police");
    settings.alertCamera = server.hasArg("a_camera");
    settings.alertCrash = server.hasArg("a_crash");
    settings.alertTraffic = server.hasArg("a_traffic");
    settings.alertRoadworks = server.hasArg("a_work");
    settings.alertHazard = server.hasArg("a_hazard");
    settings.brightness = constrain(server.arg("bright").toInt(),20,100);
    prefs.begin("wazehud", false);
    prefs.putBool("mirror", settings.mirrorHud); prefs.putBool("night", settings.nightMode);
    prefs.putBool("road", settings.showRoad); prefs.putBool("route", settings.showRoute);
    prefs.putBool("eta", settings.showEta); prefs.putBool("limit", settings.showSpeedLimit);
    prefs.putBool("a_police", settings.alertPolice); prefs.putBool("a_camera", settings.alertCamera);
    prefs.putBool("a_crash", settings.alertCrash); prefs.putBool("a_traffic", settings.alertTraffic);
    prefs.putBool("a_work", settings.alertRoadworks); prefs.putBool("a_hazard", settings.alertHazard);
    prefs.putUChar("bright", settings.brightness);
    prefs.putUChar("layout", settings.hudStyle);
    prefs.end();
    if (hud.valid) drawHud(); else drawWaiting();
    server.sendHeader("Location","/",true); server.send(303,"text/plain","");
  });

  server.on("/wifi", HTTP_POST, []() {
    String s=server.arg("ssid"); s.trim(); String p=server.arg("pass");
    if(!s.length()){server.send(400,"text/plain","SSID required");return;}
    prefs.begin("wazehud",false); prefs.putString("ssid",s); prefs.putString("pass",p); prefs.end();
    server.send(200,"text/html; charset=utf-8","<h2>Đã lưu Wi-Fi. ESP32 đang khởi động lại...</h2>");
    delay(900); ESP.restart();
  });

  server.on("/update-check", HTTP_POST, []() {
    bool ok = checkForUpdate();
    JsonDocument d; d["ok"]=ok; d["current"]=FW_VERSION; d["latest"]=latestVersion;
    d["available"]=updateAvailable; d["message"]=updateMessage;
    String out; serializeJson(d,out); server.send(200,"application/json",out);
  });

  server.on("/update-auto", HTTP_POST, []() {
    settings.autoUpdateCheck = server.arg("enabled")=="1";
    prefs.begin("wazehud",false); prefs.putBool("autoupdate",settings.autoUpdateCheck); prefs.end();
    server.send(200,"application/json","{\"ok\":true}");
  });

  server.on("/update-online", HTTP_POST, []() {
    if (otaInProgress || otaRequested) {
      server.send(409, "text/plain; charset=utf-8", "Đang có một phiên cập nhật chạy.");
      return;
    }

    if (!updateAvailable && !checkForUpdate()) {
      server.send(500, "text/plain; charset=utf-8", updateMessage);
      return;
    }

    if (!updateAvailable) {
      server.send(409, "text/plain; charset=utf-8", "Không có bản cập nhật mới.");
      return;
    }

    otaStatus = "queued";
    updateMessage = "Đã nhận lệnh cập nhật v" + latestVersion;
    otaRequestedAt = millis();
    otaRequested = true;

    // Return immediately. The actual HTTPS download + flash write starts from
    // loop(), after the browser has received this response.
    server.send(202, "text/plain; charset=utf-8",
                "Đã bắt đầu cập nhật v" + latestVersion + ". Không tắt nguồn.");
  });

  server.on("/update-status", HTTP_GET, []() {
    JsonDocument d;
    d["status"] = otaStatus;
    d["message"] = updateMessage;
    d["written"] = (uint32_t)otaBytesWritten;
    d["total"] = (uint32_t)otaBytesTotal;
    d["percent"] = otaPercent;
    d["free_ota"] = (uint32_t)ESP.getFreeSketchSpace();
    String out;
    serializeJson(d, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", out);
  });

  server.on("/state", HTTP_GET, []() {
    JsonDocument d;
    d["version"]=FW_VERSION; d["ip"]=apMode?WiFi.softAPIP().toString():WiFi.localIP().toString();
    d["ble_name"]=BLE_DEVICE_NAME; d["ble_address"]=bleLocalAddress;
    d["wifi"]=WiFi.status()==WL_CONNECTED; d["wifi_status"]=(int)WiFi.status(); d["ssid"]=wifiSSID;
    d["wifi_disconnect_reason"]=(int)lastWifiDisconnectReason;
    d["ota_status"]=otaStatus; d["ota_message"]=updateMessage; d["ota_percent"]=otaPercent;
    d["ota_free_space"]=(uint32_t)ESP.getFreeSketchSpace();
    d["ap_mode"]=apMode; d["ble"]=bleConnected; d["hud"]=hud.valid; d["age_ms"]=hud.valid?millis()-hud.updatedAt:0;
    d["alert_code"]=hud.alertCode; d["alert_distance_m"]=hud.alertDistanceM;
    d["alert_value"]=hud.alertValue; d["alert_count"]=hud.alertCount;
    d["over"]=hud.overSpeed; d["layout"]=settings.hudStyle; d["next_speed_limit"]=hud.nextSpeedLimit;
    d["next_speed_distance_m"]=hud.nextSpeedDistanceM;
    String out; serializeJson(d,out); server.send(200,"application/json",out);
  });

  server.onNotFound([](){if(apMode){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}else server.send(404,"text/plain","Not found");});
  server.begin();
}

void startSetupAP() {
  // Switching to AP+STA keeps the station configuration loaded by the single
  // WiFi.begin() call. Never call WiFi.begin() again while STA is connecting.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME, AP_PASS);

  apMode = true;
  shownIp = WiFi.softAPIP().toString();
  wifiUiDirty = true;
}

void connectWiFi() {
  prefs.begin("wazehud", true);
  wifiSSID = prefs.getString("ssid", "");
  wifiPASS = prefs.getString("pass", "");
  prefs.end();

  WiFi.persistent(false);
  WiFi.setSleep(false);

  // Use one reconnect owner only. Arduino auto reconnect + our retry loop can
  // overlap and cause "sta is connecting, cannot set config".
  WiFi.setAutoReconnect(false);

  if (!wifiSSID.length()) {
    startSetupAP();
    Serial.println("Wi-Fi: no saved SSID, setup AP started");
    return;
  }

  WiFi.mode(WIFI_STA);

  // Abort any stale station attempt left by a previous boot/state before
  // applying credentials once.
  WiFi.disconnect(false, false);
  delay(150);

  Serial.print("Wi-Fi: connecting to ");
  Serial.println(wifiSSID);

  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    lvUiPump();
    delay(30);
  }

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    shownIp = WiFi.localIP().toString();
    wifiUiDirty = true;

    Serial.print("Wi-Fi connected, IP: ");
    Serial.println(WiFi.localIP());
    return;
  }

  // Do NOT call WiFi.begin() here. The STA config is already loaded.
  // Add an AP for settings while preserving the saved station config.
  startSetupAP();
  lastWifiRetry = millis();

  Serial.print("Wi-Fi timeout, fallback AP started. status=");
  Serial.print((int)WiFi.status());
  Serial.print(", last reason=");
  Serial.println((int)lastWifiDisconnectReason);
}

void maintainWiFi() {
  wl_status_t status = WiFi.status();

  if (status != lastWifiStatus) {
    lastWifiStatus = status;
    Serial.print("Wi-Fi status: ");
    Serial.println((int)status);
  }

  if (status == WL_CONNECTED) {
    if (!ntpConfigured) {
      configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
      ntpConfigured = true;
    }
    if (apMode) {
      // Keep AP alive only until the STA succeeds. This avoids routing
      // confusion while still allowing setup during failures.
      WiFi.softAPdisconnect(true);
      apMode = false;
      wifiUiDirty = true;
    }

    String ip = WiFi.localIP().toString();
    if (ip != shownIp) {
      shownIp = ip;
      wifiUiDirty = true;
      Serial.print("Wi-Fi IP: ");
      Serial.println(ip);
    }
    return;
  }

  if (!wifiSSID.length()) {
    if (!apMode) startSetupAP();
    return;
  }

  if (!apMode) startSetupAP();

  // Reconnect reuses the already-loaded STA config and does not call
  // esp_wifi_set_config(), so it cannot trigger the previous
  // "sta is connecting, cannot set config" loop.
  if (millis() - lastWifiRetry >= 15000) {
    lastWifiRetry = millis();

    bool started = WiFi.reconnect();
    Serial.print("Wi-Fi reconnect ");
    Serial.print(started ? "started: " : "request failed: ");
    Serial.print(wifiSSID);
    Serial.print(", status=");
    Serial.print((int)WiFi.status());
    Serial.print(", reason=");
    Serial.println((int)lastWifiDisconnectReason);
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      lastWifiDisconnectReason = 0;
      Serial.print("Wi-Fi event GOT_IP: ");
      Serial.println(WiFi.localIP());
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      lastWifiDisconnectReason = info.wifi_sta_disconnected.reason;
      Serial.print("Wi-Fi disconnected, reason=");
      Serial.println((int)lastWifiDisconnectReason);
    }
  });

  displaySPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(40000000);
  tft.setRotation(1);
  tft.setTextWrap(false);
  lvUiInit();

  drawBootSplash();

  updateBootProgress(18, "LOADING SETTINGS");
  loadAppSettings();

  updateBootProgress(34, "CONNECTING NETWORK");
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    configTime(7 * 3600, 0, "pool.ntp.org", "time.google.com");
    ntpConfigured = true;
    updateBootProgress(58, "NETWORK READY");
  } else {
    updateBootProgress(58, "SETUP AP READY");
  }

  updateBootProgress(72, "STARTING BLUETOOTH");
  setupBLE();

  updateBootProgress(86, "STARTING WEB UI");
  setupServer();

  if (settings.autoUpdateCheck && WiFi.status() == WL_CONNECTED) {
    updateBootProgress(94, "CHECKING UPDATES");
    checkForUpdate();
  }

  updateBootProgress(100, "READY");
  delay(450);
  drawWaiting();

  Serial.print("Waze HUD IP: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP());
}


void drawOverspeedBorder(bool visible) {
  if (lvUiMode != LVUI_HUD) return;
  lv_obj_set_style_border_width(lv_scr_act(), visible ? 3 : 0, 0);
  lv_obj_set_style_border_color(lv_scr_act(), lc(0xFF453A), 0);
  lv_obj_set_style_border_opa(lv_scr_act(), visible ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void updateOverspeedEffect() {
  bool active = hud.valid && hud.overSpeed &&
                (millis() - hud.updatedAt <= HUD_TIMEOUT_MS);

  if (active) {
    if (millis() - lastOverspeedBlink >= 300) {
      lastOverspeedBlink = millis();
      overspeedBorderVisible = !overspeedBorderVisible;
      drawOverspeedBorder(overspeedBorderVisible);
    }
  } else if (overspeedBorderVisible) {
    overspeedBorderVisible = false;
    drawOverspeedBorder(false);
  }
}


void loop() {
  // Drain GATT bytes outside the Bluetooth callback. This prevents TFT/JSON work
  // from blocking acknowledged BLE writes.
  processBleInput();

  // Send HLP device declaration repeatedly until WazeMod answers with "hi".
  if (bleConnected && !bleHlpReady && millis() - lastBleDevNotify >= 350) {
    lastBleDevNotify = millis();
    sendHlpDev();
  }

  server.handleClient();

  // Execute OTA outside the WebServer request handler so the browser receives
  // HTTP 202 first and the server does not hold an open request while flashing.
  if (otaRequested && !otaInProgress && millis() - otaRequestedAt >= 500) {
    otaRequested = false;
    otaInProgress = true;

    otaRenderedPercent = -1;
    otaRenderedStage = "";
    bool ok = installOnlineUpdate();
    otaInProgress = false;

    if (ok) {
      delay(1000);
      ESP.restart();
    } else {
      Serial.print("OTA final error: ");
      Serial.println(updateMessage);
      drawWaiting();
    }
  }

  if (!otaInProgress) maintainWiFi();
  updateOverspeedEffect();
  lvUiPump();

  // Reflect a new DHCP/AP address on the boot/waiting screen.
  String currentIp = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() :
                     (apMode ? WiFi.softAPIP().toString() : "connecting...");
  if (currentIp != shownIp) {
    shownIp = currentIp;
    wifiUiDirty = true;
  }
  if (wifiUiDirty && !hud.valid) {
    drawWaiting();
  }

  // Only checks the LINK LOST transition. drawHud() itself is dirty-region based,
  // so identical 1 Hz HLP heartbeats do not redraw the display.
  static uint32_t lastStatusCheck = 0;
  if (millis() - lastStatusCheck >= 250) {
    lastStatusCheck = millis();
    if (hud.valid) drawHud();
  }

  delay(3);
}
