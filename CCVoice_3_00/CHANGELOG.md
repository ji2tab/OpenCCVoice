# CHANGELOG — OpenCCVoice ID Guidance Controller

本プロジェクトは、Arduino Nano (ATmega328P, 5V) 上で動作する DMR無線機の ID送出支援コントローラです。

記法: Keep a Changelog 準拠 / 日付: JST

--------------------------------------------------------------------

## v3.00 — 統合刷新版 (2026-05-07)

v1.80i の堅牢設計と v2.24f の高機能（DS3231 RTC / AT24C32デュアルEEPROM /
イベントログ）を統合した、完全リファクタ版。

### Changed [破壊的変更]
- **抑止ロジックの一本化**
  - `longSupUntil` / `burstSupUntil` / `postTxIgnore` を**廃止**。
  - 抑止タイマーを `busySupUntil` 単一に統合（起点A/起点B）。
    - **起点A**: PTT POST終了時（受信中でない場合）
    - **起点B**: ②超え受信終了時（通常会話終了）
  - `isSuppressedNow()` は `busySupUntil` のみ参照する単純な実装に。
- **PTT制御の絶対時刻化**
  - `stateTimer` からの相対時間判定を**廃止**。
  - `pttPreEndAt` / `pttPostEndAt` の絶対時刻判定に統一。
  - `Serial.parseInt()` ブロッキング中の `millis()` 進行による
    PRE/POST スキップを防止。
- **定周期ID のシンプル化**
  - `periodicDue` フラグ管理から、シンプルなスキップ方式へ移行。
  - 受信中・抑止中・PTT中に到来した周期IDは**破棄**（延期しない）。
  - 連続送出を完全に防止。
- **デフォルト値の刷新**
  - `BUSY_MAX_MS`: 3900ms → **1500ms**（通常会話との境界明確化）
  - `TX_SUP_MS`: 3000ms → **10000ms**（抑止強化）

### Added
- **`j####` / `J####` コマンド**：PTT PRE/POST時間を可変化（EEPROM保存）
- **廃止コマンドの明示通知**
  - `s` / `t` / `i` / `H` 入力時に `[CMD] Obsolete command (廃止)` を出力
- **EEPROM_VER=7 へのマイグレーション**
  - `migrateConfigV6toV7()` を実装
  - v6 (v2.24f) からの自動移行に対応
  - 旧デフォルト値（busyMax=3900, txSupMs=3000）は新デフォルトに自動更新

### Removed [廃止コマンド]
- `s0/s1`：抑止全体ON/OFF（抑止は常時有効に）
- `t0/t1`：送信後抑止ON/OFF（POST後は常に抑止）
- `i####`：送信直後ガード閾値（抑止に統合）
- `H`：汎用プリセット
- 旧EEPROMフィールド: `suppressOn` / `txAfSupOn` / `idleMin` 

### Maintained [継続機能]
- DS3231 RTC正時アライン（v2.10〜継続）
- AT24C32 デュアルEEPROM保存（v2.20〜継続）
- イベントログ（PER/CAR/SUP/BOT記録、v2.21〜継続）
  - **BST（バースト抑止）**は v3.00 で発行されません（過去ログ表示用に保持）
- `T` / `u` / `V` / `Z` / `q` / `v` / `v0` / `F` コマンド（v2.10〜v2.24f継承）
- AUTO判定（D11/A0自動選択、v1.69〜継続）

### File Structure [構成変更]
- **単一ファイル化**: ヘッダ分離（`ccvoice_log.h` / `ccvoice_config.h`）を廃止
- **配布**: `CCVoice_3_0.ino` 1ファイルのみで完結
- 理由: 個人開発・中規模での同期コスト低減、Arduino IDE での扱いやすさ
- 詳細は NOTES_FOR_UPGRADE.md を参照

### Migration Notes [v2.24f → v3.00 移行]
- EEPROM CONFIG_VERSION = 6 → 7
- 既存設定は自動移行されます（`F` コマンド不要）
- 起動時ログ例:
  ```
  [EEPROM] Migrated from V6 (internal).
  [EEPROM] Settings Loaded.
  [START] OpenCCVoice v3.00
  ```

--------------------------------------------------------------------

## v2.24f — V/Z コマンド追加版 (2026-03-18)

### Added
- **`V` コマンド：バージョン確認**（v1.73f1 由来）
  - 出力例：`[VER] OpenCCVoice v2.24f (EEPROM_VER=6)`
- **`Z` コマンド：ソフトウェアリセット**（v1.73f1 由来）
  - WDT(15ms)による MCU 再起動。EEPROM変更なし。

### Notes
- 機能的な変更はなし。v2.24 の全機能を継承。
- EEPROM CONFIG_VERSION = **6（変更なし）**。

--------------------------------------------------------------------

## v2.24 — アーキテクチャ最適化・安全性向上版 (2026-03-12)

### Changed
- メモリアクセス最適化（`volatile` 削除）
- ヘッダファイル関数に `inline` 付与

### Notes
- 機能的な変更なし。v2.23 の全機能を継承。

--------------------------------------------------------------------

## v2.23 — TX/RX ピン修正・コンパイルエラー修正版 (2026-03-12)

### Fixed
- DFPlayer TX/RX ピン修正（ARD_RX=D12 / ARD_TX=D13）
- コンパイルエラー修正（ヘッダ分離による）

### 構成ファイル
- `CCVoice_2_23.ino` / `ccvoice_log.h` / `ccvoice_config.h`

--------------------------------------------------------------------

## v2.22 / v2.21 / v2.20 — 廃止
（詳細は v2.24f の CHANGELOG.md を参照）

--------------------------------------------------------------------

## v2.10 — DS3231 RTC 正時アライン対応版 (2026-03-11)

### Added
- DS3231 RTC モジュール対応
- 周期ID の「正時アライン」方式
- `T` / `u0` / `u1` コマンド追加
- EEPROM CONFIG_VERSION = 5

--------------------------------------------------------------------

## v2.01 — Ver.5 ピンマップ対応版 (2026-03-10)

### Changed
- Ver.5 基板ピンマップへ全面移行
- STOP復帰コマンドを `r` から `R` に変更

--------------------------------------------------------------------

## v1.80 系 (2026-04〜)

v1 系の独立ライン（Nano、非RTC、シンプル設計）。
v3.00 はこの設計思想を v2 系に統合したもの。

### v1.80i (2026-05-04)
- カーチャンク検出時の `busySupUntil` 二重セット修正
- `migrateOrInit()` の安全性向上
- デッドコード削除

### v1.80h (2026-05-03)
- 仕様変更: BUSY_MAX_MS デフォルト 3900→1500
- 抑止ロジック刷新（busySupUntil 一本化、起点A/B）
- `j####` / `J####` コマンド追加
- 連打抑止（BURST）削除
- `s` / `t` / `i` / `H` コマンド廃止

詳細は v1.80i の CHANGELOG.md を参照。

--------------------------------------------------------------------

## v1.73e〜v1.73f1 (2026-01〜02)

v1 系の段階的改良版（送信直後ガード、catch-up対策、`V`/`Z`コマンド追加）。

--------------------------------------------------------------------

## 付録：ピンマップ（Ver.5 基板 / v2.01以降・v3.00継承）

| Pin | 機能 | 備考 |
|-----|------|------|
| D2  | テストSW | INPUT_PULLUP（1〜3クリックでTRACK 1〜3） |
| D3  | DFP BUSYミラー出力 | 再生中=HIGH（反転） |
| D4  | ModBusy LED | BUSY表示 |
| D5  | PTT出力 | HIGH=送信 |
| D6  | 抑止 LED | 抑止表示 |
| D7  | A0検知 LED | A0 BUSY表示 |
| D10 | DFPlayer BUSY入力 | LOW=再生中（INPUT_PULLUP） |
| D11 | TM BUSY入力（Digital入力） | 極性切替可（g0/g1）、INPUT_PULLUP |
| D12 | Arduino RX ← DFPlayer TX | SoftwareSerial 9600bps |
| D13 | Arduino TX → DFPlayer RX | SoftwareSerial 9600bps |
| A0  | アナログ入力 | ヒステリシス＋保持適用 |
| A4  | I²C SDA | DS3231 + AT24C32（v2.10追加） |
| A5  | I²C SCL | DS3231 + AT24C32（v2.10追加） |

--------------------------------------------------------------------
End of CHANGELOG
