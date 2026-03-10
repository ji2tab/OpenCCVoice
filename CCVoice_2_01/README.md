# OpenCCVoice Guidance Controller

取扱説明書 / コマンドリファレンス  
**バージョン: v2.01 — Ver.5 ピンマップ対応版**  
**最終更新日: 2026/03/10**  
対象ハード: Arduino Nano (ATmega328P / 5V) **基板 Ver.5**  
通信設定: 115200 bps / 8N1 / 改行なし（またはLF）

---

## ■ 概要

本機は DMR 無線機の ID 送出を自動制御するコントローラです。  
PC と USB 接続し、シリアルモニタから各種コマンドで設定を変更できます。

全ての設定は EEPROM に自動保存され、電源断でも保持されます。  
**v2.01 は Ver.5 基板向けにピンアサインを全面変更したバージョン**です。  
EEPROM レイアウトは v1.73d/e と同一（`config.ver = 4`）のため、  
既存の設定を引き継いだままアップデートできます。

---

## ■ v2.01 の特徴・変更点

### 【Ver.5 基板ピンマップへ移行】

| ピン | 機能 | 方向 |
|------|------|------|
| D2 | テストSW | 入力（INPUT_PULLUP） |
| D3 | DFP BUSYミラー出力（再生中=HIGH） | 出力 |
| D4 | ModBusy LED | 出力 |
| D5 | PTT（HIGH=送信） | 出力 |
| D6 | 抑止 LED | 出力 |
| D7 | A0検知 LED | 出力 |
| D10 | DFPlayer BUSY入力（LOW=再生中） | 入力（INPUT_PULLUP） |
| D11 | TM BUSY入力（Digital入力） | 入力（INPUT_PULLUP） |
| D12 | Arduino TX → DFPlayer RX | SoftwareSerial |
| D13 | Arduino RX ← DFPlayer TX | SoftwareSerial |
| A0 | アナログBUSY入力 | 入力 |

### 【RESUME コマンドを `R`（大文字）に変更】
- v1.73e の `r`（小文字）から変更
- `r####`（送信後抑止時間設定）との衝突を解消

### 【v1.73e の全機能を継承】
- 送信直後ガード（postTxIgnore）
- 周期ID catch-up 対策
- 抑止中も A0 BUSY 観測継続
- 周期ID 静寂ガード（`k####`）
- EEPROM バージョン管理（`config.ver = 4`）
- DFPlayer フェイルセーフ（`d####`）
- TM BUSY 極性切替（`g0/g1`）
- AUTO 判定（`m2`）

---

## ■ クイック・コマンド一覧

| コマンド | パラメータ | 既定値 | 説明 |
|----------|-----------|--------|------|
| `m` | 0/1/2 | 0 | DIGITAL固定 / A0固定 / AUTO判定 |
| `b` | 500〜 | 3900 | 最大受信長 (ms) |
| `n` | 100〜 | 500 | 最小受信長 (ms) |
| `i` | 0〜 | 200 | 直前アイドル時間 (ms) |
| `s` | 0/1 | 1 | 抑止 ON/OFF |
| `t` | 0/1 | 1 | 送信後無視 ON/OFF（s1時） |
| `r` | 0〜 | 3000 | 送信後無視時間 (ms) |
| `p` | 0〜 | 30 | 周期ID（分）※0=停止 |
| `k` | 0〜600000 | 2000 | 周期ID静寂時間 (ms) |
| `w` | 1〜 | 30 | AUTO観測時間（分） |
| `L` | 0〜1023 | 300 | A0閾値LOW |
| `G` | 0〜1023 | 700 | A0閾値HIGH |
| `a` | 0〜 | 800 | A0保持時間 (ms) |
| `d` | 0〜 | 20000 | DFPフェイルセーフ (ms) ※0=無効 |
| `g` | 0/1 | 1 | TM BUSY極性（LOW/HIGH=busy） |
| `q` | — | — | 設定一覧（EEPROM_VER含む） |
| `H` | — | — | 汎用プリセット（s0/t0/b3900） |
| `x` | — | — | Safe Stop（全機能停止） |
| `R` | — | — | 停止状態から復帰（**大文字**） |
| `F` | — | — | Factory Reset（EEPROM初期化） |
| `0〜3` | — | — | ログレベル |
| `h` | — | — | コマンド一覧 |

---

## ■ コマンド詳細解説

**[m：モード設定]**  
m0 … DIGITAL（D11 デジタルBUSY）  
m1 … A0（アナログ）  
m2 … AUTO（DIGITAL/A0 を観測し優位側へ自動固定）  
※AUTO中は DIGITAL/A0 どちらの入力でも ID が出る

**[b：最大受信長]**  
b3900（3.9秒）までを短発として受付  
dur ≥ b#### は長話と判定し 10秒抑止（LONG_SUP）

**[n / i：最小受信長 / 直前アイドル]**  
n#### … dur < n#### はノイズ扱い  
i#### … 直前アイドル < i#### は短発無効化

**[s / t / r：抑止ガード]**  
s1 … 抑止 ON（長話/バースト検出）  
t1 … 自局送出後、r#### ms BUSY を無視  
r#### … 送信後無視時間（ms）

**[p：周期ID]**  
p##（分）で Track 002/003 を交互送出。0で停止。  
絶対時刻基準で等間隔スケジューリング

**[k：周期IDの静寂ガード]**  
周期IDを出すための「BUSY OFF からの静寂期間（ms）」  
例：k2000 → BUSY 終了後 2秒静かなら送出  
例：k5000 → より強い割り込み防止  
k0 → 静寂ガード無効

**[w：AUTO観測期間]**  
AUTO_WINDOW 分観測 → 優位側へ固定

**[A0検知（L/G/a）]**  
LOW/HIGH 閾値＋保持時間でアナログBUSYを安定化  
A0 基準電圧は VCC=5V（閾値300≒1.46V / 700≒3.42V）

**[d：DFPフェイルセーフ]**  
DFPlayer BUSY が戻らない場合、d#### ms 経過で強制 PTT OFF  
d0 で無効化

**[g：TM BUSY極性]**  
g0 = LOW=busy  
g1 = HIGH=busy（既定）

**[x / R：停止と再開]**  
x … 全動作停止（Safe Stop）  
R … 停止中の復帰（**大文字 R**）

**[F：初期値へ]**  
全設定を初期化し EEPROM 保存

**[h / 0〜3：ヘルプ・ログ]**  
h … コマンド一覧  
0〜3 … ログレベル（NONE/ERR/INF/DBG）

---

## ■ LED 表示の意味

| LED | ピン | 点灯条件 |
|-----|------|---------|
| ModBusy LED | D4 | BUSY 受信中（readBusy()=true） |
| 抑止 LED | D6 | 抑止中（長話/バースト/POST-TX） |
| A0検知 LED | D7 | A0 BUSY 検知中 |
| DFP BUSYミラー | D3 | DFPlayer 再生中（HIGH） |

---

## ■ フェイルセーフ（DFPlayer BUSY 異常）

DFPlayer BUSY が戻らない場合、**d#### ms 経過後に強制 PTT OFF**。

- 既定：d20000（20秒）
- d0：無効（判定スキップ）

---

## ■ トラブルシューティング

**● IDが出ない**
- 停止中（x）→ `R`（大文字）で復帰
- 長話（b3900超）→ 抑止中
- D6 LED 点灯中（抑止）では動作しない

**● IDが止まらない**
- t1 と r#### を増やす
- n#### / i#### を大きくする

**● 音声が途中で切れる**
- d#### を延長（d20000 など）
- SDカード階層（/mp3/0001.mp3）を確認
- D10（BUSY）が途中 HIGH になっていないか確認

**● BUSY判定が逆**
- g0/g1 の設定確認（`q` で現在の極性を表示）

**● STOP 後に復帰できない**
- v2.01 では `R`（大文字）を入力。`r`（小文字）では復帰しない。

**● D13 に接続した外部機器が誤動作する**
- v2.01 では D13 は SoftwareSerial（DFPlayer TX 受信）に変更。
- 旧配線（抑止 LED 等）は D6 へ移設してください。

---

## ■ 配線メモ（v2.01 / Ver.5 基板）

```
DFPlayer TX → D13（Arduino RX）
DFPlayer RX ← D12（Arduino TX）
DFPlayer BUSY（LOW=再生中）→ D10
D10 BUSYミラー（反転）→ D3
TM BUSY → D11（g0/g1 で極性指定）
PTT → D5（HIGH=送信）
ModBusy LED → D4
抑止 LED → D6
A0検知 LED → D7
テストSW → D2（INPUT_PULLUP）
アナログBUSY → A0
```

---

## ■ v2.01 重要仕様（抜粋）

**【RESUME コマンド】**  
STOP 中の復帰は **`R`（大文字）** です。`r`（小文字）は送信後抑止時間設定コマンドです。

**【周期ID 静寂ガード（k####）】**  
BUSY OFF → k#### ms 静寂で周期ID 許可。  
BUSY中、静寂不足、抑止期間中は延期。`q` で QUIET(ms) を確認可能。

**【EEPROM バージョン（CONFIG_VERSION=4）】**  
v1.73d/e からの更新時、自動マイグレーションが働き設定を引き継ぎます。  
`EEPROM_VER=4` と表示されれば正常です。

---
