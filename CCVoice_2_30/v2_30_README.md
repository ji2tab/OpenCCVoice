# OpenCCVoice Guidance Controller

**バージョン: v2.30（v1.80i コア搭載 + v2.24f RTC・ログ統合版）**
対象基板: **Ver.5**
対象MCU: Arduino Nano (ATmega328P, 5V)

---

## 概要

OpenCCVoice v2.30 は DMR 無線機のレピータ向け ID 送出支援コントローラです。
v1.80i の革新的なPTT制御・抑止ロジックをコア搭載しつつ、
v2.24f の RTC アライン、イベントログ、AT24C32 外部EEPROM機能を統合しています。

DFPlayer Mini による音声ファイル再生、DS3231 RTC による正時アライン、
AT24C32 による設定の二重保存・イベントログ記録に対応しています。

---

## ファイル構成

```
OpenCCVoice_v2_30/
  ├── OpenCCVoice_v2_30.ino    メインスケッチ（必須）
  ├── ccvoice_log.h           イベントログ関連（必須）
  └── ccvoice_config.h        設定マイグレーション関連（必須）
```

> **3ファイルを同一フォルダに置いて** Arduino IDE で `OpenCCVoice_v2_30.ino` を開いてください。

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
| `x` | STOP（送出停止） | — |
| `R` | STOP解除（送出再開） | — |
| `F` | Factory Reset（EEPROM初期化） | — |
| `l0/l1/l2/l3/l4` | ログレベル(OFF/ERR/MIN/FULL/DBG) | l2 |

### v2.30 で削除されたコマンド（v2.24f からの変更）

以下のコマンドは v2.30 では**使用できません**（内部固定化）：

| 削除コマンド | 旧機能 | v2.30での対応 |
|------------|--------|-------------|
| `j####` | PTT先行時間(PRE) | 内部固定1000ms |
| `J####` | PTT後行時間(POST) | 内部固定1000ms |
| `i####` | 送信直後ガード | 内部固定200ms |
| `s0/s1` | 抑止機能 | 常時有効（変更不可） |
| `t0/t1` | 送信後抑止 | 常時有効（`r####` で時間設定） |
| `h` | ヘルプ表示 | 削除（`q` / `V` で代替） |
| `0-3` テストコマンド | テスト再生 | v2.30 では廃止 |

---

## 起動時シリアル出力例

```
[START] OpenCCVoice v2.30 (v1.80i Core + v2.24f Extended, Ver.5 pinmap)
[RTC] DS3231 found.
[EEPROM] AT24C32 found. Using external EEPROM.
[EEPROM] Migration done. (ver=6 → ver=7)
[CFG] EEPROM_VER=7 SRC=AUTO MIN=500 MAX=3900 PER(min)=30 RTC_ALIGN=ON
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

## v2.24f → v2.30 アップグレード

詳細は `NOTES_FOR_UPGRADE.md` を参照してください。

### クイック手順

```
1. OpenCCVoice_v2_30.ino / ccvoice_config.h / ccvoice_log.h を用意
2. Arduino IDE で開く
3. コンパイル・書き込み
4. 起動ログで "Migration done." を確認
5. V コマンドで EEPROM_VER=7 を確認
6. q コマンドで設定が引き継がれていることを確認
```

### EEPROM マイグレーション（自動実行）

- v2.24f (ver=6) からの自動マイグレーション対応
- v1.80i (ver=5) からの自動マイグレーション対応
- 削除フィールド（idleMin、j/J パラメータ等）は自動補完

---

## 関連ドキュメント

| ファイル | 内容 |
|---------|------|
| `CHANGELOG.md` | バージョン変更履歴（v2.30 の革新詳細） |
| `NOTES_FOR_UPGRADE.md` | v2.24f → v2.30 アップグレード手順 |
| `SPECIFICATION.md` | 詳細仕様書（技術者向け） |
| `DS3231_CONNECTION_GUIDE.md` | DS3231 接続・運用ガイド |
| `AUDIO_FILES_GUIDE.md` | 音声ファイルガイド |
| `README_PROJECT.md` | プロジェクト概要・ライセンス |
| `Schematic_Ver5.pdf` | 回路図（Ver.5） |
| `PCB_Layout_Ver5.pdf` | 部品配置図（Ver.5） |

---

## v2.30 の主要改善点

### ① 絶対時刻比較（v1.80i 由来）
```c
// v2.30: 絶対時刻比較
if ((long)(now - pttPreEndAt) >= 0) { /* 状態遷移 */ }

// 効果：Serial.read() ブロッキング時のずれを根本解決
```

### ② 抑止起点統一（v1.80i 由来）
```c
// v2.30: 単一タイマーで統一管理
busySupUntil = now + TX_SUP_MS;

// 効果：複雑なロジック削除、安定性向上
```

### ③ ブロッキング対策（v1.80i 由来）
```c
Serial.setTimeout(50);  // setup() で一度だけ設定

// 効果：parseInt() の最大ブロッキングを50msに制限
```

---

## トラブルシューティング

### 「j/J コマンドが効かない」
→ **正常です。** v2.30 では j/J コマンドは廃止されました。
PTT PRE/POST は内部固定1000ms に統一されています。

### 「i コマンドが効かない」
→ **正常です。** v2.30 では i コマンドは廃止されました。
IDLE_MIN_MS は内部固定200ms に統一されています。

### 「h コマンドが効かない」
→ **正常です。** v2.30 では h コマンドは廃止されました。
`q` で設定表示、`V` でバージョン表示をしてください。

### 「EEPROM Version mismatch と出た」
→ **正常です。** 自動マイグレーションが実行されています。
起動ログで「Migration done.」が続いていることを確認してください。

---

**Project Contributors:** JA2CCV, JA9HYM, JA2DML, JG1XWV, JI2TAB

バージョン: v2.30 (v1.80i Core + v2.24f Extended)  
最終更新: 2026-04-28
