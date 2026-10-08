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
static const char *FW_VERSION = "1.2.1";
static const char *GITHUB_REPO = "ledinhtien219/waze-mod";

SPIClass displaySPI(HSPI);
Adafruit_ILI9341 tft(&displaySPI, TFT_DC, TFT_CS, TFT_RST);
WebServer server(80);
Preferences prefs;

String wifiSSID;
String wifiPASS;
bool apMode = false;
bool bleConnected = false;
bool bleHlpReady = false;
String bleRxBuffer;
BLECharacteristic *bleNotifyCharacteristic = nullptr;
uint32_t lastBleDevNotify = 0;

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
bool alertEnabled(AlertType a);
const char* alertLabel(AlertType a);
void drawAlertGlyph(AlertType a, int cx, int cy);
void drawArrow(TurnType turn, int cx, int cy);

struct HudState {
  TurnType turn = TURN_STRAIGHT;
  uint16_t distanceM = 0;
  String road = "";
  int speed = 0;
  int speedLimit = 0;
  float remainingKm = 0;
  String eta = "--:--";
  String route = "";
  AlertType alert = ALERT_NONE;
  uint16_t alertDistanceM = 0;
  uint32_t updatedAt = 0;
  bool valid = false;
} hud;

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
  sendHlpLine(
    String("{\"v\":1,\"t\":\"dev\",\"transport\":\"ble\",\"model\":\"ESP32-WazeHUD\",\"fw\":\"") +
    FW_VERSION +
    "\",\"rate\":4}"
  );
}

TurnType parseHlpTurn(JsonDocument &doc) {
  // Prefer textual maneuver fields when present.
  const char *keys[] = {"turn", "maneuver", "man", "dir"};
  for (const char *key : keys) {
    if (!doc[key].isNull() && doc[key].is<const char*>()) {
      return parseTurn(String((const char*)doc[key]));
    }
  }
  return hud.turn;
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

    // WazeMod uses compact fields. Keep aliases so firmware also tolerates
    // protocol revisions and test payloads.
    if (!doc["lim"].isNull()) hud.speedLimit = constrain((int)doc["lim"], 0, 199);
    else if (!doc["speed_limit"].isNull()) hud.speedLimit = constrain((int)doc["speed_limit"], 0, 199);

    if (!doc["dist"].isNull()) hud.distanceM = constrain((int)doc["dist"], 0, 65000);
    else if (!doc["distance_m"].isNull()) hud.distanceM = constrain((int)doc["distance_m"], 0, 65000);

    if (!doc["st2"].isNull()) hud.road = String((const char*)doc["st2"]);
    else if (!doc["st1"].isNull()) hud.road = String((const char*)doc["st1"]);
    else if (!doc["road"].isNull()) hud.road = String((const char*)doc["road"]);

    if (!doc["eta"].isNull()) hud.eta = String((const char*)doc["eta"]);
    if (!doc["route"].isNull()) hud.route = String((const char*)doc["route"]);

    if (!doc["remaining_km"].isNull()) {
      hud.remainingKm = max(0.0f, (float)doc["remaining_km"]);
    } else if (!doc["rem_km"].isNull()) {
      hud.remainingKm = max(0.0f, (float)doc["rem_km"]);
    }

    hud.turn = parseHlpTurn(doc);

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
    if (!alert["type"].isNull()) hud.alert = parseAlert(String((const char*)alert["type"]));
    if (!alert["distance_m"].isNull()) hud.alertDistanceM = constrain((int)alert["distance_m"], 0, 65000);
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

String formatDistance(uint16_t m) {
  if (m == 0) return "--";
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
  drawTriangleSign(cx, cy, 20);
  uint16_t ink = ILI9341_BLACK;
  switch (a) {
    case ALERT_CAMERA:
      drawCameraGlyph(cx, cy + 5, ink);
      break;
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
  int w = 13;

  if (turn == TURN_STRAIGHT) {
    tft.fillRect(cx - w/2, cy - 25, w, 54, c);
    tft.fillTriangle(cx - 25, cy - 20, cx + 25, cy - 20, cx, cy - 48, c);
    return;
  }

  if (turn == TURN_LEFT || turn == TURN_RIGHT || turn == TURN_SLIGHT_LEFT || turn == TURN_SLIGHT_RIGHT) {
    bool right = (turn == TURN_RIGHT || turn == TURN_SLIGHT_RIGHT);
    int dir = right ? 1 : -1;
    tft.fillRect(cx - w/2, cy, w, 36, c);
    tft.fillRect(right ? cx : cx - 38, cy - 8, 38, w, c);
    int tipX = cx + dir * 52;
    int baseX = cx + dir * 28;
    tft.fillTriangle(tipX, cy - 2, baseX, cy - 22, baseX, cy + 18, c);
    return;
  }

  if (turn == TURN_UTURN) {
    tft.fillRect(cx + 10, cy - 5, w, 43, c);
    tft.drawCircle(cx, cy - 5, 27, c);
    tft.fillCircle(cx, cy - 5, 19, C_BG);
    tft.fillRect(cx - 35, cy - 12, 35, 28, C_BG);
    tft.fillTriangle(cx - 35, cy - 5, cx - 12, cy - 22, cx - 12, cy + 12, c);
    return;
  }

  // roundabout
  tft.drawCircle(cx, cy, 29, c);
  tft.drawCircle(cx, cy, 28, c);
  tft.drawCircle(cx, cy, 27, c);
  tft.fillTriangle(cx + 31, cy - 9, cx + 48, cy - 1, cx + 31, cy + 8, c);
}

void drawStaticFrame() {
  tft.fillScreen(C_BG);
  tft.drawFastVLine(68, 0, 200, C_DARK);
  tft.drawFastVLine(222, 0, 200, C_DARK);
  tft.drawRoundRect(2, 202, 316, 36, 8, C_BLUE2);
}

void drawHud() {
  drawStaticFrame();

  // Left: speed
  tft.setTextColor(C_WHITE, C_BG);
  tft.setTextSize(5);
  String speed = String(max(0, hud.speed));
  int16_t x1,y1; uint16_t w,h;
  tft.getTextBounds(speed,0,0,&x1,&y1,&w,&h);
  tft.setCursor(34 - w/2, 7);
  tft.print(speed);

  tft.setTextSize(1);
  tft.setCursor(19, 48);
  tft.print("km/h");

  if (settings.showSpeedLimit) drawSpeedLimit(34, 90, hud.speedLimit);

  if (hud.alert == ALERT_CAMERA && settings.alertCamera) {
    drawCameraGlyph(34, 137, C_WHITE);
    textCentered(formatDistance(hud.alertDistanceM), 0, 151, 68, 1, C_WHITE);
  }

  // Center: maneuver
  textCentered(formatDistance(hud.distanceM), 69, 5, 153, 3, C_YELLOW);
  drawArrow(hud.turn, 145, 88);

  String road = settings.showRoad ? cleanText(hud.road) : "";
  if (road.length() > 21) road = road.substring(0,21);
  textCentered(road, 72, 165, 148, road.length() > 16 ? 1 : 2, C_WHITE);

  // Right: nearest alert
  if (hud.alert != ALERT_NONE && alertEnabled(hud.alert)) {
    drawAlertGlyph(hud.alert, 246, 34);
    tft.setTextSize(1);
    tft.setTextColor(C_WHITE, C_BG);
    tft.setCursor(270, 18);
    tft.print(alertLabel(hud.alert));
    tft.setTextColor(C_BLUE, C_BG);
    tft.setTextSize(2);
    tft.setCursor(267, 41);
    String d = formatDistance(hud.alertDistanceM);
    if (d.length() > 7) {
      tft.setTextSize(1);
      tft.setCursor(270, 45);
    }
    tft.print(d);
  } else {
    textCentered("NO ALERT", 224, 35, 94, 1, C_GREY);
  }

  // Additional status blocks
  tft.drawRoundRect(229, 73, 84, 37, 5, C_DARK);
  textCentered("NAV ACTIVE", 230, 82, 82, 1, C_GREEN);

  tft.drawRoundRect(229, 117, 84, 64, 5, C_DARK);
  textCentered("NEXT", 230, 124, 82, 1, C_GREY);
  String next = formatDistance(hud.distanceM);
  textCentered(next, 230, 143, 82, 2, C_BLUE);

  // Footer
  tft.setTextColor(C_WHITE, C_BG);
  tft.setTextSize(1);
  tft.setCursor(12, 216);
  tft.print("LEFT ");
  tft.setTextColor(C_BLUE, C_BG);
  tft.setTextSize(2);
  tft.print(String(hud.remainingKm,1));

  tft.setTextColor(C_GREY, C_BG);
  tft.setTextSize(1);
  tft.setCursor(94, 216);
  tft.print("KM");

  tft.setTextColor(C_WHITE, C_BG);
  tft.setCursor(124, 216);
  tft.print("ETA ");
  tft.setTextColor(C_BLUE, C_BG);
  tft.setTextSize(2);
  tft.print(settings.showEta ? hud.eta : "--:--");

  String route = settings.showRoute ? cleanText(hud.route) : "";
  if (route.length() > 7) route = route.substring(0,7);
  tft.setTextColor(C_BLUE, C_BG);
  tft.setTextSize(2);
  int16_t rx,ry; uint16_t rw,rh;
  tft.getTextBounds(route,0,0,&rx,&ry,&rw,&rh);
  tft.setCursor(307-rw,216);
  tft.print(route);

  // Link state
  if (hud.valid && millis() - hud.updatedAt > HUD_TIMEOUT_MS) {
    tft.fillRect(229, 184, 84, 14, C_RED);
    textCentered("LINK LOST", 229, 187, 84, 1, C_WHITE);
  }
}

void drawWaiting() {
  tft.fillScreen(C_BG);
  textCentered("WAZE HUD", 0, 36, 320, 4, C_BLUE);
  textCentered(bleConnected ? "BLE CONNECTED" : "BLE: WazeHUD", 0, 100, 320, 2, bleConnected ? C_GREEN : C_WHITE);
  String ip = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  textCentered(ip, 0, 142, 320, 2, C_YELLOW);
  textCentered(apMode ? "Wi-Fi setup + Web Setting" : "Web Setting / OTA", 0, 178, 320, 1, C_GREY);
}

class HudBleServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    bleConnected = true;
    bleHlpReady = false;
    bleRxBuffer = "";
    lastBleDevNotify = 0;
    if (!hud.valid) drawWaiting();
    Serial.println("BLE HLP client connected");
  }

  void onDisconnect(BLEServer *server) override {
    bleConnected = false;
    bleHlpReady = false;
    bleRxBuffer = "";
    if (!hud.valid) drawWaiting();
    BLEDevice::getAdvertising()->start();
    Serial.println("BLE HLP client disconnected; advertising restarted");
  }
};

class HudBleTxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    std::string raw = characteristic->getValue();

    for (size_t i = 0; i < raw.size(); i++) {
      char c = raw[i];

      if (c == '\n') {
        String payload = bleRxBuffer;
        bleRxBuffer = "";
        payload.trim();

        if (payload.length()) {
          Serial.print("HLP RX: ");
          Serial.println(payload);

          bool handled = applyHudPayload(payload);
          if (handled && payload.indexOf("\"t\":\"s\"") >= 0) {
            drawHud();
          }
        }
      } else if (c != '\r') {
        // HLP/1 frames are bounded; drop an overlong/malformed frame safely.
        if (bleRxBuffer.length() < 768) {
          bleRxBuffer += c;
        } else {
          bleRxBuffer = "";
        }
      }
    }
  }
};

void setupBLE() {
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

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  advertising->start();

  Serial.println("BLE HLP/1 advertising as WazeHUD");
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
<div class="status">Android Bridge gửi dữ liệu tới <b>http://%IP%/hud</b><br>Dữ liệu HUD: %HUDSTATUS%</div>
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
    d["wifi"]=WiFi.status()==WL_CONNECTED; d["ble"]=bleConnected; d["hud"]=hud.valid; d["age_ms"]=hud.valid?millis()-hud.updatedAt:0;
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
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 12000) delay(150);
  }

  if (WiFi.status() == WL_CONNECTED) {
    apMode = false;
    return;
  }

  apMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME, AP_PASS);
}

void setup() {
  Serial.begin(115200);

  displaySPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(20000000);
  tft.setRotation(1);
  tft.setTextWrap(false);
  tft.fillScreen(C_BG);

  loadAppSettings();
  setupBLE();
  connectWiFi();
  setupServer();
  drawWaiting();
  if (settings.autoUpdateCheck && WiFi.status() == WL_CONNECTED) {
    checkForUpdate();
  }

  Serial.print("Waze HUD IP: ");
  Serial.println(apMode ? WiFi.softAPIP() : WiFi.localIP());
}

void loop() {
  // Send HLP device declaration repeatedly until WazeMod answers with "hi".
  // Notifications sent before CCCD subscription are harmless; a later retry
  // reaches Android as soon as RX notifications are enabled.
  if (bleConnected && !bleHlpReady && millis() - lastBleDevNotify >= 350) {
    lastBleDevNotify = millis();
    sendHlpDev();
  }

  server.handleClient();

  static uint32_t lastRefresh = 0;
  if (millis() - lastRefresh > 1000) {
    lastRefresh = millis();
    if (hud.valid) drawHud();
  }
  delay(4);
}
