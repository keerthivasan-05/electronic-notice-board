#pragma once
#include "Arduino.h"
struct SoftwareSerial : SerialPort {
  SoftwareSerial(int, int) {}
};
