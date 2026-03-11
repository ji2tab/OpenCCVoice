# CHANGELOG.md - OpenCCVoice ID Guidance Controller

本プロジェクトは、Arduino Nano (ATmega328P, 5V) 上で動作する、DMR無線機の ID送出支援コントローラです。

記法: Keep a Changelog 準拠  
日付: JST（日本標準時）

--------------------------------------------------------------------

## v2.10 — DS3231 RTC 正時アライン対応版 (2026-03-11)

### Added
- **DS3231 RTC モジュール対応（I²C / A4=SDA, A5=SCL）**
  - 起動時に I²C で DS3231 の接続を自動確認。
  - 未接続・故障時は millis() 相対動作に自動フォールバック。
  - 起動ログに `[RTC] DS3231 found.` または `[RTC] DS3231 not found -> millis() fallback.` を表示。

- **周期ID の「正時アライン」方式を導入**
  - `p##` が **60の約数**（1/2/3/4/5/6/10/12/15/20/30/60分）かつ DS3231 接続時 → **毎正時基準で等間隔**にアライン。
    - 例：`p15` → 毎時 :00/:15/:30/:45 に送出
    - 例：`p10` → 毎時 :00/:10/:20/:30/:40/:50 に送出
    - 例：`p30` → 毎時 :00/:30 に送出
  - 60の約数でない値（例：`p7`）または DS3231 未接続時 → 従来の millis() 相対動作（v2.01互換）。
  - **1分ごとに DS3231 から時刻を再取得**し、millis() のドリフトを自動補正。

- **`T` コマンド（RTC 時刻設定）を追加**
  - 書式：`TYYYYMMDDHHmmss`（14桁）
  - 例：`T20260311143000` → 2026/03/11 14:30:00 に設定
  - 設定後、次回発火時刻を即座に再計算。

- **`u0/u1` コマンド（RTCアライン ON/OFF）を追加**
  - `u1`：RTCアライン有効（既定）
  - `u0`：RTCアライン無効（millis() 相対に切替）
  - EEPROM に保存。

- **`q`（設定一覧）に RTC 現在時刻とアライン状態を追加表示**
  ```
  [RTC] 2026/03/11 14:23:45  Align=ON(RTC)
  [CFG] EEPROM_VER=5 ... RTC_ALIGN=ON
  ```

- **EEPROM CONFIG_VERSION を 5 に更新**
  - `rtcAlignOn` フィールドを追加（末尾 `ver` の直前）。
  - v2.01（ver=4）からの更新時、`rtcAlignOn=1`（ON）で自動補完。
  - 既存設定（MIN/MAX/PER/TXSUP 等）はすべて引き継ぎ。

### Notes
- ピンマップは **v2.01（Ver.5 基板）から変更なし**。
- AT24C32（モジュール上の外部 EEPROM）は本バージョンでは未使用。
- DS3231 との接続は基板上の **J5 コネクタ**（SDA/SCL/VCC/GND）を使用。
- v2.01 の全機能（postTxIgnore / catch-up / A0観測継続 / 静寂ガード）を完全継承。

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
- **BusySrc列挙値の名称変更**（`BUSY_SRC_D6` → `BUSY_SRC_DIGITAL`）
- **STOP中のLED消灯処理を強化**（`PIN_DFP_OUT`（D3）も LOW に落とす）

### Notes
- EEPROM レイアウトは **v1.73d/e と同一（`config.ver = 4`）**。
- v1.73e で導入された全機能を完全継承。

--------------------------------------------------------------------

## v1.73e — Unified/Safe + Stability Fixes (2026-02-13)

### Added
- **送信直後ガード（postTxIgnore）を追加**
- **周期IDスケジューラの catch-up 対策**

### Changed
- **A0 BUSY 観測ロジックの安定化**（抑止中も観測継続）
- **送信直後の再発火防止を強化**

### Notes
- EEPROM レイアウトは **v1.73d と同一（`config.ver = 4`）**。

--------------------------------------------------------------------

## v1.73d — Unified/Safe + Periodic Quiet Guard (2026-01-26)

### Added
- **周期IDの"静寂ガード"を導入（`k####` ms）**
- **EEPROM レイアウトを更新（`config.ver = 4`）**

--------------------------------------------------------------------

## v1.73c — Unified/Safe + Versioned EEPROM (2026-01-26)
### Added
- **EEPROM レイアウトバージョン管理（config.ver）を導入**

--------------------------------------------------------------------

## v1.73b — Unified/Safe + TM Busy Polarity Cmd (2026-01-22)
### Added
- `g0/g1` コマンド（TM BUSY 極性切替）

--------------------------------------------------------------------

## v1.73a — Unified/Safe + DFP Timeout Cmd (2026-01-22)
### Added
- `d####` コマンドで DFPlayer フェイルセーフ（ms）を可変化

--------------------------------------------------------------------

## v1.73-unified-safe (2026-01-22)
### Added
- DFPlayer フェイルセーフ（8秒固定）
- A0/Digital AUTO 判定
- STOP/RESUME（x / R）、HELP（h）

--------------------------------------------------------------------

## v1.72-final-EEPROM+F (2026-01-20)
### Added
- Factory Reset（F）、EEPROM 永続化

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

## 付録：ピンマップ（Arduino Nano / v2.01以降 Ver.5）

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
 A4   | I²C SDA                    | DS3231 接続（v2.10追加）
 A5   | I²C SCL                    | DS3231 接続（v2.10追加）

--------------------------------------------------------------------
End of CHANGELOG
