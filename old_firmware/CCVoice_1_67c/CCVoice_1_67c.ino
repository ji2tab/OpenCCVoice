
/************************************************************
 * TM-8250 ID Guidance Controller
 * Version : 1.67c (PTT送信中D6無視 + D2へDFP BUSY出力=反転)
 * Target  : Arduino Nano (5V)
 *
 * 変更点：
 * - D7のDFPlayer BUSY（LOW=再生中）状態を D2 出力へミラー（反転）：
 *   再生中(D7=LOW)→D2=HIGH、停止中(D7=HIGH)→D2=LOW
 *
 * 既存機能：
 * - D6極性切替（HIGH=受信 or LOW=受信）
 * - 短BUSY(0.5〜1.5s) + 直前アイドル(≥200ms) + 不応期(3s) → 001.mp3
 * - PTT先行1000ms → 再生 → BUSYヒステリシス → 終了後保持1000ms
 * - 30分おきの 002/003 交互再生（IDLE時に消化）
 * - D3テストSW（1/2/3クリック → 001/002/003）
 ************************************************************/

#include <SoftwareSerial.h>

/* ================= 設定：TM BUSY極性 =================
   現行回路（D6=HIGHが受信）→ true
   反転回路（D6=LOWが受信） → false
======================================================== */
const bool TM_BUSY_ACTIVE_HIGH = true;   // ★回路に合わせて変更★

/* ================= ピン定義 ================= */
const uint8_t PIN_TEST_SW  = 3;   // D3 : テストSW（INPUT_PULLUP）
const uint8_t PIN_BUSY_LED = 4;   // D4 : D6状態表示LED（受信中点灯）
const uint8_t PIN_PTT      = 5;   // D5 : PTT出力
const uint8_t PIN_TM_BUSY  = 6;   // D6 : TM BUSY（PS817経由）
const uint8_t PIN_DFP_BUSY = 7;   // D7 : DFPlayer BUSY（LOW=再生中）

// ★D2へDFP BUSYミラー出力（反転）
const uint8_t PIN_DFP_BUSY_OUT = 2;      // D2 : D7の状態を出力（反転）
const bool    DFP_BUSY_OUT_INVERT = true; // true=反転：再生中HIGH／停止中LOW

const uint8_t PIN_DFP_TX   = 10;  // D10: Arduino TX -> DFPlayer RX（1k推奨）
const uint8_t PIN_DFP_RX   = 11;  // D11: Arduino RX <- DFPlayer TX（未使用可）

SoftwareSerial dfpSerial(PIN_DFP_TX, PIN_DFP_RX);

/* ================= 閾値定数 ================= */
const unsigned long BUSY_MIN   = 500;     // D6「受信」継続時間の下限（ms）
const unsigned long BUSY_MAX   = 1200;    // D6「受信」継続時間の上限（ms：未満）

const unsigned long PTT_PRE    = 1000;    // 再生前PTT先行（約1000ms）
const unsigned long PTT_POST   = 1000;    // 再生後PTT保持（約1000ms）

const unsigned long CLICK_WINDOW = 1000;  // テストSW 1秒判定

const unsigned long PERIOD_MS    = 15UL * 60UL * 1000UL; // 30分（1,800,000ms）

/* D7 BUSYヒステリシス（スパイク抑制） */
const unsigned long BUSY_LOW_CONFIRM_MS  = 40;  // LOW連続で開始確定
const unsigned long BUSY_HIGH_CONFIRM_MS = 40;  // HIGH連続で終了確定

/* PTT最低ONガード（PRE終了直後の安全余裕） */
const unsigned long PTT_MIN_EXTRA_MS = 50;

/* D6デバウンス（ふらつき抑制） */
const unsigned long TM_BUSY_DEBOUNCE_MS = 5;

/* 直前アイドル最小時間＆不応期（誤トリガ抑止） */
const unsigned long IDLE_MIN_MS   = 200;  // 直前アイドルが200ms以上でなければ短BUSYを認めない
const unsigned long REFRACTORY_MS = 3000; // トリガ後の不応期（例：3秒）

/* ================= 状態管理 ================= */
enum State {
  IDLE,          // 待機：D6監視、テストSW・周期再生待ち
  PTT_ON_WAIT,   // D5 ON後、PTT_PRE 経過を待つ
  PLAYING,       // D7=LOWを一度確認 → HIGH連続で終了へ
  PTT_OFF_WAIT   // 終了後、PTT_POST 経過で D5 OFF
};
State state = IDLE;

/* ================= 変数 ================= */
// D6 BUSY
unsigned long tmBusyStart = 0;
bool tmBusyPrev = false;
bool tmBusyFiltered = false;
unsigned long tmBusyDebounceTS = 0;

// 直前アイドル管理＆不応期
unsigned long lastIdleStart = 0;   // 直近のアイドル開始時刻
unsigned long lastTriggerAt = 0;   // 直近トリガ時刻（不応期用）

// FSMタイマ
unsigned long stateTimer = 0;
bool dfpStarted = false;            // BUSY=LOW を一度確認したか
uint16_t requestedTrack = 0;        // 再生要求トラック番号

// テストSW
uint8_t clickCount = 0;
bool clickWaiting = false;
unsigned long firstClickTime = 0;
bool lastSwState = HIGH;

// 周期スケジューラ
unsigned long nextPeriodicAt = 0;   // 次の予定時刻（絶対）
bool periodicDue = false;           // 未消化フラグ
uint16_t nextPeriodicTrack = 2;     // 交互再生（2↔3）

// D7 BUSYヒステリシス用
unsigned long busyLowSince  = 0;
unsigned long busyHighSince = 0;
bool lastBusyLow = false;

// PTT最低ONガード
unsigned long pttMinOnUntil = 0;

/* ================= DFPlayer制御 ================= */
void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t frame[10] = {
    0x7E, 0xFF, 0x06, cmd, 0x00,
    (uint8_t)(param >> 8),
    (uint8_t)(param & 0xFF),
    0x00, 0x00, 0xEF
  };
  uint16_t sum = 0;
  for (int i = 1; i < 7; i++) sum += frame[i];
  sum = 0xFFFF - sum + 1;        // two's complement
  frame[7] = (uint8_t)(sum >> 8);
  frame[8] = (uint8_t)(sum & 0xFF);
  dfpSerial.write(frame, 10);
}

void dfpPlay(uint16_t num) {
  Serial.print(F("DFP PLAY : "));
  Serial.println(num);
  dfpSend(0x03, num);
}

/* ================= TM BUSY（受信）を極性に応じて読む ================= */
inline bool readTmBusySenseRaw() {
  return (digitalRead(PIN_TM_BUSY) == HIGH); // 生値（HIGH/LOW）
}
inline bool readTmBusySense() {
  // 「受信中」を true に正規化（極性切替）
  bool rawHigh = readTmBusySenseRaw();
  return TM_BUSY_ACTIVE_HIGH ? rawHigh : !rawHigh;
}

/* ================= FSM起動（PTT前後マージン＋最低ONガード） ================= */
void startPttCycle(uint16_t track) {
  if (state != IDLE) {
    Serial.println(F("startPttCycle skipped (not IDLE)"));
    return;
  }
  requestedTrack = track;
  dfpStarted = false;
  busyLowSince = 0;
  busyHighSince = 0;
  lastBusyLow = false;

  // PTT先行開始：即座にD5 HIGH & 最低ONガードセット
  unsigned long now = millis();
  digitalWrite(PIN_PTT, HIGH);
  pttMinOnUntil = now + PTT_PRE + PTT_MIN_EXTRA_MS;  // 絶対ON維持
  stateTimer = now;
  state = PTT_ON_WAIT;
  Serial.print(F("PTT ON (pre-wait start), track="));
  Serial.println(track);
}

/* ================= 初期化 ================= */
void setup() {
  Serial.begin(115200);
  dfpSerial.begin(9600);

  pinMode(PIN_TEST_SW, INPUT_PULLUP);
  pinMode(PIN_BUSY_LED, OUTPUT);
  pinMode(PIN_PTT, OUTPUT);
  pinMode(PIN_TM_BUSY, INPUT_PULLUP);
  pinMode(PIN_DFP_BUSY, INPUT_PULLUP);

  // D2出力初期化（反転仕様）
  pinMode(PIN_DFP_BUSY_OUT, OUTPUT);
  digitalWrite(PIN_DFP_BUSY_OUT, LOW);  // 反転仕様：初期は停止想定（D7=HIGH）→D2=LOW

  digitalWrite(PIN_PTT, LOW);
  digitalWrite(PIN_BUSY_LED, LOW);

  delay(500);            // DFPlayer起動安定待ち
  dfpSend(0x06, 15);     // 音量設定

  // 周期スケジューラ初期化（起動から30分後）
  nextPeriodicAt = millis() + PERIOD_MS;

  // 直前アイドルの初期化
  lastIdleStart = millis();
  lastTriggerAt = 0;

  Serial.println(F("TM-8250 Controller v1.67c READY"));
  Serial.print(F("TM_BUSY_ACTIVE_HIGH = "));
  Serial.println(TM_BUSY_ACTIVE_HIGH ? F("true (HIGH=BUSY)") : F("false (LOW=BUSY)"));
}

/* ================= メインループ ================= */
void loop() {
  unsigned long now = millis();

  /* ---- PTTアクティブ判定 ----
     PRE/PLAYING/OFF_WAIT中 または 最低ONガード中 は true
  */
  bool pttIsActive =
    (state == PTT_ON_WAIT || state == PLAYING || state == PTT_OFF_WAIT) ||
    ((long)now < (long)pttMinOnUntil);

  /* ---- D7をD2へミラー出力（反転対応） ---- */
  bool dfpBusyLow_now = (digitalRead(PIN_DFP_BUSY) == LOW);   // LOW=再生中
  if (!DFP_BUSY_OUT_INVERT) {
    digitalWrite(PIN_DFP_BUSY_OUT, dfpBusyLow_now ? LOW : HIGH);
  } else {
    // 反転仕様：再生中(D7=LOW)→D2=HIGH、停止中(D7=HIGH)→D2=LOW
    digitalWrite(PIN_DFP_BUSY_OUT, dfpBusyLow_now ? HIGH : LOW);
  }

  /* ---- D6 BUSY監視（PTT中は無視） ---- */
  if (pttIsActive) {
    digitalWrite(PIN_BUSY_LED, LOW); // 送信中は受信表示OFF
    tmBusyPrev = false;              // エッジ状態リセット
  } else {
    bool tmBusySense = readTmBusySense();  // 「受信中」ならtrue
    if (tmBusySense != tmBusyFiltered && (now - tmBusyDebounceTS) >= TM_BUSY_DEBOUNCE_MS) {
      tmBusyFiltered = tmBusySense;
      tmBusyDebounceTS = now;
    }
    bool tmBusy = tmBusyFiltered;

    // 受信LED（「受信中」なら点灯）
    digitalWrite(PIN_BUSY_LED, tmBusy ? HIGH : LOW);

    // エッジ検出（受信開始/終了）
    if (tmBusy && !tmBusyPrev) {
      tmBusyStart = now; // 受信開始（受信前のアイドルは lastIdleStart〜now）
    }
    if (!tmBusy && tmBusyPrev) {
      // 受信終了：継続時間と直前アイドル長を測定
      unsigned long busyDur = now - tmBusyStart;
      unsigned long idleDurBefore = tmBusyStart - lastIdleStart;

      Serial.print(F("BUSY dur(ms)=")); Serial.print(busyDur);
      Serial.print(F(" / idleBefore(ms)=")); Serial.println(idleDurBefore);

      // 条件：短BUSYかつ直前アイドルが十分長い、かつ不応期外、かつIDLE
      bool shortBusy = (busyDur >= BUSY_MIN && busyDur < BUSY_MAX);
      bool idleOK    = (idleDurBefore >= IDLE_MIN_MS);
      bool notRefrac = (now - lastTriggerAt >= REFRACTORY_MS);

      if (shortBusy && idleOK && notRefrac && state == IDLE) {
        startPttCycle(1);       // 001.mp3
        lastTriggerAt = now;    // 不応期開始
      } else {
        Serial.println(F("BUSY ignored (shortBusy/idleOK/notRefrac/state checks failed)"));
      }

      // この受信終了の直後からアイドル開始
      lastIdleStart = now;
    }
    tmBusyPrev = tmBusy;
  }

  /* ---- 周期スケジューラ（30分おき 002/003 交互） ---- */
  if ((long)(now - nextPeriodicAt) >= 0) {
    periodicDue = true;
    nextPeriodicAt += PERIOD_MS;    // 次の予定（絶対スケジュール）
    Serial.println(F("[Periodic] due -> flag set"));
  }
  if (periodicDue && state == IDLE) {
    startPttCycle(nextPeriodicTrack);
    nextPeriodicTrack = (nextPeriodicTrack == 2) ? 3 : 2;
    periodicDue = false;
    Serial.println(F("[Periodic] played -> toggle next"));
  }

  /* ---- FSM ---- */
  switch (state) {
    case PTT_ON_WAIT:
      // D5 ON後、PTT_PRE 経過で DFPlayer 再生コマンド送信
      if ((long)(now - stateTimer) >= (long)PTT_PRE) {
        if (requestedTrack != 0) {
          dfpPlay(requestedTrack);
          requestedTrack = 0;
        }
        state = PLAYING;
        // 以降は D7ヒステリシスで開始/終了判定
      }
      break;

    case PLAYING: {
      bool dfpBusyLow = (digitalRead(PIN_DFP_BUSY) == LOW); // LOW=再生中

      if (dfpBusyLow) {
        // LOW継続時間の計測
        if (!lastBusyLow) busyLowSince = now;
        lastBusyLow = true;

        // LOW連続確認で開始確定
        if (!dfpStarted && (now - busyLowSince) >= BUSY_LOW_CONFIRM_MS) {
          dfpStarted = true;
          Serial.println(F("DFP START confirmed (BUSY LOW)"));
        }
        // 再生中はPTT維持（最低ONガードとも合致）
      } else {
        // HIGH継続時間の計測
        if (lastBusyLow) busyHighSince = now;
        lastBusyLow = false;

        // 終了確定は「開始確定済み」かつ「HIGH連続一定時間」
        if (dfpStarted && (busyHighSince != 0) && (now - busyHighSince) >= BUSY_HIGH_CONFIRM_MS) {
          state = PTT_OFF_WAIT;
          stateTimer = now;
          Serial.println(F("DFP END confirmed (BUSY HIGH) -> post-wait"));
        }
        // dfpStarted=false の間は、まだ開始未確定（PTT維持）
      }
      break;
    }

    case PTT_OFF_WAIT:
      // 最低ONガードを満たし、かつ POST保持が終わったらOFF → IDLE
      if ((long)now >= (long)pttMinOnUntil && (now - stateTimer) >= PTT_POST) {
        digitalWrite(PIN_PTT, LOW);
        state = IDLE;
        // PTT送信が完全に終了したので、ここからアイドル開始
        lastIdleStart = now;
        Serial.println(F("PTT OFF -> IDLE (idleStart reset)"));
      }
      break;

    case IDLE:
    default:
      // 何もしない（D6待ち、テストSW・周期待ち）
      break;
  }

  /* ---- D5の「最低ONガード」の強制維持 ---- */
  if ((long)now < (long)pttMinOnUntil) {
    digitalWrite(PIN_PTT, HIGH);   // どのステートでもこの時刻までは必ずON
  }

  /* ---- D3 テストSW（1秒以内のクリック 1/2/3 → 1/2/3） ---- */
  bool sw = digitalRead(PIN_TEST_SW);
  if (sw == LOW && lastSwState == HIGH) {
    if (!clickWaiting) {
      clickWaiting   = true;
      clickCount     = 1;
      firstClickTime = now;
    } else {
      clickCount++;
    }
  }
  lastSwState = sw;

  if (clickWaiting && (now - firstClickTime >= CLICK_WINDOW)) {
    if (state == IDLE) {
      if      (clickCount == 1) startPttCycle(1);
      else if (clickCount == 2) startPttCycle(2);
      else if (clickCount == 3) startPttCycle(3);
    } else {
      Serial.println(F("TestSW ignored (not IDLE)"));
    }
    clickWaiting = false;
    clickCount = 0;
  }

  // 非ブロッキング：delayは使わない
}
