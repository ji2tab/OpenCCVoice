
/************************************************************
 * OpenCCVoice ID Guidance Controller
 * Version : 1.72-final-EEPROM+F (Factory Reset対応)
 *
 * 【操作・設定ガイド】
 * シリアルモニタ(115200bps)から以下のコマンドで設定変更が可能です。
 * 設定した内容はEEPROMに保存され、電源を切っても維持されます。
 *
 *  [モード切替]  m0:D6固定, m1:A0固定, m2:AUTO(判定後に固定)
 *  [判定秒数]    b1500:上限1.5s, n500:下限0.5s (b=MAX, n=MIN)
 *  [抑止機能]    s1:ON, s0:OFF (s=Suppress)
 *  [送信後無視]  t1:ON, t0:OFF (t=Tx-after-suppress)
 *  [各種時間]    p30:周期ID30分, w30:AUTO窓30分, r3000:送信後無視3s
 *  [感度調整]    L300:A0下限, G700:A0上限, a800:A0保持(ms)
 *  [その他]      q:設定表示, H:H1プリセット, x:システム停止, r:再開, F:初期値へ
 *
 * 【用語補足】
 *  - n####（最短時間 / MIN）: これ未満はノイズ等として無視します。
 *  - b####（最長時間 / MAX）: これ以上は「会話（ロングトーク）」とみなし、IDは出しません。
 *    ※ b#### は LONG_TALK_MS と統合されています（同一値）。
 *  - L/G（検知開始/終了）  : A0のアナログ電圧のヒステリシスしきい値です。
 *  - a####（保持）          : A0の瞬断を吸収するための受信継続保持時間(ms)です。
 *
 * 【注意】
 *  - A0とD6は必ずどちらか一方のみ配線（同時接続不可）。
 *  - DMR/SFR運用ではアナログ音声IDは乗らないため、FMでテストしてください。
 ************************************************************/

#include <SoftwareSerial.h>
#include <EEPROM.h>

/* ============================================================
 * ■ EEPROM保存用データ構造
 * ============================================================ */
struct MyConfig {
  uint32_t magic;      
  uint8_t  busySrc;    // [m] 0:D6, 1:A0, 2:AUTO
  uint8_t  suppressOn; // [s] 0:OFF, 1:ON
  uint8_t  txAfSupOn;  // [t] 0:OFF, 1:ON
  uint32_t busyMin;    // [n] 判定下限(ms)
  uint32_t busyMax;    // [b] 判定上限(ms)（＝LONGと統合）
  uint32_t idleMin;    // [i] 直前無音(ms)
  uint32_t periodMin;  // [p] 周期ID間隔(分)
  uint32_t txSupMs;    // [r] 送信後無視(ms)
  int      a0Low;      // [L] A0検知開始レベル
  int      a0High;     // [G] A0検知終了レベル
  uint32_t a0Hold;     // [a] A0保持時間(ms)
  uint32_t autoWinMin; // [w] 判定窓時間(分)
} config;

const uint32_t CONFIG_MAGIC = 0xDEADBEEF;

/* ============================================================
 * ■ 動作パラメータ（コマンドで変更可能な変数）
 *    [CMD] の対応を各行に明記
 * ============================================================ */
// [CMD: m0/m1/m2] 受信ソース（2番は判定後にD6/A0へ自動固定）
enum BusySrc { BUSY_SRC_D6, BUSY_SRC_A0, BUSY_SRC_AUTO };
volatile BusySrc BUSY_INPUT_SOURCE;

// [CMD: s0/s1] 長会話やバースト時の抑止機能 ON/OFF
bool SUPPRESSORS_ENABLED;

// [CMD: t0/t1] 送信終了後に一定時間(rコマンドの値)受信を無視する ON/OFF
bool TX_AFTER_SUPPRESS_ENABLED;

// [CMD: n####] 有効な受信とみなす最短時間(ms)
unsigned long BUSY_MIN_MS;

// [CMD: b####] これを超えるとIDを出さない最大時間(ms)。長会話判定（LONG）と連動
unsigned long BUSY_MAX_MS;

// [CMD: i####] 受信開始前に必要な無音(ms)。カブセ防止（※現時点は未使用）
unsigned long IDLE_MIN_MS;

// [CMD: p####] 誰もいない時に自動でIDを出す間隔(分)
unsigned long PERIOD_MS;

// [CMD: r####] 送信終了後の受信無視（リカバリ）時間(ms)
unsigned long TX_SUP_MS;

// [CMD: L####] A0音声検知を開始する電圧しきい値(0-1023)
int A0_LOW_TH;

// [CMD: G####] A0音声検知を終了する電圧しきい値(0-1023)
int A0_HIGH_TH;

// [CMD: a####] 音が途切れても受信継続とみなす保持時間(ms)
unsigned long A0_HOLD;

// [CMD: w####] AUTOモードでの判定確定までの集計期間(分)
unsigned long AUTO_WINDOW;

// (内部連動) 長会話とみなす秒数（bコマンドに連動＝常に BUSY_MAX_MS と同一）
unsigned long LONG_TALK_MS;

/* ============================================================
 * ■ 物理ピン・内部固定定数（変更不可）
 * ============================================================ */
const bool TM_BUSY_ACTIVE_HIGH = true; // D6信号がHIGHで受信ならtrue
const bool DFP_BUSY_OUT_INVERT = true; // ミラー出力の論理反転

const uint8_t PIN_TEST_SW = 3;    // D3: テストSW（GNDに落とす）
const uint8_t PIN_BUSY_LED = 4;   // D4: D6系LED
const uint8_t PIN_PTT     = 5;    // D5: PTT出力
const uint8_t PIN_TM_BUSY = 6;    // D6: 無線機BUSY入力
const uint8_t PIN_DFP_BSY = 7;    // D7: DFPlayer BUSY（LOW=再生中）
const uint8_t PIN_DFP_OUT = 2;    // D2: DFPlayer BUSYミラー出力
const uint8_t PIN_SUP_LED = 13;   // D13: 抑止状態表示LED
const uint8_t PIN_A0_LED  = 12;   // D12: A0系LED
const uint8_t PIN_DFP_TX  = 10;   // DFPへ送る（Arduino TX）
const uint8_t PIN_DFP_RX  = 11;   // DFPから受ける（Arduino RX）
const uint8_t A0_PIN      = A0;   // A0: アナログ入力

// A0フロントエンド利用フラグ
#define USE_A0_FRONT 1

// SoftwareSerial の引数順は (RX, TX)
SoftwareSerial dfpSerial(PIN_DFP_RX, PIN_DFP_TX);

// 抑止継続時間やチャタリング防止用の固定値
const unsigned long REFRAC_MS    = 3000;  // 再トリガ抑制
const unsigned long DEBOUNCE_MS  = 5;     // デバウンス
const unsigned long PTT_PRE_MS   = 1000;  // PTTオン→音出し開始までのプリ
const unsigned long PTT_POST_MS  = 1000;  // 音止→PTTオフまでのポスト
const unsigned long LONG_SUP_MS  = 10000; // 長会話後の抑止継続
const unsigned long BURST_WIN_MS = 10000; // バースト判定窓
const unsigned int  BURST_TH     = 2;     // バースト回数しきい値
const unsigned long BURST_SUP_MS = 10000; // バースト抑止継続

/* ============================================================
 * ■ システム状態管理変数
 * ============================================================ */
unsigned long windowStartTS = 0, autoSwitchBlinkUntil = 0;
bool autoLocked = false; // m0/m1で固定、m2(AUTO)完了後も固定

enum LogLvl { LOG_NONE=0, LOG_ERR=1, LOG_INF=2, LOG_DBG=3 };
volatile LogLvl LOG_LEVEL = LOG_INF;

enum State { IDLE, PTT_ON_WAIT, PLAYING, PTT_OFF_WAIT };
State state = IDLE;

unsigned long tmBusyStart=0, tmDebounceTS=0, a0LastSignalTS=0;
bool tmBusyPrev=false, tmBusyFiltered=false, a0Detect=false, a0Busy=false;

unsigned long lastTriggerAt=0, stateTimer=0, pttMinOn=0, nextPeriodicAt=0;
bool dfpStarted=false, lastBusyLow=false, pttOutState=false;
bool periodicDue=false, clickWaiting=false, stopped=false;

uint16_t requestedTrack=0, nextPeriodicTrack=2;
unsigned long busySupUntil=0, longSupUntil=0, burstWinStart=0, burstSupUntil=0;
unsigned long busyHighSince=0;

uint8_t clickCount=0, lastSwState=HIGH;
unsigned long firstClickTime=0;

// バースト回数カウンタ
unsigned int burstCount = 0;

// AUTO 判定用カウンタ
uint16_t d6_edge_count = 0;
uint16_t a0_event_count = 0;

/* ============================================================
 * ■ ユーティリティ
 * ============================================================ */
inline bool readTmRaw() { return (digitalRead(PIN_TM_BUSY) == HIGH); }
inline bool readTmD6()  { return TM_BUSY_ACTIVE_HIGH ? readTmRaw() : !readTmRaw(); }
inline bool readBusy()  {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) return a0Busy;
  // AUTO中の受信「代表」はD6/A0のいずれかで良いが、視認性を優先してOR
  return (tmBusyFiltered || a0Busy);
}

void formatUptime(unsigned long ms, char* buf, size_t n) {
  unsigned long total_ms = ms;
  unsigned long h = total_ms / 3600000UL; total_ms %= 3600000UL;
  unsigned long m = total_ms / 60000UL;   total_ms %= 60000UL;
  unsigned long s = total_ms / 1000UL;
  unsigned long mm = total_ms % 1000UL;
  snprintf(buf, n, "%02lu:%02lu:%02lu.%03lu", h, m, s, mm);
}

void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E,0xFF,0x06,cmd,0x00,(uint8_t)(param>>8),(uint8_t)param,0,0,0xEF};
  uint16_t s = 0; for (int i = 1; i < 7; i++) s += f[i];
  s = 0xFFFF - s + 1; f[7] = (uint8_t)(s>>8); f[8] = (uint8_t)s;
  dfpSerial.write(f, 10);
}

void setPtt(bool on) {
  if (pttOutState == on) return;
  pttOutState = on; digitalWrite(PIN_PTT, on ? HIGH : LOW);
  if (LOG_LEVEL >= LOG_INF) {
    Serial.print(F("[PTT] ")); Serial.println(on?F("ON"):F("OFF"));
  }
}

void startPtt(uint16_t trk) {
  if (state != IDLE) return;
  requestedTrack = trk; dfpStarted = false; lastBusyLow = false;
  unsigned long now = millis(); setPtt(true);
  pttMinOn = now + PTT_PRE_MS + 100;
  stateTimer = now; state = PTT_ON_WAIT;
}

/* ============================================================
 * ■ EEPROM・設定入出力
 * ============================================================ */
void saveSettings() {
  config.magic      = CONFIG_MAGIC;
  config.busySrc    = (uint8_t)BUSY_INPUT_SOURCE;
  config.suppressOn = SUPPRESSORS_ENABLED ? 1 : 0;
  config.txAfSupOn  = TX_AFTER_SUPPRESS_ENABLED ? 1 : 0;
  config.busyMin    = BUSY_MIN_MS;
  config.busyMax    = BUSY_MAX_MS;
  config.idleMin    = IDLE_MIN_MS;
  config.periodMin  = PERIOD_MS / 60000UL;
  config.txSupMs    = TX_SUP_MS;
  config.a0Low      = A0_LOW_TH;
  config.a0High     = A0_HIGH_TH;
  config.a0Hold     = A0_HOLD;
  config.autoWinMin = AUTO_WINDOW / 60000UL;
  EEPROM.put(0, config);
  if (LOG_LEVEL >= LOG_INF) Serial.println(F("[EEPROM] Settings Saved."));
}

void applyDefaults() {
  BUSY_INPUT_SOURCE = BUSY_SRC_D6; SUPPRESSORS_ENABLED = true;
  TX_AFTER_SUPPRESS_ENABLED = true;
  BUSY_MIN_MS = 500; BUSY_MAX_MS = 1500;
  IDLE_MIN_MS = 200; PERIOD_MS = 30UL * 60UL * 1000UL; TX_SUP_MS = 3000;
  A0_LOW_TH = 300; A0_HIGH_TH = 700; A0_HOLD = 800; AUTO_WINDOW = 30UL * 60UL * 1000UL;
  LONG_TALK_MS = BUSY_MAX_MS;
  autoLocked = false;
}

void loadSettings() {
  EEPROM.get(0, config);
  if (config.magic != CONFIG_MAGIC) {
    Serial.println(F("[EEPROM] No Data. Using Defaults..."));
    applyDefaults();
  } else {
    BUSY_INPUT_SOURCE = (BusySrc)config.busySrc;
    SUPPRESSORS_ENABLED = (config.suppressOn == 1);
    TX_AFTER_SUPPRESS_ENABLED = (config.txAfSupOn == 1);
    BUSY_MIN_MS = config.busyMin; BUSY_MAX_MS = config.busyMax;
    IDLE_MIN_MS = config.idleMin; PERIOD_MS = (unsigned long)config.periodMin * 60000UL;
    TX_SUP_MS = config.txSupMs; A0_LOW_TH = config.a0Low; A0_HIGH_TH = config.a0High;
    A0_HOLD = config.a0Hold; AUTO_WINDOW = (unsigned long)config.autoWinMin * 60000UL;
    LONG_TALK_MS = BUSY_MAX_MS;
    autoLocked = (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO);
    Serial.println(F("[EEPROM] Settings Loaded."));
  }
}

/* ============================================================
 * ■ 表示系
 * ============================================================ */
void printSummary() {
  Serial.print(F("[CFG] SRC="));
  if (BUSY_INPUT_SOURCE == BUSY_SRC_AUTO) Serial.print(F("AUTO"));
  else Serial.print(BUSY_INPUT_SOURCE==BUSY_SRC_D6?F("D6"):F("A0"));
  if (autoLocked) Serial.print(F("(LOCK)"));

  Serial.print(F(" MIN="));  Serial.print(BUSY_MIN_MS);
  Serial.print(F(" MAX="));  Serial.print(BUSY_MAX_MS);
  Serial.print(F(" LONG=")); Serial.print(LONG_TALK_MS);
  Serial.print(F(" PER(min)=")); Serial.print(PERIOD_MS/60000UL);
  Serial.print(F(" TXSUP=")); Serial.print(TX_SUP_MS);
  Serial.print(F(" A0[L/H]=")); Serial.print(A0_LOW_TH); Serial.print('/'); Serial.print(A0_HIGH_TH);
  Serial.print(F(" HOLD=")); Serial.print(A0_HOLD);
  Serial.print(F(" AUTO(min)=")); Serial.println(AUTO_WINDOW/60000UL);
}

/* ============================================================
 * ■ コマンド解析（変更時にEEPROMへ保存）
 * ============================================================ */
void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();

    // 短い待ち（mの次の数字など）
    unsigned long timeout = millis() + 150;
    while (!Serial.available() && millis() < timeout) { /* wait */ }
    long val = Serial.parseInt();
    bool chg = true;

    switch (c) {
      case 'm': 
        if      (val==0) { BUSY_INPUT_SOURCE=BUSY_SRC_D6;  autoLocked=true;  }
        else if (val==1) { BUSY_INPUT_SOURCE=BUSY_SRC_A0;  autoLocked=true;  }
        else if (val==2) { BUSY_INPUT_SOURCE=BUSY_SRC_AUTO;autoLocked=false; windowStartTS=millis(); d6_edge_count=0; a0_event_count=0; }
        else chg=false;
        break;

      case 'b': if (val>=500) { BUSY_MAX_MS=val; LONG_TALK_MS=val; } else chg=false; break;
      case 'n': if (val>=100) BUSY_MIN_MS=val; else chg=false; break;

      case 's': if (val==0||val==1) SUPPRESSORS_ENABLED=(val==1); else chg=false; break;
      case 't': if (val==0||val==1) TX_AFTER_SUPPRESS_ENABLED=(val==1); else chg=false; break;

      case 'i': if (val>=0) IDLE_MIN_MS=val; else chg=false; break;
      case 'p': if (val>=1) PERIOD_MS = (unsigned long)val * 60UL * 1000UL; else chg=false; break;
      case 'r': if (val>=0) TX_SUP_MS = (unsigned long)val; else chg=false; break;

      case 'L': A0_LOW_TH  = (int)val; break;
      case 'G': A0_HIGH_TH = (int)val; break;
      case 'a': if (val>=0) A0_HOLD = (unsigned long)val; else chg=false; break;
      case 'w': if (val>=1) { AUTO_WINDOW = (unsigned long)val * 60UL * 1000UL; windowStartTS=millis(); d6_edge_count=0; a0_event_count=0; } else chg=false; break;

      case 'q': printSummary(); chg=false; break;

      // H1クイックプリセット（フィールド簡易運用向け）
      case 'H': SUPPRESSORS_ENABLED=false; TX_AFTER_SUPPRESS_ENABLED=false; BUSY_MAX_MS=4000; LONG_TALK_MS=4000; break;

      // 安全停止（停止中は 'r' で再開）
      case 'x': {
        char up[20]; formatUptime(millis(), up, sizeof(up));
        Serial.print(F("[STOP] uptime=")); Serial.println(up);
        stopped = true; chg=false; break;
      }

      // Factory Reset（★追加）
      case 'F': {
        applyDefaults();
        saveSettings();
        Serial.println(F("[RESET] Factory defaults restored."));
        chg=false; // すでに save 済み
        break;
      }

      // ログレベル（任意運用）
      case '0': case '1': case '2': case '3':
        LOG_LEVEL = (LogLvl)(c - '0'); chg=false; break;

      default: chg=false; break;
    }

    if (chg) { saveSettings(); printSummary(); }
  }
}

/* ============================================================
 * ■ AUTO判定（m2: AUTO → 判定後に固定）
 * ============================================================ */
const uint16_t D6_EDGE_MIN = 10;
const uint16_t A0_EVENT_MIN = 20;

void maybeAuto(unsigned long now) {
  if (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO || autoLocked) return;
  if ((long)(now - windowStartTS) >= (long)AUTO_WINDOW) {
    BusySrc n;
    uint16_t d6 = d6_edge_count, a0 = a0_event_count;

    if      (d6 < D6_EDGE_MIN && a0 >= A0_EVENT_MIN) n = BUSY_SRC_A0;
    else if (a0 < A0_EVENT_MIN && d6 >= D6_EDGE_MIN) n = BUSY_SRC_D6;
    else n = (d6 >= a0) ? BUSY_SRC_D6 : BUSY_SRC_A0;

    BUSY_INPUT_SOURCE = n; autoLocked = true;
    autoSwitchBlinkUntil = now + 3000; // 切替通知（D13点滅）
    saveSettings(); // 固定化を保存

    Serial.print(F("[AUTO-FIXED] Lock to "));
    Serial.println(n==BUSY_SRC_D6?F("D6"):F("A0"));
  }
}

/* ============================================================
 * ■ セットアップ＆メインループ
 * ============================================================ */
void setup() {
  Serial.begin(115200);
  dfpSerial.begin(9600);

  loadSettings();

  pinMode(PIN_TEST_SW, INPUT_PULLUP);
  pinMode(PIN_BUSY_LED, OUTPUT);
  pinMode(PIN_PTT, OUTPUT);

  pinMode(PIN_TM_BUSY, INPUT);    // 外部回路でレベル確定前提（必要に応じて INPUT_PULLUP へ）
  pinMode(PIN_DFP_BSY, INPUT);    // 同上
  pinMode(PIN_DFP_OUT, OUTPUT);

  pinMode(PIN_SUP_LED, OUTPUT);
  pinMode(PIN_A0_LED, OUTPUT);

  delay(500);
  dfpSend(0x06, 20); // DFPlayer 音量初期値

  unsigned long now = millis();
  nextPeriodicAt = now + PERIOD_MS;
  windowStartTS = now;

  Serial.println(F("[START] v1.72-final-EEPROM+F"));
  printSummary();
}

void loop() {
  unsigned long now = millis();
  handleSerialCmd();

  // 停止モード：出力/LEDを落として 'r' 待ち
  if (stopped) {
    setPtt(false);
    digitalWrite(PIN_BUSY_LED, LOW);
    digitalWrite(PIN_A0_LED,   LOW);
    digitalWrite(PIN_SUP_LED,  LOW);
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'r') {
        stopped = false;
        Serial.println(F("[RESUME]"));
      }
    }
    delay(5);
    return;
  }

  bool pAct = (state != IDLE) || ((long)now < (long)pttMinOn);

  // DFPlayer BUSY ミラー出力
  bool dLow = (digitalRead(PIN_DFP_BSY) == LOW);
  digitalWrite(PIN_DFP_OUT, DFP_BUSY_OUT_INVERT ? (dLow ? HIGH : LOW)
                                                : (dLow ? LOW  : HIGH));

  // D6 取得（デバウンス、立上りのみカウント）
  bool rTm = readTmD6();
  if (rTm != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) {
    tmBusyFiltered = rTm; tmDebounceTS = now;
    if (tmBusyFiltered) d6_edge_count++; // 立上りのみをイベントとする
  }

  // A0 取得（抑止期間は無効化）
#if USE_A0_FRONT
  if (!pAct && (long)(now - busySupUntil) >= 0) {
    int v = analogRead(A0_PIN);
    if (!a0Detect && v < A0_LOW_TH) a0Detect = true;
    else if (a0Detect && v > A0_HIGH_TH) { a0Detect = false; a0_event_count++; }
    if (a0Detect) a0LastSignalTS = now;
    a0Busy = a0Detect || (a0LastSignalTS!=0 && (now - a0LastSignalTS < A0_HOLD));
  } else { a0Detect = false; a0Busy = false; }
#endif

  // LED（未使用側は点滅/AUTO時は双方点滅＋生反応）
  bool bl = ((now / 500) % 2 == 0);
  bool rB = readBusy();

  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) {
    digitalWrite(PIN_BUSY_LED, rB ? HIGH : LOW);                       // D6系：実反応
    digitalWrite(PIN_A0_LED,   a0Busy ? HIGH : (bl ? HIGH : LOW));     // A0系：未使用→点滅
  } else if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) {
    digitalWrite(PIN_A0_LED,   rB ? HIGH : LOW);                       // A0系：実反応
    digitalWrite(PIN_BUSY_LED, tmBusyFiltered ? HIGH : (bl ? HIGH : LOW)); // D6系：未使用→点滅
  } else { // AUTO
    digitalWrite(PIN_BUSY_LED, tmBusyFiltered ? HIGH : (bl ? HIGH : LOW));
    digitalWrite(PIN_A0_LED,   a0Busy         ? HIGH : (bl ? HIGH : LOW));
  }

  // 受信イベント → 判定ロジック
  if (!pAct && !(SUPPRESSORS_ENABLED && (long)(now - busySupUntil) < 0)) {
    if (rB) { 
      if (!tmBusyPrev) tmBusyStart = now;
    } else if (tmBusyPrev) {
      unsigned long dur = now - tmBusyStart;

      if (SUPPRESSORS_ENABLED) {
        if (dur >= BUSY_MAX_MS) longSupUntil = now + LONG_SUP_MS; // 長会話抑止
        if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
          if (burstWinStart==0 || (now-burstWinStart >= BURST_WIN_MS)) {
            burstWinStart=now; burstCount=0;
          }
          if (++burstCount >= BURST_TH) burstSupUntil = now + BURST_SUP_MS; // バースト抑止
        }
      }

      bool allowed = !(SUPPRESSORS_ENABLED &&
                       ((long)(now-longSupUntil)<0 || (long)(now-burstSupUntil)<0));
      if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS &&
          (now-lastTriggerAt >= REFRAC_MS) && allowed) {
        startPtt(1);
        lastTriggerAt = now;
      }
    }
  }
  tmBusyPrev = rB;

  // 周期ID
  if ((long)(now - nextPeriodicAt) >= 0) { periodicDue = true; nextPeriodicAt += PERIOD_MS; }
  if (periodicDue && state == IDLE) {
    if (LOG_LEVEL >= LOG_INF) Serial.println(F("[EVT] Periodic ID"));
    startPtt(nextPeriodicTrack);
    nextPeriodicTrack = (nextPeriodicTrack==2?3:2);
    periodicDue = false;
  }

  // 抑止表示LED（AUTO切替時は3秒点滅通知）
  bool s = SUPPRESSORS_ENABLED &&
           ((long)(now-longSupUntil)<0 || (long)(now-burstSupUntil)<0 || (long)(now-busySupUntil)<0);
  digitalWrite(PIN_SUP_LED, (long)(autoSwitchBlinkUntil-now)>0 ? ((now/250)%2==0?HIGH:LOW)
                                                              : (s?HIGH:LOW));

  // ステートマシン
  switch (state) {
    case PTT_ON_WAIT:
      if (now-stateTimer >= PTT_PRE_MS) {
        dfpSend(0x03, requestedTrack);
        state = PLAYING;
      }
      break;

    case PLAYING:
      if (dLow) {
        if (!lastBusyLow && LOG_LEVEL >= LOG_INF) Serial.println(F("[DFP] Start"));
        lastBusyLow = true; dfpStarted = true; busyHighSince = 0;
      } else {
        if (dfpStarted) {
          if (busyHighSince == 0) busyHighSince = now;
          if (now - busyHighSince >= 40) {
            state = PTT_OFF_WAIT; stateTimer = now;
            if (LOG_LEVEL >= LOG_INF) Serial.println(F("[DFP] End"));
          }
        }
      }
      break;

    case PTT_OFF_WAIT:
      if (now-stateTimer >= PTT_POST_MS && now >= pttMinOn) {
        setPtt(false); state = IDLE;
        if (SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED)
          busySupUntil = now + TX_SUP_MS;
      }
      break;
  }

  // 最低ONガード
  if ((long)now < (long)pttMinOn) setPtt(true);

  // テスト送信ボタン（1〜3クリック＝トラック1〜3送出）
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

  // AUTO（m2）
  maybeAuto(now);
}
