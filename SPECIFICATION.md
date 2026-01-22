
========================================================================
【仕様書（技術者向け）】
========================================================================

1. 基本情報
- 名称     : OpenCCVoice Guidance Controller
- バージョン: v1.73-Unified-Safe
- MCU      : Arduino Nano（ATmega328P, 5V）
- 依存     : Arduino.h / SoftwareSerial.h / EEPROM.h
- DFPlayer : BUSY監視（D7=LOW 再生中）、9600bps 制御（固定）

2. 機能仕様（Functional）
(1) 短発受信の自動ID送出
  - D6 または A0 の受信継続時間 dur が BUSY_MIN_MS ≤ dur < BUSY_MAX_MS のとき、
    トラック1（001）を送出。
  - DFPlayer BUSY（D7）の戻りは HIGH 連続 40ms で再生完了判定。
  - PTT シーケンス：PRE=1000ms → 再生 → POST=1000ms。

(2) 抑止（長話・バースト・送信後）
  - 長話抑止：dur ≥ BUSY_MAX_MS → LONG_SUP_MS=10s の抑止。
  - バースト抑止：10s 窓内で短発（BUSY_MIN_MS〜BUSY_MAX_MS）が 2回以上 → BURST_SUP_MS=10s。
  - 送信後抑止：t1 有効時、送信終了後 TX_SUP_MS(ms) の受信無視（自局送出後の誤認対策）。

(3) AUTO（m2）
  - AUTO_WINDOW（分）中に D6 エッジ数 / A0 イベント数を計測し、優位側へ自動固定。
  - 固定後 autoLocked=true、EEPROM 保存（再起動後も維持）。
  - AUTO固定時、D13 LED を高速点滅（約3秒）で通知。

(4) 周期ID
  - PERIOD_MS ごとに、トラック2/3を交互で送出（IDLE時のみ実行）。
  - スケジュールは起動時刻基準の絶対時刻で等間隔実行。

(5) EEPROM 永続化
  - m / n / b / i / p / r / L / G / a / w / s / t の変更で即保存。
  - 起動時にロード。magic 不一致時は applyDefaults() を適用。

(6) LED 可視化 / D2 ミラー
  - 使用側 LED（D6系=D4, A0系=D12）：実反応を表示。
  - 未使用側 LED：点滅（待機表示）。
  - 抑止 LED（D13）：抑止発動/短発窓動作/AUTO固定通知を表現。
  - D2：DFPlayer BUSY ミラー出力（反転）… 再生中=HIGH / 停止中=LOW。

(7) フェイルセーフ（PTT張り付き防止）
  - PLAYING 以降、DFPlayer の BUSY 応答が出ない/戻らない場合でも、
    開始から 8 秒経過で強制的に PTT OFF へ遷移（DFP_TIMEOUT_MS=8000）。

3. 非機能仕様（Non-Functional）
- 応答性：D6 デバウンス 5ms。
- 安定性：DFPlayer BUSY の HIGH 確定に 40ms 安定時間。PTT 最低 ON ガード搭載。
- 可搬性：Arduino IDE 1.8.x / 2.x。
- 保守性：printSummary() による要約、ログレベル（0:NONE/1:ERR/2:INF/3:DBG）。
- 安全性：フェイルセーフ 8s（DFPlayer 応答異常時の送信張り付き防止）。

4. インタフェース仕様（I/O / 配線）
- D2  : DFP BUSY ミラー出力（反転）… 再生中=HIGH / 停止中=LOW
- D3  : テストSW（INPUT_PULLUP, 1〜3クリックでトラック1〜3）
- D4  : D6 系 LED（受信表示）
- D5  : PTT 出力（HIGH=送信）
- D6  : TM BUSY 入力（回路により極性可変。既定は HIGH=受信）
- D7  : DFPlayer BUSY 入力（多くの個体で LOW=再生中）
- D10 : Arduino RX（← DFPlayer TX）
- D11 : Arduino TX（→ DFPlayer RX）
- D12 : A0 系 LED（アナログ受信表示）
- D13 : 抑止/AUTO LED（抑止・短発窓・AUTO固定通知）
- A0  : アナログ入力（0..1023、ヒステリシス L/G + 保持 a）

5. 既定値（Factory Defaults）
- BUSY_INPUT_SOURCE = D6
- BUSY_MIN_MS  = 500
- BUSY_MAX_MS  = 3900   （= LONG_TALK_MS）
- IDLE_MIN_MS  = 200
- PERIOD_MS    = 30 min
- TX_SUP_MS    = 3000
- SUPPRESSORS_ENABLED       = true
- TX_AFTER_SUPPRESS_ENABLED = true
- A0_LOW_TH = 300 / A0_HIGH_TH = 700 / A0_HOLD = 800
- AUTO_WINDOW = 30 min
- D6 デバウンス = 5ms
- DFPlayer フェイルセーフ = 8s

6. コマンド仕様（完全）
- m0 / m1 / m2         … モード切替（D6固定 / A0固定 / AUTO）
- n####                … 最小受信時間（ms）
- b####                … 最大受信時間（ms）
- i####                … 直前アイドル最小時間（ms）  ※使用
- s0 / s1              … 抑止 OFF / ON
- t0 / t1              … 送信後抑止 OFF / ON（s1 時のみ有効）
- r####                … 送信後抑止（ms）
- p##                  … 周期ID（分, 0=停止）
- w##                  … AUTO 観測窓（分）
- L### / G### / a####  … A0 閾値LOW / 閾値HIGH / 保持(ms)
- q                    … 現在値の要約表示
- H                    … 汎用プリセット適用（s0 / t0 / b3900）
- x / r                … 停止（Safe Stop）/ 再開（停止中のみ有効）
- F                    … Factory Reset（初期値へ戻し EEPROM 保存）
- 0 / 1 / 2 / 3        … ログレベル（NONE/ERR/INF/DBG）

7. 振る舞い仕様（状態遷移 / 判定）
- 受信長判定：
  - dur <  MIN                → 無効
  - MIN ≤ dur < MAX          → 短発（ID 送出候補）
  - dur ≥ MAX                → 長話抑止（LONG_SUP_MS）
- 短発連続：10s 窓で 2 回以上 → バースト抑止（BURST_SUP_MS）
- 送信後抑止：t1 & r#### で TX 後の受信無視
- PTT ステート：
  - IDLE → PTT_ON_WAIT（PRE）→ PLAYING（D7 LOW→HIGH確定）→ PTT_OFF_WAIT（POST）→ IDLE

8. 制約
- D6 デバウンス 5ms（可変なし）
- EEPROM 書込みは「値変更時のみ」
- SRAM 制約（ATmega328P 2KB 内で動作）

9. 試験（例）
- m0（D6 固定）：
  - 600ms 受信 → ID 送出（短発）
  - 4000ms 受信 → 長話抑止
- AUTO：m2 → w1 → 観測満了で [AUTO-FIXED] ログ & D13 高速点滅（~3s）
- 送信後抑止：t1 / r3000 → 自局送出直後 3s の受信無視
- フェイルセーフ：DFPlayer BUSY 応答異常でも 8s で強制 PTT OFF

10. リスク
- DFPlayer BUSY の個体差（LOW/HIGH の応答タイミング差）
- 外部回路のプルアップ不足による D6/A0 の不安定
- A0/D6 の同時配線禁止（片側のみ運用）… AUTO は観測/固定ロジックであり、配線は常に片側

11. 変更履歴
- 詳細は CHANGELOG.md を参照

========================================================================
