# CHANGELOG

> 本プロジェクト（TM-8250 ID Guidance Controller）は、Arduino Nano (ATmega328P, 5V) 上で動作する、TM-8250系無線機の **ID送出支援**コントローラです。本`CHANGELOG.md`は **v1.67c → v1.69b** までの変遷を、できる限り詳細に記録しています。
>
> 記法は [Keep a Changelog](https://keepachangelog.com/ja-JP/1.0.0/) を参考にし、カテゴリ（Added/Changed/Fixed/Maintained/Removed/Deprecated/Known Issues/Migration）で整理しています。
>
> 日付は JST（日本標準時）での作業基準です。

---

# CHANGELOG

## 1.69g-unified-H1 (2026-01-19)
### Fixed
- **ビルド不能のシンタックスエラーを修正**
  - `Serial.println(F("[Periodic] played -> toggle next"]));` の余計な `]` を削除。
  - `formatUptime(millis(), up, sizeof(up]);` の括弧不一致を修正。
### Changed
- 起動バナーを **H1専用プリセット表記** に統一：`[BOOT] 1.69g-unified-H1 (H1プリセット既定)`
- バージョン表記のみ更新（ロジック仕様に変更なし）。
### Notes
- 仕様や既定値は **1.69f** と同一（**H1クイックプリセット既定**）。
  - 既定：`BUSY_INPUT_SOURCE=D6` / `SUPPRESSORS=OFF (s0)` / `TX_AFTER=OFF (t0)` / `D4_BLINK=OFF (l0)` / `BUSY_MAX_MS=3000` / `LONG_TALK_MS=3000`
  - `b####` で `BUSY_MAX_MS` を変更した場合、`LONG_TALK_MS` は自動的に同値へペアリング。
- A0/D6の**同時使用は不可**（どちらか一方のみ接続）。AUTO（方式C）は30分窓+5分グレース、切替時にD13で3秒高速点滅通知。
- DFPlayerの基本制御、PTTフェーズ（`PTT_PRE_MS=1000` / `PTT_POST_MS=1000` / 最低ONガード `+50ms`）、周期ID（30分間隔で002/003交互）などの動作は変更なし。

## 1.69f-unified-H1 (2026-01-18)
### Added
- **H1カーチャンク即復旧プリセット**を既定化（H1現場でデグレ即回避）  
  - `H` コマンドで `s0 / t0 / l0 / b3000` を一括適用。
- **AUTO（方式C）** の実装  
  - `m2` で有効。観測窓30分の活動（`d6_edge_count` / `a0_event_count`）から **A0/D6のいずれかに自動固定**（同時使用なし）。
  - 切替時、D13を3秒間 2Hz 高速点滅で通知。
### Changed
- `b####` 設定時に `LONG_TALK_MS` を **自動で同値にペアリング**（上限一体化で調整ミス防止）。
- 受信LED（D4）の上限超過点滅は既定OFF（`l0`）。必要時 `l1` で有効化。
### Notes
- 既定のBUSYソースは **D6固定**（H1向け）。A0使用時は別ビルド（HOLD 600/800ms）を推奨。
- DMR/SFR運用では**アナログ音声IDは乗らない**ため、**FMでテスト**すること。
- A0/D6の**同時配線は不可**。

## [1.69f-unified] - 2026-01-09
### Goal
- **ゼロデグレ既定**（v1.67c互換）：D6のみ／抑止OFF／TX後抑止OFF／CHUNK 500..1500／D4純受信。
- 追加機能（抑止・D13表示・D4上限点滅・A0運用・AUTO切替）は **任意でON**（ランタイムで切替）。

### Added
- シリアルコマンドで動作モードを切替：
  - `m0/m1/m2`（D6/A0/Auto）
  - `s0/s1`（抑止OFF/ON）
  - `t0/t1`（TX後抑止OFF/ON）
  - `l0/l1`（D4上限超過点滅OFF/ON）
  - `b####`（BUSY_MAX_MSを設定 → LONG_TALK_MSとペアリング）
- 方式C（運用中の自動切替）を **AUTO選択時のみ**評価。

### Maintained
- 周期ID（30分/002-003交互）、D7→D2ミラー、テストSW（1/2/3クリック）。

### Notes
- 既定は **完全に v1.67c互換**。追加機能は **明示的にON**しない限り動作しません（統合によるデグレなし）。


## [1.69e] - 2026-01-09
### Added
- **方式C（運用中の自動切替）**を実装：起動時 UNDECIDED → 運用中の活動で D6/A0 を自動判定・固定。
  - D6無活動 + A0活動あり → A0へ。A0無活動 + D6活動あり → D6へ。
  - 切替直後は 1秒 判定停止（安全策）、D13を **2Hz点滅で3秒** 通知。
- **D4（BUSY LED）の上限超過点滅**：受信継続中に `dur > BUSY_MAX_MS` で **1Hz点滅**（受信終了まで）。

### Maintained
- 統一の抑止ロジック：長会話（dur≥`LONG_TALK_MS`→10s）、バースト（10s窓2回→10s）、TX後（3s）。
- D13抑止表示（長会話/TX後=常時ON、バースト=1Hz点滅、解除=OFF）。
- D7→D2 ミラー（反転：再生中HIGH/停止中LOW）。
- 周期ID（30分/002-003交互）、D3テストSW（1/2/3クリック）。

### Notes
- `LONG_TALK_MS` は `BUSY_MAX_MS` とペアリング推奨（既定 2500ms）。
- A0運用でdurが伸びやすい環境では `A0_HOLD_MS=600ms` へ短縮が有効。

## [1.69c] - 2026-01-09
### Changed
- D13ステータスLEDの表示対象に **TX後抑止（3秒）** を追加。
  - 優先度: **長会話抑止 > TX後抑止 > バースト抑止 > 解除**。
  - 長会話/TX後：**常時ON**、バースト：**1Hz点滅**、解除：**OFF**。
### Maintained
- 統合BUSY（D6||A0）によるID判定、イベントのみログ＋Δt、STOP/uptime、TX後3秒抑止の機能自体、文脈抑止、周期ID（30分/002-003交互）を継続。
- A0busy/D12ログはDEBUG時のみ、A0ログ安定化（200ms/500ms）。

## [1.69b] - 2026-01-09
### Added
- D13 を抑止ステータス LED に割り当て（長会話抑止=常時ON、バースト抑止=1Hz点滅、無抑止=OFF）。
### Changed
- `BUSY_MAX_MS=2500`、`LONG_TALK_MS=2500` に拡張/ペアリング。
### Maintained
- 統合BUSY（D6||A0）での ID 判定、イベントのみログ、Δt、STOP/uptime、TX後3秒抑止、文脈抑止、周期ID。A0busy/D12ログはDEBUG＋安定化。



### Changed
- **短受信（CHUNK）上限**を `BUSY_MAX_MS = 2500` に拡張（車由来のCHUNKの取りこぼし低減）。
- **長会話抑止の閾値**を `LONG_TALK_MS = 2500` に **ペアリング**（短受信と長会話の境界整合）。

### Maintained
- **統合BUSY（D6||A0）**でのLED表示と **ID判定**（v1.68j準拠）。
- **イベントのみログ**、**Δt併記**、**STOP(`x`) で uptime 表示**、**TX後3秒抑止**、**文脈抑止**（長会話/バースト）。
- **周期ID**（v1.67e準拠：30分おき、002/003交互、IDLE時のみ）。
- **A0busy/D12ログ**は **DEBUG時のみ**出力＋**安定化**（200ms/500ms）。

### Known Issues
- バースト抑止（10s窓・2回で抑止）は混雑・車通過頻度によって **IDが抑制される**場合あり。必要に応じて `CHUNK_BURST_THRESHOLD=3` や `CHUNK_BURST_SUPPRESS_MS=8000` へ **緩和**可能。

### Migration
- v1.69a2→v1.69b：**新しいパラメータ**のみ。
  - `BUSY_MAX_MS=2500`、`LONG_TALK_MS=2500` を設定。
  - D13 LEDは **内蔵LED (L)** が共用されるため、外付けLEDを使う場合は **330Ω**程度の抵抗を介して D13→LED→GND。

---

## [1.69a2] - 2026-01-09
### Fixed
- **自動IDが発火しない**問題へ対応：
  - **ID検出源**を **統合BUSY（D6||A0）** に **復旧**（v1.68j準拠）。
  - `idOK` から **A0静音必須**のゲート（`ID_REQUIRE_A0_QUIET`）を **撤廃**。A0が揺れていても、他条件が成立すれば ID=OK。

### Changed
- **PTTタイミング**を **安全側**へ（`PTT_PRE_MS=1000` / `PTT_POST_MS=1000`）。DFPlayerの BUSY 検出と相性を高め、確実な ON/OFF を担保。

### Maintained
- **イベントのみログ**、**Δt併記**、**STOP/uptime**、**TX後3秒抑止**、**文脈抑止**、**周期ID（v1.67e準拠）**。

### Known Issues
- **A0ログはDEBUG限定**のため、通常運用（INFO）では A0の挙動の詳細が出ません。解析時は **`3`** を送って DEBUG に切り替え。

### Migration
- v1.69a→v1.69a2：**設定変更不要**。ただし v1.69a の `A0静音必須` で運用していた場合は、挙動が **緩和**される点に留意。

---

## [1.69a] - 2026-01-09
### Changed
- **A0busy/D12ログ**を **DEBUG 限定**化。運用時のログスパム（2～4ms間隔など）を抑制。
- **A0ログ安定化**：`A0_EDGE_STABLE_MS=200ms`、`A0_LOG_MIN_INTERVAL_MS=500ms` に強化。

### Maintained
- **ID判定**は **D6限定**（A0はLED/テレメトリ専用）。
- **A0静音必須**（`ID_REQUIRE_A0_QUIET=1`）は **継続**（※環境により IDが発火しない主因になりうる）。
- **イベントのみログ**、**Δt併記**、**STOP/uptime**、**TX後3秒抑止**、**文脈抑止**、**周期ID**（v1.67e準拠）。

### Known Issues
- A0が常時ON寄り（HOLD/閾値）だと **idOKが常にfalse** となり **自動IDが出ない**場合あり（→ v1.69a2で解消）。

### Migration
- v1.69→v1.69a：**設定変更不要**。ログの出方が **静音化**される点のみ意識。

---

## [1.69] - 2026-01-09
### Added
- **周期IDロジック**を **v1.67e準拠**で **復旧**：
  - 起動時に `nextPeriodicAt = millis() + PERIOD_MS`（絶対スケジュール）。
  - `now - nextPeriodicAt >= 0` で期限到来→ `nextPeriodicAt += PERIOD_MS`。
  - **IDLE時のみ**消化、**002/003交互**。ログ：`[Periodic] due -> flag set` / `played -> toggle next`。
- **シリアルコマンド `'b'`**：**短BUSY由来の自動IDのみ**抑止/許可のトグル（周期ID/テストSWには非適用）。

### Changed
- **ID判定**を **D6のみ**に限定（A0はLED/テレメトリ専用）。
- **A0安定化**（ヒステリシス＋HOLD、ログ安定化）を導入。

### Fixed
- **TX終了後3秒間**のD6無視（誤トリガ防止）を維持（v1.67e継承）。

### Maintained
- **イベントのみログ**、**Δt併記**、**STOP/uptime**、**文脈抑止**（長会話/バースト）。

### Known Issues
- 環境によっては **A0静音必須**が自動IDの発火を阻害（→ v1.69a2で撤廃）。

### Migration
- v1.68j→v1.69：
  - **ID検出源**（v1.68jの D6||A0）→ **D6限定**へ変更。必要なら後続リリース（v1.69a2）で **復旧**。
  - 周期ID（30分/002↔003交互）を **再導入**。無効化したい場合は、条件に `&& false` を追加する等のビルド時対応が必要。

---

## [1.68j-final] - 2026-01-08
### Added
- **イベントのみログ**を徹底。**Δt**（前回同種イベントからの経過時間）併記。
- **STOP(`x`)**で **uptime** を出力、以降はログ抑制（制御は継続）。

### Changed
- **統合BUSY = D6 || A0busy** による受信判定（LED・ID判定）。
- **D6の平常表示**（常時ポーリングログ）を **廃止**（イベントのみへ）。

### Maintained
- 文脈抑止、PTT統一制御、DFPlayer BUSYミラー、テストSWのクリック判定。

### Known Issues
- A0が閾値付近で揺れる環境では **ログ量が多くなる**（→ v1.69系で DEBUG限定＋安定化）。

### Migration
- v1.68i+→v1.68j：**設定変更不要**。ログが **イベントのみ**に統一。

---

## [1.68i+] - 2026-01-xx
### Added
- **D12/D5 の出力エッジ**は **常に表示（unconditional）**。`setPtt()` を導入して PTTログの一元化。

### Changed
- テレメトリの出力項目を強化（A0raw/D12/ID_OK、`c` コマンドで内訳表示）。

### Maintained
- 文脈抑止（長会話/バースト）、A0 front-end（ヒステリシス＋HOLD）。

---

## [1.68i] - 2026-01-xx
### Added
- **静音モニタ**運用（無イベント時は出力なし）。**EVENTのみ出力**の方針を導入。

### Maintained
- v1.68h+までの見える化・テレメトリ強化を踏襲。

---

## [1.68h+] - 2026-01-xx
### Added
- **テレメトリ強化**（A0raw/D12/ID_OK、`c` コマンドで内訳表示）。

### Changed
- ログレベル／v/s/d/h の **見える化**。

---

## [1.68h] - 2026-01-xx
### Added
- ログレベル、テレメトリ、`v/s/d/h` の **見える化**。

### Maintained
- 文脈抑止（長会話/バースト）、A0 front-end。

---

## [1.68g] - 2026-01-xx
### Added
- **文脈抑止**を導入：
  - **長会話抑止**：受信継続が閾値以上（当初は1500ms）→ その後 **10秒** ID抑止。
  - **バースト抑止**：短受信（500..1500ms）が **10秒窓に2回以上** → その後 **10秒** ID抑止。

---

## [1.68b] - 2026-01-xx
### Added
- **D12 LED** を **A0 busy表示**に連動。

---

## [1.68a] - 2026-01-xx
### Added
- **A0 OR統合**（D6 + A0）を導入。A0は **HOLD** で途切れ吸収（既定 800ms）。

### Maintained
- 短受信判定（500..1500ms）、PTT統一制御、DFPlayer BUSYミラー、テストSW。

---

## [1.67e] - 2025-12-xx
### Added
- **TX終了後3秒**の D6無視（誤トリガ防止）。

### Maintained
- **周期ID**（30分おき、002/003交互、IDLE時のみ）。
- **短受信判定**（500..1500ms）、**直前アイドル**（≥200ms）、**不応期**（≥3000ms）。

### Migration
- v1.67c→v1.67e：**設定変更不要**。安全側ガードの強化のみ。

---

## [1.67c] - 2025-12-xx
### Initial
- **基本機能確立**：
  - **短受信（500..1500ms）**検出で **001.mp3** を送出。
  - **PTT先行/後保持**（初期値：`PTT_PRE_MS=700` / `PTT_POST_MS=500`）。
  - **DFPlayer BUSY**（D7=LOW=再生中）を **D2へ反転ミラー**。
  - **テストSW（D3）**：1/2/3クリック → 001/002/003 再生（1秒窓）。
  - **周期ID**（30分おき、**002/003交互**）を **IDLE時のみ消化**。
  - **不応期**（3000ms）、**直前アイドル**（≥200ms）。

---

## 付録 A：主要パラメータ一覧（v1.69b 時点）
```cpp
// 受信判定
const unsigned long BUSY_MIN_MS = 500;
const unsigned long BUSY_MAX_MS = 2500;  // 1.69b
const unsigned long TM_BUSY_DEBOUNCE_MS = 5;

// 文脈抑止
const unsigned long LONG_TALK_MS          = 2500;  // 1.69b
const unsigned long LONG_TALK_SUPPRESS_MS = 10000;
const unsigned long CHUNK_BURST_WINDOW_MS = 10000;
const unsigned int  CHUNK_BURST_THRESHOLD = 2;     // ※混雑時は3へ
const unsigned long CHUNK_BURST_SUPPRESS_MS=10000; // ※緩和時は8sなど

// 誤トリガ抑止
const unsigned long IDLE_MIN_MS       = 200;
const unsigned long REFRACTORY_MS     = 3000;
const unsigned long BUSY_SUPPRESS_AFTER_TX_MS = 3000;

// PTTタイミング（安全側）
const unsigned long PTT_PRE_MS  = 1000;
const unsigned long PTT_POST_MS = 1000;

// 周期ID
const unsigned long PERIOD_MS   = 30UL * 60UL * 1000UL; // 30分
```

## 付録 B：ピンマップ（Arduino Nano）
- D2：DFPlayer BUSY ミラー（反転：再生中HIGH/停止中LOW）
- D3：テストSW（INPUT_PULLUP、1秒窓のクリック数で 001/002/003）
- D4：統合BUSY LED（D6||A0）
- D5：PTT出力（HIGHで送信。無線機側がGNDショート型ならトランジスタ/フォトカプラで駆動）
- D6：TM BUSY（極性可変：`TM_BUSY_ACTIVE_HIGH`）
- D7：DFPlayer BUSY（LOW=再生中）
- D10/D11：SoftwareSerial（TX/RX）
- D12：A0 LED（A0busyの見える化）
- **D13**：抑止ステータスLED（**1.69b**で追加）
- A0：NJM2072 等の前段出力（ヒステリシス＋HOLDで安定化）

## 付録 C：運用ヒント
- **DEBUG**へ切替（シリアルで `3`）すると、A0/D12/DFP の詳細ログが得られます。
- 周期IDを一時停止したい場合は、`periodicDue && state == IDLE` の条件に `&& false` を加える等、**ビルド時**に明示的に無効化してください（既定では常時有効）。
- 受信の取りこぼしや連発抑止の度合いは、`BUSY_MAX_MS` / `LONG_TALK_MS` / `CHUNK_BURST_*` / `IDLE_MIN_MS` / `REFRACTORY_MS` の **組み合わせ**で調整できます。

---

**最終更新**：2026-01-09 (JST)
