========================================================================
【仕様書（技術者向け）】
========================================================================

1. 基本情報
- 名称     : OpenCCVoice Guidance Controller
- バージョン: v1.73d-Unified-Safe
- MCU      : Arduino Nano（ATmega328P, 5V）
- 依存     : Arduino.h / SoftwareSerial.h / EEPROM.h
- DFPlayer : BUSY監視（D7=LOW 再生中）、9600bps 制御（固定）
- EEPROM   : version stamp を導入（CONFIG_VERSION=4）
              → スケッチ更新時に既存設定を安全に移行 or 初期化
              → 新規項目（periodQuietMs）を補完してアップデート

========================================================================
2. 機能仕様（Functional）
========================================================================

(1) 短発受信の自動ID送出
  - D6 または A0 の受信継続時間 dur が
      BUSY_MIN_MS ≤ dur < BUSY_MAX_MS のとき、
      トラック1（001）を送出。
  - D6 BUSY は極性切替可能（g0/g1）。
      g0: LOW=受信（アクティブLOW）
      g1: HIGH=受信（アクティブHIGH）
  - DFPlayer BUSY（D7）の戻りは「HIGH連続40ms」で再生完了と判定。
  - PTT シーケンス：
      PRE=1000ms → 再生 → POST=1000ms。

(2) 抑止（長話・バースト・送信後）
  - 長話抑止      ：dur ≥ BUSY_MAX_MS → LONG_SUP_MS=10s。
  - バースト抑止  ：10秒窓内で短発が2回以上 → BURST_SUP_MS=10s。
  - 送信後抑止    ：t1有効時、TX後 TX_SUP_MS(ms) の受信を無視。

(3) AUTO（m2）
  - AUTO_WINDOW（分）観測し、
      D6エッジ数 / A0イベント数の多い側へ自動固定。
  - 固定後 autoLocked=true、EEPROM 保存（再起動後も反映）。
  - AUTO固定時は D13 LED を高速点滅（約3秒）。

(4) 周期ID（v1.73dで大幅改良）
  - PERIOD_MS ごとにトラック2/3を交互送出。
  - IDLE時のみ動作。
  - ★NEW: 「BUSYがOFFになってから periodQuietMs(ms) 静寂継続」
           の条件を満たした場合のみ送出。
  - 条件不成立（BUSY中/静寂不足/抑止期間中）は延期
      → periodicDue を保持し、条件成立後に送出する。
  - 静寂時間は k#### で設定（ms）。既定2000ms。

(5) EEPROM 永続化（v1.73c〜）
  - m / n / b / i / p / r / L / G / a / w / s / t / d / g / k（★追加）
    の変更で即保存。
  - 起動時ロード時は以下を適用：
      1) magic 不一致 → applyDefaults()（初期化）
      2) ver 不一致 → 新規項目補完・移行（migration）
      3) 不正値（0xFF等）は安全値に補完
         （例：tmBusyActiveHigh=1, dfpTimeoutMs=20000,
                periodQuietMs=2000 など）

(6) LED可視化 / D2ミラー出力
  - D6 LED（D4）: デジタルBUSY表示
  - A0 LED（D12）: A0 BUSY表示
  - 抑止LED（D13）: 抑止・AUTO固定・短発窓表示
  - 未使用側LEDはゆっくり点滅（区別のため）
  - D2: DFPlayer BUSYミラー（反転）
        再生中=HIGH / 停止中=LOW

(7) フェイルセーフ（PTT張り付き防止）
  - PLAYING 以降、DFPlayer BUSY が戻らない場合、
      設定 d####（既定20000ms = 20秒）で強制 PTT OFF。
  - d0 で無効化可能（PLAYING 内判定スキップ）。

========================================================================
3. 非機能仕様（Non-Functional）
========================================================================
- 応答性：D6 デバウンス 5ms。
- 安定性：D7 BUSY の HIGH確定に 40ms の安定時間。
- PTT 最低ONガード搭載（誤送信防止）。
- 可搬性：Arduino IDE 1.8.x / 2.x。
- 保守性：
    - printSummary() による状態確認
      （EEPROM_VER / TM_BUSY_POL / QUIET(ms) を含む）
    - ログレベル（0〜3）
- 安全性：フェイルセーフ（20秒既定、d####で可変、d0で無効）。
- 耐久性：EEPROM 書込みは「値変更時のみ」。

========================================================================
4. インタフェース仕様（I/O / 配線）
========================================================================
- D2  : DFPlayer BUSY ミラー出力（反転）… 再生中=HIGH / 停止中=LOW
- D3  : テストSW（INPUT_PULLUP, 1〜3クリックでトラック1〜3）
- D4  : D6 系 LED（デジタル受信表示）
- D5  : PTT 出力（HIGH=送信）
- D6  : TM BUSY入力（g0/g1 により極性可変）
        g0 = LOW=受信（アクティブLOW）
        g1 = HIGH=受信（アクティブHIGH）
- D7  : DFPlayer BUSY入力（LOW=再生中）
- D10 : Arduino RX（← DFPlayer TX）
- D11 : Arduino TX（→ DFPlayer RX）
- D12 : A0 系 LED（アナログ受信表示）
- D13 : 抑止/AUTO LED（抑止・短発・AUTO固定通知）
- A0  : アナログ入力（0..1023、ヒステリシス L/G + 保持 a）

========================================================================
5. 既定値（Factory Defaults）
========================================================================
- BUSY_INPUT_SOURCE           = D6
- BUSY_MIN_MS                 = 500
- BUSY_MAX_MS                 = 3900   (= LONG_TALK_MS)
- IDLE_MIN_MS                 = 200
- PERIOD_MS                   = 30 min
- TX_SUP_MS                   = 3000
- SUPPRESSORS_ENABLED         = true
- TX_AFTER_SUPPRESS_ENABLED   = true
- A0_LOW_TH                   = 300
- A0_HIGH_TH                  = 700
- A0_HOLD                     = 800
- AUTO_WINDOW                 = 30 min
- D6 デバウンス               = 5ms
- DFP_TIMEOUT_MS              = 20000ms
- TM_BUSY_ACTIVE_HIGH         = true（=HIGHがBUSY）
- periodQuietMs               = 2000ms  （★v1.73d新規）
- EEPROM_VER                  = 4（CONFIG_VERSION=4）

========================================================================
6. コマンド仕様（完全）
========================================================================
- m0 / m1 / m2         … モード切替（D6固定 / A0固定 / AUTO）
- n####                … 最小受信時間（ms）
- b####                … 最大受信時間（ms）
- i####                … 直前アイドル最小時間（ms）
- s0 / s1              … 抑止 OFF / ON
- t0 / t1              … 送信後抑止 OFF / ON（s1 時のみ有効）
- r####                … 送信後抑止（ms）
- p##                  … 周期ID（分, 0=停止）
- k####                … ★周期ID 静寂時間（ms）
- w##                  … AUTO 観測窓（分）
- L### / G### / a####  … A0閾値LOW / HIGH / 保持(ms)
- d####                … DFPフェイルセーフ（ms, 0=無効）
- g0 / g1              … TM BUSY 極性設定（0:LOW=BUSY, 1:HIGH=BUSY）
- q                    … 現在値の要約表示（QUIET / BUSY_POL / EEPROM_VER）
- H                    … 汎用プリセット（s0 / t0 / b3900）
- x / r                … 停止（Safe Stop）/ 再開（停止時のみ有効）
- F                    … Factory Reset（初期化+EEPROM保存）
- 0 / 1 / 2 / 3        … ログレベル（NONE / ERR / INF / DBG）

========================================================================
7. 振る舞い仕様（状態遷移 / BUSY判定）
========================================================================
【受信長判定】
- dur < MIN                → 無効
- MIN ≤ dur < MAX         → 短発（ID送出候補）
- dur ≥ MAX               → 長話抑止（LONG_SUP_MS）

【短発連続】
- 10s窓で2回以上          → BURST_SUP_MS=10s 抑止

【送信後抑止】
- s1 & t1 & r####         → TX後の BUSY を無視

【周期ID（v1.73d 静寂ガード）】
- periodicDue = true の状態で
  - state == IDLE
  - BUSY == false
  - (now - lastBusyOffAt) >= periodQuietMs
  - 抑止期間外（長話 / バースト / 送信後）
  をすべて満たした時のみ周期ID送出。

- 条件不成立 → periodicDue を保持（延期）

【フェイルセーフ】
- PLAYING →
  (now - playingEnterAt >= d####) → 強制PTT OFF
- d0 の場合はチェックをスキップ

【PTT状態遷移】
- IDLE → PTT_ON_WAIT（PRE）
       → PLAYING（D7 LOW→HIGH確定）
       → PTT_OFF_WAIT（POST）
       → IDLE

========================================================================
8. 制約
========================================================================
- D6 デバウンス 5ms（固定）
- EEPROM 書込みは「値変更時のみ」
- EEPROMの寿