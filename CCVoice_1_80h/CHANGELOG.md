# CHANGELOG.md - OpenCCVoice ID Guidance Controller

本プロジェクトは、Arduino Nano (ATmega328P, 5V) 上で動作する、DMR無線機の ID送出支援コントローラです。

記法: Keep a Changelog 準拠  
日付: JST（日本標準時）

--------------------------------------------------------------------

## v1.80h — 仕様刷新・抑止ロジック統合・バグ修正 (2026-03-24)

v1.73f1 からの直接アップグレード版。v1.73f1 と v1.80h の間にリリースされた
中間バージョンは v1.80h に統合され、廃止扱いとなります。

### Changed（仕様変更）

- **カーチャンク判定範囲を明確化**
  - ① MIN(n) 以上 ② MAX(b) 未満の受信継続時間をカーチャンクと判定する仕様に統一
  - ② MAX のデフォルト: 3900ms → **1500ms**（通常の通話時間との境界を明確化）
  - ⑤ 抑止時間(r) のデフォルト: 3000ms → **10000ms**

- **PTT 先行・後行無音時間をコマンドで可変化**
  - ③ PTT先行無音時間(PRE): `j####` コマンドで設定（デフォルト 1000ms）
  - ④ PTT後行無音時間(POST): `J####` コマンドで設定（デフォルト 1000ms）
  - いずれも EEPROM に保存される

- **通常会話終了時の抑止起点を統一**
  - 起点A: ④ POST 終了の瞬間（カーチャンクID送出後・受信中でない場合）
  - 起点B: ② 超え受信の終了の瞬間（通常会話終了）
  - 受信中に POST が終わった場合は起点B が起点A を上書き

- **連打抑止（BURSTロジック）を削除**
  - 旧 BURST ロジックはカーチャンクが来るたびに抑止を延長し続ける問題があった
  - 抑止は ⑤抑止（busySupUntil）のみで一元管理

- **定周期IDのスキップ仕様に変更**
  - 受信中・抑止中・PTT中に周期が到来したらその回をスキップ（破棄）
  - 旧仕様の「延期（periodicDue 保持）」から変更

- **ログレベルコマンドを変更**
  - 旧 `0/1/2/3` → 新 `l0/l1/l2/l3` に変更
  - l0=OFF / l1=MIN（起動時デフォルト）/ l2=FULL / l3=DBG
  - EEPROM 保存なし（起動時は常に l1=MIN）

- **STOP 復帰コマンドを変更**
  - `r`（小文字）→ `R`（大文字）に変更
  - 旧来 `r` コマンドが「送信後無視時間設定」と「STOP復帰」を兼用していた問題を解消

### Added（追加）

- `j####` コマンド: ③ PTT先行無音時間 PRE（ms）設定・EEPROM保存
- `J####` コマンド: ④ PTT後行無音時間 POST（ms）設定・EEPROM保存
- 抑止中カーチャンクのスキップログに残り時間を付記（l1=MIN から表示）
  - `[RX] CK dur=###ms skip(pAct)` — PTT送出中のスキップ
  - `[RX] CK dur=###ms skip(sup remain=####ms)` — 抑止中のスキップ（残り時間付き）
- 抑止状態変化検出ログを追加
  - `[SUP] ACTIVE` — 抑止開始時
  - `[SUP] CLEAR` — 抑止解除時
- `[SUP] START ####ms (long-talk dur=####ms)` — 長話受信終了時の抑止開始ログ
- `[SUP] START ####ms (from POST-end)` — POST終了起点の抑止開始ログ
- `[SUP] Defer to RX-end (point-B)` — 受信中の POST 終了時（起点B 待ち）ログ

### Removed（削除）

- `s0/s1` コマンド（抑止全体 ON/OFF）→ 抑止は常時有効
- `t0/t1` コマンド（送信後抑止 ON/OFF）→ `r0` で実質 OFF
- `i####` コマンド（送信直後ガード閾値）→ ⑤抑止に統合
- `H` コマンド（汎用プリセット）
- 送信直後ガード（postTxIgnore）変数・ロジック → ⑤抑止に統合
- 連打抑止関連の定数: `BURST_WIN_MS` / `BURST_TH` / `BURST_SUP_MS`
- 連打抑止関連の変数: `burstWinStart` / `burstSupUntil` / `burstCount`
- 長話専用抑止タイマー変数 `longSupUntil`（⑤抑止タイマー busySupUntil に統合）
- EEPROM フィールド: `suppressOn` / `txAfSupOn` / `idleMin`
- 旧ログレベルコマンド `0/1/2/3`（単体数字）
- STOP 復帰コマンド `r`（小文字）

### Fixed（バグ修正）

- **PRE/POST 無音時間がスキップされる問題を修正**
  - `Serial.setTimeout(50)` を `setup()` で一度だけ設定するよう変更
  - 旧実装では `handleSerialCmd()` 内で毎回 `Serial.setTimeout()` を呼んでいたため、
    `parseInt()` のブロッキング中に `millis()` が進み、PRE 無音時間がスキップされる場合があった

- **PRE/POST 待機を絶対時刻比較に変更**
  - `stateTimer`（相対比較: `now - stateTimer >= PTT_PRE_MS`）を廃止
  - `pttPreEndAt` / `pttPostEndAt`（絶対時刻: `(long)(now - pttPreEndAt) >= 0`）に置き換え
  - ブロッキングで `now` がずれても PRE/POST が確実に保証される

### EEPROM

- `CONFIG_VERSION`: 4 → **5**
- 追加フィールド: `pttPreMs` / `pttPostMs`
- 削除フィールド: `suppressOn` / `txAfSupOn` / `idleMin`
- ver=4（v1.73系）からの自動マイグレーション対応
  - `pttPreMs` / `pttPostMs` を 1000ms で補完
  - `busyMax` が旧デフォルト値 3900ms の場合のみ 1500ms に更新
  - `txSupMs` が旧デフォルト値 3000ms の場合のみ 10000ms に更新

### Notes

- EEPROM はスケッチ書き込み後に自動マイグレーションされるため、
  原則として `F`（Factory Reset）は不要
- 起動後に `V` でバージョンを、`q` で設定を確認すること
- 定周期IDがスキップされやすい場合は `k####` を短くするか、
  `p##` の周期を見直すこと

--------------------------------------------------------------------

## v1.73f1 — コマンド競合回避（`v` → `V` 変更） (2026-03-18)

⚠️ **廃止（v1.80h に統合）** — 歴史的記録として保持

### Changed
- **`v` コマンドを `V`（大文字）に変更**
  - v2.x 系では `v` をイベントログ表示コマンドとして使用するため、
    将来の統合・移行時の競合を避けるため大文字 `V` に変更
  - 機能・出力内容は変更なし
  - 出力例：`[VER] OpenCCVoice v1.73f1 (EEPROM_VER=4)`

### Notes
- EEPROM レイアウトは **v1.73f と同一（`config.ver = 4`）**

--------------------------------------------------------------------

## v1.73f — Unified/Safe + Version/Reset Commands (2026-02-13)

⚠️ **廃止（v1.80h に統合）** — 歴史的記録として保持

### Added
- **`V` コマンド（v1.73f では `v`）：バージョン確認**
  - スケッチバージョンと EEPROM_VER を即時表示
  - 出力例：`[VER] OpenCCVoice v1.73f (EEPROM_VER=4)`
  （v1.73f1 にて `v` → `V` に変更）

- **`Z` コマンド：ソフトウェアリセット**
  - ウォッチドッグタイマー（WDT, 15ms）による MCU 再起動
  - EEPROM は変更しない（設定保持のまま再起動）
  - `F` → `Z` で完全クリーン起動が可能

### Notes
- EEPROM レイアウトは **v1.73d/e と同一（`config.ver = 4`）**

--------------------------------------------------------------------

## v1.73e — Unified/Safe + Stability Fixes (2026-02-13)

⚠️ **廃止（v1.80h に統合）** — 歴史的記録として保持

### Added
- **送信直後ガード（postTxIgnore）を追加**（v1.80h で削除・⑤抑止に統合）
  - PTT OFF 直後に postTxIgnore=true をセット
  - BUSY が idleMin ms 連続 OFF になった時点で自動解除
  - 短発ID・周期ID の両方に適用

- **周期IDスケジューラの catch-up 対策**（v1.80h のスキップ仕様に置き換え）
  - nextPeriodicAt を while で未来へ追い付かせ、periodicDue は最大1回のみ

### Changed
- A0 BUSY 観測ロジックの安定化（抑止中も観測継続・AUTOカウントのみ停止）
- 送信直後の再発火防止強化（lastTriggerAt を PTT OFF 時に更新）

### Notes
- EEPROM レイアウトは **v1.73d と同一（`config.ver = 4`）**

--------------------------------------------------------------------

## v1.73d — Unified/Safe + Periodic Quiet Guard (2026-01-26)

⚠️ **廃止（v1.80h に統合）** — 歴史的記録として保持

### Added
- **周期IDの静寂ガードを導入（`k####` ms）**
  - 周期ID は BUSY が OFF になってから k ms 以上静寂が継続した場合のみ送出
  - BUSY中・静寂不足・抑止中は延期（periodicDue を保持）
  - 既定値：k2000（2秒）。k0 で静寂条件なし

### Changed
- **EEPROM レイアウトを更新（`config.ver = 4`）**
  - `periodQuietMs` フィールドを追加

--------------------------------------------------------------------

## v1.73c — Unified/Safe + Versioned EEPROM (2026-01-26)

⚠️ **廃止（v1.80h に統合）** — 歴史的記録として保持

### Added
- **EEPROM レイアウトバージョン管理（config.ver）を導入**
  - `config.magic` 不一致 → 完全初期化
  - `config.ver` 不一致 → 自動マイグレーション（新規項目の既定補完）

--------------------------------------------------------------------

## v1.73b — Unified/Safe + TM Busy Polarity Cmd (2026-01-22)

⚠️ **廃止（v1.80h に統合）** — 歴史的記録として保持

### Added
- `g0/g1` コマンド: TM BUSY 極性切替・EEPROM 保存
  - g0 = LOW=受信（アクティブLOW）
  - g1 = HIGH=受信（アクティブHIGH）

--------------------------------------------------------------------

## v1.73a — Unified/Safe + DFP Timeout Cmd (2026-01-22)

⚠️ **廃止（v1.80h に統合）** — 歴史的記録として保持

### Added
- `d####` コマンドで DFPlayer フェイルセーフ時間（ms）を可変化
- 既定タイムアウトを 20000ms（20秒）に設定

--------------------------------------------------------------------

## v1.73-unified-safe / v1.72 / v1.71 / v1.69 系 / v1.68 系 / v1.67 系 / v1.64 以前

⚠️ **廃止（v1.80h に統合）** — 歴史的記録として保持

主要マイルストーン：
- v1.73-unified-safe: 完全統合・安全性強化・機種依存排除版
- v1.72: Factory Reset（F）・EEPROM 永続化
- v1.71: HELP・STOP・q に LONG_TALK_MS 追加
- v1.69e: AUTO（D6/A0発生数比較）
- v1.68 系: A0 ヒステリシス・長話/連打抑止
- v1.67 系: 送信後抑止・D2 ミラー出力
- v1.64: PTT最低ONガード・BUSYヒステリシス再設計
- v1.50: 初の安定版（D6 BUSY判定）

--------------------------------------------------------------------

## 付録：ピンマップ（Arduino Nano / Ver.5 PCB / v1.80h 時点）

| Pin | 機能 | 備考 |
|-----|------|------|
| D2  | DFP BUSYミラー出力 | 再生中=HIGH（反転） |
| D3  | テストSW | INPUT_PULLUP |
| D4  | 受信LED(D6) | D6/AUTO時 |
| D5  | PTT出力 | HIGH=送信 |
| D6  | TM BUSY入力 | 極性切替可（g0/g1） |
| D7  | DFPlayer BUSY入力 | LOW=再生中 |
| D10 | Arduino RX | ← DFPlayer TX |
| D11 | Arduino TX | → DFPlayer RX |
| D12 | A0 LED | A0/AUTO時 |
| D13 | 抑止/AUTO LED | 抑止中・AUTO固定通知 |
| A0  | アナログ入力 | ヒステリシス＋保持 |

--------------------------------------------------------------------

## 付録：用語対応表（コード識別子 ↔ ドキュメント表現）

変数名・定数名はコード互換性のため変更せず、ドキュメントおよびコメントのみ読み替えを行っています。

| コード識別子 | ドキュメント表現 |
|---|---|
| `PIN_TM_BUSY` / `tmBusy*` | 受信BUSY信号（D6入力） |
| `TMBUSY_ACTIVE_HIGH` | 受信BUSY信号極性 |
| `busySupUntil` | ⑤抑止終了時刻 |
| `TX_SUP_MS` | ⑤抑止時間 |
| `PTT_PRE_MS` | ③ PTT先行無音時間（PRE） |
| `PTT_POST_MS` | ④ PTT後行無音時間（POST） |
| `BUSY_MIN_MS` | ① カーチャンク最小受信長 |
| `BUSY_MAX_MS` | ② カーチャンク最大受信長 |
| `PERIOD_QUIET_MS` | 定周期送出前の静寂待機時間 |
| `pttPreEndAt` | PRE終了予定時刻（絶対値） |
| `pttPostEndAt` | POST終了予定時刻（絶対値） |

### v1.73系から削除された識別子（v1.80h 以降は存在しない）

| 識別子 | 旧用途 |
|---|---|
| `postTxIgnore` | 送信直後ガードフラグ（⑤抑止に統合） |
| `postTxIdleStart` | 送信直後ガード解除用計測 |
| `IDLE_MIN_MS` / `idleMin` | 送信後BUSY安定待ち時間 |
| `BURST_WIN_MS` / `burstWinStart` | 連打検出窓 |
| `BURST_SUP_MS` / `burstSupUntil` | 連打抑止 |
| `BURST_TH` / `burstCount` | 連打閾値・カウンタ |
| `LONG_SUP_MS` / `longSupUntil` | 長話抑止（⑤抑止に統合） |
| `suppressOn` / `SUPPRESSORS_ENABLED` | 抑止全体ON/OFF |
| `txAfSupOn` / `TX_AFTER_SUPPRESS_ENABLED` | 送信後抑止ON/OFF |
| `stateTimer` | ステート遷移タイマー（絶対時刻比較に置き換え） |
| `periodicDue` | 周期ID送出待ちフラグ（スキップ仕様に変更） |
| `lastTriggerAt` | 最終ID送出時刻（不応期管理、不応期ロジック削除により廃止） |

--------------------------------------------------------------------
End of CHANGELOG
