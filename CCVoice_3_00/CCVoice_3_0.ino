/************************************************************
 * OpenCCVoice Guidance Controller
 * Version : 3.00 (Unified Logic & Advanced Hardware - All-in-One)
 * Target  : Arduino Nano (ATmega328P, 5V)
 *
 * 【v3.00 変更点 (v2.24f + v1.80i 統合)】
 * - [ロジック刷新] v1.80iベースの絶対時刻ステートマシンを採用
 *   PTT_ON_WAIT / PTT_OFF_WAIT のブロッキング耐性を向上
 * - [抑止統合] burst/long-talkの複雑なタイマーを廃止し、
 *   busySupUntil 単一での管理（起点A/起点B）へ統合
 * - [コマンド・変数整理] s0/s1, t0/t1, i####, H を廃止
 *   PRE/POST を j#### / J#### に可変化（EEPROM保存）
 * - [周期ID] busy/抑止中の場合は「破棄（SKIP）」する仕様に変更
 * - [ハードウェア維持] DS3231 RTC正時アライン、AT24C32ログ・設定保存、
 *   Z/V/T/u等の各種拡張コマンドはv2.24fから継続搭載
 * - [EEPROM] CONFIG_VERSION=7。v6からのマイグレーションロジック実装
 * - [ファイル統合] v3.0 では ccvoice_log.h / ccvoice_config.h を本体に統合
 *   → 配布は本ファイル1個のみで完結
 *
 * 【Ver.5 ピンマップ】
 * DF Player  : D3   (DFP BUSYミラー出力：再生中=HIGH 反転)
 * Test SW    : D2   (テストSW)
 * ModBusy    : D4   (BUSY LED)
 * PTT        : D5  
 * 抑止       : D6   (抑止LED)
 * A0検知     : D7   (A0検知LED)
 * DFBusy     : D10  (DFPlayer BUSY入力：LOW=再生中)
 * Digital入力: D11  (TM BUSY入力)
 * TX         : D13  (Arduino TX → DFPlayer RX)
 * RX         : D12  (Arduino RX ← DFPlayer TX)
 * I2C SDA    : A4   (DS3231 + AT24C32)
 * I2C SCL    : A5   (DS3231 + AT24C32)
 ************************************************************/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Wire.h>
#include <avr/wdt.h>

/* ============================== DS3231 ============================ */
#define DS3231_ADDR 0x68

struct RtcTime {
  uint8_t  sec, min, hour, day, month;
  uint16_t year;
};

uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

bool rtcAvailable = false;

bool rtcRead(RtcTime &t) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7) != 7) return false;
  t.sec   = bcd2dec(Wire.read() & 0x7F);
  t.min   = bcd2dec(Wire.read() & 0x7F);
  t.hour  = bcd2dec(Wire.read() & 0x3F);
  Wire.read(); // skip day of week
  t.day   = bcd2dec(Wire.read() & 0x3F);
  t.month = bcd2dec(Wire.read() & 0x1F);
  t.year  = 2000 + bcd2dec(Wire.read());
  return true;
}

bool rtcWrite(const RtcTime &t) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  Wire.write(dec2bcd(t.sec));
  Wire.write(dec2bcd(t.min));
  Wire.write(dec2bcd(t.hour));
  Wire.write(0x01);
  Wire.write(dec2bcd(t.day));
  Wire.write(dec2bcd(t.month));
  Wire.write(dec2bcd((uint8_t)(t.year - 2000)));
  return (Wire.endTransmission() == 0);
}

bool rtcProbe() {
  Wire.beginTransmission(DS3231_ADDR);
  return (Wire.endTransmission() == 0);
}

/* ============================== AT24C32 =========================== */
#define AT24C32_ADDR 0x57
#define AT24C32_PAGE 32

bool extEepromAvailable = false;

bool extEepromProbe() {
  Wire.beginTransmission(AT24C32_ADDR);
  return (Wire.endTransmission() == 0);
}

void extEepromRead(uint16_t addr, uint8_t* buf, uint16_t len) {
  Wire.beginTransmission(AT24C32_ADDR);
  Wire.write((uint8_t)(addr >> 8));
  Wire.write((uint8_t)(addr & 0xFF));
  Wire.endTransmission();
  uint16_t remaining = len, offset = 0;
  while (remaining > 0) {
    uint8_t chunk = (remaining > 32) ? 32 : (uint8_t)remaining;
    Wire.requestFrom((uint8_t)AT24C32_ADDR, chunk);
    for (uint8_t i = 0; i < chunk && Wire.available(); i++) buf[offset++] = Wire.read();
    remaining -= chunk;
  }
}

bool extEepromWrite(uint16_t addr, const uint8_t* buf, uint16_t len) {
  uint16_t offset = 0;
  while (offset < len) {
    uint8_t pageRem = AT24C32_PAGE - (uint8_t)((addr + offset) % AT24C32_PAGE);
    uint8_t chunk   = (uint8_t)min((uint16_t)pageRem, (uint16_t)(len - offset));
    Wire.beginTransmission(AT24C32_ADDR);
    Wire.write((uint8_t)((addr + offset) >> 8));
    Wire.write((uint8_t)((addr + offset) & 0xFF));
    for (uint8_t i = 0; i < chunk; i++) Wire.write(buf[offset + i]);
    if (Wire.endTransmission() != 0) return false;
    delay(10);
    offset += chunk;
  }
  return true;
}

/* =========================== Enums & Logging ====================== */
enum BusySrc { BUSY_SRC_DIGITAL, BUSY_SRC_A0, BUSY_SRC_AUTO };
enum LogLvl  { LOG_OFF=0, LOG_MIN=1, LOG_FULL=2, LOG_DBG=3 };
volatile LogLvl LOG_LEVEL = LOG_MIN;

/* ============================== EEPROM ============================ */
struct MyConfig {
  uint32_t magic;
  uint8_t  busySrc;           // [m] 0:DIGITAL(D11), 1:A0, 2:AUTO
  uint32_t busyMin;           // [n]
  uint32_t busyMax;           // [b]
  uint32_t pttPreMs;          // [j]
  uint32_t pttPostMs;         // [J]
  uint32_t periodMin;         // [p] 分
  uint32_t txSupMs;           // [r]
  int      a0Low;             // [L]
  int      a0High;            // [G]
  uint32_t a0Hold;            // [a]
  uint32_t autoWinMin;        // [w]
  uint32_t dfpTimeoutMs;      // [d]
  uint8_t  tmBusyActiveHigh;  // [g]
  uint32_t periodQuietMs;     // [k]
  uint8_t  rtcAlignOn;        // [u]
  uint32_t saveTimestamp;     // 保存時刻（秒換算）
  uint8_t  ver;               // レイアウト版
} config;

struct MyConfigV6 {
  uint32_t magic; uint8_t busySrc; uint8_t suppressOn; uint8_t txAfSupOn;
  uint32_t busyMin; uint32_t busyMax; uint32_t idleMin; uint32_t periodMin;
  uint32_t txSupMs; int a0Low; int a0High; uint32_t a0Hold; uint32_t autoWinMin;
  uint32_t dfpTimeoutMs; uint8_t tmBusyActiveHigh; uint32_t periodQuietMs;
  uint8_t rtcAlignOn; uint32_t saveTimestamp; uint8_t ver;
};

const uint32_t CONFIG_MAGIC   = 0xDEADBEEF;
const uint8_t  CONFIG_VERSION = 7;
const uint16_t EXT_EEPROM_ADDR = 0x0000;

/* ============================== Event Log =========================
 * AT24C32 レイアウト:
 *   0x0000〜0x003F : config構造体（64バイト境界）
 *   0x0048〜0x004F : ログヘッダ（8バイト）
 *   0x0050〜0x0FFF : ログエントリ × 最大448件（9バイト/件）
 *
 * ログヘッダ（8バイト）:
 *   uint32_t magic    : 0xLOG1（初期化済み判定）
 *   uint16_t count    : 総記録件数（448超でラップ）
 *   uint16_t head     : 次書き込みインデックス（0〜447）
 *
 * ログエントリ（9バイト）:
 *   uint32_t ts       : タイムスタンプ（DS3231 秒換算、未取得時=0）
 *   uint8_t  evt      : イベント種別
 *   uint32_t data     : 付加データ
 *
 * イベント種別:
 *   LOG_EVT_BOT=0x01  : 起動（data=EEPROM ver）
 *   LOG_EVT_PER=0x02  : 周期ID送出（data=Track番号）
 *   LOG_EVT_CAR=0x03  : カーチャンク検知（data=BUSY時間ms）
 *   LOG_EVT_SUP=0x04  : 長話抑止（data=BUSY時間ms）
 *   LOG_EVT_BST=0x05  : バースト抑止（v3.0未使用、過去ログ表示用に保持）
 * ================================================================= */

#define LOG_MAGIC       0x4C4F4731UL  // "LOG1"
#define LOG_HDR_ADDR    0x0048U
#define LOG_DAT_ADDR    0x0050U
#define LOG_MAX_ENTRIES 448U
#define LOG_ENTRY_SIZE  9U

#define LOG_EVT_BOT  0x01
#define LOG_EVT_PER  0x02
#define LOG_EVT_CAR  0x03
#define LOG_EVT_SUP  0x04
#define LOG_EVT_BST  0x05

struct LogHeader {
  uint32_t magic;
  uint16_t count;
  uint16_t head;
};

struct LogEntry {
  uint32_t ts;
  uint8_t  evt;
  uint32_t data;
};

/* =========================== Runtime Params ======================= */
volatile BusySrc BUSY_INPUT_SOURCE;

unsigned long BUSY_MIN_MS;
unsigned long BUSY_MAX_MS;
unsigned long PTT_PRE_MS;
unsigned long PTT_POST_MS;
unsigned long PERIOD_MS;
unsigned long TX_SUP_MS;

int A0_LOW_TH;
int A0_HIGH_TH;
unsigned long A0_HOLD;
unsigned long AUTO_WINDOW;

unsigned long DFP_TIMEOUT_MS;
bool          TMBUSY_ACTIVE_HIGH;
unsigned long PERIOD_QUIET_MS;
bool          RTC_ALIGN_ON;

/* ============================== Pins (Ver.5) ====================== */
const bool    DFP_MIRROR_INVERT  = true;
const uint8_t PIN_DFP_OUT        = 3;
const uint8_t PIN_TEST_SW        = 2;
const uint8_t PIN_BUSY_LED       = 4;
const uint8_t PIN_PTT            = 5;
const uint8_t PIN_SUP_LED        = 6;
const uint8_t PIN_A0_LED         = 7;
const uint8_t PIN_DFP_BSY        = 10;
const uint8_t PIN_TM_BUSY        = 11;
const uint8_t A0_PIN             = A0;
const uint8_t ARD_RX_FROM_DFP    = 12; 
const uint8_t ARD_TX_TO_DFP      = 13;
SoftwareSerial dfpSerial(ARD_RX_FROM_DFP, ARD_TX_TO_DFP);

const unsigned long DEBOUNCE_MS  = 5;

// RTCアライン補正を行う周期（分）: 60の約数のみ
bool isAlignablePeriod(uint32_t pMin) {
  if (pMin == 0 || pMin > 60) return false;
  return (60 % pMin == 0);
}

/* ============================ State / Vars ======================== */
unsigned long windowStartTS = 0, autoSwitchBlinkUntil = 0;
bool autoLocked = false;

enum State { IDLE, PTT_ON_WAIT, PLAYING, PTT_OFF_WAIT };
State state = IDLE;

unsigned long tmBusyStart=0, tmDebounceTS=0, a0LastSignalTS=0;
bool tmBusyPrev=false, tmBusyFiltered=false, a0Detect=false, a0Busy=false;

unsigned long pttPreEndAt=0, pttPostEndAt=0, pttMinOn=0, nextPeriodicAt=0;
bool dfpStarted=false, pttOutState=false, clickWaiting=false, stopped=false;
uint16_t requestedTrack=0, nextPeriodicTrack=2;

unsigned long busySupUntil=0, busyHighSince=0, playingEnterAt=0;
uint8_t  clickCount=0, lastSwState=HIGH;
unsigned long firstClickTime=0;

uint16_t dig_edge_count = 0, a0_event_count = 0;
unsigned long lastBusyOffAt = 0;
bool prevSuppressed = false;

bool          rtcAlignActive = false;
unsigned long rtcResyncMs    = 0;
const unsigned long RTC_RESYNC_INTERVAL = 60000UL;

/* ============================== Utils ============================= */
inline bool readTmRaw()     { return (digitalRead(PIN_TM_BUSY) == HIGH); }
inline bool readTmDigital() { return TMBUSY_ACTIVE_HIGH ? readTmRaw() : !readTmRaw(); }
inline bool readBusy() {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_DIGITAL) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0)      return a0Busy;
  return (tmBusyFiltered || a0Busy);
}

void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E,0xFF,0x06,cmd,0x00,
                   (uint8_t)(param>>8),(uint8_t)(param & 0xFF),
                   0x00,0x00,0xEF};
  uint16_t s = 0;
  for (int i = 1; i < 7; i++) s += f[i];
  s = 0xFFFF - s + 1;
  f[7] = (uint8_t)(s >> 8); f[8] = (uint8_t)s;
  dfpSerial.write(f, 10);
}

void setPtt(bool on) {
  if (pttOutState == on) return;
  pttOutState = on;
  digitalWrite(PIN_PTT, on ? HIGH : LOW);
  if (LOG_LEVEL >= LOG_FULL) {
    Serial.print(F("[PTT] ")); Serial.println(on ? F("ON") : F("OFF"));
  }
}

void startPtt(uint16_t trk) {
  if (state != IDLE) return;
  requestedTrack = trk; dfpStarted = false;
  unsigned long now = millis();
  setPtt(true); 
  pttPreEndAt = now + PTT_PRE_MS; 
  pttMinOn = now + PTT_PRE_MS + 100;
  pttPostEndAt = 0;
  playingEnterAt = 0;
  state = PTT_ON_WAIT;
  if (LOG_LEVEL >= LOG_MIN) {
    Serial.print(F("[TX] Track ")); Serial.print(trk); Serial.println(F(" -> PRE start"));
  }
}

static inline bool isSuppressedNow(unsigned long now) {
  return ((long)(now - busySupUntil) < 0);
}

unsigned long calcNextAlignedAt(const RtcTime &t, uint32_t pMin, unsigned long now) {
  uint32_t totalSec  = (uint32_t)t.hour * 3600UL + (uint32_t)t.min * 60UL + (uint32_t)t.sec;
  uint32_t periodSec = pMin * 60UL;
  uint32_t slotSec   = totalSec % periodSec;
  uint32_t waitSec   = (slotSec == 0) ? periodSec : (periodSec - slotSec);
  return now + (unsigned long)waitSec * 1000UL;
}

uint32_t getCurrentTimestamp() {
  if (!rtcAvailable) return 0;
  RtcTime t;
  if (!rtcRead(t)) return 0;
  return (uint32_t)t.hour * 3600UL + (uint32_t)t.min * 60UL + (uint32_t)t.sec;
}

/* ========================= Event Log Functions ==================== */
bool logReadHeader(LogHeader &h) {
  extEepromRead(LOG_HDR_ADDR, (uint8_t*)&h, sizeof(h));
  return (h.magic == LOG_MAGIC);
}

void logWriteHeader(const LogHeader &h) {
  extEepromWrite(LOG_HDR_ADDR, (const uint8_t*)&h, sizeof(h));
}

void logInit() {
  LogHeader h;
  if (logReadHeader(h)) return;
  h.magic = LOG_MAGIC; h.count = 0; h.head = 0;
  logWriteHeader(h);
  Serial.println(F("[LOG] Initialized."));
}

void logWrite(uint8_t evt, uint32_t data) {
  if (!extEepromAvailable) return;
  LogHeader h;
  if (!logReadHeader(h)) { logInit(); logReadHeader(h); }

  LogEntry e;
  e.ts   = getCurrentTimestamp();
  e.evt  = evt;
  e.data = data;

  uint16_t addr = LOG_DAT_ADDR + (uint16_t)h.head * LOG_ENTRY_SIZE;
  extEepromWrite(addr, (const uint8_t*)&e, sizeof(e));

  h.head = (h.head + 1) % LOG_MAX_ENTRIES;
  if (h.count < LOG_MAX_ENTRIES) h.count++;
  logWriteHeader(h);
}

void printLogTime(uint32_t ts) {
  if (!rtcAvailable || ts == 0) {
    Serial.print(F("--/--/-- --:--:--"));
    return;
  }
  RtcTime t;
  if (!rtcRead(t)) { Serial.print(F("--/--/-- --:--:--")); return; }
  uint8_t h2 = (uint8_t)(ts / 3600UL % 24);
  uint8_t m2 = (uint8_t)(ts % 3600UL / 60);
  uint8_t s2 = (uint8_t)(ts % 60);
  Serial.print(t.year); Serial.print('/');
  if (t.month < 10) Serial.print('0'); Serial.print(t.month); Serial.print('/');
  if (t.day   < 10) Serial.print('0'); Serial.print(t.day);   Serial.print(' ');
  if (h2 < 10) Serial.print('0'); Serial.print(h2); Serial.print(':');
  if (m2 < 10) Serial.print('0'); Serial.print(m2); Serial.print(':');
  if (s2 < 10) Serial.print('0'); Serial.print(s2);
}

#define LOG_SHOW_COUNT 20
void logPrint() {
  if (!extEepromAvailable) {
    Serial.println(F("[LOG] AT24C32 not connected."));
    return;
  }
  LogHeader h;
  if (!logReadHeader(h) || h.count == 0) {
    Serial.println(F("[LOG] No entries."));
    return;
  }

  uint16_t show  = (h.count < LOG_SHOW_COUNT) ? h.count : LOG_SHOW_COUNT;
  uint16_t start;
  if (h.count <= LOG_MAX_ENTRIES) {
    start = (h.head >= show) ? (h.head - show) : 0;
  } else {
    start = (h.head + LOG_MAX_ENTRIES - show) % LOG_MAX_ENTRIES;
  }

  Serial.print(F("---- EVENT LOG ("));
  Serial.print(h.count); Serial.print('/');
  Serial.print(LOG_MAX_ENTRIES);
  Serial.println(F(") ----"));
  Serial.println(F("No  Time                Event  Data"));

  for (uint16_t i = 0; i < show; i++) {
    uint16_t idx  = (start + i) % LOG_MAX_ENTRIES;
    uint16_t addr = LOG_DAT_ADDR + idx * LOG_ENTRY_SIZE;
    LogEntry e;
    extEepromRead(addr, (uint8_t*)&e, sizeof(e));

    uint16_t no = (h.count > LOG_MAX_ENTRIES) ? (h.count - show + i + 1) : (i + 1);
    if (no < 100) Serial.print('0');
    if (no < 10)  Serial.print('0');
    Serial.print(no); Serial.print(' ');

    printLogTime(e.ts); Serial.print(' ');

    switch (e.evt) {
      case LOG_EVT_BOT: Serial.print(F("BOT    ver="));   Serial.println(e.data); break;
      case LOG_EVT_PER: Serial.print(F("PER    Track=")); Serial.println(e.data); break;
      case LOG_EVT_CAR: Serial.print(F("CAR    BUSY="));  Serial.print(e.data); Serial.println(F("ms")); break;
      case LOG_EVT_SUP: Serial.print(F("SUP    BUSY="));  Serial.print(e.data); Serial.println(F("ms")); break;
      case LOG_EVT_BST: Serial.print(F("BST    count=")); Serial.println(e.data); break;
      default:          Serial.print(F("???    "));       Serial.println(e.data); break;
    }
  }
  Serial.println(F("---- END LOG ----"));
}

void logClear() {
  if (!extEepromAvailable) {
    Serial.println(F("[LOG] AT24C32 not connected."));
    return;
  }
  LogHeader h;
  h.magic = LOG_MAGIC; h.count = 0; h.head = 0;
  logWriteHeader(h);
  Serial.println(F("[LOG] Cleared."));
}

/* ============================= Defaults / EEPROM ================== */
void applyDefaults() {
  BUSY_INPUT_SOURCE  = BUSY_SRC_DIGITAL;
  BUSY_MIN_MS        = 500;
  BUSY_MAX_MS        = 1500;
  PTT_PRE_MS         = 1000;
  PTT_POST_MS        = 1000;
  PERIOD_MS          = 30UL * 60UL * 1000UL;
  TX_SUP_MS          = 10000;
  A0_LOW_TH          = 300; A0_HIGH_TH = 700; A0_HOLD = 800;
  AUTO_WINDOW        = 30UL * 60UL * 1000UL;
  DFP_TIMEOUT_MS     = 20000;
  TMBUSY_ACTIVE_HIGH = true;
  PERIOD_QUIET_MS    = 2000;
  RTC_ALIGN_ON       = true;
  autoLocked         = false;

  config.magic            = CONFIG_MAGIC;
  config.busySrc          = (uint8_t)BUSY_INPUT_SOURCE;
  config.busyMin          = BUSY_MIN_MS;
  config.busyMax          = BUSY_MAX_MS;
  config.pttPreMs         = PTT_PRE_MS;
  config.pttPostMs        = PTT_POST_MS;
  config.periodMin        = PERIOD_MS / 60000UL;
  config.txSupMs          = TX_SUP_MS;
  config.a0Low            = A0_LOW_TH;
  config.a0High           = A0_HIGH_TH;
  config.a0Hold           = A0_HOLD;
  config.autoWinMin       = AUTO_WINDOW / 60000UL;
  config.dfpTimeoutMs     = DFP_TIMEOUT_MS;
  config.tmBusyActiveHigh = 1;
  config.periodQuietMs    = PERIOD_QUIET_MS;
  config.rtcAlignOn       = 1;
  config.saveTimestamp    = 0;
  config.ver              = CONFIG_VERSION;
}

void applyConfig() {
  BUSY_INPUT_SOURCE  = (BusySrc)config.busySrc;
  BUSY_MIN_MS        = config.busyMin;
  BUSY_MAX_MS        = config.busyMax;
  PTT_PRE_MS         = config.pttPreMs;
  PTT_POST_MS        = config.pttPostMs;
  PERIOD_MS          = (unsigned long)config.periodMin * 60000UL;
  TX_SUP_MS          = config.txSupMs;
  A0_LOW_TH          = config.a0Low;
  A0_HIGH_TH         = config.a0High;
  A0_HOLD            = config.a0Hold;
  AUTO_WINDOW        = (unsigned long)config.autoWinMin * 60000UL;
  DFP_TIMEOUT_MS     = config.dfpTimeoutMs;
  TMBUSY_ACTIVE_HIGH = (config.tmBusyActiveHigh == 1);
  PERIOD_QUIET_MS    = config.periodQuietMs;
  RTC_ALIGN_ON       = (config.rtcAlignOn == 1);
  autoLocked         = (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO);
}

void saveSettings() {
  config.magic            = CONFIG_MAGIC;
  config.busySrc          = (uint8_t)BUSY_INPUT_SOURCE;
  config.busyMin          = BUSY_MIN_MS;
  config.busyMax          = BUSY_MAX_MS;
  config.pttPreMs         = PTT_PRE_MS;
  config.pttPostMs        = PTT_POST_MS;
  config.periodMin        = PERIOD_MS / 60000UL;
  config.txSupMs          = TX_SUP_MS;
  config.a0Low            = A0_LOW_TH;
  config.a0High           = A0_HIGH_TH;
  config.a0Hold           = A0_HOLD;
  config.autoWinMin       = AUTO_WINDOW / 60000UL;
  config.dfpTimeoutMs     = DFP_TIMEOUT_MS;
  config.tmBusyActiveHigh = TMBUSY_ACTIVE_HIGH ? 1 : 0;
  config.periodQuietMs    = PERIOD_QUIET_MS;
  config.rtcAlignOn       = RTC_ALIGN_ON ? 1 : 0;
  config.saveTimestamp    = getCurrentTimestamp();
  config.ver              = CONFIG_VERSION;

  EEPROM.put(0, config);
  if (extEepromAvailable) extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config));
  if (LOG_LEVEL >= LOG_FULL) Serial.println(F("[EEPROM] Settings Saved."));
}

void migrateConfigV6toV7(const MyConfigV6& v6) {
  config.magic = CONFIG_MAGIC;
  config.busySrc = v6.busySrc;
  config.busyMin = v6.busyMin;
  config.busyMax = v6.busyMax == 3900 ? 1500 : v6.busyMax;
  config.pttPreMs = 1000;  // V7新設
  config.pttPostMs = 1000; // V7新設
  config.periodMin = v6.periodMin;
  config.txSupMs = v6.txSupMs == 3000 ? 10000 : v6.txSupMs;
  config.a0Low = v6.a0Low;
  config.a0High = v6.a0High;
  config.a0Hold = v6.a0Hold;
  config.autoWinMin = v6.autoWinMin;
  config.dfpTimeoutMs = v6.dfpTimeoutMs;
  config.tmBusyActiveHigh = v6.tmBusyActiveHigh;
  config.periodQuietMs = v6.periodQuietMs;
  config.rtcAlignOn = v6.rtcAlignOn;
  config.saveTimestamp = v6.saveTimestamp;
  config.ver = CONFIG_VERSION;
}

void migrateOrInit() {
  bool intOk = false, extOk = false;
  uint8_t intVer = 0, extVer = 0;
  
  uint32_t intMagic; EEPROM.get(0, intMagic);
  if (intMagic == CONFIG_MAGIC) { 
    intOk = true; 
    intVer = EEPROM.read(offsetof(MyConfigV6, ver));
  }

  uint32_t extMagic;
  if (extEepromAvailable) {
    extEepromRead(EXT_EEPROM_ADDR, (uint8_t*)&extMagic, 4);
    if (extMagic == CONFIG_MAGIC) { 
      extOk = true; 
      uint8_t tmp; 
      extEepromRead(EXT_EEPROM_ADDR + offsetof(MyConfigV6, ver), &tmp, 1); 
      extVer = tmp; 
    }
  }

  if (!intOk && !extOk) {
    Serial.println(F("[EEPROM] Init defaults."));
    applyDefaults(); saveSettings(); return;
  }

  MyConfigV6 oldCfg;
  if (intOk) {
    if (intVer == 6) { 
      EEPROM.get(0, oldCfg); 
      migrateConfigV6toV7(oldCfg); 
      EEPROM.put(0, config);
      Serial.println(F("[EEPROM] Migrated from V6 (internal)."));
    }
    else EEPROM.get(0, config);
  } else if (extOk) {
    if (extVer == 6) { 
      extEepromRead(EXT_EEPROM_ADDR, (uint8_t*)&oldCfg, sizeof(MyConfigV6)); 
      migrateConfigV6toV7(oldCfg);
      Serial.println(F("[EEPROM] Migrated from V6 (external)."));
    }
    else extEepromRead(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config));
  }

  applyConfig();
  Serial.println(F("[EEPROM] Settings Loaded."));
}

/* ============================== Prints ============================ */
void printRtcTime() {
  if (!rtcAvailable) { Serial.print(F("N/A(no RTC)")); return; }
  RtcTime t; 
  if (!rtcRead(t)) { Serial.print(F("N/A(read err)")); return; }
  Serial.print(t.year); Serial.print('/');
  if (t.month < 10) Serial.print('0'); Serial.print(t.month); Serial.print('/');
  if (t.day < 10) Serial.print('0'); Serial.print(t.day); Serial.print(' ');
  if (t.hour < 10) Serial.print('0'); Serial.print(t.hour); Serial.print(':');
  if (t.min < 10) Serial.print('0'); Serial.print(t.min); Serial.print(':');
  if (t.sec < 10) Serial.print('0'); Serial.print(t.sec);
}

void printSummary() {
  Serial.print(F("[RTC] ")); printRtcTime();
  Serial.print(F("  Align=")); Serial.println(rtcAlignActive ? F("ON(RTC)") : F("OFF(millis)"));
  Serial.print(F("[CFG] EEPROM_VER=")); Serial.print(config.ver);
  Serial.print(F(" SRC="));
  if (BUSY_INPUT_SOURCE == BUSY_SRC_AUTO) Serial.print(F("AUTO"));
  else Serial.print(BUSY_INPUT_SOURCE == BUSY_SRC_DIGITAL ? F("DIGITAL") : F("A0"));
  if (autoLocked) Serial.print(F("(LOCK)"));
  Serial.print(F(" MIN=")); Serial.print(BUSY_MIN_MS);
  Serial.print(F(" MAX=")); Serial.print(BUSY_MAX_MS);
  Serial.print(F(" PRE=")); Serial.print(PTT_PRE_MS);
  Serial.print(F(" POST=")); Serial.print(PTT_POST_MS);
  Serial.print(F(" SUP=")); Serial.print(TX_SUP_MS);
  Serial.print(F(" PER(min)=")); Serial.print(PERIOD_MS / 60000UL);
  Serial.print(F(" QUIET(ms)=")); Serial.print(PERIOD_QUIET_MS);
  Serial.print(F(" A0[L/H]=")); Serial.print(A0_LOW_TH); Serial.print('/'); Serial.print(A0_HIGH_TH);
  Serial.print(F(" HOLD=")); Serial.print(A0_HOLD);
  Serial.print(F(" AUTO(min)=")); Serial.print(AUTO_WINDOW / 60000UL);
  Serial.print(F(" DFP_TO=")); Serial.print(DFP_TIMEOUT_MS);
  Serial.print(F(" POL=")); Serial.println(TMBUSY_ACTIVE_HIGH ? F("HIGH=busy") : F("LOW=busy"));
}

void printHelp() {
  Serial.println(F("---- HELP (v3.00) ----"));
  Serial.println(F("[受信] n###=MIN(ms) b####=MAX(ms)"));
  Serial.println(F("[送信] j####=PRE(ms) J####=POST(ms)"));
  Serial.println(F("[抑止] r####=SUP(ms)"));
  Serial.println(F("[周期] p##=period(min)  k####=quiet(ms)"));
  Serial.println(F("[BUSY] m0=D11 m1=A0 m2=AUTO  g0/g1=POLARITY"));
  Serial.println(F("[A0]   L###=lo G###=hi a####=hold w##=AUTO(min)"));
  Serial.println(F("[DFP]  d####=timeout(ms,0=off)"));
  Serial.println(F("[RTC]  TYYYYMMDDHHmmss=set  u0/u1=align OFF/ON"));
  Serial.println(F("[LOG]  v=show  v0=clear"));
  Serial.println(F("[CMD]  V=ver q=cfg h=help x=STOP R=RESUME F=factory Z=SW_reset"));
  Serial.println(F("[LVL]  l0=off l1=min l2=full l3=dbg"));
}

/* ============================== Logic Modules ===================== */
void recalcPeriodicAlign(unsigned long now) {
  uint32_t pMin = PERIOD_MS / 60000UL;
  if (!RTC_ALIGN_ON || !rtcAvailable || !isAlignablePeriod(pMin)) {
    rtcAlignActive = false; return;
  }
  RtcTime t;
  if (!rtcRead(t)) { rtcAlignActive = false; return; }
  rtcAlignActive = true;
  nextPeriodicAt = calcNextAlignedAt(t, pMin, now);
}

void maybeAuto(unsigned long now) {
  if (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO || autoLocked) return;
  if ((long)(now - windowStartTS) >= (long)AUTO_WINDOW) {
    BusySrc n;
    if      (dig_edge_count < 10 && a0_event_count >= 20) n = BUSY_SRC_A0;
    else if (a0_event_count < 20 && dig_edge_count >= 10) n = BUSY_SRC_DIGITAL;
    else n = (dig_edge_count >= a0_event_count) ? BUSY_SRC_DIGITAL : BUSY_SRC_A0;
    BUSY_INPUT_SOURCE = n; autoLocked = true;
    autoSwitchBlinkUntil = now + 3000; saveSettings();
    if (LOG_LEVEL >= LOG_MIN) {
      Serial.print(F("[AUTO-FIXED] Lock to "));
      Serial.println(n == BUSY_SRC_DIGITAL ? F("DIGITAL") : F("A0"));
    }
  }
}

void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'q') { printSummary(); continue; }
    if (c == 'h') { printHelp(); continue; }
    if (c == 'v') {
      unsigned long t0 = millis() + 100; while (!Serial.available() && millis() < t0);
      if (Serial.available() && Serial.peek() == '0') { Serial.read(); logClear(); } else logPrint();
      continue;
    }
    if (c == 'V') { 
      Serial.print(F("[VER] OpenCCVoice v3.00 (EEPROM_VER=")); 
      Serial.print(config.ver); 
      Serial.println(F(")")); 
      continue; 
    }
    if (c == 'Z') { 
      Serial.println(F("[RESET] SW reset...")); 
      delay(100); 
      wdt_enable(WDTO_15MS); 
      while(1); 
    }
    if (c == 'x') { stopped = true; Serial.println(F("[STOP]")); continue; }
    if (c == 'R') { 
      if (stopped) { stopped = false; Serial.println(F("[RESUME]")); } 
      continue; 
    }
    if (c == 'F') { 
      applyDefaults(); saveSettings(); recalcPeriodicAlign(millis()); 
      Serial.println(F("[RESET] Factory")); 
      continue; 
    }
    
    if (c == 'l') {
      long val = Serial.parseInt();
      if (val>=0 && val<=3) LOG_LEVEL = (LogLvl)val;
      Serial.print(F("[LOG] level=")); Serial.println(LOG_LEVEL); 
      continue;
    }

    if (c == 'T') {
      char buf[15]; memset(buf, 0, sizeof(buf)); 
      uint8_t idx = 0; unsigned long tOut = millis() + 2000;
      while (idx < 14 && millis() < tOut) if (Serial.available()) buf[idx++] = Serial.read();
      if (idx == 14) {
        RtcTime t;
        t.year = (buf[0]-'0')*1000 + (buf[1]-'0')*100 + (buf[2]-'0')*10 + (buf[3]-'0');
        t.month= (buf[4]-'0')*10 + (buf[5]-'0'); 
        t.day  = (buf[6]-'0')*10 + (buf[7]-'0');
        t.hour = (buf[8]-'0')*10 + (buf[9]-'0'); 
        t.min  = (buf[10]-'0')*10 + (buf[11]-'0');
        t.sec  = (buf[12]-'0')*10 + (buf[13]-'0');
        if (t.year < 2000 || t.year > 2099 ||
            t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 ||
            t.hour > 23 || t.min > 59 || t.sec > 59) {
          Serial.println(F("[RTC] T: invalid datetime"));
          continue;
        }
        if (!rtcAvailable) { Serial.println(F("[RTC] T: DS3231 not found")); continue; }
        if (rtcWrite(t)) { 
          Serial.print(F("[RTC] Set: ")); printRtcTime(); Serial.println(); 
          recalcPeriodicAlign(millis()); 
        } else {
          Serial.println(F("[RTC] T: write failed"));
        }
      } else {
        Serial.println(F("[RTC] T: timeout"));
      }
      continue;
    }

    long val = Serial.parseInt(); bool chg = true;
    switch (c) {
      case 'm': if (val==0) { BUSY_INPUT_SOURCE=BUSY_SRC_DIGITAL; autoLocked=true; }
                else if (val==1) { BUSY_INPUT_SOURCE=BUSY_SRC_A0; autoLocked=true; }
                else if (val==2) { BUSY_INPUT_SOURCE=BUSY_SRC_AUTO; autoLocked=false; 
                                   windowStartTS=millis(); dig_edge_count=0; a0_event_count=0; }
                else chg=false; break;
      case 'b': if (val>=500) BUSY_MAX_MS=val; else chg=false; break;
      case 'n': if (val>=100) BUSY_MIN_MS=val; else chg=false; break;
      case 'j': if (val>=100) PTT_PRE_MS=val; else chg=false; break;
      case 'J': if (val>=100) PTT_POST_MS=val; else chg=false; break;
      case 'r': if (val>=0)   TX_SUP_MS=val; else chg=false; break;
      case 'p': if (val>=0) { 
                  PERIOD_MS=val*60000UL; 
                  nextPeriodicAt=millis()+PERIOD_MS; 
                  recalcPeriodicAlign(millis()); 
                } else chg=false; break;
      case 'k': if (val>=0 && val<=600000) PERIOD_QUIET_MS=val; else chg=false; break;
      case 'L': A0_LOW_TH=val; break;
      case 'G': A0_HIGH_TH=val; break;
      case 'a': if (val>=0) A0_HOLD=val; else chg=false; break;
      case 'w': if (val>=1) { 
                  AUTO_WINDOW=val*60000UL; 
                  windowStartTS=millis(); 
                  dig_edge_count=0; a0_event_count=0; 
                } else chg=false; break;
      case 'd': if (val>=0 && val<=600000) DFP_TIMEOUT_MS=val; else chg=false; break;
      case 'g': if (val==0||val==1) TMBUSY_ACTIVE_HIGH=(val==1); else chg=false; break;
      case 'u': if (val==0||val==1) { 
                  RTC_ALIGN_ON=(val==1); 
                  recalcPeriodicAlign(millis()); 
                } else chg=false; break;
      case 's': case 't': case 'i': case 'H': 
                Serial.println(F("[CMD] Obsolete command (廃止)")); chg = false; break;
      default: chg = false; break;
    }
    if (chg) { saveSettings(); printSummary(); }
  }
}

void processBusyLogic(unsigned long now, bool pAct, bool rB) {
  if (rB) {
    if (tmBusyStart == 0) tmBusyStart = now;
  } else {
    if (tmBusyStart != 0) {
      unsigned long dur = now - tmBusyStart;
      tmBusyStart = 0;

      if (dur >= BUSY_MAX_MS) {
        busySupUntil = now + TX_SUP_MS;  // 起点B
        logWrite(LOG_EVT_SUP, dur);
        if (LOG_LEVEL >= LOG_MIN) { 
          Serial.print(F("[SUP] START ")); Serial.print(TX_SUP_MS); 
          Serial.println(F("ms (from RX-end)")); 
        }
      } else if (dur >= BUSY_MIN_MS) {
        if (!pAct && !isSuppressedNow(now)) {
          logWrite(LOG_EVT_CAR, dur);
          startPtt(1);
        } else if (LOG_LEVEL >= LOG_FULL) {
          Serial.print(F("[RX] CK skip ")); 
          Serial.println(pAct ? F("(pAct)") : F("(suppressed)"));
        }
      }
    }
  }
}

void processPeriodicId(unsigned long now, bool rB) {
  if (PERIOD_MS == 0) return;
  bool crossed = false;

  if (rtcAlignActive) {
    if ((long)(now - nextPeriodicAt) >= 0) { 
      crossed = true; recalcPeriodicAlign(now); 
    }
    if ((long)(now - rtcResyncMs) >= 0) { 
      rtcResyncMs = now + RTC_RESYNC_INTERVAL; 
      recalcPeriodicAlign(now); 
    }
  } else {
    while ((long)(now - nextPeriodicAt) >= 0) { 
      nextPeriodicAt += PERIOD_MS; crossed = true; 
    }
  }

  if (!crossed) return;

  bool quietOK = (!rB) && ((long)(now - lastBusyOffAt) >= (long)PERIOD_QUIET_MS);
  bool guardOK = !isSuppressedNow(now);
  bool idleOK  = (state == IDLE);

  if (idleOK && quietOK && guardOK) {
    logWrite(LOG_EVT_PER, (uint32_t)nextPeriodicTrack);
    startPtt(nextPeriodicTrack);
    nextPeriodicTrack = (nextPeriodicTrack == 2 ? 3 : 2);
  } else if (LOG_LEVEL >= LOG_FULL) {
    Serial.println(F("[Periodic] SKIP"));
  }
}

void processStateMachine(unsigned long now) {
  switch (state) {
    case PTT_ON_WAIT:
      if ((long)(now - pttPreEndAt) >= 0) {
        if (requestedTrack) { dfpSend(0x03, requestedTrack); requestedTrack = 0; }
        state = PLAYING; playingEnterAt = now;
      }
      break;

    case PLAYING:
      if (digitalRead(PIN_DFP_BSY) == LOW) {
        dfpStarted = true; busyHighSince = 0;
      } else if (dfpStarted) {
        if (busyHighSince == 0) busyHighSince = now;
        if (now - busyHighSince >= 40) { 
          pttPostEndAt = now + PTT_POST_MS; 
          state = PTT_OFF_WAIT; 
        }
      }
      if (DFP_TIMEOUT_MS > 0 && playingEnterAt > 0 && (now - playingEnterAt >= DFP_TIMEOUT_MS)) {
        if (LOG_LEVEL >= LOG_MIN) Serial.println(F("[ERR] DFP Timeout -> Force PTT OFF"));
        pttPostEndAt = now + PTT_POST_MS; state = PTT_OFF_WAIT;
      }
      break;

    case PTT_OFF_WAIT:
      if ((long)(now - pttPostEndAt) >= 0 && (long)(now - pttMinOn) >= 0) {
        setPtt(false); state = IDLE;
        if (!readBusy()) {
          busySupUntil = now + TX_SUP_MS; // 起点A
          if (LOG_LEVEL >= LOG_MIN) { 
            Serial.print(F("[SUP] START ")); Serial.print(TX_SUP_MS); 
            Serial.println(F("ms (from POST-end)")); 
          }
        }
      }
      break;
    case IDLE: default: break;
  }
  if ((long)(now - pttMinOn) < 0) setPtt(true);
}

/* =========================== setup / loop ========================= */
void setup() {
  Serial.begin(115200); Serial.setTimeout(50);
  dfpSerial.begin(9600); Wire.begin();

  rtcAvailable = rtcProbe(); 
  if (rtcAvailable) Serial.println(F("[RTC] DS3231 found."));
  else Serial.println(F("[RTC] DS3231 not found -> millis() fallback."));
  
  extEepromAvailable = extEepromProbe();
  migrateOrInit();

  pinMode(PIN_TEST_SW, INPUT_PULLUP); pinMode(PIN_BUSY_LED, OUTPUT); pinMode(PIN_PTT, OUTPUT);
  pinMode(PIN_TM_BUSY, INPUT_PULLUP); pinMode(PIN_DFP_BSY, INPUT_PULLUP);
  pinMode(PIN_DFP_OUT, OUTPUT); pinMode(PIN_SUP_LED, OUTPUT); pinMode(PIN_A0_LED, OUTPUT);

  delay(500); dfpSend(0x06, 20);
  unsigned long now = millis();
  windowStartTS = now; lastBusyOffAt = now; rtcResyncMs = now + RTC_RESYNC_INTERVAL;
  if (PERIOD_MS > 0) { nextPeriodicAt = now + PERIOD_MS; recalcPeriodicAlign(now); }

  Serial.println(F("[START] OpenCCVoice v3.00"));
  printSummary(); printHelp();

  if (extEepromAvailable) { logInit(); logWrite(LOG_EVT_BOT, (uint32_t)CONFIG_VERSION); }
}

void loop() {
  unsigned long now = millis(); 
  handleSerialCmd();

  if (stopped) {
    setPtt(false); 
    digitalWrite(PIN_BUSY_LED, LOW); 
    digitalWrite(PIN_A0_LED, LOW); 
    digitalWrite(PIN_SUP_LED, LOW);
    delay(5); return;
  }

  bool pAct = (state != IDLE) || ((long)(now - pttMinOn) < 0);
  bool supNow = isSuppressedNow(now);
  
  bool dLow = (digitalRead(PIN_DFP_BSY) == LOW);
  digitalWrite(PIN_DFP_OUT, DFP_MIRROR_INVERT ? (dLow ? HIGH : LOW) : (dLow ? LOW : HIGH));

  bool rTm = readTmDigital();
  if (rTm != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) { 
    tmBusyFiltered = rTm; tmDebounceTS = now; 
    if (tmBusyFiltered) dig_edge_count++; 
  }

  if (!pAct) {
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
    a0Detect = false; a0Busy = false; 
  }

  bool rB = readBusy();
  if (!rB && tmBusyPrev) lastBusyOffAt = now;
  tmBusyPrev = rB;

  bool curSup = isSuppressedNow(now);
  digitalWrite(PIN_BUSY_LED, rB ? HIGH : LOW);
  digitalWrite(PIN_A0_LED, a0Busy ? HIGH : LOW);
  digitalWrite(PIN_SUP_LED, 
    (long)(autoSwitchBlinkUntil - now) > 0 ? ((now/250)%2==0 ? HIGH : LOW) : (curSup ? HIGH : LOW));

  processBusyLogic(now, pAct, rB);
  processPeriodicId(now, rB);
  processStateMachine(now);

  // テストSW 処理 (1-3クリックでTrack送出)
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

  // AUTO判定の実行
  maybeAuto(now);

  // 抑止状態変化の検出とログ出力
  if (!prevSuppressed && curSup) {
    if (LOG_LEVEL >= LOG_MIN) Serial.println(F("[SUP] ACTIVE"));
  } else if (prevSuppressed && !curSup) {
    if (LOG_LEVEL >= LOG_MIN) Serial.println(F("[SUP] CLEAR"));
  }
  prevSuppressed = curSup;
}
