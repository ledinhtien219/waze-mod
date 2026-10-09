#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
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
static const char *FW_VERSION = "1.4.2";
static const char *GITHUB_REPO = "ledinhtien219/waze-mod";

SPIClass displaySPI(HSPI);
Adafruit_ILI9341 tft(&displaySPI, TFT_DC, TFT_CS, TFT_RST);
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
BLECharacteristic *bleNotifyCharacteristic = nullptr;
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
  uint8_t hudStyle = 0; // 0 Balanced, 1 Navigation, 2 Minimal
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
  settings.hudStyle = constrain((int)prefs.getUChar("layout", 0), 0, 2);
  prefs.end();
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

void textCentered(const String &s, int x, int y, int w, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color, C_BG);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(s, 0, 0, &x1, &y1, &tw, &th);
  tft.setCursor(x + max(0, (w - (int)tw) / 2), y);
  tft.print(s);
}

void useDefaultFont() {
  tft.setFont(nullptr);
  tft.setTextSize(1);
}

void smoothText(const String &s, int x, int baseline, const GFXfont *font, uint16_t color) {
  tft.setFont(font);
  tft.setTextColor(color);
  tft.setCursor(x, baseline);
  tft.print(s);
}

void smoothTextCentered(const String &s, int x, int baseline, int w, const GFXfont *font, uint16_t color) {
  tft.setFont(font);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(s, 0, baseline, &x1, &y1, &tw, &th);
  tft.setCursor(x + max(0, (w - (int)tw) / 2), baseline);
  tft.print(s);
}

void smoothTextRight(const String &s, int rightX, int baseline, const GFXfont *font, uint16_t color) {
  tft.setFont(font);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t tw, th;
  tft.getTextBounds(s, 0, baseline, &x1, &y1, &tw, &th);
  tft.setCursor(rightX - tw, baseline);
  tft.print(s);
}

void drawTriangleSign(int cx, int cy, int r) {
  int x1 = cx, y1 = cy - r;
  int x2 = cx - r, y2 = cy + r;
  int x3 = cx + r, y3 = cy + r;
  tft.fillTriangle(x1, y1, x2, y2, x3, y3, C_RED);
  int ir = r - 5;
  tft.fillTriangle(cx, cy - ir, cx - ir, cy + ir, cx + ir, cy + ir, C_YELLOW);
}

void drawSpeedLimit(int cx, int cy, int limit) {
  tft.fillCircle(cx, cy, 29, C_RED);
  tft.fillCircle(cx, cy, 23, C_WHITE);
  tft.setTextColor(ILI9341_BLACK, C_WHITE);
  tft.setTextSize(limit >= 100 ? 2 : 3);
  String n = limit > 0 ? String(limit) : "--";
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(n, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(cx - w/2, cy - h/2);
  tft.print(n);
}

void drawCameraGlyph(int cx, int cy, uint16_t color) {
  tft.fillRoundRect(cx - 14, cy - 9, 28, 18, 4, color);
  tft.fillRect(cx - 8, cy - 13, 10, 5, color);
  tft.fillCircle(cx, cy, 7, C_BG);
  tft.fillCircle(cx, cy, 3, color);
}

void drawAlertBadge(int cx, int cy, uint16_t bg) {
  tft.fillCircle(cx, cy, 20, bg);
  tft.drawCircle(cx, cy, 20, C_WHITE);
}

void drawTinyArrow(int cx, int cy, int dx, int dy, uint16_t color) {
  int ex = cx + dx, ey = cy + dy;
  tft.drawLine(cx, cy, ex, ey, color);
  tft.drawLine(cx + 1, cy, ex + 1, ey, color);
  if (abs(dx) >= abs(dy)) {
    int sx = dx >= 0 ? 1 : -1;
    tft.drawLine(ex, ey, ex - sx * 6, ey - 5, color);
    tft.drawLine(ex, ey, ex - sx * 6, ey + 5, color);
  } else {
    int sy = dy >= 0 ? 1 : -1;
    tft.drawLine(ex, ey, ex - 5, ey - sy * 6, color);
    tft.drawLine(ex, ey, ex + 5, ey - sy * 6, color);
  }
}

void drawCarTiny(int cx, int cy, uint16_t color) {
  tft.fillRoundRect(cx - 11, cy - 5, 22, 11, 3, color);
  tft.fillRect(cx - 7, cy - 9, 14, 5, color);
  tft.fillCircle(cx - 7, cy + 7, 3, C_BG);
  tft.fillCircle(cx + 7, cy + 7, 3, C_BG);
}

void drawMotoTiny(int cx, int cy, uint16_t color) {
  tft.drawCircle(cx - 8, cy + 7, 4, color);
  tft.drawCircle(cx + 9, cy + 7, 4, color);
  tft.drawLine(cx - 5, cy + 4, cx, cy - 3, color);
  tft.drawLine(cx, cy - 3, cx + 6, cy + 5, color);
  tft.drawLine(cx - 2, cy + 2, cx + 7, cy + 2, color);
  tft.fillCircle(cx - 1, cy - 8, 3, color);
}

void drawNoCircle(int cx, int cy) {
  tft.fillCircle(cx, cy, 20, C_WHITE);
  tft.drawCircle(cx, cy, 20, C_RED);
  tft.drawCircle(cx, cy, 19, C_RED);
  tft.drawCircle(cx, cy, 18, C_RED);
}

void drawMandatoryCircle(int cx, int cy) {
  tft.fillCircle(cx, cy, 20, C_BLUE2);
  tft.drawCircle(cx, cy, 20, C_WHITE);
}

void drawCloud(int cx, int cy, uint16_t color) {
  tft.fillCircle(cx - 8, cy - 3, 6, color);
  tft.fillCircle(cx, cy - 7, 8, color);
  tft.fillCircle(cx + 9, cy - 3, 5, color);
  tft.fillRect(cx - 12, cy - 3, 25, 8, color);
}

void drawTrafficLightTiny(int cx, int cy, bool broken) {
  tft.fillRoundRect(cx - 7, cy - 15, 14, 30, 3, C_WHITE);
  tft.fillCircle(cx, cy - 8, 4, C_RED);
  tft.fillCircle(cx, cy, 4, C_YELLOW);
  tft.fillCircle(cx, cy + 8, 4, C_GREEN);
  if (broken) {
    tft.drawLine(cx - 14, cy - 14, cx + 14, cy + 14, C_RED);
    tft.drawLine(cx + 14, cy - 14, cx - 14, cy + 14, C_RED);
  }
}

void drawCameraVariant(int cx, int cy, uint8_t code) {
  drawAlertBadge(cx, cy, C_BLUE2);
  drawCameraGlyph(cx, cy, C_WHITE);
  useDefaultFont();
  tft.setTextColor(C_YELLOW, C_BLUE2);
  tft.setTextSize(1);
  const char *mark = "";
  switch (code) {
    case 40: mark = "P"; break;   // phone
    case 41: mark = "D"; break;   // dummy
    case 42: mark = "B"; break;   // belt
    case 43: mark = "<>"; break;  // distance
    case 44: mark = "BUS"; break;
    case 45: mark = "~"; break;   // noise
    case 46: mark = "S"; break;   // stop
    default: break;
  }
  if (mark[0]) {
    int16_t x1,y1; uint16_t w,h;
    tft.getTextBounds(mark,0,0,&x1,&y1,&w,&h);
    tft.setCursor(cx - w/2, cy + 15);
    tft.print(mark);
  }
}

void drawRestrictionArrow(uint8_t code, int cx, int cy) {
  drawNoCircle(cx, cy);
  uint16_t c = ILI9341_BLACK;
  if (code == 28 || code == 72) {
    tft.drawLine(cx + 4, cy + 10, cx + 4, cy - 5, c);
    drawTinyArrow(cx + 4, cy - 5, -13, -8, c);
  } else if (code == 29 || code == 73) {
    tft.drawLine(cx - 4, cy + 10, cx - 4, cy - 5, c);
    drawTinyArrow(cx - 4, cy - 5, 13, -8, c);
  } else if (code == 30 || code == 74) {
    tft.drawLine(cx + 7, cy + 10, cx + 7, cy - 2, c);
    tft.drawCircle(cx, cy - 2, 9, c);
    drawTinyArrow(cx - 8, cy - 2, 0, 10, c);
  } else if (code == 31) {
    drawTinyArrow(cx, cy + 9, 0, -19, c);
  } else {
    // Combined turn restrictions 65..71.
    drawTinyArrow(cx, cy + 8, 0, -17, c);
    if (code == 65 || code == 67) drawTinyArrow(cx, cy, code == 65 ? 12 : -12, -9, c);
    else if (code == 66 || code == 69) drawTinyArrow(cx + 3, cy, -12, 10, c);
    else if (code == 70 || code == 71) drawTinyArrow(cx - 3, cy, 12, 10, c);
    else if (code == 68) {
      drawTinyArrow(cx, cy, -12, -9, c);
      drawTinyArrow(cx, cy, 12, -9, c);
    }
  }
  tft.drawLine(cx - 13, cy - 13, cx + 13, cy + 13, C_RED);
  tft.drawLine(cx - 12, cy - 14, cx + 14, cy + 12, C_RED);
}

void drawWazeAlertIcon(uint8_t code, int cx, int cy) {
  useDefaultFont();
  const uint16_t ORANGE = 0xFD20;

  // 0=None
  if (code == 0) return;

  // 1 Police
  if (code == 1) {
    drawAlertBadge(cx, cy, C_BLUE);
    tft.fillRect(cx - 11, cy - 8, 22, 4, C_WHITE);
    tft.fillRect(cx - 6, cy - 12, 12, 5, C_WHITE);
    tft.fillCircle(cx, cy + 1, 6, C_WHITE);
    tft.fillRect(cx - 9, cy + 7, 18, 7, C_WHITE);
    return;
  }

  // 2 Speed camera + 40..46 camera variants.
  if (code == 2 || (code >= 40 && code <= 46)) {
    drawCameraVariant(cx, cy, code);
    return;
  }

  // 3 Red-light camera.
  if (code == 3) {
    drawAlertBadge(cx, cy, C_BLUE2);
    drawTrafficLightTiny(cx - 3, cy, false);
    tft.fillRect(cx + 9, cy - 5, 7, 10, C_WHITE);
    tft.fillCircle(cx + 12, cy, 2, C_BG);
    return;
  }

  // 4 generic hazard / 18 dangerous road / 39 combined restriction.
  if (code == 4 || code == 18 || code == 39) {
    drawTriangleSign(cx, cy, 20);
    tft.setTextColor(C_DARK, C_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(cx - 3, cy - 6);
    tft.print("!");
    return;
  }

  // 5 Accident.
  if (code == 5) {
    drawAlertBadge(cx, cy, C_RED);
    drawCarTiny(cx - 7, cy + 3, C_WHITE);
    drawCarTiny(cx + 8, cy - 3, C_YELLOW);
    tft.drawLine(cx - 2, cy - 11, cx + 3, cy - 5, C_WHITE);
    tft.drawLine(cx + 3, cy - 11, cx - 2, cy - 5, C_WHITE);
    return;
  }

  // 6 Traffic jam - severity bars.
  if (code == 6) {
    drawAlertBadge(cx, cy, ORANGE);
    for (int i=0;i<3;i++) {
      tft.fillRoundRect(cx - 12, cy - 11 + i*10, 24, 6, 2, C_WHITE);
    }
    int bars = constrain((int)hud.alertSeverity, 1, 5);
    for (int i=0;i<5;i++) {
      tft.fillRect(cx - 14 + i*6, cy + 14, 4, 3, i < bars ? C_RED : C_DARK);
    }
    return;
  }

  // 7 closed road / 38 prohibited road.
  if (code == 7 || code == 38) {
    drawNoCircle(cx, cy);
    tft.fillRoundRect(cx - 13, cy - 4, 26, 8, 3, C_RED);
    if (code == 7) {
      tft.drawFastVLine(cx - 8, cy - 11, 22, ILI9341_BLACK);
      tft.drawFastVLine(cx + 8, cy - 11, 22, ILI9341_BLACK);
    }
    return;
  }

  // 8 speed drop / 22 end speed restriction.
  if (code == 8 || code == 22) {
    if (hud.alertValue > 0) {
      drawMiniSpeedLimit(cx, cy, hud.alertValue, 20);
      if (code == 22) {
        tft.drawLine(cx - 14, cy + 14, cx + 14, cy - 14, C_GREY);
        tft.drawLine(cx - 10, cy + 17, cx + 17, cy - 10, C_GREY);
      }
    } else {
      drawNoCircle(cx, cy);
    }
    return;
  }

  // 9 no passing / 10 end no passing.
  if (code == 9 || code == 10) {
    drawNoCircle(cx, cy);
    tft.fillCircle(cx - 6, cy, 5, ILI9341_BLACK);
    tft.fillCircle(cx + 6, cy, 5, C_RED);
    if (code == 10) {
      tft.drawLine(cx - 14, cy + 14, cx + 14, cy - 14, C_GREY);
    }
    return;
  }

  // 11 railway.
  if (code == 11) {
    drawAlertBadge(cx, cy, C_YELLOW);
    tft.drawLine(cx - 12, cy - 12, cx + 12, cy + 12, C_DARK);
    tft.drawLine(cx + 12, cy - 12, cx - 12, cy + 12, C_DARK);
    tft.drawFastHLine(cx - 15, cy + 12, 30, C_DARK);
    return;
  }

  // 12 toll booth.
  if (code == 12) {
    drawAlertBadge(cx, cy, C_BLUE2);
    tft.fillRect(cx - 13, cy - 8, 26, 5, C_WHITE);
    tft.fillRect(cx - 11, cy - 3, 5, 15, C_WHITE);
    tft.fillRect(cx + 6, cy - 3, 5, 15, C_WHITE);
    tft.fillRect(cx - 2, cy - 3, 4, 9, C_YELLOW);
    return;
  }

  // 13 stopped vehicle.
  if (code == 13) {
    drawAlertBadge(cx, cy, ORANGE);
    drawCarTiny(cx - 2, cy + 2, C_WHITE);
    tft.drawFastVLine(cx + 13, cy - 13, 27, C_WHITE);
    return;
  }

  // 14 construction.
  if (code == 14) {
    drawTriangleSign(cx, cy, 20);
    tft.fillCircle(cx - 2, cy - 5, 3, C_DARK);
    tft.drawLine(cx, cy - 1, cx - 6, cy + 11, C_DARK);
    tft.drawLine(cx, cy - 1, cx + 8, cy + 9, C_DARK);
    tft.drawLine(cx - 5, cy + 2, cx + 10, cy - 1, C_DARK);
    return;
  }

  // 15 pothole.
  if (code == 15) {
    drawAlertBadge(cx, cy, ORANGE);
    tft.drawFastHLine(cx - 14, cy - 7, 28, C_WHITE);
    tft.drawLine(cx - 13, cy - 6, cx - 7, cy + 8, C_WHITE);
    tft.drawLine(cx - 7, cy + 8, cx, cy + 2, C_WHITE);
    tft.drawLine(cx, cy + 2, cx + 8, cy + 9, C_WHITE);
    tft.drawLine(cx + 8, cy + 9, cx + 14, cy - 6, C_WHITE);
    return;
  }

  // 16 weather.
  if (code == 16) {
    drawAlertBadge(cx, cy, C_BLUE2);
    drawCloud(cx, cy - 4, C_WHITE);
    tft.drawLine(cx - 8, cy + 8, cx - 11, cy + 14, C_BLUE);
    tft.drawLine(cx, cy + 8, cx - 3, cy + 14, C_BLUE);
    tft.drawLine(cx + 8, cy + 8, cx + 5, cy + 14, C_BLUE);
    return;
  }

  // 17 blocked lane.
  if (code == 17) {
    drawAlertBadge(cx, cy, ORANGE);
    tft.drawFastVLine(cx - 10, cy - 13, 27, C_WHITE);
    tft.drawFastVLine(cx + 10, cy - 13, 27, C_WHITE);
    tft.drawLine(cx - 6, cy - 6, cx + 6, cy + 6, C_RED);
    tft.drawLine(cx + 6, cy - 6, cx - 6, cy + 6, C_RED);
    return;
  }

  // 19 expressway exit.
  if (code == 19) {
    drawAlertBadge(cx, cy, C_GREEN);
    drawTinyArrow(cx - 4, cy + 10, 0, -20, C_WHITE);
    drawTinyArrow(cx, cy, 13, -12, C_WHITE);
    return;
  }

  // 20 expressway rest / 21 rest stop.
  if (code == 20 || code == 21) {
    drawAlertBadge(cx, cy, C_BLUE2);
    tft.setTextColor(C_WHITE, C_BLUE2);
    tft.setTextSize(2);
    tft.setCursor(cx - 6, cy - 7);
    tft.print(code == 20 ? "P" : "R");
    return;
  }

  // 23 residential start / 24 residential end.
  if (code == 23 || code == 24) {
    drawAlertBadge(cx, cy, C_BLUE2);
    tft.fillTriangle(cx, cy - 13, cx - 13, cy - 1, cx + 13, cy - 1, C_WHITE);
    tft.fillRect(cx - 9, cy - 1, 18, 13, C_WHITE);
    tft.fillRect(cx - 3, cy + 5, 6, 7, C_BG);
    if (code == 24) tft.drawLine(cx - 14, cy + 14, cx + 14, cy - 14, C_RED);
    return;
  }

  // 25 end all prohibitions.
  if (code == 25) {
    drawNoCircle(cx, cy);
    for (int k=-10;k<=10;k+=7) tft.drawLine(cx+k-7, cy+15, cx+k+15, cy-7, C_GREY);
    return;
  }

  // 26 no car.
  if (code == 26) {
    drawNoCircle(cx, cy);
    drawCarTiny(cx, cy, ILI9341_BLACK);
    tft.drawLine(cx - 14, cy - 14, cx + 14, cy + 14, C_RED);
    return;
  }

  // 27 no motorcycle.
  if (code == 27) {
    drawNoCircle(cx, cy);
    drawMotoTiny(cx, cy - 2, ILI9341_BLACK);
    tft.drawLine(cx - 14, cy - 14, cx + 14, cy + 14, C_RED);
    return;
  }

  // 28..31 turn prohibitions, 65..74 combined/car restrictions.
  if ((code >= 28 && code <= 31) || (code >= 65 && code <= 74)) {
    drawRestrictionArrow(code, cx, cy);
    if (code >= 69 && code <= 74) {
      drawCarTiny(cx, cy + 10, ILI9341_BLACK);
    }
    return;
  }

  // 32 mandatory straight / 33 right / 34 left.
  if (code >= 32 && code <= 34) {
    drawMandatoryCircle(cx, cy);
    if (code == 32) drawTinyArrow(cx, cy + 10, 0, -20, C_WHITE);
    if (code == 33) drawTinyArrow(cx - 7, cy + 7, 14, -14, C_WHITE);
    if (code == 34) drawTinyArrow(cx + 7, cy + 7, -14, -14, C_WHITE);
    return;
  }

  // 35 car lane / 36 motorcycle lane.
  if (code == 35 || code == 36) {
    drawAlertBadge(cx, cy, C_BLUE2);
    if (code == 35) drawCarTiny(cx, cy, C_WHITE);
    else drawMotoTiny(cx, cy - 2, C_WHITE);
    tft.drawFastVLine(cx - 17, cy - 15, 30, C_WHITE);
    tft.drawFastVLine(cx + 17, cy - 15, 30, C_WHITE);
    return;
  }

  // 37 one way.
  if (code == 37) {
    drawAlertBadge(cx, cy, C_BLUE2);
    drawTinyArrow(cx - 12, cy, 24, 0, C_WHITE);
    return;
  }

  // 47 animal.
  if (code == 47) {
    drawAlertBadge(cx, cy, ORANGE);
    tft.fillCircle(cx - 6, cy, 5, C_WHITE);
    tft.fillCircle(cx + 5, cy + 1, 5, C_WHITE);
    tft.fillRect(cx - 7, cy, 13, 8, C_WHITE);
    tft.fillTriangle(cx - 9, cy - 4, cx - 4, cy - 13, cx - 1, cy - 3, C_WHITE);
    tft.fillTriangle(cx + 9, cy - 4, cx + 4, cy - 13, cx + 1, cy - 3, C_WHITE);
    return;
  }

  // 48 object on road.
  if (code == 48) {
    drawAlertBadge(cx, cy, ORANGE);
    tft.fillRect(cx - 10, cy - 10, 20, 20, C_WHITE);
    tft.drawLine(cx - 10, cy - 10, cx + 10, cy + 10, C_DARK);
    tft.drawLine(cx + 10, cy - 10, cx - 10, cy + 10, C_DARK);
    return;
  }

  // 49 roadkill.
  if (code == 49) {
    drawAlertBadge(cx, cy, ORANGE);
    tft.drawLine(cx - 12, cy - 9, cx + 12, cy + 9, C_WHITE);
    tft.drawLine(cx + 12, cy - 9, cx - 12, cy + 9, C_WHITE);
    tft.fillCircle(cx, cy, 4, C_WHITE);
    return;
  }

  // 50 flood.
  if (code == 50) {
    drawAlertBadge(cx, cy, C_BLUE2);
    for (int y=-7;y<=9;y+=8) {
      tft.drawLine(cx-14,cy+y,cx-7,cy+y-2,C_WHITE);
      tft.drawLine(cx-7,cy+y-2,cx,cy+y,C_WHITE);
      tft.drawLine(cx,cy+y,cx+7,cy+y-2,C_WHITE);
      tft.drawLine(cx+7,cy+y-2,cx+14,cy+y,C_WHITE);
    }
    return;
  }

  // 51 fog.
  if (code == 51) {
    drawAlertBadge(cx, cy, C_BLUE2);
    drawCloud(cx, cy - 8, C_WHITE);
    for (int y=3;y<=13;y+=5) tft.drawFastHLine(cx-14,cy+y,28,C_GREY);
    return;
  }

  // 52 hail.
  if (code == 52) {
    drawAlertBadge(cx, cy, C_BLUE2);
    drawCloud(cx, cy - 8, C_WHITE);
    for (int x=-9;x<=9;x+=9) tft.fillCircle(cx+x,cy+10,2,C_WHITE);
    return;
  }

  // 53 snow / 54 ice.
  if (code == 53 || code == 54) {
    drawAlertBadge(cx, cy, C_BLUE2);
    for (int a=-12;a<=12;a+=24) {
      tft.drawLine(cx+a,cy,cx-a,cy,C_WHITE);
      tft.drawLine(cx,cy+a,cx,cy-a,C_WHITE);
    }
    tft.drawLine(cx-9,cy-9,cx+9,cy+9,C_WHITE);
    tft.drawLine(cx+9,cy-9,cx-9,cy+9,C_WHITE);
    if (code == 54) tft.drawFastHLine(cx-14,cy+14,28,C_BLUE);
    return;
  }

  // 55 slippery.
  if (code == 55) {
    drawAlertBadge(cx, cy, ORANGE);
    drawCarTiny(cx, cy - 5, C_WHITE);
    tft.drawLine(cx-12,cy+10,cx-4,cy+14,C_WHITE);
    tft.drawLine(cx+2,cy+10,cx+10,cy+14,C_WHITE);
    return;
  }

  // 56 speed bump.
  if (code == 56) {
    drawAlertBadge(cx, cy, ORANGE);
    tft.drawFastHLine(cx - 15, cy + 9, 30, C_WHITE);
    tft.drawCircle(cx, cy + 8, 13, C_WHITE);
    tft.fillRect(cx - 15, cy - 7, 30, 16, ORANGE);
    return;
  }

  // 57 school.
  if (code == 57) {
    drawAlertBadge(cx, cy, ORANGE);
    tft.fillCircle(cx - 5, cy - 8, 3, C_WHITE);
    tft.fillCircle(cx + 5, cy - 6, 3, C_WHITE);
    tft.drawLine(cx - 5, cy - 4, cx - 8, cy + 10, C_WHITE);
    tft.drawLine(cx + 5, cy - 2, cx + 8, cy + 10, C_WHITE);
    tft.drawLine(cx - 5, cy + 1, cx + 4, cy + 7, C_WHITE);
    return;
  }

  // 58 merging lanes.
  if (code == 58) {
    drawAlertBadge(cx, cy, C_BLUE2);
    drawTinyArrow(cx - 8, cy + 12, 8, -22, C_WHITE);
    tft.drawLine(cx + 13, cy + 12, cx + 2, cy - 5, C_WHITE);
    return;
  }

  // 59 dangerous curve.
  if (code == 59) {
    drawAlertBadge(cx, cy, ORANGE);
    tft.drawLine(cx - 9, cy + 13, cx - 9, cy + 4, C_WHITE);
    tft.drawLine(cx - 9, cy + 4, cx + 7, cy - 10, C_WHITE);
    drawTinyArrow(cx + 7, cy - 10, 6, -6, C_WHITE);
    return;
  }

  // 60 fork.
  if (code == 60) {
    drawAlertBadge(cx, cy, C_BLUE2);
    tft.drawLine(cx,cy+13,cx,cy-3,C_WHITE);
    drawTinyArrow(cx,cy-3,-11,-11,C_WHITE);
    drawTinyArrow(cx,cy-3,11,-11,C_WHITE);
    return;
  }

  // 61 broken light / 75 traffic light.
  if (code == 61 || code == 75) {
    drawAlertBadge(cx, cy, C_DARK);
    drawTrafficLightTiny(cx, cy, code == 61);
    return;
  }

  // 62 cyclist.
  if (code == 62) {
    drawAlertBadge(cx, cy, C_BLUE2);
    tft.drawCircle(cx-9,cy+7,5,C_WHITE);
    tft.drawCircle(cx+10,cy+7,5,C_WHITE);
    tft.fillCircle(cx,cy-9,3,C_WHITE);
    tft.drawLine(cx,cy-5,cx-5,cy+4,C_WHITE);
    tft.drawLine(cx-5,cy+4,cx+4,cy+4,C_WHITE);
    tft.drawLine(cx+4,cy+4,cx+10,cy+7,C_WHITE);
    return;
  }

  // 63 emergency vehicle.
  if (code == 63) {
    drawAlertBadge(cx, cy, C_RED);
    drawCarTiny(cx, cy + 3, C_WHITE);
    tft.fillRect(cx - 5, cy - 12, 10, 4, C_BLUE);
    tft.drawFastVLine(cx, cy - 10, 8, C_WHITE);
    tft.drawFastHLine(cx - 4, cy - 6, 8, C_WHITE);
    return;
  }

  // 64 personal safety.
  if (code == 64) {
    drawAlertBadge(cx, cy, C_BLUE2);
    tft.fillTriangle(cx,cy-14,cx-12,cy-8,cx+12,cy-8,C_WHITE);
    tft.fillTriangle(cx-12,cy-8,cx+12,cy-8,cx,cy+14,C_WHITE);
    tft.fillCircle(cx,cy-3,4,C_BLUE2);
    return;
  }

  // Fallback hazard for any future HLP code.
  drawTriangleSign(cx, cy, 20);
  tft.setTextColor(C_DARK, C_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(cx - 3, cy - 6);
  tft.print("!");
}

void drawArrow(TurnType turn, int cx, int cy) {
  const uint16_t c = C_BLUE;
  const int shaft = 9;

  auto thickLine = [&](int x1, int y1, int x2, int y2) {
    tft.drawLine(x1, y1, x2, y2, c);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, c);
    tft.drawLine(x1 - 1, y1, x2 - 1, y2, c);
    tft.drawLine(x1, y1 + 1, x2, y2 + 1, c);
    tft.drawLine(x1, y1 - 1, x2, y2 - 1, c);
  };

  if (turn == TURN_STRAIGHT) {
    tft.fillRect(cx - shaft/2, cy - 12, shaft, 34, c);
    tft.fillTriangle(cx, cy - 34, cx - 15, cy - 12, cx + 15, cy - 12, c);
    return;
  }

  if (turn == TURN_LEFT || turn == TURN_RIGHT) {
    int dir = turn == TURN_RIGHT ? 1 : -1;
    tft.fillRect(cx - shaft/2, cy - 1, shaft, 24, c);
    if (dir < 0) tft.fillRect(cx - 27, cy - 1, 28, shaft, c);
    else         tft.fillRect(cx,      cy - 1, 28, shaft, c);
    int tip = cx + dir * 34;
    int base = cx + dir * 20;
    tft.fillTriangle(tip, cy + 3, base, cy - 8, base, cy + 14, c);
    return;
  }

  if (turn == TURN_SLIGHT_LEFT || turn == TURN_SLIGHT_RIGHT ||
      turn == TURN_KEEP_LEFT || turn == TURN_KEEP_RIGHT ||
      turn == TURN_EXIT_LEFT || turn == TURN_EXIT_RIGHT) {
    bool right = (turn == TURN_SLIGHT_RIGHT || turn == TURN_KEEP_RIGHT || turn == TURN_EXIT_RIGHT);
    int dir = right ? 1 : -1;

    // Short vertical stem and a 45-degree branch.
    tft.fillRect(cx - 4, cy + 2, 8, 21, c);
    thickLine(cx, cy + 5, cx + dir * 24, cy - 19);
    thickLine(cx, cy + 7, cx + dir * 25, cy - 17);

    int tipX = cx + dir * 31;
    int tipY = cy - 25;
    int baseX = cx + dir * 19;
    tft.fillTriangle(tipX, tipY,
                     baseX, tipY + 3,
                     tipX - dir * 3, tipY + 12, c);

    // KEEP is shown as a fork; EXIT is shown with a short continuation.
    if (turn == TURN_KEEP_LEFT || turn == TURN_KEEP_RIGHT) {
      thickLine(cx, cy + 4, cx, cy - 16);
    } else if (turn == TURN_EXIT_LEFT || turn == TURN_EXIT_RIGHT) {
      tft.drawFastVLine(cx, cy - 11, 14, C_GREY);
    }
    return;
  }

  if (turn == TURN_SHARP_LEFT || turn == TURN_SHARP_RIGHT) {
    int dir = turn == TURN_SHARP_RIGHT ? 1 : -1;
    tft.fillRect(cx - 4, cy + 1, 8, 22, c);
    int elbowX = cx + dir * 17;
    tft.fillRect(dir < 0 ? elbowX : cx, cy - 10, 18, 8, c);
    thickLine(cx, cy + 4, cx, cy - 7);
    int tipX = cx + dir * 31;
    tft.fillTriangle(tipX, cy - 6,
                     cx + dir * 18, cy - 17,
                     cx + dir * 18, cy + 5, c);
    return;
  }

  if (turn == TURN_UTURN) {
    tft.fillRect(cx + 8, cy - 1, 8, 25, c);
    tft.drawCircle(cx, cy - 4, 20, c);
    tft.drawCircle(cx, cy - 4, 19, c);
    tft.drawCircle(cx, cy - 4, 18, c);
    tft.fillRect(cx - 27, cy - 11, 26, 23, C_BG);
    tft.fillTriangle(cx - 27, cy - 4, cx - 11, cy - 16, cx - 11, cy + 8, c);
    return;
  }

  if (turn == TURN_ROUNDABOUT) {
    tft.drawCircle(cx, cy, 20, c);
    tft.drawCircle(cx, cy, 19, c);
    tft.drawCircle(cx, cy, 18, c);
    tft.fillTriangle(cx + 21, cy - 7, cx + 34, cy, cx + 21, cy + 7, c);
    return;
  }

  if (turn == TURN_ARRIVE) {
    tft.fillRect(cx - 4, cy - 24, 8, 30, c);
    tft.fillTriangle(cx, cy - 35, cx - 14, cy - 17, cx + 14, cy - 17, c);
    tft.fillCircle(cx, cy + 18, 8, C_GREEN);
    tft.fillCircle(cx, cy + 18, 3, C_BG);
    return;
  }

  // Defensive fallback.
  tft.fillRect(cx - 4, cy - 12, 8, 34, c);
  tft.fillTriangle(cx, cy - 34, cx - 15, cy - 12, cx + 15, cy - 12, c);
}

void drawStaticFrame() {
  tft.fillScreen(C_BG);

  if (settings.hudStyle == 0) {
    // Balanced
    tft.drawFastHLine(8, 34, 304, C_DARK);
    tft.drawRoundRect(6,   44, 88, 146, 10, C_DARK);
    tft.drawRoundRect(100, 44, 124, 146, 10, C_BLUE2);
    tft.drawRoundRect(230, 44, 84, 146, 10, C_DARK);
    tft.drawFastHLine(8, 198, 304, C_DARK);
  } else if (settings.hudStyle == 1) {
    // Navigation focus
    tft.drawFastHLine(8, 39, 304, C_DARK);
    tft.drawRoundRect(6,   48, 78, 142, 10, C_DARK);
    tft.drawRoundRect(90,  48, 148, 142, 12, C_BLUE2);
    tft.drawRoundRect(244, 48, 70, 142, 10, C_DARK);
    tft.drawFastHLine(8, 199, 304, C_DARK);
  } else {
    // Minimal
    tft.drawFastHLine(8, 31, 304, C_DARK);
    tft.drawFastVLine(103, 39, 153, C_DARK);
    tft.drawFastVLine(238, 39, 153, C_DARK);
    tft.drawFastHLine(8, 199, 304, C_DARK);
  }
}

void drawTopPanel() {
  const int headerH = settings.hudStyle == 1 ? 39 : (settings.hudStyle == 2 ? 31 : 34);
  tft.fillRect(0, 0, 320, headerH, C_BG);

  String road = settings.showRoad ? normalizeRoadName(hud.road) : "";
  if (!road.length()) road = "WAZE HUD";
  if (road.length() > 30) road = road.substring(0, 30);

  if (settings.hudStyle == 0) {
    tft.fillRoundRect(8, 7, 5, 20, 2, C_BLUE);
    smoothText(road, 20, 23, &FreeSans9pt7b, C_WHITE);
  } else if (settings.hudStyle == 1) {
    smoothText(road, 10, 25, &FreeSans9pt7b, C_WHITE);
  } else {
    smoothText(road, 8, 22, &FreeSans9pt7b, C_WHITE);
  }
}

void drawMiniSpeedLimit(int cx, int cy, int limit, int radius) {
  if (limit <= 0) return;
  tft.fillCircle(cx, cy, radius, C_RED);
  tft.fillCircle(cx, cy, radius - 3, C_WHITE);

  String n = String(limit);
  const GFXfont *font = &FreeSans9pt7b;
  tft.setFont(font);
  tft.setTextColor(ILI9341_BLACK);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(n, 0, 0, &x1, &y1, &w, &h);
  int baseline = cy + (int)h / 2 - 1;
  tft.setCursor(cx - w / 2, baseline);
  tft.print(n);
}

String compactDistance(int m) {
  if (m < 0) return "";
  if (m < 1000) return String(m) + "m";
  return String(m / 1000.0f, 1) + "k";
}

void drawSpeedPanel() {
  String speed = String(max(0, hud.speed));
  uint16_t speedColor = hud.overSpeed ? C_RED : C_WHITE;

  if (settings.hudStyle == 0) {
    tft.fillRoundRect(7, 45, 86, 144, 9, C_BG);
    tft.drawRoundRect(6, 44, 88, 146, 10, hud.overSpeed ? C_RED : C_DARK);

    smoothTextCentered("SPEED", 6, 63, 88, &FreeSans9pt7b, C_GREY);
    smoothTextCentered(speed, 6, 111, 88, &FreeSansBold18pt7b, speedColor);
    smoothTextCentered("km/h", 6, 128, 88, &FreeSans9pt7b, C_GREY);

    tft.drawFastVLine(50, 136, 46, C_DARK);
    smoothTextCentered("NOW", 8, 148, 39, &FreeSans9pt7b, C_WHITE);
    smoothTextCentered("NEXT", 52, 148, 39, &FreeSans9pt7b, hud.nextSpeedLimit > 0 ? C_BLUE : C_GREY);

    if (settings.showSpeedLimit && hud.speedLimit > 0) drawMiniSpeedLimit(28, 169, hud.speedLimit, 15);
    else smoothTextCentered("--", 8, 172, 39, &FreeSans9pt7b, C_GREY);

    if (settings.showSpeedLimit && hud.nextSpeedLimit > 0 && hud.nextSpeedLimit != hud.speedLimit) {
      drawMiniSpeedLimit(72, 169, hud.nextSpeedLimit, 14);
      String d = compactDistance(hud.nextSpeedDistanceM);
      if (d.length()) smoothTextCentered(d, 52, 188, 39, &FreeSans9pt7b, C_GREY);
    } else {
      smoothTextCentered("--", 52, 172, 39, &FreeSans9pt7b, C_GREY);
    }
    return;
  }

  if (settings.hudStyle == 1) {
    tft.fillRoundRect(7, 49, 76, 140, 9, C_BG);
    tft.drawRoundRect(6, 48, 78, 142, 10, hud.overSpeed ? C_RED : C_DARK);

    smoothTextCentered("SPEED", 6, 66, 78, &FreeSans9pt7b, C_GREY);
    smoothTextCentered(speed, 6, 116, 78, &FreeSansBold18pt7b, speedColor);
    smoothTextCentered("km/h", 6, 134, 78, &FreeSans9pt7b, C_GREY);

    if (settings.showSpeedLimit && hud.speedLimit > 0) {
      smoothTextCentered("LIMIT", 6, 151, 78, &FreeSans9pt7b, C_GREY);
      drawMiniSpeedLimit(45, 171, hud.speedLimit, 16);
    }
    return;
  }

  // Minimal
  tft.fillRect(0, 32, 102, 166, C_BG);
  smoothTextCentered("SPEED", 0, 54, 102, &FreeSans9pt7b, C_GREY);
  smoothTextCentered(speed, 0, 115, 102, &FreeSansBold18pt7b, speedColor);
  smoothTextCentered("km/h", 0, 134, 102, &FreeSans9pt7b, C_GREY);

  if (settings.showSpeedLimit && hud.speedLimit > 0) {
    smoothTextCentered("NOW", 0, 157, 51, &FreeSans9pt7b, C_GREY);
    drawMiniSpeedLimit(28, 179, hud.speedLimit, 15);
  }
  if (settings.showSpeedLimit && hud.nextSpeedLimit > 0 && hud.nextSpeedLimit != hud.speedLimit) {
    smoothTextCentered("NEXT", 51, 157, 51, &FreeSans9pt7b, C_BLUE);
    drawMiniSpeedLimit(76, 179, hud.nextSpeedLimit, 14);
  }
}

void drawNavPanel() {
  String dist = formatDistance(hud.distanceM);

  if (settings.hudStyle == 0) {
    tft.fillRoundRect(101, 45, 122, 144, 9, C_BG);
    tft.drawRoundRect(100, 44, 124, 146, 10, C_BLUE2);
    smoothText("NEXT", 111, 64, &FreeSans9pt7b, C_BLUE);
    smoothTextRight(dist, 214, 64, &FreeSans9pt7b, C_YELLOW);
    drawArrow(hud.turn, 162, 133);
    return;
  }

  if (settings.hudStyle == 1) {
    tft.fillRoundRect(91, 49, 146, 140, 11, C_BG);
    tft.drawRoundRect(90, 48, 148, 142, 12, C_BLUE2);
    smoothText("NEXT TURN", 102, 68, &FreeSans9pt7b, C_BLUE);
    smoothTextRight(dist, 226, 69, &FreeSansBold12pt7b, C_YELLOW);
    drawArrow(hud.turn, 164, 132);

    String road = settings.showRoad ? normalizeRoadName(hud.road) : "";
    if (road.length() > 18) road = road.substring(0, 18);
    if (road.length()) smoothTextCentered(road, 94, 181, 140, &FreeSans9pt7b, C_WHITE);
    return;
  }

  // Minimal
  tft.fillRect(104, 32, 133, 166, C_BG);
  smoothText("NEXT", 114, 54, &FreeSans9pt7b, C_BLUE);
  smoothTextRight(dist, 229, 58, &FreeSansBold12pt7b, C_YELLOW);
  drawArrow(hud.turn, 171, 124);

  String road = settings.showRoad ? normalizeRoadName(hud.road) : "";
  if (road.length() > 16) road = road.substring(0, 16);
  if (road.length()) smoothTextCentered(road, 106, 184, 129, &FreeSans9pt7b, C_WHITE);
}

void drawAlertPanel(bool linkLost) {
  int x = settings.hudStyle == 1 ? 244 : (settings.hudStyle == 2 ? 239 : 230);
  int w = settings.hudStyle == 1 ? 70 : (settings.hudStyle == 2 ? 81 : 84);
  int cy = 77;

  if (settings.hudStyle == 0) {
    tft.fillRoundRect(231, 45, 82, 144, 9, C_BG);
    tft.drawRoundRect(230, 44, 84, 146, 10, linkLost ? C_RED : C_DARK);
  } else if (settings.hudStyle == 1) {
    tft.fillRoundRect(245, 49, 68, 140, 9, C_BG);
    tft.drawRoundRect(244, 48, 70, 142, 10, linkLost ? C_RED : C_DARK);
  } else {
    tft.fillRect(239, 32, 81, 166, C_BG);
  }

  if (linkLost) {
    tft.fillCircle(x + w/2, cy, 5, C_RED);
    smoothTextCentered("LINK", x, 108, w, &FreeSans9pt7b, C_WHITE);
    smoothTextCentered("LOST", x, 134, w, &FreeSansBold12pt7b, C_RED);
    return;
  }

  if (hud.alertCode != 0 && alertEnabled(hud.alert)) {
    drawWazeAlertIcon(hud.alertCode, x + w/2, cy);

    String label = String(hlpAlertLabel(hud.alertCode));
    if (label.length() > 10) label = label.substring(0, 10);
    smoothTextCentered(label, x, 112, w, &FreeSans9pt7b, C_WHITE);

    if (hud.alertValue >= 0 && (hud.alertCode == 8 || hud.alertCode == 22)) {
      smoothTextCentered(String(hud.alertValue) + " km/h", x, 137, w, &FreeSans9pt7b, C_YELLOW);
      smoothTextCentered(formatDistance(hud.alertDistanceM), x, 161, w, &FreeSans9pt7b, C_GREY);
    } else if (hud.alertCode == 6 && hud.alertSeverity > 0) {
      smoothTextCentered("JAM " + String(hud.alertSeverity) + "/5", x, 137, w, &FreeSans9pt7b, C_YELLOW);
      String extra = hud.alertDelayMin >= 0 ? ("+" + String(hud.alertDelayMin) + " min") : formatDistance(hud.alertDistanceM);
      smoothTextCentered(extra, x, 161, w, &FreeSans9pt7b, C_GREY);
    } else {
      smoothTextCentered(formatDistance(hud.alertDistanceM), x, 143, w, &FreeSansBold12pt7b, C_YELLOW);
    }
  } else {
    // No alert: keep this area intentionally clean. Connectivity/IP belongs
    // on the boot/settings screen, not on the driving HUD.
  }
}

void drawFooterPanel() {
  tft.fillRect(0, 199, 320, 41, C_BG);
  tft.drawFastHLine(8, 198, 304, C_DARK);

  if (settings.hudStyle == 2) {
    String left = "LEFT " + String(hud.remainingKm, 1) + " km";
    String eta = settings.showEta ? ("ETA " + hud.eta) : "ETA --:--";
    smoothText(left, 10, 226, &FreeSans9pt7b, C_WHITE);
    smoothTextRight(eta, 310, 226, &FreeSans9pt7b, C_BLUE);
    return;
  }

  smoothText("LEFT", 10, 214, &FreeSans9pt7b, C_GREY);
  smoothText(String(hud.remainingKm, 1) + " km", 10, 235, &FreeSans9pt7b, C_WHITE);

  smoothText("ETA", 121, 214, &FreeSans9pt7b, C_GREY);
  smoothText(settings.showEta ? hud.eta : "--:--", 121, 235, &FreeSans9pt7b, C_BLUE);

  String route = settings.showRoute ? cleanText(hud.route) : "";
  if (route.length() > 8) route = route.substring(0, 8);
  smoothText("ROUTE", 236, 214, &FreeSans9pt7b, C_GREY);
  smoothTextRight(route.length() ? route : "--", 310, 235, &FreeSans9pt7b, C_WHITE);
}

void drawHud() {
  if (!hud.valid) return;

  bool linkLost = millis() - hud.updatedAt > HUD_TIMEOUT_MS;
  bool layoutChanged = settings.hudStyle != renderedSettings.hudStyle;
  bool first = !hudRenderValid || layoutChanged;

  bool topDirty =
    first ||
    hud.road != renderedHud.road ||
    settings.showRoad != renderedSettings.showRoad;

  bool speedDirty =
    first ||
    hud.speed != renderedHud.speed ||
    hud.speedLimit != renderedHud.speedLimit ||
    hud.overSpeed != renderedHud.overSpeed ||
    hud.nextSpeedLimit != renderedHud.nextSpeedLimit ||
    hud.nextSpeedDistanceM != renderedHud.nextSpeedDistanceM ||
    settings.showSpeedLimit != renderedSettings.showSpeedLimit;

  bool navDirty =
    first ||
    hud.turn != renderedHud.turn;

  bool alertSettingChanged =
    settings.alertPolice != renderedSettings.alertPolice ||
    settings.alertCamera != renderedSettings.alertCamera ||
    settings.alertCrash != renderedSettings.alertCrash ||
    settings.alertTraffic != renderedSettings.alertTraffic ||
    settings.alertRoadworks != renderedSettings.alertRoadworks ||
    settings.alertHazard != renderedSettings.alertHazard;

  bool alertDirty =
    first ||
    hud.alertCode != renderedHud.alertCode ||
    hud.alert != renderedHud.alert ||
    hud.alertDistanceM != renderedHud.alertDistanceM ||
    hud.alertValue != renderedHud.alertValue ||
    hud.alertSeverity != renderedHud.alertSeverity ||
    hud.alertDelayMin != renderedHud.alertDelayMin ||
    hud.alertCount != renderedHud.alertCount ||
    linkLost != renderedLinkLost ||
    alertSettingChanged;

  bool footerDirty =
    first ||
    fabsf(hud.remainingKm - renderedHud.remainingKm) > 0.01f ||
    hud.eta != renderedHud.eta ||
    hud.route != renderedHud.route ||
    settings.showEta != renderedSettings.showEta ||
    settings.showRoute != renderedSettings.showRoute;

  if (!topDirty && !speedDirty && !navDirty && !alertDirty && !footerDirty) return;

  if (first) drawStaticFrame();
  if (topDirty) drawTopPanel();
  if (speedDirty) drawSpeedPanel();
  if (navDirty) drawNavPanel();
  if (alertDirty) drawAlertPanel(linkLost);
  if (footerDirty) drawFooterPanel();

  renderedHud = hud;
  renderedSettings = settings;
  renderedLinkLost = linkLost;
  hudRenderValid = true;
}
void drawWaiting() {
  hudRenderValid = false;
  tft.fillScreen(C_BG);

  String ip = apMode ? WiFi.softAPIP().toString() :
              (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "connecting...");

  tft.fillRoundRect(14, 18, 292, 204, 16, C_PANEL);
  tft.drawRoundRect(14, 18, 292, 204, 16, C_DARK);
  tft.fillRoundRect(29, 33, 6, 44, 3, C_BLUE);

  smoothText("WAZE HUD", 48, 61, &FreeSansBold18pt7b, C_WHITE);
  smoothText(String("v") + FW_VERSION, 50, 82, &FreeSans9pt7b, C_GREY);

  tft.fillCircle(42, 110, 5, bleConnected ? C_GREEN : C_YELLOW);
  smoothText(bleConnected ? "BLE CONNECTED" : "BLE WAITING", 56, 115, &FreeSans9pt7b, C_WHITE);

  bool wifiOk = WiFi.status() == WL_CONNECTED;
  tft.fillCircle(42, 141, 5, wifiOk ? C_GREEN : (apMode ? C_YELLOW : C_GREY));
  String wifiLabel = wifiOk ? "WIFI CONNECTED" : (apMode ? "SETUP AP" : "WIFI CONNECTING");
  smoothText(wifiLabel, 56, 146, &FreeSans9pt7b, C_WHITE);

  smoothText("WEB IP", 30, 173, &FreeSans9pt7b, C_GREY);
  tft.fillRoundRect(93, 154, 193, 30, 7, C_BG);
  smoothText(ip, 104, 176, &FreeSansBold12pt7b, C_BLUE);

  smoothText(apMode ? "AP WAZE-HUD / pass 12345678" : "Open IP for settings / OTA",
             30, 207, &FreeSans9pt7b, C_GREY);

  shownIp = ip;
  wifiUiDirty = false;
}

class HudBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    bleHlpReady = false;
    bleRxBuffer = "";
    if (bleRxQueue != nullptr) xQueueReset(bleRxQueue);
    lastBleDevNotify = 0;
    if (!hud.valid) drawWaiting();
    Serial.println("BLE HLP client connected");
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    bleHlpReady = false;
    bleRxBuffer = "";
    if (bleRxQueue != nullptr) xQueueReset(bleRxQueue);
    if (!hud.valid) drawWaiting();
    BLEDevice::getAdvertising()->start();
    Serial.println("BLE HLP client disconnected; advertising restarted");
  }
};

class HudBleTxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
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

  BLEDevice::init(BLE_DEVICE_NAME);

  BLEServer *bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new HudBleServerCallbacks());

  BLEService *service = bleServer->createService(BLE_SERVICE_UUID);

  // Android -> HUD. WazeMod requires write WITH response.
  BLECharacteristic *tx = service->createCharacteristic(
    BLE_TX_UUID,
    BLECharacteristic::PROPERTY_WRITE
  );
  tx->setCallbacks(new HudBleTxCallbacks());

  // HUD -> Android. WazeMod requires notification + CCCD 0x2902.
  bleNotifyCharacteristic = service->createCharacteristic(
    BLE_RX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  bleNotifyCharacteristic->addDescriptor(new BLE2902());

  // Optional capabilities characteristic defined by HLP/1.
  BLECharacteristic *caps = service->createCharacteristic(
    BLE_CAPS_UUID,
    BLECharacteristic::PROPERTY_READ
  );
  caps->setValue("{\"v\":1,\"caps\":{\"transport\":\"ble\",\"maxFrame\":512}}\n");

  service->start();

  bleLocalAddress = String(BLEDevice::getAddress().toString().c_str());

  // Build the ADV payload explicitly so WazeMod's BLE picker can filter on the
  // HLP service UUID without depending on automatic payload packing.
  BLEAdvertisementData advData;
  advData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advData.setCompleteServices(BLEUUID(BLE_SERVICE_UUID));

  // Keep the name in scan response; ADV remains small and always contains the
  // full 128-bit HLP UUID used by WazeMod's BLE filter.
  BLEAdvertisementData scanData;
  scanData.setName(BLE_DEVICE_NAME);

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->setAdvertisementData(advData);
  advertising->setScanResponseData(scanData);
  advertising->start();

  Serial.print("BLE HLP/1 advertising as WazeHUD, address: ");
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

  int total = http.getSize();
  otaBytesTotal = total > 0 ? (size_t)total : latestFirmwareSize;

  Serial.print("OTA content length: ");
  Serial.println(total);

  if (total > 0 && (size_t)total > freeSketch) {
    updateMessage = "Firmware vượt quá OTA partition";
    otaStatus = "failed";
    http.end();
    return false;
  }

  size_t beginSize = total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN;
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
  updateMessage = "Đang ghi firmware...";

  size_t written = Update.writeStream(*http.getStreamPtr());
  otaBytesWritten = written;

  Serial.print("OTA written: ");
  Serial.print((unsigned long)written);
  Serial.print("/");
  Serial.println(total);

  bool lengthOK = total <= 0 || written == (size_t)total;
  bool endOK = Update.end(true);
  bool finished = Update.isFinished();

  http.end();

  if (!lengthOK || !endOK || !finished) {
    int errCode = Update.getError();
    updateMessage = "OTA ghi lỗi " + String(errCode) + " (" +
                    String((unsigned long)written) + "/" +
                    String(total) + " bytes)";
    otaStatus = "failed";

    Serial.print("OTA failed: lengthOK=");
    Serial.print(lengthOK);
    Serial.print(" endOK=");
    Serial.print(endOK);
    Serial.print(" finished=");
    Serial.print(finished);
    Serial.print(" error=");
    Serial.println(errCode);
    Update.printError(Serial);

    if (!endOK) Update.abort();
    return false;
  }

  otaStatus = "success";
  updateMessage = "Cập nhật v" + latestVersion + " thành công";
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
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}.status{padding:12px;border-radius:12px;background:#09141e;color:var(--muted);margin-top:8px}.ok{color:var(--green)}.warn{color:#ffd34d}
@media(max-width:560px){.grid{grid-template-columns:1fr}}
</style></head><body><main>
<header><div><div class="brand">WAZE <span style="color:var(--cyan)">HUD</span></div><div class="sub">ESP32 DevKit V1 · ILI9341 320×240</div></div><div class="pill">%IP%</div></header>

<form method="post" action="/settings">
<div class="card"><h2>Hiển thị HUD</h2>
<div class="row"><div><b>Kiểu hiển thị</b><div class="sub">Đổi bố cục HUD, lưu qua lần khởi động sau</div></div><select name="layout"><option value="0" %LAYOUT0%>Balanced</option><option value="1" %LAYOUT1%>Navigation</option><option value="2" %LAYOUT2%>Minimal</option></select></div>
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
async function installUpdate(){if(!confirm("Cập nhật firmware ngay? Không tắt nguồn trong quá trình cập nhật."))return;let e=document.getElementById("updatemsg");e.textContent="Đang gửi lệnh cập nhật...";try{let r=await fetch("/update-online",{method:"POST"});let t=await r.text();e.textContent=t;if(r.ok)setTimeout(pollUpdate,1200)}catch(_){e.textContent="Không gửi được lệnh cập nhật."}}
async function pollUpdate(){let e=document.getElementById("updatemsg");try{let r=await fetch("/update-status",{cache:"no-store"});let j=await r.json();e.textContent=j.message||j.status;if(j.status==="failed")return;if(j.status==="success"){e.textContent="Cập nhật thành công, ESP32 đang khởi động lại...";return}setTimeout(pollUpdate,1500)}catch(_){e.textContent="ESP32 đang cập nhật hoặc khởi động lại...";setTimeout(pollUpdate,2500)}}
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
    settings.hudStyle = constrain(server.arg("layout").toInt(), 0, 2);
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
    d["ota_status"]=otaStatus; d["ota_message"]=updateMessage;
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
  drawWaiting();

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(150);
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
  // ILI9341 normally handles 40 MHz SPI on ESP32; this halves large-region
  // draw time versus the old 20 MHz setting.
  tft.begin(40000000);
  tft.setRotation(1);
  tft.setTextWrap(false);
  tft.fillScreen(C_BG);

  loadAppSettings();
  drawWaiting();
  connectWiFi();
  setupBLE();
  setupServer();
  drawWaiting();
  if (settings.autoUpdateCheck && WiFi.status() == WL_CONNECTED) {
    checkForUpdate();
  }

  Serial.print("Waze HUD IP: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP() : WiFi.softAPIP());
}

void drawOverspeedBorder(bool visible) {
  uint16_t color = visible ? C_RED : C_BG;
  tft.drawRect(0, 0, 320, 240, color);
  tft.drawRect(1, 1, 318, 238, color);
  tft.drawRect(2, 2, 316, 236, color);
}

void updateOverspeedEffect() {
  bool active = hud.valid && hud.overSpeed &&
                (millis() - hud.updatedAt <= HUD_TIMEOUT_MS);

  if (active) {
    if (millis() - lastOverspeedBlink >= 250) {
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

    tft.fillScreen(C_BG);
    smoothTextCentered("UPDATING", 0, 95, 320, &FreeSansBold18pt7b, C_YELLOW);
    smoothTextCentered("DO NOT POWER OFF", 0, 132, 320, &FreeSans9pt7b, C_WHITE);
    smoothTextCentered("v" + latestVersion, 0, 165, 320, &FreeSans9pt7b, C_BLUE);

    bool ok = installOnlineUpdate();
    otaInProgress = false;

    if (ok) {
      smoothTextCentered("DONE - REBOOTING", 0, 203, 320, &FreeSans9pt7b, C_GREEN);
      delay(800);
      ESP.restart();
    } else {
      Serial.print("OTA final error: ");
      Serial.println(updateMessage);
      drawWaiting();
    }
  }

  if (!otaInProgress) maintainWiFi();
  updateOverspeedEffect();

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

  delay(2);
}
