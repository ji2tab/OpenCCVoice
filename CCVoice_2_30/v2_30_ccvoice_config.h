#pragma once
// Config migration and apply for OpenCCVoice v2.30
// Separated to avoid Arduino IDE auto-prototype ordering issues.

// 設定構造体のマイグレーション（旧バージョン補完）
inline void migrateConfig(MyConfig &c) {
  // v2.30: ver=5 (v1.80i) / ver=6 (v2.24f) からの自動マイグレーション

  // ver < 5 の場合のサポート（v2.01, v1.73e等）
  if (c.ver < 5) { 
    c.rtcAlignOn = 1; 
  }

  // ver < 6 の場合のサポート（v2.10等）
  if (c.ver < 6) { 
    c.saveTimestamp = 0; 
  }

  // v2.30: ver=5 / ver=6 からのマイグレーション
  // 削除フィールド (v1.80i/v2.24f では存在) の処理は不要
  // ─ suppressOn, txAfSupOn, idleMin, pttPreMs, pttPostMs は
  //   v2.30 では内部固定値のため EEPROM に保存しない

  // ハードウェア設定の安全性チェック
  if (!(c.tmBusyActiveHigh == 0 || c.tmBusyActiveHigh == 1)) {
    c.tmBusyActiveHigh = 1;
  }
  
  if (c.dfpTimeoutMs > 600000UL) {
    c.dfpTimeoutMs = 20000UL;
  }
  
  if (c.periodQuietMs == 0 || c.periodQuietMs > 600000UL) {
    c.periodQuietMs = 2000UL;
  }

  // バージョンを現在の CONFIG_VERSION に更新
  c.ver = CONFIG_VERSION;
}

// ランタイム変数に config を適用
// v2.30: 削除フィールド (suppressOn, txAfSupOn, idleMin) は参照しない
inline void applyConfig() {
  BUSY_INPUT_SOURCE         = (BusySrc)config.busySrc;
  BUSY_MIN_MS               = config.busyMin;
  BUSY_MAX_MS               = config.busyMax;
  PERIOD_MS                 = (unsigned long)config.periodMin * 60000UL;
  TX_SUP_MS                 = config.txSupMs;
  A0_LOW_TH                 = config.a0Low;
  A0_HIGH_TH                = config.a0High;
  A0_HOLD                   = config.a0Hold;
  AUTO_WINDOW               = (unsigned long)config.autoWinMin * 60000UL;
  LONG_TALK_MS              = BUSY_MAX_MS;
  DFP_TIMEOUT_MS            = config.dfpTimeoutMs;
  TMBUSY_ACTIVE_HIGH        = (config.tmBusyActiveHigh == 1);
  PERIOD_QUIET_MS           = config.periodQuietMs;
  RTC_ALIGN_ON              = (config.rtcAlignOn == 1);
  autoLocked                = (BUSY_INPUT_SOURCE != BUSY_SRC_AUTO);

  // v2.30 内部固定値（ユーザー設定値は使用しない）
  // ─ PTT_PRE_MS = 1000 (固定、j#### コマンド廃止)
  // ─ PTT_POST_MS = 1000 (固定、J#### コマンド廃止)
  // ─ IDLE_MIN_MS = 200 (固定、i#### コマンド廃止)
  // ─ 抑止は常時有効（s0/s1, t0/t1 コマンド廃止）
}

