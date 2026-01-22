
# CHANGELOG.md - OpenCCVoice ID Guidance Controller

本プロジェクトは、Arduino Nano (ATmega328P, 5V) 上で動作する、DMR無線機の **ID送出支援**コントローラです。

> **記法:** Keep a Changelog 準拠  
> **日付:** JST（日本標準時）

---

## [v1.73-unified-safe] - 2026-01-22
**（最新） 完全統合・安全性強化・機種依存排除版**

本版は、過去の試験的記述や個別モデル名への依存をすべて排除し、  
**どの環境でも使える完全汎用版** として再構成したシリーズ最新の完成形です。

### Added
- **DFPlayer フェイルセーフ（8秒固定）**  
  BUSY 信号が出ない／戻らない場合でも、PTT の張り付きを 8 秒時限で強制停止。
- **A0/D6 AUTO 判定（固定型）**  
  観測窓 `w##` 分間で D6 / A0 の発生数を統計比較し **自動固定**（EEPROM保存）。  
  固定時は D13 LED を 3 秒点滅通知。
- **Arduino視点の配線命名へ統一（誤読排除）**  
  `ARD_RX_FROM_DFP=10`（DFP TX が入る） / `ARD_TX_TO_DFP=11`（DFP RX へ送る）。  
  ※従来配線は不変（物理入れ替え不要）。
- **STOP/RESUME（x / r）完全統合**  
  停止中は BUSY/A0/周期ID/抑止など全ロジック停止。`r` は停止中のみ有効。
- **HELP（h）コマンド**  
  コマンド一覧をシリアルから即時確認可能。

### Changed
- **完全汎用化**  
  試験過程で登場した固有名や機種依存の記述を全廃（コード／ドキュメント共）。  
  `H` プリセットは「汎用」プリセットとして再定義（`s0/t0/b3900`）。
- **周期IDの安定化**  
  絶対時刻スケジューリング方式に統一（電源投入から等間隔）。
- **A0/D6 前段処理の整理**  
  A0は L/G ヒステリシス + a(ms) 保持。D6はデバウンス＋統計カウントをAUTOと連動。

### Fixed
- I/O 方向や命名の混乱箇所を全面整理（D10=Arduino側RX / D11=Arduino側TX を明示）。  
- すべての HTML エスケープ（`&lt;` `&gt;`）を排除。  
- AUTO 統計が稀に固定されない条件を修正。  
- STOP 状態で LED が残点灯する可能性を排除。

---

## [v1.72-final-EEPROM+F] - 2026-01-20
**概要:** 1.72 系列の最終完成形。運用の「安定性」「永続設定」「誤操作対策」を統合。

### Added
- **Factory Reset（F）**  
  全設定を初期値へ戻し、EEPROMにも即時保存（電源再投入後も初期状態で起動）。
- **EEPROM 永続化**  
  主要パラメータ（m, b, n, s, t, r, p, w, L/G/a）を即保存。
- **AUTO 確定固定**  
  AUTO判定後に固定化し EEPROM 保存（再起動後も保持）。

### Changed
- `x`（停止）→`r`（再開）の仕様明確化。  
- 汎用プリセット `H`（`s0/t0/b4000`）を定義。

### Fixed
- 変数定義不足や A0 前段の初期化漏れを修正。  
- HTMLエスケープ残存を除去。

---

## [v1.71-stable-final] - 2026-01-19
**概要:** 安定化パッチの集大成。

### Fixed
- DFPlayer BUSY 戻り判定に 40ms 安定時間を追加。  
- A0_PIN 未定義、pinMode 初期化漏れを修正。

### Added
- `h`（ヘルプ）、`x`（STOP）、`t0/t1` を完全実装。  
- `q`（要約）に LONG_TALK_MS を追加。

---

## [v1.71] - 2026-01-19 (Integrated-MAX)

### Added
- BUSY_MAX_MS と LONG_TALK_MS の統合制御（`b####`で両方更新）。  
- 未使用側 LED の点滅。

### Notes
- AUTO は 30 分窓 + グレースを想定。

---

## [v1.69g] - 2026-01-15 (Preset & Stability)
### Added
- プリセット `H`（`s0/t0/b3900`）を追加。

### Fixed
- 複数のコンパイル・安定性の問題を修正。

---

## [v1.69f] - 2026-01-12 (Unified Architecture)
### Changed
- 既定を v1.67c 互換（D6固定・抑止なし）へ統一。  
- 必要機能はコマンドで ON にする方針へ変更。

---

## [v1.69e] - 2026-01-09 (AUTO Mode – Count Based)
### Added
- AUTO（D6/A0 発生数比較）を実装。  
- 決定時 D13 LED を 2Hz で 3 秒点滅。

---

## [v1.69c] - 2026-01-09 (Status LED Priority)
### Changed
- D13 LED の優先順を整理（長会話 ＞ 送信後抑止 ＞ バースト ＞ OFF）。

---

## [v1.69b] - 2026-01-09 (Extended Limits)
### Added
- 抑止ステータスLED（D13）。

### Changed
- BUSY_MAX_MS を 2500ms に拡張。

---

## [v1.68] Series - 2025-12-End (A0 Integration & Monitoring)
### Added
- A0 ヒステリシス（L/G）＋保持 a(ms)。  
- 長会話・バーストの文脈抑止。  
- Δt ログの可視化。

---

## [v1.67] Series - 2025-12-Mid
### Added
- 送信後抑止（3秒）。  
- DFPlayer BUSY ミラー D2 出力。

---

## [v1.64] - 2025-12-Early
### Changed
- PTT最低ONガード。  
- BUSYヒステリシス。

---

## [Pre-v1.64 / Prototype] - 2025-08 ~
### History
- 1.50：初の安定版（D6 BUSY判定、PTT PRE/POST）。  
- D6 monitor：D6テストツール。  
- CCVoice：初期プロトタイプ。

---

## 付録：ピンマップ（Arduino Nano / 最新 v1.73 時点）

| Pin | 機能 | 備考 |
|:---:|:--- |:--- |
| D2  | DFP BUSYミラー出力 | 再生中=HIGH（反転出力） |
| D3  | テストSW | INPUT_PULLUP |
| D4  | 受信LED (D6) | D6/AUTO時 |
| D5  | PTT出力 | HIGH=送信 |
| D6  | TM BUSY入力 | HIGH=受信（極性は回路に依存し切替可） |
| D7  | DFPlayer BUSY入力 | LOW=再生中 |
| D10 | Arduino RX | ← DFPlayer TX |
| D11 | Arduino TX | → DFPlayer RX |
| D12 | A0 LED | A0/AUTO時 |
| D13 | 抑止/AUTO LED | 長会話・バースト・AUTO固定通知 |
| A0  | アナログ入力 | ヒステリシス＋保持適用 |
