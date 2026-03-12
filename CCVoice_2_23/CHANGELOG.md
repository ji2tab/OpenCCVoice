# CHANGELOG — OpenCCVoice ID Guidance Controller

本プロジェクトは、Arduino Nano (ATmega328P, 5V) 上で動作する DMR無線機の ID送出支援コントローラです。

記法: Keep a Changelog 準拠 / 日付: JST

--------------------------------------------------------------------

## v2.23 — TX/RX ピン修正・コンパイルエラー修正版 (2026-03-12)

### Fixed
- **DFPlayer TX/RX ピンアサインのバグを修正**（v2.22 より）

  | | ARD_RX（← DFPlayer TX） | ARD_TX（→ DFPlayer RX） |
  |---|---|---|
  | **誤（v2.21以前）** | D13 | D12 |
  | **正（v2.23）** | D12 | D13 |

- **Arduino IDE でのコンパイルエラーを修正**
  - Arduino IDE は `.ino` ファイルのコンパイル前に全関数のプロトタイプ宣言を
    自動生成する。この際 `struct LogHeader` / `struct MyConfig` より前に
    プロトタイプが挿入されてしまい「未宣言」エラーが発生していた。
  - ログ関連コードを `ccvoice_log.h`、設定マイグレーションコードを
    `ccvoice_config.h` に分離することで根本解決。

### 構成ファイル（3ファイルを同一フォルダに置くこと）
```
CCVoice_2_23.ino    メインスケッチ
ccvoice_log.h       イベントログ関連
ccvoice_config.h    設定マイグレーション関連
```

### Notes
- 機能的な変更はありません。v2.21 の全機能を継承。
- v2.22 は廃止（GitHub からも削除）。
- EEPROM CONFIG_VERSION = **6（変更なし）**。`F` コマンド不要。

--------------------------------------------------------------------

## v2.21 — AT24C32 イベントログ記録対応版 (2026-03-11)

### Added
- **AT24C32 へのイベントログ記録機能を追加**
  - ログ領域: AT24C32 アドレス 0x0048〜（リングバッファ、最大 448件）
  - 448件超過後は古いものから上書き（9バイト/件）

- **記録イベント一覧**

  | 種別 | 表示 | 記録タイミング | 付加データ |
  |------|------|----------------|-----------|
  | 起動 | BOT | 起動時 | EEPROM ver |
  | 周期ID送出 | PER | Track2/3送出時 | Track番号 |
  | カーチャンク | CAR | ID送出トリガー時 | BUSY時間(ms) |
  | 長話抑止 | SUP | 長話抑止発生時 | BUSY時間(ms) |
  | バースト抑止 | BST | バースト抑止発生時 | バースト回数 |

- **`v` コマンド追加**: 直近20件をシリアルモニタに表示
- **`v0` コマンド追加**: ログ全件消去

### Notes
- AT24C32 未接続時はログ記録・表示をスキップ（動作に影響なし）
- EEPROM CONFIG_VERSION = **6（変更なし）**

--------------------------------------------------------------------

## v2.20 — AT24C32 外部EEPROM 二重保存対応版 (2026-03-11)

### Added
- **AT24C32（外部EEPROM / I²C 0x57）対応**
  - 設定変更時に AT24C32 と内蔵EEPROM の両方へ同時保存
  - 起動時は AT24C32 を優先して読み込み（可搬性設計）
  - AT24C32 未接続時は内蔵EEPROM にフォールバック
  - DS3231 時刻（秒換算）をタイムスタンプとして保存し新旧を自動判定
- **EEPROM CONFIG_VERSION を 6 に更新**（`saveTimestamp` フィールド追加）

### Notes
- v2.10（ver=5）からの更新時、既存設定を自動継承。`F` コマンド不要。

--------------------------------------------------------------------

## v2.10 — DS3231 RTC 正時アライン対応版 (2026-03-11)

### Added
- **DS3231 RTC モジュール対応**（I²C / A4=SDA, A5=SCL）
- **周期ID の「正時アライン」方式を導入**
  - `p##` が60の約数かつ DS3231 接続時 → 毎正時基準で等間隔送出
  - 例：`p15` → 毎時 :00/:15/:30/:45 に送出
  - 60の約数でない値または DS3231 未接続時 → millis() 相対動作
- **`T` コマンド追加**: RTC 時刻設定（例: `T20260311143000`）
- **`u0/u1` コマンド追加**: RTCアライン OFF/ON
- **EEPROM CONFIG_VERSION を 5 に更新**（`rtcAlignOn` フィールド追加）

--------------------------------------------------------------------

## v2.01 — Ver.5 ピンマップ対応版 (2026-03-10)

### Changed
- **Ver.5 基板ピンマップへ全面移行**（v1.73e からのピン変更）

  | 信号 | v1.73e | v2.01〜 (Ver.5) |
  |------|--------|----------------|
  | DFPlayer BUSYミラー出力 | D2 | D3 |
  | テストSW | D3 | D2 |
  | TM BUSY入力 | D6 | D11 |
  | DFPlayer BUSY入力 | D7 | D10 |
  | 抑止 LED | D13 | D6 |
  | A0検知 LED | D12 | D7 |
  | Arduino RX ← DFPlayer TX | D10 | D12 |
  | Arduino TX → DFPlayer RX | D11 | D13 |

- **STOP復帰コマンドを `r` から `R` に変更**

### Notes
- EEPROM レイアウトは v1.73d/e と同一（`config.ver = 4`）

--------------------------------------------------------------------

## v1.73e — Unified/Safe + Stability Fixes (2026-02-13)
- 送信直後ガード（postTxIgnore）追加
- A0 BUSY 観測ロジックの安定化
- EEPROM レイアウト: config.ver = 4（変更なし）

## v1.73d — Periodic Quiet Guard (2026-01-26)
- 周期IDの「静寂ガード」導入（`k####` ms）
- EEPROM config.ver = 4 に更新

## v1.73c — Versioned EEPROM (2026-01-26)
- EEPROM レイアウトバージョン管理導入

## v1.73b (2026-01-22)
- `g0/g1` コマンド（TM BUSY 極性切替）追加

## v1.73a (2026-01-22)
- `d####` コマンド（DFPlayer フェイルセーフ ms）追加

## v1.73-unified-safe (2026-01-22)
- DFPlayer フェイルセーフ、A0/Digital AUTO判定、STOP/RESUME、HELP

## v1.72-final-EEPROM+F (2026-01-20)
- Factory Reset（F）、EEPROM 永続化

## v1.71-stable-final (2026-01-19)
- HELP、STOP、t0/t1 追加

## Pre-v1.71 / Prototype — 2025-08〜
- v1.50：初の安定版（Digital BUSY判定、PTT PRE/POST）

--------------------------------------------------------------------

## 付録：ピンマップ（Ver.5 基板 / v2.01以降）

| Pin | 機能 | 備考 |
|-----|------|------|
| D2  | テストSW | INPUT_PULLUP（1〜3クリックでTRACK 1〜3） |
| D3  | DFP BUSYミラー出力 | 再生中=HIGH（反転） |
| D4  | ModBusy LED | BUSY表示 |
| D5  | PTT出力 | HIGH=送信 |
| D6  | 抑止 LED | 抑止/バースト/POST-TX表示 |
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
