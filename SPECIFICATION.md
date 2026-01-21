
```text
========================================================================
【仕様書（技術者向け）】
========================================================================

1. 基本情報
- 名称     : OpenCCVoice ID Guidance Controller
- バージョン: 1.72-final-EEPROM+F
- MCU      : Arduino Nano（ATmega328P, 5V）
- 依存     : SoftwareSerial.h、EEPROM.h
- DFPlayer : BUSY監視（D7=LOW 再生中）、9600bps制御

2. 機能仕様（Functional）
(1) 短発受信の自動ID送出
  dur が BUSY_MIN_MS ≤ dur < BUSY_MAX_MS のとき ID（トラック1）送出
  DFPlayer BUSY HIGH を 40ms 安定で再生完了判定
  PTT PRE/POST = 各 1000ms

(2) 抑止（長話・バースト・送信後）
  dur ≥ BUSY_MAX_MS → LONG_SUP_MS=10s
  10秒間に2回以上の短発 → BURST_SUP_MS=10s
  t1 時、送信後 TX_SUP_MS(ms) の受信無視

(3) AUTO（m2）
  AUTO_WINDOW(分)中の D6エッジ/A0イベントで優位側へ自動固定
  固定後 autoLocked=true、EEPROM保存
  切替時 D13 を3秒高速点滅、直後1秒の受信抑止

(4) 周期ID
  PERIOD_MSごとにトラック2/3を交互送出（IDLE時のみ）

(5) EEPROM 永続化
  m/n/b/p/r/L/G/a/w/s/t の変更で即保存
  起動時にロード。magicエラー時は applyDefaults()

(6) LED可視化
  使用側LED=実反応、未使用側=点滅
  抑止LED: 抑止/バースト/AUTO確定などを可視化

3. 非機能仕様（Non-Functional）
- 応答性: D6デバウンス5ms
- 安定性: DFPlayer BUSY 40ms確認、PTTガード
- 可搬性: Arduino IDE 1.8.x/2.x
- 保守性: printSummary()、ログレベル

4. インタフェース仕様
- D2  : DFP BUSYミラー出力
- D3  : テストSW INPUT_PULLUP
- D4  : D6系LED
- D5  : PTT出力
- D6  : TM BUSY 入力
- D7  : DFPlayer BUSY 入力
- D10 : DFPlayer TX
- D11 : DFPlayer RX
- D12 : A0系LED
- D13 : 抑止LED
- A0  : アナログ入力（0..1023）

5. 既定値
BUSY_INPUT_SOURCE=D6
BUSY_MIN_MS=500
BUSY_MAX_MS=1500（＝LONG_TALK_MS）
PERIOD_MS=30min
TX_SUP_MS=3000
SUPPRESSORS_ENABLED=true
TX_AFTER_SUPPRESS_ENABLED=true
A0_LOW_TH=300 / A0_HIGH_TH=700 / A0_HOLD=800
AUTO_WINDOW=30min

6. コマンド仕様（完全）
m0/m1/m2
n####
b####
i####(未使用)
s0/s1
t0/t1
r####
p##
w##
L### / G### / a####
q
H
x / r
F（Factory Reset）
0/1/2/3

7. 振る舞い仕様
dur < MIN       → 無効
MIN ≤ dur < MAX → 短発
dur ≥ MAX       → 長話抑止
PTTステート：IDLE → ON_WAIT → PLAYING → OFF_WAIT → IDLE

8. 制約
デバウンス 5ms
EEPROM 書込みは変更時のみ
SRAM制約内（2KB）

9. 試験
m0 にて 600ms→ID、1600ms→抑止
AUTO: m2→w1→[AUTO-FIXED]
t1/r3000：送信後3s無視

10. リスク
DFPlayer BUSY 個体差
外部回路のプルアップ不足
A0/D6 同時配線は禁止

11. 変更履歴
詳細は CHANGELOG.md を参照

========================================================================
```
``
