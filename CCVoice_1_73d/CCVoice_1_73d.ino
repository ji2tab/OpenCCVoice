/************************************************************
 * OpenCCVoice Guidance Controller  (Unified / Safe)
 * Version : 1.73d
 * Target  : Arduino Nano (ATmega328P, 5V)
 *
 * 【概要】
 * - 既存配線（DFPlayer: D10=Arduino RX / D11=Arduino TX）を維持
 * - 入力は INPUT_PULLUP（浮き対策）
 * - フェイルセーフ（DFPlayer応答なし）: d#### (ms), d0=無効
 * - TM BUSY極性: g0=LOW=busy / g1=HIGH=busy
 * - 周期ID: p##（分、0で停止）
 * - ★改良: 周期IDは「BUSYがOFFになってから k#### ms（静寂時間）」を満たす時のみ送出
 *           BUSY中/静寂不足/抑止中は延期（periodicDueを保持）
 * - AUTO 判定（m2）で D6/A0 を観測→優位側へ固定
 * - 抑止（長話/バースト/送信後）、PTTガード、BUSYデバウンスを統合
 * - EEPROM バージョン管理（config.ver）で自動移行/初期化
 *
 * 【操作（115200 8N1, 改行なし/LF）】
 *  m0/m1/m2, n####, b####, i####, s0/1, t0/1, r####, p##, k####,
 *  L###, G###, a####, w##, d####, g0/g1, q, H, x, r, F, 0..3, h
 ************************************************************/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

/* ============================== EEPROM =============================== */
/* 構造の方針：
 * - 旧構造からの互換性を保つため、verは末尾に配置
 * - v1.73c(ver=3) → v1.73d(ver=4) で periodQuietMs を追加
 */
struct MyConfig {
  uint32_t magic;
  uint8_t  busySrc;          // [m] 0:D6, 1:A0, 2:AUTO
  uint8_t  suppressOn;       // [s] 0:OFF, 1:ON
  uint8_t  txAfSupOn;        // [t] 0:OFF, 1:ON
  uint32_t busyMin;          // [n]
  uint32_t busyMax;          // [b]
  uint32_t idleMin;          // [i]
  uint32_t periodMin;        // [p] 分
  uint32_t txSupMs;          // [r]
  int      a0Low;            // [L]
  int      a0High;           // [G]
  uint32_t a0Hold;           // [a]
  uint32_t autoWinMin;       // [w] 分
  uint32_t dfpTimeoutMs;     // [d] ms, 0=無効
  uint8_t  tmBusyActiveHigh; // [g] 0=LOW=busy, 1=HIGH=busy
  uint32_t periodQuietMs;    // [k] 周期IDの静寂条件(ms) ★v1.73d追加
  uint8_t  ver;              // ★EEPROMレイアウト版 (v1.73d=4)
} config;

const uint32_t CONFIG_MAGIC   = 0xDEADBEEF;
const uint8_t  CONFIG_VERSION = 4;  // v1.73d のレイアウト番号

/* =========================== Runtime Params =========================== */
enum BusySrc { BUSY_SRC_D6, BUSY_SRC_A0, BUSY_SRC_AUTO };
volatile BusySrc BUSY_INPUT_SOURCE;

bool SUPPRESSORS_ENABLED;
bool TX_AFTER_SUPPRESS_ENABLED;

unsigned long BUSY_MIN_MS;
unsigned long BUSY_MAX_MS;
unsigned long IDLE_MIN_MS;
unsigned long PERIOD_MS;
unsigned long TX_SUP_MS;

int A0_LOW_TH;
int A0_HIGH_TH;
unsigned long A0_HOLD;
unsigned long AUTO_WINDOW;
unsigned long LONG_TALK_MS;

unsigned long DFP_TIMEOUT_MS;     // d#### (ms), 0=無効
bool TMBUSY_ACTIVE_HIGH;          // g0/g1
unsigned long PERIOD_QUIET_MS;    // k#### (ms)

/* ============================== Pins ================================== */
const bool DFP_BUSYOUT_INVERT = true;

const uint8_t PIN_TEST_SW  = 3;   // D3
const uint8_t PIN_BUSY_LED = 4;   // D4
const uint8_t PIN_PTT      = 5;   // D5
const uint8_t PIN_TM_BUSY  = 6;   // D6
const uint8_t PIN_DFP_BSY  = 7;   // D7
const uint8_t PIN_DFP_OUT  = 2;   // D2
const uint8_t PIN_SUP_LED  = 13;  // D13
const uint8_t PIN_A0_LED   = 12;  // D12
const uint8_t A0_PIN       = A0;

const uint8_t ARD_RX_FROM_DFP = 10; // DFPlayer TX → D10(RX)
const uint8_t ARD_TX_TO_DFP   = 11; // DFPlayer RX ← D11(TX)
SoftwareSerial dfpSerial(ARD_RX_FROM_DFP, ARD_TX_TO_DFP);

/* ========================= Constants / Guards ========================= */
const unsigned long REFRAC_MS    = 3000;   // 不応期
const unsigned long DEBOUNCE_MS  = 5;      // D6デバウンス
const unsigned long PTT_PRE_MS   = 1000;   // 再生前PTT先行
const unsigned long PTT_POST_MS  = 1000;   // 再生後PTT保持
const unsigned long LONG_SUP_MS  = 10000;  // 長話抑止
const unsigned long BURST_WIN_MS = 10000;  // バースト窓
const unsigned int  BURST_TH     = 2;      // バースト閾値
const unsigned long BURST_SUP_MS = 10000;  // バースト抑止

/* ============================ State / Vars ============================ */
unsigned long windowStartTS = 0, autoSwitchBlinkUntil = 0;
bool autoLocked = false;

enum LogLvl { LOG_NONE=0, LOG_ERR=1, LOG_INF=2, LOG_DBG=3 };
volatile LogLvl LOG_LEVEL = LOG_INF;

enum State { IDLE, PTT_ON_WAIT, PLAYING, PTT_OFF_WAIT };
State state = IDLE;

unsigned long tmBusyStart=0, tmDebounceTS=0, a0LastSignalTS=0;
bool tmBusyPrev=false, tmBusyFiltered=false, a0Detect=false, a0Busy=false;

unsigned long lastTriggerAt=0, stateTimer=0, pttMinOn=0, nextPeriodicAt=0;
bool dfpStarted=false, pttOutState=false;
bool periodicDue=false, clickWaiting=false, stopped=false;
uint16_t requestedTrack=0, nextPeriodicTrack=2;

unsigned long busySupUntil=0, longSupUntil=0, burstWinStart=0, burstSupUntil=0;
unsigned long busyHighSince=0, playingEnterAt=0;

uint8_t  clickCount=0, lastSwState=HIGH;
unsigned long firstClickTime=0;
unsigned int  burstCount = 0;
uint16_t d6_edge_count = 0, a0_event_count = 0;

unsigned long lastBusyOffAt = 0;  // BUSYがOFFになった時刻（静寂起点）

/* ============================== Utils ================================= */
inline bool readTmRaw() { return (digitalRead(PIN_TM_BUSY) == HIGH); }
inline bool readTmD6()  { bool rawHigh = readTmRaw(); return TMBUSY_ACTIVE_HIGH ? rawHigh : !rawHigh; }
inline bool readBusy()  {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) return a0Busy;
  return (tmBusyFiltered || a0Busy);
}

void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E,0xFF,0x06,cmd,0x00,(uint8_t)(param>>8),(uint8_t)(param & 0xFF),0x00,0x00,0xEF};
  uint16_t s = 0; for (int i = 1; i < 7; i++) s += f[i];
  s = 0xFFFF - s + 1; f[7] = (uint8_t)(s >> 8); f[8] = (uint8_t)s;
  dfpSerial.write(f, 10);
}

void setPtt(bool on) {
  if (pttOutState == on) return;
  pttOutState = on;
  digitalWrite(PIN_PTT, on ? HIGH : LOW);
  if (LOG_LEVEL >= LOG_INF) { Serial.print(F("[PTT] ")); Serial.println(on ? F("ON") : F("OFF")); }
}

void startPtt(uint16_t trk) {
  if (state != IDLE) return;
  requestedTrack = trk; dfpStarted = false;
  unsigned long now = millis();
  setPtt(true); pttMinOn = now + PTT_PRE_MS + 100; stateTimer = now;
  playingEnterAt = 0; state = PTT_ON_WAIT;
}

static inline bool isSuppressedNow(unsigned long now) {
  if (!SUPPRESSORS_ENABLED) return false;
  if ((long)(now - longSupUntil)  < 0) return true;
  if ((long)(now - burstSupUntil) < 0) return true;
  if ((long)(now - busySupUntil)  < 0) return true;
  return false;
}

/* ============================ Defaults/EEPROM ========================== */
void applyDefaults() {
  BUSY_INPUT_SOURCE = BUSY_SRC_D6;
  SUPPRESSORS_ENABLED = true;
  TX_AFTER_SUPPRESS_ENABLED = true;

  BUSY_MIN_MS = 500; BUSY_MAX_MS = 3900; IDLE_MIN_MS = 200;
  PERIOD_MS   = 30UL * 60UL * 1000UL;  // 30分
  TX_SUP_MS   = 3000;

  A0_LOW_TH = 300; A0_HIGH_TH = 700; A0_HOLD = 800;
  AUTO_WINDOW = 30UL * 60UL * 1000UL; LONG_TALK_MS = BUSY_MAX_MS;

  DFP_TIMEOUT_MS = 20000;          // 20秒
  TMBUSY_ACTIVE_HIGH = true;       // g1
  PERIOD_QUIET_MS    = 2000;       // ★静寂2秒（新規）

  autoLocked = false;

  // configへも反映
  config.magic            = CONFIG_MAGIC;
  config.busySrc          = (uint8_t)BUSY_INPUT_SOURCE;
  config.suppressOn       = SUPPRESSORS_ENABLED ? 1 : 0;
  config.txAfSupOn        = TX_AFTER_SUPPRESS_ENABLED ? 1 : 0;
  config.busyMin          = BUSY_MIN_MS;
  config.busyMax          = BUSY_MAX_MS;
  config.idleMin          = IDLE_MIN_MS;
  config.periodMin        = PERIOD_MS/60000UL;
  config.txSupMs          = TX_SUP_MS;
  config.a0Low            = A0_LOW_TH;
  config.a0High           = A0_HIGH_TH;
  config.a0Hold           = A0_HOLD;
  config.autoWinMin       = AUTO_WINDOW/60000UL;
  config.dfpTimeoutMs     = DFP_TIMEOUT_MS;
  config.tmBusyActiveHigh = 1;
  config.periodQuietMs    = PERIOD_QUIET_MS;
  config.ver              = CONFIG_VERSION;
}

void saveSettings() {
  config.magic            = CONFIG_MAGIC;
  config.busySrc          = (uint8_t)BUSY_INPUT_SOURCE;
  config.suppressOn       = SUPPRESSORS_ENABLED ? 1 : 0;
  config.txAfSupOn        = TX_AFTER_SUPPRESS_ENABLED ? 1 : 0;
  config.busyMin          = BUSY_MIN_MS;
  config.busyMax          = BUSY_MAX_MS;
  config.idleMin          = IDLE_MIN_MS;
  config.periodMin        = PERIOD_MS/60000UL;
  config.txSupMs          = TX_SUP_MS;
  config.a0Low            = A0_LOW_TH;
  config.a0High           = A0_HIGH_TH;
  config.a0Hold           = A0_HOLD;
  config.autoWinMin       = AUTO_WINDOW/60000UL;
  config.dfpTimeoutMs     = DFP_TIMEOUT_MS;
  config.tmBusyActiveHigh = TMBUSY_ACTIVE_HIGH ? 1 : 0;
  config.periodQuietMs    = PERIOD_QUIET_MS;
  config.ver              = CONFIG_VERSION;
  EEPROM.put(0, config);
  if (LOG_LEVEL >= LOG_INF) Serial.println(F("[EEPROM] Settings Saved."));
}

void migrateOrInit() {
  EEPROM.get(0, config);

  if (config.magic != CONFIG_MAGIC) {
    Serial.println(F("[EEPROM] No/Other Data. Init defaults."));
    applyDefaults(); EEPROM.put(0, config); return;
  }

  // ver不一致の場合の補完/移行（ver未定義(0xFF)も含む）
  bool needSave = false;
  if (config.ver != CONFIG_VERSION) {
    Serial.print(F("[EEPROM] Version mismatch: stored="));
    Serial.print(config.ver); Serial.print(F(" expected="));
    Serial.println(CONFIG_VERSION);

    // 既存値を活かしつつ、新項目/不正値を補完
    if (!(config.tmBusyActiveHigh == 0 || config.tmBusyActiveHigh == 1)) {
      config.tmBusyActiveHigh = 1; needSave = true;
    }
    if (config.dfpTimeoutMs > 600000UL) { // 10分超は異常
      config.dfpTimeoutMs = 20000UL; needSave = true;
    }
    if (config.periodQuietMs == 0 || config.periodQuietMs > 600000UL) {
      // v1.73d で新規：静寂条件は既定2000msへ
      config.periodQuietMs = 2000UL; needSave = true;
    }

    config.ver = CONFIG_VERSION; needSave = true;
    if (needSave) EEPROM.put(0, config);
    Serial.println(F("[EEPROM] Migration/completion done -> updated version."));
  }

  // ランタイムへ反映
  BUSY_INPUT_SOURCE         = (BusySrc)config.busySrc;
  SUPPRESSORS_ENABLED       = (config.suppressOn == 1);
  TX_AFTER_SUPPRESS_ENABLED = (config.txAfSupOn == 1);
  BUSY_MIN_MS               = config.busyMin;
  BUSY_MAX_MS               = config.busyMax;
  IDLE_MIN_MS               = config.idleMin;
  PERIOD_MS                 = (unsigned long)config.periodMin * 60000UL;
  TX_SUP_MS                 = config.txSupMs;
  A0_LOW_TH                 = config.a0Low;
  A0_HIGH_TH                = config.a0High;
  A0_HOLD                   = config.a0Hold;
  AUTO_WINDOW               = (unsigned long)config.autoWinMin * 60000UL;
  LONG_TALK_MS              = BUSY_MAX_MS;
  DFP_TIMEOUT_MS            = config.dfpTimeoutMs;
  TMBUSY_ACTIVE_HIGH        = (config.tmBusyActiveHigh == 1);
  PERIOD_QUIET_MS           = config.periodQuietMs;
  autoLocked                = (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO);
  Serial.println(F("[EEPROM] Settings Loaded."));
}

/* ============================== Prints ================================ */
void printSummary() {
  Serial.print(F("[CFG] EEPROM_VER=")); Serial.print(config.ver);
  Serial.print(F(" SRC="));
  if (BUSY_INPUT_SOURCE == BUSY_SRC_AUTO) Serial.print(F("AUTO"));
  else Serial.print(BUSY_INPUT_SOURCE==BUSY_SRC_D6?F("D6"):F("A0"));
  if (autoLocked) Serial.print(F("(LOCK)"));

  Serial.print(F(" MIN="));  Serial.print(BUSY_MIN_MS);
  Serial.print(F(" MAX="));  Serial.print(BUSY_MAX_MS);
  Serial.print(F(" PER(min)=")); Serial.print(PERIOD_MS/60000UL);
  Serial.print(F(" TXSUP="));   Serial.print(TX_SUP_MS);
  Serial.print(F(" A0[L/H]=")); Serial.print(A0_LOW_TH); Serial.print('/'); Serial.print(A0_HIGH_TH);
  Serial.print(F(" HOLD="));    Serial.print(A0_HOLD);
  Serial.print(F(" AUTO(min)=")); Serial.print(AUTO_WINDOW/60000UL);
  Serial.print(F(" DFP_TIMEOUT(ms)=")); Serial.print(DFP_TIMEOUT_MS);
  Serial.print(F(" TM_BUSY_POL=")); Serial.print(TMBUSY_ACTIVE_HIGH ? F("HIGH=busy") : F("LOW=busy"));
  Serial.print(F(" QUIET(ms)=")); Serial.println(PERIOD_QUIET_MS);
}

void printHelp() {
  Serial.println(F("---- HELP ----"));
  Serial.println(F("m0=D6, m1=A0, m2=AUTO"));
  Serial.println(F("n###=busyMin(ms), b####=busyMax(ms), i###=idleMin(ms)"));
  Serial.println(F("s0/1=suppress OFF/ON, t0/1=txAfterSuppress OFF/ON, r####=ms"));
  Serial.println(F("p##=period(min, 0=stop), k####=period quiet(ms)"));
  Serial.println(F("L###/G###/a####=A0 lo/hi/hold(ms), w##=AUTO(min)"));
  Serial.println(F("d####=DFP timeout(ms), 0=disable"));
  Serial.println(F("g0/g1=TM BUSY polarity (0:LOW=busy, 1:HIGH=busy)"));
  Serial.println(F("q=summary, x=STOP, r=RESUME (STOP only), H=preset(s0/t0/b3900), F=factory"));
  Serial.println(F("0..3=log level"));
}

/* ============================ Command Parser ========================== */
void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();
    unsigned long timeout = millis() + 150;
    while (!Serial.available() && millis() < timeout) { }
    long val = Serial.parseInt();
    bool chg = true;

    switch (c) {
      case 'm':
        if      (val==0) { BUSY_INPUT_SOURCE=BUSY_SRC_D6;  autoLocked=true; }
        else if (val==1) { BUSY_INPUT_SOURCE=BUSY_SRC_A0;  autoLocked=true; }
        else if (val==2) { BUSY_INPUT_SOURCE=BUSY_SRC_AUTO;autoLocked=false; windowStartTS=millis(); d6_edge_count=0; a0_event_count=0; }
        else chg=false; break;

      case 'b': if (val>=500) { BUSY_MAX_MS=(unsigned long)val; LONG_TALK_MS=BUSY_MAX_MS; } else chg=false; break;
      case 'n': if (val>=100) BUSY_MIN_MS=(unsigned long)val; else chg=false; break;
      case 'i': if (val>=0)   IDLE_MIN_MS=(unsigned long)val; else chg=false; break;

      case 's': if (val==0||val==1) SUPPRESSORS_ENABLED=(val==1); else chg=false; break;
      case 't': if (val==0||val==1) TX_AFTER_SUPPRESS_ENABLED=(val==1); else chg=false; break;

      case 'r': if (val>=0)   TX_SUP_MS=(unsigned long)val; else chg=false; break;

      case 'p':
        if (val>=0) {
          PERIOD_MS = (unsigned long)val * 60UL * 1000UL;
          unsigned long now = millis();
          if (PERIOD_MS > 0) nextPeriodicAt = now + PERIOD_MS;
          else periodicDue = false;
        } else chg=false;
        break;

      case 'k': // 周期IDの静寂時間(ms)
        if (val>=0 && val<=600000) PERIOD_QUIET_MS=(unsigned long)val;
        else chg=false;
        break;

      case 'L': A0_LOW_TH  = (int)val; break;
      case 'G': A0_HIGH_TH = (int)val; break;
      case 'a': if (val>=0) A0_HOLD=(unsigned long)val; else chg=false; break;

      case 'w': if (val>=1) { AUTO_WINDOW=(unsigned long)val*60UL*1000UL; windowStartTS=millis(); d6_edge_count=0; a0_event_count=0; } else chg=false; break;

      case 'd': if (val>=0 && val<=600000) DFP_TIMEOUT_MS=(unsigned long)val; else chg=false; break;

      case 'g':
        if (val==0) TMBUSY_ACTIVE_HIGH=false;
        else if (val==1) TMBUSY_ACTIVE_HIGH=true;
        else chg=false;
        break;

      case 'q': printSummary(); chg=false; break;

      case 'H':
        SUPPRESSORS_ENABLED=false; TX_AFTER_SUPPRESS_ENABLED=false;
        BUSY_MAX_MS=3900; LONG_TALK_MS=3900; break;

      case 'x': stopped = true; chg=false; Serial.println(F("[STOP]")); break;

      case 'F': applyDefaults(); saveSettings(); Serial.println(F("[RESET] Factory defaults restored.")); chg=false; break;

      case 'h': printHelp(); chg=false; break;

      case '0': case '1': case '2': case '3': LOG_LEVEL=(LogLvl)(c-'0'); chg=false; break;

      default: chg=false; break;
    }

    if (chg) { saveSettings(); printSummary(); }
  }
}

/* ============================== AUTO Fix ============================== */
void maybeAuto(unsigned long now) {
  if (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO || autoLocked) return;
  if ((long)(now - windowStartTS) >= (long)AUTO_WINDOW) {
    BusySrc n;
    uint16_t d6 = d6_edge_count, a0 = a0_event_count;
    if      (d6 < 10 && a0 >= 20) n = BUSY_SRC_A0;
    else if (a0 < 20 && d6 >= 10) n = BUSY_SRC_D6;
    else n = (d6 >= a0) ? BUSY_SRC_D6 : BUSY_SRC_A0;

    BUSY_INPUT_SOURCE = n; autoLocked = true;
    autoSwitchBlinkUntil = now + 3000; saveSettings();

    Serial.print(F("[AUTO-FIXED] Lock to ")); Serial.println(n==BUSY_SRC_D6?F("D6"):F("A0"));
  }
}

/* ============================ setup / loop ============================ */
void setup() {
  Serial.begin(115200);
  dfpSerial.begin(9600);

  migrateOrInit();

  pinMode(PIN_TEST_SW, INPUT_PULLUP);
  pinMode(PIN_BUSY_LED, OUTPUT);
  pinMode(PIN_PTT, OUTPUT);
  pinMode(PIN_TM_BUSY, INPUT_PULLUP);
  pinMode(PIN_DFP_BSY, INPUT_PULLUP);
  pinMode(PIN_DFP_OUT, OUTPUT);
  pinMode(PIN_SUP_LED, OUTPUT);
  pinMode(PIN_A0_LED,  OUTPUT);

  delay(500);
  dfpSend(0x06, 20); // volume

  unsigned long now = millis();
  nextPeriodicAt = (PERIOD_MS>0) ? now + PERIOD_MS : 0;
  windowStartTS  = now;
  lastBusyOffAt  = now; // 起動直後は静寂開始とみなす

  Serial.println(F("[START] OpenCCVoice v1.73d Unified/Safe (+Periodic Quiet Guard)"));
  printSummary(); printHelp();
}

void loop() {
  unsigned long now = millis();
  handleSerialCmd();

  if (stopped) {
    setPtt(false);
    digitalWrite(PIN_BUSY_LED, LOW);
    digitalWrite(PIN_A0_LED,   LOW);
    digitalWrite(PIN_SUP_LED,  LOW);
    if (Serial.available()) {
      char c = Serial.read(); if (c == 'r') { stopped = false; Serial.println(F("[RESUME]")); }
    }
    delay(5); return;
  }

  bool pAct = (state != IDLE) || ((long)now < (long)pttMinOn);

  // D7→D2ミラー
  bool dLow = (digitalRead(PIN_DFP_BSY) == LOW);
  digitalWrite(PIN_DFP_OUT, DFP_BUSYOUT_INVERT ? (dLow ? HIGH : LOW)
                                               : (dLow ? LOW  : HIGH));

  // D6 デバウンス
  bool rTm = readTmD6();
  if (rTm != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) {
    tmBusyFiltered = rTm; tmDebounceTS = now;
    if (tmBusyFiltered) d6_edge_count++;
  }

  // A0判定（抑止中は無効）
  if (!pAct && (long)(now - busySupUntil) >= 0) {
    int v = analogRead(A0_PIN);
    if (!a0Detect && v < A0_LOW_TH) a0Detect = true;
    else if (a0Detect && v > A0_HIGH_TH) { a0Detect = false; a0_event_count++; }
    if (a0Detect) a0LastSignalTS = now;
    a0Busy = a0Detect || (a0LastSignalTS && (now - a0LastSignalTS < A0_HOLD));
  } else { a0Detect = false; a0Busy = false; }

  bool rB = readBusy();

  // BUSY→OFF の瞬間で静寂起点更新
  if (!rB && tmBusyPrev) lastBusyOffAt = now;
  tmBusyPrev = rB;

  // LED（未使用側は点滅）
  bool bl = ((now/500)%2==0);
  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) {
    digitalWrite(PIN_BUSY_LED, rB ? HIGH : LOW);
    digitalWrite(PIN_A0_LED,   a0Busy ? HIGH : (bl ? HIGH : LOW));
  } else if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) {
    digitalWrite(PIN_A0_LED,   rB ? HIGH : LOW);
    digitalWrite(PIN_BUSY_LED, tmBusyFiltered ? HIGH : (bl ? HIGH : LOW));
  } else {
    digitalWrite(PIN_BUSY_LED, tmBusyFiltered ? HIGH : (bl ? HIGH : LOW));
    digitalWrite(PIN_A0_LED,   a0Busy         ? HIGH : (bl ? HIGH : LOW));
  }

  // 短発（001）
  if (!pAct && !isSuppressedNow(now)) {
    if (rB) {
      if (tmBusyStart == 0) tmBusyStart = now;
    } else {
      if (tmBusyStart != 0) {
        unsigned long dur = now - tmBusyStart; tmBusyStart = 0;

        if (SUPPRESSORS_ENABLED) {
          if (dur >= BUSY_MAX_MS) longSupUntil = now + LONG_SUP_MS;
          if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
            if (burstWinStart==0 || (now - burstWinStart >= BURST_WIN_MS)) { burstWinStart=now; burstCount=0; }
            if (++burstCount >= BURST_TH) burstSupUntil = now + BURST_SUP_MS;
          }
        }

        bool allowed = !(SUPPRESSORS_ENABLED && ((long)(now-longSupUntil)<0 || (long)(now-burstSupUntil)<0));
        if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS &&
            (now - lastTriggerAt >= REFRAC_MS) && allowed) {
          startPtt(1); lastTriggerAt = now;
        }
      }
    }
  } else { tmBusyStart = 0; }

  // 周期ID スケジューラ
  if (PERIOD_MS > 0) {
    if ((long)(now - nextPeriodicAt) >= 0) {
      periodicDue = true; nextPeriodicAt += PERIOD_MS;
      if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[Periodic] due flag set"));
    }
  } else periodicDue = false;

  // ★静寂条件＋抑止考慮：BUSY中は延期
  if (periodicDue && state == IDLE) {
    bool quietOK = (!rB) && ((long)(now - lastBusyOffAt) >= (long)PERIOD_QUIET_MS);
    bool guardOK = !isSuppressedNow(now);
    if (quietOK && guardOK) {
      if (LOG_LEVEL >= LOG_INF) Serial.println(F("[EVT] Periodic ID (quiet ok)"));
      startPtt(nextPeriodicTrack);
      nextPeriodicTrack = (nextPeriodicTrack==2 ? 3 : 2);
      periodicDue = false;
    } else {
      if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[Periodic] deferred (busy/quiet/suppress)"));
    }
  }

  // 抑止/AUTO通知 LED
  bool s = isSuppressedNow(now);
  digitalWrite(PIN_SUP_LED,
    (long)(autoSwitchBlinkUntil-now)>0 ? ((now/250)%2==0?HIGH:LOW) : (s?HIGH:LOW));

  // ステートマシン
  switch (state) {
    case PTT_ON_WAIT:
      if (now - stateTimer >= PTT_PRE_MS) {
        if (requestedTrack) { dfpSend(0x03, requestedTrack); requestedTrack=0; }
        state = PLAYING; playingEnterAt = now;
      } break;

    case PLAYING:
      if (digitalRead(PIN_DFP_BSY) == LOW) {
        dfpStarted = true; busyHighSince = 0;
      } else {
        if (dfpStarted) {
          if (busyHighSince == 0) busyHighSince = now;
          if (now - busyHighSince >= 40) { state = PTT_OFF_WAIT; stateTimer = now; }
        }
      }
      if (DFP_TIMEOUT_MS > 0 && playingEnterAt>0 && (now - playingEnterAt >= DFP_TIMEOUT_MS)) {
        if (LOG_LEVEL >= LOG_ERR) Serial.println(F("[ERR] DFP Timeout -> Force PTT OFF"));
        state = PTT_OFF_WAIT; stateTimer = now;
      }
      break;

    case PTT_OFF_WAIT:
      if (now - stateTimer >= PTT_POST_MS && now >= pttMinOn) {
        setPtt(false); state = IDLE;
        if (SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED) busySupUntil = now + TX_SUP_MS;
      } break;

    case IDLE:
    default: break;
  }

  // 最低ONガード
  if ((long)now < (long)pttMinOn) setPtt(true);

  // テストSW（1〜3クリック＝1〜3番トラック）
  bool sw = digitalRead(PIN_TEST_SW);
  if (sw == LOW && lastSwState == HIGH) {
    if (!clickWaiting) { clickWaiting=true; clickCount=1; firstClickTime=now; }
    else clickCount++;
  }
  lastSwState = sw;
  if (clickWaiting && (now - firstClickTime >= 1000)) {
    if (state == IDLE && clickCount>=1 && clickCount<=3) startPtt(clickCount);
    clickWaiting=false; clickCount=0;
  }

  // AUTO 固定
  maybeAuto(now);
}