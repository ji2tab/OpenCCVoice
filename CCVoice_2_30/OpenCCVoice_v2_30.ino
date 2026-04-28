/************************************************************
 * OpenCCVoice Guidance Controller  (v1.80i Core + v2.24f Extended)
 * Version : 2.30 (v1.80i improvements integrated / v2.24f RTC・ログ統合)
 * Target  : Arduino Nano (ATmega328P, 5V)
 *
 * 【v2.30 の特徴】
 * ★ v1.80i の3つの革新を完全搭載:
 *   1) 絶対時刻比較（pttPreEndAt/pttPostEndAt）
 *   2) 抑止起点統一（POST終了・受信終了を busySupUntil で管理）
 *   3) ブロッキング対策（Serial.setTimeout(50)）
 * ★ v2.24f の RTC・ログ・AUTO機能を統合
 * ★ Ver.5 ピンマップ完全踏襲（ハードウェア変更なし）
 *
 * 【v2.30 変更点（v2.24f → v2.30）】
 * 
 * ★ v1.80i の改善ロジックを100%搭載・継承:
 * 
 *   [抑止ロジック刷新 - v1.80i で確立]
 *   - カーチャンク検出時の busySupUntil セットを削除
 *     v1.80i では processBusyLogic() と processStateMachine() での
 *     二重セット問題を解決。抑止セットを POST終了（起点A）に一本化。
 *   - 起点A：POST終了時 + BUSY OFF
 *   - 起点B：通常会話終了時（② MAX超え受信）
 *   - v1.80i で確立→v2.30で継承（ロジック変更なし）
 * 
 *   [PTT制御の改善 - v1.80h/v1.80i で確立]
 *   - Serial.setTimeout(50) を setup() で一度だけ設定
 *     v1.80h で導入・v1.80i で確定。parseInt() の最大ブロッキングを制限
 *   - PTT_ON_WAIT / PTT_OFF_WAIT の待機判定を相対時間比較から
 *     絶対時刻比較（pttPreEndAt / pttPostEndAt）に変更
 *   - v1.80i で完全確立→v2.30で継承（ロジック変更なし）
 * 
 *   [コマンド体系の整理 - v1.80i 基盤・v2.30 完成]
 *   - ログレベル形式を l0/l1/l2/l3 で統一（v1.80i 確立）
 *   - v2.30 で l4 (LOG_DBG) を追加
 *   - j/J コマンド削除（PRE/POST は内部固定 1000ms）
 *     理由：v1.80i の全ユーザーが1000ms使用実績
 *   - 削除: i####（idleMin は内部固定 200ms）
 *   - 削除: s0/s1, t0/t1（v1.80i で抑止ロジック統合に伴い不要）
 *   - 削除: h（ヘルプ表示は廃止、V/q で確認可能）
 * 
 *   [EEPROM マイグレーション - v1.80i(ver=5)→v2.30(ver=7)]
 *   - CONFIG_VERSION: 5(v1.80i) → 7(v2.30)
 *   - ver=6（v2.24f）からの自動マイグレーション対応
 *   - pttPreMs / pttPostMs フィールドを削除（固定 1000ms）
 *   - suppressOn / txAfSupOn / idleMin フィールドを削除
 *   - rtcAlignOn / saveTimestamp フィールドを新規追加
 * 
 *   [v1.80i で確立された仕様・v2.30で継続]
 *   - デフォルト BUSY_MAX_MS = 3900（v2.24f 継続）
 *   - デフォルト TX_SUP_MS = 3000（v2.24f 継続）
 *     ※ v1.80i は BUSY_MAX=1500, TX_SUP=10000 がデフォルト
 *   - デフォルト BUSY_MIN = 500（v1.80i と同一）
 * 
 *   [バグ修正・コード整理]
 *   - v1.80i の改善を継承・安定化
 *   - マイグレーション対応の強化
 *     明確に分離、コメント修正
 *   - pAct=true 時の② 超え受信も起点B になる旨をコメントに明記
 *   - handleStoppedState() → loop() 内に統合（デッドコード削除）
 * 
 *   [周期ID・RTC]
 *   - 既存の RTC アライン機能は継続
 *   - recalcPeriodicAlign() / calcNextAlignedAt() は v2.24f から継続
 *   - 1分ごとドリフト補正（RTC_RESYNC_INTERVAL）も継続
 * 
 *   [LED表示・ピンマップ]
 *   - ピンマップは Ver.5 で変更なし
 *   - LED表示ロジック（D3/D4/D6 ミラーとモード表示）は継続
 * 
 * 【v2.24f からの継続機能】
 * - DS3231 RTC モジュール対応（I²C A4/A5）
 * - AT24C32 外部EEPROM 二重保存＋タイムスタンプ判定
 * - イベントログ記録（AT24C32 リングバッファ、448件）
 * - T コマンド（RTC時刻設定）
 * - u0/u1 コマンド（RTCアライン OFF/ON）
 * - v / v0 コマンド（ログ表示・消去）
 * - AUTO判定（m2）
 * - V / Z コマンド
 * 
 * 【操作（115200 8N1, 改行なし/LF）】
 *  n####      ① 最小受信長(ms)  デフォルト:500
 *  b####      ② 最大受信長(ms)  デフォルト:3900
 *  r####      ⑤ 抑止時間(ms)    デフォルト:3000
 *  p##        ⑥ 定周期ID（分、0=停止）
 *  k####      定周期送出前の静寂待機時間(ms)
 *  m0/m1/m2   BUSYソース（D11デジタル/A0/AUTO）
 *  g0/g1      BUSY極性（LOW=busy / HIGH=busy）
 *  L###       A0閾値LOW
 *  G###       A0閾値HIGH
 *  a####      A0保持時間(ms)
 *  w##        AUTO観測時間（分）
 *  d####      DFPフェイルセーフ(ms、0=無効)
 *  u0/u1      RTCアライン OFF/ON
 *  TYYYYMMDDHHmmss RTC時刻設定（例: T20260428143000）
 *  v          ログ表示（直近20件）
 *  v0         ログ消去
 *  V          バージョン表示
 *  q          設定一覧（全パラメータ表示）
 *  x          Safe Stop（全機能停止）
 *  R          Safe Stopからの復帰
 *  Z          ソフトウェアリセット（WDT、EEPROM変更なし）
 *  F          Factory Reset（EEPROM初期化＋保存）
 *  l0/l1/l2/l3  ログレベル（OFF/MIN/FULL/DBG）起動時はl1固定
 *  h          コマンド一覧
 * 
 * 【Ver.5 ピンマップ】
 *  DF Player  : D3   (DFP BUSYミラー出力：反転)
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
enum LogLvl  { LOG_OFF=0, LOG_ERR=1, LOG_MIN=2, LOG_FULL=3, LOG_DBG=4 };
LogLvl LOG_LEVEL = LOG_MIN;

/* ============================== EEPROM ============================
 * CONFIG_VERSION 変更履歴:
 *   ver=4 : v1.73d/e, v1.74/v2.x初期 (periodQuietMs 追加)
 *   ver=5 : v1.80h/v2.10 (pttPreMs/pttPostMs 追加)
 *   ver=6 : v2.20-v2.24f (rtcAlignOn/saveTimestamp 追加)
 *   ver=7 : v2.30 (PRE/POST固定化、suppressOn/txAfSupOn削除)
 * 
 * v2.30 (ver=7):
 * - pttPreMs / pttPostMs削除 → 固定 1000ms
 * - suppressOn / txAfSupOn削除 → 抑止ロジック統合
 * - idleMin削除 → 内部固定 200ms
 * ================================================================= */
struct MyConfig {
  uint32_t magic;
  uint8_t  busySrc;
  uint32_t busyMin;
  uint32_t busyMax;
  uint32_t txSupMs;
  uint32_t periodMin;
  int      a0Low;
  int      a0High;
  uint32_t a0Hold;
  uint32_t autoWinMin;
  uint32_t dfpTimeoutMs;
  uint8_t  tmBusyActiveHigh;
  uint32_t periodQuietMs;
  uint8_t  rtcAlignOn;
  uint32_t saveTimestamp;
  uint8_t  ver;              // ★末尾固定
} config;

const uint32_t CONFIG_MAGIC   = 0xDEADBEEF;
const uint8_t  CONFIG_VERSION = 7;
const uint16_t EXT_EEPROM_ADDR = 0x0000;

/* =========================== Runtime Params =========================== */
enum BusySrc BUSY_INPUT_SOURCE;

bool SUPPRESSORS_ENABLED;
bool TX_AFTER_SUPPRESS_ENABLED;

unsigned long BUSY_MIN_MS;
unsigned long BUSY_MAX_MS;
unsigned long PERIOD_MS;
unsigned long TX_SUP_MS;

int A0_LOW_TH;
int A0_HIGH_TH;
unsigned long A0_HOLD;
unsigned long AUTO_WINDOW;
unsigned long DFP_TIMEOUT_MS;
bool TMBUSY_ACTIVE_HIGH;
unsigned long PERIOD_QUIET_MS;
bool RTC_ALIGN_ON;

// 内部固定定数
const unsigned long IDLE_MIN_MS = 200;
const unsigned long PTT_PRE_MS = 1000;
const unsigned long PTT_POST_MS = 1000;
const unsigned long DEBOUNCE_MS = 5;
const unsigned long REFRAC_MS = 3000;
const unsigned long LONG_SUP_MS = 10000;
const unsigned long BURST_WIN_MS = 10000;
const unsigned int  BURST_TH = 2;
const unsigned long BURST_SUP_MS = 10000;
const bool DFP_MIRROR_INVERT = true;

/* ============================== Pins (Ver.5) ======================== */
const uint8_t PIN_TEST_SW   = 2;   // D2
const uint8_t PIN_DFP_OUT   = 3;   // D3（DFP ミラー反転）
const uint8_t PIN_BUSY_LED  = 4;   // D4
const uint8_t PIN_PTT       = 5;   // D5
const uint8_t PIN_SUP_LED   = 6;   // D6
const uint8_t PIN_A0_LED    = 7;   // D7
const uint8_t PIN_DFP_BSY   = 10;  // D10（DFPlayer BUSY入力）
const uint8_t PIN_TM_BUSY   = 11;  // D11（タイムアウトマン BUSY）
const uint8_t A0_PIN        = A0;  // A0
const uint8_t ARD_RX_FROM_DFP = 12; // D12（RX ← DFPlayer TX）
const uint8_t ARD_TX_TO_DFP   = 13; // D13（TX → DFPlayer RX）
SoftwareSerial dfpSerial(ARD_RX_FROM_DFP, ARD_TX_TO_DFP);

/* ========================= RTC Align Helpers ======================= */
bool isAlignablePeriod(uint32_t pMin) {
  if (pMin == 0 || pMin > 60) return false;
  return (60 % pMin == 0);
}

unsigned long calcNextAlignedAt(const RtcTime &t, uint32_t pMin, unsigned long now) {
  uint32_t totalSec  = (uint32_t)t.hour * 3600UL + (uint32_t)t.min * 60UL + (uint32_t)t.sec;
  uint32_t periodSec = pMin * 60UL;
  uint32_t slotSec   = totalSec % periodSec;
  uint32_t waitSec   = (slotSec == 0) ? periodSec : (periodSec - slotSec);
  return now + (unsigned long)waitSec * 1000UL;
}

/* ============================ State / Vars ============================ */
unsigned long windowStartTS = 0;
unsigned long autoSwitchBlinkUntil = 0;
bool autoLocked = false;

enum State { IDLE, PTT_ON_WAIT, PLAYING, PTT_OFF_WAIT };
State state = IDLE;

// BUSY検出
unsigned long tmBusyStart = 0, tmDebounceTS = 0, a0LastSignalTS = 0;
bool tmBusyPrev = false, tmBusyFiltered = false, a0Detect = false, a0Busy = false;

// PTT制御
unsigned long pttPreEndAt = 0;   // PRE終了予定時刻（絶対値）★v2.30改善
unsigned long pttPostEndAt = 0;  // POST終了予定時刻（絶対値）★v2.30改善
unsigned long pttMinOn = 0;
unsigned long nextPeriodicAt = 0;
bool dfpStarted = false;
bool pttOutState = false;
bool clickWaiting = false;
bool stopped = false;
uint16_t requestedTrack = 0;
uint16_t nextPeriodicTrack = 2;

// 抑止タイマー（★v2.30: 起点統一）
unsigned long busySupUntil = 0;
unsigned long longSupUntil = 0;
unsigned long burstWinStart = 0;
unsigned long burstSupUntil = 0;
unsigned long busyHighSince = 0;
unsigned long playingEnterAt = 0;

uint8_t clickCount = 0;
bool lastSwState = HIGH;
unsigned long firstClickTime = 0;
unsigned int burstCount = 0;
uint16_t dig_edge_count = 0;
uint16_t a0_event_count = 0;

unsigned long lastBusyOffAt = 0;

// 送信後ガード
bool postTxIgnore = false;
unsigned long postTxIdleStart = 0;

// RTCアライン管理
bool rtcAlignActive = false;
unsigned long rtcResyncMs = 0;
const unsigned long RTC_RESYNC_INTERVAL = 60000UL;  // 1分ごと補正

// その他
bool prevSuppressed = false;
unsigned long stateTimer = 0;
bool periodicDue = false;

/* ============================== Utils ============================= */
inline bool readTmRaw() { return (digitalRead(PIN_TM_BUSY) == HIGH); }
inline bool readTmDigital()  { bool rawHigh = readTmRaw(); return TMBUSY_ACTIVE_HIGH ? rawHigh : !rawHigh; }

bool readBusy() {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_DIGITAL) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) return a0Busy;
  return (tmBusyFiltered || a0Busy);  // AUTO
}

void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E,0xFF,0x06,cmd,0x00,(uint8_t)(param>>8),(uint8_t)(param & 0xFF),0x00,0x00,0xEF};
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
  if (LOG_LEVEL >= LOG_MIN) {
    Serial.print(F("[PTT] ")); Serial.println(on ? F("ON") : F("OFF"));
  }
}

void startPtt(uint16_t trk) {
  if (state != IDLE) return;
  requestedTrack = trk;
  dfpStarted = false;
  unsigned long now = millis();
  setPtt(true);
  pttPreEndAt = now + PTT_PRE_MS;  // ★v2.30改善：絶対時刻
  stateTimer = now;
  playingEnterAt = 0;
  state = PTT_ON_WAIT;
}

// ★v2.30: 抑止判定を統一（起点A/B）
static inline bool isSuppressedNow(unsigned long now) {
  if (postTxIgnore) return true;
  if (!SUPPRESSORS_ENABLED) return false;
  if ((long)(now - longSupUntil)  < 0) return true;
  if ((long)(now - burstSupUntil) < 0) return true;
  if ((long)(now - busySupUntil)  < 0) return true;
  return false;
}

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
  
  if (LOG_LEVEL >= LOG_MIN) {
    unsigned long waitMs = nextPeriodicAt - now;
    Serial.print(F("[RTC] Next aligned in "));
    Serial.print(waitMs / 60000UL); Serial.print(F("m"));
    Serial.print((waitMs % 60000UL) / 1000UL); Serial.println(F("s"));
  }
}

uint32_t getCurrentTimestamp() {
  if (!rtcAvailable) return 0;
  RtcTime t;
  if (!rtcRead(t)) return 0;
  return (uint32_t)t.hour * 3600UL + (uint32_t)t.min * 60UL + (uint32_t)t.sec;
}

void applyDefaults() {
  BUSY_INPUT_SOURCE = BUSY_SRC_DIGITAL;
  SUPPRESSORS_ENABLED = true;
  TX_AFTER_SUPPRESS_ENABLED = true;
  
  BUSY_MIN_MS = 500;
  BUSY_MAX_MS = 3900;
  PERIOD_MS = 30UL * 60UL * 1000UL;
  TX_SUP_MS = 3000;
  
  A0_LOW_TH = 300;
  A0_HIGH_TH = 700;
  A0_HOLD = 800;
  AUTO_WINDOW = 30UL * 60UL * 1000UL;
  
  DFP_TIMEOUT_MS = 20000;
  TMBUSY_ACTIVE_HIGH = true;
  PERIOD_QUIET_MS = 2000;
  RTC_ALIGN_ON = true;
  
  autoLocked = false;
  
  config.magic = CONFIG_MAGIC;
  config.busySrc = (uint8_t)BUSY_INPUT_SOURCE;
  config.busyMin = BUSY_MIN_MS;
  config.busyMax = BUSY_MAX_MS;
  config.txSupMs = TX_SUP_MS;
  config.periodMin = PERIOD_MS / 60000UL;
  config.a0Low = A0_LOW_TH;
  config.a0High = A0_HIGH_TH;
  config.a0Hold = A0_HOLD;
  config.autoWinMin = AUTO_WINDOW / 60000UL;
  config.dfpTimeoutMs = DFP_TIMEOUT_MS;
  config.tmBusyActiveHigh = 1;
  config.periodQuietMs = PERIOD_QUIET_MS;
  config.rtcAlignOn = 1;
  config.saveTimestamp = 0;
  config.ver = CONFIG_VERSION;
}

void saveSettings() {
  config.magic = CONFIG_MAGIC;
  config.busySrc = (uint8_t)BUSY_INPUT_SOURCE;
  config.busyMin = BUSY_MIN_MS;
  config.busyMax = BUSY_MAX_MS;
  config.txSupMs = TX_SUP_MS;
  config.periodMin = PERIOD_MS / 60000UL;
  config.a0Low = A0_LOW_TH;
  config.a0High = A0_HIGH_TH;
  config.a0Hold = A0_HOLD;
  config.autoWinMin = AUTO_WINDOW / 60000UL;
  config.dfpTimeoutMs = DFP_TIMEOUT_MS;
  config.tmBusyActiveHigh = TMBUSY_ACTIVE_HIGH ? 1 : 0;
  config.periodQuietMs = PERIOD_QUIET_MS;
  config.rtcAlignOn = RTC_ALIGN_ON ? 1 : 0;
  config.saveTimestamp = getCurrentTimestamp();
  config.ver = CONFIG_VERSION;
  
  // 内蔵EEPROM に保存
  EEPROM.put(0, config);
  
  // AT24C32 にも保存（接続時のみ）
  if (extEepromAvailable) {
    if (!extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config))) {
      if (LOG_LEVEL >= LOG_ERR) Serial.println(F("[EEPROM] AT24C32 write failed."));
    }
  }
  
  if (LOG_LEVEL >= LOG_MIN) Serial.println(F("[EEPROM] Settings Saved."));
}

#include "ccvoice_config.h"

void migrateOrInit() {
  MyConfig intCfg, extCfg;
  bool intOk = false, extOk = false;
  
  EEPROM.get(0, intCfg);
  intOk = (intCfg.magic == CONFIG_MAGIC);
  
  if (extEepromAvailable) {
    extEepromRead(EXT_EEPROM_ADDR, (uint8_t*)&extCfg, sizeof(extCfg));
    extOk = (extCfg.magic == CONFIG_MAGIC);
  }
  
  if (!intOk && !extOk) {
    Serial.println(F("[EEPROM] No data. Init defaults."));
    applyDefaults();
    EEPROM.put(0, config);
    if (extEepromAvailable)
      extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config));
    return;
  }
  
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
  
  if (!intOk && extOk) {
    Serial.println(F("[EEPROM] Using AT24C32 (internal EEPROM empty)."));
    config = extCfg;
    if (config.ver != CONFIG_VERSION) {
      migrateConfig(config);
      extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config));
      Serial.println(F("[EEPROM] Migration done."));
    }
    EEPROM.put(0, config);
    applyConfig();
    Serial.println(F("[EEPROM] Settings Loaded."));
    return;
  }
  
  // ★v2.30改善：両方有効時のマイグレーション処理
  if (intCfg.ver != CONFIG_VERSION) migrateConfig(intCfg);
  if (extCfg.ver != CONFIG_VERSION) migrateConfig(extCfg);
  
  // タイムスタンプで比較 → 新しい方採用
  bool intNewer = (intCfg.saveTimestamp >= extCfg.saveTimestamp);
  config = intNewer ? intCfg : extCfg;
  
  if (config.ver != CONFIG_VERSION) {
    migrateConfig(config);
    EEPROM.put(0, config);
    if (extEepromAvailable)
      extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config));
    Serial.println(F("[EEPROM] Migration done."));
  }
  
  applyConfig();
  Serial.println(F("[EEPROM] Settings Loaded."));
}

/* =========================== Serial Commands ==== ==================
 * ★v2.30: コマンド体系を v1.80i に合わせて整理
 *
 * 削除コマンド:
 *   i#### : IDLE_MIN_MS（内部固定 200ms）
 *   s0/s1 : suppressOn（抑止ロジック統合）
 *   t0/t1 : txAfSupOn（抑止ロジック統合）
 *   j/J   : PTT_PRE_MS / PTT_POST_MS（固定 1000ms）
 * 
 * ログレベルコマンド:
 *   旧: 0/1/2/3
 *   新: l0/l1/l2/l3（v1.80i準拠）
 * 
 * ======================================================================= */
void printSummary() {
  Serial.println(F("[SUMMARY] Current Parameters:"));
  Serial.print(F("  BusyMin="));Serial.print(BUSY_MIN_MS);
  Serial.print(F("ms  BusyMax="));Serial.print(BUSY_MAX_MS);
  Serial.print(F("ms  TxSup="));Serial.print(TX_SUP_MS);Serial.println(F("ms"));
  Serial.print(F("  Period="));Serial.print(PERIOD_MS/60000UL);
  Serial.print(F("min  QuietGuard="));Serial.print(PERIOD_QUIET_MS);Serial.println(F("ms"));
  Serial.print(F("  BusySrc="));
  if (BUSY_INPUT_SOURCE == BUSY_SRC_DIGITAL) Serial.print(F("D11(Digital)"));
  else if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) Serial.print(F("A0(Analog)"));
  else Serial.print(F("AUTO"));
  Serial.print(F("  A0_LOW="));Serial.print(A0_LOW_TH);
  Serial.print(F("  A0_HIGH="));Serial.print(A0_HIGH_TH);
  Serial.print(F("  A0_HOLD="));Serial.println(A0_HOLD);
  Serial.print(F("  AutoWindow="));Serial.print(AUTO_WINDOW/60000UL);
  Serial.print(F("min  DFP_Timeout="));Serial.print(DFP_TIMEOUT_MS);
  Serial.print(F("ms  RTCAlign="));Serial.println(RTC_ALIGN_ON ? F("ON") : F("OFF"));
}

void printHelp() {
  Serial.println(F("[HELP] Commands:"));
  Serial.println(F("  n#### b#### r#### p## k####"));
  Serial.println(F("  m0/m1/m2 g0/g1 L### G### a#### w## d####"));
  Serial.println(F("  u0/u1 TYYYYMMDDHHmmss v v0"));
  Serial.println(F("  V q x R Z F l0/l1/l2/l3 h"));
}

void printRtcTime() {
  if (!rtcAvailable) { Serial.print(F("N/A")); return; }
  RtcTime t;
  if (!rtcRead(t)) { Serial.print(F("ERR")); return; }
  if (t.hour < 10) Serial.print(F("0")); Serial.print(t.hour);
  Serial.print(F(":"));
  if (t.min < 10) Serial.print(F("0")); Serial.print(t.min);
  Serial.print(F(":"));
  if (t.sec < 10) Serial.print(F("0")); Serial.print(t.sec);
}

void handleSerialCmd() {
  if (!Serial.available()) return;
  
  char c = Serial.read();
  
  // 制御コマンド（先に処理）
  switch (c) {
    case 'V':
      Serial.print(F("[VER] v2.30 / CONFIG_VERSION="));
      Serial.println(CONFIG_VERSION);
      return;
    case 'q':
      printSummary();
      return;
    case 'h':
    case 'H':
      printHelp();
      return;
    case 'x':
      stopped = true;
      Serial.println(F("[STOP] All functions disabled. Send R to resume."));
      return;
    case 'R':
      if (stopped) { stopped = false; Serial.println(F("[RESUME]")); }
      return;
    case 'Z':
      Serial.println(F("[RESET] WDT reboot..."));
      delay(100);
      wdt_enable(WDTO_15MS);
      while (1) {}
      return;
    case 'F':
      Serial.println(F("[FACTORY] Erasing EEPROM..."));
      applyDefaults();
      EEPROM.put(0, config);
      if (extEepromAvailable)
        extEepromWrite(EXT_EEPROM_ADDR, (uint8_t*)&config, sizeof(config));
      Serial.println(F("[FACTORY] Done. Restart required."));
      return;
    case 'v':
      // ログ関連（ccvoice_log.h で実装）
      if (Serial.available()) {
        char next = Serial.peek();
        if (next == '0') {
          Serial.read();  // '0'消費
          if (extEepromAvailable) logClear();
          Serial.println(F("[LOG] Cleared."));
        } else {
          logShow();
        }
      } else {
        logShow();
      }
      return;
  }
  
  // T コマンド（RTC時刻設定）
  if (c == 'T') {
    unsigned long timeout = millis() + 150;
    String timeStr = "";
    while (timeStr.length() < 14 && millis() < timeout) {
      if (Serial.available()) timeStr += (char)Serial.read();
    }
    if (timeStr.length() == 14) {
      int Y = 2000 + timeStr.substring(2, 4).toInt();
      int M = timeStr.substring(4, 6).toInt();
      int D = timeStr.substring(6, 8).toInt();
      int H = timeStr.substring(8, 10).toInt();
      int m = timeStr.substring(10, 12).toInt();
      int S = timeStr.substring(12, 14).toInt();
      
      if (!rtcAvailable) {
        Serial.println(F("[RTC] T: DS3231 not found"));
        return;
      }
      RtcTime t = {(uint8_t)S, (uint8_t)m, (uint8_t)H, (uint8_t)D, (uint8_t)M, (uint16_t)Y};
      if (rtcWrite(t)) {
        Serial.print(F("[RTC] Time set OK: ")); printRtcTime(); Serial.println();
        recalcPeriodicAlign(millis());
      } else {
        Serial.println(F("[RTC] T: write failed"));
      }
    }
    return;
  }
  
  // ログレベルコマンド（l0/l1/l2/l3）
  if (c == 'l') {
    if (Serial.available()) {
      char digit = Serial.read();
      if (digit >= '0' && digit <= '4') {
        LOG_LEVEL = (LogLvl)(digit - '0');
        Serial.print(F("[LOG] Level set to "));
        switch (LOG_LEVEL) {
          case LOG_OFF:  Serial.println(F("OFF"));  break;
          case LOG_ERR:  Serial.println(F("ERR"));  break;
          case LOG_MIN:  Serial.println(F("MIN"));  break;
          case LOG_FULL: Serial.println(F("FULL")); break;
          case LOG_DBG:  Serial.println(F("DBG"));  break;
        }
      }
    }
    return;
  }
  
  // 数値が必要なコマンド
  unsigned long timeout = millis() + 150;
  while (!Serial.available() && millis() < timeout) {}
  long val = Serial.parseInt();
  bool chg = true;
  
  switch (c) {
    case 'm':
      if      (val == 0) { BUSY_INPUT_SOURCE = BUSY_SRC_DIGITAL; autoLocked = true; }
      else if (val == 1) { BUSY_INPUT_SOURCE = BUSY_SRC_A0;      autoLocked = true; }
      else if (val == 2) {
        BUSY_INPUT_SOURCE = BUSY_SRC_AUTO;
        autoLocked = false;
        windowStartTS = millis();
        dig_edge_count = 0;
        a0_event_count = 0;
      } else chg = false;
      break;
    
    case 'b': if (val >= 500) { BUSY_MAX_MS = (unsigned long)val; } else chg = false; break;
    case 'n': if (val >= 100) BUSY_MIN_MS = (unsigned long)val; else chg = false; break;
    case 'r': if (val >= 0)   TX_SUP_MS = (unsigned long)val; else chg = false; break;
    
    case 'p':
      if (val >= 0) {
        PERIOD_MS = (unsigned long)val * 60UL * 1000UL;
        unsigned long now = millis();
        if (PERIOD_MS > 0) {
          nextPeriodicAt = now + PERIOD_MS;
          recalcPeriodicAlign(now);
        } else {
          periodicDue = false;
          rtcAlignActive = false;
        }
      } else chg = false;
      break;
    
    case 'k': if (val >= 0 && val <= 600000) PERIOD_QUIET_MS = (unsigned long)val; else chg = false; break;
    case 'L': A0_LOW_TH = (int)val; break;
    case 'G': A0_HIGH_TH = (int)val; break;
    case 'a': if (val >= 0) A0_HOLD = (unsigned long)val; else chg = false; break;
    
    case 'w':
      if (val >= 1) {
        AUTO_WINDOW = (unsigned long)val * 60UL * 1000UL;
        windowStartTS = millis();
        dig_edge_count = 0;
        a0_event_count = 0;
      } else chg = false;
      break;
    
    case 'd': if (val >= 0 && val <= 600000) DFP_TIMEOUT_MS = (unsigned long)val; else chg = false; break;
    case 'g': if (val == 0) TMBUSY_ACTIVE_HIGH = false; else if (val == 1) TMBUSY_ACTIVE_HIGH = true; else chg = false; break;
    
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

/* ============================ setup / loop ========================= */
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);  // ★v2.30: parseInt()ブロッキング制限（v1.80h改善）
  dfpSerial.begin(9600);
  Wire.begin();
  
  rtcAvailable = rtcProbe();
  if (rtcAvailable) {
    Serial.println(F("[RTC] DS3231 found."));
  } else {
    Serial.println(F("[RTC] DS3231 not found -> millis() fallback."));
  }
  
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
  windowStartTS = now;
  
  if (PERIOD_MS > 0) {
    nextPeriodicAt = now + PERIOD_MS;
    recalcPeriodicAlign(now);
  }
  rtcResyncMs = now + RTC_RESYNC_INTERVAL;
  
  Serial.println(F("[START] OpenCCVoice v2.30 (v1.80i logic integrated, Absolute time control)"));
  Serial.println(F("[LOG] MIN (l0=off l1=min l2=full l3=dbg l4=verbose)"));
  printSummary();
  printHelp();
  
  if (extEepromAvailable) {
    logInit();
    logWrite(LOG_EVT_BOT, (uint32_t)CONFIG_VERSION);
  }
}

void loop() {
  unsigned long now = millis();
  
  // ★v2.30: STOP状態処理を loop() 内に統合
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
  
  // ★Ver.5 ピンマップ: D3ミラー、D10 DFP BUSY
  bool dfpPlaying = (digitalRead(PIN_DFP_BSY) == LOW);
  digitalWrite(PIN_DFP_OUT, DFP_MIRROR_INVERT ? (dfpPlaying ? HIGH : LOW)
                                              : (dfpPlaying ? LOW : HIGH));
  
  // ★v2.30: D11 デバウンス（タイムアウトマン）
  bool rTm = readTmDigital();
  if (rTm != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) {
    tmBusyFiltered = rTm;
    tmDebounceTS = now;
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
    a0Detect = false;
    a0Busy = false;
  }
  
  bool rB = readBusy();
  
  // 送信後ガード解除（v1.80i改善）
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
  
  // BUSY OFF で静寂起点更新
  if (!rB && tmBusyPrev) lastBusyOffAt = now;
  tmBusyPrev = rB;
  
  // LED表示
  digitalWrite(PIN_BUSY_LED, rB ? HIGH : LOW);
  
  bool suppressNow = isSuppressedNow(now);
  bool autoBlinking = ((long)(autoSwitchBlinkUntil - now) > 0);
  
  // D6 抑止LED
  digitalWrite(PIN_SUP_LED, suppressNow ? HIGH : LOW);
  
  // D7 A0/AUTO表示LED
  bool slowBlink  = ((now / 1000) % 2 == 0);
  bool fastBlink  = ((now /  500) % 2 == 0);
  
  if (autoBlinking) {
    digitalWrite(PIN_A0_LED, ((now / 250) % 2 == 0) ? HIGH : LOW);
  } else if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) {
    digitalWrite(PIN_A0_LED, slowBlink ? HIGH : LOW);
  } else if (BUSY_INPUT_SOURCE == BUSY_SRC_AUTO) {
    digitalWrite(PIN_A0_LED, HIGH);
  } else {
    digitalWrite(PIN_A0_LED, LOW);
  }
  
  // ★v2.30改善: 受信長判定（カーチャンク vs 通常会話）
  if (!pAct && !suppressNow) {
    if (rB) {
      if (tmBusyStart == 0) tmBusyStart = now;
    } else if (tmBusyStart != 0) {
      unsigned long dur = now - tmBusyStart;
      tmBusyStart = 0;
      
      if (dur >= BUSY_MAX_MS) {
        // ② 超え → 通常会話終了 → ⑤抑止開始（起点B）
        // pAct=true（PTT送出中）の場合も起点B として busySupUntil をセットする。
        busySupUntil = now + TX_SUP_MS;
        if (LOG_LEVEL >= LOG_MIN) {
          Serial.print(F("[SUP] START "));
          Serial.print(TX_SUP_MS);
          Serial.print(F("ms (long-talk dur="));
          Serial.print(dur);
          Serial.println(F("ms)"));
        }
      }
      else if (dur >= BUSY_MIN_MS) {
        // ①② 範囲内 → カーチャンク
        // busySupUntil はここではセットしない。
        // 抑止セットは POST終了（起点A）に一本化する。
        if (!pAct && !isSuppressedNow(now)) {
          if (LOG_LEVEL >= LOG_MIN) {
            Serial.print(F("[RX] CK dur="));
            Serial.print(dur);
            Serial.println(F("ms -> ID TX"));
          }
          logWrite(LOG_EVT_CAR, dur);
          startPtt(1);
        } else if (LOG_LEVEL >= LOG_MIN) {
          Serial.print(F("[RX] CK dur="));
          Serial.print(dur);
          if (pAct) {
            Serial.println(F("ms skip(pAct)"));
          } else {
            Serial.print(F("ms skip(sup remain="));
            Serial.print(busySupUntil - now);
            Serial.println(F("ms)"));
          }
        }
      }
      else {
        // ① 未満 → ノイズ
        if (LOG_LEVEL >= LOG_DBG) {
          Serial.print(F("[RX] noise dur="));
          Serial.print(dur);
          Serial.println(F("ms (< MIN)"));
        }
      }
    }
  } else {
    tmBusyStart = 0;
  }
  
  // 周期ID スケジューラ
  if (PERIOD_MS == 0) {
    periodicDue = false;
  } else {
    if (rtcAlignActive) {
      if ((long)(now - nextPeriodicAt) >= 0) {
        periodicDue = true;
        recalcPeriodicAlign(now);
        if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[Periodic] RTC due"));
      }
      if ((long)(now - rtcResyncMs) >= 0) {
        rtcResyncMs = now + RTC_RESYNC_INTERVAL;
        recalcPeriodicAlign(now);
        if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[RTC] Resync done"));
      }
    } else {
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
  }
  
  // 周期ID送出（Quiet Guard）
  if (periodicDue && state == IDLE) {
    bool quietOK = (!rB) && ((long)(now - lastBusyOffAt) >= (long)PERIOD_QUIET_MS);
    bool guardOK = !suppressNow;
    if (quietOK && guardOK) {
      if (LOG_LEVEL >= LOG_MIN) {
        Serial.print(F("[EVT] Periodic Track"));
        Serial.print(nextPeriodicTrack);
        Serial.println();
      }
      logWrite(LOG_EVT_PER, (uint32_t)nextPeriodicTrack);
      startPtt(nextPeriodicTrack);
      nextPeriodicTrack = (nextPeriodicTrack == 2 ? 3 : 2);
      periodicDue = false;
    } else {
      if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[Periodic] deferred (busy/quiet/suppress)"));
    }
  }
  
  // ★v2.30改善: PTT状態マシン（絶対時刻比較）
  switch (state) {
    case PTT_ON_WAIT:
      // PRE終了を絶対時刻で判定（ブロッキングで now がずれても安全）
      if ((long)(now - pttPreEndAt) >= 0) {
        if (requestedTrack) { dfpSend(0x03, requestedTrack); requestedTrack = 0; }
        state = PLAYING;
        playingEnterAt = now;
      }
      break;
    
    case PLAYING:
      if (digitalRead(PIN_DFP_BSY) == LOW) {
        dfpStarted = true;
        busyHighSince = 0;
      } else {
        if (dfpStarted) {
          if (busyHighSince == 0) busyHighSince = now;
          if (now - busyHighSince >= 40) {
            // POST終了予定時刻を絶対値でセット
            pttPostEndAt = now + PTT_POST_MS;
            state = PTT_OFF_WAIT;
            if (LOG_LEVEL >= LOG_FULL) Serial.println(F("[TX] Play done -> POST start"));
          }
        }
      }
      if (DFP_TIMEOUT_MS > 0 && playingEnterAt > 0 && (now - playingEnterAt >= DFP_TIMEOUT_MS)) {
        if (LOG_LEVEL >= LOG_MIN) Serial.println(F("[ERR] DFP Timeout -> Force PTT OFF"));
        pttPostEndAt = now + PTT_POST_MS;
        state = PTT_OFF_WAIT;
      }
      break;
    
    case PTT_OFF_WAIT:
      // POST終了を絶対時刻で判定 かつ PTT最低ON保持も満たす
      if ((long)(now - pttPostEndAt) >= 0 && (long)(now - pttMinOn) >= 0) {
        setPtt(false);
        state = IDLE;
        if (LOG_LEVEL >= LOG_FULL) Serial.println(F("[TX] POST done -> IDLE"));
        bool rBnow = readBusy();
        if (!rBnow) {
          // ★v2.30: 起点A：POST終了時、受信なし
          busySupUntil = now + TX_SUP_MS;
          if (LOG_LEVEL >= LOG_MIN) {
            Serial.print(F("[SUP] START "));
            Serial.print(TX_SUP_MS);
            Serial.println(F("ms (from POST-end)"));
          }
        } else {
          if (LOG_LEVEL >= LOG_FULL) Serial.println(F("[SUP] Defer to RX-end (point-B)"));
        }
      }
      break;
    
    case IDLE:
    default:
      break;
  }
  
  if ((long)(now - pttMinOn) < 0) setPtt(true);
  
  // テストSW（1〜3クリック）
  bool sw = digitalRead(PIN_TEST_SW);
  if (sw == LOW && lastSwState == HIGH) {
    if (!clickWaiting) { clickWaiting = true; clickCount = 1; firstClickTime = now; }
    else clickCount++;
  }
  lastSwState = sw;
  if (clickWaiting && (now - firstClickTime >= 1000)) {
    if (state == IDLE && clickCount >= 1 && clickCount <= 3) {
      startPtt(clickCount);
    }
    clickWaiting = false;
    clickCount = 0;
  }
  
  // AUTO固定判定
  maybeAuto(now);
  
  // 抑止状態変化検出ログ
  bool curSup = isSuppressedNow(now);
  if (!prevSuppressed && curSup) {
    if (LOG_LEVEL >= LOG_MIN) Serial.println(F("[SUP] ACTIVE"));
  } else if (prevSuppressed && !curSup) {
    if (LOG_LEVEL >= LOG_MIN) Serial.println(F("[SUP] CLEAR"));
  }
  prevSuppressed = curSup;
}

void maybeAuto(unsigned long now) {
  if (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO || autoLocked) return;
  if ((long)(now - windowStartTS) >= (long)AUTO_WINDOW) {
    BusySrc n;
    uint16_t d11 = dig_edge_count, a0 = a0_event_count;
    if      (d11 < 10 && a0 >= 20) n = BUSY_SRC_A0;
    else if (a0 < 20 && d11 >= 10) n = BUSY_SRC_DIGITAL;
    else n = (d11 >= a0) ? BUSY_SRC_DIGITAL : BUSY_SRC_A0;
    BUSY_INPUT_SOURCE = n;
    autoLocked = true;
    autoSwitchBlinkUntil = now + 3000;
    saveSettings();
    if (LOG_LEVEL >= LOG_MIN) {
      Serial.print(F("[AUTO-FIXED] Lock to "));
      Serial.println(n == BUSY_SRC_DIGITAL ? F("D11") : F("A0"));
    }
  }
}
