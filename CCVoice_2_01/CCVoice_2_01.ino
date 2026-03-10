/************************************************************
 * OpenCCVoice Guidance Controller  (Unified / Safe)
 * Version : 2.01  (Mainline)
 * Target  : Arduino Nano (ATmega328P, 5V)
 *
 * 【概要】
 * - DFPlayer配線: TX/RX を D13/D12（SoftwareSerial）へ移動（Ver.5）
 * - 入力は INPUT_PULLUP（浮き対策）
 * - フェイルセーフ（DFPlayer応答なし）: d#### (ms), d0=無効
 * - TM BUSY極性: g0=LOW=busy / g1=HIGH=busy
 * - 周期ID: p##（分、0で停止）
 * - 周期IDは「BUSYがOFFになってから k#### ms（静寂時間）」を満たす時のみ送出
 *   BUSY中/静寂不足/抑止中は延期（periodicDueを保持）
 * - AUTO 判定（m2）で D11/A0 を観測→優位側へ固定
 * - 抑止（長話/バースト/送信後）、PTTガード、BUSYデバウンスを統合
 * - EEPROM バージョン管理（config.ver）で自動移行/初期化
 *
 * 【v2.01: 仕様整理】
 * - STOP復帰を R（大文字）に統一（r#### 設定との衝突回避）
 * - Ver.5 ピンマップへ変更（D10/D11/D12/D13/D6/D7/D3）
 *
 * 【操作（115200 8N1, 改行なし/LF）】
 *  m0/m1/m2, n####, b####, i####, s0/1, t0/1, r####, p##, k####,
 *  L###, G###, a####, w##, d####, g0/g1, q, H, x, R, F, 0..3, h
 *
 * 【Ver.5 ピンマップ（本流）】
 *  DF Player  : D3   （DFP BUSYミラー出力：再生中=HIGH 反転）
 *  ID・Test   : D2   （テストSW）
 *  ModBusy    : D4   （BUSY LED）
 *  PTT        : D5
 *  抑止       : D6   （抑止LED）
 *  A0検知     : D7   （A0検知LED）
 *  DFBusy     : D10  （DFPlayer BUSY入力：LOW=再生中）
 *  Digital入力: D11  （TM BUSY入力）
 *  TX         : D12  （Arduino TX → DFPlayer RX）
 *  RX         : D13  （Arduino RX ← DFPlayer TX）
 ************************************************************/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

/* ============================== EEPROM =============================== */
/* 互換方針：
 * - verは末尾に配置
 * - v1.73d/ver=4 で periodQuietMs を追加
 * - v2.01 は挙動・ピン変更のみ（EEPROMレイアウト変更なし）→ ver=4 のまま
 */
struct MyConfig {
  uint32_t magic;
  uint8_t  busySrc;          // [m] 0:DIGITAL(D11), 1:A0, 2:AUTO
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
  uint32_t periodQuietMs;    // [k] 周期IDの静寂条件(ms)
  uint8_t  ver;              // EEPROMレイアウト版（v1.73d/e/v2.01=4）
} config;

const uint32_t CONFIG_MAGIC   = 0xDEADBEEF;
const uint8_t  CONFIG_VERSION = 4;

/* =========================== Runtime Params =========================== */
enum BusySrc { BUSY_SRC_DIGITAL, BUSY_SRC_A0, BUSY_SRC_AUTO };
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

/* ============================== Pins (Ver.5) ========================== */
const bool DFP_MIRROR_INVERT = true;

// Ver.5: DFPlayer BUSY mirror OUT
const uint8_t PIN_DFP_OUT   = 3;   // D3  (DF Player)

// Ver.5: Test SW
const uint8_t PIN_TEST_SW   = 2;   // D2  (ID・Test)

// LEDs
const uint8_t PIN_BUSY_LED  = 4;   // D4  (ModBusy)
const uint8_t PIN_PTT       = 5;   // D5  (PTT)
const uint8_t PIN_SUP_LED   = 6;   // D6  (抑止)
const uint8_t PIN_A0_LED    = 7;   // D7  (A0検知)

// Inputs
const uint8_t PIN_DFP_BSY   = 10;  // D10 (DFBusy) LOW=再生中
const uint8_t PIN_TM_BUSY   = 11;  // D11 (Digital入力)

// Analog
const uint8_t A0_PIN        = A0;

// DFPlayer UART (SoftwareSerial)  Ver.5: TX=D12, RX=D13
const uint8_t ARD_RX_FROM_DFP = 13; // D13 (RX) ← DFPlayer TX
const uint8_t ARD_TX_TO_DFP   = 12; // D12 (TX) → DFPlayer RX
SoftwareSerial dfpSerial(ARD_RX_FROM_DFP, ARD_TX_TO_DFP);

/* ========================= Constants / Guards ========================= */
const unsigned long REFRAC_MS    = 3000;   // 不応期
const unsigned long DEBOUNCE_MS  = 5;      // Digital入力デバウンス
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
uint16_t dig_edge_count = 0, a0_event_count = 0;

unsigned long lastBusyOffAt = 0;  // BUSYがOFFになった時刻（静寂起点）

// 送信後、BUSYが十分落ち着くまでイベント発火を禁止
bool postTxIgnore = false;
unsigned long postTxIdleStart = 0;

/* ============================== Utils ================================= */
inline bool readTmRaw() { return (digitalRead(PIN_TM_BUSY) == HIGH); }
inline bool readTmDigital()  {
  bool rawHigh = readTmRaw();
  return TMBUSY_ACTIVE_HIGH ? rawHigh : !rawHigh;
}
inline bool readBusy()  {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_DIGITAL) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0)      return a0Busy;
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
  if (postTxIgnore) return true;
  if (!SUPPRESSORS_ENABLED) return false;
  if ((long)(now - longSupUntil)  < 0) return true;
  if ((long)(now - burstSupUntil) < 0) return true;
  if ((long)(now - busySupUntil)  < 0) return true;
  return false;
}

/* ============================ Defaults/EEPROM ========================== */
void applyDefaults() {
  BUSY_INPUT_SOURCE = BUSY_SRC_DIGITAL;
  SUPPRESSORS_ENABLED = true;
  TX_AFTER_SUPPRESS_ENABLED = true;

  BUSY_MIN_MS = 500; BUSY_MAX_MS = 3900; IDLE_MIN_MS = 200;
  PERIOD_MS   = 30UL * 60UL * 1000UL;  // 30分
  TX_SUP_MS   = 3000;

  A0_LOW_TH = 300; A0_HIGH_TH = 700; A0_HOLD = 800;
  AUTO_WINDOW = 30UL * 60UL * 1000UL; LONG_TALK_MS = BUSY_MAX_MS;

  DFP_TIMEOUT_MS = 20000;          // 20秒
  TMBUSY_ACTIVE_HIGH = true;       // g1
  PERIOD_QUIET_MS    = 2000;       // 静寂2秒

  autoLocked = false;

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

  bool needSave = false;
  if (config.ver != CONFIG_VERSION) {
    Serial.print(F("[EEPROM] Version mismatch: stored="));
    Serial.print(config.ver); Serial.print(F(" expected="));
    Serial.println(CONFIG_VERSION);

    if (!(config.tmBusyActiveHigh == 0 || config.tmBusyActiveHigh == 1)) { config.tmBusyActiveHigh = 1; needSave = true; }
    if (config.dfpTimeoutMs > 600000UL) { config.dfpTimeoutMs = 20000UL; needSave = true; }
    if (config.periodQuietMs == 0 || config.periodQuietMs > 600000UL) { config.periodQuietMs = 2000UL; needSave = true; }

    config.ver = CONFIG_VERSION; needSave = true;
    if (needSave) EEPROM.put(0, config);
    Serial.println(F("[EEPROM] Migration/completion done -> updated version."));
  }

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
  else Serial.print(BUSY_INPUT_SOURCE==BUSY_SRC_DIGITAL?F("DIGITAL"):F("A0"));
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
  Serial.println(F("m0=DIGITAL(D11), m1=A0, m2=AUTO"));
  Serial.println(F("n###=busyMin(ms), b####=busyMax(ms), i###=idleMin(ms)"));
  Serial.println(F("s0/1=suppress OFF/ON, t0/1=txAfterSuppress OFF/ON, r####=ms"));
  Serial.println(F("p##=period(min, 0=stop), k####=period quiet(ms)"));
  Serial.println(F("L###/G###/a####=A0 lo/hi/hold(ms), w##=AUTO(min)"));
  Serial.println(F("d####=DFP timeout(ms), 0=disable"));
  Serial.println(F("g0/g1=TM BUSY polarity (0:LOW=busy, 1:HIGH=busy)"));
  Serial.println(F("q=summary, x=STOP, R=RESUME (STOP only), H=preset(s0/t0/b3900), F=factory"));
  Serial.println(F("0..3=log level, h=help"));
}

/* ============================ Command Parser ========================== */
void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();

    // 数値不要コマンドは即処理（反応改善）
    if (c=='q') { printSummary(); continue; }
    if (c=='h') { printHelp();    continue; }
    if (c=='x') { stopped = true; Serial.println(F("[STOP]")); continue; }
    if (c=='H') {
      SUPPRESSORS_ENABLED=false; TX_AFTER_SUPPRESS_ENABLED=false;
      BUSY_MAX_MS=3900; LONG_TALK_MS=3900;
      saveSettings(); printSummary();
      continue;
    }
    if (c=='F') {
      applyDefaults(); saveSettings();
      Serial.println(F("[RESET] Factory defaults restored."));
      continue;
    }
    if (c>='0' && c<='3') { LOG_LEVEL=(LogLvl)(c-'0'); continue; }
    if (c=='R') { continue; } // STOP中のみ loop() 側で処理

    // 数値が必要なコマンド
    unsigned long timeout = millis() + 150;
    while (!Serial.available() && millis() < timeout) { }
    long val = Serial.parseInt();
    bool chg = true;

    switch (c) {
      case 'm':
        if      (val==0) { BUSY_INPUT_SOURCE=BUSY_SRC_DIGITAL; autoLocked=true; }
        else if (val==1) { BUSY_INPUT_SOURCE=BUSY_SRC_A0;      autoLocked=true; }
        else if (val==2) { BUSY_INPUT_SOURCE=BUSY_SRC_AUTO;    autoLocked=false; windowStartTS=millis(); dig_edge_count=0; a0_event_count=0; }
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

      case 'k':
        if (val>=0 && val<=600000) PERIOD_QUIET_MS=(unsigned long)val;
        else chg=false;
        break;

      case 'L': A0_LOW_TH  = (int)val; break;
      case 'G': A0_HIGH_TH = (int)val; break;
      case 'a': if (val>=0) A0_HOLD=(unsigned long)val; else chg=false; break;

      case 'w': if (val>=1) { AUTO_WINDOW=(unsigned long)val*60UL*1000UL; windowStartTS=millis(); dig_edge_count=0; a0_event_count=0; } else chg=false; break;

      case 'd': if (val>=0 && val<=600000) DFP_TIMEOUT_MS=(unsigned long)val; else chg=false; break;

      case 'g':
        if (val==0) TMBUSY_ACTIVE_HIGH=false;
        else if (val==1) TMBUSY_ACTIVE_HIGH=true;
        else chg=false;
        break;

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
    uint16_t dig = dig_edge_count, a0 = a0_event_count;

    if      (dig < 10 && a0 >= 20) n = BUSY_SRC_A0;
    else if (a0 < 20 && dig >= 10) n = BUSY_SRC_DIGITAL;
    else n = (dig >= a0) ? BUSY_SRC_DIGITAL : BUSY_SRC_A0;

    BUSY_INPUT_SOURCE = n; autoLocked = true;
    autoSwitchBlinkUntil = now + 3000; saveSettings();

    Serial.print(F("[AUTO-FIXED] Lock to ")); Serial.println(n==BUSY_SRC_DIGITAL?F("DIGITAL"):F("A0"));
  }
}

/* ============================ setup / loop ============================ */
void setup() {
  Serial.begin(115200);
  dfpSerial.begin(9600);

  migrateOrInit();

  pinMode(PIN_TEST_SW,  INPUT_PULLUP);

  pinMode(PIN_DFP_OUT,  OUTPUT);
  pinMode(PIN_BUSY_LED, OUTPUT);
  pinMode(PIN_SUP_LED,  OUTPUT);
  pinMode(PIN_A0_LED,   OUTPUT);

  pinMode(PIN_PTT,      OUTPUT);
  pinMode(PIN_TM_BUSY,  INPUT_PULLUP);
  pinMode(PIN_DFP_BSY,  INPUT_PULLUP);

  delay(500);
  dfpSend(0x06, 20); // volume

  unsigned long now = millis();
  nextPeriodicAt = (PERIOD_MS>0) ? now + PERIOD_MS : 0;
  windowStartTS  = now;
  lastBusyOffAt  = now;

  Serial.println(F("[START] OpenCCVoice v2.01 Unified/Safe (Ver.5 pinmap, R-RESUME)"));
  printSummary(); printHelp();
}

void loop() {
  unsigned long now = millis();

  // STOP中：Rで復帰（確実に効かせるため最優先処理）
  if (stopped) {
    setPtt(false);
    digitalWrite(PIN_BUSY_LED, LOW);
    digitalWrite(PIN_SUP_LED,  LOW);
    digitalWrite(PIN_A0_LED,   LOW);
    digitalWrite(PIN_DFP_OUT,  LOW);

    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'R') { stopped = false; Serial.println(F("[RESUME]")); }
      else if (c == 'h') { printHelp(); }
      else if (c == 'q') { printSummary(); }
    }
    delay(5);
    return;
  }

  handleSerialCmd();

  bool pAct = (state != IDLE) || ((long)now < (long)pttMinOn);

  // DFPlayer BUSY(D10) → D3ミラー（反転）
  bool dfpPlaying = (digitalRead(PIN_DFP_BSY) == LOW); // LOW=再生中
  digitalWrite(PIN_DFP_OUT, DFP_MIRROR_INVERT ? (dfpPlaying ? HIGH : LOW)
                                              : (dfpPlaying ? LOW  : HIGH));

  // Digital入力(D11) デバウンス
  bool rTm = readTmDigital();
  if (rTm != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) {
    tmBusyFiltered = rTm; tmDebounceTS = now;
    if (tmBusyFiltered) dig_edge_count++;
  }

  // A0判定（抑止中でもBUSY観測は継続、AUTOカウントだけ抑止中は増やさない）
  if (!pAct) {
    bool supNow = isSuppressedNow(now);
    int v = analogRead(A0_PIN);

    if (!a0Detect && v < A0_LOW_TH) {
      a0Detect = true;
    } else if (a0Detect && v > A0_HIGH_TH) {
      a0Detect = false;
      if (!supNow) a0_event_count++;
    }

    if (a0Detect) a0LastSignalTS = now;
    a0Busy = a0Detect || (a0LastSignalTS && (now - a0LastSignalTS < A0_HOLD));
  } else {
    a0Detect = false;
    a0Busy   = false;
  }

  bool rB = readBusy();

  // 送信後ガード解除：BUSYが連続で IDLE_MIN_MS 以上OFF になったら解除
  if (postTxIgnore) {
    if (!rB) {
      if (postTxIdleStart == 0) postTxIdleStart = now;
      if (now - postTxIdleStart >= IDLE_MIN_MS) {
        postTxIgnore = false;
        postTxIdleStart = 0;
        if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[POST-TX] cleared by idle-stable"));
      }
    } else {
      postTxIdleStart = 0;
    }
  }

  // BUSY→OFF の瞬間で静寂起点更新
  if (!rB && tmBusyPrev) lastBusyOffAt = now;
  tmBusyPrev = rB;

  // LED表示
  digitalWrite(PIN_BUSY_LED, rB ? HIGH : LOW);    // D4: BUSY
  digitalWrite(PIN_A0_LED,   a0Busy ? HIGH : LOW); // D7: A0検知
  bool sup = isSuppressedNow(now);
  digitalWrite(PIN_SUP_LED,  sup ? HIGH : LOW);   // D6: 抑止

  // 短発（001）
  if (!pAct && !sup) {
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

  // 周期ID スケジューラ（catch-up）
  if (PERIOD_MS > 0) {
    bool crossed = false;
    while ((long)(now - nextPeriodicAt) >= 0) {
      nextPeriodicAt += PERIOD_MS;
      crossed = true;
    }
    if (crossed) {
      periodicDue = true;
      if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[Periodic] due flag set (catch-up)"));
    }
  } else {
    periodicDue = false;
  }

  // 周期ID（Quiet Guard）
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
        setPtt(false);
        state = IDLE;

        // 送信直後ガード
        postTxIgnore = true;
        postTxIdleStart = 0;

        // 不応期の起点更新
        lastTriggerAt = now;

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
