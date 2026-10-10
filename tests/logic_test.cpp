#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <string>
#include <map>
#include <vector>
#include <sys/time.h>
using std::string;
#define constrain(a,lo,hi) ((a)<(lo)?(lo):((a)>(hi)?(hi):(a)))
#define min(a,b) ((a)<(b)?(a):(b))
#define BUZZER_PIN 32
static const int BUZ_CH=7, BTN_PIN=0;
static const uint32_t WATCHDOG_MS=45000, HUD_STALE_MS=3000;
// --- time control
static uint32_t FAKE_MS=0; uint32_t millis(){return FAKE_MS;}
static time_t FAKE_T=0; static time_t fake_time(time_t*){return FAKE_T;}
#define time(x) fake_time(x)
static std::vector<time_t> SET_TIMES;
int fake_settimeofday(const struct timeval*tv,const struct timezone*){SET_TIMES.push_back(tv->tv_sec);FAKE_T=tv->tv_sec;return 0;}
#define settimeofday(a,b) fake_settimeofday(a,b)
// --- minimal arduino/json/lvgl stubs
struct StrStub{};
struct Var{bool has=false;double v=0;bool isNull()const{return !has;}template<class T>T as()const{return (T)v;}};
template<class T> T operator|(const Var&a,T d){return a.has?(T)a.v:d;}
struct JsonDocument{std::map<string,Var> m;Var& operator[](const char*k){return m[k];}};
struct{int st=0;int status(){return st;}}WiFi;
#define WL_CONNECTED 3
enum LvUiMode{LVUI_NONE,LVUI_HUD,LVUI_WAITING};static LvUiMode lvUiMode=LVUI_HUD;
struct obj{};static obj SCR;obj*lv_scr_act(){return &SCR;}static int invalidations=0;void lv_obj_invalidate(obj*){invalidations++;}
enum AlertType{ALERT_NONE,ALERT_POLICE,ALERT_CAMERA,ALERT_CRASH};
struct{uint8_t brightness=100;bool autoDim=true;uint8_t dimLevel=40,dimFrom=21,dimTo=5;bool buzzer=true;bool standbyClock=true;}settings;
struct{bool valid=true;uint32_t updatedAt=0;AlertType alert=ALERT_NONE;uint8_t alertCode=0;int alertDistanceM=-1;bool overSpeed=false;}hud;
bool alertEnabled(AlertType){return true;}
static uint8_t effBright=100;static int btnBrightPct=0;static bool forceClock=false;static float wxTempC=NAN;static int wxCode=-1;static bool wxDirty=false;static uint32_t wxPhoneAt=0;
static int sbLastMin=0,sbLastDay=0,sbLastSec=0;static bool hudRenderValid=true;static bool otaInProgress=false,otaRequested=false;
static int drawStandbyCalls=0,drawHudCalls=0,drawWaitingCalls=0,resets=0;
void drawStandby(){drawStandbyCalls++;}void drawHud(){drawHudCalls++;}void drawWaiting(){drawWaitingCalls++;}
#define LOW 0
static int digitalLevel=1;int digitalRead(int){return digitalLevel;}
struct{void println(const char*){}void print(const char*){}}Serial;
static std::vector<int> TONES;static void ledcWriteTone(int,int f){TONES.push_back(f);}
static void ledcSetup(int,int,int){}static void ledcAttachPin(int,int){}
struct PrefsStub{void begin(const char*,bool){}void clear(){resets++;}void end(){}}prefs;
struct{void restart(){resets+=100;}}ESP;void delay(int){}
#define vTaskDelay(x)
#define pdMS_TO_TICKS(x) x
#include "build/fw_logic_part.cpp"
static int fails=0;
#define CHECK(c,msg) do{if(!(c)){printf("FAIL: %s\n",msg);fails++;}else printf("ok:   %s\n",msg);}while(0)
static void setLocal(int Y,int M,int D,int h,int m){struct tm t={};t.tm_year=Y-1900;t.tm_mon=M-1;t.tm_mday=D;t.tm_hour=h;t.tm_min=m;t.tm_isdst=-1;FAKE_T=mktime(&t);}
int main(){
  setenv("TZ","ICT-7",1);tzset();
  // ---- auto-dim
  settings.dimFrom=21;settings.dimTo=5;settings.dimLevel=40;settings.brightness=100;
  setLocal(2026,10,10,22,0);updateEffectiveBrightness(true);CHECK(effBright==40,"22:00 dims to 40 (wrap window)");
  setLocal(2026,10,10,12,0);updateEffectiveBrightness(true);CHECK(effBright==100,"12:00 full brightness");
  setLocal(2026,10,10,4,59);updateEffectiveBrightness(true);CHECK(effBright==40,"04:59 still dimmed");
  setLocal(2026,10,10,5,0);updateEffectiveBrightness(true);CHECK(effBright==100,"05:00 back to normal");
  settings.dimFrom=8;settings.dimTo=17;setLocal(2026,10,10,9,0);updateEffectiveBrightness(true);CHECK(effBright==40,"non-wrapping window 08-17");
  settings.dimFrom=21;settings.dimTo=5;settings.autoDim=false;setLocal(2026,10,10,23,0);updateEffectiveBrightness(true);CHECK(effBright==100,"auto-dim off");
  settings.autoDim=true;settings.brightness=30;setLocal(2026,10,10,23,0);updateEffectiveBrightness(true);CHECK(effBright==30,"never brighter than user setting");
  settings.brightness=100;FAKE_T=0;updateEffectiveBrightness(true);CHECK(effBright==100,"no valid clock -> no dimming");
  btnBrightPct=70;setLocal(2026,10,10,12,0);updateEffectiveBrightness(true);CHECK(effBright==70,"button preset 70");
  btnBrightPct=0;
  // ---- phone time
  WiFi.st=0;FAKE_T=0;{JsonDocument d;d["ts"].has=true;d["ts"].v=1791000000;handlePhoneTime(d);}CHECK(SET_TIMES.size()==1&&SET_TIMES[0]==1791000000,"phone time applied without Wi-Fi");
  WiFi.st=WL_CONNECTED;{JsonDocument d;d["ts"].has=true;d["ts"].v=1791000999;handlePhoneTime(d);}CHECK(SET_TIMES.size()==1,"phone time ignored when Wi-Fi + valid clock");
  WiFi.st=0;{JsonDocument d;d["ts"].has=true;d["ts"].v=1000;handlePhoneTime(d);}CHECK(SET_TIMES.size()==2?false:true,"bogus timestamp rejected");
  {JsonDocument d;d["temp"].has=true;d["temp"].v=27.5;d["wx"].has=true;d["wx"].v=61;handlePhoneTime(d);}CHECK(fabs(wxTempC-27.5)<1e-6&&wxCode==61&&wxDirty&&wxPhoneAt,"phone weather stored");
  // ---- button
  digitalLevel=1;handleButton();
  digitalLevel=0;FAKE_MS=1000;handleButton();digitalLevel=1;FAKE_MS=1200;handleButton();CHECK(forceClock&&drawStandbyCalls==1,"short press forces clock screen");
  digitalLevel=0;FAKE_MS=2000;handleButton();digitalLevel=1;FAKE_MS=2300;handleButton();CHECK(!forceClock&&drawHudCalls>=1,"second short press returns to HUD");
  setLocal(2026,10,10,12,0);
  digitalLevel=0;FAKE_MS=3000;handleButton();digitalLevel=1;FAKE_MS=5000;handleButton();CHECK(btnBrightPct==70&&effBright==70,"2 s hold -> brightness 70");
  digitalLevel=0;FAKE_MS=6000;handleButton();digitalLevel=1;FAKE_MS=8000;handleButton();CHECK(btnBrightPct==40,"next hold -> 40");
  digitalLevel=0;FAKE_MS=9000;handleButton();digitalLevel=1;FAKE_MS=11000;handleButton();CHECK(btnBrightPct==0&&effBright==100,"next hold -> back to full");
  digitalLevel=0;FAKE_MS=20000;handleButton();FAKE_MS=29000;handleButton();CHECK(resets==0,"9 s hold does not reset");FAKE_MS=30100;handleButton();CHECK(resets>=100,"10 s hold factory-resets");
  digitalLevel=1;
  // ---- buzzer
  TONES.clear();buzPlay(BUZ_ALERT_HIGH,3);for(FAKE_MS=100000;FAKE_MS<100400;FAKE_MS+=10)buzTick();
  std::vector<int> want={2400,0,2400,0};CHECK(TONES==want,"double beep sequence 2400/0/2400/off");
  settings.buzzer=false;TONES.clear();buzPlay(BUZ_ALERT_MID,1);for(FAKE_MS=200000;FAKE_MS<200400;FAKE_MS+=10)buzTick();CHECK(TONES.empty(),"silent when buzzer disabled");
  settings.buzzer=true;TONES.clear();FAKE_MS=300000;hud.updatedAt=FAKE_MS;hud.alert=ALERT_CAMERA;hud.alertCode=2;hud.alertDistanceM=500;
  updateBuzzerTriggers();for(int i=0;i<60;i++){FAKE_MS+=10;hud.updatedAt=FAKE_MS;buzTick();}size_t n1=TONES.size();
  updateBuzzerTriggers();for(int i=0;i<60;i++){FAKE_MS+=10;hud.updatedAt=FAKE_MS;buzTick();}CHECK(n1>0&&TONES.size()==n1,"new alert beeps once, not repeatedly");
  hud.alertDistanceM=3000;hud.alertCode=5;updateBuzzerTriggers();size_t n2=TONES.size();for(int i=0;i<80;i++){FAKE_MS+=10;hud.updatedAt=FAKE_MS;buzTick();}CHECK(TONES.size()==n2,"far-away alert stays silent");
  hud.alert=ALERT_NONE;hud.alertCode=0;hud.overSpeed=true;updateBuzzerTriggers();for(int i=0;i<80;i++){FAKE_MS+=10;hud.updatedAt=FAKE_MS;buzTick();}CHECK(TONES.size()>n2,"overspeed start beeps");
  printf("\n%s (%d failed)\n",fails?"SOME TESTS FAILED":"ALL LOGIC TESTS PASSED",fails);return fails;}
