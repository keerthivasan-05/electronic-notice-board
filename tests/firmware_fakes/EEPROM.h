#pragma once
#include <array>
#include <cstdint>

struct PowerLoss {};

struct FakeEEPROM {
  std::array<uint8_t, 1024> bytes;
  int writesBeforeFailure = -1;

  FakeEEPROM() { bytes.fill(0xff); }
  uint16_t length() const { return bytes.size(); }
  uint8_t read(uint16_t address) const { return bytes.at(address); }
  void update(uint16_t address, uint8_t value) {
    if (bytes.at(address) == value) return;
    if (writesBeforeFailure == 0) throw PowerLoss{};
    if (writesBeforeFailure > 0) --writesBeforeFailure;
    bytes.at(address) = value;
  }
};

extern FakeEEPROM EEPROM;
