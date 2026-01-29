
# 📄 **OpenCCVoice Guidance Controller — 技術者向け仕様書（v1.73d）**

（全文テキスト）

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
      - ★ NEW: 「BUSY が OFF になってから periodQuietMs(ms) 静寂継続」
                の条件を満たす場合のみ送出。
      - BUSY中・静寂不足・抑止期間中は送出せず延期（periodicDue保持）。

    (5) EEPROM 永続化（v1.73c〜）
      - m / n / b / i / p / r / L / G / a / w / s / t / d / g / k（★追加）
        の変更で即保存。
      - 起動時ロード時には以下を実施：
          1) magic 不一致 → 初期化（applyDefaults）
          2) ver 不一致 → 自動移行（新規項目補完）
          3) 不正値は安全値に補完
             （例：tmBusyActiveHigh=1、dfpTimeoutMs=20000、periodQuietMs=2000）

    (6) LED可視化 / D2ミラー出力
      - D6 LED（D4）: D6 BUSY 表示
      - A0 LED（D12）: A0 BUSY 表示
      - 抑止LED（D13）: 抑止 / バースト窓 / AUTO固定通知
      - 未使用LEDはゆっくり点滅（待機表示）
      - D2: DFPlayer BUSY ミラー（反転）
            再生中=HIGH、停止中=LOW

    (7) フェイルセーフ（PTT張り付き防止）
      - PLAYING突入後、DFPlayer BUSY が戻らない場合、
          設定 d####（既定20000ms）到達で強制 PTT OFF。
      - d0 で無効化（PLAYING内でのチェックスキップ）。

    ========================================================================
    3. 非機能仕様（Non-Functional）
    ========================================================================
    - 応答性：D6 デバウンス 5ms
    - 安定性：DFPlayer BUSY の HIGH確定に 40ms
    - ガード：PTT 最低 ON ガード搭載（誤送信防止）
    - 可搬性：Arduino IDE 1.8.x / 2.x
    - 保守性：
        - printSummary() による状態一覧
          （EEPROM_VER / QUIET(ms) / BUSY極性 含む）
        - ログレベル選択（0〜3）
    - 安全性：DFPlayer タイムアウト監視
    - 耐久性：EEPROM 書込みは「値変更時のみ」

    ========================================================================
    4. インタフェース仕様（I/O / 配線）
    ========================================================================
    - D2  : DFPlayer BUSY ミラー出力（反転）… 再生中=HIGH / 停止中=LOW
    - D3  : テストSW（INPUT_PULLUP, 1〜3クリックでTRACK1〜3再生）
    - D4  : D6系 LED（デジタル受信表示）
    - D5  : PTT出力（HIGH=送信）
    - D6  : TM BUSY入力（g0/g1で極性可変）
            g0 = LOW=busy（アクティブLOW）
            g1 = HIGH=busy（アクティブHIGH）
    - D7  : DFPlayer BUSY入力（LOW=再生中）
    - D10 : Arduino RX（DFPlayer TX）
    - D11 : Arduino TX（DFPlayer RX）
    - D12 : A0 LED（アナログ受信表示）
    - D13 : 抑止LED（抑止・短発・AUTO固定）
    - A0  : アナログ入力 0..1023（L/G閾値＋保持 a）

    ========================================================================
    5. 既定値（Factory Defaults）
    ========================================================================
    - BUSY_INPUT_SOURCE           = D6
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
    - D6 デバウンス               = 5ms
    - DFP_TIMEOUT_MS              = 20000ms
    - TM_BUSY_ACTIVE_HIGH         = true（HIGH=busy）
    - periodQuietMs               = 2000ms （★v1.73d 新規）
    - EEPROM_VER                  = 4（CONFIG_VERSION=4）

    ========================================================================
    6. コマンド仕様（完全）
    ========================================================================
    - m0 / m1 / m2         … BUSYソース切替（D6 / A0 / AUTO）
    - n####                … 最小受信長（ms）
    - b####                … 最大受信長（ms）
    - i####                … 直前アイドル時間（ms）
    - s0 / s1              … 抑止 OFF / ON
    - t0 / t1              … 送信後抑止 OFF / ON
    - r####                … TX後無視時間（ms）
    - p##                  … 周期ID（分 / 0=停止）
    - k####                … ★周期ID静寂時間（ms）
    - w##                  … AUTO観測窓（分）
    - L### / G### / a####  … A0 閾値LOW/HIGH/保持(ms)
    - d####                … DFPlayerタイムアウト（ms / 0=無効）
    - g0 / g1              … TM BUSY極性（LOW=busy / HIGH=busy）
    - q                    … 要約（EEPROM_VER / QUIET(ms) / BUSY_POL含む）
    - H                    … 汎用プリセット（s0/t0/b3900）
    - x / r                … 停止（Safe Stop）/ 再開
    - F                    … Factory Reset（初期化＋保存）
    - 0 / 1 / 2 / 3        … ログレベル（NONE/ERR/INF/DBG）

    ========================================================================
    7. 振る舞い仕様（状態遷移 / BUSY判定）
    ========================================================================
    【受信長判定】
    - dur < BUSY_MIN_MS          → 無効
    - BUSY_MIN_MS ≤ dur < BUSY_MAX_MS → 短発（001）
    - dur ≥ BUSY_MAX_MS          → 長話抑止（10秒）

    【短発連続（バースト）】
    - 10s 窓で短発が2回以上 → BURST_SUP_MS=10秒 抑止

    【送信後抑止】
    - s1 & t1 → TX後 r#### ms の BUSY を無視

    【周期ID（v1.73d 静寂ガード付き）】
    - periodicDue=true かつ state=IDLE のとき、
      次の全条件を満たした場合に送出：
      1) BUSY=false（受信していない）
      2) (now - lastBusyOffAt) ≥ periodQuietMs
      3) 抑止期間外（長話/バースト/送信後ガード）
    - 条件不成立 → periodicDue を保持（延期）

    【フェイルセーフ】
    - PLAYING → (now - playingEnterAt ≥ d####) → 強制PTT OFF
    - d0 → 判定スキップ（無効）

    【PTTステート】
    - IDLE → PTT_ON_WAIT（PRE）
           → PLAYING（DFP BUSY LOW→HIGH）
           → PTT_OFF_WAIT（POST）
           → IDLE

    ========================================================================
    8. 制約
    ========================================================================
    - D6 デバウンス 5ms（固定）
    - A0/D6 の同時配線禁止（AUTOは論理選択であり、配線は片側のみ）
    - EEPROM 書込みは「値変更時のみ」
    - EEPROM 耐久性に配慮（無変更時は書かない）
    - SRAM：ATmega328P 2KB 内で運用可能

    ========================================================================
    9. 試験（例）
    ========================================================================
    - D6固定（m0）
      600ms BUSY → ID送出  
      4000ms BUSY → 長話抑止発動

    - AUTO：m2 → w1  
      → 観測 → [AUTO-FIXED] → D13高速点滅

    - 送信後抑止：t1 / r3000  
      → 自局送出後 3s の BUSY を無視

    - フェイルセーフ：d20000  
      → DFPlayer BUSY戻らず20秒 → 強制PTT OFF  
      → d0 で判定無効

    - 周期ID（v1.73d）
      BUSY OFF → 2秒静寂 → 周期ID送出  
      BUSY中/静寂不足は送
