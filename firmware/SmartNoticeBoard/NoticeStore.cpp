#include "NoticeStore.h"
#include "Crc8.h"

#include <EEPROM.h>
#include <string.h>

namespace {
constexpr uint8_t MAGIC = 0xB7;
constexpr uint8_t VERSION = 2;
constexpr uint8_t RECORD_SIZE = 3 + TEXT_CAPACITY;
constexpr uint16_t BANK_SIZE = 4 + NOTICE_CAPACITY * RECORD_SIZE + 1;

uint16_t bankAddress(uint8_t bank) { return uint16_t(bank) * BANK_SIZE; }

bool validSnapshot(uint16_t base, uint8_t version) {
  const uint8_t headerSize = version == VERSION ? 4 : 3;
  if (base + headerSize + 1 > EEPROM.length() ||
      EEPROM.read(base) != MAGIC || EEPROM.read(base + 1) != version) return false;

  const uint8_t count = EEPROM.read(base + 2);
  const uint16_t checksumAddress = base + headerSize + uint16_t(count) * RECORD_SIZE;
  if (count > NOTICE_CAPACITY || checksumAddress >= EEPROM.length()) return false;
  uint8_t crc = 0;
  for (uint16_t address = base; address < checksumAddress; ++address) {
    crc = crc8Update(crc, EEPROM.read(address));
  }
  if (crc != EEPROM.read(checksumAddress)) return false;

  uint8_t ids[NOTICE_CAPACITY] = {};
  for (uint8_t i = 0; i < count; ++i) {
    const uint16_t record = base + headerSize + uint16_t(i) * RECORD_SIZE;
    const uint8_t id = EEPROM.read(record);
    const uint8_t priority = EEPROM.read(record + 1);
    const uint8_t length = EEPROM.read(record + 2);
    if (!id || priority < 1 || priority > 3 || !length || length > TEXT_CAPACITY) return false;
    for (uint8_t previous = 0; previous < i; ++previous) {
      if (ids[previous] == id) return false;
    }
    for (uint8_t index = 0; index < length; ++index) {
      const uint8_t byte = EEPROM.read(record + 3 + index);
      if (byte < 32 || byte > 126) return false;
    }
    ids[i] = id;
  }
  return true;
}
} // namespace

int8_t NoticeStore::find(uint8_t id) const {
  for (uint8_t i = 0; i < NOTICE_CAPACITY; ++i) {
    if (entries[i].used && entries[i].id == id) return i;
  }
  return -1;
}

bool NoticeStore::contains(uint8_t id) const {
  return find(id) >= 0;
}

uint8_t NoticeStore::count() const {
  uint8_t count = 0;
  for (const auto& entry : entries) {
    if (entry.used) ++count;
  }
  return count;
}

bool NoticeStore::add(uint8_t id, uint8_t priority, uint16_t ttlMinutes, const char* text, uint32_t now) {
  if (!id || priority < 1 || priority > 3 || ttlMinutes > 1440 || !text) return false;
  const size_t length = strlen(text);
  if (!length || length > TEXT_CAPACITY) return false;

  int8_t index = find(id);
  if (index < 0) {
    for (uint8_t i = 0; i < NOTICE_CAPACITY; ++i) {
      if (!entries[i].used) {
        index = i;
        break;
      }
    }
  }
  if (index < 0) return false;

  Notice& entry = entries[index];
  const bool previouslyPermanent = entry.used && !entry.temporary;
  entry.id = id;
  entry.priority = priority;
  memcpy(entry.text, text, length + 1);
  entry.temporary = ttlMinutes != 0;
  entry.expiresAt = now + uint32_t(ttlMinutes) * 60000UL;
  entry.used = true;
  if (previouslyPermanent || !entry.temporary) persist();
  return true;
}

bool NoticeStore::remove(uint8_t id) {
  const int8_t index = find(id);
  if (index < 0) return false;

  Notice& entry = entries[index];
  entry.used = false;
  if (!entry.temporary) persist();
  return true;
}

void NoticeStore::clear() {
  bool hadPermanent = false;
  for (auto& entry : entries) {
    hadPermanent |= entry.used && !entry.temporary;
    entry.used = false;
  }
  if (hadPermanent) persist();
}

void NoticeStore::expire(uint32_t now) {
  for (auto& entry : entries) {
    // TTL is less than half the clock range, so this also handles rollover.
    if (entry.used && entry.temporary && uint32_t(now - entry.expiresAt) < UINT32_C(0x80000000)) {
      entry.used = false;
    }
  }
}

int8_t NoticeStore::nextIndex(int8_t previous) const {
  uint8_t best = 0;
  for (const auto& entry : entries) {
    if (entry.used && entry.priority > best) best = entry.priority;
  }
  if (!best) return -1;

  // Round-robin among entries with the highest priority.
  for (uint8_t step = 1; step <= NOTICE_CAPACITY; ++step) {
    const uint8_t index = uint8_t((int(previous) + step + NOTICE_CAPACITY) % NOTICE_CAPACITY);
    if (entries[index].used && entries[index].priority == best) return index;
  }
  return -1;
}

void NoticeStore::persist() {
  if (EEPROM.length() < 2 * BANK_SIZE) return;

  uint8_t bytes[BANK_SIZE] = {MAGIC, VERSION, 0, uint8_t(generation + 1)};
  uint16_t length = 4;
  for (const auto& entry : entries) {
    if (!entry.used || entry.temporary) continue;
    const uint8_t textLength = uint8_t(strlen(entry.text));
    ++bytes[2];
    bytes[length++] = entry.id;
    bytes[length++] = entry.priority;
    bytes[length++] = textLength;
    memcpy(bytes + length, entry.text, textLength);
    length += TEXT_CAPACITY;
  }

  if (activeBank >= 0) {
    const uint16_t current = bankAddress(activeBank);
    bool identical = EEPROM.read(current + 2) == bytes[2];
    for (uint16_t i = 4; identical && i < length; ++i) {
      identical = EEPROM.read(current + i) == bytes[i];
    }
    if (identical) return;
  }

  uint8_t crc = 0;
  for (uint16_t i = 0; i < length; ++i) crc = crc8Update(crc, bytes[i]);
  bytes[length++] = crc;

  // Bank zero may still contain a legacy snapshot. Commit bank one first.
  const uint8_t target = activeBank == 1 ? 0 : 1;
  const uint16_t base = bankAddress(target);
  EEPROM.update(base, 0);
  for (uint16_t i = 1; i < length; ++i) EEPROM.update(base + i, bytes[i]);
  EEPROM.update(base, MAGIC);
  activeBank = target;
  generation = bytes[3];
}

void NoticeStore::begin() {
  for (auto& entry : entries) entry.used = false;
  activeBank = -1;
  generation = 0;

  const bool firstValid = validSnapshot(0, VERSION);
  const bool secondValid = validSnapshot(BANK_SIZE, VERSION);
  if (firstValid && secondValid) {
    // Successive commits differ by one, including when the byte wraps.
    const uint8_t difference = EEPROM.read(BANK_SIZE + 3) - EEPROM.read(3);
    activeBank = difference > 0 && difference < 128 ? 1 : 0;
  } else if (firstValid || secondValid) {
    activeBank = secondValid ? 1 : 0;
  } else if (!validSnapshot(0, 1)) {
    return;
  }

  const uint16_t base = activeBank < 0 ? 0 : bankAddress(activeBank);
  const uint8_t countSaved = EEPROM.read(base + 2);
  uint16_t record = base + 3;
  if (activeBank >= 0) generation = EEPROM.read(record++);
  for (uint8_t i = 0; i < countSaved; ++i) {
    Notice& entry = entries[i];
    entry.id = EEPROM.read(record);
    entry.priority = EEPROM.read(record + 1);
    const uint8_t length = EEPROM.read(record + 2);
    for (uint8_t k = 0; k < length; ++k) entry.text[k] = char(EEPROM.read(record + 3 + k));
    entry.text[length] = '\0';
    entry.expiresAt = 0;
    entry.temporary = false;
    entry.used = true;
    record += RECORD_SIZE;
  }
}
