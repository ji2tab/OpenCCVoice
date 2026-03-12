#pragma once
// Config migration and apply for OpenCCVoice
// Separated to avoid Arduino IDE auto-prototype ordering issues.

// 設定構造体のマイグレーション（旧バージョン補完）
void migrateConfig(MyConfig &c) {
  if (c.ver < 5) { c.rtcAlignOn = 1; }
  if (c.ver < 6) { c.saveTimestamp = 0; }
  if (!(c.tmBusyActiveHigh == 0 || c.tmBusyActiveHigh == 1)) c.tmBusyActiveHigh = 1;
  if (c.dfpTimeoutMs > 600000UL)                              c.dfpTimeoutMs = 20000UL;
  if (c.periodQuietMs == 0 || c.periodQuietMs > 600000UL)    c.periodQuietMs = 2000UL;
  c.ver = CONFIG_VERSION;
}

// ランタイム変数に config を適用
void applyConfig() {
  BUSY_INPUT_SOURCE         = (BusySrc)config.busySrc;
  SUPPRESSORS_ENABLED       = (config.suppressOn == 1);
  TX_AFTER_SUPPRESS_ENABLED = (config.txAfSupOn  == 1);
  BUSY_MIN_MS               = config.busyMin;
  BUSY_MAX_MS               = config.busyMax;
  IDLE_MIN_MS               = config.idleMin;
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
}
