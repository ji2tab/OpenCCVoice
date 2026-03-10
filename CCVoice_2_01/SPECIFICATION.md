========================================================================
【仕様書（技術者向け）】
OpenCCVoice Guidance Controller — v2.01
========================================================================

1. 基本情報
- 名称      : OpenCCVoice Guidance Controller
- バージョン : v2.01 — Ver.5 ピンマップ対応版
- MCU       : Arduino Nano（ATmega328P, 5V）
- 対象基板  : Ver.5
- 依存      : Arduino.h / SoftwareSerial.h / EEPROM.h
- DFPlayer  : BUSY監視（D10=LOW 再生中）、9600bps 制御（SoftwareSerial D12/D13）
- EEPROM    : version stamp を継続（CONFIG_VERSION=4）
               → v1.73d/e と同一レイアウト、既存設定を引き継ぎ可能

========================================================================
2. ピンアサイン（Ver.5）
========================================================================

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
 D12  | ARD_TX_TO_DFP       | 出力   | Arduino TX → DFPlayer RX     | SoftwareSerial 9600bps
 D13  | ARD_RX_FROM_DFP     | 入力   | Arduino RX ← DFPlayer TX     | SoftwareSerial 9600bps
 A0   | A0_PIN              | 入力   | アナログBUSY                  | 0〜1023、ヒステリシス＋保持

========================================================================
3. 機能仕様（Functional）
========================================================================

(1) 短発受信の自動ID送出
  - TM BUSY（D11）または A0 の受信継続時間 dur が
      BUSY_MIN_MS ≤ dur < BUSY_MAX_MS のとき、Track 1（001）を送出。
  - TM BUSY は極性切替可能（g0/g1）。
      g0: LOW=受信（アクティブLOW）、INPUT_PULLUP と組み合わせる場合は注意
      g1: HIGH=受信（アクティブHIGH）（既定）
  - DFPlayer BUSY（D10）の戻りは「HIGH連続40ms」で再生完了と判定。
  - PTT シーケンス：PRE=1000ms → 再生 → POST=1000ms。

(2) 抑止（長話・バースト・送信後）
  - 長話抑止      : dur ≥ BUSY_MAX_MS → LONG_SUP_MS=10s
  - バースト抑止  : 10秒窓内で短発が2回以上 → BURST_SUP_MS=10s
  - 送信後抑止    : t1有効時、TX後 TX_SUP_MS(ms) の受信を無視
  - 送信直後ガード: PTT OFF 直後、BUSYが idleMin 連続OFF まで全発火禁止
                    （postTxIgnore フラグ）

(3) AUTO（m2）
  - AUTO_WINDOW（分）観測し、
    Digital エッジ数 / A0 イベント数の多い側へ自動固定。
  - 固定後 autoLocked=true、EEPROM 保存（再起動後も反映）。

(4) 周期ID
  - PERIOD_MS ごとに Track 2/3 を交互送出。
  - IDLE時のみ動作。
  - 送出条件（全て満たすこと）：
    1) periodicDue=true
    2) state==IDLE
    3) readBusy()==false
    4) BUSY が OFFになってから periodQuietMs(ms) 以上静寂
    5) 抑止中でない（長話/バースト/送信後ガード）
  - BUSY中・静寂不足・抑止期間中は送出せず延期（periodicDue 保持）。
  - catch-up 対策：nextPeriodicAt を while で未来へ追い付かせ、
    periodicDue は最大1回分のみ立つ設計。

(5) EEPROM 永続化
  - m / n / b / i / p / r / L / G / a / w / s / t / d / g / k
    の変更で即保存。
  - 起動時ロード時には以下を実施：
    1) magic 不一致 → 初期化（applyDefaults）
    2) ver 不一致 → 自動移行（新規項目補完）
    3) 不正値は安全値に補完
       （tmBusyActiveHigh=1、dfpTimeoutMs=20000、periodQuietMs=2000）

(6) LED表示 / D3ミラー出力
  - ModBusy LED（D4）: readBusy()=true で点灯
  - 抑止 LED（D6）   : isSuppressedNow()=true で点灯
  - A0検知 LED（D7） : a0Busy=true で点灯
  - D3               : DFPlayer BUSY ミラー（反転）再生中=HIGH

(7) フェイルセーフ（PTT張り付き防止）
  - PLAYING突入後、DFPlayer BUSY が戻らない場合、
    設定 d####（既定20000ms）到達で強制 PTT OFF。
  - d0 で無効化。

(8) テストSW（D2）
  - 1クリック → Track 1 再生
  - 2クリック → Track 2 再生
  - 3クリック → Track 3 再生
  - 1秒窓でクリック数をカウント

========================================================================
4. 非機能仕様（Non-Functional）
========================================================================
- 応答性 : TM BUSY デバウンス 5ms
- 安定性 : DFPlayer BUSY の HIGH確定に 40ms
- ガード : PTT 最低 ON ガード搭載（誤送信防止）
- 可搬性 : Arduino IDE 1.8.x / 2.x
- 保守性 : printSummary() による状態一覧
           （EEPROM_VER / QUIET(ms) / BUSY極性 含む）
- 安全性 : DFPlayer タイムアウト監視 / PTT張り付き防止
- 耐久性 : EEPROM 書込みは「値変更時のみ」

========================================================================
5. インタフェース仕様（I/O 詳細）
========================================================================

【入力】
- D2  (INPUT_PULLUP) : テストSW。LOW=押下（アクティブLOW）。
                       チャタリングは1秒窓によるソフト処理。
- D10 (INPUT_PULLUP) : DFPlayer BUSY。LOW=再生中（アクティブLOW）。
- D11 (INPUT_PULLUP) : TM BUSY。g0: LOW=busy / g1: HIGH=busy（既定）。
                       デバウンス 5ms。
                       ※g0設定時は INPUT_PULLUP と外部信号の競合に注意。
- A0                 : アナログBUSY入力（0〜1023 / 基準VCC=5V）。
                       LOW閾値 300（≒1.46V）/ HIGH閾値 700（≒3.42V）。
                       HOLDタイマ 800ms。

【出力】
- D3  : DFPlayer BUSY ミラー（反転）。再生中=HIGH（5V）、停止中=LOW（0V）。
- D4  : ModBusy LED。BUSY中 HIGH（5V）。
- D5  : PTT。送信中 HIGH（5V）。PTT_PRE 1000ms 先行、PTT_POST 1000ms 保持。
- D6  : 抑止 LED。抑止中 HIGH（5V）。
- D7  : A0検知 LED。a0Busy 中 HIGH（5V）。

【SoftwareSerial（DFPlayer通信）】
- D12 (TX) : Arduino → DFPlayer RX。UART 9600bps、TTLレベル（5V）。
- D13 (RX) : DFPlayer TX → Arduino。UART 9600bps、TTLレベル（5V）。
             ※D13 は Arduino Nano オンボード LED と共有。
             LED が通信の負荷になる場合は直列抵抗（470Ω程度）を推奨。

========================================================================
6. 既定値（Factory Defaults）
========================================================================
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
- TM BUSY デバウンス          = 5ms
- DFP_TIMEOUT_MS              = 20000ms
- TM_BUSY_ACTIVE_HIGH         = true（HIGH=busy）
- periodQuietMs               = 2000ms
- EEPROM_VER                  = 4（CONFIG_VERSION=4）

========================================================================
7. コマンド仕様（完全）
========================================================================
- m0 / m1 / m2         … BUSYソース切替（DIGITAL / A0 / AUTO）
- n####                … 最小受信長（ms）
- b####                … 最大受信長（ms）
- i####                … 直前アイドル時間（ms）
- s0 / s1              … 抑止 OFF / ON
- t0 / t1              … 送信後抑止 OFF / ON
- r####                … TX後無視時間（ms）
- p##                  … 周期ID（分 / 0=停止）
- k####                … 周期ID静寂時間（ms）
- w##                  … AUTO観測窓（分）
- L### / G### / a####  … A0 閾値LOW/HIGH/保持(ms)
- d####                … DFPlayerタイムアウト（ms / 0=無効）
- g0 / g1              … TM BUSY極性（LOW=busy / HIGH=busy）
- q                    … 要約（EEPROM_VER / QUIET(ms) / BUSY_POL含む）
- H                    … 汎用プリセット（s0/t0/b3900）
- x                    … 停止（Safe Stop）
- R                    … 再開（STOP中のみ有効。大文字）
- F                    … Factory Reset（初期化＋保存）
- 0 / 1 / 2 / 3        … ログレベル（NONE / ERR / INF / DBG）
- h                    … ヘルプ表示

========================================================================
8. 振る舞い仕様（状態遷移 / BUSY判定）
========================================================================

【受信長判定】
- dur < BUSY_MIN_MS               → 無効（ノイズ）
- BUSY_MIN_MS ≤ dur < BUSY_MAX_MS → 短発（001 送出候補）
- dur ≥ BUSY_MAX_MS               → 長話抑止（10秒）

【短発連続（バースト抑止）】
- 10s 窓で短発が2回以上 → BURST_SUP_MS=10秒 抑止

【送信後抑止】
- s1 & t1 → TX後 r#### ms の BUSY を無視

【送信直後ガード（postTxIgnore）】
- PTT OFF 直後から、BUSYが連続 idleMin 以上 OFF になるまで全発火禁止
- A0 残留BUSY・スケルチテールによる誤発火を防止

【周期ID（静寂ガード付き）】
- periodicDue=true かつ state=IDLE のとき、次の全条件を満たした場合のみ送出：
  1) BUSY=false
  2) (now - lastBusyOffAt) ≥ periodQuietMs
  3) 抑止期間外（長話/バースト/送信後ガード）
- 条件不成立 → periodicDue を保持（延期）
- catch-up: 複数周期をまたいでも最大1回分のみ送出

【フェイルセーフ】
- PLAYING → (now - playingEnterAt ≥ d####) → 強制 PTT OFF
- d0 → 判定スキップ

【PTT状態遷移】
- IDLE → PTT_ON_WAIT（PTT 先行 1000ms）
       → PLAYING（DFPlayer BUSY 監視）
       → PTT_OFF_WAIT（PTT 保持 1000ms）
       → IDLE

========================================================================
9. EEPROM 仕様
========================================================================

【レイアウト（CONFIG_VERSION=4 / v1.73d 以降共通）】

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
 periodQuietMs       | uint32_t | k        | ms（v1.73d 追加）
 ver                 | uint8_t  | 自動     | レイアウト版（現在=4）

【バージョン不一致時の自動補完値】
- tmBusyActiveHigh  → 1（HIGH=busy）
- dfpTimeoutMs      → 20000
- periodQuietMs     → 2000

========================================================================
10. 制約
========================================================================
- TM BUSY デバウンス 5ms（固定）
- A0 と Digital（D11）の同時配線は禁止
- EEPROM 書込みは「値変更時のみ」（耐久性考慮）
- SRAM：ATmega328P の 2KB 内で動作
- D13 は SoftwareSerial（RX）兼用のため、外部回路との競合に注意
- g0（LOW=busy）設定時は INPUT_PULLUP との競合に注意

========================================================================
11. 試験（例）
========================================================================
- Digital固定（m0）
  600ms BUSY → ID送出
  4000ms BUSY → 長話抑止

- AUTO：m2 → w1
  → 観測 → AUTO-FIXED

- 送信後抑止：t1 / r3000
  → 自局送出後 3s の BUSY を無視

- フェイルセーフ：d20000
  → DFPlayer BUSY戻らず20秒 → 強制 PTT OFF
  → d0 で無効化可能

- 周期ID
  BUSY OFF → 2秒静寂 → 002/003 送出
  BUSY中/静寂不足 → 延期

- TM BUSY極性：g0=LOW=busy / g1=HIGH=busy

- STOP/RESUME：x → R（大文字）

- テストSW：D2を1〜3クリック → Track 1〜3 再生

========================================================================
12. リスク
========================================================================
- DFPlayer BUSY の個体差により再生完了の HIGH 時刻に±誤差あり
- プルアップ不足・配線不良により D11/A0 が不安定になる可能性
- A0 と Digital の同時配線は禁止（誤検知の原因）
- TM BUSY 極性を誤ると短発/長話判定が崩壊する → g0/g1 必ず確認
- 周期ID静寂時間（k####）を短くしすぎると割り込み感が戻る
- D13 オンボード LED が SoftwareSerial の負荷になる可能性（要確認）

========================================================================
13. 変更履歴
========================================================================
- 詳細は CHANGELOG.md を参照

========================================================================
