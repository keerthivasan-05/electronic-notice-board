#pragma once
#include <Arduino.h>

constexpr uint8_t NOTICE_CAPACITY = 5;
constexpr uint8_t TEXT_CAPACITY = 64;

struct Notice {
  uint8_t id;
  uint8_t priority; // 1 low, 2 normal, 3 urgent
  char text[TEXT_CAPACITY + 1];
  uint32_t expiresAt; // millis deadline, including across rollover
  bool temporary;
  bool used;
};

class NoticeStore {
public:
  void begin();
  bool add(uint8_t id, uint8_t priority, uint16_t ttlMinutes, const char* text, uint32_t now);
  bool remove(uint8_t id);
  void clear();
  void expire(uint32_t now);
  int8_t nextIndex(int8_t previous) const;
  const Notice& at(uint8_t index) const { return entries[index]; }
  uint8_t count() const;
  bool contains(uint8_t id) const;
private:
  void persist();
  int8_t activeBank = -1;
  uint8_t generation = 0;
  Notice entries[NOTICE_CAPACITY] = {};
  int8_t find(uint8_t id) const;
};
