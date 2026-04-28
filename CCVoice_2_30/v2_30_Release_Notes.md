# OpenCCVoice v2.30 リリースノート＆マイグレーションガイド
# 【v1.80i コア設計搭載・v2.24f RTC・ログ統合版】

---

## 概要

**v2.30** は、**v1.80i（メジャーバージョン）の改善ロジックを完全搭載**し、v2.24f（RTC・ログ機能）と統合したバージョンです。

### v1.80i が確立した3つの革新（v2.30で継承）

1. **絶対時刻比較**
   - pttPreEndAt / pttPostEndAt で時刻を記録
   - (long)(now - pttPreEndAt) >= 0 で判定
   - Serial.read() ブロッキングで now が50msずれても確実

2. **抑止起点統一**
   - 起点A：POST終了時 + BUSY OFF
   - 起点B：通常会話終了時（受信OFF）
   - 単一の busySupUntil タイマーで管理

3. **ブロッキング対策**
   - Serial.setTimeout(50) を setup() で一度設定
   - parseInt() の最大ブロッキングを50msに制限

### v2.30 の主な改善

- **PTT制御**：相対時間 → **絶対時刻比較**（v1.80h→v1.80i確立→v2.30継承）
- **抑止ロジック**：二重セット削除、起点統一（v1.80i確立→v2.30継承）
- **コマンド体系**：j/J/i/s/t削除、ログレベル l0/l1/l2/l3/l4（v1.80i基盤・v2.30完成）
- **EEPROM ver**：5(v1.80i) → **7**(v2.30)（フィールド削除対応）

**ピンマップ・RTC・ログ・AUTO機能**は v2.24f から継続。

---

## v2.30 の主要変更

### 1. PTT制御：絶対時刻比較への改善（v1.80h/v1.80i確立・v2.30継承）

**v1.80h で導入された改善**：
- Serial.setTimeout(50) で parseInt() ブロッキング上限を50msに制限
- 絶対時刻比較（pttPreEndAt / pttPostEndAt）による確実な PRE/POST 保証

**v1.80i で完全確立**：
- 上記改善を整理・デッドコード削除
- CONFIG_VERSION = 5 で確定

**v2.30 で継承**：
- v1.80i のロジックを完全踏襲
- 設定コマンド j/J 削除による簡素化
```c
case PTT_ON_WAIT:
  if (now - stateTimer >= PTT_PRE_MS) {
    // 問題: Serial.parseInt() などのブロッキングで now がずれる可能性
    state = PLAYING;
  }
```

#### v1.80h/v1.80i（絶対時刻比較・改善）
```c
void startPtt(uint16_t trk) {
  unsigned long now = millis();
  setPtt(true);
  pttPreEndAt = now + PTT_PRE_MS;    // ★v1.80hで導入・v1.80iで確立
  state = PTT_ON_WAIT;
}

case PTT_ON_WAIT:
  if ((long)(now - pttPreEndAt) >= 0) {
    // ★v1.80iで確定された仕様
    dfpSend(0x03, requestedTrack);
    state = PLAYING;
  }
```

#### v2.30（v1.80i ロジック継承・j/J コマンド削除）
```c
// v1.80i の絶対時刻比較ロジックをそのまま使用
// PTT_PRE_MS = 1000ms （内部固定、j/J コマンド廃止）
case PTT_ON_WAIT:
  if ((long)(now - pttPreEndAt) >= 0) {
    dfpSend(0x03, requestedTrack);
    state = PLAYING;
  }
```

**効果**（v1.80h で導入・v1.80i で確立・v2.30 で継承）：
- Serial.read() / Serial.parseInt() で実行時間がずれても、PRE/POST期間が確実に保証される
- Serial.setTimeout(50) を setup() で一度だけ設定し、ブロッキング上限を制限
- v1.80i で設計が確定。v2.30 では j/J コマンド削除で設定を簡素化

---

### 2. 抑止ロジック：起点統一（v1.80i確立・v2.30継承）

**v1.80i で確立された仕様**：
- 起点A：POST終了時 + BUSY OFF → busySupUntil をセット
- 起点B：通常会話終了時（② MAX超え受信） → busySupUntil をセット
- v1.80i で「カーチャンク検出時の二重セット」を削除
- 設計意図を明確化：「抑止セットは POST終了（起点A）に一本化」

**v2.30 で継承**：
- v1.80i のロジックをそのまま使用
- マイグレーション対応強化（ver=5 → ver=7）

#### v1.80i で削除された二重セット問題

**旧仕様（v1.73系）での問題**：
```c
processBusyLogic():
  if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
    // カーチャンク検出時に busySupUntil セット
    busySupUntil = now + TX_SUP_MS;  // ★1つ目のセット
    startPtt(1);
  }

processStateMachine() PTT_OFF_WAIT:
  if (POST終了) {
    // POST終了時にも busySupUntil セット
    busySupUntil = now + TX_SUP_MS;  // ★2つ目のセット（二重）
    state = IDLE;
  }
```

**問題（v1.73系で発生）**：
- カーチャンク後 → POST終了時で抑止がリセットされ、設計意図が不明確
- マイグレーション時に動作不一致が生じる可能性

#### v1.80i（一本化・確立）
```c
processBusyLogic():
  if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
    // カーチャンク検出時は busySupUntil を セット しない
    // ★v1.80i で二重セット削除
    if (!pAct && !isSuppressedNow(now)) {
      startPtt(1);
    }
  } else if (dur >= BUSY_MAX_MS) {
    // ② MAX超え → 通常会話終了 → 起点B
    busySupUntil = now + TX_SUP_MS;
  }

processStateMachine() PTT_OFF_WAIT:
  if (POST終了 && BUSY OFF) {
    // 起点A：POST終了 + BUSY OFF
    busySupUntil = now + TX_SUP_MS;  // ★v1.80i で確定
    state = IDLE;
  }
```

**v1.80i での設計意図**：
- 「抑止セットは POST終了（起点A）に一本化する」
- カーチャンク検出時は抑止セット不要（POST終了時に処理）
- これにより「PTT送出完了後から ⑤抑止時間」という仕様を正確に実現

#### v2.30（v1.80i ロジック継承）
```c
// v1.80i の実装をそのまま使用
// v1.80i で確立されたロジックに変更なし
processBusyLogic():
  // カーチャンク検出時は busySupUntil セットなし
  if (!pAct && !isSuppressedNow(now)) {
    startPtt(1);
  } else if (dur >= BUSY_MAX_MS) {
    // ② MAX超え → 通常会話終了 → 起点B
    busySupUntil = now + TX_SUP_MS;
  }

processStateMachine() PTT_OFF_WAIT:
  if (!rBnow) {
    // 起点A：POST終了 + BUSY OFF
    busySupUntil = now + TX_SUP_MS;
  }
```
    // ★カーチャンク検出時は busySupUntil セットなし
    if (!isSuppressedNow(now)) {
      startPtt(1);
    }
  }
  if (dur >= BUSY_MAX_MS) {
    // ★通常会話終了時に抑止（起点B）
    busySupUntil = now + TX_SUP_MS;
  }

processStateMachine() PTT_OFF_WAIT:
  if (POST終了) {
    bool rBnow = readBusy();
    if (!rBnow) {
      // ★POST終了時、受信なし（起点A）
      busySupUntil = now + TX_SUP_MS;
    } else {
      // ★受信中なら起点B で処理待ち
    }
    state = IDLE;
  }
```

**改善**：
- **起点A**：POST終了時 + BUSY OFF
- **起点B**：通常会話終了時（受信OFF）
- 両方で統一された busySupUntil セット
- 抑止ロジックが明確・一元化

---

### 3. コマンド体系の整理（v1.80i準拠）

#### 削除されたコマンド

| コマンド | 理由 | 補足 |
|---------|------|------|
| `i####` | IDLE_MIN_MS 内部固定化 | 内部で常に 200ms |
| `s0/s1` | 抑止ロジック統合 | suppressOn フィールド削除 |
| `t0/t1` | 抑止ロジック統合 | txAfSupOn フィールド削除 |
| `j####` | PTT_PRE_MS 固定化 | 常に 1000ms |
| `J####` | PTT_POST_MS 固定化 | 常に 1000ms |
| `0/1/2/3` | ログレベル形式統一 | → `l0/l1/l2/l3` へ |

#### ログレベルコマンド統一（v1.80i準拠）

v2.24f：
```
0 = LOG_OFF
1 = LOG_INF
2 = LOG_DBG
3 = （未定義）
```

v2.30：
```
l0 = LOG_OFF  : 出力なし
l1 = LOG_ERR  : エラーのみ
l2 = LOG_MIN  : 必要最低限（TX/抑止/エラー）← 起動時デフォルト
l3 = LOG_FULL : 標準ログ（PTT ON/OFF・受信長等）
l4 = LOG_DBG  : デバッグ詳細
```

**起動時**: `LOG_LEVEL = LOG_MIN`（必要最低限）
**EEPROM保存**: なし（起動時に常に LOG_MIN リセット）

---

### 4. EEPROM CONFIG_VERSION: 6 → 7

#### v2.24f (ver=6)
```c
struct MyConfig {
  // ... 基本フィールド
  uint32_t pttPreMs;         // ★削除
  uint32_t pttPostMs;        // ★削除
  uint8_t suppressOn;        // ★削除
  uint8_t txAfSupOn;         // ★削除
  uint32_t idleMin;          // ★削除
  uint8_t rtcAlignOn;
  uint32_t saveTimestamp;
  uint8_t ver;
};
```

#### v2.30 (ver=7)
```c
struct MyConfig {
  // ... 基本フィールド
  uint32_t txSupMs;          // r#### パラメータ
  uint32_t periodMin;        // p## パラメータ
  int a0Low, a0High;         // L###, G###
  uint32_t a0Hold;           // a####
  uint32_t autoWinMin;       // w##
  uint32_t dfpTimeoutMs;     // d####
  uint8_t tmBusyActiveHigh;  // g0/g1
  uint32_t periodQuietMs;    // k####
  uint8_t rtcAlignOn;        // u0/u1
  uint32_t saveTimestamp;    // タイムスタンプ
  uint8_t ver;               // ★末尾固定
};
```

**サイズの目安**：
- v2.24f: 約 48 バイト
- v2.30: 約 40 バイト（フィールド削除）

#### マイグレーション処理

```c
void migrateOrInit() {
  // ver=6 → ver=7 自動マイグレーション
  
  if (config.ver == 6) {
    // 削除フィールドを無視し、新 ver=7 構造で再構築
    config.ver = 7;
    // pttPreMs, pttPostMs, suppressOn, txAfSupOn, idleMin は新コードでは不要
  }
}
```

**注意**：v1.80i (ver=5) との互換性はなし（分岐設計のため）

---

## 動作仕様の詳細変更

### 1. カーチャンク→PTT→抑止 のタイミング

#### フロー図

```
[D11/A0 BUSY ON]
  ↓ (500-3900ms 継続)
[BUSY OFF]
  ↓
① BUSY OFF から dur 計算
  ├─ dur < 500ms     → ノイズ（スキップ）
  ├─ 500-3900ms      → カーチャンク
  │   ├─ !pAct && !suppress && refrac解除
  │   │ → startPtt(1)
  │   │   PTT ON → PTT_ON_WAIT (PRE 1000ms)
  │   │   → PLAYING (DFPlayer 再生)
  │   │   → PTT_OFF_WAIT (POST 1000ms)
  │   │   → IDLE
  │   │     ↓
  │   │   POST終了 + BUSY OFF（起点A）
  │   │   → busySupUntil = now + 3000ms
  │   │   → isSuppressedNow() = true (3秒間)
  │   │
  │   └─ 抑止中/pAct中 → スキップ
  │       （busySupUntil はセットしない！）
  │
  └─ dur ≥ 3900ms    → 通常会話
      ├─ 受信継続 → 抑止なし
      │
      └─ BUSY OFF（受信終了・起点B）
          → busySupUntil = now + 3000ms
```

#### v2.24f との主な違い

v2.24f：カーチャンク検出時に busySupUntil をセット
```c
if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
  busySupUntil = now + TX_SUP_MS;  // ★セット
  startPtt(1);
}
```

v2.30：カーチャンク検出時にセットしない（起点A/B へ一本化）
```c
if (dur >= BUSY_MIN_MS && dur < BUSY_MAX_MS) {
  // busySupUntil セットなし
  startPtt(1);
  // POST終了時の起点A で統一
}
```

### 2. 相対時間 vs 絶対時刻

#### v2.24f（相対時間）
```c
unsigned long stateTimer;

case PTT_ON_WAIT:
  if (now - stateTimer >= PTT_PRE_MS) {
    // 問題: stateTimer の記録から経過時間で判定
    // ブロッキングで now のズレ → PRE時間が短縮される可能性
  }
```

#### v2.30（絶対時刻）
```c
unsigned long pttPreEndAt;

void startPtt() {
  pttPreEndAt = now + PTT_PRE_MS;
}

case PTT_ON_WAIT:
  if ((long)(now - pttPreEndAt) >= 0) {
    // 絶対時刻で比較 → 確実に保証
    // ブロッキングで now がズレても、予定時刻に到達したら遷移
  }
```

**実例**：

```
[t=1000ms] startPtt() → pttPreEndAt = 1000 + 1000 = 2000
[t=1050ms] Serial.parseInt() ブロッキング開始（50ms）
[t=1100ms] Serial.parseInt() 終了、now = 1100
           判定: (1100 - 2000) >= 0 ? NO（-900） → PTT_ON_WAIT継続
[t=2000ms] now = 2000
           判定: (2000 - 2000) >= 0 ? YES → PLAYING へ遷移

→ Serial.parseInt() のブロッキング 50ms は無視され、
  pttPreEndAt = 2000ms で確実に遷移
```

### 3. ログレベル挙動

#### v2.24f
```
起動時: LOG_LEVEL = LOG_INF（全メッセージ）
コマンド: 0/1/2/3（形式が短い）
```

#### v2.30
```
起動時: LOG_LEVEL = LOG_MIN（軽量・必要最低限）
コマンド: l0/l1/l2/l3（形式を明確化）
保存: EEPROM に保存しない（起動時常にリセット）

ログレベル別:
  LOG_ERR (1)  : エラーのみ      → [ERR]ラベル
  LOG_MIN (2)  : 最小限          → [PTT], [SUP], [RX], [TX], [EVT]
  LOG_FULL (3) : 標準ログ        → 上記 + [SKIP], [Resume], [AUTO-FIXED]
  LOG_DBG (4)  : デバッグ        → 上記 + [RTC], [Periodic], [POST-TX]
```

---

## マイグレーション（v2.24f → v2.30）

### 自動マイグレーション実施

```c
void migrateOrInit() {
  // 起動時に自動実行
  
  if (config.ver == 6) {
    // v2.24f の設定を v2.30 へ変換
    
    // ★削除フィールドの処理
    // - pttPreMs / pttPostMs → 無視（常に 1000ms 固定）
    // - suppressOn / txAfSupOn → 無視（抑止ロジック統合）
    // - idleMin → 無視（常に 200ms 固定）
    
    // ★保持フィールド
    // - busySrc, busyMin, busyMax, txSupMs, periodMin
    // - a0Low, a0High, a0Hold, autoWinMin, dfpTimeoutMs
    // - tmBusyActiveHigh, periodQuietMs, rtcAlignOn, saveTimestamp
    
    config.ver = 7;
    EEPROM.put(0, config);
  }
}
```

### 設定の動作への影響

#### v2.24f の suppressOn=1, txAfSupOn=1 の場合

v2.30 では以下に統合：
- **起点A** で POST終了時に busySupUntil をセット（常に有効）
- **起点B** で通常会話終了時に busySupUntil をセット（常に有効）

つまり、v2.30 では常に抑止が有効。suppressOn/txAfSupOn の概念は不要。

#### PTT_PRE_MS / PTT_POST_MS の扱い

v2.30 では常に 1000ms 固定。v2.24f で j/J コマンドで設定していた値は無視される。

---

## テスト項目（マイグレーション後の確認）

### 1. カーチャンク応答

```
[テスト]
1. D11 を 700ms ON（MIN=500, MAX=3900）
2. カーチャンク応答が発生？ → YES（正常）
3. 再度カーチャンク（500ms以内）→ スキップ？ → YES（抑止期間 3秒）
4. 3秒経過後、カーチャンク → 応答？ → YES（抑止解除）
```

### 2. 通常会話検出

```
[テスト]
1. D11 を 5秒 ON（> MAX=3900）
2. オーディオレスポンス無し？ → YES（通常会話として扱われる）
3. D11 OFF → 抑止開始？ → YES（3秒間）
4. 抑止期間中のカーチャンク → スキップ？ → YES
```

### 3. ログレベル変更

```
[シリアル]
l1 → [LOG] Level set to MIN
l2 → [LOG] Level set to FULL
起動時デフォルト → [LOG] MIN
```

### 4. RTC・ログ機能（v2.24f から継続）

```
[テスト]
1. 周期ID は正時スロット同期？ → YES（u1=ON時）
2. イベントログ記録？ → YES（v コマンド確認）
3. T コマンドで時刻設定？ → YES
4. AT24C32 なしで動作？ → YES（millis 相対へフォールバック）
```

---

## v2.30 のメリット・デメリット

### メリット

✅ **PRE/POST 期間の確実性**
- Serial.read() などのブロッキングに強い
- 無線機の DFPlayer 制御がより正確

✅ **抑止ロジックの明確化**
- 二重セット削除で設計意図が明確
- マイグレーション時の動作保証が容易

✅ **コマンド体系の整理**
- j/J, i, s, t 削除で操作性向上（不要な設定項目減少）
- ログレベル形式統一（l0/l1/l2/l3）

✅ **EEPROM 節約**
- ver=7 で不要フィールド削除、ストレージ効率化

### デメリット

❌ **v2.24f との後方互換性なし**
- ver=6 → ver=7 は自動マイグレーションされるが、ver=5 (v1.80i) との直接互換なし
- PTT_PRE_MS / PTT_POST_MS の可変性廃止（常に 1000ms）

❌ **設定項目の削除**
- j/J コマンドで PRE/POST を調整していた場合、再設定不要（常に 1000ms）
- suppressOn/txAfSupOn フラグでのきめ細い制御は不可（常に有効）

---

## v2.30 導入チェックリスト

- [ ] v2.24f からのマイグレーション確認
- [ ] EEPROM ver=6 → ver=7 への自動変換確認
- [ ] Serial.setTimeout(50) が setup() で設定されている
- [ ] pttPreEndAt / pttPostEndAt の絶対時刻比較が動作
- [ ] カーチャンク検出後に POST終了時に busySupUntil がセット（二重セット なし）
- [ ] ログレベル l0/l1/l2/l3 が正常に機能
- [ ] RTC アライン・ログ機能が v2.24f 相当で動作
- [ ] テストSW・AUTO固定機能が継続動作
- [ ] LED 表示（D4/D6/D7）が正常に機能

---

## 参考資料

- **v1.80i の改善点**：「v1_80i_vs_v1_81_ja9hym_comparison.md」
- **タイミング詳細**：「v1_81_ja9hym_detailed_timing_reference.md」
- **ピンマップ**：コードの先頭コメント（Ver.5 ピンマップ）

---

## サポート

問題が発生した場合：
1. ログレベルを l3（FULL） または l4（DBG） に上げて、詳細なログを確認
2. EEPROM を初期化（F コマンド）して動作確認
3. コンパイルエラーが発生した場合、ccvoice_config.h / ccvoice_log.h の include を確認
