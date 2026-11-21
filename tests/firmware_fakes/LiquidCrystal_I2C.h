#pragma once
struct LiquidCrystal_I2C {
  LiquidCrystal_I2C(int, int, int) {}
  void init() {}
  void backlight() {}
  void setCursor(int, int) {}
  void print(const char*) {}
  void write(char) {}
};
