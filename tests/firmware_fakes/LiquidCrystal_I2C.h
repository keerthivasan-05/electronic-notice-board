#pragma once
#include <string>

struct LiquidCrystal_I2C {
  LiquidCrystal_I2C(int, int columns, int) : rows{std::string(columns, ' '), std::string(columns, ' ')} {}
  void init() {}
  void backlight() {}
  void setCursor(int column, int row) {
    cursorColumn = column;
    cursorRow = row;
  }
  void print(const char* text) {
    while (*text) write(*text++);
  }
  void write(char value) { rows[cursorRow].at(cursorColumn++) = value; }

  std::string rows[2];

private:
  int cursorColumn = 0;
  int cursorRow = 0;
};
