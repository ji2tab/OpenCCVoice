# OpenCCVoice Guidance Controller

**バージョン: v2.24f**
対象基板: **Ver.5**
対象MCU: Arduino Nano (ATmega328P, 5V)

---

## 概要

OpenCCVoice は DMR 無線機のレピータ向け ID 送出支援コントローラです。
DFPlayer Mini による音声ファイル再生、DS3231 RTC による正時アライン、
AT24C32 による設定の二重保存・イベントログ記録に対応しています。

---

## ファイル構成

```
CCVoice_2_24f/
  ├── CCVoice_2_24f.ino    メインスケッチ（必須）
  ├── ccvoice_log.h       イベントログ関連（必須）
  └── ccvoice_config.h    設定マイグレーション関連（必須）
```

> **3ファイルを同一フォルダに置いて** Arduino IDE で `CCVoice_2_24f.ino` を開いてください。

---

## Ver.5 ピンマップ

| Pin | 機能 | 備考 |
|-----|------|------|
| D2  | テストSW | 1〜3クリックで TRACK 1〜3 手動送出 |
| D3  | DFP BUSYミラー出力 | 再生中=HIGH（反転） |
| D4  | ModBusy LED | BUSY 表示 |
| D5  | PTT 出力 | HIGH=送信 |
| D6  | 抑止 LED | 抑止/バースト/POST-TX 表示 |
| D7  | A0検知 LED | A0 BUSY 表示 |
| D10 | DFPlayer BUSY 入力 | LOW=再生中（INPUT_PULLUP） |
| D11 | TM BUSY 入力 | 極性切替可（g0/g1）、INPUT_PULLUP |
| D12 | Arduino RX ← DFPlayer TX | SoftwareSerial 9600bps |
| D13 | Arduino TX → DFPlayer RX | SoftwareSerial 9600bps |
| A0  | アナログ入力 | ヒステリシス＋保持適用 |
| A4  | I²C SDA | DS3231 + AT24C32 |
| A5  | I²C SCL | DS3231 + AT24C32 |

---

## コマンド一覧（シリアル 115200bps 8N1 / 改行なし or LF）

| コマンド | 説明 | 既定値 |
|---------|------|--------|
| `m0/m1/m2` | BUSY検知ソース: Digital/A0/AUTO | AUTO |
| `n####` | BUSY最小時間 (ms) | 500 |
| `b####` | BUSY最大時間（長話抑止）(ms) | 3900 |
| `i####` | IDLE最小時間 (ms) | 200 |
| `s0/s1` | 抑止機能 OFF/ON | ON |
| `t0/t1` | 送信後抑止 OFF/ON | ON |
| `r####` | 送信後抑止時間 (ms) | 3000 |
| `p##` | 定周期送出間隔（分、0=停止） | 30 |
| `k####` | 周期ID静寂ガード (ms) | 2000 |
| `L###` | A0低閾値 | 300 |
| `G###` | A0高閾値 | 700 |
| `a####` | A0保持時間 (ms) | 800 |
| `w##` | AUTO判定ウィンドウ（分） | 30 |
| `d####` | DFPlayer タイムアウト (ms、0=無効) | 20000 |
| `g0/g1` | TM BUSY 極性: LOW/HIGH | HIGH |
| `u0/u1` | RTCアライン OFF/ON | ON |
| `TYYYYMMDDHHmmss` | RTC時刻設定（例: T20260311143000） | — |
| `v` | イベントログ表示（直近20件） | — |
| `v0` | イベントログ消去 | — |
| `V` | バージョン表示（スケッチver + EEPROM_VER） | — |
| `Z` | ソフトウェアリセット（WDT、EEPROM変更なし） | — |
| `q` | 現在設定・時刻の一覧表示 | — |
| `h` | ヘルプ表示 | — |
| `H` | 詳細ヘルプ表示 | — |
| `x` | STOP（送出停止） | — |
| `R` | STOP解除（送出再開） | — |
| `F` | Factory Reset（EEPROM初期化） | — |
| `0〜3` | テスト送出（TRACK 0〜3） | — |

---

## 起動時シリアル出力例

```
[START] OpenCCVoice v2.24f (Event Log, Dual EEPROM, RTC-Aligned Periodic, Ver.5 pinmap)
[RTC] DS3231 found.
[EEPROM] AT24C32 found. Using external EEPROM.
[CFG] EEPROM_VER=6 SRC=AUTO MIN=500 MAX=3900 PER(min)=30 RTC_ALIGN=ON
[RTC] 2026/03/12 09:00:00  Align=ON(RTC)
```

---

## 音声ファイル

詳細は `AUDIO_FILES_GUIDE.md` を参照してください。

| トラック | ファイル名 | 用途 |
|---------|-----------|------|
| 1 | `001.mp3` | カーチャンク ID |
| 2 | `002.mp3` | 定周期 ID（奇数回） |
| 3 | `003.mp3` | 定周期 ID（偶数回） |

> **001〜003 の3ファイルを必ず用意してください。**
> 詳細は `AUDIO_FILES_GUIDE.md` を参照。

---

## DS3231 接続

詳細は `DS3231_CONNECTION_GUIDE.md` を参照してください。

- J5 コネクタ（SDA/SCL/VCC/GND）に接続
- I²C アドレス: DS3231=0x68、AT24C32=0x57
- 未接続時は millis() 相対動作にフォールバック

> DS3231 のコイン電池（CR2032）が切れると時刻が `2000/01/01` 付近にリセットされます。
> `q` コマンドで確認し、`T` コマンドで再設定してください。

---

## 関連ドキュメント

| ファイル | 内容 |
|---------|------|
| `CHANGELOG.md` | バージョン変更履歴 |
| `NOTES_FOR_UPGRADE.md` | アップグレード手順 |
| `SPECIFICATION.md` | 詳細仕様書 |
| `DS3231_CONNECTION_GUIDE.md` | DS3231 接続ガイド |
| `AUDIO_FILES_GUIDE.md` | 音声ファイルガイド |
| `README_PROJECT.md` | プロジェクト概要・ライセンス |
| `Schematic_Ver5.pdf` | 回路図（Ver.5） |
| `PCB_Layout_Ver5.pdf` | 部品配置図（Ver.5） |

---

**Project Contributors:** JA2CCV, JA9HYM, JA2DML, JG1XWV, JI2TAB
