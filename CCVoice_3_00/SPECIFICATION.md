========================================================================
【仕様書（技術者向け）】OpenCCVoice v3.00
========================================================================

1. 基本情報
- 名称       : OpenCCVoice Guidance Controller
- バージョン : v3.00 (Unified Logic & Advanced Hardware - All-in-One)
- MCU        : Arduino Nano（ATmega328P, 5V）
- 依存       : Arduino.h / SoftwareSerial.h / EEPROM.h / Wire.h / avr/wdt.h
- DFPlayer   : BUSY監視（D10=LOW 再生中）、9600bps 制御（固定）
- I²Cデバイス: DS3231 RTC（0x68）, AT24C32 EEPROM（0x57）（オプション）
- EEPROM     : version stamp（CONFIG_VERSION=7）
- 配布形態   : 単一ファイル（CCVoice_3_0.ino）

========================================================================
2. 機能仕様（Functional）
========================================================================

(1) カーチャンク自動ID送出
  - D11 または A0 の受信継続時間 dur が
      BUSY_MIN_MS ≤ dur < BUSY_MAX_MS のとき、
      トラック1（001.mp3）を送出。
  - D11 BUSY は極性切替可能（g0/g1）。
      g0: LOW=受信（アクティブLOW）
      g1: HIGH=受信（アクティブHIGH）
  - DFPlayer BUSY（D10）の戻りは「HIGH連続40ms」で再生完了と判定。
  - PTTシーケンス：
      PRE(j####) → 再生 → POST(J####) → 抑止セット

(2) 抑止ロジック（busySupUntil 一本化）★v3.00 仕様
  - 抑止タイマーは `busySupUntil` の1個のみ。
  - 起点A: PTT POST終了時（受信中でない場合）
      busySupUntil = now + TX_SUP_MS
  - 起点B: ②超え受信終了時（通常会話終了）
      busySupUntil = now + TX_SUP_MS
  - 抑止中はカーチャンクID・定周期ID共にスキップ。

(3) AUTO（m2）
  - AUTO_WINDOW（分）観測し、
      D11エッジ数 / A0イベント数の多い側へ自動固定。
  - 判定基準:
      D11<10 かつ A0≥20 → A0 固定
      A0<20 かつ D11≥10 → DIGITAL 固定
      その他 → 多い方
  - 固定後 autoLocked=true、EEPROM 保存。
  - 固定時は D6 LED を 250ms 間隔で 3秒間点滅。

(4) 定周期ID（シンプルスキップ）★v3.00 仕様
  - PERIOD_MS ごとにトラック2/3を交互送出。
  - 以下の全条件を満たす場合のみ送出:
      ① state == IDLE
      ② BUSY が OFF
      ③ (now - lastBusyOffAt) ≥ PERIOD_QUIET_MS
      ④ !isSuppressedNow(now)
  - 条件不成立時は破棄（延期しない）。
  - catch-up 方式廃止（連続発火防止）。

(5) 正時アライン（DS3231 接続時のみ）
  - PERIOD_MS の分が60の約数（10/15/30等）の場合に有効。
  - DS3231時刻に基づき、毎時の :00/:15/:30/:45 等に同期。
  - 1分ごとに RTC 再読取りで補正（ドリフト対策）。
  - DS3231 未接続時または非約数時は millis() 相対動作。

(6) AT24C32 デュアルEEPROM（オプション）
  - 設定変更時、内蔵EEPROMとAT24C32の両方に保存。
  - 起動時、両者のタイムスタンプを比較し、新しい方を優先。
  - AT24C32 未接続時は内蔵EEPROM のみで動作。

(7) イベントログ（AT24C32 接続時のみ）
  - 最大448件のリングバッファ（9バイト/件）。
  - 記録イベント:
      LOG_EVT_BOT (0x01): 起動（data=ver）
      LOG_EVT_PER (0x02): 周期ID送出（data=Track番号）
      LOG_EVT_CAR (0x03): カーチャンク（data=BUSY時間ms）
      LOG_EVT_SUP (0x04): 長話抑止（data=BUSY時間ms）
      LOG_EVT_BST (0x05): バースト抑止（v3.00では未発行、過去ログ表示用）

(8) PTT制御（絶対時刻判定）★v3.00 仕様
  - pttPreEndAt = startPtt時刻 + PTT_PRE_MS
  - pttPostEndAt = 再生完了時刻 + PTT_POST_MS
  - 判定: ((long)(now - pttXxxEndAt) >= 0)
  - Serial.parseInt() ブロッキング中の millis() 進行に強い。

(9) フェイルセーフ（PTT張り付き防止）
  - PLAYING 突入後、DFPlayer BUSY が戻らない場合、
      設定 d####（既定20000ms）到達で強制 PTT OFF。
  - d0 で無効化（PLAYING内チェックスキップ）。

(10) LED可視化 / D3ミラー出力
  - D4: D11 BUSY 表示
  - D7: A0 BUSY 表示
  - D6: 抑止 LED（抑止 / AUTO固定通知）
  - D3: DFPlayer BUSY ミラー（反転）
        再生中=HIGH、停止中=LOW

(11) Safe Stop / Resume
  - x コマンド: 全機能停止（PTT/LED/DFP出力をOFF）
  - R コマンド: 復帰
  - STOP中もシリアルコマンド受付可（R で復帰）

(12) ソフトウェアリセット（Z コマンド）
  - WDT(15ms) によるMCU再起動。
  - EEPROM変更なし（設定保持）。
  - PTT中に実行すると即解放。

========================================================================
3. EEPROM レイアウト（CONFIG_VERSION=7）
========================================================================

struct MyConfig {
  uint32_t magic;             // 0xDEADBEEF
  uint8_t  busySrc;           // [m] 0:DIGITAL, 1:A0, 2:AUTO
  uint32_t busyMin;           // [n] カーチャンク最小受信長(ms)
  uint32_t busyMax;           // [b] カーチャンク最大受信長(ms)
  uint32_t pttPreMs;          // [j] PTT先行無音(ms)
  uint32_t pttPostMs;         // [J] PTT後行無音(ms)
  uint32_t periodMin;         // [p] 周期ID間隔(分)
  uint32_t txSupMs;           // [r] 抑止時間(ms)
  int      a0Low;             // [L] A0閾値LOW
  int      a0High;            // [G] A0閾値HIGH
  uint32_t a0Hold;            // [a] A0保持時間(ms)
  uint32_t autoWinMin;        // [w] AUTO観測窓(分)
  uint32_t dfpTimeoutMs;      // [d] DFPフェイルセーフ(ms)
  uint8_t  tmBusyActiveHigh;  // [g] BUSY極性 (1=HIGH=busy)
  uint32_t periodQuietMs;     // [k] 静寂ガード(ms)
  uint8_t  rtcAlignOn;        // [u] RTCアライン (1=ON)
  uint32_t saveTimestamp;     // 保存時刻（DS3231秒換算）
  uint8_t  ver;               // CONFIG_VERSION (=7)
};

【V6 → V7 マイグレーション】
- 削除フィールド: suppressOn / txAfSupOn / idleMin
- 追加フィールド: pttPreMs / pttPostMs（共に1000msで初期化）
- 自動更新:
    busyMax: 3900 → 1500（デフォルト値変更時のみ）
    txSupMs: 3000 → 10000（デフォルト値変更時のみ）

========================================================================
4. インタフェース仕様（I/O / 配線 - Ver.5）
========================================================================
- D2  : テストSW (INPUT_PULLUP, 1〜3クリックでTRACK 1〜3再生)
- D3  : DFPlayer BUSY ミラー出力（反転）
- D4  : D11系 LED（デジタル受信表示）
- D5  : PTT出力（HIGH=送信）
- D6  : 抑止 LED
- D7  : A0 LED（アナログ受信表示）
- D10 : DFPlayer BUSY入力（LOW=再生中）
- D11 : TM BUSY入力（g0/g1で極性可変）
- D12 : Arduino RX ← DFPlayer TX (SoftwareSerial 9600bps)
- D13 : Arduino TX → DFPlayer RX (SoftwareSerial 9600bps)
- A0  : アナログ入力 0..1023（L/G閾値＋保持 a）
- A4  : I²C SDA (DS3231 + AT24C32)
- A5  : I²C SCL (DS3231 + AT24C32)

========================================================================
5. 既定値（Factory Defaults）
========================================================================
- BUSY_INPUT_SOURCE  = DIGITAL (D11)
- BUSY_MIN_MS        = 500
- BUSY_MAX_MS        = 1500    ★v3.00 変更（旧3900）
- PTT_PRE_MS         = 1000    ★v3.00 新規（可変化）
- PTT_POST_MS        = 1000    ★v3.00 新規（可変化）
- PERIOD_MS          = 30 min
- TX_SUP_MS          = 10000   ★v3.00 変更（旧3000）
- A0_LOW_TH          = 300
- A0_HIGH_TH         = 700
- A0_HOLD            = 800
- AUTO_WINDOW        = 30 min
- DFP_TIMEOUT_MS     = 20000ms
- TM_BUSY_ACTIVE_HIGH = true (HIGH=busy)
- PERIOD_QUIET_MS    = 2000ms
- RTC_ALIGN_ON       = true
- DEBOUNCE_MS        = 5ms（固定）
- EEPROM_VER         = 7

========================================================================
6. コマンド仕様（完全）★v3.00
========================================================================

【受信判定】
- n####  : カーチャンク最小受信長(ms)  範囲: 100以上
- b####  : カーチャンク最大受信長(ms)  範囲: 500以上

【送信制御】
- j####  : PTT先行無音 PRE(ms)        範囲: 100以上
- J####  : PTT後行無音 POST(ms)       範囲: 100以上

【抑止】
- r####  : 抑止時間 SUP(ms)            範囲: 0以上

【定周期】
- p##    : 周期ID間隔(分、0=停止)
- k####  : 静寂ガード(ms)              範囲: 0〜600000

【BUSY入力】
- m0     : DIGITAL (D11) 固定
- m1     : A0 固定
- m2     : AUTO（観測後自動選択）
- g0     : 極性 LOW=busy
- g1     : 極性 HIGH=busy

【A0】
- L###   : 閾値LOW
- G###   : 閾値HIGH
- a####  : 保持時間(ms)
- w##    : AUTO観測窓(分)              範囲: 1以上

【DFP】
- d####  : DFPフェイルセーフ(ms)       範囲: 0〜600000

【RTC】
- TYYYYMMDDHHmmss : 時刻設定 (例: T20260311143000)
- u0 / u1         : アラインOFF/ON

【ログ（AT24C32）】
- v      : 直近20件表示
- v0     : 全件消去

【ユーティリティ】
- V      : バージョン表示
- q      : 設定一覧
- h      : ヘルプ
- x      : Safe Stop
- R      : Resume
- F      : Factory Reset
- Z      : Software Reset (WDT)
- l0..l3 : ログレベル (OFF/MIN/FULL/DBG)

【★v3.00 廃止コマンド】
- s0/s1, t0/t1, i####, H : `[CMD] Obsolete command (廃止)` を返す

========================================================================
7. 振る舞い仕様（状態遷移 / BUSY判定）
========================================================================

【受信長判定】
- dur < BUSY_MIN_MS              → 無効（ノイズ）
- BUSY_MIN_MS ≤ dur < BUSY_MAX_MS → カーチャンク（001送出候補）
- dur ≥ BUSY_MAX_MS              → 通常会話 → 起点B 抑止セット

【抑止セット】
- 起点A: PTT POST終了 + 受信OFF
    busySupUntil = now + TX_SUP_MS
- 起点B: ②超え受信終了
    busySupUntil = now + TX_SUP_MS

【カーチャンク送出条件】
- !pAct (PTT非アクティブ)
- !isSuppressedNow(now) (抑止外)

【定周期ID 送出条件】
- state == IDLE
- !rB (BUSY OFF)
- (now - lastBusyOffAt) ≥ PERIOD_QUIET_MS
- !isSuppressedNow(now)

【PTT状態遷移】
- IDLE → PTT_ON_WAIT (PRE: pttPreEndAt まで待機)
       → PLAYING (DFPlayer 再生)
       → PTT_OFF_WAIT (POST: pttPostEndAt まで待機)
       → IDLE (起点A 抑止セット)

【V コマンド】
- 出力例: [VER] OpenCCVoice v3.00 (EEPROM_VER=7)

【Z コマンド】
- "[RESET] SW reset..." 出力 → delay(100) → wdt_enable(WDTO_15MS)
- EEPROM 変更なし
- PTT中に実行すると即解放

========================================================================
8. 制約
========================================================================
- D11 デバウンス 5ms（固定）
- A0/D11 の同時配線は禁止（誤検知の原因）
- EEPROM 書込みは「値変更時のみ」（耐久性配慮）
- SRAM: ATmega328P の 2KB 内で動作
- k0（periodQuietMs=0）は再起動後に 2000ms に補完される場合あり（既知）
- PTT中に Z を実行すると WDT ハードリセットにより PTT が即解放
- v6 → v7 マイグレーションは片道（v6 へは戻れない）

========================================================================
9. 試験例
========================================================================

【受信判定】
- D11固定（m0）
  600ms BUSY → カーチャンクID（001）送出
  2000ms BUSY → 通常会話扱い → 起点B 抑止セット

【AUTO】
- m2 → w1 → 観測 → AUTO-FIXED → D6 LED高速点滅（3秒）

【送信後抑止（起点A）】
- ID送出後、TX_SUP_MS の間は受信を無視

【フェイルセーフ】
- d20000 → DFPlayer BUSY戻らず20秒 → 強制PTT OFF
- d0 → 無効化

【定周期ID】
- BUSY OFF → 2秒静寂 → 002/003送出
- BUSY中 / 静寂不足 → 破棄（延期しない）

【正時アライン（DS3231接続時）】
- p15 → 毎時 :00/:15/:30/:45 に送出
- 1分ごとに RTC 再取得でドリフト補正

【マイグレーション】
- V6 EEPROM → 起動時に "Migrated from V6" 表示
- q で EEPROM_VER=7 を確認

【コマンド廃止確認】
- s0 入力 → "[CMD] Obsolete command (廃止)"
- t0, i100, H 入力 → 同上

【Z コマンド】
- Z → "[RESET] SW reset..." → 再起動 → 設定保持を確認
- F → Z → 完全クリーン起動を確認

========================================================================
10. リスク
========================================================================
- DFPlayer BUSY の個体差により再生完了のHIGH時刻に±誤差
- プルアップ不足・配線不良により D11/A0 が不安定になる可能性
- A0/D11 の同時配線は禁止
- 受信BUSY信号の極性を誤ると判定が崩壊 → g0/g1 必ず確認
- 周期ID静寂時間（k####）を短くしすぎると割り込み感が戻る
- k0 設定後の再起動で periodQuietMs が 2000ms に補完される場合あり
- PTT 出力中に Z を実行すると PTT が即解放
- DS3231 のコイン電池切れで時刻が 2000/01/01 にリセット
- AT24C32 と内蔵EEPROM のタイムスタンプ不一致時は警告表示

========================================================================
11. 付属ドキュメント
========================================================================
- CHANGELOG.md          : バージョン別の変更履歴
- README.md             : コマンドリファレンス・設定値解説
- NOTES_FOR_UPGRADE.md  : v2.24f → v3.00 アップグレード手順
- DS3231_CONNECTION_GUIDE.md : DS3231/AT24C32接続ガイド
- AUDIO_FILES_GUIDE.md  : 音声ファイル準備ガイド

========================================================================
12. 用語対応表
========================================================================

変数名・定数名はコード互換性のため変更せず、ドキュメントで読み替え。

 コード識別子               | ドキュメント表現
----------------------------|------------------------------
 PIN_TM_BUSY / tmBusy*      | 受信BUSY信号（D11入力）
 TMBUSY_ACTIVE_HIGH         | 受信BUSY信号極性
 busySupUntil               | 統合抑止タイマー
 pttPreEndAt                | PRE終了予定時刻（絶対値）
 pttPostEndAt               | POST終了予定時刻（絶対値）
 起点A                       | PTT POST終了時の抑止セット
 起点B                       | ②超え受信終了時の抑止セット
 rtcAlignActive             | RTC正時アライン動作中フラグ
 nextPeriodicAt             | 次回周期ID発火予定時刻
 lastBusyOffAt              | 最終BUSY OFF時刻（静寂ガード基準）

========================================================================
13. v2.24f との主な違い（v3.00 サマリー）
========================================================================

| 領域 | v2.24f | v3.00 |
|------|--------|-------|
| 抑止タイマー | 4種 | 1種（busySupUntil） |
| PTT判定 | 相対時間 | 絶対時刻 |
| 定周期ID | catch-up方式 | スキップ方式 |
| ファイル数 | 3 | 1 |
| EEPROM_VER | 6 | 7 |
| BUSY_MAX デフォルト | 3900ms | 1500ms |
| TX_SUP デフォルト | 3000ms | 10000ms |
| バースト抑止 | 有 | 廃止 |
| 送信直後ガード | 有（postTxIgnore） | 廃止（busySupUntilに統合） |
| s/t/i/H コマンド | 有 | 廃止 |
| j/J コマンド | 無 | 新規追加 |

========================================================================
End of SPECIFICATION.md
========================================================================
