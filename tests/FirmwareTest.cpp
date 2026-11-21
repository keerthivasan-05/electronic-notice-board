#include <EEPROM.h>
#include <cassert>
#include <iostream>
#include "../firmware/SmartNoticeBoard/SmartNoticeBoard.ino"

FakeEEPROM EEPROM;
SerialPort Serial;
uint32_t clockMillis = 0;

static void testExpiryAndPriority() {
  NoticeStore notices;
  assert(notices.add(1, 1, 1, "expires at zero", uint32_t(0) - 60000));
  assert(notices.add(2, 3, 1, "urgent", 0));
  assert(notices.add(3, 3, 1, "also urgent", 0));
  assert(notices.at(notices.nextIndex(-1)).id == 2);
  assert(notices.at(notices.nextIndex(1)).id == 3);
  notices.expire(UINT32_MAX);
  assert(notices.contains(1));
  notices.expire(0);
  assert(!notices.contains(1));
  notices.expire(60000);
  assert(notices.count() == 0);
}

static void testPersistence() {
  EEPROM = FakeEEPROM{};
  // Legacy v1 record for notice 7 with a one-character message.
  EEPROM.bytes[0] = 0xB7;
  EEPROM.bytes[1] = 1;
  EEPROM.bytes[2] = 1;
  EEPROM.bytes[3] = 7;
  EEPROM.bytes[4] = 2;
  EEPROM.bytes[5] = 1;
  EEPROM.bytes[6] = 'A';
  uint8_t crc = 0;
  for (uint16_t i = 0; i < 70; ++i) crc = crc8Update(crc, EEPROM.read(i));
  EEPROM.bytes[70] = crc;

  // Interrupt each changed-byte write during migration and then during a regular commit.
  for (int phase = 0; phase < 2; ++phase) {
    const FakeEEPROM baseline = EEPROM;
    const uint8_t newId = uint8_t(8 + phase);
    bool committed = false;
    for (int limit = 0; limit < 341; ++limit) {
      EEPROM = baseline;
      NoticeStore writer;
      writer.begin();
      assert(writer.contains(7));
      EEPROM.writesBeforeFailure = limit;
      try {
        assert(writer.add(newId, 3, 0, "permanent", 0));
        committed = true;
      } catch (const PowerLoss&) {}
      NoticeStore reader;
      reader.begin();
      assert(reader.count() == 1 + phase + (committed ? 1 : 0));
      assert(reader.contains(7));
      if (phase == 1) assert(reader.contains(8));
      assert(reader.contains(newId) == committed);
      if (committed) break;
    }
    assert(committed);
    EEPROM.writesBeforeFailure = -1;
  }

  const FakeEEPROM validBanks = EEPROM;
  EEPROM.bytes[4] ^= 1; // Corrupt the newest bank's first record.
  NoticeStore fallback;
  fallback.begin();
  assert(fallback.count() == 2 && fallback.contains(8) && !fallback.contains(9));
  EEPROM = validBanks;

  NoticeStore writer;
  writer.begin();
  EEPROM.writesBeforeFailure = 0;
  assert(writer.add(7, 2, 0, "A", 0)); // An identical snapshot writes no bytes.
  assert(writer.add(10, 2, 1, "RAM only", 0));
  EEPROM.writesBeforeFailure = -1;
  for (unsigned i = 0; i < 260; ++i) {
    assert(writer.add(7, 2, 0, i % 2 == 0 ? "even" : "odd", 0));
  }
  NoticeStore afterWrap;
  afterWrap.begin();
  assert(strcmp(afterWrap.at(0).text, "odd") == 0);
  assert(writer.add(7, 2, 1, "now temporary", 0));
  afterWrap.begin();
  assert(!afterWrap.contains(7));
  writer.clear();
  afterWrap.begin();
  assert(afterWrap.count() == 0);
}

static void expectReply(const std::string& request, const std::string& expected) {
  bluetooth.output.clear();
  for (unsigned char byte : request) bluetooth.input.push(byte);
  while (bluetooth.available()) loop();
  assert(bluetooth.output == expected + "\r\n");
}

static std::string request(const char* payload, const char* ending = "\n") {
  char checksum[4];
  snprintf(checksum, sizeof(checksum), "*%02X", unsigned(crc8(payload)));
  return std::string("@") + payload + checksum + ending;
}

static void testWireFraming() {
  setup();
  expectReply("@7|PING*6D\r\n", "ACK|7|PONG");
  expectReply(request("7|PING|"), "ERR|7|ARG");
  expectReply(request("7||CLEAR"), "ERR|0|FORMAT");
  expectReply(request("7|ADD|1||2|0|message"), "ERR|7|ARG");
  expectReply(request("7|ADD|1|2|0|hello@example.com"), "ACK|7|SAVED");
  expectReply(std::string("@7|PING*6D") + '\0' + "\n", "ERR|0|FORMAT");
  expectReply("@7|PI\rNG*6D\n", "ERR|0|FORMAT");
  expectReply("@7|PING*+D\n", "ERR|0|FORMAT");
  expectReply(std::string(108, 'A') + "\n", "ERR|0|OVERSIZE");
  expectReply(request("7|STATUS"), "ACK|7|COUNT=1");
  for (uint8_t id = 2; id <= NOTICE_CAPACITY; ++id) {
    const std::string payload = "7|ADD|" + std::to_string(id) + "|2|1|temporary";
    expectReply(request(payload.c_str()), "ACK|7|SAVED");
  }
  expectReply(request("7|ADD|6|2|0|new"), "ERR|7|FULL");
  clockMillis = 60000;
  expectReply(request("7|ADD|6|2|0|new"), "ACK|7|SAVED");
  expectReply(request("7|STATUS"), "ACK|7|COUNT=2");
  expectReply(request("7|LIST"), "ITEM|7|1|2\r\nITEM|7|6|2\r\nACK|7|END");
  expectReply(request("7|DELETE|6"), "ACK|7|DELETED");
  expectReply(request("7|DELETE|6"), "ERR|7|NOT_FOUND");
  expectReply(request("7|CLEAR"), "ACK|7|CLEARED");
  expectReply(request("7|STATUS"), "ACK|7|COUNT=0");
}

int main() {
  testExpiryAndPriority();
  testPersistence();
  testWireFraming();
  std::cout << "PASS: firmware framing and persistence tests\n";
}
