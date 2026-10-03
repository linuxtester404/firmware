#include "CST820.h"

static constexpr uint8_t CST820_ADDR = 0x15;

CST820::CST820(int8_t sdaPin, int8_t sclPin, int8_t rstPin, int8_t intPin)
    : _sda(sdaPin), _scl(sclPin), _rst(rstPin), _int(intPin) {}

bool CST820::begin(uint16_t width, uint16_t height, uint8_t rotation) {
  _width = width;
  _height = height;
  _rotation = rotation & 3;

  Wire.begin(_sda, _scl);
  Wire.setClock(400000);
  Wire.setTimeOut(50);

  if (_int >= 0) {
    pinMode(_int, INPUT_PULLUP);
  }

  if (_rst >= 0) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, LOW);
    delay(12);
    digitalWrite(_rst, HIGH);
    delay(300);
  }

  // Disable automatic low-power mode so touch remains responsive.
  writeByte(0xFE, 0xFF);

  uint8_t fingers = 0;
  return readByte(0x02, fingers);
}

bool CST820::readByte(uint8_t reg, uint8_t &value) {
  for (int attempt = 0; attempt < 3; ++attempt) {
    Wire.beginTransmission(CST820_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
      delay(1);
      continue;
    }
    if (Wire.requestFrom((int)CST820_ADDR, 1) == 1 && Wire.available()) {
      value = Wire.read();
      return true;
    }
    delay(1);
  }
  value = 0;
  return false;
}

bool CST820::readBytes(uint8_t reg, uint8_t *data, size_t len) {
  Wire.beginTransmission(CST820_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  size_t got = Wire.requestFrom((int)CST820_ADDR, (int)len);
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!Wire.available()) return false;
    data[i] = Wire.read();
  }
  return true;
}

bool CST820::writeByte(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(CST820_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission(true) == 0;
}

bool CST820::getTouch(uint16_t *x, uint16_t *y, uint8_t *gesture) {
  uint8_t fingers = 0;
  if (!readByte(0x02, fingers) || fingers == 0) return false;

  uint8_t g = 0;
  readByte(0x01, g);

  uint8_t data[4] = {};
  if (!readBytes(0x03, data, sizeof(data))) return false;

  uint16_t rawX = ((data[0] & 0x0F) << 8) | data[1];
  uint16_t rawY = ((data[2] & 0x0F) << 8) | data[3];

  uint16_t tx = rawX, ty = rawY;
  if (_rotation == 1 || _rotation == 3) {
    tx = rawY;
    ty = rawX;
  }
  if (_rotation == 0) {
    tx = (_width > tx) ? (_width - 1 - tx) : 0;
    ty = (_height > ty) ? (_height - 1 - ty) : 0;
  } else if (_rotation == 2) {
    tx = (_width > tx) ? (_width - 1 - tx) : 0;
  } else if (_rotation == 3) {
    ty = (_height > ty) ? (_height - 1 - ty) : 0;
  }

  if (tx >= _width) tx = _width - 1;
  if (ty >= _height) ty = _height - 1;

  *x = tx;
  *y = ty;
  if (gesture) *gesture = g;
  return true;
}
