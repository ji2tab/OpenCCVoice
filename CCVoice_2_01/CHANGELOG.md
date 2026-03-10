# CHANGELOG.md - OpenCCVoice ID Guidance Controller

本プロジェクトは、Arduino Nano (ATmega328P, 5V) 上で動作する、DMR無線機の ID送出支援コントローラです。

記法: Keep a Changelog 準拠  
日付: JST（日本標準時）

--------------------------------------------------------------------

## v2.01 — Ver.5 ピンマップ対応 / 仕様整理版 (2026-03-10)

### Changed
- **Ver.5 基板ピンマップへ全面移行**
  - 旧配線（v1.73e）から下記の通りピンアサインを変更：

  | 信号 | v1.73e | v2.01 (Ver.5) |
  |------|--------|---------------|
  | DF Player BUSYミラー出力 | D2 | D3 |
  | テストSW | D3 | D2 |
  | TM BUSY入力（Digital入力） | D6 | D11 |
  | DFPlayer BUSY入力 | D7 | D10 |
  | 抑止 LED | D13 | D6 |
  | A0検知 LED | D12 | D7 |
  | Arduino RX ← DFPlayer TX | D10 | D13 |
  | Arduino TX → DFPlayer RX | D11 | D12 |

- **STOP復帰コマンドを `r`（小文字）から `R`（大文字）に変更**
  - `r####`（送信後抑止時間設定）との衝突を回避
  - `handleSerialCmd()` 内で `R` を受け取り `continue`、
    STOP中は `loop()` 先頭ブロックで処理する設計に分離

- **BusySrc列挙値の名称変更**
  - `BUSY_SRC_D6` → `BUSY_SRC_DIGITAL`
  - EEPROMへは数値（0/1/2）で保存されるため **互換性に影響なし**
  - `printSummary()` 表示も `"D6"` → `"DIGITAL"` に統一

- **STOP中のLED消灯処理を強化**
  - `PIN_DFP_OUT`（D3）も `LOW` に落とすよう追加

### Notes
- EEPROM レイアウトは **v1.73d/e と同一（`config.ver = 4`）**。
  既存機の EEPROM を破壊せず、そのままアップデート可能。
- v1.73e で導入された全機能（postTxIgnore / catch-up / A0観測継続）を完全継承。
- 操作コマンド体系に変更なし（`R` への改名を除く）。

--------------------------------------------------------------------

## v1.73e — Unified/Safe + Stability Fixes (2026-02-13)

### Added
- **送信直後ガード（postTxIgnore）を追加**
  - 音声ID送出直後の A0 残留BUSY、スケルチテール、瞬間的なBUSY揺れによる
    **短発ID（001）や周期IDの誤発火を防止**。
  - PTT OFF 直後は一時的に「抑止扱い」とし、以下を抑制：
    - 短発ID（001）
    - 周期ID（002/003）
  - BUSY が **連続して `idleMin` ms 以上 OFF** になった時点で自動解除。

- **周期IDスケジューラの catch-up 対策**
  - BUSYや抑止により周期IDが長時間延期された場合でも、
    **解除後に周期IDが連続送信されない**よう制御を改善。
  - `nextPeriodicAt` を `while` で未来へ追い付かせ、
    **`periodicDue` は最大1回分のみ**立つ設計に変更。

### Changed
- **A0 BUSY 観測ロジックの安定化**
  - 抑止中であっても A0 の BUSY 状態（`a0Busy`）は継続して観測。
  - 抑止中は AUTO 判定用イベントカウント（`a0_event_count`）のみ増加させない。
- **送信直後の再発火防止を強化**
  - PTT OFF 時に `lastTriggerAt` を更新し、
    不応期（REFRAC_MS）の起点を送信直後に再設定。

### Notes
- EEPROM レイアウトは **v1.73d と同一（`config.ver = 4`）**。
- v1.73d で導入された Quiet Guard（`k####`）と完全互換。

--------------------------------------------------------------------

## v1.73d — Unified/Safe + Periodic Quiet Guard (2026-01-26)

### Added
- **周期IDの"静寂ガード"を導入（`k####` ms）**
  - 周期ID（002/003）は **BUSYがOFFになってから `k` ms 以上静寂が継続**した場合のみ送出
  - **BUSY中／静寂不足／抑止中** は **延期（`periodicDue` を保持）**
  - 既定値：`k2000`（2秒）
- **要約（`q`）に `QUIET(ms)=...` を表示**

### Changed
- **EEPROM レイアウトを更新（`config.ver = 4`）**
  - v1.73c（`ver=3`）以前からの更新時に `periodQuietMs` を **既定 2000ms** で補完

--------------------------------------------------------------------

## v1.73c — Unified/Safe + Versioned EEPROM (2026-01-26)
### Added
- **EEPROM レイアウトバージョン管理（config.ver）を導入**
- **初回読み込み時の自動補完ロジックを追加**

--------------------------------------------------------------------

## v1.73b — Unified/Safe + TM Busy Polarity Cmd (2026-01-22)
### Added
- `g0/g1` コマンド（TM BUSY 極性切替）を追加・EEPROM保存対応

--------------------------------------------------------------------

## v1.73a — Unified/Safe + DFP Timeout Cmd (2026-01-22)
### Added
- `d####` コマンドで DFPlayer フェイルセーフ（ms）を可変化。`d0` で無効化。

--------------------------------------------------------------------

## v1.73-unified-safe (2026-01-22)
### Added
- DFPlayer フェイルセーフ（8秒固定）
- A0/Digital AUTO 判定（固定型）
- STOP/RESUME（x / R）、HELP（h）

--------------------------------------------------------------------

## v1.72-final-EEPROM+F (2026-01-20)
### Added
- Factory Reset（F）、EEPROM 永続化、AUTO固定後に保存

--------------------------------------------------------------------

## v1.71-stable-final (2026-01-19)
### Added
- HELP、STOP、t0/t1
### Fixed
- DFPlayer BUSY 戻り判定に 40ms 安定時間

--------------------------------------------------------------------

## Pre-v1.71 / Prototype — 2025-08〜
- v1.50：初の安定版（Digital BUSY判定、PTT PRE/POST）
- CCVoice 初期プロトタイプ

--------------------------------------------------------------------

## 付録：ピンマップ（Arduino Nano / v2.01 Ver.5）

 Pin  | 機能                        | 備考
------|-----------------------------|------
 D2   | テストSW                    | INPUT_PULLUP（1〜3クリックでTRACK 1〜3）
 D3   | DFP BUSYミラー出力          | 再生中=HIGH（反転）
 D4   | ModBusy LED                 | BUSY表示
 D5   | PTT出力                     | HIGH=送信
 D6   | 抑止 LED                    | 抑止/バースト/POST-TX表示
 D7   | A0検知 LED                  | A0 BUSY表示
 D10  | DFPlayer BUSY入力           | LOW=再生中（INPUT_PULLUP）
 D11  | TM BUSY入力（Digital入力）  | 極性切替可（g0/g1）、INPUT_PULLUP
 D12  | Arduino TX → DFPlayer RX   | SoftwareSerial 9600bps
 D13  | Arduino RX ← DFPlayer TX   | SoftwareSerial 9600bps
 A0   | アナログ入力                | ヒステリシス＋保持適用

--------------------------------------------------------------------
End of CHANGELOG
