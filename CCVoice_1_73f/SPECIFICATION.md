========================================================================
【仕様書（技術者向け）】
========================================================================

1. 基本情報
- 名称     : OpenCCVoice Guidance Controller
- バージョン: v1.73f-Unified-Safe
- MCU      : Arduino Nano（ATmega328P, 5V）
- 依存     : Arduino.h / SoftwareSerial.h / EEPROM.h / avr/wdt.h
- DFPlayer : BUSY監視（D7=LOW 再生中）、9600bps 制御（固定）
- EEPROM   : version stamp を導入（CONFIG_VERSION=4）
              → v1.73d/e と同一レイアウト。スケッチ更新のみで移行可。
              → v1.73c 以前からの更新時は自動マイグレーションを実施

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

(2) 抑止（長話・連打・送信後・送信直後ガード）
  - 長話抑止      ：dur ≥ BUSY_MAX_MS → LONG_SUP_MS=10s。
  - 連打抑止    ：10秒窓内で短発が2回以上 → BURST_SUP_MS=10s。
  - 送信後抑止    ：t1有効時、TX後 TX_SUP_MS(ms) の受信を無視。
  - 送信直後ガード（★v1.73e）：
      PTT OFF 直後に postTxIgnore=true をセット。
      BUSY が idleMin ms 連続 OFF になるまで 短発・周期ID を抑止扱いにする。
      isSuppressedNow() に統合されており、送信後抑止とは独立して動作。

(3) AUTO（m2）
  - AUTO_WINDOW（分）観測し、
      D6エッジ数 / A0イベント数の多い側へ自動固定。
  - 固定後 autoLocked=true、EEPROM 保存（再起動後も反映）。
  - AUTO固定時は D13 LED を高速点滅（約3秒）。

(4) 周期ID（v1.73d/e 改良版）
  - PERIOD_MS ごとにトラック2/3を交互送出。
  - IDLE時のみ動作。
  - 静寂ガード（v1.73d）：
      「BUSY が OFF になってから periodQuietMs(ms) 静寂継続」の条件を満たす場合のみ送出。
      BUSY中・静寂不足・抑止期間中は送出せず延期（periodicDue保持）。
  - catch-up 対策（★v1.73e）：
      nextPeriodicAt を while で未来へ追い付かせ、periodicDue は最大1回のみ立てる。
      長時間延期後も連続送出しない。

(5) A0 BUSY観測（★v1.73e 改良）
  - 抑止中であっても a0Busy の観測は継続。
  - 抑止中は AUTO 判定用カウント（a0_event_count）のみ増加させない。
  - これにより、抑止解除後の AUTO 判定がずれる問題を解消。

(6) EEPROM 永続化（v1.73c〜）
  - m / n / b / i / p / r / L / G / a / w / s / t / d / g / k
    の変更で即保存。
  - 起動時ロード時には以下を実施：
      1) magic 不一致 → 初期化（applyDefaults）
      2) ver 不一致 → 自動移行（新規項目補完）
      3) 不正値は安全値に補完
         （例：tmBusyActiveHigh=1、dfpTimeoutMs=20000、periodQuietMs=2000）
  - 注意：k0（periodQuietMs=0）は補完ロジックで 2000ms に戻る場合あり。
          再起動後は q で QUIET(ms) を確認すること。

(7) LED可視化 / D2ミラー出力
  - D6 LED（D4）: D6 BUSY 表示
  - A0 LED（D12）: A0 BUSY 表示
  - 抑止LED（D13）: 抑止 / 連打検出窓 / AUTO固定通知
  - 未使用LEDはゆっくり点滅（待機表示）
  - D2: DFPlayer BUSY ミラー（反転）
        再生中=HIGH、停止中=LOW

(8) フェイルセーフ（PTT張り付き防止）
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
- 安全性：DFPlayer タイムアウト監視 / 送信直後ガード（postTxIgnore）
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
- periodQuietMs               = 2000ms
- EEPROM_VER                  = 4（CONFIG_VERSION=4、v1.73dと同一）

========================================================================
6. コマンド仕様（完全）
========================================================================
- m0 / m1 / m2         … BUSYソース切替（D6 / A0 / AUTO）
- n####                … 最小受信長（ms）
- b####                … 最大受信長（ms）
- i####                … 送信後BUSY安定待ち時間（ms）/ postTxIgnore解除閾値
- s0 / s1              … 抑止 OFF / ON
- t0 / t1              … 送信後抑止 OFF / ON
- r####                … TX後無視時間（ms）※STOP中は r のみで復帰コマンド
- p##                  … 周期ID（分 / 0=停止）
- k####                … 周期ID静寂時間（ms / 0=無効、再起動後に2000に戻る場合あり）
- w##                  … AUTO観測窓（分）
- L### / G### / a####  … A0 閾値LOW/HIGH/保持(ms)
- d####                … DFPlayerタイムアウト（ms / 0=無効）
- g0 / g1              … TM BUSY極性（LOW=busy / HIGH=busy）
- v                    … バージョン表示（スケッチver + EEPROM_VER）
- q                    … 要約（EEPROM_VER / QUIET(ms) / BUSY_POL含む）
- H                    … 汎用プリセット（s0/t0/b3900）
- x / r                … 停止（Safe Stop）/ 再開（STOP中のみ）
- F                    … Factory Reset（初期化＋保存）
- Z                    … ソフトウェアリセット（WDT 15ms / EEPROM変更なし）
- 0 / 1 / 2 / 3        … ログレベル（NONE / ERR / INF / DBG）

========================================================================
7. 振る舞い仕様（状態遷移 / BUSY判定）
========================================================================
【受信長判定】
- dur < BUSY_MIN_MS              → 無効
- BUSY_MIN_MS ≤ dur < BUSY_MAX_MS → 短発（001送出候補）
- dur ≥ BUSY_MAX_MS              → 長話抑止（10秒）

【短発連続（連打抑止）】
- 10s 窓で短発が2回以上 → BURST_SUP_MS=10秒 抑止

【送信後抑止】
- s1 & t1 → TX後 r#### ms の BUSY を無視

【送信直後ガード（★v1.73e）】
- PTT OFF → postTxIgnore=true
- BUSY が idleMin ms 連続 OFF → postTxIgnore=false（自動解除）
- 解除までの間、isSuppressedNow()=true（短発・周期ID ともに抑止扱い）

【周期ID（v1.73d/e 静寂ガード・catch-up対策付き）】
- while ループで nextPeriodicAt を未来へ追い付かせ、crossed=true なら periodicDue=true（最大1回）
- periodicDue=true かつ state=IDLE のとき、
  次の全条件を満たした場合に送出：
  1) BUSY=false
  2) (now - lastBusyOffAt) ≥ periodQuietMs
  3) 抑止期間外（長話/連打/送信後/送信直後ガード）
- 条件不成立 → periodicDue を保持（延期）

【フェイルセーフ】
- PLAYING →
  (now - playingEnterAt ≥ d####) → 強制 PTT OFF
- d0 → 判定スキップ

【PTT状態遷移】
- IDLE → PTT_ON_WAIT（PRE）
       → PLAYING
       → PTT_OFF_WAIT（POST）
       → IDLE（★postTxIgnore=true をセット）

【v コマンド】
- スケッチバージョン文字列と config.ver を Serial 出力
- 出力例：[VER] OpenCCVoice v1.73f (EEPROM_VER=4)

【Z コマンド：ソフトウェアリセット】
- Serial に "[RESET] Software reset by WDT..." を出力後 delay(100)
- wdt_enable(WDTO_15MS) → while(1) で WDT 発火 → MCU リセット
- EEPROM は変更しない
- PTT 出力中に実行した場合、WDT ハードリセットにより PTT は即解放される

========================================================================
8. 制約
========================================================================
- D6 デバウンス 5ms（固定）
- A0/D6 の同時配線は禁止
- EEPROM 書込みは「値変更時のみ」
- EEPROM耐久性に配慮（無関係な再書き込みを避ける）
- SRAM：ATmega328P の 2KB 内で動作
- k0（periodQuietMs=0）は再起動後に 2000ms に補完される場合がある（既知）
- PTT 出力中に Z を実行すると WDT ハードリセットにより PTT が即解放される

========================================================================
9. 試験（例）
========================================================================
- D6固定（m0）
  600ms BUSY → ID送出
  4000ms BUSY → 長話抑止

- AUTO：m2 → w1
  → 観測 → AUTO-FIXED → D13高速点滅

- 送信後抑止：t1 / r3000
  → 自局送出後 3s の BUSY を無視

- フェイルセーフ：d20000
  → DFPlayer BUSY戻らず20秒 → 強制PTT OFF
  → d0 で無効化可能

- 周期ID（v1.73d/e）
  BUSY OFF → 2秒静寂 → 002/003送出
  BUSY中/静寂不足 → 延期

- 送信直後ガード（v1.73e）
  PTT OFF後に A0 残留BUSY発生 → postTxIgnore 中は ID 送出なし
  BUSY が idleMin ms 連続 OFF → ガード解除
  ログレベル 3 で "[POST-TX] cleared by idle-stable" を確認

- catch-up 対策（v1.73e）
  長時間 BUSY 後に周期ID 解除 → 最大1回のみ送出
  ログレベル 3 で "[Periodic] due flag set (catch-up)" を確認

- TM BUSY極性
  g0=LOW=busy
  g1=HIGH=busy

- バージョン確認（v1.73f）
  v → "[VER] OpenCCVoice v1.73f (EEPROM_VER=4)" を確認

- ソフトウェアリセット（v1.73f）
  Z → "[RESET] Software reset by WDT..." → 再起動 → 設定保持を確認
  F → Z → 完全クリーン起動を確認

========================================================================
10. リスク
========================================================================
- DFPlayer BUSY の個体差により再生完了のHIGH時刻に±誤差
- プルアップ不足・配線不良により D6/A0 が不安定になる可能性
- A0/D6 の同時配線は禁止（誤検知の原因）
- 受信BUSY信号（TM BUSY）の極性を誤ると短発/長話判定が崩壊する → g0/g1 必ず確認
- 周期ID静寂時間（k####）を短くしすぎると割り込み感が戻る
- k0 設定後の再起動で periodQuietMs が 2000ms に補完される場合がある（既知）
- PTT 出力中に Z を実行すると PTT が即解放される（意図せず実行しないこと）

========================================================================
11. 付属ドキュメント
========================================================================
- CHANGELOG.md          : バージョン別の変更履歴
- README.md             : コマンドリファレンス・設定値解説
- NOTES_FOR_UPGRADE.md  : アップグレード手順・チェックリスト
- OpenCCVoice_TimingChart.xlsx : タイムチャート
  各挙動（短発ID送出 / 長話抑止 / 連打抑止 / 送信直後ガード / 周期ID静寂ガード）を
  共通タイムライン（1セル=200ms）で図示。①〜⑳の番号解説付き。

========================================================================

========================================================================
12. 用語対応表（コード識別子 ↔ ドキュメント表現）
========================================================================

変数名・定数名はコード互換性のため変更せず、ドキュメントおよびコメントのみ
読み替えを行っています。詳細な変更履歴は CHANGELOG.md 付録を参照。

 コード識別子               | ドキュメント表現
----------------------------|------------------------------
 PIN_TM_BUSY / tmBusy*      | 受信BUSY信号（D6入力）
 TMBUSY_ACTIVE_HIGH         | 受信BUSY信号極性
 BURST_WIN_MS / burstWin*   | 連打検出窓
 BURST_SUP_MS / burstSup*   | 連打抑止
 BURST_TH / burstCount      | 連打閾値 / 連打カウンタ
 postTxIgnore               | 送信直後ガード
 postTxIdleStart            | 送信直後ガード解除用BUSY安定計測
 idleMin / IDLE_MIN_MS      | 送信後BUSY安定待ち時間

========================================================================
