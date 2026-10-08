#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
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
static const char *FW_VERSION = "1.3.2";
static const char *GITHUB_REPO = "ledinhtien219/waze-mod";

SPIClass displaySPI(HSPI);
Adafruit_ILI9341 tft(&displaySPI, TFT_DC, TFT_CS, TFT_RST);
WebServer server(80);
Preferences prefs;

String wifiSSID;
String wifiPASS;
String shownIp = "";
volatile bool wifiUiDirty = false;
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
} settings;

String latestVersion = "";
String latestFirmwareUrl = "";
String updateMessage = "Chưa kiểm tra";
bool updateAvailable = false;

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
  prefs.end();
}


enum TurnType {
  TURN_STRAIGHT,
  TURN_LEFT,
  TURN_RIGHT,
  TURN_SLIGHT_LEFT,
  TURN_SLIGHT_RIGHT,
  TURN_UTURN,
  TURN_ROUNDABOUT
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
TurnType parseTurn(String s);
AlertType parseAlert(String s);
TurnType parseHlpTurn(JsonDocument &doc);
AlertType mapHlpAlert(uint8_t code);
const char* hlpAlertLabel(uint8_t code);
bool alertEnabled(AlertType a);
const char* alertLabel(AlertType a);
void drawAlertGlyph(AlertType a, int cx, int cy);
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
  // Built-in Adafruit GFX font is ASCII only.
  // Preserve ASCII and replace unsupported UTF-8 bytes with spaces.
  String out;
  out.reserve(s.length());
  bool lastSpace = false;
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c >= 32 && c <= 126) {
      out += (char)c;
      lastSpace = false;
    } else if (!lastSpace) {
      out += ' ';
      lastSpace = true;
    }
  }
  out.trim();
  return out;
}

TurnType parseTurn(String s) {
  s.toLowerCase();
  if (s == "left") return TURN_LEFT;
  if (s == "right") return TURN_RIGHT;
  if (s == "slight_left") return TURN_SLIGHT_LEFT;
  if (s == "slight_right") return TURN_SLIGHT_RIGHT;
  if (s == "uturn") return TURN_UTURN;
  if (s == "roundabout") return TURN_ROUNDABOUT;
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
  // HLP/1 uses the numeric trn enum. Map additive codes to the closest
  // primitive supported by this renderer.
  if (!doc["trn"].isNull()) {
    int code = (int)doc["trn"];
    switch (code) {
      case 1: return TURN_STRAIGHT;       // CONTINUE
      case 2: return TURN_LEFT;
      case 3: return TURN_RIGHT;
      case 4: return TURN_SLIGHT_LEFT;
      case 5: return TURN_SLIGHT_RIGHT;
      case 6: return TURN_LEFT;           // SHARP_LEFT
      case 7: return TURN_RIGHT;          // SHARP_RIGHT
      case 8:
      case 9: return TURN_UTURN;
      case 10:
      case 11:
      case 12:
      case 19:
      case 20: return TURN_ROUNDABOUT;
      case 13:
      case 15: return TURN_SLIGHT_LEFT;   // KEEP/EXIT LEFT
      case 14:
      case 16: return TURN_SLIGHT_RIGHT;  // KEEP/EXIT RIGHT
      case 17: return TURN_STRAIGHT;      // ARRIVE
      default: return TURN_STRAIGHT;
    }
  }

  // Compatibility with local HTTP test payloads.
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
  tft.fillRect(cx - 13, cy - 9, 26, 18, color);
  tft.fillRect(cx - 8, cy - 13, 10, 5, color);
  tft.fillCircle(cx, cy, 7, C_BG);
  tft.drawCircle(cx, cy, 4, color);
}

void drawAlertGlyph(AlertType a, int cx, int cy) {
  if (a == ALERT_NONE) return;
  if (a == ALERT_CAMERA) { drawCameraGlyph(cx, cy, C_YELLOW); return; }
  drawTriangleSign(cx, cy, 18);
  uint16_t ink = ILI9341_BLACK;
  switch (a) {
    case ALERT_POLICE:
      tft.fillCircle(cx, cy + 3, 6, ink);
      tft.fillRect(cx - 8, cy + 9, 16, 8, ink);
      tft.drawLine(cx - 9, cy - 5, cx + 9, cy - 5, ink);
      tft.drawLine(cx - 5, cy - 9, cx + 5, cy - 9, ink);
      break;
    case ALERT_ROADWORKS:
      tft.fillCircle(cx, cy - 2, 3, ink);
      tft.drawLine(cx, cy + 2, cx - 6, cy + 13, ink);
      tft.drawLine(cx, cy + 2, cx + 6, cy + 12, ink);
      tft.drawLine(cx - 5, cy + 4, cx + 8, cy + 1, ink);
      break;
    case ALERT_POTHOLE:
      tft.drawLine(cx - 10, cy + 8, cx - 3, cy + 4, ink);
      tft.drawLine(cx - 3, cy + 4, cx + 2, cy + 9, ink);
      tft.drawLine(cx + 2, cy + 9, cx + 10, cy + 5, ink);
      break;
    case ALERT_CRASH:
      tft.fillCircle(cx - 6, cy + 7, 5, ink);
      tft.fillCircle(cx + 6, cy + 7, 5, ink);
      tft.drawLine(cx - 8, cy, cx + 8, cy + 10, ink);
      tft.drawLine(cx + 8, cy, cx - 8, cy + 10, ink);
      break;
    case ALERT_CLOSURE:
      tft.fillRect(cx - 11, cy + 1, 22, 7, ink);
      break;
    case ALERT_TRAFFIC:
    case ALERT_BLOCKED_LANE:
      for (int i=-8;i<=8;i+=8) tft.drawLine(cx+i,cy-4,cx+i,cy+12,ink);
      break;
    default:
      tft.setTextColor(ink, C_YELLOW);
      tft.setTextSize(2);
      tft.setCursor(cx - 5, cy - 3);
      tft.print("!");
      break;
  }
}

void drawArrow(TurnType turn, int cx, int cy) {
  uint16_t c = C_BLUE;
  int w = 11;

  if (turn == TURN_STRAIGHT) {
    tft.fillRect(cx - w/2, cy - 21, w, 44, c);
    tft.fillTriangle(cx - 20, cy - 17, cx + 20, cy - 17, cx, cy - 39, c);
    return;
  }

  if (turn == TURN_LEFT || turn == TURN_RIGHT || turn == TURN_SLIGHT_LEFT || turn == TURN_SLIGHT_RIGHT) {
    bool right = (turn == TURN_RIGHT || turn == TURN_SLIGHT_RIGHT);
    int dir = right ? 1 : -1;
    tft.fillRect(cx - w/2, cy, w, 30, c);
    tft.fillRect(right ? cx : cx - 31, cy - 7, 31, w, c);
    int tipX = cx + dir * 43;
    int baseX = cx + dir * 23;
    tft.fillTriangle(tipX, cy - 2, baseX, cy - 17, baseX, cy + 14, c);
    return;
  }

  if (turn == TURN_UTURN) {
    tft.fillRect(cx + 8, cy - 4, w, 35, c);
    tft.drawCircle(cx, cy - 4, 23, c);
    tft.fillCircle(cx, cy - 4, 16, C_BG);
    tft.fillRect(cx - 29, cy - 10, 29, 23, C_BG);
    tft.fillTriangle(cx - 29, cy - 4, cx - 10, cy - 18, cx - 10, cy + 9, c);
    return;
  }

  // roundabout
  tft.drawCircle(cx, cy, 24, c);
  tft.drawCircle(cx, cy, 23, c);
  tft.drawCircle(cx, cy, 22, c);
  tft.fillTriangle(cx + 25, cy - 7, cx + 39, cy - 1, cx + 25, cy + 7, c);
}

void drawStaticFrame() {
  tft.fillScreen(C_BG);

  // Top navigation strip
  tft.drawFastHLine(8, 38, 304, C_DARK);

  // Main cards
  tft.drawRoundRect(6, 47, 84, 145, 10, C_DARK);
  tft.drawRoundRect(96, 47, 130, 145, 10, C_BLUE2);
  tft.drawRoundRect(232, 47, 82, 145, 10, C_DARK);

  // Footer
  tft.drawFastHLine(8, 201, 304, C_DARK);
}

void drawTopPanel() {
  tft.fillRect(0, 0, 320, 38, C_BG);

  String road = settings.showRoad ? cleanText(hud.road) : "";
  if (!road.length()) road = "WAZE HUD";
  if (road.length() > 25) road = road.substring(0, 25);

  // Small accent marker
  tft.fillRoundRect(8, 8, 5, 22, 2, C_BLUE);

  tft.setTextColor(C_WHITE, C_BG);
  tft.setTextSize(road.length() > 18 ? 1 : 2);
  tft.setCursor(20, road.length() > 18 ? 13 : 10);
  tft.print(road);

  String dist = formatDistance(hud.distanceM);
  tft.setTextSize(2);
  tft.setTextColor(C_YELLOW, C_BG);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(dist, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(309 - w, 10);
  tft.print(dist);
}

void drawSpeedPanel() {
  tft.fillRoundRect(7, 48, 82, 143, 9, C_BG);
  tft.drawRoundRect(6, 47, 84, 145, 10, C_DARK);

  tft.setTextColor(C_GREY, C_BG);
  tft.setTextSize(1);
  tft.setCursor(18, 58);
  tft.print("SPEED");

  String speed = String(max(0, hud.speed));
  tft.setTextColor(C_WHITE, C_BG);
  tft.setTextSize(speed.length() >= 3 ? 4 : 5);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(speed, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(48 - w / 2, 78);
  tft.print(speed);

  tft.setTextColor(C_GREY, C_BG);
  tft.setTextSize(1);
  textCentered("km/h", 6, 126, 84, 1, C_GREY);

  if (settings.showSpeedLimit) {
    // Smaller road-sign style limit so the speed remains the visual priority.
    int cx = 48;
    int cy = 161;
    tft.fillCircle(cx, cy, 22, C_RED);
    tft.fillCircle(cx, cy, 17, C_WHITE);

    String limit = hud.speedLimit > 0 ? String(hud.speedLimit) : "--";
    tft.setTextColor(ILI9341_BLACK, C_WHITE);
    tft.setTextSize(hud.speedLimit >= 100 ? 1 : 2);
    tft.getTextBounds(limit, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(cx - w / 2, cy - h / 2);
    tft.print(limit);
  }
}

void drawNavPanel() {
  tft.fillRoundRect(97, 48, 128, 143, 9, C_BG);
  tft.drawRoundRect(96, 47, 130, 145, 10, C_BLUE2);

  tft.setTextColor(C_BLUE, C_BG);
  tft.setTextSize(1);
  tft.setCursor(108, 58);
  tft.print("NEXT TURN");

  // Large maneuver glyph; centered lower so the card breathes.
  drawArrow(hud.turn, 161, 121);

  // Add a subtle baseline under the maneuver.
  tft.drawFastHLine(116, 174, 90, C_DARK);
}

void drawAlertPanel(bool linkLost) {
  tft.fillRoundRect(233, 48, 80, 143, 9, C_BG);
  tft.drawRoundRect(232, 47, 82, 145, 10, linkLost ? C_RED : C_DARK);

  if (linkLost) {
    tft.fillCircle(273, 76, 6, C_RED);
    textCentered("LINK", 232, 93, 82, 1, C_WHITE);
    textCentered("LOST", 232, 108, 82, 2, C_RED);
    textCentered("WAITING", 232, 150, 82, 1, C_GREY);
    return;
  }

  if (hud.alertCode != 0 && alertEnabled(hud.alert)) {
    drawAlertGlyph(hud.alert, 273, 75);

    String label = String(hlpAlertLabel(hud.alertCode));
    if (label.length() > 12) label = label.substring(0, 12);
    textCentered(label, 233, 105, 80, 1, C_WHITE);

    // SPEED_DROP / END_SPEED_RESTRICTION carry a speed value.
    if (hud.alertValue >= 0 && (hud.alertCode == 8 || hud.alertCode == 22)) {
      String value = String(hud.alertValue) + " KM/H";
      textCentered(value, 233, 124, 80, 1, C_YELLOW);
      textCentered(formatDistance(hud.alertDistanceM), 233, 143, 80, 1, C_GREY);
    } else if (hud.alertCode == 6 && hud.alertSeverity > 0) {
      String jam = "JAM " + String(hud.alertSeverity) + "/5";
      textCentered(jam, 233, 125, 80, 1, C_YELLOW);
      if (hud.alertDelayMin >= 0) {
        textCentered("+" + String(hud.alertDelayMin) + " MIN", 233, 144, 80, 1, C_WHITE);
      } else {
        textCentered(formatDistance(hud.alertDistanceM), 233, 144, 80, 1, C_GREY);
      }
    } else {
      textCentered(formatDistance(hud.alertDistanceM), 233, 132, 80, 2, C_YELLOW);
    }
  } else {
    tft.fillCircle(273, 74, 6, C_GREEN);
    textCentered("ONLINE", 232, 92, 82, 1, C_GREEN);
    textCentered("NO ALERT", 232, 132, 82, 1, C_GREY);
  }
}

void drawFooterPanel() {
  tft.fillRect(0, 202, 320, 38, C_BG);
  tft.drawFastHLine(8, 201, 304, C_DARK);

  // LEFT
  tft.setTextColor(C_GREY, C_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 209);
  tft.print("LEFT");

  tft.setTextColor(C_WHITE, C_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 222);
  tft.print(String(hud.remainingKm, 1));
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  tft.print(" km");

  // ETA
  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_BG);
  tft.setCursor(122, 209);
  tft.print("ETA");

  tft.setTextColor(C_BLUE, C_BG);
  tft.setTextSize(2);
  tft.setCursor(122, 222);
  tft.print(settings.showEta ? hud.eta : "--:--");

  // ROUTE
  String route = settings.showRoute ? cleanText(hud.route) : "";
  if (route.length() > 8) route = route.substring(0, 8);

  tft.setTextColor(C_GREY, C_BG);
  tft.setTextSize(1);
  tft.setCursor(246, 209);
  tft.print("ROUTE");

  if (route.length()) {
    tft.setTextColor(C_WHITE, C_BG);
    tft.setTextSize(route.length() > 5 ? 1 : 2);
    int16_t x1, y1;
    uint16_t w, h;
    tft.getTextBounds(route, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor(310 - w, route.length() > 5 ? 225 : 222);
    tft.print(route);
  } else {
    tft.setTextColor(C_GREY, C_BG);
    tft.setTextSize(1);
    tft.setCursor(281, 225);
    tft.print("--");
  }
}

void drawHud() {
  if (!hud.valid) return;

  bool linkLost = millis() - hud.updatedAt > HUD_TIMEOUT_MS;
  bool first = !hudRenderValid;

  bool topDirty =
    first ||
    hud.road != renderedHud.road ||
    hud.distanceM != renderedHud.distanceM ||
    settings.showRoad != renderedSettings.showRoad;

  bool speedDirty =
    first ||
    hud.speed != renderedHud.speed ||
    hud.speedLimit != renderedHud.speedLimit ||
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

  // Brand.
  tft.fillRoundRect(29, 34, 6, 43, 3, C_BLUE);
  tft.setTextColor(C_WHITE, C_PANEL);
  tft.setTextSize(3);
  tft.setCursor(48, 37);
  tft.print("WAZE HUD");

  tft.setTextColor(C_GREY, C_PANEL);
  tft.setTextSize(1);
  tft.setCursor(50, 68);
  tft.print("v");
  tft.print(FW_VERSION);

  // BLE status.
  tft.fillCircle(42, 105, 5, bleConnected ? C_GREEN : C_YELLOW);
  tft.setTextColor(C_WHITE, C_PANEL);
  tft.setTextSize(1);
  tft.setCursor(56, 101);
  tft.print(bleConnected ? "BLE CONNECTED" : "BLE WAITING: WazeHUD");

  // Wi-Fi status and current DHCP/AP IP.
  bool wifiOk = WiFi.status() == WL_CONNECTED;
  tft.fillCircle(42, 137, 5, wifiOk ? C_GREEN : (apMode ? C_YELLOW : C_GREY));
  tft.setTextColor(C_WHITE, C_PANEL);
  tft.setCursor(56, 133);
  if (wifiOk) tft.print("WIFI CONNECTED");
  else if (apMode) tft.print("SETUP AP: WAZE-HUD");
  else tft.print("WIFI CONNECTING");

  tft.setTextColor(C_GREY, C_PANEL);
  tft.setCursor(30, 164);
  tft.print("WEB IP");

  tft.fillRoundRect(85, 153, 201, 29, 7, C_BG);
  tft.setTextColor(C_BLUE, C_BG);
  tft.setTextSize(2);
  tft.setCursor(98, 160);
  tft.print(ip);

  tft.setTextSize(1);
  tft.setTextColor(C_GREY, C_PANEL);
  tft.setCursor(30, 198);
  tft.print(apMode ? "Password: 12345678" : "Open IP above for settings / OTA");

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
  if (WiFi.status() != WL_CONNECTED) {
    updateMessage = "Wi-Fi chưa kết nối";
    return false;
  }
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setUserAgent("ESP32-Waze-HUD");
  String api = String("https://api.github.com/repos/") + GITHUB_REPO + "/releases/latest";
  if (!http.begin(client, api)) {
    updateMessage = "Không mở được GitHub";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    updateMessage = "GitHub HTTP " + String(code);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    updateMessage = "Lỗi dữ liệu phiên bản";
    return false;
  }

  latestVersion = String((const char*)(doc["tag_name"] | ""));
  latestVersion.replace("v", "");
  latestFirmwareUrl = "";
  JsonArray assets = doc["assets"].as<JsonArray>();
  for (JsonObject asset : assets) {
    String name = String((const char*)(asset["name"] | ""));
    if (name == "firmware.bin" || name.endsWith("-firmware.bin")) {
      latestFirmwareUrl = String((const char*)(asset["browser_download_url"] | ""));
      break;
    }
  }

  updateAvailable = latestVersion.length() && latestVersion != FW_VERSION && latestFirmwareUrl.length();
  updateMessage = updateAvailable ? ("Có bản " + latestVersion) : "Đang dùng bản mới nhất";
  return true;
}

bool installOnlineUpdate() {
  if (!latestFirmwareUrl.length() && !checkForUpdate()) return false;
  if (!latestFirmwareUrl.length()) {
    updateMessage = "Release chưa có firmware.bin";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setConnectTimeout(15000);
  if (!http.begin(client, latestFirmwareUrl)) {
    updateMessage = "Không mở được firmware";
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    updateMessage = "Tải firmware lỗi HTTP " + String(code);
    http.end();
    return false;
  }

  int total = http.getSize();
  if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN)) {
    updateMessage = "Không đủ bộ nhớ OTA";
    http.end();
    return false;
  }

  size_t written = Update.writeStream(*http.getStreamPtr());
  bool lengthOK = total <= 0 || written == (size_t)total;
  bool ok = lengthOK && Update.end(true) && Update.isFinished();
  http.end();

  if (!ok) {
    Update.abort();
    updateMessage = "Ghi firmware thất bại";
    return false;
  }
  updateMessage = "Cập nhật thành công";
  return true;
}

String checked(bool v) { return v ? "checked" : ""; }

String pageHtml() {
  String ip = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
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
async function installUpdate(){if(!confirm("Cập nhật firmware ngay? Không tắt nguồn trong quá trình cập nhật."))return;let e=document.getElementById("updatemsg");e.textContent="Đang tải và ghi firmware...";try{let r=await fetch("/update-online",{method:"POST"});e.textContent=await r.text()}catch(_){e.textContent="Kết nối bị đóng. ESP32 có thể đang khởi động lại."}}
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
    checkForUpdate();
    JsonDocument d; d["ok"]=true; d["current"]=FW_VERSION; d["latest"]=latestVersion;
    d["available"]=updateAvailable; d["message"]=updateMessage;
    String out; serializeJson(d,out); server.send(200,"application/json",out);
  });

  server.on("/update-auto", HTTP_POST, []() {
    settings.autoUpdateCheck = server.arg("enabled")=="1";
    prefs.begin("wazehud",false); prefs.putBool("autoupdate",settings.autoUpdateCheck); prefs.end();
    server.send(200,"application/json","{\"ok\":true}");
  });

  server.on("/update-online", HTTP_POST, []() {
    if (!updateAvailable && !checkForUpdate()) { server.send(500,"text/plain; charset=utf-8",updateMessage); return; }
    if (!updateAvailable) { server.send(409,"text/plain; charset=utf-8","Không có bản cập nhật mới."); return; }
    tft.fillScreen(C_BG);
    textCentered("UPDATING",0,78,320,3,C_YELLOW);
    textCentered("DO NOT POWER OFF",0,125,320,2,C_WHITE);
    bool ok = installOnlineUpdate();
    if (!ok) { drawWaiting(); server.send(500,"text/plain; charset=utf-8",updateMessage); return; }
    server.send(200,"text/plain; charset=utf-8","Cập nhật thành công. ESP32 đang khởi động lại...");
    delay(1200); ESP.restart();
  });

  server.on("/state", HTTP_GET, []() {
    JsonDocument d;
    d["version"]=FW_VERSION; d["ip"]=apMode?WiFi.softAPIP().toString():WiFi.localIP().toString();
    d["ble_name"]=BLE_DEVICE_NAME; d["ble_address"]=bleLocalAddress;
    d["wifi"]=WiFi.status()==WL_CONNECTED; d["ble"]=bleConnected; d["hud"]=hud.valid; d["age_ms"]=hud.valid?millis()-hud.updatedAt:0;
    d["alert_code"]=hud.alertCode; d["alert_distance_m"]=hud.alertDistanceM;
    d["alert_value"]=hud.alertValue; d["alert_count"]=hud.alertCount;
    String out; serializeJson(d,out); server.send(200,"application/json",out);
  });

  server.onNotFound([](){if(apMode){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}else server.send(404,"text/plain","Not found");});
  server.begin();
}

void connectWiFi() {
  prefs.begin("wazehud", true);
  wifiSSID = prefs.getString("ssid", "");
  wifiPASS = prefs.getString("pass", "");
  prefs.end();

  if (wifiSSID.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());

    // Show Wi-Fi startup immediately instead of leaving a blank/static screen.
    drawWaiting();

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
      delay(100);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    shownIp = WiFi.localIP().toString();
    wifiUiDirty = true;
    return;
  }

  apMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME, AP_PASS);
  shownIp = WiFi.softAPIP().toString();
  wifiUiDirty = true;
}

void setup() {
  Serial.begin(115200);

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
  Serial.println(apMode ? WiFi.softAPIP() : WiFi.localIP());
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

  // Reflect a new DHCP/AP address on the boot/waiting screen.
  String currentIp = apMode ? WiFi.softAPIP().toString() :
                     (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "connecting...");
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
