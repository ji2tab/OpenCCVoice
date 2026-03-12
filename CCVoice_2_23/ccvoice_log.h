#pragma once
// Event Log module for OpenCCVoice
// Separated to avoid Arduino IDE auto-prototype ordering issues.

/* ============================== Event Log =========================
 * AT24C32 レイアウト:
 *   0x0000〜0x003F : config構造体（64バイト境界）
 *   0x0040〜0x0047 : ログヘッダ（8バイト）
 *   0x0048〜0x0FFF : ログエントリ × 最大448件（9バイト/件）
 *
 * ログヘッダ（8バイト）:
 *   uint32_t magic    : 0xLOG1（初期化済み判定）
 *   uint16_t count    : 総記録件数（448超でラップ）
 *   uint16_t head     : 次書き込みインデックス（0〜447）
 *
 * ログエントリ（9バイト）:
 *   uint32_t ts       : タイムスタンプ（DS3231 秒換算、未取得時=0）
 *   uint8_t  evt      : イベント種別
 *   uint32_t data     : 付加データ
 *
 * イベント種別:
 *   LOG_EVT_BOT=0x01  : 起動（data=EEPROM ver）
 *   LOG_EVT_PER=0x02  : 周期ID送出（data=Track番号）
 *   LOG_EVT_CAR=0x03  : カーチャンク検知（data=BUSY時間ms）
 *   LOG_EVT_SUP=0x04  : 長話抑止（data=BUSY時間ms）
 *   LOG_EVT_BST=0x05  : バースト抑止（data=バースト回数）
 * ================================================================= */

#define LOG_MAGIC       0x4C4F4731UL  // "LOG1"
#define LOG_HDR_ADDR    0x0048U
#define LOG_DAT_ADDR    0x0050U
#define LOG_MAX_ENTRIES 448U
#define LOG_ENTRY_SIZE  9U

#define LOG_EVT_BOT  0x01
#define LOG_EVT_PER  0x02
#define LOG_EVT_CAR  0x03
#define LOG_EVT_SUP  0x04
#define LOG_EVT_BST  0x05

struct LogHeader {
  uint32_t magic;
  uint16_t count;
  uint16_t head;
};

struct LogEntry {
  uint32_t ts;
  uint8_t  evt;
  uint32_t data;
};
// ログヘッダ読み込み
bool logReadHeader(LogHeader &h) {
  extEepromRead(LOG_HDR_ADDR, (uint8_t*)&h, sizeof(h));
  return (h.magic == LOG_MAGIC);
}

// ログヘッダ書き込み
void logWriteHeader(const LogHeader &h) {
  extEepromWrite(LOG_HDR_ADDR, (const uint8_t*)&h, sizeof(h));
}

// ログ初期化（ヘッダ未設定時）
void logInit() {
  LogHeader h;
  if (logReadHeader(h)) return;  // 既に初期化済み
  h.magic = LOG_MAGIC;
  h.count = 0;
  h.head  = 0;
  logWriteHeader(h);
  Serial.println(F("[LOG] Initialized."));
}

// エントリ1件書き込み
void logWrite(uint8_t evt, uint32_t data) {
  if (!extEepromAvailable) return;
  LogHeader h;
  if (!logReadHeader(h)) { logInit(); logReadHeader(h); }

  LogEntry e;
  e.ts   = getCurrentTimestamp();
  e.evt  = evt;
  e.data = data;

  uint16_t addr = LOG_DAT_ADDR + (uint16_t)h.head * LOG_ENTRY_SIZE;
  extEepromWrite(addr, (const uint8_t*)&e, sizeof(e));

  h.head = (h.head + 1) % LOG_MAX_ENTRIES;
  if (h.count < LOG_MAX_ENTRIES) h.count++;
  logWriteHeader(h);
}

// 時刻文字列出力ヘルパー
void printLogTime(uint32_t ts) {
  if (!rtcAvailable || ts == 0) {
    Serial.print(F("--/--/-- --:--:--"));
    return;
  }
  // タイムスタンプは秒換算のみのため、日付はDS3231から別途取得
  RtcTime t;
  if (!rtcRead(t)) { Serial.print(F("--/--/-- --:--:--")); return; }
  // 現在時刻の秒換算と比較してオフセットを補正（簡易）
  uint32_t nowSec = (uint32_t)t.hour*3600UL + (uint32_t)t.min*60UL + t.sec;
  // 日付込みの厳密な復元は不要: 記録時刻を時分秒で表示
  uint8_t h2 = (uint8_t)(ts / 3600UL % 24);
  uint8_t m2 = (uint8_t)(ts % 3600UL / 60);
  uint8_t s2 = (uint8_t)(ts % 60);
  // 日付はDS3231の現在値を流用（同日ログ前提）
  Serial.print(t.year); Serial.print('/');
  if (t.month < 10) Serial.print('0'); Serial.print(t.month); Serial.print('/');
  if (t.day   < 10) Serial.print('0'); Serial.print(t.day);   Serial.print(' ');
  if (h2 < 10) Serial.print('0'); Serial.print(h2); Serial.print(':');
  if (m2 < 10) Serial.print('0'); Serial.print(m2); Serial.print(':');
  if (s2 < 10) Serial.print('0'); Serial.print(s2);
  (void)nowSec;
}

// ログ表示（直近 LOG_SHOW_COUNT 件）
#define LOG_SHOW_COUNT 20
void logPrint() {
  if (!extEepromAvailable) {
    Serial.println(F("[LOG] AT24C32 not connected."));
    return;
  }
  LogHeader h;
  if (!logReadHeader(h) || h.count == 0) {
    Serial.println(F("[LOG] No entries."));
    return;
  }

  uint16_t show  = (h.count < LOG_SHOW_COUNT) ? h.count : LOG_SHOW_COUNT;
  // 古い順に表示するため、先頭インデックスを計算
  uint16_t start;
  if (h.count <= LOG_MAX_ENTRIES) {
    // まだラップしていない
    start = (h.head >= show) ? (h.head - show) : 0;
  } else {
    start = (h.head + LOG_MAX_ENTRIES - show) % LOG_MAX_ENTRIES;
  }

  Serial.print(F("---- EVENT LOG ("));
  Serial.print(h.count); Serial.print('/');
  Serial.print(LOG_MAX_ENTRIES);
  Serial.println(F(") ----"));
  Serial.println(F("No  Time                Event  Data"));

  for (uint16_t i = 0; i < show; i++) {
    uint16_t idx  = (start + i) % LOG_MAX_ENTRIES;
    uint16_t addr = LOG_DAT_ADDR + idx * LOG_ENTRY_SIZE;
    LogEntry e;
    extEepromRead(addr, (uint8_t*)&e, sizeof(e));

    // No
    uint16_t no = (h.count > LOG_MAX_ENTRIES)
                ? (h.count - show + i + 1)
                : (i + 1);
    if (no < 100) Serial.print('0');
    if (no < 10)  Serial.print('0');
    Serial.print(no); Serial.print(' ');

    // Time
    printLogTime(e.ts); Serial.print(' ');

    // Event + Data
    switch (e.evt) {
      case LOG_EVT_BOT:
        Serial.print(F("BOT    ver="));   Serial.println(e.data); break;
      case LOG_EVT_PER:
        Serial.print(F("PER    Track=")); Serial.println(e.data); break;
      case LOG_EVT_CAR:
        Serial.print(F("CAR    BUSY="));  Serial.print(e.data); Serial.println(F("ms")); break;
      case LOG_EVT_SUP:
        Serial.print(F("SUP    BUSY="));  Serial.print(e.data); Serial.println(F("ms")); break;
      case LOG_EVT_BST:
        Serial.print(F("BST    count=")); Serial.println(e.data); break;
      default:
        Serial.print(F("???    "));       Serial.println(e.data); break;
    }
  }
  Serial.println(F("---- END LOG ----"));
}

// ログ消去
void logClear() {
  if (!extEepromAvailable) {
    Serial.println(F("[LOG] AT24C32 not connected."));
    return;
  }
  LogHeader h;
  h.magic = LOG_MAGIC;
  h.count = 0;
  h.head  = 0;
  logWriteHeader(h);
  Serial.println(F("[LOG] Cleared."));
}
