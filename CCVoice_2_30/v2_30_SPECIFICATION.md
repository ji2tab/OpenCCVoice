**===== 仕様書（技術者向け） OpenCCVoice Guidance Controller v2.30 =====**

**バージョン: v2.30（v1.80i コア搭載 + v2.24f RTC・ログ統合版）**
**最終更新: 2026-04-28**

---

## 1. 基本情報

- 名称      : OpenCCVoice Guidance Controller
- バージョン : v2.30 — v1.80i コア搭載版 + v2.24f RTC・ログ統合
- MCU       : Arduino Nano（ATmega328P, 5V）
- 対象基板  : Ver.5
- 依存      : Arduino.h / SoftwareSerial.h / EEPROM.h / Wire.h / avr/wdt.h
- DFPlayer  : BUSY監視（D10=LOW 再生中）、9600bps 制御（SoftwareSerial D12/D13）
- DS3231    : I²C（A4=SDA, A5=SCL）、レジスタ直接アクセス（外部ライブラリ不要）
- EEPROM    : **CONFIG_VERSION=7（v2.24f の ver=6 から自動マイグレーション対応）**
             **v1.80i (ver=5) からの自動マイグレーション対応**
- AT24C32   : 外部EEPROM（I²C 0x57）/ 内蔵EEPROMと二重保存

---

## 2. ピンアサイン（Ver.5）

 Pin  | 定数名              | 方向   | 信号                          | 備考
------|---------------------|--------|-------------------------------|----------------------------
 D2   | PIN_TEST_SW         | 入力   | テストSW                      | INPUT_PULLUP、LOW=押下
 D3   | PIN_DFP_OUT         | 出力   | DFPlayer BUSYミラー           | 再生中=HIGH（反転出力）
 D4   | PIN_BUSY_LED        | 出力   | ModBusy LED                   | HIGH=点灯
 D5   | PIN_PTT             | 出力   | PTT                           | HIGH=送信
 D6   | PIN_SUP_LED         | 出力   | 抑止 LED                      | HIGH=点灯（抑止中）
 D7   | PIN_A0_LED          | 出力   | A0検知 LED                    | HIGH=点灯（a0Busy時）
 D10  | PIN_DFP_BSY         | 入力   | DFPlayer BUSY入力             | INPUT_PULLUP、LOW=再生中
 D11  | PIN_TM_BUSY         | 入力   | TM BUSY入力（Digital入力）    | INPUT_PULLUP、極性g0/g1
 D13  | ARD_TX_TO_DFP       | 出力   | Arduino TX → DFPlayer RX     | SoftwareSerial 9600bps
 D12  | ARD_RX_FROM_DFP     | 入力   | Arduino RX ← DFPlayer TX     | SoftwareSerial 9600bps
 A0   | A0_PIN              | 入力   | アナログBUSY                  | 0〜1023、ヒステリシス＋保持
 A4   | —（Wire固定）        | I²C    | DS3231 SDA                    | Wire.h使用
 A5   | —（Wire固定）        | I²C    | DS3231 SCL                    | Wire.h使用

---

## 3. v2.30 での内部固定定数（変更点）

v2.30 では以下の値が**ユーザーコマンドで変更できなくなり、内部固定化**されました：

```c
#define PTT_PRE_MS        1000   // 固定（v2.24f: j#### で可変）
#define PTT_POST_MS       1000   // 固定（v2.24f: J#### で可変）
#define IDLE_MIN_MS       200    // 固定（v2.24f: i#### で可変）
#define DEBOUNCE_MS       5      // 内部タイマー（変更なし）
#define REFRAC_MS         3000   // カーチャンク間隔（変更なし）
#define LONG_SUP_MS       10000  // 長話抑止時間（変更なし）
#define BURST_SUP_MS      10000  // バースト抑止時間（変更なし）
```

---

## 4. v2.30 での機能仕様（Functional）

### (1) 短発受信の自動ID送出

v1.80i の改善ロジックをコア搭載。

- **PTT制御：絶対時刻比較へ変更**
  ```c
  pttPreEndAt = now + PTT_PRE_MS;   // 固定1000ms
  if ((long)(now - pttPreEndAt) >= 0) { /* 状態遷移 */ }
  ```
  - Serial.read() ブロッキングによる `now` のずれに耐性あり

- **受信判定：v2.24f と同等**
  - TM BUSY（D11）または A0 の受信継続時間 dur が
      BUSY_MIN_MS ≤ dur < BUSY_MAX_MS のとき、Track 1（001）を送出。

- **送出タイミング：v2.30 で確実化**
  - PRE フェーズ：1000ms
  - 再生フェーズ：DFPlayer 出力に従う（~2s〜5s 想定）
  - POST フェーズ：1000ms
  - 抑止開始：POST終了 + BUSY OFF時点

### (2) 抑止（v1.80i 改善ロジック搭載）

v1.80i で確立された「単一タイマー統一管理」方式：

- **抑止起点の統一**
  - 起点A：POST終了時 + BUSY OFF → `busySupUntil = now + TX_SUP_MS`
  - 起点B：通常会話終了時（dur ≥ BUSY_MAX_MS など）→ 同上
  - v1.73系の複雑なロジック（suppressOn/txAfSupOn/postTxIgnore）を統合

- **抑止種別**
  - 長話抑止  : dur ≥ BUSY_MAX_MS → LONG_SUP_MS=10s
  - バースト抑止  : 10秒窓内で短発が2回以上 → BURST_SUP_MS=10s
  - 送信後抑止: TX後 TX_SUP_MS(ms) の受信を無視

### (3) AUTO（m2）

v2.24f と同等。AUTO_WINDOW（分）観測し、Digital/A0 の優位側へ自動固定。

### (4) 周期ID（v2.10 の正時アライン機能 + v2.24f の二重保存）

DS3231接続時：
- `p##` が60の約数かつ `u1` → 正時アライン動作
- 次回発火時刻を DS3231 から計算し、秒単位精度で送出
- 1分ごとにドリフト補正

未接続時：
- millis() 相対動作（v2.01互換の catch-up 方式）

### (5) イベントログ（v2.21 の AT24C32 機能 + v2.30 継続）

AT24C32 があれば自動的にログ記録。ない場合は無視。

| イベント | コード | データ |
|---------|--------|--------|
| 起動 | BOT | EEPROM ver |
| 周期ID送出 | PER | Track番号 |
| カーチャンク | CAR | BUSY時間(ms) |
| 長話抑止 | SUP | BUSY時間(ms) |
| バースト抑止 | BST | バースト回数 |

---

## 5. EEPROM 仕様

### レイアウト（CONFIG_VERSION=7 / v2.30）

 フィールド名        | 型       | コマンド | 説明
---------------------|----------|----------|-------------------------
 magic               | uint32_t | 自動     | 識別子 0xDEADBEEF
 busySrc             | uint8_t  | m        | 0=DIGITAL, 1=A0, 2=AUTO
 busyMin             | uint32_t | n        | ms
 busyMax             | uint32_t | b        | ms
 periodMin           | uint32_t | p        | 分
 txSupMs             | uint32_t | r        | ms
 a0Low               | int      | L        | 0〜1023
 a0High              | int      | G        | 0〜1023
 a0Hold              | uint32_t | a        | ms
 autoWinMin          | uint32_t | w        | 分
 dfpTimeoutMs        | uint32_t | d        | ms
 tmBusyActiveHigh    | uint8_t  | g        | 0=LOW=busy, 1=HIGH=busy
 periodQuietMs       | uint32_t | k        | ms
 rtcAlignOn          | uint8_t  | u        | 0=OFF, 1=ON（v2.10追加）
 saveTimestamp       | uint32_t | 自動     | 保存時刻（秒換算）v2.20追加
 ver                 | uint8_t  | 自動     | レイアウト版（現在=7）

### v2.30 で削除されたフィールド

以下は v2.24f (ver=6) / v1.80i (ver=5) に存在していましたが、v2.30 では**削除**されました：

- `suppressOn` (suppressors_enabled) ← 抑止は常時有効
- `txAfSupOn` (tx_after_suppress) ← 抑止は常時有効（r#### で時間制御）
- `idleMin` (idle_min_ms) ← 内部固定200ms
- `pttPreMs` (ptt_pre_ms) ← 内部固定1000ms
- `pttPostMs` (ptt_post_ms) ← 内部固定1000ms

### マイグレーション（自動実行）

起動時に `config.ver` をチェック：

```
config.ver = 5（v1.80i）→ ver=7 へ自動更新、新フィールド補完
config.ver = 6（v2.24f）→ ver=7 へ自動更新、新フィールド補完
config.ver = 7（v2.30）→ そのまま使用
```

---

## 6. コマンド仕様（v2.30）

### 継続コマンド（v2.24f との互換性あり）

```
m0/m1/m2 b#### n#### p## k####
r#### w## L### G### a#### d####
g0/g1 u0/u1 TYYYYMMDDHHmmss
v v0 V Z q x R F
l0/l1/l2/l3/l4
```

### 削除されたコマンド

```
j####   (PTT PRE)
J####   (PTT POST)
i####   (IDLE_MIN)
s0/s1   (SUPPRESSORS_ENABLED)
t0/t1   (TX_AFTER_SUPPRESS)
h       (HELP)
0/1/2/3 (TEST outputs) ← l0/l1/l2/l3 ではなく log level へ変更
```

---

## 7. ログレベル（v2.30 拡張）

```
l0  = LOG_OFF       (ログ出力なし)
l1  = LOG_ERR       (エラーのみ)
l2  = LOG_MIN       (起動時メッセージ) ← 既定
l3  = LOG_FULL      (全イベント)
l4  = LOG_VERBOSE   (v2.30追加：詳細デバッグ)
```

---

## 8. デフォルト値（v2.30）

| パラメータ | v2.30 | 備考 |
|-----------|-------|------|
| BUSY_INPUT_SOURCE | AUTO | m2 |
| BUSY_MIN_MS | 500 | n500 |
| BUSY_MAX_MS | 3900 | b3900 |
| PERIOD_MS | 30分 | p30 |
| TX_SUP_MS | 3000 | r3000 |
| A0_LOW_TH | 300 | L300 |
| A0_HIGH_TH | 700 | G700 |
| A0_HOLD | 800 | a800 |
| AUTO_WINDOW | 30分 | w30 |
| LONG_TALK_MS | BUSY_MAX_MS | 内部 |
| DFP_TIMEOUT_MS | 20000 | d20000 |
| TM_BUSY_ACTIVE_HIGH | true | g1 |
| PERIOD_QUIET_MS | 2000 | k2000 |
| RTC_ALIGN_ON | true | u1 |
| EEPROM_VER | 7 | 自動 |

---

## 9. 変更履歴（v2.30 固有）

### 追加・改善（v1.80i 由来）

- 絶対時刻比較（PTT PRE/POST）
- 抑止起点統一（単一 busySupUntil タイマー）
- ブロッキング対策（Serial.setTimeout(50)）
- CONFIG_VERSION = 7 へ更新
- ログレベル l4 追加

### 削除（内部固定化）

- j/J/i/s/t/h コマンド

### 継続（v2.24f との互換性）

- DS3231 RTC / AT24C32 EEPROM
- イベントログ機能
- 正時アライン機能
- Ver.5 ピンマップ

---

## 10. テスト項目（v2.30固有）

```
□ 起動ログで "v2.30" 表示
□ V コマンドで EEPROM_VER=7 表示
□ q コマンドで既存設定継承確認
□ r コマンド（抑止時間）が機能
□ DS3231接続時: RTC時刻表示
□ AT24C32接続時: イベントログ記録
□ l2-l4 でログレベル切替可能
□ j/J/i/s/t/h が効かないこと確認（正常）
```

---

## 11. 制約・注意事項

- PTT PRE/POST は固定1000ms（ユーザーカスタマイズ不可）
- IDLE_MIN_MS は固定200ms（ユーザーカスタマイズ不可）
- 抑止は常時有効（OFF できない）
- ブロッキング上限：Serial.setTimeout(50) により最大50ms
- EEPROM 書込みは「値変更時のみ」

---

**End of SPECIFICATION**

