/************************************************************
 * OpenCCVoice Guidance Controller  (Unified / Safe)
 * Version : 1.80h
 * Target  : Arduino Nano (ATmega328P, 5V)
 *
 * ============================================================
 * 【概要】
 * ============================================================
 * DMR無線機のID送出を自動制御するコントローラ。
 * 受信検知（D6デジタル / A0アナログ / AUTO自動判定）に応じて
 * DFPlayerへ音声トラックを送出し、PTTを制御する。
 *
 * - 既存配線（DFPlayer: D10=Arduino RX / D11=Arduino TX）を維持
 * - 入力は INPUT_PULLUP（浮き対策）
 * - フェイルセーフ（DFPlayer応答なし）: d#### (ms), d0=無効
 * - BUSY極性切替: g0=LOW=busy / g1=HIGH=busy
 * - 周期ID: p##（分、0で停止）起算は起動時刻基準（v1.x系）
 * - 周期IDは「BUSYがOFFになってから k#### ms 静寂継続」の場合のみ送出
 *   BUSY中/静寂不足/抑止中はスキップ（破棄）
 * - AUTO判定（m2）でD6/A0を観測し優位側へ固定
 * - 抑止（長話/連打/送信後）を統合管理
 * - PTTガード、BUSYデバウンスを実装
 * - EEPROMバージョン管理（config.ver）で自動移行/初期化
 *
 * ============================================================
 * 【仕様番号とコマンド対応】
 * ============================================================
 *  ① カーチャンク最小受信長   n####  (デフォルト: 500ms)
 *  ② カーチャンク最大受信長   b####  (デフォルト: 1500ms)
 *  ③ PTT先行無音時間(PRE)    j####  (デフォルト: 1000ms)
 *  ④ PTT後行無音時間(POST)   J####  (デフォルト: 1000ms)
 *  ⑤ 抑止時間               r####  (デフォルト: 10000ms)
 *  ⑥ 定周期間隔              p##    (デフォルト: 30分)
 *
 * ============================================================
 * 【バージョン履歴】
 * ============================================================
 * v1.80h 変更点（v1.73f1 → v1.80h）：
 *
 *   [仕様変更]
 *   - カーチャンク判定: ① MIN(n) 以上 ② MAX(b) 未満に統一
 *   - ② MAX デフォルト: 3900ms → 1500ms（通常会話との境界を明確化）
 *   - ⑤ 抑止時間(r) デフォルト: 3000ms → 10000ms
 *   - ③ PTT先行無音時間(PRE) をコマンド j#### で可変化（EEPROM保存）
 *   - ④ PTT後行無音時間(POST) をコマンド J#### で可変化（EEPROM保存）
 *   - 通常会話（② MAX 超え受信）終了後は受信終了起点で⑤抑止を開始
 *   - 定周期IDは受信中・抑止中・PTT中に到来したらスキップ（破棄）
 *     旧仕様の「延期（periodicDue保持）」から変更
 *
 *   [抑止ロジック刷新]
 *   - 連打抑止（BURSTロジック）を削除
 *     抑止は ⑤抑止（busySupUntil）のみで管理
 *   - ⑤抑止の起点を統一:
 *       起点A: ④POST終了の瞬間（受信中でない場合）
 *       起点B: ②超え受信の終了の瞬間（通常会話終了）
 *   - PTT送出中(pAct=true)も tmBusyStart を記録継続
 *
 *   [コマンド整理]
 *   - 追加: j####（③ PRE時間）/ J####（④ POST時間）
 *   - 削除: s0/s1（抑止全体ON/OFF）
 *   - 削除: t0/t1（送信後抑止ON/OFF）
 *   - 削除: i####（送信直後ガード閾値）
 *   - 削除: H（汎用プリセット）
 *   - 変更: STOP復帰コマンドを r → R（大文字）に変更
 *   - 変更: ログレベルコマンドを 0/1/2 → l0/l1/l2/l3 に変更
 *     （l3=DBG 新設。起動時は l1=MIN 固定、EEPROM保存なし）
 *
 *   [バグ修正]
 *   - Serial.setTimeout(50) を setup() で一度だけ設定するよう変更
 *     旧実装では parseInt() ブロッキング中に millis() が進み、
 *     PRE/POST 無音時間がスキップされる場合があった
 *   - PTT_ON_WAIT / PTT_OFF_WAIT の待機判定を相対時間比較から
 *     絶対時刻比較（pttPreEndAt / pttPostEndAt）に変更
 *     ブロッキングで now がずれても PRE/POST が確実に保証される
 *
 *   [ログ改善]
 *   - 抑止中カーチャンクのスキップログに残り時間を付記
 *     "skip(pAct)" / "skip(sup remain=####ms)" を LOG_MIN から表示
 *
 *   [EEPROM]
 *   - CONFIG_VERSION: 4 → 5
 *   - 追加フィールド: pttPreMs / pttPostMs
 *   - 削除フィールド: suppressOn / txAfSupOn / idleMin
 *   - ver=4（v1.73系）からの自動マイグレーション対応
 *
 * v1.73f1 変更点：
 *   - `v` コマンドを `V`（大文字）に変更（v2.x系との競合回避）
 *
 * v1.73f 変更点：
 *   - V コマンド追加、Z コマンド追加
 *   - loop() の処理を機能別関数に分割
 *
 * v1.73e 変更点：
 *   - 送信直後ガード（postTxIgnore）追加（v1.80h で削除・⑤抑止に統合）
 *   - 周期IDのcatch-up対策
 *   - A0 BUSY観測安定化
 *
 * ============================================================
 * 【操作（115200 8N1, 改行なし/LF）】
 * ============================================================
 *  n####      ① 最小受信長(ms)  デフォルト:500
 *  b####      ② 最大受信長(ms)  デフォルト:1500
 *  j####      ③ PTT PRE時間(ms) デフォルト:1000
 *  J####      ④ PTT POST時間(ms) デフォルト:1000
 *  r####      ⑤ 抑止時間(ms)    デフォルト:10000
 *  p##        ⑥ 定周期ID（分、0=停止）
 *  k####      定周期送出前の静寂待機時間(ms)
 *  m0/m1/m2   BUSYソース（D6/A0/AUTO）
 *  g0/g1      BUSY極性（LOW=busy / HIGH=busy）
 *  L###       A0閾値LOW
 *  G###       A0閾値HIGH
 *  a####      A0保持時間(ms)
 *  w##        AUTO観測時間（分）
 *  d####      DFPフェイルセーフ(ms、0=無効)
 *  V          バージョン表示（スケッチver + EEPROM_VER）
 *  q          設定一覧（全パラメータ表示）
 *  x          Safe Stop（全機能停止）
 *  R          Safe Stopからの復帰
 *  Z          ソフトウェアリセット（WDT、EEPROM変更なし）
 *  F          Factory Reset（EEPROM初期化＋保存）
 *  l0/l1/l2/l3  ログレベル（OFF/MIN/FULL/DBG）起動時はl1固定
 *  h          コマンド一覧
 ************************************************************/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <avr/wdt.h>

/* ============================== EEPROM =============================== */
struct MyConfig {
  uint32_t magic;
  uint8_t  busySrc;
  uint32_t busyMin;
  uint32_t busyMax;
  uint32_t pttPreMs;
  uint32_t pttPostMs;
  uint32_t txSupMs;
  uint32_t periodMin;
  uint32_t periodQuietMs;
  int      a0Low;
  int      a0High;
  uint32_t a0Hold;
  uint32_t autoWinMin;
  uint32_t dfpTimeoutMs;
  uint8_t  tmBusyActiveHigh;
  uint8_t  ver;              // ★末尾固定
} config;

const uint32_t CONFIG_MAGIC   = 0xDEADBEEF;
const uint8_t  CONFIG_VERSION = 5;

/* =========================== Runtime Params =========================== */
enum BusySrc { BUSY_SRC_D6, BUSY_SRC_A0, BUSY_SRC_AUTO };
volatile BusySrc BUSY_INPUT_SOURCE;

unsigned long BUSY_MIN_MS;
unsigned long BUSY_MAX_MS;
unsigned long PTT_PRE_MS;
unsigned long PTT_POST_MS;
unsigned long TX_SUP_MS;
unsigned long PERIOD_MS;
unsigned long PERIOD_QUIET_MS;
int           A0_LOW_TH;
int           A0_HIGH_TH;
unsigned long A0_HOLD;
unsigned long AUTO_WINDOW;
unsigned long DFP_TIMEOUT_MS;
bool          TMBUSY_ACTIVE_HIGH;

/* ============================== Pins ================================== */
const bool DFP_BUSYOUT_INVERT = true;

const uint8_t PIN_TEST_SW  = 3;
const uint8_t PIN_BUSY_LED = 4;
const uint8_t PIN_PTT      = 5;
const uint8_t PIN_TM_BUSY  = 6;
const uint8_t PIN_DFP_BSY  = 7;
const uint8_t PIN_DFP_OUT  = 2;
const uint8_t PIN_SUP_LED  = 13;
const uint8_t PIN_A0_LED   = 12;
const uint8_t A0_PIN       = A0;

const uint8_t ARD_RX_FROM_DFP = 10;
const uint8_t ARD_TX_TO_DFP   = 11;
SoftwareSerial dfpSerial(ARD_RX_FROM_DFP, ARD_TX_TO_DFP);

/* ========================= Fixed Constants ============================ */
const unsigned long DEBOUNCE_MS  = 5;

/* ============================ State / Vars ============================ */
unsigned long windowStartTS = 0;
unsigned long autoSwitchBlinkUntil = 0;
bool autoLocked = false;

/*
 * ログレベル:
 *   l0 = LOG_OFF  : 出力なし
 *   l1 = LOG_MIN  : 必要最低限（TX開始/抑止/エラー）  ← 起動時デフォルト
 *   l2 = LOG_FULL : 標準ログ（PTT ON/OFF・受信長等）
 *   l3 = LOG_DBG  : デバッグ（ノイズ・スキップ詳細等）
 * ※EEPROM保存なし。起動時は常に LOG_MIN。
 */
enum LogLvl { LOG_OFF=0, LOG_MIN=1, LOG_FULL=2, LOG_DBG=3 };
volatile LogLvl LOG_LEVEL = LOG_MIN;

enum State { IDLE, PTT_ON_WAIT, PLAYING, PTT_OFF_WAIT };
State state = IDLE;

// BUSY検出
unsigned long tmBusyStart=0;
unsigned long tmDebounceTS=0;
unsigned long a0LastSignalTS=0;
bool tmBusyPrev=false;
bool tmBusyFiltered=false;
bool a0Detect=false;
bool a0Busy=false;

// PTT制御
unsigned long pttPreEndAt=0;   // PRE終了予定時刻（絶対値）
unsigned long pttPostEndAt=0;  // POST終了予定時刻（絶対値）
unsigned long pttMinOn=0;
unsigned long nextPeriodicAt=0;
bool dfpStarted=false;
bool pttOutState=false;
bool clickWaiting=false;
bool stopped=false;
uint16_t requestedTrack=0;
uint16_t nextPeriodicTrack=2;

// 抑止タイマー
unsigned long busySupUntil=0;
unsigned long busyHighSince=0;
unsigned long playingEnterAt=0;

// テストSW
uint8_t  clickCount=0;
uint8_t  lastSwState=HIGH;
unsigned long firstClickTime=0;

// AUTO判定カウンタ
uint16_t d6_edge_count = 0;
uint16_t a0_event_count = 0;

// 静寂ガード / 抑止変化検出
unsigned long lastBusyOffAt = 0;
bool prevSuppressed = false;

/* ============================== Utils ================================= */
inline bool readTmRaw() { return (digitalRead(PIN_TM_BUSY) == HIGH); }
inline bool readTmD6()  { bool r = readTmRaw(); return TMBUSY_ACTIVE_HIGH ? r : !r; }
inline bool readBusy()  {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) return a0Busy;
  return (tmBusyFiltered || a0Busy);
}

void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x00,
                   (uint8_t)(param >> 8), (uint8_t)(param & 0xFF),
                   0x00, 0x00, 0xEF};
  uint16_t s = 0;
  for (int i = 1; i < 7; i++) s += f[i];
  s = 0xFFFF - s + 1;
  f[7] = (uint8_t)(s >> 8);
  f[8] = (uint8_t)s;
  dfpSerial.write(f, 10);
}

void setPtt(bool on) {
  if (pttOutState == on) return;
  pttOutState = on;
  digitalWrite(PIN_PTT, on ? HIGH : LOW);
  if (LOG_LEVEL >= LOG_FULL) { Serial.print(F("[PTT] ")); Serial.println(on ? F("ON") : F("OFF")); }
}

void startPtt(uint16_t trk) {
  if (state != IDLE) return;
  requestedTrack = trk; dfpStarted = false;
  unsigned long now = millis();
  setPtt(true);
  pttPreEndAt  = now + PTT_PRE_MS;        // PRE終了予定時刻（絶対値）
  pttMinOn     = now + PTT_PRE_MS + 100;  // PTT最低ON保持時刻
  pttPostEndAt = 0;                        // POST開始時にセット
  playingEnterAt = 0;
  state = PTT_ON_WAIT;
  if (LOG_LEVEL >= LOG_MIN) {
    Serial.print(F("[TX] Track "));
    if (trk < 10) Serial.print('0');
    Serial.print(trk);
    Serial.print(F(" ("));
    if      (trk == 1) Serial.print(F("001.mp3 ID"));
    else if (trk == 2) Serial.print(F("002.mp3 Periodic-A"));
    else if (trk == 3) Serial.print(F("003.mp3 Periodic-B"));
    else               Serial.print(F("custom"));
    Serial.println(F(") -> PRE start"));
  }
}

static inline bool isSuppressedNow(unsigned long now) {
  return ((long)(now - busySupUntil) < 0);
}

/* ============================ Defaults/EEPROM ========================= */
void applyDefaults() {
  BUSY_INPUT_SOURCE  = BUSY_SRC_D6;
  BUSY_MIN_MS        = 500;
  BUSY_MAX_MS        = 1500;
  PTT_PRE_MS         = 1000;
  PTT_POST_MS        = 1000;
  TX_SUP_MS          = 10000;
  PERIOD_MS          = 30UL * 60UL * 1000UL;
  PERIOD_QUIET_MS    = 2000;
  A0_LOW_TH          = 300;
  A0_HIGH_TH         = 700;
  A0_HOLD            = 800;
  AUTO_WINDOW        = 30UL * 60UL * 1000UL;
  DFP_TIMEOUT_MS     = 20000;
  TMBUSY_ACTIVE_HIGH = true;
  autoLocked         = false;
  config.ver         = CONFIG_VERSION;
}

void saveSettings() {
  config.magic            = CONFIG_MAGIC;
  config.busySrc          = (uint8_t)BUSY_INPUT_SOURCE;
  config.busyMin          = BUSY_MIN_MS;
  config.busyMax          = BUSY_MAX_MS;
  config.pttPreMs         = PTT_PRE_MS;
  config.pttPostMs        = PTT_POST_MS;
  config.txSupMs          = TX_SUP_MS;
  config.periodMin        = PERIOD_MS / 60000UL;
  config.periodQuietMs    = PERIOD_QUIET_MS;
  config.a0Low            = A0_LOW_TH;
  config.a0High           = A0_HIGH_TH;
  config.a0Hold           = A0_HOLD;
  config.autoWinMin       = AUTO_WINDOW / 60000UL;
  config.dfpTimeoutMs     = DFP_TIMEOUT_MS;
  config.tmBusyActiveHigh = TMBUSY_ACTIVE_HIGH ? 1 : 0;
  config.ver              = CONFIG_VERSION;
  EEPROM.put(0, config);
  if (LOG_LEVEL >= LOG_FULL) Serial.println(F("[EEPROM] Settings Saved."));
}

void migrateOrInit() {
  EEPROM.get(0, config);

  if (config.magic != CONFIG_MAGIC) {
    Serial.println(F("[EEPROM] No/Other Data. Init defaults."));
    applyDefaults(); saveSettings(); return;
  }

  if (config.ver != CONFIG_VERSION) {
    Serial.print(F("[EEPROM] Version mismatch: stored="));
    Serial.print(config.ver); Serial.print(F(" expected="));
    Serial.println(CONFIG_VERSION);

    if (config.ver == 4) {
      config.pttPreMs  = 1000;
      config.pttPostMs = 1000;
      if (config.busyMax == 3900) config.busyMax = 1500;
      if (config.txSupMs == 3000) config.txSupMs = 10000;
    }
    if (!(config.tmBusyActiveHigh == 0 || config.tmBusyActiveHigh == 1))
      config.tmBusyActiveHigh = 1;
    if (config.dfpTimeoutMs > 600000UL)
      config.dfpTimeoutMs = 20000UL;
    if (config.periodQuietMs == 0 || config.periodQuietMs > 600000UL)
      config.periodQuietMs = 2000UL;
    if (config.pttPreMs  < 100 || config.pttPreMs  > 5000) config.pttPreMs  = 1000;
    if (config.pttPostMs < 100 || config.pttPostMs > 5000) config.pttPostMs = 1000;

    config.ver = CONFIG_VERSION;
    EEPROM.put(0, config);
    Serial.println(F("[EEPROM] Migration done."));
  }

  BUSY_INPUT_SOURCE  = (BusySrc)config.busySrc;
  BUSY_MIN_MS        = config.busyMin;
  BUSY_MAX_MS        = config.busyMax;
  PTT_PRE_MS         = config.pttPreMs;
  PTT_POST_MS        = config.pttPostMs;
  TX_SUP_MS          = config.txSupMs;
  PERIOD_MS          = (unsigned long)config.periodMin * 60000UL;
  PERIOD_QUIET_MS    = config.periodQuietMs;
  A0_LOW_TH          = config.a0Low;
  A0_HIGH_TH         = config.a0High;
  A0_HOLD            = config.a0Hold;
  AUTO_WINDOW        = (unsigned long)config.autoWinMin * 60000UL;
  DFP_TIMEOUT_MS     = config.dfpTimeoutMs;
  TMBUSY_ACTIVE_HIGH = (config.tmBusyActiveHigh == 1);
  autoLocked         = (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO);
  Serial.println(F("[EEPROM] Settings Loaded."));
}

/* ============================== Prints ================================ */
void printSummary() {
  Serial.print(F("[CFG] EEPROM_VER=")); Serial.print(config.ver);
  Serial.print(F(" SRC="));
  if (BUSY_INPUT_SOURCE == BUSY_SRC_AUTO) Serial.print(F("AUTO"));
  else Serial.print(BUSY_INPUT_SOURCE == BUSY_SRC_D6 ? F("D6") : F("A0"));
  if (autoLocked) Serial.print(F("(LOCK)"));
  Serial.print(F(" MIN="));       Serial.print(BUSY_MIN_MS);
  Serial.print(F(" MAX="));       Serial.print(BUSY_MAX_MS);
  Serial.print(F(" PRE="));       Serial.print(PTT_PRE_MS);
  Serial.print(F(" POST="));      Serial.print(PTT_POST_MS);
  Serial.print(F(" SUP="));       Serial.print(TX_SUP_MS);
  Serial.print(F(" PER(min)="));  Serial.print(PERIOD_MS / 60000UL);
  Serial.print(F(" QUIET(ms)=")); Serial.print(PERIOD_QUIET_MS);
  Serial.print(F(" A0[L/H]="));   Serial.print(A0_LOW_TH); Serial.print('/'); Serial.print(A0_HIGH_TH);
  Serial.print(F(" HOLD="));      Serial.print(A0_HOLD);
  Serial.print(F(" AUTO(min)=")); Serial.print(AUTO_WINDOW / 60000UL);
  Serial.print(F(" DFP_TO="));    Serial.print(DFP_TIMEOUT_MS);
  Serial.print(F(" POL="));       Serial.println(TMBUSY_ACTIVE_HIGH ? F("HIGH=busy") : F("LOW=busy"));
}

void printHelp() {
  Serial.println(F("---- OpenCCVoice v1.80h HELP ----"));
  Serial.println(F("[受信] n####=MIN(ms) b####=MAX(ms)"));
  Serial.println(F("[送信] j####=PRE(ms) J####=POST(ms)"));
  Serial.println(F("[抑止] r####=SUP(ms)"));
  Serial.println(F("[周期] p##=period(min) k####=quiet(ms)"));
  Serial.println(F("[BUSY] m0=D6 m1=A0 m2=AUTO | g0=LOW=busy g1=HIGH=busy"));
  Serial.println(F("[A0]   L###=lo G###=hi a####=hold(ms) w##=AUTO(min)"));
  Serial.println(F("[SYS]  d####=DFP_timeout(ms,0=off)"));
  Serial.println(F("[UTIL] V=ver q=cfg h=help x=STOP R=resume"));
  Serial.println(F("       F=factory_reset Z=SW_reset"));
  Serial.println(F("       l0=off l1=min l2=full l3=dbg"));
}

/* ============================ Command Parser ========================== */
/*
 * handleSerialCmd()
 *
 * 【v1.80d 修正】
 *   Serial.setTimeout() は setup() で一度だけ設定。ここでは呼ばない。
 *   旧実装では parseInt() ブロッキング中に millis() が進み、
 *   PTT_PRE_MS がスキップされる場合があった。
 */
void handleSerialCmd() {
  bool needSave  = false;
  bool needPrint = false;

  while (Serial.available()) {
    char c = Serial.read();
    long val = Serial.parseInt();
    bool chg = true;

    switch (c) {
      case 'm':
        if      (val==0) { BUSY_INPUT_SOURCE=BUSY_SRC_D6;  autoLocked=true; }
        else if (val==1) { BUSY_INPUT_SOURCE=BUSY_SRC_A0;  autoLocked=true; }
        else if (val==2) { BUSY_INPUT_SOURCE=BUSY_SRC_AUTO; autoLocked=false;
                           windowStartTS=millis(); d6_edge_count=0; a0_event_count=0; }
        else chg=false; break;

      case 'n': if (val>=100)  BUSY_MIN_MS=val;  else chg=false; break;
      case 'b': if (val>=500)  BUSY_MAX_MS=val;  else chg=false; break;
      case 'j': if (val>=100)  PTT_PRE_MS=val;   else chg=false; break;
      case 'J': if (val>=100)  PTT_POST_MS=val;  else chg=false; break;
      case 'r': if (val>=0)    TX_SUP_MS=val;    else chg=false; break;

      case 'p':
        if (val>=0) {
          PERIOD_MS = (unsigned long)val * 60000UL;
          nextPeriodicAt = (PERIOD_MS > 0) ? millis() + PERIOD_MS : 0;
        } else chg=false; break;

      case 'k': if (val>=0 && val<=600000) PERIOD_QUIET_MS=val; else chg=false; break;
      case 'L': A0_LOW_TH  = (int)val; break;
      case 'G': A0_HIGH_TH = (int)val; break;
      case 'a': if (val>=0) A0_HOLD=(unsigned long)val; else chg=false; break;
      case 'w':
        if (val>=1) { AUTO_WINDOW=(unsigned long)val*60000UL;
                      windowStartTS=millis(); d6_edge_count=0; a0_event_count=0; }
        else chg=false; break;
      case 'd': if (val>=0 && val<=600000) DFP_TIMEOUT_MS=(unsigned long)val; else chg=false; break;
      case 'g': if (val==0||val==1) TMBUSY_ACTIVE_HIGH=(val==1); else chg=false; break;

      case 'V':
        Serial.print(F("[VER] OpenCCVoice v1.80h (EEPROM_VER="));
        Serial.print(config.ver);
        Serial.println(F(")"));
        chg=false; break;

      case 'q': needPrint=true; chg=false; break;
      case 'x': stopped=true; chg=false; Serial.println(F("[STOP]")); break;

      case 'F': applyDefaults(); needSave=true; Serial.println(F("[FACTORY RESET]")); chg=false; break;

      case 'Z':
        Serial.println(F("[RESET] Software reset by WDT..."));
        delay(100);
        wdt_enable(WDTO_15MS);
        while(1) {}
        break;

      case 'h': printHelp(); chg=false; break;

      case 'l':
        if      (val==0) { LOG_LEVEL=LOG_OFF;  Serial.println(F("[LOG] OFF"));  }
        else if (val==1) { LOG_LEVEL=LOG_MIN;  Serial.println(F("[LOG] MIN"));  }
        else if (val==2) { LOG_LEVEL=LOG_FULL; Serial.println(F("[LOG] FULL")); }
        else if (val==3) { LOG_LEVEL=LOG_DBG;  Serial.println(F("[LOG] DBG"));  }
        else {
          Serial.print(F("[LOG] level="));
          if      (LOG_LEVEL==LOG_OFF)  Serial.println(F("0(OFF)"));
          else if (LOG_LEVEL==LOG_MIN)  Serial.println(F("1(MIN)"));
          else if (LOG_LEVEL==LOG_FULL) Serial.println(F("2(FULL)"));
          else                          Serial.println(F("3(DBG)"));
        }
        chg=false; break;  // EEPROMに保存しない

      case 'R':
        if (stopped) { stopped=false; Serial.println(F("[RESUME]")); }
        chg=false; break;

      default: chg=false; break;
    }
    if (chg) { needSave=true; needPrint=true; }
  }

  if (needSave)  saveSettings();
  if (needPrint) printSummary();
}

/* ============================== Logic Modules ========================= */

void updateInputs(unsigned long now, bool pAct) {
  bool dLow = (digitalRead(PIN_DFP_BSY) == LOW);
  digitalWrite(PIN_DFP_OUT, DFP_BUSYOUT_INVERT ? (dLow ? HIGH : LOW) : (dLow ? LOW : HIGH));

  bool rTm = readTmD6();
  if (rTm != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) {
    tmBusyFiltered = rTm; tmDebounceTS = now;
    if (tmBusyFiltered) d6_edge_count++;
  }

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

  if (!rB && tmBusyPrev) {
    lastBusyOffAt = now;
    if (LOG_LEVEL >= LOG_FULL) Serial.println(F("[RX] BUSY OFF"));
  }
  if (rB && !tmBusyPrev) {
    if (LOG_LEVEL >= LOG_FULL) Serial.println(F("[RX] BUSY ON"));
  }
  tmBusyPrev = rB;
}

void updateLEDs(unsigned long now, bool rB) {
  bool bl = ((now / 500) % 2 == 0);
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
  bool s = isSuppressedNow(now);
  digitalWrite(PIN_SUP_LED,
    (long)(autoSwitchBlinkUntil - now) > 0 ? ((now/250)%2==0 ? HIGH : LOW)
                                            : (s ? HIGH : LOW));
}

/*
 * processBusyLogic()：受信長を判定し、ID送出・抑止タイマーを管理する。
 */
void processBusyLogic(unsigned long now, bool pAct, bool rB) {
  if (rB) {
    if (tmBusyStart == 0) tmBusyStart = now;
  } else {
    if (tmBusyStart != 0) {
      unsigned long dur = now - tmBusyStart;
      tmBusyStart = 0;

      if (dur >= BUSY_MAX_MS) {
        // ②超え → 長話抑止（起点B）
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
        // ①②範囲内 → カーチャンク
        if (!pAct && !isSuppressedNow(now)) {
          busySupUntil = now + TX_SUP_MS;
          if (LOG_LEVEL >= LOG_MIN) {
            Serial.print(F("[RX] CK dur="));
            Serial.print(dur);
            Serial.println(F("ms -> ID TX"));
          }
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
        // ①未満 → ノイズ
        if (LOG_LEVEL >= LOG_DBG) {
          Serial.print(F("[RX] noise dur="));
          Serial.print(dur);
          Serial.println(F("ms (< MIN)"));
        }
      }
    }
  }
}

void processPeriodicId(unsigned long now, bool rB) {
  if (PERIOD_MS == 0) return;

  bool crossed = false;
  while ((long)(now - nextPeriodicAt) >= 0) {
    nextPeriodicAt += PERIOD_MS;
    crossed = true;
  }
  if (!crossed) return;

  bool quietOK = (!rB) && ((long)(now - lastBusyOffAt) >= (long)PERIOD_QUIET_MS);
  bool guardOK = !isSuppressedNow(now);
  bool idleOK  = (state == IDLE);

  if (idleOK && quietOK && guardOK) {
    if (LOG_LEVEL >= LOG_MIN) {
      Serial.print(F("[EVT] Periodic ID -> "));
      Serial.print(F("00"));
      Serial.print(nextPeriodicTrack);
      Serial.println(F(".mp3"));
    }
    startPtt(nextPeriodicTrack);
    nextPeriodicTrack = (nextPeriodicTrack == 2 ? 3 : 2);
  } else {
    if (LOG_LEVEL >= LOG_FULL) {
      Serial.print(F("[Periodic] SKIP reason:"));
      if (!idleOK)         Serial.print(F(" ptt"));
      if (rB)              Serial.print(F(" busy"));
      if (!quietOK && !rB) Serial.print(F(" quiet"));
      if (!guardOK)        Serial.print(F(" suppress"));
      Serial.println();
    }
  }
}

/*
 * processStateMachine()：PTT送出ステートマシン。
 */
void processStateMachine(unsigned long now) {
  switch (state) {
    case PTT_ON_WAIT:
      // PRE終了を絶対時刻で判定（ブロッキングでnowがずれても安全）
      if ((long)(now - pttPreEndAt) >= 0) {
        if (requestedTrack) { dfpSend(0x03, requestedTrack); requestedTrack=0; }
        state = PLAYING; playingEnterAt = now;
      } break;

    case PLAYING:
      if (digitalRead(PIN_DFP_BSY) == LOW) {
        dfpStarted = true; busyHighSince = 0;
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
          busySupUntil = now + TX_SUP_MS;
          if (LOG_LEVEL >= LOG_MIN) {
            Serial.print(F("[SUP] START "));
            Serial.print(TX_SUP_MS);
            Serial.println(F("ms (from POST-end)"));
          }
        } else {
          if (LOG_LEVEL >= LOG_FULL) Serial.println(F("[SUP] Defer to RX-end (point-B)"));
        }
      } break;

    case IDLE:
    default: break;
  }

  if ((long)(now - pttMinOn) < 0) setPtt(true);
}

void processTestSwitch(unsigned long now) {
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
}

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
    if (LOG_LEVEL >= LOG_MIN) {
      Serial.print(F("[AUTO-FIXED] Lock to "));
      Serial.println(n==BUSY_SRC_D6?F("D6"):F("A0"));
    }
  }
}

void handleStoppedState() {
  setPtt(false);
  digitalWrite(PIN_BUSY_LED, LOW);
  digitalWrite(PIN_A0_LED,   LOW);
  digitalWrite(PIN_SUP_LED,  LOW);
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'R') { stopped=false; Serial.println(F("[RESUME]")); }
  }
  delay(5);
}

/* ============================ setup / loop ============================ */
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);   // v1.80d/e: parseInt()ブロッキング上限を50msに固定
  dfpSerial.begin(9600);

  migrateOrInit();

  pinMode(PIN_TEST_SW,  INPUT_PULLUP);
  pinMode(PIN_BUSY_LED, OUTPUT);
  pinMode(PIN_PTT,      OUTPUT);
  pinMode(PIN_TM_BUSY,  INPUT_PULLUP);
  pinMode(PIN_DFP_BSY,  INPUT_PULLUP);
  pinMode(PIN_DFP_OUT,  OUTPUT);
  pinMode(PIN_SUP_LED,  OUTPUT);
  pinMode(PIN_A0_LED,   OUTPUT);

  delay(500);
  dfpSend(0x06, 20);

  unsigned long now = millis();
  nextPeriodicAt = (PERIOD_MS > 0) ? now + PERIOD_MS : 0;
  windowStartTS  = now;
  lastBusyOffAt  = now;

  Serial.println(F("[START] OpenCCVoice v1.80h"));
  Serial.println(F("[LOG] MIN (l1=min l2=full l3=dbg l0=off)"));
  printSummary();
  printHelp();
}

/*
 * loop()：メインループ。
 *
 * 処理順序：
 *   1. handleSerialCmd()     シリアルコマンド処理
 *   2. handleStoppedState()  Safe Stop中はここで return
 *   3. updateInputs()        入力読み取り・状態更新
 *   4. updateLEDs()          LED表示更新
 *   5. processBusyLogic()    受信長判定・ID送出・抑止制御
 *   6. processPeriodicId()   周期IDスケジューラ
 *   7. processStateMachine() PTTステートマシン
 *   8. processTestSwitch()   テストSWクリック処理
 *   9. maybeAuto()           AUTO固定判定
 */
void loop() {
  unsigned long now = millis();
  handleSerialCmd();

  if (stopped) {
    handleStoppedState();
    return;
  }

  bool pAct = (state != IDLE) || ((long)(now - pttMinOn) < 0);

  updateInputs(now, pAct);

  bool rB = readBusy();

  updateLEDs(now, rB);
  processBusyLogic(now, pAct, rB);
  processPeriodicId(now, rB);
  processStateMachine(now);
  processTestSwitch(now);
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
