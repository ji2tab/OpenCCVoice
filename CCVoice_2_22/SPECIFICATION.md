**===== 仕様書（技術者向け） OpenCCVoice Guidance Controller v2.22 =====**

1. 基本情報
- 名称      : OpenCCVoice Guidance Controller
- バージョン : v2.22 — DFPlayer TX/RX ピンアサイン修正版
- MCU       : Arduino Nano（ATmega328P, 5V）
- 対象基板  : Ver.5
- 依存      : Arduino.h / SoftwareSerial.h / EEPROM.h / Wire.h
- DFPlayer  : BUSY監視（D10=LOW 再生中）、9600bps 制御（SoftwareSerial D12/D13）
- DS3231    : I²C（A4=SDA, A5=SCL）、レジスタ直接アクセス（外部ライブラリ不要）
- EEPROM    : CONFIG_VERSION=6（v2.10=5 から saveTimestamp フィールドを追加）
               → v2.10 / v2.01 設定を引き継ぎ可能
- AT24C32   : 外部EEPROM（I²C 0x57）/ 内蔵EEPROMと二重保存（v2.20追加）

**===== 2. ピンアサイン（Ver.5） =====**

 Pin  | 定数名              | 方向   | 信号                          | 備考
------|---------------------|--------|-------------------------------|----------------------------
 D2   | PIN_TEST_SW         | 入力   | テストSW                      | INPUT_PULLUP、LOW=押下
 D3   | PIN_DFP_OUT         | 出力   | DFPlayer BUSYミラー           | 再生中=HIGH（反転出力）
 D4   | PIN_BUSY_LED        | 出力   | ModBusy LED                   | HIGH=点灯
 D5   | PIN_PTT             | 出力   | PTT                           | HIGH=送信
 D6   | PIN_SUP_LED         | 出力   | 抑止 LED                      | HIGH=点灯（抑止中）
 D7   | PIN_A0_LED          | 出力   | A0検知 LED                    | HIGH=点灯（a0Busy時）
 D10  | PIN_DFP_BSY         | 入力   | DFPlayer BUSY入力             | INPUT_PULLUP、LOW=再生中
 D11  | PIN_TM_BUSY         | 入力   | TM BUSY入力（Digital入力）    | INPUT_PULLUP、極性g0/g1
 D13  | ARD_TX_TO_DFP       | 出力   | Arduino TX → DFPlayer RX     | SoftwareSerial 9600bps ★v2.22修正
 D12  | ARD_RX_FROM_DFP     | 入力   | Arduino RX ← DFPlayer TX     | SoftwareSerial 9600bps ★v2.22修正
 A0   | A0_PIN              | 入力   | アナログBUSY                  | 0〜1023、ヒステリシス＋保持
 A4   | —（Wire固定）        | I²C    | DS3231 SDA                    | Wire.h使用（v2.10追加）
 A5   | —（Wire固定）        | I²C    | DS3231 SCL                    | Wire.h使用（v2.10追加）

**===== 3. 機能仕様（Functional） =====**

(1) 短発受信の自動ID送出（v2.01から変更なし）
  - TM BUSY（D11）または A0 の受信継続時間 dur が
      BUSY_MIN_MS ≤ dur < BUSY_MAX_MS のとき、Track 1（001）を送出。
  - PTT シーケンス：PRE=1000ms → 再生 → POST=1000ms。

(2) 抑止（長話・バースト・送信後）（v2.01から変更なし）
  - 長話抑止      : dur ≥ BUSY_MAX_MS → LONG_SUP_MS=10s
  - バースト抑止  : 10秒窓内で短発が2回以上 → BURST_SUP_MS=10s
  - 送信後抑止    : t1有効時、TX後 TX_SUP_MS(ms) の受信を無視
  - 送信直後ガード: PTT OFF 直後、BUSYが idleMin 連続OFF まで全発火禁止

(3) AUTO（m2）（v2.01から変更なし）
  - AUTO_WINDOW（分）観測し、Digital/A0 の優位側へ自動固定。

(4) 周期ID（v2.10で正時アライン機能を追加）
  【RTCアライン動作（DS3231接続 かつ p## が60の約数 かつ u1）】
  - PERIOD_MS ごとに Track 2/3 を交互送出。
  - 次回発火時刻 = 「現在時刻から次の正時アライン境界まで」を DS3231 から計算。
  - 発火後、即座に次回時刻を再計算（recalcPeriodicAlign）。
  - 1分ごとに DS3231 から時刻を再取得してドリフト補正（RTC_RESYNC_INTERVAL=60s）。
  - 送出条件（全て満たすこと）：
    1) periodicDue=true（nextPeriodicAt を超過）
    2) state==IDLE
    3) readBusy()==false
    4) BUSY が OFFになってから periodQuietMs(ms) 以上静寂
    5) 抑止中でない（長話/バースト/送信後ガード）
  - BUSY中・静寂不足・抑止期間中は送出せず延期（periodicDue 保持）。

  【millis()相対動作（DS3231未接続 / 60の約数でない / u0）】
  - v2.01 互換の catch-up 方式。
  - nextPeriodicAt += PERIOD_MS で追い付かせ、最大1回分のみ発火。

  【切替判定ロジック】
  - isAlignablePeriod(pMin): 60 % pMin == 0 かつ 0 < pMin ≤ 60 → true
  - rtcAvailable && RTC_ALIGN_ON && isAlignablePeriod → rtcAlignActive=true

(5) DS3231 RTC 管理（v2.10新規）
  - Wire.h を使用、外部ライブラリ不要。
  - 起動時に rtcProbe() で I²C 疎通確認（アドレス 0x68）。
  - rtcRead()  : 7バイト読み取り、BCD→DEC変換、RtcTime 構造体に格納。
  - rtcWrite() : RtcTime 構造体からBCD変換して書き込み。
  - DS3231 未応答時は rtcAvailable=false → millis() 相対にフォールバック。

(6) EEPROM 永続化（v2.10で rtcAlignOn を追加）
  - 変更コマンド（m/n/b/i/p/r/L/G/a/w/s/t/d/g/k/u）で即保存。
  - CONFIG_VERSION=5 / v2.01（ver=4）から自動マイグレーション。

(7) LED表示 / D3ミラー出力（v2.01から変更なし）

(8) フェイルセーフ（PTT張り付き防止）（v2.01から変更なし）

(9) テストSW（D2）（v2.01から変更なし）
  - 1〜3クリック → Track 1〜3 再生

**===== 4. 非機能仕様（Non-Functional） =====**
（v2.01から変更なし）
- 応答性 : TM BUSY デバウンス 5ms
- 安定性 : DFPlayer BUSY の HIGH確定に 40ms
- ガード : PTT 最低 ON ガード搭載
- 可搬性 : Arduino IDE 1.8.x / 2.x
- 保守性 : printSummary() による状態一覧（RTC時刻・アライン状態含む）
- 安全性 : DFPlayer タイムアウト監視 / PTT張り付き防止
- 耐久性 : EEPROM 書込みは「値変更時のみ」

**===== 5. インタフェース仕様（I/O 詳細） =====**

【入力】
- D2  (INPUT_PULLUP) : テストSW。LOW=押下。
- D10 (INPUT_PULLUP) : DFPlayer BUSY。LOW=再生中。
- D11 (INPUT_PULLUP) : TM BUSY。g0: LOW=busy / g1: HIGH=busy（既定）。
- A0                 : アナログBUSY（0〜1023 / 基準VCC=5V）。
- A4                 : I²C SDA（DS3231）。Wire.h が管理。
- A5                 : I²C SCL（DS3231）。Wire.h が管理。

【出力】
- D3  : DFPlayer BUSY ミラー（反転）。再生中=HIGH。
- D4  : ModBusy LED。
- D5  : PTT。送信中 HIGH。
- D6  : 抑止 LED。
- D7  : A0検知 LED。

【SoftwareSerial（DFPlayer通信）】
- D12 (TX) / D13 (RX) : 9600bps TTLレベル（5V）。

**===== 6. 既定値（Factory Defaults） =====**
（v2.01から変更なし、rtcAlignOn=1 を追加）
- BUSY_INPUT_SOURCE           = DIGITAL（D11）
- BUSY_MIN_MS                 = 500
- BUSY_MAX_MS                 = 3900
- IDLE_MIN_MS                 = 200
- PERIOD_MS                   = 30 min
- TX_SUP_MS                   = 3000
- SUPPRESSORS_ENABLED         = true
- TX_AFTER_SUPPRESS_ENABLED   = true
- A0_LOW_TH                   = 300
- A0_HIGH_TH                  = 700
- A0_HOLD                     = 800
- AUTO_WINDOW                 = 30 min
- DFP_TIMEOUT_MS              = 20000ms
- TM_BUSY_ACTIVE_HIGH         = true（HIGH=busy）
- periodQuietMs               = 2000ms
- RTC_ALIGN_ON                = true（ON）
- EEPROM_VER                  = 5（CONFIG_VERSION=5）

**===== 7. コマンド仕様（完全） =====**
- m0 / m1 / m2         … BUSYソース切替（DIGITAL / A0 / AUTO）
- n####                … 最小受信長（ms）
- b####                … 最大受信長（ms）
- i####                … 直前アイドル時間（ms）
- s0 / s1              … 抑止 OFF / ON
- t0 / t1              … 送信後抑止 OFF / ON
- r####                … TX後無視時間（ms）
- p##                  … 周期ID（分 / 0=停止）※60の約数→正時アライン
- k####                … 周期ID静寂時間（ms）
- w##                  … AUTO観測窓（分）
- L### / G### / a####  … A0 閾値LOW/HIGH/保持(ms)
- d####                … DFPlayerタイムアウト（ms / 0=無効）
- g0 / g1              … TM BUSY極性（LOW=busy / HIGH=busy）
- u0 / u1              … RTCアライン OFF / ON（v2.10追加）
- v                    … ログ表示（直近20件）（v2.21追加）
- v0                   … ログ消去（v2.21追加）
- TYYYYMMDDHHmmss      … RTC時刻設定（v2.10追加）例: T20260311143000
- q                    … 要約（RTC時刻・アライン状態含む）
- H                    … 汎用プリセット（s0/t0/b3900）
- x                    … 停止（Safe Stop）
- R                    … 再開（STOP中のみ有効。大文字）
- F                    … Factory Reset（初期化＋保存）
- 0 / 1 / 2 / 3        … ログレベル（NONE / ERR / INF / DBG）
- h                    … ヘルプ表示

**===== 8. 振る舞い仕様（状態遷移 / BUSY判定） =====**
（v2.01から変更なし。周期IDスケジューラのみ追記）

【周期ID（RTCアライン動作）】
- isAlignablePeriod(pMin) = true かつ rtcAvailable かつ RTC_ALIGN_ON
  → rtcAlignActive=true
- nextPeriodicAt = calcNextAlignedAt(t, pMin, now)
  = now + ((periodSec - totalSec % periodSec) % periodSec) * 1000
  ※ slotSec==0（ちょうど境界）の場合は次の周期に設定
- 発火後: recalcPeriodicAlign(now) で即座に次回時刻を再計算
- 1分ごと: rtcResyncMs 到達で recalcPeriodicAlign(now) を再実行（ドリフト補正）

【周期ID（millis()相対動作）】
- rtcAlignActive=false の場合
- v2.01 互換 catch-up: while(now >= nextPeriodicAt) nextPeriodicAt += PERIOD_MS

**===== 9. EEPROM 仕様 =====**

【レイアウト（CONFIG_VERSION=5 / v2.10）】

 フィールド名        | 型       | コマンド | 説明
---------------------|----------|----------|-------------------------
 magic               | uint32_t | 自動     | 識別子 0xDEADBEEF
 busySrc             | uint8_t  | m        | 0=DIGITAL, 1=A0, 2=AUTO
 suppressOn          | uint8_t  | s        | 0=OFF, 1=ON
 txAfSupOn           | uint8_t  | t        | 0=OFF, 1=ON
 busyMin             | uint32_t | n        | ms
 busyMax             | uint32_t | b        | ms
 idleMin             | uint32_t | i        | ms
 periodMin           | uint32_t | p        | 分
 txSupMs             | uint32_t | r        | ms
 a0Low               | int      | L        | 0〜1023
 a0High              | int      | G        | 0〜1023
 a0Hold              | uint32_t | a        | ms
 autoWinMin          | uint32_t | w        | 分
 dfpTimeoutMs        | uint32_t | d        | ms
 tmBusyActiveHigh    | uint8_t  | g        | 0=LOW=busy, 1=HIGH=busy
 periodQuietMs       | uint32_t | k        | ms
 rtcAlignOn          | uint8_t  | u        | 0=OFF, 1=ON（v2.10追加）
 saveTimestamp       | uint32_t | 自動     | 保存時刻（秒換算）v2.20追加
 ver                 | uint8_t  | 自動     | レイアウト版（現在=6）

【バージョン不一致時の自動補完値】
- v2.01→v2.20: rtcAlignOn=1（ON）、saveTimestamp=0
- v2.10→v2.20: saveTimestamp=0

**===== 10. 制約 =====**
- TM BUSY デバウンス 5ms（固定）
- A0 と Digital（D11）の同時配線は禁止
- EEPROM 書込みは「値変更時のみ」（耐久性考慮）
- SRAM：ATmega328P の 2KB 内で動作
- D13 は SoftwareSerial（RX）兼用のため、外部回路との競合に注意
- A4/A5 は Wire.h が占有するため他用途不可
- DS3231 の T コマンドは秒単位の精度（手打ちのため数秒の誤差は許容）
- AT24C32 未接続中に設定変更した場合、次回モジュール接続時に不一致警告が出る
- AT24C32 のページ書き込み完了待ち 10ms（最大5msに対して余裕あり）

**===== 11. 試験（例） =====**
（v2.01の試験項目に加えて以下を追加）

- RTC正時アライン：p15 設定、DS3231接続
  → q で Align=ON(RTC) を確認
  → 毎時 :00/:15/:30/:45 に周期ID 送出確認

- RTCフォールバック：DS3231 未接続で p15
  → q で Align=OFF(millis) を確認
  → millis() 相対で15分ごとに送出

- 時刻設定：T20260311143000
  → q で 2026/03/11 14:30:xx が表示されること

- RTCアライン OFF：u0
  → q で RTC_ALIGN=OFF、Align=OFF(millis) を確認

- EEPROM マイグレーション：v2.01（ver=4）から書き込み
  → 起動ログに Migration done. が出て既存設定を引き継ぐこと
  → q で EEPROM_VER=5 を確認

**===== 12. リスク =====**
（v2.01のリスクに加えて以下を追加）
- DS3231 のコイン電池切れ → 時刻リセット。電池交換後に T コマンドで再設定が必要。
- T コマンドの手打ちによる秒単位の誤差（許容範囲内）
- I²C バス上に AT24C32（0x57）が存在するが、本プログラムはアクセスしないため競合なし
- A4/A5 を他用途に使用している旧設計との非互換

**===== 13. 変更履歴 =====**
- 詳細は CHANGELOG.md を参照

