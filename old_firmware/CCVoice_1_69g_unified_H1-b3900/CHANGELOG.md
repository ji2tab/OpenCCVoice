
# CHANGELOG  
TM-8250 ID Guidance Controller  
Arduino Nano (ATmega328P, 5V)

本 CHANGELOG は v1.67c → v1.69g-unified-H1-b3900 までの全履

---

# [1.69g-unified-H1-b3900] - 2026-01-19
### Changed
- H1即復旧プリセットの上限を **3900ms** に最適化  
  - `BUSY_MAX_MS=3900`、`LONG_TALK_MS=3900` に既定化  
  - H1実環境（2.3〜3.9s カーチャンク）をフル許容
- 起動バナーを  
  `"[BOOT] 1.69g-unified-H1-b3900 (H1プリセット既定)"`  
  に変更
- `'H'` プリセットを `s0 / t0 / l0 / b3900` に統一

### Notes
- 既定：  
  - BUSY_INPUT_SOURCE=D6  
  - SUPPRESSORS=OFF（s0）  
  - TX_AFTER=OFF（t0）  
  - D4_BLINK=OFF（l0）  
  - BUSY_MIN_MS=500／BUSY_MAX_MS=3900／LONG_TALK_MS=3900  
- AUTO方式C、周期ID（30分 002/003）、DFPlayer制御などは 1.69g と同一  
- SFR/DMR中はアナログIDは乗らない（FMでテスト）  
- A0とD6は同時接続不可

---

# [1.69g-unified-H1] - 2026-01-19
### Fixed
- ビルド不能のシンタックスエラーを修正  
  - `Serial.println(...toggle next"]);` の余計な `]` を削除  
  - `formatUptime(...sizeof(up]);` の括弧不一致を修正

### Changed
- 起動バナーを **H1専用表記** に変更  
  - `[BOOT] 1.69g-unified-H1 (H1プリセット既定)`
- バージョン表記を更新（機能仕様は 1.69f-H1 と同一）

### Notes
- 既定：  
  - BUSY_INPUT_SOURCE=D6  
  - SUPPRESSORS=OFF  
  - TX_AFTER=OFF  
  - D4_BLINK=OFF  
  - BUSY_MAX_MS=3000  
  - LONG_TALK_MS=3000  
- 方式C（AUTO）は30分窓＋5分グレース  
- A0/D6 同時使用不可

---

# [1.69f-unified-H1] - 2026-01-18
### Added
- **H1即復旧プリセット**（s0 / t0 / l0 / b3000）を既定化  
- **AUTO方式C** を標準搭載  
  - 30分窓の D6エッジ数・A0イベント数で D6/A0 のどちらかへ自動固定  
  - 切替時 D13 を 2Hz で 3秒高速点滅

### Changed
- `b####` 設定時に `LONG_TALK_MS` を自動ペアリング  
- D4の上限超過点滅（l0/l1）を追加

### Notes
- BUSYソースは既定 D6  
- DMR/SFRはアナログIDが乗らない（FMでテスト必須）  
- A0/D6 同時配線不可

---

# [1.69f-unified] - 2026-01-09
### Goal
- v1.67c 互換の **ゼロデグレ既定**（D6のみ／抑止OFF）を維持  
- 新機能（抑止・A0・AUTO・D4点滅）はユーザーが明示 ON したときのみ動作

### Added
- シリアルコマンド：m0/m1/m2, s0/s1, t0/t1, l0/l1, b####  
- AUTO方式C（m2時のみ有効）

### Maintained
- 周期ID（30分 002/003）、D7→D2ミラー、テストSW（1/2/3クリック）

---

# [1.69e] - 2026-01-09
### Added
- AUTO方式C 初実装  
- D4 上限超過点滅（dur > BUSY_MAX_MS で点滅）

### Maintained
- 文脈抑止（長会話/バースト）、D13 表示、周期ID

---

# [1.69c] - 2026-01-09
### Added
- D13ステータスLEDに TX後抑止（3秒）も表示

### Changed
- LED優先順位：長会話 ＞ TX後 ＞ バースト ＞ OFF

---

# [1.69b] - 2026-01-09
### Added
- D13 を抑止ステータスLEDに割当

### Changed
- BUSY_MAX_MS=2500、LONG_TALK_MS=2500 に拡張

---

# [1.69a2] - 2026-01-09
### Fixed
- IDが発火しない問題を修正  
  - ID検出源を **D6||A0 に復旧**（v1.68j準拠）  
  - `A0静音必須` を撤廃

### Changed
- PTT_PRE_MS=1000 / POST_MS=1000 に安全側調整

---

# [1.69a] - 2026-01-09
### Changed
- A0ログを DEBUG 限定化  
- A0ログ安定化（200ms/500ms）

---

# [1.69] - 2026-01-09
### Added
- 周期ID（v1.67e準拠）を復旧  
- `b####` で短受信ID抑止を切替

### Changed
- ID判定を D6専用へ（A0はテレメトリのみ）

---

# [1.68j-final] - 2026-01-08
### Added
- 全ログをイベント化（Δt付き）
- STOP(x) で uptime 表示

### Changed
- 統合BUSY = D6 || A0

---

# [1.68a〜1.68i+]  - 2026-01-xx  
（A0 front-end・文脈抑止・テレメトリ強化 等を段階的に追加）

---

# [1.67e] - 2025-12-xx
### Added
- TX後3秒の D6無視（誤トリガ防止）

### Maintained
- 短受信判定（500..1500）、不応期（3s）、周期ID

---

# [1.67c] - 2025-12-xx
### Initial
- 初期実装（短BUSY→001、先行/後保持、テストSW、周期ID、DFPlayer制御）

---

# 最終更新：2026-01-19
