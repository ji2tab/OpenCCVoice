
/************************************************************
 * TM-8250 ID Guidance Controller (Unified & Configurable)
 * Version : 1.71-stable-final (Verified Stable)
 * Target  : Arduino Nano (ATmega328P, 5V)
 *
 * 【本版の目的：設定の統合・視認性の向上、および安定化パッチ適用】
 *
 * ■ 修正履歴
 * - v1.71 (Integrated-MAX): BUSY_MAX_MS統合、LEDモード判別実装。
 * - v1.71-stable: A0_PIN定義漏れ修正、シリアル受信(200ms)堅牢化。
 * - v1.71-final: DFP終了判定ガード追加、t/x/hコマンド完全実装。
 *
 * ■ 既定（標準プリセット既定）
 * - BUSYソース      : D6固定
 * - 抑止            : ON（すべて有効）
 * - CHUNK上限       : 1500ms（LONG_TALK判定もしきい値を共有）
 * - モード表示      : 未使用側のLED（D4 or D12）が待機中に点滅/点灯
 *
 * ■ ワンキー（シリアル）
 * - 'H' : H1クイックプリセット適用（s0 / t0 / l0 / b4000）
 * - 'q' : 現在設定の要約表示（BUSYソース／抑止／上限 等）
 * - 既存：m0/m1/m2, s0/s1, t0/t1, b####, h, x, 0-3
 *
 * ■ 注意
 * - A0とD6の同時接続不可。必ず片側のみ配線。
 * - DMR/SFR運用ではアナログ音声IDは乗りません（FMでテスト）。
 ************************************************************/

#include <SoftwareSerial.h>

/* --- 入力ソース・フラグ設定 --- */
enum BusySrc { BUSY_SRC_D6, BUSY_SRC_A0, BUSY_SRC_AUTO };
volatile BusySrc BUSY_INPUT_SOURCE = BUSY_SRC_D6;

bool SUPPRESSORS_ENABLED       = true;  // 抑止機能
bool TX_AFTER_SUPPRESS_ENABLED = true;  // 送信直後(3s)無視
bool D4_OVERMAX_BLINK_ENABLED  = false; // 上限超過点滅(非推奨)

/* --- AUTO（方式C）安定化設定 --- */
const unsigned long AUTO_WINDOW = 30UL * 60UL * 1000UL; 
const unsigned long AUTO_GRACE  = 5UL * 60UL * 1000UL;  
const uint16_t D6_EDGE_MIN   = 10;
const uint16_t A0_EVENT_MIN  = 20;

unsigned long windowStartTS = 0, switchGraceUntil = 0;
uint16_t d6_edge_count = 0, a0_event_count = 0;
unsigned long autoSwitchBlinkUntil = 0;

/* --- 物理ピン定義 --- */
const bool TM_BUSY_ACTIVE_HIGH = true;
const bool DFP_BUSY_OUT_INVERT = true;

const uint8_t PIN_TEST_SW  = 3;  // D3
const uint8_t PIN_BUSY_LED = 4;  // D4 受信LED
const uint8_t PIN_PTT      = 5;  // D5
const uint8_t PIN_TM_BUSY  = 6;  // D6
const uint8_t PIN_DFP_BUSY = 7;  // D7
const uint8_t PIN_DFP_OUT  = 2;  // D2 ミラー出力
const uint8_t PIN_SUP_LED  = 13; // D13
const uint8_t PIN_A0_LED   = 12; // D12
const uint8_t PIN_DFP_TX   = 10;
const uint8_t PIN_DFP_RX   = 11;

SoftwareSerial dfpSerial(PIN_DFP_TX, PIN_DFP_RX);

/* --- タイミング・しきい値 --- */
const unsigned long BUSY_MIN_MS = 500;  
unsigned long BUSY_MAX_MS       = 1500; // 上限(統合)
const unsigned long DEBOUNCE_MS = 5;    
const unsigned long PTT_PRE_MS  = 1000; 
const unsigned long PTT_POST_MS = 1000; 
const unsigned long PERIOD_MS   = 30UL * 60UL * 1000UL; 
const unsigned long IDLE_MIN_MS = 200;  
const unsigned long REFRAC_MS   = 3000; 
const unsigned long TX_SUP_MS   = 3000; 

// A0フロントエンド
#define USE_A0_FRONT 1
const uint8_t A0_PIN = A0; 
const int A0_LOW_TH  = 300;
const int A0_HIGH_TH = 700;
const unsigned long A0_HOLD = 800; 

// 文脈抑止
unsigned long LONG_TALK_MS = 1500; // BUSY_MAX_MSと同期
const unsigned long LONG_SUP_MS   = 10000;
const unsigned long BURST_WIN_MS  = 10000;
const unsigned int  BURST_TH      = 2;
const unsigned long BURST_SUP_MS  = 10000;

/* --- 内部状態・ログ管理 --- */
enum LogLvl { LOG_NONE=0, LOG_ERR=1, LOG_INF=2, LOG_DBG=3 };
volatile LogLvl LOG_LEVEL = LOG_INF;
enum State { IDLE, PTT_ON_WAIT, PLAYING, PTT_OFF_WAIT };
State state = IDLE;

unsigned long tmBusyStart=0, tmDebounceTS=0, a0LastSignalTS=0;
bool tmBusyPrev=false, tmBusyFiltered=false, a0Detect=false, a0Busy=false;
unsigned long lastIdle=0, lastTriggerAt=0, stateTimer=0, pttMinOn=0;
bool dfpStarted=false, lastBusyLow=false, pttOutState=false, stopped=false;
bool periodicDue=false, clickWaiting=false;
uint16_t requestedTrack=0, nextPeriodicTrack=2;
unsigned long busySupUntil=0, longSupUntil=0, burstWinStart=0;
unsigned long burstSupUntil=0, busyLowTS=0, busyHighTS=0, nextPeriodicAt=0;
unsigned int burstCount=0;
uint8_t clickCount=0, lastSwState=HIGH;
unsigned long firstClickTime=0;

/* --- ユーティリティ --- */
inline bool readTmRaw() { return (digitalRead(PIN_TM_BUSY) == HIGH); }
inline bool readTmD6() { 
  return TM_BUSY_ACTIVE_HIGH ? readTmRaw() : !readTmRaw(); 
}
inline bool readBusy() {
  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) return tmBusyFiltered;
  if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) return a0Busy;
  return tmBusyFiltered;
}
void formatUptime(unsigned long ms, char* buf, size_t n) {
  unsigned long total_ms = ms;
  unsigned long h = total_ms / 3600000UL; total_ms %= 3600000UL;
  unsigned long m = total_ms / 60000UL;   total_ms %= 60000UL;
  unsigned long s = total_ms / 1000UL;
  unsigned long mm = total_ms % 1000UL;
  snprintf(buf, n, "%02lu:%02lu:%02lu.%03lu", h, m, s, mm);
}
void printSummary() {
  Serial.print(F("[CFG] SRC="));
  Serial.print(BUSY_INPUT_SOURCE==BUSY_SRC_D6 ? F("D6") : 
               BUSY_INPUT_SOURCE==BUSY_SRC_A0 ? F("A0") : F("AUTO"));
  Serial.print(F(" SUP="));  Serial.print(SUPPRESSORS_ENABLED ? F("ON") : F("OFF"));
  Serial.print(F(" TXAF=")); Serial.print(TX_AFTER_SUPPRESS_ENABLED ? F("ON") : F("OFF"));
  Serial.print(F(" MAX="));  Serial.print(BUSY_MAX_MS);
  Serial.print(F(" LONG=")); Serial.println(LONG_TALK_MS);
}
void printHelp() {
  Serial.println(F("Cmds: m[0-2], b####, s[0-1], t[0-1], H, q, h, x, 0-3"));
}

/* --- シリアル受信パッチ --- */
void handleSerialCmd() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'm') {
      unsigned long until = millis() + 200;  // 200ms待ちで堅牢化
      char d = '\0';
      while ((long)(until - millis()) > 0) {
        if (!Serial.available()) { delay(1); continue; }
        char next = Serial.read();
        if (next=='\r'||next=='\n'||next==' '||next=='\t') continue;
        if (next>='0' && next<='2') { d = next; break; }
      }
      if (d == '0') BUSY_INPUT_SOURCE = BUSY_SRC_D6;
      else if (d == '1') BUSY_INPUT_SOURCE = BUSY_SRC_A0;
      else if (d == '2') { 
        BUSY_INPUT_SOURCE = BUSY_SRC_AUTO; windowStartTS = millis(); 
      }
      printSummary();
    }
    else if (c == 'b') {
      long val = Serial.parseInt();
      if (val >= 800 && val <= 5000) { 
        BUSY_MAX_MS = val; LONG_TALK_MS = val; 
        Serial.print(F("[CFG] MAX=")); Serial.println(BUSY_MAX_MS);
      } else {
        Serial.println(F("[WARN] MAX ignored (range 800..5000)"));
      }
    }
    else if (c == 's') { 
      char d = Serial.read(); 
      if (d=='0') SUPPRESSORS_ENABLED=false; 
      else if (d=='1') SUPPRESSORS_ENABLED=true; 
      printSummary(); 
    }
    else if (c == 't') {
      char d = Serial.read();
      if (d=='0') TX_AFTER_SUPPRESS_ENABLED = false;
      else if (d=='1') TX_AFTER_SUPPRESS_ENABLED = true;
      printSummary();
    }
    else if (c == 'H') { 
      SUPPRESSORS_ENABLED = false; 
      TX_AFTER_SUPPRESS_ENABLED = false; 
      BUSY_MAX_MS = 4000; 
      LONG_TALK_MS = 4000;
      Serial.println(F("[H1] s0/t0/b4000"));
    }
    else if (c == 'q') printSummary();
    else if (c == 'h') printHelp();
    else if (c == 'x') {
      char up[16]; formatUptime(millis(), up, sizeof(up));
      Serial.print(F("[STOP] uptime=")); Serial.println(up);
      stopped = true;
    }
    else if (c >= '0' && c <= '3') LOG_LEVEL = (LogLvl)(c - '0');
  }
}

void dfpSend(uint8_t cmd, uint16_t param) {
  uint8_t f[10] = {0x7E,0xFF,0x06,cmd,0x00,(uint8_t)(param>>8),(uint8_t)param,0,0,0xEF};
  uint16_t s = 0; for (int i=1; i<7; i++) s += f[i];
  s = 0xFFFF - s + 1; f[7] = (uint8_t)(s>>8); f[8] = (uint8_t)s;
  dfpSerial.write(f, 10);
}

void setPtt(bool on) {
  if (pttOutState == on) return;
  pttOutState = on; digitalWrite(PIN_PTT, on ? HIGH : LOW);
  if (LOG_LEVEL >= LOG_INF) { 
    Serial.print(F("[EVT] PTT ")); Serial.println(on?F("ON"):F("OFF")); 
  }
}

void startPtt(uint16_t trk) {
  if (state != IDLE) return;
  requestedTrack = trk; dfpStarted = false;
  unsigned long now = millis(); setPtt(true); 
  pttMinOn = now + PTT_PRE_MS + 100;
  stateTimer = now; state = PTT_ON_WAIT;
}

void maybeAuto(unsigned long now) {
  if (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO) return;
  if ((long)(now - windowStartTS) >= (long)AUTO_WINDOW && 
      (long)(now - switchGraceUntil) >= 0) {
    BusySrc n = BUSY_INPUT_SOURCE;
    if (d6_edge_count < D6_EDGE_MIN && a0_event_count >= A0_EVENT_MIN) 
      n = BUSY_SRC_A0;
    else if (a0_event_count < A0_EVENT_MIN && d6_edge_count >= D6_EDGE_MIN) 
      n = BUSY_SRC_D6;
    if (n != BUSY_INPUT_SOURCE) {
      BUSY_INPUT_SOURCE = n; autoSwitchBlinkUntil = now + 3000;
      switchGraceUntil = now + AUTO_GRACE; 
      if (SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED) 
        busySupUntil = now + 1000;  // 切替直後 1秒抑止（TX_AFTERがONのとき）
      Serial.print(F("[AUTO] Switched to ")); Serial.println(n==BUSY_SRC_D6?F("D6"):F("A0"));
    }
    windowStartTS = now; d6_edge_count = 0; a0_event_count = 0;
  }
}

void setup() {
  Serial.begin(115200); 
  dfpSerial.begin(9600);

  // ★ pinMode の役割を正しく設定（必須パッチ）
  pinMode(PIN_TEST_SW,  INPUT_PULLUP); // 押下=LOW
  pinMode(PIN_BUSY_LED, OUTPUT);
  pinMode(PIN_PTT,      OUTPUT);

  pinMode(PIN_TM_BUSY,  INPUT_PULLUP); // PS817出力を受ける（HIGH/LOW）
  pinMode(PIN_DFP_BUSY, INPUT_PULLUP); // DFPlayer BUSY（LOW=再生中）
  pinMode(PIN_DFP_OUT,  OUTPUT);

  pinMode(PIN_SUP_LED,  OUTPUT);
  pinMode(PIN_A0_LED,   OUTPUT);

  delay(500); 
  dfpSend(0x06, 20);    // DFPlayer 音量初期値

  unsigned long now = millis(); 
  nextPeriodicAt = now + PERIOD_MS; 
  windowStartTS  = now;

  Serial.println(F("[BOOT] v1.71-stable-final"));
  printSummary();
}

void loop() {
  unsigned long now = millis(); 
  handleSerialCmd();
  bool pAct = (state != IDLE) || ((long)now < (long)pttMinOn);

  // D7ミラー
  bool dLow = (digitalRead(PIN_DFP_BUSY) == LOW);
  digitalWrite(PIN_DFP_OUT, DFP_BUSY_OUT_INVERT ? (dLow ? HIGH : LOW)
                                                : (dLow ? LOW  : HIGH));

  // 信号取得
#if USE_A0_FRONT
  if (!pAct && (long)(now - busySupUntil) >= 0) {
    int v = analogRead(A0_PIN);
    if (!a0Detect && v < A0_LOW_TH) a0Detect = true;
    else if (a0Detect && v > A0_HIGH_TH) { a0Detect = false; a0_event_count++; }
    if (a0Detect) a0LastSignalTS = now;
    a0Busy = a0Detect || (a0LastSignalTS!=0 && (now - a0LastSignalTS < A0_HOLD));
  } else { a0Detect = false; a0Busy = false; }
#endif
  {
    bool r = readTmD6();
    if (r != tmBusyFiltered && (now - tmDebounceTS) >= DEBOUNCE_MS) {
      tmBusyFiltered = r; tmDebounceTS = now; d6_edge_count++;
    }
  }

  // LED表示制御（AUTO対応）
  bool bl = ((now / 500) % 2 == 0); 
  bool rB = readBusy();
  if (BUSY_INPUT_SOURCE == BUSY_SRC_D6) {
    digitalWrite(PIN_BUSY_LED, rB ? HIGH : LOW);
    digitalWrite(PIN_A0_LED,   a0Busy ? HIGH : (bl ? HIGH : LOW));
  } else if (BUSY_INPUT_SOURCE == BUSY_SRC_A0) {
    digitalWrite(PIN_A0_LED,   rB ? HIGH : LOW);
    digitalWrite(PIN_BUSY_LED, tmBusyFiltered ? HIGH : (bl ? HIGH : LOW));
  } else if (BUSY_INPUT_SOURCE == BUSY_SRC_AUTO) {
    // AUTO中は両方を点滅＋生反応で可視化
    digitalWrite(PIN_BUSY_LED, tmBusyFiltered ? HIGH : (bl ? HIGH : LOW));
    digitalWrite(PIN_A0_LED,   a0Busy         ? HIGH : (bl ? HIGH : LOW));
  }

  // 判定ロジック
  if (!pAct && !(SUPPRESSORS_ENABLED && (long)(now - busySupUntil) < 0)) {
    if (rB) { 
      if (!tmBusyPrev) tmBusyStart = now; 
    } else if (tmBusyPrev) {
      unsigned long dur = now - tmBusyStart;

      if (SUPPRESSORS_ENABLED) {
        if (dur >= BUSY_MAX_MS) longSupUntil = now + LONG_SUP_MS;
        if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
          if (burstWinStart==0 || (now-burstWinStart >= BURST_WIN_MS)) { 
            burstWinStart=now; burstCount=0; 
          }
          if (++burstCount >= BURST_TH) burstSupUntil = now + BURST_SUP_MS;
        }
      }

      bool al = !(SUPPRESSORS_ENABLED && ((long)(now-longSupUntil)<0 || (long)(now-burstSupUntil)<0));
      if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS && (now-lastTriggerAt >= REFRAC_MS) && al) {
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

  // 抑止LED（D13）
  bool s = SUPPRESSORS_ENABLED && ((long)(now-longSupUntil)<0 || (long)(now-burstSupUntil)<0 || (long)(now-busySupUntil)<0);
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
        lastBusyLow = true; 
        dfpStarted = true; 
      } else { 
        if (dfpStarted && (busyHighTS != 0) && (now-busyHighTS >= 40)) { 
          state = PTT_OFF_WAIT; 
          stateTimer = now; 
          if (LOG_LEVEL >= LOG_INF) Serial.println(F("[DFP] End"));
        }
        if (lastBusyLow) busyHighTS = now; 
        lastBusyLow = false;
      } 
      break;

    case PTT_OFF_WAIT: 
      if (now-stateTimer >= PTT_POST_MS && now >= pttMinOn) { 
        setPtt(false); 
        state = IDLE; 
        if (SUPPRESSORS_ENABLED && TX_AFTER_SUPPRESS_ENABLED) 
          busySupUntil = now + TX_SUP_MS; 
      } 
      break;
  }

  // 最低ONガード
  if ((long)now < (long)pttMinOn) setPtt(true);

  // テストSW
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

  // AUTO（方式C）
  maybeAuto(now);
}