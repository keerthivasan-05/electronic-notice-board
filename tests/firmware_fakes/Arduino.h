#pragma once
#include <cstdint>
#include <cstdio>
#include <queue>
#include <string>

#define F(value) (value)

extern uint32_t clockMillis;
inline uint32_t millis() { return clockMillis; }

struct SerialPort {
  std::queue<unsigned char> input;
  std::string output;

  void begin(unsigned long) {}
  bool available() const { return !input.empty(); }
  int read() {
    int value = input.front();
    input.pop();
    return value;
  }
  void print(const char* value) { output += value; }
  void print(char value) { output += value; }
  void print(uint8_t value) { output += std::to_string(value); }
  template <typename T> void println(T value) {
    print(value);
    output += "\r\n";
  }
};

extern SerialPort Serial;
