#pragma once
#include <Arduino.h>
#include <Wire.h>

class CST820 {
 public:
  CST820(int8_t sdaPin, int8_t sclPin, int8_t rstPin, int8_t intPin);
  bool begin(uint16_t width, uint16_t height, uint8_t rotation);
  bool getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture = nullptr);

 private:
  bool readByte(uint8_t reg, uint8_t &value);
  bool readBytes(uint8_t reg, uint8_t *data, size_t len);
  bool writeByte(uint8_t reg, uint8_t value);

  int8_t _sda, _scl, _rst, _int;
  uint16_t _width = 320, _height = 240;
  uint8_t _rotation = 3;
};
