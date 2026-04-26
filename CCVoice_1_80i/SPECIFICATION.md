========================================================================
【仕様書（技術者向け）】
========================================================================

1. 基本情報
- 名称     : OpenCCVoice Guidance Controller
- バージョン: v1.80i
- MCU      : Arduino Nano（ATmega328P, 5V）
- 依存     : Arduino.h / SoftwareSerial.h / EEPROM.h / avr/wdt.h
- DFPlayer : BUSY監視（D7=LOW 再生中）、9600bps 制御（固定）
- EEPROM   : CONFIG_VERSION=5（v1.80h から変更なし）
              → v1.73系（ver=4）からの更新時は自動マイグレーションを実施
              → v1.80h からの書き込みは EEPROM 操作不要

========================================================================
2. 機能仕様（Functional）
========================================================================

(1) カーチャンク受信の自動ID送出
  - D6 または A0 の受信継続時間 dur が
      BUSY_MIN_MS（①） ≤ dur < BUSY_MAX_MS（②） のとき、
      トラック1（001）を送出。
  - D6 BUSY は極性切替可能（g0/g1）。
      g0: LOW=受信（アクティブLOW）
      g1: HIGH=受信（アクティブHIGH）
  - DFPlayer BUSY（D7）の戻りは「HIGH連続40ms」で再生完了と判定。
  - PTT シーケンス：
      PRE=j#### ms → 再生 → POST=J#### ms。
      PRE/POST は絶対時刻（pttPreEndAt / pttPostEndAt）で判定する。

(2) 抑止（⑤ busySupUntil 一元管理）
  - 抑止は busySupUntil タイマー一本で管理（常時有効）。
  - 抑止の起点：
      起点A: ④ POST 終了の瞬間（カーチャンクID送出後・受信中でない場合）
      起点B: ② 超え受信（通常会話）の終了の瞬間
      POST 終了時に受信中の場合は起点B が起点A を上書き。
  - 旧来の「長話抑止（longSupUntil）」「連打抑止（burstSupUntil）」
    「送信直後ガード（postTxIgnore）」はすべて廃止し、本タイマーに統合。

(3) AUTO（m2）
  - AUTO_WINDOW（分）観測し、
      D6エッジ数 / A0イベント数の多い側へ自動固定。
  - 固定後 autoLocked=true、EEPROM 保存（再起動後も反映）。
  - AUTO固定時は D13 LED を高速点滅（約3秒）。

(4) 定周期ID（スキップ仕様 — v1.80h 変更、v1.80i 継承）
  - PERIOD_MS ごとにトラック2/3を交互送出。
  - 周期タイミングが到来したとき、以下の全条件を満たした場合のみ送出：
      1) state == IDLE
      2) BUSY == false
      3) (now - lastBusyOffAt) >= PERIOD_QUIET_MS（静寂条件）
      4) 抑止期間外（busySupUntil を経過している）
  - 条件不成立の場合はその回をスキップ（破棄）。
    旧仕様の「延期（periodicDue 保持）」とは異なり、次の定周期タイミングまで待機する。
  - ★注意：スキップ仕様のため、通話が頻繁な環境では送出機会が減少する。
    k#### を短くするか p## の周期を見直すこと。

(5) A0 BUSY観測
  - PTT送出中（pAct=true）は A0 の観測を停止（a0Detect=false, a0Busy=false）。
  - PTT 送出中でない場合は A0 ヒステリシス判定を継続。
  - AUTO 判定用カウント（a0_event_count）は抑止中はインクリメントしない。

(6) EEPROM 永続化
  - n / b / j / J / r / p / k / m / g / L / G / a / w / d
    の変更で即保存。
  - ログレベル（l0〜l3）は EEPROM 保存なし（起動時は常に LOG_MIN）。
  - 起動時ロード時には以下を実施：
      1) magic 不一致 → 初期化（applyDefaults）
      2) ver 不一致 → 自動移行（新規項目補完、不正値補正）
      3) ver=4 からの移行時の補完：
         pttPreMs / pttPostMs を 1000ms で補完
         busyMax が 3900ms の場合のみ 1500ms に更新
         txSupMs が 3000ms の場合のみ 10000ms に更新

(7) LED可視化 / D2ミラー出力
  - D6 LED（D4）: D6 BUSY 表示
  - A0 LED（D12）: A0 BUSY 表示
  - 抑止LED（D13）: ⑤抑止中点灯 / AUTO固定通知（高速点滅）
  - 未使用LEDはゆっくり点滅（待機表示）
  - D2: DFPlayer BUSY ミラー（反転）
        再生中=HIGH、停止中=LOW

(8) フェイルセーフ（PTT張り付き防止）
  - PLAYING突入後、DFPlayer BUSY が戻らない場合、
      設定 d####（既定20000ms）到達で強制 PTT OFF。
  - d0 で無効化（PLAYING内でのチェックスキップ）。

(9) PRE/POST バグ修正（v1.80h）・コード整理（v1.80i）
  - Serial.setTimeout(50) を setup() で一度だけ設定。
  - PTT_ON_WAIT の待機判定を pttPreEndAt（絶対時刻）で行う。
  - PTT_OFF_WAIT の待機判定を pttPostEndAt（絶対時刻）で行う。
  - ブロッキング（parseInt）で now がずれても PRE/POST が確実に保証される。

========================================================================
3. 非機能仕様（Non-Functional）
========================================================================
- 応答性：D6 デバウンス 5ms
- 安定性：DFPlayer BUSY の HIGH確定に 40ms
- ガード：PTT 最低 ON ガード搭載（誤送信防止）
- 可搬性：Arduino IDE 1.8.x / 2.x
- 保守性：
    - printSummary() による状態一覧
      （EEPROM_VER / PRE / POST / SUP / QUIET / BUSY極性 含む）
    - ログレベル選択（l0〜l3）
- 安全性：DFPlayer タイムアウト監視 / ⑤抑止による誤発火防止
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
- D10 : Arduino RX（← DFPlayer TX）
- D11 : Arduino TX（→ DFPlayer RX）
- D12 : A0 LED（アナログ受信表示）
- D13 : 抑止LED（⑤抑止中・AUTO固定通知）
- A0  : アナログ入力 0..1023（L/G閾値＋保持 a）

========================================================================
5. 既定値（Factory Defaults）
========================================================================
- BUSY_INPUT_SOURCE           = D6
- BUSY_MIN_MS（①）           = 500ms
- BUSY_MAX_MS（②）           = 1500ms
- PTT_PRE_MS（③）            = 1000ms
- PTT_POST_MS（④）           = 1000ms
- TX_SUP_MS（⑤）             = 10000ms
- PERIOD_MS（⑥）             = 30分
- PERIOD_QUIET_MS             = 2000ms
- A0_LOW_TH                   = 300
- A0_HIGH_TH                  = 700
- A0_HOLD                     = 800ms
- AUTO_WINDOW                 = 30分
- D6 デバウンス               = 5ms
- DFP_TIMEOUT_MS              = 20000ms
- TM_BUSY_ACTIVE_HIGH         = true（HIGH=busy）
- CONFIG_VERSION              = 5

========================================================================
6. コマンド仕様（完全）
========================================================================
- n####                … ① 最小受信長（ms）（100〜）
- b####                … ② 最大受信長（ms）（500〜）
- j####                … ③ PTT先行無音時間 PRE（ms）（100〜）★v1.80h新設
- J####                … ④ PTT後行無音時間 POST（ms）（100〜）★v1.80h新設
- r####                … ⑤ 抑止時間（ms）（0〜）
- p##                  … ⑥ 定周期ID（分 / 0=停止）
- k####                … 定周期IDの静寂条件（ms / 0〜600000）
- m0 / m1 / m2         … BUSYソース切替（D6 / A0 / AUTO）
- g0 / g1              … BUSY極性（LOW=busy / HIGH=busy）
- L### / G### / a####  … A0 閾値LOW/HIGH/保持(ms)
- w##                  … AUTO観測窓（分）
- d####                … DFPlayerタイムアウト（ms / 0=無効）
- V                    … バージョン表示（スケッチver + EEPROM_VER）
- q                    … 要約（EEPROM_VER / PRE / POST / SUP / QUIET / POL 含む）
- x                    … 停止（Safe Stop）
- R                    … 復帰（STOP中のみ）★v1.80h変更（旧: r）
- F                    … Factory Reset（初期化＋保存）
- Z                    … ソフトウェアリセット（WDT 15ms / EEPROM変更なし）
- l0 / l1 / l2 / l3   … ログレベル（OFF/MIN/FULL/DBG）★v1.80h変更（旧: 0/1/2/3）
- h                    … コマンド一覧

【廃止されたコマンド（v1.73f1 以前）】
- s0/s1                … 抑止全体 ON/OFF（廃止。抑止は常時有効）
- t0/t1                … 送信後抑止 ON/OFF（廃止。r0 で実質 OFF）
- i####                … 送信直後ガード閾値（廃止。⑤抑止に統合）
- H                    … 汎用プリセット（廃止）
- r（引数なし）        … STOP復帰（廃止。R に変更）
- 0/1/2/3（単体数字）  … ログレベル（廃止。l0〜l3 に変更）

========================================================================
7. 振る舞い仕様（状態遷移 / BUSY判定）
========================================================================
【受信長判定】
- dur < BUSY_MIN_MS              → 無効（ノイズ）
- BUSY_MIN_MS ≤ dur < BUSY_MAX_MS → カーチャンク（001送出候補）
- dur ≥ BUSY_MAX_MS              → 通常会話 → ⑤抑止開始（起点B）

【カーチャンク送出条件】
- pAct == false（PTT送出中でない）
- isSuppressedNow() == false（⑤抑止期間外）
- 上記を満たす場合：busySupUntil = now + TX_SUP_MS をセットし、startPtt(1)

【抑止（⑤ busySupUntil）】
- 起点A: PTT_OFF_WAIT → IDLE 遷移時（受信中でない場合）
    busySupUntil = now + TX_SUP_MS
- 起点B: dur ≥ BUSY_MAX_MS の受信終了時
    busySupUntil = now + TX_SUP_MS
- 受信中に POST が終わった場合: IDLE 遷移時に受信中なら起点B へ委ねる（[SUP] Defer to RX-end ログ）
- isSuppressedNow() : (long)(now - busySupUntil) < 0

【定周期ID（スキップ仕様 — v1.80h 変更、v1.80i 継承）】
- while ループで nextPeriodicAt を未来へ追い付かせ、crossed=true なら送出判定を行う（最大1回）
- crossed=true かつ以下の全条件を満たした場合に送出：
  1) state == IDLE
  2) BUSY == false
  3) (now - lastBusyOffAt) >= PERIOD_QUIET_MS
  4) isSuppressedNow() == false
- 条件不成立 → その回をスキップ（periodicDue は保持しない）

【フェイルセーフ】
- PLAYING →
  (now - playingEnterAt >= d####) → 強制 PTT_OFF_WAIT 遷移
- d0 → 判定スキップ

【PTT状態遷移】
- IDLE → PTT_ON_WAIT（PTT ON、pttPreEndAt = now + PTT_PRE_MS）
       → PLAYING（DFPlayer 再生コマンド送出）
       → PTT_OFF_WAIT（pttPostEndAt = now + PTT_POST_MS）
       → IDLE（PTT OFF。受信中でない場合、⑤抑止開始（起点A））

【V コマンド】
- スケッチバージョン文字列と config.ver を Serial 出力
- 出力例：[VER] OpenCCVoice v1.80i (EEPROM_VER=5)

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
- EEPROM 耐久性に配慮（無関係な再書き込みを避ける）
- SRAM：ATmega328P の 2KB 内で動作
- ログレベル（l0〜l3）は EEPROM に保存されない。起動時は常に l1=MIN
- PTT 出力中に Z を実行すると WDT ハードリセットにより PTT が即解放される
- 定周期IDはスキップ仕様のため、通話が頻繁な環境では送出機会が減少する

========================================================================
9. 試験（例）
========================================================================
- D6固定（m0）
  600ms BUSY → カーチャンク→ ID送出
  2000ms BUSY → 通常会話 → ⑤抑止（dur ≥ b1500）

- AUTO：m2 → w1
  → 観測 → AUTO-FIXED → D13高速点滅

- 抑止：r10000
  → カーチャンク送出後 10秒の ⑤ 抑止
  → 同期間内のカーチャンクはスキップ（[RX] CK ... skip(sup remain=####ms)）

- フェイルセーフ：d20000
  → DFPlayer BUSY戻らず 20秒 → 強制 PTT OFF
  → d0 で無効化可能

- 定周期ID（スキップ仕様）
  BUSY OFF → 2秒静寂（k2000）→ 002/003送出
  BUSY中 / 静寂不足 / 抑止中 → スキップ（[Periodic] SKIP reason:...）
  スキップ後は次の定周期タイミングまで待機

- PRE/POST 保証（v1.80h 修正・v1.80i 継承）
  シリアルコマンド受信中でも PRE/POST が確実に保持される（絶対時刻比較）

- BUSY極性
  g0=LOW=busy
  g1=HIGH=busy

- バージョン確認
  V → "[VER] OpenCCVoice v1.80i (EEPROM_VER=5)" を確認

- ソフトウェアリセット
  Z → "[RESET] Software reset by WDT..." → 再起動 → 設定保持を確認
  F → Z → 完全クリーン起動を確認

========================================================================
10. リスク
========================================================================
- DFPlayer BUSY の個体差により再生完了のHIGH時刻に±誤差
- プルアップ不足・配線不良により D6/A0 が不安定になる可能性
- A0/D6 の同時配線は禁止（誤検知の原因）
- BUSY 極性を誤るとカーチャンク/通常会話判定が崩壊する → g0/g1 必ず確認
- 定周期ID静寂時間（k####）を長くしすぎるとスキップが多発する
- PTT 出力中に Z を実行すると PTT が即解放される（意図せず実行しないこと）
- ② MAX（b####）のデフォルト変更（3900ms → 1500ms）により、
  旧バージョンとカーチャンク判定範囲が異なる。運用環境に応じて b#### を再確認すること

========================================================================
11. 付属ドキュメント
========================================================================
- CHANGELOG.md          : バージョン別の変更履歴
- README.md             : コマンドリファレンス・設定値解説
- NOTES_FOR_UPGRADE.md  : アップグレード手順・チェックリスト
- OpenCCVoice_TimingChart.xlsx : タイムチャート
  各挙動（カーチャンクID送出 / 通常会話抑止 / 抑止中スキップ / 定周期ID静寂ガード / PRE/POST保証動作）を
  共通タイムライン（1セル=200ms）で図示。

========================================================================
12. 用語対応表（コード識別子 ↔ ドキュメント表現）
========================================================================

変数名・定数名はコード互換性のため変更せず、ドキュメントおよびコメントのみ
読み替えを行っています。

 コード識別子               | ドキュメント表現
----------------------------|------------------------------
 PIN_TM_BUSY / tmBusy*      | 受信BUSY信号（D6入力）
 TMBUSY_ACTIVE_HIGH         | 受信BUSY信号極性
 busySupUntil               | ⑤抑止終了時刻
 TX_SUP_MS                  | ⑤抑止時間
 PTT_PRE_MS / pttPreEndAt   | ③ PTT先行無音時間（PRE）/ PRE終了予定時刻
 PTT_POST_MS / pttPostEndAt | ④ PTT後行無音時間（POST）/ POST終了予定時刻
 BUSY_MIN_MS                | ① カーチャンク最小受信長
 BUSY_MAX_MS                | ② カーチャンク最大受信長
 PERIOD_QUIET_MS            | 定周期送出前の静寂待機時間
 lastBusyOffAt              | 最後にBUSYがOFFになった時刻（静寂起点）

【v1.73系から削除された識別子（v1.80h/v1.80i 以降は存在しない）】

 コード識別子               | 旧用途
----------------------------|------------------------------
 postTxIgnore               | 送信直後ガードフラグ（⑤抑止に統合）
 postTxIdleStart            | 送信直後ガード解除用計測
 IDLE_MIN_MS / idleMin      | 送信後BUSY安定待ち時間
 BURST_WIN_MS / burstWinStart| 連打検出窓
 BURST_SUP_MS / burstSupUntil| 連打抑止
 BURST_TH / burstCount      | 連打閾値・カウンタ
 LONG_SUP_MS / longSupUntil | 長話抑止（⑤抑止に統合）
 suppressOn                 | 抑止全体ON/OFF
 txAfSupOn                  | 送信後抑止ON/OFF
 SUPPRESSORS_ENABLED        | 抑止全体有効フラグ
 TX_AFTER_SUPPRESS_ENABLED  | 送信後抑止有効フラグ
 stateTimer                 | ステート遷移タイマー（絶対時刻比較に置き換え）
 periodicDue                | 周期ID送出待ちフラグ（スキップ仕様に変更）
 lastTriggerAt              | 最終ID送出時刻（不応期ロジック削除により廃止）
 REFRAC_MS                  | 短発ID不応期（廃止）

========================================================================
