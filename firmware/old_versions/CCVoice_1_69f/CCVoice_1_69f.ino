
/************************************************************
 * TM-8250 ID Guidance Controller (Unified & Configurable)
 * Version : 1.69g-unified-H1 (H1カーチャンク即復旧プリセット付き, hotfix)
 * Target  : Arduino Nano (ATmega328P, 5V)
 *
 * 【本版の目的：H1カーチャンクの“dur=2.3〜3.9s”環境で、
 *   まず確実にIDが出る既定値にする（デグレ回避・即復旧）】
 *
 * ■ 既定（H1プリセット既定）
 *   - BUSYソース      : D6固定
 *   - 抑止            : OFF（長会話/バースト/TX後 すべて無効）
 *   - CHUNK上限       : 3000ms（LONG_TALK_MS も 3000ms に自動ペア）
 *   - D4上限超過点滅 : OFF（純受信LED）
 *   - A0運用時        : HOLDは 800ms（必要なら600ms版も別ビルド可）
 *
 * ■ ワンキー（シリアル）
 *   - 'H' : H1クイックプリセット適用（s0 / t0 / l0 / b3000）
 *   - 'q' : 現在設定の要約表示（BUSYソース／抑止／上限 等）
 *   - 既存：m0/m1/m2, s0/s1, t0/t1, l0/l1, b####, h, x, 0-3
 *
 * ■ AUTO（方式C）
 *   - 'm2' で有効。観測窓30分の実活動から A0/D6 どちらかに自動固定（同時使用なし）。
 *
 * ■ 注意
 *   - A0とD6の同時接続は不可。必ず片側のみ配線。
 *   - DMR/SFR運用ではアナログ音声IDは乗りません（FMでテスト）。
 ************************************************************/

#include <SoftwareSerial.h>

/* ================= BUSY入力ソース管理 ================= */
enum BusySrc { BUSY_SRC_D6, BUSY_SRC_A0, BUSY_SRC_AUTO };
volatile BusySrc BUSY_INPUT_SOURCE = BUSY_SRC_D6;   // ★既定：D6（H1向け）

/* ================= 既定フラグ（H1即復旧既定） ================= */
bool SUPPRESSORS_ENABLED       = false;   // ★H1既定：抑止OFF
bool TX_AFTER_SUPPRESS_ENABLED = false;   // H1既定：TX後3s抑止OFF
bool D4_OVERMAX_BLINK_ENABLED  = false;   // ★H1既定：D4点滅OFF（純受信LED）

/* ================= 自動切替（方式C）用 ================= */
const unsigned long AUTO_SWITCH_WINDOW_MS = 30UL * 60UL * 1000UL; // 30分
const uint16_t D6_EDGE_MIN   = 10;  // 30分で D6 エッジ 10 以上
const uint16_t A0_EVENT_MIN  = 20;  // 30分で A0 イベント 20 以上
unsigned long windowStartTS  = 0;
uint16_t d6_edge_count       = 0;
uint16_t a0_event_count      = 0;
unsigned long switchGraceUntil = 0;  // 切替後5分禁止
unsigned long autoSwitchBlinkUntil = 0; // D13 2Hz通知 3秒

/* ================= ユーザー設定 ================= */
const bool TM_BUSY_ACTIVE_HIGH = true;   // D6=HIGHが受信なら true、LOWが受信なら false
const bool DFP_BUSY_OUT_INVERT = true;   // D7=LOW(再生中)→D2反転ミラー：再生中HIGH/停止中LOW

// ピン定義（Arduino Nano）
const uint8_t PIN_TEST_SW      = 3;   // D3 : テストSW（INPUT_PULLUP）
const uint8_t PIN_BUSY_LED     = 4;   // D4 : 受信表示LED
const uint8_t PIN_PTT          = 5;   // D5 : PTT出力（HIGHで送信）
const uint8_t PIN_TM_BUSY      = 6;   // D6 : TM BUSY（PS817）
const uint8_t PIN_DFP_BUSY     = 7;   // D7 : DFPlayer BUSY（LOW=再生中）
const uint8_t PIN_DFP_BUSY_OUT = 2;   // D2 : D7 BUSYミラー出力
const uint8_t PIN_SUP_LED      = 13;  // D13: 抑止/通知ステータスLED

// DFPlayer SoftwareSerial
const uint8_t PIN_DFP_TX       = 10;  // D10: Arduino TX -> DFPlayer RX
const uint8_t PIN_DFP_RX       = 11;  // D11: Arduino RX <- DFPlayer TX（未接続可）
SoftwareSerial dfpSerial(PIN_DFP_TX, PIN_DFP_RX);

// しきい値・タイミング（ms）
const unsigned long BUSY_MIN_MS          = 500;
unsigned long BUSY_MAX_MS                = 3000;  // ★H1既定：3.0s
const unsigned long TM_BUSY_DEBOUNCE_MS  = 5;

// PTTタイミング（安全側）
const unsigned long PTT_PRE_MS           = 1000;  // 先行1s
const unsigned long PTT_POST_MS          = 1000;  // 後保持1s
const unsigned long PTT_MIN_EXTRA_MS     = 50;    // 最低ONガード

// テストSW
const unsigned long CLICK_WINDOW_MS      = 1000;

// 周期送出（002/003交互）
const unsigned long PERIOD_MS            = 30UL * 60UL * 1000UL; // 30分

// 誤トリガ抑止（短BUSYトリガ用）
const unsigned long IDLE_MIN_MS          = 200;   // 直前アイドル下限
const unsigned long REFRACTORY_MS        = 3000;  // 不応期

// ★TX後抑止（SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED のときのみ有効）
const unsigned long BUSY_SUPPRESS_AFTER_TX_MS = 3000; // PTT OFF後3秒間受信無視

// --- A0 front-end（ヒステリシス＋HOLD） ---
#define USE_A0_FRONT 1
const uint8_t A0_PIN = A0;
const int A0_LOW_THRESHOLD  = 300;
const int A0_HIGH_THRESHOLD = 700;
const unsigned long A0_HOLD_MS = 800;   // ★既定800ms（必要なら600msビルド可）

// --- D12 LED（A0 busyの見える化） ---
#define USE_A0_LED 1
const uint8_t PIN_A0_LED = 12;

/* ======= 文脈抑止 ======= */
unsigned long LONG_TALK_MS            = 3000;     // ★H1既定：BUSY_MAX_MSにペア
const unsigned long LONG_TALK_SUPPRESS_MS   = 10000;
const unsigned long CHUNK_BURST_WINDOW_MS   = 10000;
const unsigned int  CHUNK_BURST_THRESHOLD   = 2;
const unsigned long CHUNK_BURST_SUPPRESS_MS = 10000;

/* ================= ログレベル（イベントのみ） ================= */
enum LogLevel { LOG_NONE=0, LOG_ERROR=1, LOG_INFO=2, LOG_DEBUG=3 };
volatile LogLevel LOG_LEVEL = LOG_INFO;

/* ================= 内部状態 ================= */

enum State { IDLE, PTT_ON_WAIT, PLAYING, PTT_OFF_WAIT };
State state = IDLE;

// D6 BUSY（D6運用時のエッジ・デバウンス）
unsigned long tmBusyStart = 0;
bool tmBusyPrev = false;
bool tmBusyFiltered = false;
unsigned long tmBusyDebounceTS = 0;

// A0 front-end（A0運用時）
bool a0Detecting = false;
unsigned long a0LastSignalTS = 0;
bool a0Busy = false;
bool a0LedState = false;

// アイドル管理＆不応期
unsigned long lastIdleStart  = 0;
unsigned long lastTriggerAt  = 0;

// PTT状態
unsigned long stateTimer     = 0;
bool dfpStarted              = false;
uint16_t requestedTrack      = 0;
unsigned long pttMinOnUntil  = 0;
bool pttOutState             = false;
unsigned long pttCycleStart  = 0;

// 抑止タイマ＆CHUNK
unsigned long busySuppressUntil      = 0;
unsigned long longTalkSuppressUntil  = 0;
unsigned long chunkBurstWindowStart  = 0;
unsigned int  chunkBurstCount        = 0;
unsigned long chunkBurstSuppressUntil= 0;

// DFPlayer
unsigned long busyLowSince  = 0;
unsigned long busyHighSince = 0;
bool lastBusyLow            = false;

// STOP（アップタイム表示用）
bool stopped = false;

// 周期ID（002/003交互）
unsigned long nextPeriodicAt = 0;
bool     periodicDue = false;
uint16_t nextPeriodicTrack = 2; // 2↔3

// テストSW
uint8_t  clickCount = 0;
bool     clickWaiting = false;
unsigned long firstClickTime = 0;
bool lastSwState = HIGH;

/* ================= Δtタイムスタンプ ================= */
unsigned long ts_last_stop         = 0;
unsigned long ts_last_ptt_edge     = 0;
unsigned long ts_last_rx_edge      = 0;
unsigned long ts_last_sup_long     = 0;
unsigned long ts_last_sup_chunk    = 0;
unsigned long ts_last_sup_tx       = 0;
unsigned long ts_last_dfp_start    = 0;
unsigned long ts_last_dfp_end      = 0;

/* ================= D4（BUSY LED）上限超過点滅制御 ================= */
bool d4BlinkingOverMax = false;
unsigned long d4BlinkStartTS = 0;
const unsigned long D4_BLINK_PERIOD_MS = 500; // 1Hz点滅（500ms周期)

/* ================= ユーティリティ ================= */
inline bool readTmBusySenseRaw() { return (digitalRead(PIN_TM_BUSY) == HIGH); }
inline bool readTmBusySenseD6() {
  bool rawHigh = readTmBusySenseRaw();
  return TM_BUSY_ACTIVE_HIGH ? rawHigh : !rawHigh;
}

inline bool readBusySelected() {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) return a0Busy;
  return tmBusyFiltered; // AUTO中も現固定側に準拠（開始時はD6）
}

void formatUptime(unsigned long ms, char* buf, size_t n) {
  unsigned long total_ms = ms;
  unsigned long h = total_ms / 3600000UL; total_ms %= 3600000UL;
  unsigned long m = total_ms / 60000UL;   total_ms %= 60000UL;
  unsigned long s = total_ms / 1000UL;
  unsigned long mm = total_ms % 1000UL;
  snprintf(buf, n, "%02lu:%02lu:%02lu.%03lu", h, m, s, mm);
}

void print_dt(const char* key, unsigned long prev_ts, unsigned long now) {
  if (prev_ts != 0) { Serial.print(" "); Serial.print(key); Serial.print("Δt="); Serial.print(now - prev_ts); Serial.print("ms"); }
}

/* ================= ヘルプ ================= */
void printHelp() {
  Serial.println(F("Commands:"));
  Serial.println(F(" 0/1/2/3 : log level (none/error/info/debug)"));
  Serial.println(F(" h       : help"));
  Serial.println(F(" x       : STOP (print uptime & suppress further logs)"));
  Serial.println(F(" m0/m1/m2: BUSY source D6/A0/Auto(C)"));
  Serial.println(F(" s0/s1   : suppressors OFF/ON (long talk/burst/tx-after)"));
  Serial.println(F(" t0/t1   : tx-after suppress OFF/ON (effective only when s1)"));
  Serial.println(F(" l0/l1   : D4 blink over max OFF/ON"));
  Serial.println(F(" b####   : set BUSY_MAX_MS to ####; LONG_TALK_MS pairs"));
  Serial.println(F(" q       : print current configuration summary"));
  Serial.println(F(" H       : apply H1 quick preset (s0 / t0 / l0 / b3000)"));
}

/* ================= 設定要約 ================= */
void printSummary() {
  Serial.print(F("[CFG] BUSY_INPUT_SOURCE="));
  Serial.print(BUSY_INPUT_SOURCE==BUSY_SRC_D6?F("D6"):BUSY_INPUT_SOURCE==BUSY_SRC_A0?F("A0"):F("AUTO"));
  Serial.print(F(" / SUPPRESSORS=")); Serial.print(SUPPRESSORS_ENABLED?F("ON"):F("OFF"));
  Serial.print(F(" / TX_AFTER=")); Serial.print(TX_AFTER_SUPPRESS_ENABLED?F("ON"):F("OFF"));
  Serial.print(F(" / D4_BLINK=")); Serial.print(D4_OVERMAX_BLINK_ENABLED?F("ON"):F("OFF"));
  Serial.print(F(" / BUSY_MAX_MS=")); Serial.print(BUSY_MAX_MS);
  Serial.print(F(" / LONG_TALK_MS=")); Serial.print(LONG_TALK_MS);
  Serial.println();
}

/* ================= H1プリセット ================= */
void applyH1Preset() {
  SUPPRESSORS_ENABLED = false;
  TX_AFTER_SUPPRESS_ENABLED = false;
  D4_OVERMAX_BLINK_ENABLED = false;
  BUSY_MAX_MS = 3000; LONG_TALK_MS = BUSY_MAX_MS;
  Serial.println(F("[H1] preset applied: s0 / t0 / l0 / b3000"));
  printSummary();
}

/* ================= シリアルコマンド ================= */
void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '0') LOG_LEVEL = LOG_NONE;
    else if (c == '1') LOG_LEVEL = LOG_ERROR;
    else if (c == '2') LOG_LEVEL = LOG_INFO;
    else if (c == '3') LOG_LEVEL = LOG_DEBUG;
    else if (c == 'x') {
      unsigned long now = millis();
      char up[16]; formatUptime(now, up, sizeof(up));
      Serial.print(F("[STOP] uptime=")); Serial.print(up);
      print_dt("stop", ts_last_stop, now); Serial.println();
      ts_last_stop = now; stopped = true;
    }
    else if (c == 'h') printHelp();
    else if (c == 'q') printSummary();
    else if (c == 'H') applyH1Preset();
    else if (c == 'm') {
      while (Serial.available()) {
        char d = Serial.read();
        if (d=='0') BUSY_INPUT_SOURCE = BUSY_SRC_D6;
        else if (d=='1') BUSY_INPUT_SOURCE = BUSY_SRC_A0;
        else if (d=='2') BUSY_INPUT_SOURCE = BUSY_SRC_AUTO;
        break;
      }
      Serial.print(F("[CFG] BUSY_INPUT_SOURCE="));
      Serial.println(BUSY_INPUT_SOURCE==BUSY_SRC_D6?F("D6"):BUSY_INPUT_SOURCE==BUSY_SRC_A0?F("A0"):F("AUTO"));
    }
    else if (c == 's') {
      while (Serial.available()) { char d = Serial.read(); if (d=='0') SUPPRESSORS_ENABLED=false; else if (d=='1') SUPPRESSORS_ENABLED=true; break; }
      Serial.print(F("[CFG] SUPPRESSORS=")); Serial.println(SUPPRESSORS_ENABLED?F("ON"):F("OFF"));
    }
    else if (c == 't') {
      while (Serial.available()) { char d = Serial.read(); if (d=='0') TX_AFTER_SUPPRESS_ENABLED=false; else if (d=='1') TX_AFTER_SUPPRESS_ENABLED=true; break; }
      Serial.print(F("[CFG] TX_AFTER_SUPPRESS=")); Serial.println(TX_AFTER_SUPPRESS_ENABLED?F("ON"):F("OFF"));
    }
    else if (c == 'l') {
      while (Serial.available()) { char d = Serial.read(); if (d=='0') D4_OVERMAX_BLINK_ENABLED=false; else if (d=='1') D4_OVERMAX_BLINK_ENABLED=true; break; }
      Serial.print(F("[CFG] D4_BLINK_OVERMAX=")); Serial.println(D4_OVERMAX_BLINK_ENABLED?F("ON"):F("OFF"));
    }
    else if (c == 'b') {
      long val = 0; bool got=false;
      while (Serial.available()) {
        char d = Serial.read();
        if (d>='0' && d<='9') { val = val*10 + (d-'0'); got=true; } else break;
      }
      if (got && val>=800 && val<=5000) {
        BUSY_MAX_MS = (unsigned long)val;
        LONG_TALK_MS = BUSY_MAX_MS;
        Serial.print(F("[CFG] BUSY_MAX_MS=")); Serial.print(BUSY_MAX_MS);
        Serial.print(F(" LONG_TALK_MS=")); Serial.println(LONG_TALK_MS);
      } else {
        Serial.println(F("[CFG] BUSY_MAX_MS ignored (range 800..5000)"));
      }
    }
  }
}

/* ================= DFPlayer制御 ================= */
void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t frame[10] = { 0x7E, 0xFF, 0x06, cmd, 0x00,
                        (uint8_t)(param >> 8), (uint8_t)(param & 0xFF),
                        0x00, 0x00, 0xEF };
  uint16_t sum = 0; for (int i = 1; i < 7; i++) sum += frame[i];
  sum = 0xFFFF - sum + 1; frame[7] = (uint8_t)(sum >> 8); frame[8] = (uint8_t)(sum & 0xFF);
  dfpSerial.write(frame, 10);
}
void dfpPlay(uint16_t num) { dfpSend(0x03, num); }

/* ================= PTT制御 ================= */
void setPtt(bool on) {
  bool changed = (pttOutState != on);
  unsigned long now = millis();
  pttOutState = on;
  digitalWrite(PIN_PTT, on ? HIGH : LOW);
  if (!stopped && changed && LOG_LEVEL != LOG_NONE) {
    Serial.print(F("[EVT] PTT ")); Serial.print(on ? F("ON") : F("OFF"));
    print_dt("PTT", ts_last_ptt_edge, now); Serial.println();
  }
  ts_last_ptt_edge = now;
}

/* ================= ID送出開始 ================= */
void startPttCycle(uint16_t track) {
  if (state != IDLE) return;
  requestedTrack = track;
  dfpStarted = false;
  busyLowSince = busyHighSince = 0;
  lastBusyLow = false;

  unsigned long now = millis();
  pttCycleStart = now;
  setPtt(true);
  pttMinOnUntil = now + PTT_PRE_MS + PTT_MIN_EXTRA_MS;

  stateTimer = now;
  state = PTT_ON_WAIT;

  if (!stopped && LOG_LEVEL != LOG_NONE) {
    Serial.print(F("[EVT] ID_TRIG track="));
    Serial.print(track);
    print_dt("ID_TRIG", ts_last_ptt_edge, now);
    Serial.println();
  }
}

/* ================= 初期化 ================= */
void setup() {
  Serial.begin(115200);
  dfpSerial.begin(9600);

  pinMode(PIN_TEST_SW,   INPUT_PULLUP);
  pinMode(PIN_BUSY_LED,  OUTPUT);
  pinMode(PIN_PTT,       OUTPUT);
  pinMode(PIN_TM_BUSY,   INPUT_PULLUP);
  pinMode(PIN_DFP_BUSY,  INPUT_PULLUP);
  pinMode(PIN_DFP_BUSY_OUT, OUTPUT);
  digitalWrite(PIN_DFP_BUSY_OUT, LOW);

  pinMode(PIN_SUP_LED, OUTPUT);
  digitalWrite(PIN_SUP_LED, LOW);

#if USE_A0_LED
  pinMode(PIN_A0_LED, OUTPUT);
  digitalWrite(PIN_A0_LED, LOW);
#endif

  digitalWrite(PIN_BUSY_LED, LOW);
  setPtt(false);

  delay(500);
  dfpSend(0x06, 20);

  unsigned long now = millis();
  lastIdleStart     = now;
  lastTriggerAt     = 0;
  busySuppressUntil = 0;
  a0Detecting = false;
  a0LastSignalTS = 0;
  a0Busy = false;
  a0LedState = false;
  longTalkSuppressUntil   = 0;
  chunkBurstWindowStart   = 0;
  chunkBurstCount         = 0;
  chunkBurstSuppressUntil = 0;
  stopped = false;
  nextPeriodicAt = now + PERIOD_MS;

  windowStartTS = now;
  d6_edge_count = 0;
  a0_event_count = 0;
  switchGraceUntil = 0;
  autoSwitchBlinkUntil = 0;

  Serial.println(F("[BOOT] 1.69g-unified-H1 (H1プリセット既定)"));
  printHelp();
  printSummary();
}

/* ================= AUTO（方式C） ================= */
void maybeAutoSwitch(unsigned long now) {
  if (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO) return;

  if ((long)(now - windowStartTS) >= (long)AUTO_SWITCH_WINDOW_MS
      && (long)(switchGraceUntil - now) <= 0) {

    BusySrc newSrc = BUSY_INPUT_SOURCE;

    if (d6_edge_count < D6_EDGE_MIN && a0_event_count >= A0_EVENT_MIN)
      newSrc = BUSY_SRC_A0;
    else if (a0_event_count < A0_EVENT_MIN && d6_edge_count >= D6_EDGE_MIN)
      newSrc = BUSY_SRC_D6;
    else if (newSrc == BUSY_SRC_AUTO)
      newSrc = (d6_edge_count >= D6_EDGE_MIN) ? BUSY_SRC_D6 :
               (a0_event_count >= A0_EVENT_MIN ? BUSY_SRC_A0 : BUSY_SRC_D6);

    if (newSrc != BUSY_INPUT_SOURCE) {
      BUSY_INPUT_SOURCE = newSrc;
      switchGraceUntil = now + 5UL * 60UL * 1000UL;

      if (SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED)
        busySuppressUntil = now + 1000UL;

      autoSwitchBlinkUntil = now + 3000UL;

      Serial.print(F("[AUTO] source fixed: "));
      Serial.println(BUSY_INPUT_SOURCE==BUSY_SRC_D6?F("D6"):F("A0"));
      printSummary();
    }

    windowStartTS = now;
    d6_edge_count = 0;
    a0_event_count = 0;
  }
}

/* ================= メインループ ================= */
void loop() {
  unsigned long now = millis();
  handleSerialCmd();

  bool pttIsActive =
      (state == PTT_ON_WAIT || state == PLAYING || state == PTT_OFF_WAIT) ||
      ((long)now < (long)pttMinOnUntil);

  /* --- D7→D2ミラー（DFPlayer busy） --- */
  bool dfpLow = (digitalRead(PIN_DFP_BUSY) == LOW);
  digitalWrite(PIN_DFP_BUSY_OUT,
               DFP_BUSY_OUT_INVERT ? (dfpLow ? HIGH : LOW)
                                   : (dfpLow ? LOW  : HIGH));

  /* ---- A0 front-end ---- */
#if USE_A0_FRONT
  if (!pttIsActive && (long)(busySuppressUntil - now) <= 0) {
    int a0v = analogRead(A0_PIN);
    if (!a0Detecting && a0v < A0_LOW_THRESHOLD)
      a0Detecting = true;
    else if (a0Detecting && a0v > A0_HIGH_THRESHOLD)
      a0Detecting = false, a0_event_count++;

    if (a0Detecting) a0LastSignalTS = now;

    a0Busy = a0Detecting ||
             (a0LastSignalTS != 0 && (now - a0LastSignalTS < A0_HOLD_MS));

#if USE_A0_LED
    digitalWrite(PIN_A0_LED, a0Busy ? HIGH : LOW);
#endif
  } else {
    a0Detecting = false;
    a0Busy = false;
    a0LastSignalTS = 0;
  }
#endif

  /* ---- D6活動カウント ---- */
  {
    bool s_raw = readTmBusySenseD6();
    if (s_raw != tmBusyFiltered &&
        (now - tmBusyDebounceTS) >= TM_BUSY_DEBOUNCE_MS) {
      tmBusyFiltered = s_raw;
      tmBusyDebounceTS = now;
      d6_edge_count++;
    }
  }

  /* ---- BUSY監視 & D4表示 ---- */
  if (pttIsActive) {
    digitalWrite(PIN_BUSY_LED, LOW);
    tmBusyPrev = false;
    d4BlinkingOverMax = false;
    d4BlinkStartTS = 0;
  }
  else if (SUPPRESSORS_ENABLED && (long)(busySuppressUntil - now) > 0) {
    digitalWrite(PIN_BUSY_LED, LOW);
    tmBusyPrev = false;
    tmBusyFiltered = false;
    d4BlinkingOverMax = false;
    d4BlinkStartTS = 0;
  }
  else {
    bool rxBusy = readBusySelected();

    if (rxBusy) {
      /* BUSY 上昇 */
      if (!tmBusyPrev) {
        tmBusyStart = now;
        d4BlinkingOverMax = false;
        d4BlinkStartTS = 0;
        if (!stopped && LOG_LEVEL != LOG_NONE) {
          Serial.print(F("[EVT] RX rise"));
          print_dt("RX", ts_last_rx_edge, now);
          Serial.println();
        }
        ts_last_rx_edge = now;
      }
      else {
        unsigned long durNow = now - tmBusyStart;
        if (D4_OVERMAX_BLINK_ENABLED &&
            !d4BlinkingOverMax && durNow > BUSY_MAX_MS) {
          d4BlinkingOverMax = true;
          d4BlinkStartTS = now;
          Serial.print(F("[EVT] D4_BLINK_OVERMAX start dur="));
          Serial.println(durNow);
        }
      }

      if (D4_OVERMAX_BLINK_ENABLED && d4BlinkingOverMax) {
        bool blink = (((now - d4BlinkStartTS) / D4_BLINK_PERIOD_MS) % 2) == 0;
        digitalWrite(PIN_BUSY_LED, blink ? HIGH : LOW);
      } else {
        digitalWrite(PIN_BUSY_LED, HIGH);
      }
    }
    else {
      /* BUSY 下降 */
      digitalWrite(PIN_BUSY_LED, LOW);

      if (tmBusyPrev) {
        unsigned long dur   = now - tmBusyStart;
        unsigned long idleB = tmBusyStart - lastIdleStart;

        /* 抑止（必要なら） */
        if (SUPPRESSORS_ENABLED) {
          if (dur >= LONG_TALK_MS) {
            longTalkSuppressUntil = now + LONG_TALK_SUPPRESS_MS;
            Serial.println(F("[EVT] SUP_ON LONG"));
            ts_last_sup_long = now;
          }

          bool isChunkS = (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS);
          if (isChunkS) {
            if (chunkBurstWindowStart == 0 ||
                (now - chunkBurstWindowStart >= CHUNK_BURST_WINDOW_MS)) {
              chunkBurstWindowStart = now;
              chunkBurstCount = 0;
            }
            chunkBurstCount++;
            Serial.print(F("[EVT] CHUNK count="));
            Serial.println(chunkBurstCount);

            if (chunkBurstCount >= CHUNK_BURST_THRESHOLD) {
              chunkBurstSuppressUntil = now + CHUNK_BURST_SUPPRESS_MS;
              Serial.println(F("[EVT] SUP_ON CHUNK"));
              ts_last_sup_chunk = now;
            }
          }
        }

        bool isChunk = (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS);
        bool idleOK    = (idleB >= IDLE_MIN_MS);
        bool notRefrac = (now - lastTriggerAt >= REFRACTORY_MS);

        bool inLong    = (SUPPRESSORS_ENABLED &&
                          ((long)(longTalkSuppressUntil   - now) > 0));
        bool inBurst   = (SUPPRESSORS_ENABLED &&
                          ((long)(chunkBurstSuppressUntil - now) > 0));

        bool allowCtx  = !(inLong || inBurst);
        bool idOK      =
            (isChunk && idleOK && notRefrac && state==IDLE && allowCtx);

        if (!stopped && LOG_LEVEL != LOG_NONE) {
          Serial.print(F("[EVT] RX fall dur=")); Serial.print(dur);
          Serial.print(F("ms idleBefore="));      Serial.print(idleB);
          Serial.print(F("ms ID="));              Serial.print(idOK ? F("OK") : F("NG"));
          print_dt("RX", ts_last_rx_edge, now);
          Serial.println();
        }
        ts_last_rx_edge = now;

        if (idOK) {
          startPttCycle(1);
          lastTriggerAt = now;
        }

        lastIdleStart = now;
      }

      d4BlinkingOverMax = false;
      d4BlinkStartTS = 0;
    }

    tmBusyPrev = rxBusy;
  }

  /* ---- 抑止終了 ---- */
  if (!stopped && LOG_LEVEL != LOG_NONE) {
    unsigned long now2 = now;
    if (SUPPRESSORS_ENABLED) {
      if (longTalkSuppressUntil != 0 &&
          (long)(longTalkSuppressUntil - now2) <= 0) {
        Serial.println(F("[EVT] SUP_END LONG"));
        longTalkSuppressUntil = 0;
      }
      if (chunkBurstSuppressUntil != 0 &&
          (long)(chunkBurstSuppressUntil - now2) <= 0) {
        Serial.println(F("[EVT] SUP_END CHUNK"));
        chunkBurstSuppressUntil = 0;
      }
      if (TX_AFTER_SUPPRESS_ENABLED &&
          busySuppressUntil != 0 &&
          (long)(busySuppressUntil - now2) <= 0) {
        Serial.println(F("[EVT] SUP_END TX"));
        busySuppressUntil = 0;
      }
    }
  }

  /* ---- 周期ID ---- */
  if ((long)(now - nextPeriodicAt) >= 0) {
    periodicDue = true;
    nextPeriodicAt += PERIOD_MS;
    Serial.println(F("[Periodic] due -> flag set"));
  }
  if (periodicDue && state == IDLE) {
    startPttCycle(nextPeriodicTrack);
    nextPeriodicTrack = (nextPeriodicTrack == 2) ? 3 : 2;
    periodicDue = false;
    Serial.println(F("[Periodic] played -> toggle next"));
  }

  /* ---- D13 表示 ---- */
  {
    bool supLongActive  = (SUPPRESSORS_ENABLED &&
                           ((long)(longTalkSuppressUntil   - now) > 0));
    bool supBurstActive = (SUPPRESSORS_ENABLED &&
                           ((long)(chunkBurstSuppressUntil - now) > 0));
    bool supTxActive    = (SUPPRESSORS_ENABLED &&
                           TX_AFTER_SUPPRESS_ENABLED &&
                           ((long)(busySuppressUntil - now) > 0));
    bool autoBlinkPhase = ((long)(autoSwitchBlinkUntil - now) > 0);

    if (autoBlinkPhase) {
      bool fastBlink = ((now / 250) % 2) == 0;
      digitalWrite(PIN_SUP_LED, fastBlink ? HIGH : LOW);
    }
    else if (supLongActive || supTxActive) {
      digitalWrite(PIN_SUP_LED, HIGH);
    }
    else if (supBurstActive) {
      bool blink = ((now / 500) % 2) == 0;
      digitalWrite(PIN_SUP_LED, blink ? HIGH : LOW);
    }
    else {
      digitalWrite(PIN_SUP_LED, LOW);
    }
  }

  /* ---- FSM ---- */
  switch (state) {

    case PTT_ON_WAIT:
      if ((long)(now - stateTimer) >= (long)PTT_PRE_MS) {
        if (requestedTrack != 0) {
          dfpPlay(requestedTrack);
          requestedTrack = 0;
        }
        state = PLAYING;
      }
      break;

    case PLAYING: {
      bool dfpBusyLow = (digitalRead(PIN_DFP_BUSY) == LOW);

      if (dfpBusyLow) {
        if (!lastBusyLow) busyLowSince = now;
        lastBusyLow = true;

        if (!dfpStarted && (now - busyLowSince) >= 40) {
          dfpStarted = true;
          Serial.println(F("[EVT] DFP_START"));
          ts_last_dfp_start = now;
        }
      }
      else {
        if (lastBusyLow) busyHighSince = now;
        lastBusyLow = false;

        if (dfpStarted &&
            (busyHighSince != 0) &&
            (now - busyHighSince) >= 40) {
          state = PTT_OFF_WAIT;
          stateTimer = now;
          Serial.println(F("[EVT] DFP_END"));
          ts_last_dfp_end = now;
        }
      }
      break;
    }

    case PTT_OFF_WAIT:
      if ((long)now >= (long)pttMinOnUntil &&
          (now - stateTimer) >= PTT_POST_MS) {
        setPtt(false);
        state = IDLE;

        if (SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED)
          busySuppressUntil = now + BUSY_SUPPRESS_AFTER_TX_MS;

        if (!stopped && LOG_LEVEL != LOG_NONE) {
          char up[16];
          formatUptime(millis(), up, sizeof(up));
          unsigned long cycleMs =
              (pttCycleStart == 0) ? 0UL : (now - pttCycleStart);

          Serial.print(F("[EVT] TX_END cycle="));
          Serial.print(cycleMs);
          Serial.print(F("ms uptime="));
          Serial.print(up);
          print_dt("PTT", ts_last_ptt_edge, now);
          Serial.println();
        }

        lastIdleStart = now;
      }
      break;

    case IDLE:
    default:
      break;
  }

  /* ---- 最低ONガード ---- */
  if ((long)now < (long)pttMinOnUntil) setPtt(true);

  /* ---- テストSW（クリック数で001/002/003） ---- */
  bool sw = digitalRead(PIN_TEST_SW);

  if (sw == LOW && lastSwState == HIGH) {
    if (!clickWaiting) {
      clickWaiting = true;
      clickCount = 1;
      firstClickTime = now;
    } else {
      clickCount++;
    }
  }
  lastSwState = sw;

  if (clickWaiting &&
      (now - firstClickTime >= CLICK_WINDOW_MS)) {
    if (state == IDLE) {
      if (clickCount == 1) startPttCycle(1);
      else if (clickCount == 2) startPttCycle(2);
      else if (clickCount == 3) startPttCycle(3);
    }
    clickWaiting = false;
    clickCount = 0;
  }

  /* ---- AUTO方式C ---- */
  maybeAutoSwitch(now);
}
