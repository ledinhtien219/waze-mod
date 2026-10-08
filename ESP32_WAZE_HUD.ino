#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <ArduinoJson.h>

// ===== ESP32 DevKit V1 + ILI9341 320x240 =====
#define TFT_CS   27
#define TFT_RST  25
#define TFT_DC   26
#define TFT_MOSI 13
#define TFT_SCK  14
#define TFT_MISO 35

static const char *AP_NAME = "WAZE-HUD";
static const char *AP_PASS = "12345678";
static const uint32_t HUD_TIMEOUT_MS = 10000;

SPIClass displaySPI(HSPI);
Adafruit_ILI9341 tft(&displaySPI, TFT_DC, TFT_CS, TFT_RST);
WebServer server(80);
Preferences prefs;

String wifiSSID;
String wifiPASS;
bool apMode = false;

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

  drawSpeedLimit(34, 90, hud.speedLimit);

  if (hud.alert == ALERT_CAMERA) {
    drawCameraGlyph(34, 137, C_WHITE);
    textCentered(formatDistance(hud.alertDistanceM), 0, 151, 68, 1, C_WHITE);
  }

  // Center: maneuver
  textCentered(formatDistance(hud.distanceM), 69, 5, 153, 3, C_YELLOW);
  drawArrow(hud.turn, 145, 88);

  String road = cleanText(hud.road);
  if (road.length() > 21) road = road.substring(0,21);
  textCentered(road, 72, 165, 148, road.length() > 16 ? 1 : 2, C_WHITE);

  // Right: nearest alert
  if (hud.alert != ALERT_NONE) {
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
  tft.print(hud.eta);

  String route = cleanText(hud.route);
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
  textCentered("WAZE HUD", 0, 45, 320, 4, C_BLUE);
  textCentered(apMode ? "SETUP MODE" : "WAITING FOR DATA", 0, 105, 320, 2, C_WHITE);
  tft.setTextSize(2);
  tft.setTextColor(C_YELLOW, C_BG);
  String ip = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  textCentered(ip, 0, 145, 320, 2, C_YELLOW);
  textCentered("Open IP in browser", 0, 180, 320, 1, C_GREY);
}

String pageHtml() {
  String ip = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  String html = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width">
<title>Waze HUD</title><style>
body{font-family:system-ui;background:#080b10;color:#fff;margin:0;padding:18px}main{max-width:720px;margin:auto}
.card{background:#101722;border:1px solid #1e87ff;border-radius:16px;padding:16px;margin:12px 0}
input,select,button{box-sizing:border-box;width:100%;padding:12px;margin:5px 0;border-radius:9px;border:1px solid #334;background:#0a1018;color:#fff}
button{background:#087cff;font-weight:700;border:0}.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}small{color:#8ca0b8}
</style></head><body><main><h1>ESP32 Waze HUD</h1><small>IP: %IP%</small>
<div class="card"><h3>HUD test</h3><div class="grid">
<select id="turn"><option>right</option><option>left</option><option>straight</option><option>slight_right</option><option>slight_left</option><option>uturn</option><option>roundabout</option></select>
<input id="dist" type="number" value="350" placeholder="Turn distance m">
<input id="road" value="Vo Nguyen Giap" placeholder="Road">
<input id="speed" type="number" value="62" placeholder="Speed">
<input id="limit" type="number" value="60" placeholder="Limit">
<input id="remain" type="number" step=".1" value="8.6" placeholder="Remaining km">
<input id="eta" value="10:42" placeholder="ETA">
<input id="route" value="QL1A" placeholder="Route">
<select id="alert"><option>camera</option><option>police</option><option>crash</option><option>traffic</option><option>roadworks</option><option>pothole</option><option>object</option><option>car_on_shoulder</option><option>broken_light</option><option>closure</option><option>bad_weather</option><option>blocked_lane</option><option>high_risk</option><option>animal</option><option>none</option></select>
<input id="adist" type="number" value="500" placeholder="Alert distance m"></div>
<button onclick="sendHud()">SEND HUD</button><p id="msg"></p></div>
<div class="card"><h3>Wi-Fi</h3><form method="post" action="/wifi">
<input name="ssid" placeholder="Wi-Fi SSID"><input name="pass" type="password" placeholder="Password"><button>SAVE & RESTART</button></form></div>
<script>
async function sendHud(){const v=id=>document.getElementById(id).value;
const body={turn:v('turn'),distance_m:+v('dist'),road:v('road'),speed:+v('speed'),speed_limit:+v('limit'),remaining_km:+v('remain'),eta:v('eta'),route:v('route'),alert:{type:v('alert'),distance_m:+v('adist')}};
try{let r=await fetch('/hud',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});document.getElementById('msg').textContent=await r.text()}catch(e){document.getElementById('msg').textContent=e}}
</script></main></body></html>)HTML";
  html.replace("%IP%", ip);
  return html;
}

void setupServer() {
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control","no-store");
    server.send(200, "text/html; charset=utf-8", pageHtml());
  });

  server.on("/hud", HTTP_POST, []() {
    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      server.send(400, "application/json", "{"ok":false,"error":"json"}");
      return;
    }

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
      if (!alert["distance_m"].isNull()) hud.alertDistanceM = constrain((int)alert["distance_m"],0,65000);
    }

    hud.updatedAt = millis();
    hud.valid = true;
    drawHud();
    server.send(200, "application/json", "{"ok":true}");
  });

  server.on("/state", HTTP_GET, []() {
    JsonDocument doc;
    doc["speed"] = hud.speed;
    doc["speed_limit"] = hud.speedLimit;
    doc["distance_m"] = hud.distanceM;
    doc["road"] = hud.road;
    doc["remaining_km"] = hud.remainingKm;
    doc["eta"] = hud.eta;
    doc["route"] = hud.route;
    doc["age_ms"] = hud.valid ? millis() - hud.updatedAt : 0;
    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/wifi", HTTP_POST, []() {
    String s = server.arg("ssid"); s.trim();
    String p = server.arg("pass");
    if (!s.length()) {
      server.send(400, "text/plain", "SSID required");
      return;
    }
    prefs.begin("wazehud", false);
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    prefs.end();
    server.send(200, "text/html", "<h2>Saved. ESP32 restarting...</h2>");
    delay(800);
    ESP.restart();
  });

  server.onNotFound([](){
    if (apMode) {
      server.sendHeader("Location","http://192.168.4.1/",true);
      server.send(302,"text/plain","");
    } else {
      server.send(404,"text/plain","Not found");
    }
  });

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

  connectWiFi();
  setupServer();
  drawWaiting();

  Serial.print("Waze HUD IP: ");
  Serial.println(apMode ? WiFi.softAPIP() : WiFi.localIP());
}

void loop() {
  server.handleClient();

  static uint32_t lastRefresh = 0;
  if (millis() - lastRefresh > 1000) {
    lastRefresh = millis();
    if (hud.valid) drawHud();
  }
  delay(4);
}
