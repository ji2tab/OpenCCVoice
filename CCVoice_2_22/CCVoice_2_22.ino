/************************************************************
 * OpenCCVoice Guidance Controller  (Unified / Safe)
 * Version : 2.22  (Fix TX/RX pin assignment)
 * Target  : Arduino Nano (ATmega328P, 5V)
 *
 * 【v2.22 変更点】
 * - DFPlayer TX/RX ピンアサイン修正（バグフィックス）
 *   誤: ARD_RX=D13 / ARD_TX=D12
 *   正: ARD_RX=D12 / ARD_TX=D13（配線図 Ver.5 に合わせる）
 *
 * 【v2.21 変更点】
 * - AT24C32 にイベントログ記録機能を追加
 *   - ログ領域: AT24C32 0x0040〜（リングバッファ、448件）
 *   - 記録イベント: PER/CAR/SUP/BST/BOT（9バイト/件）
 *   - v コマンド追加: 直近20件をシリアルモニタに表示
 *   - v0 コマンド追加: ログ全件消去
 *   - AT24C32 未接続時はログ記録をスキップ（動作に影響なし）
 *
 * 【v2.20 変更点】
 * - AT24C32（外部EEPROM / I²C 0x57）対応
 *   - 設定変更時に AT24C32 と内蔵EEPROM の両方へ同時保存
 *   - 起動時は AT24C32 を優先して読み込み
 *   - AT24C32 未接続時は内蔵EEPROM にフォールバック
 *   - タイムスタンプ（DS3231時刻）で新旧判定、不一致時に警告表示
 *
 * 【v2.10 変更点】
 * - DS3231 RTC モジュール対応（Wire.h / I2C A4=SDA, A5=SCL）
 * - 周期ID を「毎正時アライン」方式に変更
 * - T コマンド追加: 時刻設定 T20260311143000（YYYYMMDDHHmmss）
 * - u0/u1 コマンド追加: RTCアライン OFF/ON
 *
 * 【操作（115200 8N1, 改行なし/LF）】
 *  m0/m1/m2, n####, b####, i####, s0/1, t0/1, r####,
 *  p## (0=停止, 10/15/30 等=正時アライン, 60の約数でなければmillis),
 *  k####, L###, G###, a####, w##, d####, g0/g1,
 *  u0/u1 (RTCアライン OFF/ON),
 *  TYYYYMMDDHHmmss (RTC時刻設定  例: T20260311143000),
 *  v=ログ表示(直近20件)  v0=ログ消去,
 *  q, H, x, R, F, 0..3, h
 *
 * 【Ver.5 ピンマップ（変更なし）】
 *  DF Player  : D3   (DFP BUSYミラー出力：再生中=HIGH 反転)
 *  Test SW    : D2   (テストSW)
 *  ModBusy    : D4   (BUSY LED)
 *  PTT        : D5
 *  抑止       : D6   (抑止LED)
 *  A0検知     : D7   (A0検知LED)
 *  DFBusy     : D10  (DFPlayer BUSY入力：LOW=再生中)
 *  Digital入力: D11  (TM BUSY入力)
 *  TX         : D13  (Arduino TX → DFPlayer RX)
 *  RX         : D12  (Arduino RX ← DFPlayer TX)
 *  I2C SDA    : A4   (DS3231 + AT24C32)
 *  I2C SCL    : A5   (DS3231 + AT24C32)
 ************************************************************/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <Wire.h>

/* ============================== DS3231 ============================ */
#define DS3231_ADDR 0x68

struct RtcTime {
  uint8_t  sec, min, hour, day, month;
  uint16_t year;
};

static uint8_t bcd2dec(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }
static uint8_t dec2bcd(uint8_t d) { return ((d / 10) << 4) | (d % 10); }

bool rtcAvailable = false;

bool rtcRead(RtcTime &t) {
  Wire.beginTransmission(DS3231_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((uint8_t)DS3231_ADDR, (uint8_t)7) != 7) return false;
  t.sec   = bcd2dec(Wire.read() & 0x7F);
  t.min   = bcd2dec(Wire.read() & 0x7F);
  t.hour  = bcd2dec(Wire.read() & 0x3F);
  Wire.read();                            // day of week (skip)
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

/* =========================== Enums (前方宣言) ===================== */
enum BusySrc { BUSY_SRC_DIGITAL, BUSY_SRC_A0, BUSY_SRC_AUTO };
enum LogLvl  { LOG_NONE=0, LOG_ERR=1, LOG_INF=2, LOG_DBG=3 };
volatile LogLvl LOG_LEVEL = LOG_INF;

/* ============================== EEPROM ============================
 * CONFIG_VERSION 変更履歴:
 *   ver=4 : v1.73d/e, v2.01  (periodQuietMs 追加)
 *   ver=5 : v2.10             (rtcAlignOn 追加)
 *   ver=6 : v2.20             (saveTimestamp 追加、AT24C32二重保存)
 *
 * saveTimestamp: 設定保存時のDS3231時刻（秒換算）
 *   起動時にAT24C32と内蔵EEPROMのタイムスタンプを比較し新旧を判定。
 *   DS3231未接続時は 0（比較不能のためAT24C32優先）。
 * ================================================================= */
struct MyConfig {
  uint32_t magic;
  uint8_t  busySrc;           // [m] 0:DIGITAL(D11), 1:A0, 2:AUTO
  uint8_t  suppressOn;        // [s]
  uint8_t  txAfSupOn;         // [t]
  uint32_t busyMin;           // [n]
  uint32_t busyMax;           // [b]
  uint32_t idleMin;           // [i]
  uint32_t periodMin;         // [p] 分
  uint32_t txSupMs;           // [r]
  int      a0Low;             // [L]
  int      a0High;            // [G]
  uint32_t a0Hold;            // [a]
  uint32_t autoWinMin;        // [w]
  uint32_t dfpTimeoutMs;      // [d]
  uint8_t  tmBusyActiveHigh;  // [g]
  uint32_t periodQuietMs;     // [k]
  uint8_t  rtcAlignOn;        // [u] v2.10追加
  uint32_t saveTimestamp;     // v2.20追加: 保存時刻（秒換算）
  uint8_t  ver;               // レイアウト版（末尾固定）
} config;

const uint32_t CONFIG_MAGIC   = 0xDEADBEEF;
const uint8_t  CONFIG_VERSION = 6;
const uint16_t EXT_EEPROM_ADDR = 0x0000;

/* ============================== Event Log =========================
 * AT24C32 レイアウト:
 *   0x0000〜0x003F : config構造体（64バイト境界）
 *   0x0040〜0x0047 : ログヘッダ（8バイト）
 *   0x0048〜0x0FFF : ログエントリ × 最大448件（9バイト/件）
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
 *   LOG_EVT_BST=0x05  : バースト抑止（data=バースト回数）
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

// ログヘッダ読み込み
static bool logReadHeader(LogHeader &h) {
  extEepromRead(LOG_HDR_ADDR, (uint8_t*)&h, sizeof(h));
  return (h.magic == LOG_MAGIC);
}

// ログヘッダ書き込み
static void logWriteHeader(const LogHeader &h) {
  extEepromWrite(LOG_HDR_ADDR, (const uint8_t*)&h, sizeof(h));
}

// ログ初期化（ヘッダ未設定時）
static void logInit() {
  LogHeader h;
  if (logReadHeader(h)) return;  // 既に初期化済み
  h.magic = LOG_MAGIC;
  h.count = 0;
  h.head  = 0;
  logWriteHeader(h);
  Serial.println(F("[LOG] Initialized."));
}

// エントリ1件書き込み
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

// 時刻文字列出力ヘルパー
static void printLogTime(uint32_t ts) {
  if (!rtcAvailable || ts == 0) {
    Serial.print(F("--/--/-- --:--:--"));
    return;
  }
  // タイムスタンプは秒換算のみのため、日付はDS3231から別途取得
  RtcTime t;
  if (!rtcRead(t)) { Serial.print(F("--/--/-- --:--:--")); return; }
  // 現在時刻の秒換算と比較してオフセットを補正（簡易）
  uint32_t nowSec = (uint32_t)t.hour*3600UL + (uint32_t)t.min*60UL + t.sec;
  // 日付込みの厳密な復元は不要: 記録時刻を時分秒で表示
  uint8_t h2 = (uint8_t)(ts / 3600UL % 24);
  uint8_t m2 = (uint8_t)(ts % 3600UL / 60);
  uint8_t s2 = (uint8_t)(ts % 60);
  // 日付はDS3231の現在値を流用（同日ログ前提）
  Serial.print(t.year); Serial.print('/');
  if (t.month < 10) Serial.print('0'); Serial.print(t.month); Serial.print('/');
  if (t.day   < 10) Serial.print('0'); Serial.print(t.day);   Serial.print(' ');
  if (h2 < 10) Serial.print('0'); Serial.print(h2); Serial.print(':');
  if (m2 < 10) Serial.print('0'); Serial.print(m2); Serial.print(':');
  if (s2 < 10) Serial.print('0'); Serial.print(s2);
  (void)nowSec;
}

// ログ表示（直近 LOG_SHOW_COUNT 件）
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
  // 古い順に表示するため、先頭インデックスを計算
  uint16_t start;
  if (h.count <= LOG_MAX_ENTRIES) {
    // まだラップしていない
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

    // No
    uint16_t no = (h.count > LOG_MAX_ENTRIES)
                ? (h.count - show + i + 1)
                : (i + 1);
    if (no < 100) Serial.print('0');
    if (no < 10)  Serial.print('0');
    Serial.print(no); Serial.print(' ');

    // Time
    printLogTime(e.ts); Serial.print(' ');

    // Event + Data
    switch (e.evt) {
      case LOG_EVT_BOT:
        Serial.print(F("BOT    ver="));   Serial.println(e.data); break;
      case LOG_EVT_PER:
        Serial.print(F("PER    Track=")); Serial.println(e.data); break;
      case LOG_EVT_CAR:
        Serial.print(F("CAR    BUSY="));  Serial.print(e.data); Serial.println(F("ms")); break;
      case LOG_EVT_SUP:
        Serial.print(F("SUP    BUSY="));  Serial.print(e.data); Serial.println(F("ms")); break;
      case LOG_EVT_BST:
        Serial.print(F("BST    count=")); Serial.println(e.data); break;
      default:
        Serial.print(F("???    "));       Serial.println(e.data); break;
    }
  }
  Serial.println(F("---- END LOG ----"));
}

// ログ消去
void logClear() {
  if (!extEepromAvailable) {
    Serial.println(F("[LOG] AT24C32 not connected."));
    return;
  }
  LogHeader h;
  h.magic = LOG_MAGIC;
  h.count = 0;
  h.head  = 0;
  logWriteHeader(h);
  Serial.println(F("[LOG] Cleared."));
}

/* =========================== Runtime Params ======================= */
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
const uint8_t ARD_RX_FROM_DFP   = 12;  // D12 ← DFPlayer TX  ★v2.22修正
const uint8_t ARD_TX_TO_DFP     = 13;  // D13 → DFPlayer RX  ★v2.22修正
SoftwareSerial dfpSerial(ARD_RX_FROM_DFP, ARD_TX_TO_DFP);

/* ====================== Constants / Guards ======================== */
const unsigned long REFRAC_MS    = 3000;
const unsigned long DEBOUNCE_MS  = 5;
const unsigned long PTT_PRE_MS   = 1000;
const unsigned long PTT_POST_MS  = 1000;
const unsigned long LONG_SUP_MS  = 10000;
const unsigned long BURST_WIN_MS = 10000;
const unsigned int  BURST_TH     = 2;
const unsigned long BURST_SUP_MS = 10000;

// RTCアライン補正を行う周期（分）: 60の約数のみ
static bool isAlignablePeriod(uint32_t pMin) {
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

unsigned long lastBusyOffAt = 0;

bool postTxIgnore = false;
unsigned long postTxIdleStart = 0;

// RTCアライン管理
bool          rtcAlignActive = false;
unsigned long rtcResyncMs    = 0;
const unsigned long RTC_RESYNC_INTERVAL = 60000UL;  // 1分ごとに補正

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
  if (LOG_LEVEL >= LOG_INF) {
    Serial.print(F("[PTT] ")); Serial.println(on ? F("ON") : F("OFF"));
  }
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

/* ============== RTC アライン: 次回発火 millis() を計算 ============
 *
 * 現在時刻(RtcTime)と周期(分)から、次の正時アライン時刻を返す。
 *
 * 例: 周期15分, 現在 14:23:45
 *   アライン時刻 = :00, :15, :30, :45
 *   次は 14:30:00 → 残り 6分15秒 = 375s → now + 375000ms
 *
 * 現在がちょうどスロット境界（slotSec==0）の場合は1周期後に設定する
 * （送出直後の再計算でループしないよう）。
 * ================================================================= */
unsigned long calcNextAlignedAt(const RtcTime &t, uint32_t pMin, unsigned long now) {
  uint32_t totalSec  = (uint32_t)t.hour * 3600UL
                     + (uint32_t)t.min  * 60UL
                     + (uint32_t)t.sec;
  uint32_t periodSec = pMin * 60UL;
  uint32_t slotSec   = totalSec % periodSec;
  uint32_t waitSec   = (slotSec == 0) ? periodSec : (periodSec - slotSec);
  return now + (unsigned long)waitSec * 1000UL;
}

/* ============================= Defaults / EEPROM ================== */
// DS3231時刻を秒換算（タイムスタンプ用、日付は考慮せず時分秒のみ）
// 日付を含めた厳密な比較は不要なため、時分秒の日内秒数で十分
uint32_t getCurrentTimestamp() {
  if (!rtcAvailable) return 0;
  RtcTime t;
  if (!rtcRead(t)) return 0;
  return (uint32_t)t.hour * 3600UL + (uint32_t)t.min * 60UL + (uint32_t)t.sec;
}

void applyDefaults() {
  BUSY_INPUT_SOURCE         = BUSY_SRC_DIGITAL;
  SUPPRESSORS_ENABLED       = true;
  TX_AFTER_SUPPRESS_ENABLED = true;
  BUSY_MIN_MS   = 500;
  BUSY_MAX_MS   = 3900;
  IDLE_MIN_MS   = 200;
  PERIOD_MS     = 30UL * 60UL * 1000UL;
  TX_SUP_MS     = 3000;
  A0_LOW_TH     = 300; A0_HIGH_TH = 700; A0_HOLD = 800;
  AUTO_WINDOW   = 30UL * 60UL * 1000UL;
  LONG_TALK_MS  = BUSY_MAX_MS;
  DFP_TIMEOUT_MS     = 20000;
  TMBUSY_ACTIVE_HIGH = true;
  PERIOD_QUIET_MS    = 2000;
  RTC_ALIGN_ON       = true;
  autoLocked = false;

  config.magic            = CONFIG_MAGIC;
  config.busySrc          = (uint8_t)BUSY_INPUT_SOURCE;
  config.suppressOn       = 1;
  config.txAfSupOn        = 1;
  config.busyMin          = BUSY_MIN_MS;
  config.busyMax          = BUSY_MAX_MS;
  config.idleMin          = IDLE_MIN_MS;
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

void saveSettings() {
  config.magic            = CONFIG_MAGIC;
  config.busySrc          = (uint8_t)BUSY_INPUT_SOURCE;
  config.suppressOn       = SUPPRESSORS_ENABLED       ? 1 : 0;
  config.txAfSupOn        = TX_AFTER_SUPPRESS_ENABLED ? 1 : 0;
  config.busyMin          = BUSY_MIN_MS;
  config.busyMax          = BUSY_MAX_MS;
  config.idleMin          = IDLE_MIN_MS;
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

  // 内蔵EEPROM に保存
  EEPROM.put(0, config);

  // AT24C32 にも保存（接続時のみ）
  if (extEepromAvailable) {
    if (!extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config))) {
      if (LOG_LEVEL >= LOG_ERR) Serial.println(F("[EEPROM] AT24C32 write failed."));
    }
  }

  if (LOG_LEVEL >= LOG_INF) Serial.println(F("[EEPROM] Settings Saved."));
}

// 設定構造体のマイグレーション（旧バージョン補完）
static void migrateConfig(MyConfig &c) {
  if (c.ver < 5) { c.rtcAlignOn = 1; }
  if (c.ver < 6) { c.saveTimestamp = 0; }
  if (!(c.tmBusyActiveHigh == 0 || c.tmBusyActiveHigh == 1)) c.tmBusyActiveHigh = 1;
  if (c.dfpTimeoutMs > 600000UL)                              c.dfpTimeoutMs = 20000UL;
  if (c.periodQuietMs == 0 || c.periodQuietMs > 600000UL)    c.periodQuietMs = 2000UL;
  c.ver = CONFIG_VERSION;
}

// ランタイム変数に config を適用
static void applyConfig() {
  BUSY_INPUT_SOURCE         = (BusySrc)config.busySrc;
  SUPPRESSORS_ENABLED       = (config.suppressOn == 1);
  TX_AFTER_SUPPRESS_ENABLED = (config.txAfSupOn  == 1);
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
  RTC_ALIGN_ON              = (config.rtcAlignOn == 1);
  autoLocked                = (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO);
}

void migrateOrInit() {
  MyConfig intCfg, extCfg;
  bool intOk = false, extOk = false;

  // 内蔵EEPROM 読み込み
  EEPROM.get(0, intCfg);
  intOk = (intCfg.magic == CONFIG_MAGIC);

  // AT24C32 読み込み（接続時のみ）
  if (extEepromAvailable) {
    extEepromRead(EXT_EEPROM_ADDR, (uint8_t*)&extCfg, sizeof(extCfg));
    extOk = (extCfg.magic == CONFIG_MAGIC);
  }

  // ─── どちらも無効 → デフォルト初期化 ───
  if (!intOk && !extOk) {
    Serial.println(F("[EEPROM] No data. Init defaults."));
    applyDefaults();
    EEPROM.put(0, config);
    if (extEepromAvailable)
      extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config));
    return;
  }

  // ─── 内蔵のみ有効 ───
  if (intOk && !extOk) {
    Serial.println(F("[EEPROM] AT24C32 not found. Using internal EEPROM."));
    config = intCfg;
    if (config.ver != CONFIG_VERSION) {
      migrateConfig(config);
      EEPROM.put(0, config);
      Serial.println(F("[EEPROM] Migration done."));
    }
    applyConfig();
    Serial.println(F("[EEPROM] Settings Loaded."));
    return;
  }

  // ─── AT24C32のみ有効 ───
  if (!intOk && extOk) {
    Serial.println(F("[EEPROM] Using AT24C32 (internal EEPROM empty)."));
    config = extCfg;
    if (config.ver != CONFIG_VERSION) {
      migrateConfig(config);
      extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config));
      Serial.println(F("[EEPROM] Migration done."));
    }
    EEPROM.put(0, config);  // 内蔵にも同期
    applyConfig();
    Serial.println(F("[EEPROM] Settings Loaded."));
    return;
  }

  // ─── 両方有効 → タイムスタンプで比較 ───
  // マイグレーションが必要な場合は先に補完してタイムスタンプを有効化
  if (intCfg.ver != CONFIG_VERSION) migrateConfig(intCfg);
  if (extCfg.ver != CONFIG_VERSION) migrateConfig(extCfg);

  if (intCfg.saveTimestamp != extCfg.saveTimestamp) {
    // 不一致：AT24C32を優先（可搬性設計）
    Serial.println(F("[EEPROM] WARNING: AT24C32 and internal EEPROM mismatch."));
    Serial.println(F("[EEPROM] Using AT24C32. Re-save settings to sync."));
    config = extCfg;
  } else {
    // 一致：AT24C32を使用
    Serial.println(F("[EEPROM] AT24C32 found. Using external EEPROM."));
    config = extCfg;
  }

  EEPROM.put(0, config);  // 内蔵を AT24C32 に同期
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
  if (t.day   < 10) Serial.print('0'); Serial.print(t.day);   Serial.print(' ');
  if (t.hour  < 10) Serial.print('0'); Serial.print(t.hour);  Serial.print(':');
  if (t.min   < 10) Serial.print('0'); Serial.print(t.min);   Serial.print(':');
  if (t.sec   < 10) Serial.print('0'); Serial.print(t.sec);
}

void printSummary() {
  Serial.print(F("[RTC] "));
  printRtcTime();
  Serial.print(F("  Align="));
  Serial.println(rtcAlignActive ? F("ON(RTC)") : F("OFF(millis)"));

  Serial.print(F("[CFG] EEPROM_VER=")); Serial.print(config.ver);
  Serial.print(F(" SRC="));
  if (BUSY_INPUT_SOURCE == BUSY_SRC_AUTO) Serial.print(F("AUTO"));
  else Serial.print(BUSY_INPUT_SOURCE == BUSY_SRC_DIGITAL ? F("DIGITAL") : F("A0"));
  if (autoLocked) Serial.print(F("(LOCK)"));
  Serial.print(F(" MIN="));          Serial.print(BUSY_MIN_MS);
  Serial.print(F(" MAX="));          Serial.print(BUSY_MAX_MS);
  Serial.print(F(" PER(min)="));     Serial.print(PERIOD_MS / 60000UL);
  Serial.print(F(" TXSUP="));        Serial.print(TX_SUP_MS);
  Serial.print(F(" A0[L/H]="));      Serial.print(A0_LOW_TH); Serial.print('/'); Serial.print(A0_HIGH_TH);
  Serial.print(F(" HOLD="));         Serial.print(A0_HOLD);
  Serial.print(F(" AUTO(min)="));    Serial.print(AUTO_WINDOW / 60000UL);
  Serial.print(F(" DFP_TIMEOUT(ms)=")); Serial.print(DFP_TIMEOUT_MS);
  Serial.print(F(" TM_BUSY_POL=")); Serial.print(TMBUSY_ACTIVE_HIGH ? F("HIGH=busy") : F("LOW=busy"));
  Serial.print(F(" QUIET(ms)="));   Serial.print(PERIOD_QUIET_MS);
  Serial.print(F(" RTC_ALIGN="));   Serial.println(RTC_ALIGN_ON ? F("ON") : F("OFF"));
}

void printHelp() {
  Serial.println(F("---- HELP (v2.22) ----"));
  Serial.println(F("m0=DIGITAL(D11) m1=A0 m2=AUTO"));
  Serial.println(F("n###=busyMin(ms) b####=busyMax(ms) i###=idleMin(ms)"));
  Serial.println(F("s0/1=suppress OFF/ON  t0/1=txAfSup OFF/ON  r####=txSupMs"));
  Serial.println(F("p##=period(min,0=stop) ※10/15/30等(60の約数)→RTC正時アライン"));
  Serial.println(F("k####=periodicQuiet(ms)"));
  Serial.println(F("L###/G###/a####=A0 lo/hi/hold  w##=AUTO(min)"));
  Serial.println(F("d####=DFP timeout(ms,0=off)  g0/g1=TM BUSY polarity"));
  Serial.println(F("u0/u1=RTCアライン OFF/ON"));
  Serial.println(F("TYYYYMMDDHHmmss=RTC時刻設定  例:T20260311143000"));
  Serial.println(F("v=ログ表示(直近20件)  v0=ログ消去"));
  Serial.println(F("q=summary  x=STOP  R=RESUME  H=preset  F=factory  0..3=log  h=help"));
}

/* ============== RTC アライン: nextPeriodicAt を再計算 =============
 * 条件を満たす場合に rtcAlignActive=true、nextPeriodicAt を更新。
 * 条件不成立時は rtcAlignActive=false のまま（millis相対に委ねる）。
 * ================================================================= */
void recalcPeriodicAlign(unsigned long now) {
  uint32_t pMin = PERIOD_MS / 60000UL;

  if (!RTC_ALIGN_ON || !rtcAvailable || !isAlignablePeriod(pMin)) {
    rtcAlignActive = false;
    return;
  }

  RtcTime t;
  if (!rtcRead(t)) {
    rtcAlignActive = false;
    if (LOG_LEVEL >= LOG_ERR) Serial.println(F("[RTC] Read error -> millis fallback"));
    return;
  }

  rtcAlignActive = true;
  nextPeriodicAt = calcNextAlignedAt(t, pMin, now);

  if (LOG_LEVEL >= LOG_INF) {
    unsigned long waitMs = nextPeriodicAt - now;
    Serial.print(F("[RTC] Next aligned in "));
    Serial.print(waitMs / 60000UL); Serial.print(F("m"));
    Serial.print((waitMs % 60000UL) / 1000UL); Serial.println(F("s"));
  }
}

/* ========================= AUTO Fix =============================== */
void maybeAuto(unsigned long now) {
  if (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO || autoLocked) return;
  if ((long)(now - windowStartTS) >= (long)AUTO_WINDOW) {
    BusySrc n;
    if      (dig_edge_count < 10 && a0_event_count >= 20) n = BUSY_SRC_A0;
    else if (a0_event_count < 20 && dig_edge_count >= 10) n = BUSY_SRC_DIGITAL;
    else n = (dig_edge_count >= a0_event_count) ? BUSY_SRC_DIGITAL : BUSY_SRC_A0;
    BUSY_INPUT_SOURCE = n; autoLocked = true;
    autoSwitchBlinkUntil = now + 3000; saveSettings();
    Serial.print(F("[AUTO-FIXED] Lock to "));
    Serial.println(n == BUSY_SRC_DIGITAL ? F("DIGITAL") : F("A0"));
  }
}

/* ========================= Command Parser ========================= */
void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == 'q') { printSummary(); continue; }
    if (c == 'h') { printHelp();   continue; }
    if (c == 'v') {
      // 次の文字を確認（v0=消去、v単独=表示）
      unsigned long t0 = millis() + 100;
      while (!Serial.available() && millis() < t0) {}
      if (Serial.available() && Serial.peek() == '0') {
        Serial.read(); logClear();
      } else {
        logPrint();
      }
      continue;
    }
    if (c == 'x') { stopped = true; Serial.println(F("[STOP]")); continue; }
    if (c == 'R') { continue; }
    if (c == 'H') {
      SUPPRESSORS_ENABLED = false; TX_AFTER_SUPPRESS_ENABLED = false;
      BUSY_MAX_MS = 3900; LONG_TALK_MS = 3900;
      saveSettings(); printSummary(); continue;
    }
    if (c == 'F') {
      applyDefaults(); saveSettings();
      Serial.println(F("[RESET] Factory defaults restored."));
      unsigned long now = millis();
      if (PERIOD_MS > 0) { nextPeriodicAt = now + PERIOD_MS; recalcPeriodicAlign(now); }
      continue;
    }
    if (c >= '0' && c <= '3') { LOG_LEVEL = (LogLvl)(c - '0'); continue; }

    // ---- T コマンド: 時刻設定 TYYYYMMDDHHmmss ----
    if (c == 'T') {
      char buf[15]; memset(buf, 0, sizeof(buf));
      unsigned long tOut = millis() + 2000;
      uint8_t idx = 0;
      while (idx < 14 && millis() < tOut) {
        if (Serial.available()) buf[idx++] = Serial.read();
      }
      if (idx < 14) {
        Serial.println(F("[RTC] T: timeout (need TYYYYMMDDHHmmss, 14 digits)"));
        continue;
      }
      // パース
      RtcTime t;
      t.year  = (uint16_t)((buf[0]-'0')*1000 + (buf[1]-'0')*100
                          + (buf[2]-'0')*10   + (buf[3]-'0'));
      t.month = (buf[4]-'0')*10 + (buf[5]-'0');
      t.day   = (buf[6]-'0')*10 + (buf[7]-'0');
      t.hour  = (buf[8]-'0')*10 + (buf[9]-'0');
      t.min   = (buf[10]-'0')*10 + (buf[11]-'0');
      t.sec   = (buf[12]-'0')*10 + (buf[13]-'0');

      if (t.year < 2000 || t.year > 2099 ||
          t.month < 1   || t.month > 12  ||
          t.day   < 1   || t.day   > 31  ||
          t.hour  > 23  || t.min   > 59  || t.sec   > 59) {
        Serial.println(F("[RTC] T: invalid datetime"));
        continue;
      }
      if (!rtcAvailable) {
        Serial.println(F("[RTC] T: DS3231 not found"));
        continue;
      }
      if (rtcWrite(t)) {
        Serial.print(F("[RTC] Time set OK: ")); printRtcTime(); Serial.println();
        recalcPeriodicAlign(millis());
      } else {
        Serial.println(F("[RTC] T: write failed"));
      }
      continue;
    }

    // ---- 数値が必要なコマンド ----
    unsigned long timeout = millis() + 150;
    while (!Serial.available() && millis() < timeout) {}
    long val = Serial.parseInt();
    bool chg = true;

    switch (c) {
      case 'm':
        if      (val == 0) { BUSY_INPUT_SOURCE = BUSY_SRC_DIGITAL; autoLocked = true; }
        else if (val == 1) { BUSY_INPUT_SOURCE = BUSY_SRC_A0;      autoLocked = true; }
        else if (val == 2) {
          BUSY_INPUT_SOURCE = BUSY_SRC_AUTO; autoLocked = false;
          windowStartTS = millis(); dig_edge_count = 0; a0_event_count = 0;
        } else chg = false;
        break;

      case 'b': if (val >= 500) { BUSY_MAX_MS=(unsigned long)val; LONG_TALK_MS=BUSY_MAX_MS; } else chg=false; break;
      case 'n': if (val >= 100) BUSY_MIN_MS=(unsigned long)val; else chg=false; break;
      case 'i': if (val >= 0)   IDLE_MIN_MS=(unsigned long)val; else chg=false; break;
      case 's': if (val==0||val==1) SUPPRESSORS_ENABLED=(val==1); else chg=false; break;
      case 't': if (val==0||val==1) TX_AFTER_SUPPRESS_ENABLED=(val==1); else chg=false; break;
      case 'r': if (val >= 0)   TX_SUP_MS=(unsigned long)val; else chg=false; break;

      case 'p':
        if (val >= 0) {
          PERIOD_MS = (unsigned long)val * 60UL * 1000UL;
          unsigned long now = millis();
          if (PERIOD_MS > 0) {
            nextPeriodicAt = now + PERIOD_MS;  // 仮設定
            recalcPeriodicAlign(now);          // RTC可能ならアライン上書き
          } else {
            periodicDue = false;
            rtcAlignActive = false;
          }
        } else chg = false;
        break;

      case 'k': if (val>=0 && val<=600000) PERIOD_QUIET_MS=(unsigned long)val; else chg=false; break;
      case 'L': A0_LOW_TH  = (int)val; break;
      case 'G': A0_HIGH_TH = (int)val; break;
      case 'a': if (val >= 0) A0_HOLD=(unsigned long)val; else chg=false; break;

      case 'w':
        if (val >= 1) {
          AUTO_WINDOW=(unsigned long)val*60UL*1000UL;
          windowStartTS=millis(); dig_edge_count=0; a0_event_count=0;
        } else chg=false;
        break;

      case 'd': if (val>=0 && val<=600000) DFP_TIMEOUT_MS=(unsigned long)val; else chg=false; break;
      case 'g': if(val==0) TMBUSY_ACTIVE_HIGH=false; else if(val==1) TMBUSY_ACTIVE_HIGH=true; else chg=false; break;

      case 'u':
        if (val == 0 || val == 1) {
          RTC_ALIGN_ON = (val == 1);
          unsigned long now = millis();
          if (RTC_ALIGN_ON && PERIOD_MS > 0) recalcPeriodicAlign(now);
          else rtcAlignActive = false;
        } else chg = false;
        break;

      default: chg = false; break;
    }

    if (chg) { saveSettings(); printSummary(); }
  }
}

/* =========================== setup / loop ========================= */
void setup() {
  Serial.begin(115200);
  dfpSerial.begin(9600);
  Wire.begin();

  // DS3231 接続確認（migrateOrInit より先に行う）
  rtcAvailable = rtcProbe();
  if (rtcAvailable) {
    Serial.println(F("[RTC] DS3231 found."));
  } else {
    Serial.println(F("[RTC] DS3231 not found -> millis() fallback."));
  }

  // AT24C32 接続確認（migrateOrInit より先に行う）
  extEepromAvailable = extEepromProbe();

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
  dfpSend(0x06, 20);

  unsigned long now = millis();
  lastBusyOffAt = now;
  windowStartTS  = now;

  // 周期タイマー初期化
  if (PERIOD_MS > 0) {
    nextPeriodicAt = now + PERIOD_MS;
    recalcPeriodicAlign(now);
  }
  rtcResyncMs = now + RTC_RESYNC_INTERVAL;

  Serial.println(F("[START] OpenCCVoice v2.22 (Fix TX/RX, Event Log, Dual EEPROM, RTC-Aligned Periodic, Ver.5 pinmap)"));
  printSummary();
  printHelp();

  // ログ初期化・起動イベント記録
  if (extEepromAvailable) {
    logInit();
    logWrite(LOG_EVT_BOT, (uint32_t)CONFIG_VERSION);
  }
}

void loop() {
  unsigned long now = millis();

  // STOP 中: R で復帰
  if (stopped) {
    setPtt(false);
    digitalWrite(PIN_BUSY_LED, LOW);
    digitalWrite(PIN_SUP_LED,  LOW);
    digitalWrite(PIN_A0_LED,   LOW);
    digitalWrite(PIN_DFP_OUT,  LOW);
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'R') { stopped = false; Serial.println(F("[RESUME]")); }
      else if (c == 'h') printHelp();
      else if (c == 'q') printSummary();
    }
    delay(5);
    return;
  }

  handleSerialCmd();

  bool pAct = (state != IDLE) || ((long)now < (long)pttMinOn);

  // DFPlayer BUSY(D10) → D3 ミラー（反転）
  bool dfpPlaying = (digitalRead(PIN_DFP_BSY) == LOW);
  digitalWrite(PIN_DFP_OUT, DFP_MIRROR_INVERT ? (dfpPlaying ? HIGH : LOW)
                                              : (dfpPlaying ? LOW  : HIGH));

  // Digital 入力(D11) デバウンス
  bool rTm = readTmDigital();
  if (rTm != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) {
    tmBusyFiltered = rTm; tmDebounceTS = now;
    if (tmBusyFiltered) dig_edge_count++;
  }

  // A0 判定
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
    a0Detect = false; a0Busy = false;
  }

  bool rB = readBusy();

  // 送信後ガード解除
  if (postTxIgnore) {
    if (!rB) {
      if (postTxIdleStart == 0) postTxIdleStart = now;
      if (now - postTxIdleStart >= IDLE_MIN_MS) {
        postTxIgnore = false; postTxIdleStart = 0;
        if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[POST-TX] cleared"));
      }
    } else {
      postTxIdleStart = 0;
    }
  }

  // BUSY→OFF の瞬間で静寂起点更新
  if (!rB && tmBusyPrev) lastBusyOffAt = now;
  tmBusyPrev = rB;

  // LED 表示
  digitalWrite(PIN_BUSY_LED, rB     ? HIGH : LOW);
  digitalWrite(PIN_A0_LED,   a0Busy ? HIGH : LOW);
  bool sup = isSuppressedNow(now);
  digitalWrite(PIN_SUP_LED, sup ? HIGH : LOW);

  // 短発（Track 1 = カーチャンク ID）
  if (!pAct && !sup) {
    if (rB) {
      if (tmBusyStart == 0) tmBusyStart = now;
    } else {
      if (tmBusyStart != 0) {
        unsigned long dur = now - tmBusyStart; tmBusyStart = 0;

        if (SUPPRESSORS_ENABLED) {
          if (dur >= BUSY_MAX_MS) {
            longSupUntil = now + LONG_SUP_MS;
            logWrite(LOG_EVT_SUP, dur);  // 長話抑止ログ
          }
          if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
            if (burstWinStart == 0 || (now - burstWinStart >= BURST_WIN_MS)) {
              burstWinStart = now; burstCount = 0;
            }
            if (++burstCount >= BURST_TH) {
              burstSupUntil = now + BURST_SUP_MS;
              logWrite(LOG_EVT_BST, (uint32_t)burstCount);  // バースト抑止ログ
            }
          }
        }

        bool allowed = !(SUPPRESSORS_ENABLED &&
                        ((long)(now - longSupUntil)  < 0 ||
                         (long)(now - burstSupUntil) < 0));
        if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS &&
            (now - lastTriggerAt >= REFRAC_MS) && allowed) {
          logWrite(LOG_EVT_CAR, dur);  // カーチャンクログ
          startPtt(1); lastTriggerAt = now;
        }
      }
    }
  } else { tmBusyStart = 0; }

  // ================================================================
  // 周期 ID スケジューラ
  //   RTCアライン有効: nextPeriodicAt は実時刻ベース、1分ごとに補正
  //   millis 相対    : catch-up 方式（v2.01 互換）
  // ================================================================
  if (PERIOD_MS > 0) {
    if (rtcAlignActive) {
      // RTC アライン動作
      if ((long)(now - nextPeriodicAt) >= 0) {
        periodicDue = true;
        recalcPeriodicAlign(now);  // 次回時刻を再計算
        if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[Periodic] RTC due"));
      }
      // 1分ごとのドリフト補正
      if ((long)(now - rtcResyncMs) >= 0) {
        rtcResyncMs = now + RTC_RESYNC_INTERVAL;
        recalcPeriodicAlign(now);
        if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[RTC] Resync done"));
      }
    } else {
      // millis 相対 catch-up（v2.01 互換）
      bool crossed = false;
      while ((long)(now - nextPeriodicAt) >= 0) {
        nextPeriodicAt += PERIOD_MS;
        crossed = true;
      }
      if (crossed) {
        periodicDue = true;
        if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[Periodic] due (millis)"));
      }
    }
  } else {
    periodicDue = false;
  }

  // 周期 ID 送出（Quiet Guard）
  if (periodicDue && state == IDLE) {
    bool quietOK = (!rB) && ((long)(now - lastBusyOffAt) >= (long)PERIOD_QUIET_MS);
    bool guardOK = !isSuppressedNow(now);
    if (quietOK && guardOK) {
      if (LOG_LEVEL >= LOG_INF) {
        Serial.print(F("[EVT] Periodic Track"));
        Serial.print(nextPeriodicTrack);
        if (rtcAlignActive) { Serial.print(F(" @")); printRtcTime(); }
        Serial.println();
      }
      logWrite(LOG_EVT_PER, (uint32_t)nextPeriodicTrack);  // 周期IDログ
      startPtt(nextPeriodicTrack);
      nextPeriodicTrack = (nextPeriodicTrack == 2 ? 3 : 2);
      periodicDue = false;
    } else {
      if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[Periodic] deferred"));
    }
  }

  // ステートマシン
  switch (state) {
    case PTT_ON_WAIT:
      if (now - stateTimer >= PTT_PRE_MS) {
        if (requestedTrack) { dfpSend(0x03, requestedTrack); requestedTrack = 0; }
        state = PLAYING; playingEnterAt = now;
      }
      break;

    case PLAYING:
      if (digitalRead(PIN_DFP_BSY) == LOW) {
        dfpStarted = true; busyHighSince = 0;
      } else {
        if (dfpStarted) {
          if (busyHighSince == 0) busyHighSince = now;
          if (now - busyHighSince >= 40) { state = PTT_OFF_WAIT; stateTimer = now; }
        }
      }
      if (DFP_TIMEOUT_MS > 0 && playingEnterAt > 0 &&
          (now - playingEnterAt >= DFP_TIMEOUT_MS)) {
        if (LOG_LEVEL >= LOG_ERR) Serial.println(F("[ERR] DFP Timeout -> Force PTT OFF"));
        state = PTT_OFF_WAIT; stateTimer = now;
      }
      break;

    case PTT_OFF_WAIT:
      if (now - stateTimer >= PTT_POST_MS && now >= pttMinOn) {
        setPtt(false);
        state = IDLE;
        postTxIgnore = true; postTxIdleStart = 0;
        lastTriggerAt = now;
        if (SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED)
          busySupUntil = now + TX_SUP_MS;
      }
      break;

    case IDLE: default: break;
  }

  // 最低 ON ガード
  if ((long)now < (long)pttMinOn) setPtt(true);

  // テストSW（1〜3クリック = Track 1〜3）
  bool sw = digitalRead(PIN_TEST_SW);
  if (sw == LOW && lastSwState == HIGH) {
    if (!clickWaiting) { clickWaiting = true; clickCount = 1; firstClickTime = now; }
    else clickCount++;
  }
  lastSwState = sw;
  if (clickWaiting && (now - firstClickTime >= 1000)) {
    if (state == IDLE && clickCount >= 1 && clickCount <= 3) startPtt(clickCount);
    clickWaiting = false; clickCount = 0;
  }

  // AUTO 固定
  maybeAuto(now);
}
