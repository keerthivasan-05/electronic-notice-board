#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#ifndef NOTICE_BOARD_SIMULATION
#include <SoftwareSerial.h>
#endif
#include <string.h>
#include "NoticeStore.h"
#include "Crc8.h"

constexpr uint8_t BT_RX = 10; // HC-05 TX -> UNO D10
constexpr uint8_t BT_TX = 11; // UNO D11 -> divider -> HC-05 RX
constexpr uint32_t PAGE_INTERVAL = 4000;
constexpr uint32_t SCROLL_INTERVAL = 300;
constexpr uint8_t FRAME_SIZE = 108;
#ifdef NOTICE_BOARD_SIMULATION
HardwareSerial& commandPort = Serial;
#else
SoftwareSerial commandPort(BT_RX, BT_TX);
#endif
LiquidCrystal_I2C lcd(0x27, 16, 2);
NoticeStore store;
char line[FRAME_SIZE];
uint8_t lineLength = 0;
bool overflow = false;
bool invalidByte = false;
bool trailingCr = false;
int8_t selected = -1;
uint8_t scrollOffset = 0;
uint32_t switchedAt = 0;
uint32_t scrolledAt = 0;

uint8_t crc8(const char* s) {
  uint8_t crc = 0;
  while (*s) crc = crc8Update(crc, uint8_t(*s++));
  return crc;
}

int8_t hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Preserve empty fields so a missing value cannot shift subsequent arguments.
char* nextField(char*& cursor) {
  if (!cursor) return nullptr;
  char* field = cursor;
  cursor = strchr(field, '|');
  if (cursor) *cursor++ = '\0';
  return field;
}

void reply(uint8_t sequence, bool ok, const char* detail) {
  commandPort.print(ok ? F("ACK|") : F("ERR|"));
  commandPort.print(sequence);
  commandPort.print('|');
  commandPort.println(detail);
}
bool parseUint(const char* s, unsigned long minimum, unsigned long maximum, unsigned long &value) {
  if (!s || !*s) return false;
  unsigned long result = 0;
  for (; *s; ++s) {
    if (*s < '0' || *s > '9') return false;
    uint8_t digit = uint8_t(*s - '0');
    if (digit > maximum || result > (maximum - digit) / 10UL) return false;
    result = result * 10UL + digit;
  }
  if (result < minimum || result > maximum) return false;
  value = result;
  return true;
}
void draw() {
  char title[17];
  if (selected < 0 || !store.at(uint8_t(selected)).used) {
    lcd.setCursor(0, 0); lcd.print(F("NOTICE BOARD    "));
    lcd.setCursor(0, 1); lcd.print(F("No active notice"));
    return;
  }
  const Notice &n = store.at(uint8_t(selected));
  snprintf(title, sizeof(title), "ID:%03u  PRIO:%u", unsigned(n.id), unsigned(n.priority));
  const size_t titleLength = strlen(title);
  lcd.setCursor(0, 0);
  for (uint8_t j = 0; j < 16; ++j) lcd.write(j < titleLength ? title[j] : ' ');
  const uint8_t len = uint8_t(strlen(n.text));
  lcd.setCursor(0, 1);
  const uint8_t cycle = uint8_t(len + 3);
  for (uint8_t i = 0; i < 16; ++i) {
    uint8_t pos = len > 16 ? uint8_t((scrollOffset + i) % cycle) : i;
    lcd.write(pos < len ? n.text[pos] : ' ');
  }
}
void selectNext(uint32_t now) {
  selected = store.nextIndex(selected);
  scrollOffset = 0;
  switchedAt = now;
  scrolledAt = now;
  draw();
}
void processFrame(char* input) {
  // Request: @seq|COMMAND|arg1|...*HH\n where HH = CRC-8/ATM over text after @ and before *.
  if (input[0] != '@') { reply(0, false, "FORMAT"); return; }
  char* asterisk = strrchr(input, '*');
  if (!asterisk || strlen(asterisk + 1) != 2) { reply(0, false, "FORMAT"); return; }
  const int8_t high = hexDigit(asterisk[1]);
  const int8_t low = hexDigit(asterisk[2]);
  if (high < 0 || low < 0) { reply(0, false, "FORMAT"); return; }
  const uint8_t expected = uint8_t(high * 16 + low);
  *asterisk = '\0';
  if (crc8(input + 1) != uint8_t(expected)) { reply(0, false, "CRC"); return; }
  char* ctx = input + 1;
  char* seqStr = nextField(ctx);
  char* command = nextField(ctx);
  unsigned long seq = 0;
  if (!parseUint(seqStr, 0, 255, seq) || !command || !*command) { reply(0, false, "FORMAT"); return; }
  uint8_t idSeq = uint8_t(seq);
  store.expire(millis());
  if (!strcmp(command, "PING")) {
    if (nextField(ctx)) { reply(idSeq, false, "ARG"); return; }
    reply(idSeq, true, "PONG"); return;
  }
  if (!strcmp(command, "STATUS")) {
    if (nextField(ctx)) { reply(idSeq, false, "ARG"); return; }
    char info[20];
    snprintf(info, sizeof(info), "COUNT=%u", unsigned(store.count()));
    reply(idSeq, true, info);
    return;
  }
  if (!strcmp(command, "LIST")) {
    if (nextField(ctx)) { reply(idSeq, false, "ARG"); return; }
    for (uint8_t i = 0; i < NOTICE_CAPACITY; ++i)
      if (store.at(i).used) {
        commandPort.print(F("ITEM|")); commandPort.print(idSeq); commandPort.print('|');
        commandPort.print(store.at(i).id); commandPort.print('|');
        commandPort.println(store.at(i).priority);
      }
    reply(idSeq, true, "END");
    return;
  }
  if (!strcmp(command, "CLEAR")) {
    if (nextField(ctx)) { reply(idSeq, false, "ARG"); return; }
    store.clear(); selected = -1; selectNext(millis());
    reply(idSeq, true, "CLEARED"); return;
  }
  if (!strcmp(command, "DELETE")) {
    char* idField = nextField(ctx);
    unsigned long id = 0;
    if (!parseUint(idField, 1, 255, id) || nextField(ctx)) {
      reply(idSeq, false, "ARG"); return;
    }
    if (!store.remove(uint8_t(id))) { reply(idSeq, false, "NOT_FOUND"); return; }
    selected = -1; selectNext(millis());
    reply(idSeq, true, "DELETED"); return;
  }
  if (!strcmp(command, "ADD")) {
    char* idField = nextField(ctx);
    char* priorityField = nextField(ctx);
    char* ttlField = nextField(ctx);
    char* message = nextField(ctx);
    unsigned long id = 0, priority = 0, ttl = 0;
    if (!parseUint(idField, 1, 255, id) ||
        !parseUint(priorityField, 1, 3, priority) ||
        !parseUint(ttlField, 0, 1440, ttl) ||
        !message || !*message || strlen(message) > TEXT_CAPACITY ||
        nextField(ctx)) { reply(idSeq, false, "ARG"); return; }
    for (char* c = message; *c; ++c)
      if (*c < 32 || *c > 126 || *c == '*') { reply(idSeq, false, "TEXT"); return; }
    if (!store.add(uint8_t(id), uint8_t(priority), uint16_t(ttl), message, millis())) {
      reply(idSeq, false, "FULL"); return;
    }
    selected = -1; selectNext(millis());
    reply(idSeq, true, "SAVED"); return;
  }
  reply(idSeq, false, "UNKNOWN");
}
void pollBluetooth() {
  uint8_t budget = 32; // let the scheduler run even during incoming serial bursts
  while (commandPort.available() && budget--) {
    char ch = char(commandPort.read());
    if (ch == '\n') {
      if (overflow) reply(0, false, "OVERSIZE");
      else if (invalidByte) reply(0, false, "FORMAT");
      else {
        line[lineLength] = '\0';
        processFrame(line);
      }
      lineLength = 0;
      overflow = invalidByte = trailingCr = false;
    } else if (trailingCr) {
      invalidByte = true;
    } else if (ch == '\r') {
      trailingCr = true;
    } else if (!overflow) {
      if (uint8_t(ch) < 32 || uint8_t(ch) > 126) invalidByte = true;
      if (lineLength < FRAME_SIZE - 1) line[lineLength++] = ch;
      else overflow = true;
    }
  }
}
void setup() {
  Serial.begin(9600);
#ifndef NOTICE_BOARD_SIMULATION
  commandPort.begin(9600);
#endif
  lcd.init(); lcd.backlight();
  store.begin();
  selectNext(millis());
  Serial.println(F("Smart notice board ready"));
}
void loop() {
  pollBluetooth();
  uint32_t now = millis();
  store.expire(now);
  if (selected >= 0 && !store.at(uint8_t(selected)).used) selectNext(now);
  if (selected >= 0 && now - switchedAt >= PAGE_INTERVAL) selectNext(now);
  if (selected >= 0) {
    const Notice& n = store.at(uint8_t(selected));
    if (strlen(n.text) > 16 && now - scrolledAt >= SCROLL_INTERVAL) {
      scrolledAt = now;
      scrollOffset = uint8_t((scrollOffset + 1) % (strlen(n.text) + 3));
      draw();
    }
  }
}
