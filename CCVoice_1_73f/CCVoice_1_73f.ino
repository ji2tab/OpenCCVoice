/************************************************************
 * OpenCCVoice Guidance Controller  (Unified / Safe)
 * Version : 1.73f (Refactored & Optimized)
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
 * - TM BUSY極性切替: g0=LOW=busy / g1=HIGH=busy
 * - 周期ID: p##（分、0で停止）
 * - 周期IDは「BUSYがOFFになってから k#### ms 静寂継続」した場合のみ送出
 *   BUSY中/静寂不足/抑止中は延期（periodicDueを保持）
 * - AUTO判定（m2）でD6/A0を観測し優位側へ固定
 * - 抑止（長話/バースト/送信後/送信直後ガード）を統合
 * - PTTガード、BUSYデバウンスを実装
 * - EEPROMバージョン管理（config.ver）で自動移行/初期化
 *
 * ============================================================
 * 【バージョン履歴】
 * ============================================================
 * v1.73e 修正点：
 *   - 周期IDのcatch-up連続発火を根治
 *     nextPeriodicAtをwhileで未来へ追い付かせ、
 *     periodicDueは最大1回のみ立てる設計に変更
 *   - 送信直後の誤発火（A0残留BUSY/スケルチテール等）を根治
 *     PTT OFF直後にpostTxIgnoreガードを追加
 *   - A0 BUSY観測は抑止中でも継続
 *     観測停止による AUTO判定ずれを防止
 *     （AUTOカウント増加のみ停止）
 *
 * v1.73f 変更点：
 *   - vコマンド追加：スケッチバージョン＋EEPROM_VERを即時表示
 *   - Zコマンド追加：ウォッチドッグタイマー（WDT）によるソフトウェアリセット
 *     EEPROMは変更せず、設定を保持したまま再起動可能
 *     F → Z で完全クリーン起動が可能
 *   - loop()の処理を機能別関数に分割（保守性向上）
 *     updateInputs / updateLEDs / processBusyLogic /
 *     processPeriodicId / processStateMachine /
 *     processTestSwitch / handleStoppedState
 *   - handleSerialCmd()をneedSave/needPrintフラグによる
 *     一括保存・表示に最適化（while内の重複呼び出しを排除）
 *   - applyDefaults()をDRY原則に従いランタイム変数設定のみに簡潔化
 *     構造体への反映はsaveSettings()に完全委譲
 *     ※ config.ver = CONFIG_VERSION のみ残存（防衛的コーディング）
 *   - Serial.setTimeout(50)によるコマンド受信のブロッキング防止
 *   - pAct判定式を (long)(now - pttMinOn) < 0 に統一（符号安全）
 *
 * ============================================================
 * 【操作（115200 8N1, 改行なし/LF）】
 * ============================================================
 *  m0/m1/m2   BUSYソース（D6/A0/AUTO）
 *  n####      最小受信長(ms)
 *  b####      最大受信長(ms)
 *  i####      直前アイドル時間(ms) / postTxIgnore解除閾値
 *  s0/1       抑止 OFF/ON
 *  t0/1       送信後抑止 OFF/ON
 *  r####      送信後無視時間(ms)
 *  p##        周期ID（分、0=停止）
 *  k####      周期ID静寂時間(ms)
 *  L###       A0閾値LOW
 *  G###       A0閾値HIGH
 *  a####      A0保持時間(ms)
 *  w##        AUTO観測時間（分）
 *  d####      DFPフェイルセーフ(ms、0=無効)
 *  g0/g1      TM BUSY極性（LOW=busy / HIGH=busy）
 *  v          バージョン表示（スケッチver + EEPROM_VER）
 *  q          設定一覧（全パラメータ表示）
 *  H          汎用プリセット（s0/t0/b3900）
 *  x          Safe Stop（全機能停止）
 *  r          停止からの復帰（STOP中のみ有効）
 *  Z          ソフトウェアリセット（WDT、EEPROM変更なし）
 *  F          Factory Reset（EEPROM初期化＋保存）
 *  0..3       ログレベル（NONE/ERR/INF/DBG）
 *  h          コマンド一覧
 ************************************************************/

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <avr/wdt.h>

/* ============================== EEPROM =============================== */
/*
 * 構造体の方針：
 *   - 旧構造からの互換性を保つため、verは末尾に配置
 *   - v1.73c(ver=3) → v1.73d(ver=4) で periodQuietMs を追加
 *   - v1.73e/f は挙動改善のみ（レイアウト変更なし）→ ver=4 のまま
 *
 * CONFIG_VERSION の更新ルール：
 *   構造体にフィールドを追加した場合のみインクリメント。
 *   挙動変更・コマンド追加だけでは変更不要。
 */
struct MyConfig {
  uint32_t magic;
  uint8_t  busySrc;          // [m] 0:D6, 1:A0, 2:AUTO
  uint8_t  suppressOn;       // [s] 0:OFF, 1:ON
  uint8_t  txAfSupOn;        // [t] 0:OFF, 1:ON
  uint32_t busyMin;          // [n] 最小受信長(ms)
  uint32_t busyMax;          // [b] 最大受信長(ms)
  uint32_t idleMin;          // [i] 直前アイドル時間(ms)
  uint32_t periodMin;        // [p] 周期ID間隔（分）
  uint32_t txSupMs;          // [r] 送信後無視時間(ms)
  int      a0Low;            // [L] A0閾値LOW
  int      a0High;           // [G] A0閾値HIGH
  uint32_t a0Hold;           // [a] A0保持時間(ms)
  uint32_t autoWinMin;       // [w] AUTO観測窓（分）
  uint32_t dfpTimeoutMs;     // [d] DFPフェイルセーフ(ms), 0=無効
  uint8_t  tmBusyActiveHigh; // [g] 0=LOW=busy, 1=HIGH=busy
  uint32_t periodQuietMs;    // [k] 周期IDの静寂条件(ms) ★v1.73d追加
  uint8_t  ver;              // EEPROMレイアウト版 ★末尾固定
} config;

const uint32_t CONFIG_MAGIC   = 0xDEADBEEF;
const uint8_t  CONFIG_VERSION = 4;  // v1.73d/e/f 共通

/* =========================== Runtime Params =========================== */
/*
 * EEPROMから読み出した設定値をランタイム変数として保持。
 * コマンド受信時はこれらを直接変更し、saveSettings()でEEPROMへ同期する。
 */
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
unsigned long LONG_TALK_MS;   // = BUSY_MAX_MS （b####変更時に連動更新）

unsigned long DFP_TIMEOUT_MS;
bool TMBUSY_ACTIVE_HIGH;
unsigned long PERIOD_QUIET_MS;

/* ============================== Pins ================================== */
const bool DFP_BUSYOUT_INVERT = true;  // D2ミラーを反転出力（再生中=HIGH）

const uint8_t PIN_TEST_SW  = 3;   // D3: テストSW（INPUT_PULLUP）
const uint8_t PIN_BUSY_LED = 4;   // D4: D6系BUSY LED
const uint8_t PIN_PTT      = 5;   // D5: PTT出力（HIGH=送信）
const uint8_t PIN_TM_BUSY  = 6;   // D6: TM BUSY入力（極性切替可）
const uint8_t PIN_DFP_BSY  = 7;   // D7: DFPlayer BUSY入力（LOW=再生中）
const uint8_t PIN_DFP_OUT  = 2;   // D2: DFPlayer BUSYミラー出力（反転）
const uint8_t PIN_SUP_LED  = 13;  // D13: 抑止/AUTO通知LED
const uint8_t PIN_A0_LED   = 12;  // D12: A0系BUSY LED
const uint8_t A0_PIN       = A0;  // A0: アナログBUSY入力

const uint8_t ARD_RX_FROM_DFP = 10; // DFPlayer TX → D10(Arduino RX)
const uint8_t ARD_TX_TO_DFP   = 11; // DFPlayer RX ← D11(Arduino TX)
SoftwareSerial dfpSerial(ARD_RX_FROM_DFP, ARD_TX_TO_DFP);

/* ========================= Constants / Guards ========================= */
const unsigned long REFRAC_MS    = 3000;   // 短発ID不応期(ms)
const unsigned long DEBOUNCE_MS  = 5;      // D6デバウンス(ms)
const unsigned long PTT_PRE_MS   = 1000;   // 再生前PTT先行時間(ms)
const unsigned long PTT_POST_MS  = 1000;   // 再生後PTT保持時間(ms)
const unsigned long LONG_SUP_MS  = 10000;  // 長話抑止時間(ms)
const unsigned long BURST_WIN_MS = 10000;  // バースト検出窓(ms)
const unsigned int  BURST_TH     = 2;      // バースト閾値（回数）
const unsigned long BURST_SUP_MS = 10000;  // バースト抑止時間(ms)

/* ============================ State / Vars ============================ */
unsigned long windowStartTS = 0;          // AUTO観測窓の開始時刻
unsigned long autoSwitchBlinkUntil = 0;   // AUTO固定通知点滅の終了時刻
bool autoLocked = false;                  // AUTO固定済みフラグ

enum LogLvl { LOG_NONE=0, LOG_ERR=1, LOG_INF=2, LOG_DBG=3 };
volatile LogLvl LOG_LEVEL = LOG_INF;

enum State { IDLE, PTT_ON_WAIT, PLAYING, PTT_OFF_WAIT };
State state = IDLE;

// BUSY検出
unsigned long tmBusyStart=0;     // D6/A0 BUSYの立ち上がり時刻
unsigned long tmDebounceTS=0;    // D6デバウンス用タイムスタンプ
unsigned long a0LastSignalTS=0;  // A0最終検知時刻（保持用）
bool tmBusyPrev=false;           // 前回のBUSY状態（静寂起点検出用）
bool tmBusyFiltered=false;       // D6デバウンス後のBUSY状態
bool a0Detect=false;             // A0ヒステリシス検出フラグ
bool a0Busy=false;               // A0 BUSY確定状態（保持込み）

// PTT制御
unsigned long lastTriggerAt=0;   // 最終ID送出時刻（不応期管理）
unsigned long stateTimer=0;      // ステート遷移タイマー
unsigned long pttMinOn=0;        // PTT最低ON保証の終了時刻
unsigned long nextPeriodicAt=0;  // 次回周期ID送出予定時刻
bool dfpStarted=false;           // DFPlayer再生開始確認フラグ
bool pttOutState=false;          // 現在のPTT出力状態
bool periodicDue=false;          // 周期ID送出待ちフラグ
bool clickWaiting=false;         // テストSWクリック集計中フラグ
bool stopped=false;              // Safe Stop状態フラグ
uint16_t requestedTrack=0;       // 送出待ちトラック番号
uint16_t nextPeriodicTrack=2;    // 次回周期IDトラック（2/3交互）

// 抑止タイマー
unsigned long busySupUntil=0;    // 送信後抑止の終了時刻
unsigned long longSupUntil=0;    // 長話抑止の終了時刻
unsigned long burstWinStart=0;   // バースト検出窓の開始時刻
unsigned long burstSupUntil=0;   // バースト抑止の終了時刻
unsigned long busyHighSince=0;   // DFPlayer BUSY HIGH確定待ち開始時刻
unsigned long playingEnterAt=0;  // PLAYING状態突入時刻（タイムアウト用）

// テストSW
uint8_t  clickCount=0;           // クリック回数カウント
uint8_t  lastSwState=HIGH;       // 前回のSW状態
unsigned long firstClickTime=0;  // 最初のクリック時刻

// AUTO判定カウンタ
unsigned int  burstCount = 0;    // バースト抑止用短発カウンタ
uint16_t d6_edge_count = 0;      // D6エッジ検出数（AUTO判定用）
uint16_t a0_event_count = 0;     // A0イベント検出数（AUTO判定用）

// 静寂ガード
unsigned long lastBusyOffAt = 0; // 最後にBUSYがOFFになった時刻（周期ID静寂起点）

// 送信直後ガード（v1.73e）
bool postTxIgnore = false;          // 送信直後の誤発火防止フラグ
unsigned long postTxIdleStart = 0;  // postTxIgnore解除用アイドル計測開始時刻

/* ============================== Utils ================================= */
inline bool readTmRaw() { return (digitalRead(PIN_TM_BUSY) == HIGH); }

// g0/g1 極性設定を反映したD6 BUSY読み取り
inline bool readTmD6()  { bool rawHigh = readTmRaw(); return TMBUSY_ACTIVE_HIGH ? rawHigh : !rawHigh; }

// 現在のBUSYソース設定に従ってBUSY状態を返す
inline bool readBusy()  {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) return a0Busy;
  return (tmBusyFiltered || a0Busy);  // AUTO: どちらかがBUSYなら BUSY
}

// DFPlayer コマンド送信（チェックサム付き10バイトフレーム）
void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, (uint8_t)(param >> 8), (uint8_t)(param & 0xFF), 0x00, 0x00, 0xEF};
  uint16_t s = 0;
  for (int i = 1; i < 7; i++) s += f[i];
  s = 0xFFFF - s + 1;
  f[7] = (uint8_t)(s >> 8);
  f[8] = (uint8_t)s;
  dfpSerial.write(f, 10);
}

// PTT出力制御（変化があった場合のみ実行）
void setPtt(bool on) {
  if (pttOutState == on) return;
  pttOutState = on;
  digitalWrite(PIN_PTT, on ? HIGH : LOW);
  if (LOG_LEVEL >= LOG_INF) { Serial.print(F("[PTT] ")); Serial.println(on ? F("ON") : F("OFF")); }
}

// PTT送出シーケンス開始（IDLE状態のみ受け付け）
void startPtt(uint16_t trk) {
  if (state != IDLE) return;
  requestedTrack = trk; dfpStarted = false;
  unsigned long now = millis();
  setPtt(true);
  pttMinOn   = now + PTT_PRE_MS + 100;  // 最低ON保証（PRE+マージン）
  stateTimer = now;
  playingEnterAt = 0;
  state = PTT_ON_WAIT;
}

/*
 * 現在の抑止状態を返す。
 * 以下のいずれかが true なら抑止中とみなす：
 *   - postTxIgnore     : 送信直後ガード（v1.73e）
 *   - longSupUntil     : 長話抑止中
 *   - burstSupUntil    : バースト抑止中
 *   - busySupUntil     : 送信後抑止中
 * SUPPRESSORS_ENABLED=false の場合、postTxIgnoreのみ有効。
 */
static inline bool isSuppressedNow(unsigned long now) {
  if (postTxIgnore) return true;
  if (!SUPPRESSORS_ENABLED) return false;
  if ((long)(now - longSupUntil)  < 0) return true;
  if ((long)(now - burstSupUntil) < 0) return true;
  if ((long)(now - busySupUntil)  < 0) return true;
  return false;
}

/* ============================ Defaults/EEPROM ========================== */
/*
 * applyDefaults()：ランタイム変数を工場出荷状態に設定する。
 *
 * 設計方針（DRY原則）：
 *   ランタイム変数の初期化のみを担当し、config構造体への
 *   反映はsaveSettings()に完全委譲する。
 *   config.ver のみ防衛的に設定（saveSettings()でも上書きされるが、
 *   万一saveSettings()呼び出し前に参照された場合の保険）。
 *
 * 呼び出し元：
 *   1. migrateOrInit()：EEPROM未初期化時 → 直後にsaveSettings()を明示呼び出し
 *   2. Fコマンド      ：needSave=trueをセット → whileループ後にsaveSettings()を一括呼び出し
 */
void applyDefaults() {
  BUSY_INPUT_SOURCE = BUSY_SRC_D6;
  SUPPRESSORS_ENABLED = true;
  TX_AFTER_SUPPRESS_ENABLED = true;

  BUSY_MIN_MS = 500; BUSY_MAX_MS = 3900; IDLE_MIN_MS = 200;
  PERIOD_MS   = 30UL * 60UL * 1000UL;
  TX_SUP_MS   = 3000;

  A0_LOW_TH = 300; A0_HIGH_TH = 700; A0_HOLD = 800;
  AUTO_WINDOW = 30UL * 60UL * 1000UL; LONG_TALK_MS = BUSY_MAX_MS;

  DFP_TIMEOUT_MS     = 20000;
  TMBUSY_ACTIVE_HIGH = true;
  PERIOD_QUIET_MS    = 2000;

  autoLocked = false;
  config.ver = CONFIG_VERSION;  // 防衛的コーディング（saveSettings()でも上書き）
}

/*
 * saveSettings()：現在のランタイム変数をconfig構造体に詰め直してEEPROMへ保存。
 * 設定変更コマンド受信後、および applyDefaults() 後に呼ばれる。
 * EEPROM書込みは値変化時のみとなるよう EEPROM.put() の差分書込みを利用。
 */
void saveSettings() {
  config.magic            = CONFIG_MAGIC;
  config.busySrc          = (uint8_t)BUSY_INPUT_SOURCE;
  config.suppressOn       = SUPPRESSORS_ENABLED ? 1 : 0;
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
  config.ver              = CONFIG_VERSION;
  EEPROM.put(0, config);
  if (LOG_LEVEL >= LOG_INF) Serial.println(F("[EEPROM] Settings Saved."));
}

/*
 * migrateOrInit()：起動時にEEPROMを読み込み、状態に応じて初期化または移行を行う。
 *
 * 判定フロー：
 *   1. magic 不一致 → 完全初期化（applyDefaults + saveSettings）
 *   2. ver 不一致   → 自動移行（新規項目を安全値で補完後、ver=4に更新保存）
 *   3. 正常         → そのままランタイム変数へロード
 *
 * 補完ロジックの注意：
 *   periodQuietMs == 0 は「不正値」として 2000ms に補完する。
 *   そのため k0（静寂ガード無効）を設定して再起動すると 2000ms に
 *   リセットされる既知の挙動がある。再起動後は q で確認すること。
 */
void migrateOrInit() {
  EEPROM.get(0, config);

  if (config.magic != CONFIG_MAGIC) {
    Serial.println(F("[EEPROM] No/Other Data. Init defaults."));
    applyDefaults(); saveSettings(); return;
  }

  bool needSave = false;
  if (config.ver != CONFIG_VERSION) {
    Serial.print(F("[EEPROM] Version mismatch: stored="));
    Serial.print(config.ver); Serial.print(F(" expected="));
    Serial.println(CONFIG_VERSION);

    // 旧EEPROMの不正値・未定義項目を安全値で補完
    if (!(config.tmBusyActiveHigh == 0 || config.tmBusyActiveHigh == 1)) {
      config.tmBusyActiveHigh = 1; needSave = true;
    }
    if (config.dfpTimeoutMs > 600000UL) {
      config.dfpTimeoutMs = 20000UL; needSave = true;
    }
    if (config.periodQuietMs == 0 || config.periodQuietMs > 600000UL) {
      config.periodQuietMs = 2000UL; needSave = true;
    }

    config.ver = CONFIG_VERSION; needSave = true;
    if (needSave) EEPROM.put(0, config);
    Serial.println(F("[EEPROM] Migration done."));
  }

  // EEPROMの値をランタイム変数へロード
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
/*
 * printSummary()：現在の全設定値をシリアル出力する（q コマンド）。
 * 現地保守のライフライン。HOLD / AUTO(min) / QUIET(ms) 等を含む全項目を出力。
 */
void printSummary() {
  Serial.print(F("[CFG] EEPROM_VER=")); Serial.print(config.ver);
  Serial.print(F(" SRC="));
  if (BUSY_INPUT_SOURCE == BUSY_SRC_AUTO) Serial.print(F("AUTO"));
  else Serial.print(BUSY_INPUT_SOURCE==BUSY_SRC_D6?F("D6"):F("A0"));
  if (autoLocked) Serial.print(F("(LOCK)"));
  Serial.print(F(" MIN="));        Serial.print(BUSY_MIN_MS);
  Serial.print(F(" MAX="));        Serial.print(BUSY_MAX_MS);
  Serial.print(F(" PER(min)="));   Serial.print(PERIOD_MS/60000UL);
  Serial.print(F(" TXSUP="));      Serial.print(TX_SUP_MS);
  Serial.print(F(" A0[L/H]="));    Serial.print(A0_LOW_TH); Serial.print('/'); Serial.print(A0_HIGH_TH);
  Serial.print(F(" HOLD="));       Serial.print(A0_HOLD);
  Serial.print(F(" AUTO(min)="));  Serial.print(AUTO_WINDOW/60000UL);
  Serial.print(F(" DFP_TIMEOUT(ms)=")); Serial.print(DFP_TIMEOUT_MS);
  Serial.print(F(" TM_BUSY_POL=")); Serial.print(TMBUSY_ACTIVE_HIGH ? F("HIGH=busy") : F("LOW=busy"));
  Serial.print(F(" QUIET(ms)="));  Serial.println(PERIOD_QUIET_MS);
}

void printHelp() {
  Serial.println(F("---- HELP ----"));
  Serial.println(F("m0=D6, m1=A0, m2=AUTO | n###=busyMin, b####=busyMax, i###=idleMin"));
  Serial.println(F("s0/1=sup, t0/1=txSup | p##=period(min), k####=quiet(ms)"));
  Serial.println(F("L###/G###/a####=A0 lo/hi/hold(ms), w##=AUTO(min)"));
  Serial.println(F("d####=DFP timeout | g0/g1=POL | q=summary, v=version, x=STOP"));
  Serial.println(F("H=preset, F=factory reset, Z=SW reset | 0..3=log level"));
}

/* ============================ Command Parser ========================== */
/*
 * handleSerialCmd()：シリアル受信コマンドを解析し、設定を変更する。
 *
 * 最適化（v1.73f）：
 *   旧実装では while ループ内でコマンドごとに saveSettings()/printSummary() を
 *   呼び出していたが、複数コマンドが連続入力された場合に無駄な EEPROM 書込みと
 *   シリアル出力が発生していた。
 *   新実装では needSave/needPrint フラグを使い、while ループ完了後に一括で
 *   saveSettings()/printSummary() を1回だけ呼び出す。
 *
 * rコマンドの二重用途：
 *   - 通常運転中: r#### → 送信後無視時間(TX_SUP_MS)の設定
 *   - STOP中    : r（パラメータなし）→ 復帰コマンド（handleStoppedState内で処理）
 */
void handleSerialCmd() {
  bool needSave  = false;
  bool needPrint = false;

  while (Serial.available()) {
    char c = Serial.read();
    Serial.setTimeout(50);   // ブロッキング防止（parseInt待ちの上限を短縮）
    long val = Serial.parseInt();
    bool chg = true;

    switch (c) {
      case 'm':
        if      (val==0) { BUSY_INPUT_SOURCE=BUSY_SRC_D6;  autoLocked=true; }
        else if (val==1) { BUSY_INPUT_SOURCE=BUSY_SRC_A0;  autoLocked=true; }
        else if (val==2) { BUSY_INPUT_SOURCE=BUSY_SRC_AUTO; autoLocked=false;
                           windowStartTS=millis(); d6_edge_count=0; a0_event_count=0; }
        else chg=false; break;

      case 'b': if (val>=500) { BUSY_MAX_MS=val; LONG_TALK_MS=val; } else chg=false; break;
      case 'n': if (val>=100) BUSY_MIN_MS=val; else chg=false; break;
      case 'i': if (val>=0)   IDLE_MIN_MS=val; else chg=false; break;
      case 's': if (val==0||val==1) SUPPRESSORS_ENABLED=(val==1); else chg=false; break;
      case 't': if (val==0||val==1) TX_AFTER_SUPPRESS_ENABLED=(val==1); else chg=false; break;
      case 'r': if (val>=0)   TX_SUP_MS=val; else chg=false; break;

      case 'p':
        if (val>=0) {
          PERIOD_MS = (unsigned long)val * 60000UL;
          if (PERIOD_MS > 0) nextPeriodicAt = millis() + PERIOD_MS;
          else periodicDue = false;
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

      // vコマンド：スケッチバージョンとEEPROM_VERを表示（v1.73f追加）
      case 'v':
        Serial.print(F("[VER] OpenCCVoice v1.73f (EEPROM_VER="));
        Serial.print(config.ver);
        Serial.println(F(")"));
        chg=false; break;

      case 'q': needPrint = true; chg=false; break;

      case 'H':
        SUPPRESSORS_ENABLED=false; TX_AFTER_SUPPRESS_ENABLED=false;
        BUSY_MAX_MS=3900; LONG_TALK_MS=3900; break;

      case 'x': stopped = true; chg=false; Serial.println(F("[STOP]")); break;

      // Fコマンド：Factory Reset
      // applyDefaults()でランタイム変数を初期化 → needSave=true →
      // whileループ後のsaveSettings()で構造体反映＋EEPROM保存
      case 'F': applyDefaults(); needSave=true; Serial.println(F("[RESET]")); chg=false; break;

      // Zコマンド：ソフトウェアリセット（v1.73f追加）
      // EEPROMは変更せず、WDTによるハードリセットでMCUを再起動する。
      // delay(100)でシリアル出力を確実に吐き出してからWDT発火。
      // PTT出力中に実行するとPTTが即解放されるため、意図せず実行しないこと。
      case 'Z':
        Serial.println(F("[RESET] Software reset by WDT..."));
        delay(100);
        wdt_enable(WDTO_15MS);
        while(1) {}  // WDT発火待ち（到達しないが念のため）
        break;

      case 'h': printHelp(); chg=false; break;
      case '0': case '1': case '2': case '3': LOG_LEVEL=(LogLvl)(c-'0'); chg=false; break;
      default: chg=false; break;
    }
    if (chg) { needSave = true; needPrint = true; }
  }

  // whileループ完了後に一括で保存・表示（重複呼び出しを防ぐ）
  if (needSave)  saveSettings();
  if (needPrint) printSummary();
}

/* ============================== Logic Modules ========================= */

/*
 * updateInputs()：各入力を読み取り、状態変数を更新する。
 *
 * - D7→D2ミラー出力更新
 * - D6デバウンス処理
 * - A0ヒステリシス+保持処理
 *   ★抑止中でもa0Busyの観測は継続（v1.73e）
 *   　AUTOカウント(a0_event_count)のみ抑止中は増加しない
 * - postTxIgnoreガード解除判定（v1.73e）
 *   BUSYがIDLE_MIN_MS以上連続OFFになった時点で解除
 * - lastBusyOffAt の更新（周期ID静寂起点）
 */
void updateInputs(unsigned long now, bool pAct) {
  // D7→D2ミラー（DFPlayer BUSY反転出力）
  bool dLow = (digitalRead(PIN_DFP_BSY) == LOW);
  digitalWrite(PIN_DFP_OUT, DFP_BUSYOUT_INVERT ? (dLow ? HIGH : LOW) : (dLow ? LOW : HIGH));

  // D6デバウンス
  bool rTm = readTmD6();
  if (rTm != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) {
    tmBusyFiltered = rTm; tmDebounceTS = now;
    if (tmBusyFiltered) d6_edge_count++;
  }

  // A0判定（抑止中でもBUSY状態観測は継続、AUTOカウントのみ停止）
  if (!pAct) {
    bool supNow = isSuppressedNow(now);
    int v = analogRead(A0_PIN);
    if (!a0Detect && v < A0_LOW_TH) {
      a0Detect = true;
    } else if (a0Detect && v > A0_HIGH_TH) {
      a0Detect = false;
      if (!supNow) a0_event_count++;  // 抑止中はカウントしない
    }
    if (a0Detect) a0LastSignalTS = now;
    a0Busy = a0Detect || (a0LastSignalTS && (now - a0LastSignalTS < A0_HOLD));
  } else {
    a0Detect = false; a0Busy = false;
  }

  bool rB = readBusy();

  // 送信直後ガード解除（v1.73e）
  // BUSYがIDLE_MIN_MS以上連続OFFになったら postTxIgnore を解除する
  if (postTxIgnore) {
    if (!rB) {
      if (postTxIdleStart == 0) postTxIdleStart = now;
      if (now - postTxIdleStart >= IDLE_MIN_MS) {
        postTxIgnore    = false;
        postTxIdleStart = 0;
        if (LOG_LEVEL >= LOG_DBG) Serial.println(F("[POST-TX] cleared"));
      }
    } else {
      postTxIdleStart = 0;  // BUSY再来でリセット
    }
  }

  // BUSY→OFFの瞬間に静寂起点を更新
  if (!rB && tmBusyPrev) lastBusyOffAt = now;
  tmBusyPrev = rB;
}

/*
 * updateLEDs()：受信ソース・抑止状態に応じてLEDを更新する。
 * 未使用側LEDはゆっくり点滅（待機表示）。
 * AUTO固定直後はD13を高速点滅（約3秒）。
 */
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
 * processBusyLogic()：受信長を判定し、短発ID送出・各種抑止タイマーを管理する。
 *
 * 長話抑止：dur >= BUSY_MAX_MS → LONG_SUP_MS(10s) 抑止
 * バースト抑止：10s窓内に短発2回以上 → BURST_SUP_MS(10s) 抑止
 * 短発ID送出：BUSY_MIN_MS <= dur < BUSY_MAX_MS かつ不応期外かつ抑止外
 */
void processBusyLogic(unsigned long now, bool pAct, bool rB) {
  if (!pAct && !isSuppressedNow(now)) {
    if (rB) {
      if (tmBusyStart == 0) tmBusyStart = now;
    } else {
      if (tmBusyStart != 0) {
        unsigned long dur = now - tmBusyStart;
        tmBusyStart = 0;

        if (SUPPRESSORS_ENABLED) {
          if (dur >= BUSY_MAX_MS) longSupUntil = now + LONG_SUP_MS;
          if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
            if (burstWinStart==0 || (now - burstWinStart >= BURST_WIN_MS)) {
              burstWinStart=now; burstCount=0;
            }
            if (++burstCount >= BURST_TH) burstSupUntil = now + BURST_SUP_MS;
          }
        }

        bool allowed = !(SUPPRESSORS_ENABLED &&
                         ((long)(now-longSupUntil)<0 || (long)(now-burstSupUntil)<0));
        if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS &&
            (now - lastTriggerAt >= REFRAC_MS) && allowed) {
          startPtt(1); lastTriggerAt = now;
        }
      }
    }
  } else {
    tmBusyStart = 0;
  }
}

/*
 * processPeriodicId()：周期IDスケジューラ。
 *
 * catch-up対策（v1.73e）：
 *   whileループでnextPeriodicAtを未来へ追い付かせる。
 *   長時間延期後も periodicDue は最大1回のみ立てる。
 *
 * 送出条件（v1.73d/e）：
 *   1. periodicDue == true
 *   2. state == IDLE
 *   3. BUSY == false
 *   4. (now - lastBusyOffAt) >= PERIOD_QUIET_MS（静寂条件）
 *   5. isSuppressedNow() == false（送信直後ガード含む）
 * 条件不成立 → periodicDue を保持（延期）
 */
void processPeriodicId(unsigned long now, bool rB) {
  if (PERIOD_MS > 0) {
    bool crossed = false;
    while ((long)(now - nextPeriodicAt) >= 0) {
      nextPeriodicAt += PERIOD_MS;
      crossed = true;
    }
    if (crossed) periodicDue = true;
  } else {
    periodicDue = false;
  }

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
}

/*
 * processStateMachine()：PTT送出ステートマシン。
 *
 * IDLE → PTT_ON_WAIT（PTT ON + PRE待ち）
 *       → PLAYING（DFPlayer再生中）
 *       → PTT_OFF_WAIT（再生完了 + POST待ち）
 *       → IDLE（PTT OFF + postTxIgnore セット）
 *
 * PTT_OFF_WAIT→IDLE 時：
 *   - postTxIgnore=true をセット（送信直後ガード開始）
 *   - lastTriggerAt=now を更新（不応期の起点を送信直後に再設定）
 *   - busySupUntil を設定（送信後抑止）
 */
void processStateMachine(unsigned long now) {
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
      if (now - stateTimer >= PTT_POST_MS && (long)(now - pttMinOn) >= 0) {
        setPtt(false);
        state = IDLE;
        postTxIgnore    = true;  // 送信直後ガード開始（v1.73e）
        postTxIdleStart = 0;
        lastTriggerAt   = now;   // 不応期の起点を送信直後に更新
        if (SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED)
          busySupUntil = now + TX_SUP_MS;
      } break;

    case IDLE:
    default: break;
  }

  // PTT最低ON保証ガード
  if ((long)(now - pttMinOn) < 0) setPtt(true);
}

/*
 * processTestSwitch()：D3テストSWの1〜3クリックを検出し、対応トラックを送出する。
 * 1クリック=001、2クリック=002、3クリック=003
 */
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

/*
 * maybeAuto()：AUTO観測窓が終了したらD6/A0の優位側へ固定する。
 * 固定後はautoLocked=trueとなり、以降はこの関数は何もしない。
 * 固定内容はEEPROMへ保存され、再起動後も反映される。
 */
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

/*
 * handleStoppedState()：Safe Stop状態のループ処理。
 * 全出力をOFFにしてシリアル入力を監視。
 * 'r' 受信で stopped=false となり通常動作に復帰。
 */
void handleStoppedState() {
  setPtt(false);
  digitalWrite(PIN_BUSY_LED, LOW);
  digitalWrite(PIN_A0_LED,   LOW);
  digitalWrite(PIN_SUP_LED,  LOW);
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') { stopped = false; Serial.println(F("[RESUME]")); }
  }
  delay(5);
}

/* ============================ setup / loop ============================ */
void setup() {
  Serial.begin(115200);
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
  dfpSend(0x06, 20);  // DFPlayer 音量設定（0〜30）

  unsigned long now = millis();
  nextPeriodicAt = (PERIOD_MS > 0) ? now + PERIOD_MS : 0;
  windowStartTS  = now;
  lastBusyOffAt  = now;  // 起動直後は静寂開始とみなす

  Serial.println(F("[START] OpenCCVoice v1.73f Unified/Safe (Refactored)"));
  printSummary();
  printHelp();
}

/*
 * loop()：メインループ。
 * 各処理を機能別関数に委譲し、可読性・保守性を向上させている。
 *
 * 処理順序：
 *   1. handleSerialCmd()     シリアルコマンド処理
 *   2. handleStoppedState()  Safe Stop中はここで return
 *   3. updateInputs()        入力読み取り・状態更新
 *   4. updateLEDs()          LED表示更新
 *   5. processBusyLogic()    受信長判定・短発ID・抑止制御
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

  // pAct：PTT送出シーケンス中、またはPTT最低ON保証中
  bool pAct = (state != IDLE) || ((long)(now - pttMinOn) < 0);

  updateInputs(now, pAct);

  bool rB = readBusy();

  updateLEDs(now, rB);
  processBusyLogic(now, pAct, rB);
  processPeriodicId(now, rB);
  processStateMachine(now);
  processTestSwitch(now);
  maybeAuto(now);
}
