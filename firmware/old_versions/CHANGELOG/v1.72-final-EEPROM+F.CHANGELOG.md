
# CHANGELOG.md - OpenCCVoice ID Guidance Controller

本プロジェクトは、Arduino Nano (ATmega328P, 5V) 上で動作する、DMR無線機の **ID送出支援**コントローラです。

> **記法:** Keep a Changelog 準拠  
> **日付:** JST（日本標準時）

---

## [v1.72-final-EEPROM+F] - 2026-01-20 (Factory Reset 対応 / Final Release)
**概要:** 1.72 系列の最終完成形。現場運用の「安定性」「永久設定保持」「誤操作対策」を統合した版。

### Added
- **Factory Reset（F コマンド）追加**  
  - `F` を受信すると `applyDefaults()` を実行し、**すべての設定を初期値に戻す**。  
  - その後 `saveSettings()` を実行し、EEPROM も即時初期化。  
  - 電源再投入後も「初期値の状態」で起動する安全設計。

- **EEPROM設定保存（永続化）**  
  - m, b, n, s, t, r, p, w, L/G/a など主要な全パラメータが即保存される。  
  - 書込み後、再起動しても同じ設定で開始。

- **AUTOモード：確定固定（One-Shot Lock + 永続保存）**  
  - AUTO判定後、優位ソース(D6/A0)へ自動固定し **autoLocked = true**。  
  - さらに EEPROM に保存するため、再起動しても固定モードを保持。

### Changed
- **取扱説明書／コマンド体系の整合性**  
  - `x`（停止）→`r`（再開）の動作を整理。  
  - `H` プリセット（s0/t0/b4000）の仕様を最終版として確定。

### Fixed
- **burstCount 未定義によるビルドエラーを修正。**  
- **A0フロントの有効化フラグ（USE_A0_FRONT）を明示的に 1 に設定。**  
- **HTMLエスケープ（< / >）残存を除去**し、Arduino IDE で確実にビルド可にした。

---

## [v1.71-stable-final] - 2026-01-19
**概要:** 安定化パッチの集大成。DFPlayer・AUTO・pinMode の最終調整版。

### Fixed
- **DFPlayer 終了判定の早期遷移を防止**  
  - BUSY の HIGH戻り後、40ms の安定時間を追加（`busyHighTS != 0` 条件）。
- **A0_PIN 未定義の修正**  
- **pinMode 初期化誤り修正（INPUT/OUTPUT の誤設定を全て是正）**

### Added
- `h`（ヘルプ）、`x`（STOP）、`t0/t1` を完全実装  
- 状態要約 `q` に `LONG_TALK_MS` を追加

### Changed
- AUTOモードの見える化：D6/A0 のリアル反応＋点滅で判別性向上

---

## [v1.71] - 2026-01-19 (Integrated-MAX)
**概要:** CHUNK上限と長話判定の閾値を統合し、設定を一元化した版。

### Added
- **BUSY_MAX_MS と LONG_TALK_MS の統合制御**（`b####` のみで両方更新）
- **未使用側LEDの点滅仕様**（D6/A0の可視化）

### Notes
- AUTO は 30分窓 + 5分グレース

---

## [v1.69g] - 2026-01-15 (H1 Preset Hotfix)
**概要:** H1リピータ（2〜4秒の長いカーチャンク）用の緊急改善版。

### Added
- **H1プリセット：'H' → s0 / t0 / b3900**  
  H1環境 2.3〜3.9s の CHUNK を全て 1発で ID 扱い可能に。

### Fixed
- コンパイルエラーの修正

---

## [v1.69f] - 2026-01-12 (Unified Architecture)
**概要:** 全機能をひとつの共通アーキテクチャに統合。

### Changed
- 既定設定を **v1.67c 互換（D6固定・抑止なし）** へ統一。  
- 必要な機能をコマンドで ON にする構成へ変更。

---

## [v1.69e] - 2026-01-09 (AUTO Mode C)
### Added
- AUTO（方式C）を初実装。D6/A0 の発生数で自動決定し固定。  
- 固定時、D13 LED を 2Hz で 3 秒点滅。

---

## [v1.69c] - 2026-01-09 (Status LED Priority)
### Changed
- D13 LED の優先順位  
  長会話 > TX後抑止 > バースト > OFF

---

## [v1.69b] - 2026-01-09 (Extended Limits)
### Added
- 抑止ステータスLED（D13）  

### Changed
- BUSY_MAX_MS を 2500ms へ拡張（LONG_TALK_MS と同期）

---

## [v1.68] Series - 2025-12-End (A0 Integration & Monitoring)
### Added
- A0 検知ヒステリシス（L/G）＋保持 a（800ms）  
- 文脈抑止（長会話 / バースト）  
- Δt ログの可視化

---

## [v1.67] Series - 2025-12-Mid
### Added
- TX後抑止（3秒）  
- DFPlayer BUSY ミラー D2 出力

---

## [v1.64] - 2025-12-Early
### Changed
- PTT最低ONガード  
- BUSYヒステリシス

---

## [Pre-v1.64 / Prototype] - 2025-08 ~
### History
- 1.50：初の安定版（D6 BUSY判定、PTT PRE/POST）  
- D6 monitor：D6テストツール  
- CCVoice：最初期プロトタイプ

---

## 付録：ピンマップ（Arduino Nano v1.72 時点）

| Pin | 機能 | 備考 |
|:---:|:--- |:--- |
| D2  | DFP BUSYミラー出力 | 再生中=HIGH |
| D3  | テストSW | INPUT_PULLUP |
| D4  | 受信LED (D6) | D6/AUTO時 |
| D5  | PTT出力 | HIGH=送信 |
| D6  | TM BUSY入力 | HIGH=受信 |
| D7  | DFPlayer BUSY入力 | LOW=再生中 |
| D10 | SoftwareSerial TX | → DFPlayer RX |
| D11 | SoftwareSerial RX | ← DFPlayer TX |
| D12 | A0受信LED | A0/AUTO時 |
| D13 | 抑止ステータスLED | 長会話/バースト等 |
| A0  | アナログ入力 | NJM2072出力へ接続 |
