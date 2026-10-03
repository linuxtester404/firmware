#include <Arduino.h>
#include <SPI.h>

static constexpr int PIN_MISO = 12;
static constexpr int PIN_MOSI = 13;
static constexpr int PIN_SCLK = 14;
static constexpr int PIN_CS   = 15;
static constexpr int PIN_DC   = 2;
static constexpr int PIN_BL   = 27;

SPIClass lcdSpi(HSPI);

static inline void csLow()  { digitalWrite(PIN_CS, LOW); }
static inline void csHigh() { digitalWrite(PIN_CS, HIGH); }
static inline void dcCmd()  { digitalWrite(PIN_DC, LOW); }
static inline void dcData() { digitalWrite(PIN_DC, HIGH); }

void writeCommand(uint8_t cmd) {
  lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  csLow();
  dcCmd();
  lcdSpi.write(cmd);
  csHigh();
  lcdSpi.endTransaction();
}

void writeData8(uint8_t data) {
  lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  csLow();
  dcData();
  lcdSpi.write(data);
  csHigh();
  lcdSpi.endTransaction();
}

void writeData(const uint8_t *data, size_t len) {
  lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  csLow();
  dcData();
  lcdSpi.writeBytes(data, len);
  csHigh();
  lcdSpi.endTransaction();
}

void commandData(uint8_t cmd, const uint8_t *data, size_t len) {
  lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  csLow();
  dcCmd();
  lcdSpi.write(cmd);
  if (len) {
    dcData();
    lcdSpi.writeBytes(data, len);
  }
  csHigh();
  lcdSpi.endTransaction();
}

void ili9341Init() {
  Serial.println("[LCD] ILI9341 init start");
  writeCommand(0x01); // Software reset
  delay(150);
  writeCommand(0x28); // Display off

  const uint8_t cf[] = {0x00, 0xC1, 0x30};
  const uint8_t ed[] = {0x64, 0x03, 0x12, 0x81};
  const uint8_t e8[] = {0x85, 0x00, 0x78};
  const uint8_t cb[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
  const uint8_t f7[] = {0x20};
  const uint8_t ea[] = {0x00, 0x00};
  const uint8_t c0[] = {0x23};
  const uint8_t c1[] = {0x10};
  const uint8_t c5[] = {0x3E, 0x28};
  const uint8_t c7[] = {0x86};
  const uint8_t madctl[] = {0x48};
  const uint8_t pixfmt[] = {0x55};
  const uint8_t b1[] = {0x00, 0x18};
  const uint8_t b6[] = {0x08, 0x82, 0x27};
  const uint8_t f2[] = {0x00};
  const uint8_t gamma[] = {0x01};
  const uint8_t e0[] = {0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,0x37,0x07,0x10,0x03,0x0E,0x09,0x00};
  const uint8_t e1[] = {0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F};

  commandData(0xCF, cf, sizeof(cf));
  commandData(0xED, ed, sizeof(ed));
  commandData(0xE8, e8, sizeof(e8));
  commandData(0xCB, cb, sizeof(cb));
  commandData(0xF7, f7, sizeof(f7));
  commandData(0xEA, ea, sizeof(ea));
  commandData(0xC0, c0, sizeof(c0));
  commandData(0xC1, c1, sizeof(c1));
  commandData(0xC5, c5, sizeof(c5));
  commandData(0xC7, c7, sizeof(c7));
  commandData(0x36, madctl, sizeof(madctl));
  commandData(0x3A, pixfmt, sizeof(pixfmt));
  commandData(0xB1, b1, sizeof(b1));
  commandData(0xB6, b6, sizeof(b6));
  commandData(0xF2, f2, sizeof(f2));
  commandData(0x26, gamma, sizeof(gamma));
  commandData(0xE0, e0, sizeof(e0));
  commandData(0xE1, e1, sizeof(e1));

  writeCommand(0x11); // Sleep out
  delay(120);
  writeCommand(0x29); // Display on
  delay(20);
  Serial.println("[LCD] ILI9341 init complete");
}

void setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  uint8_t col[] = {uint8_t(x0 >> 8), uint8_t(x0), uint8_t(x1 >> 8), uint8_t(x1)};
  uint8_t row[] = {uint8_t(y0 >> 8), uint8_t(y0), uint8_t(y1 >> 8), uint8_t(y1)};
  commandData(0x2A, col, sizeof(col));
  commandData(0x2B, row, sizeof(row));
  writeCommand(0x2C);
}

void fillScreen(uint16_t rgb565) {
  setAddrWindow(0, 0, 239, 319);
  const uint8_t hi = rgb565 >> 8;
  const uint8_t lo = rgb565 & 0xFF;

  lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
  csLow();
  dcData();

  static uint8_t block[512];
  for (size_t i = 0; i < sizeof(block); i += 2) {
    block[i] = hi;
    block[i + 1] = lo;
  }

  const uint32_t pixels = 240UL * 320UL;
  uint32_t sent = 0;
  while (sent < pixels) {
    uint32_t chunkPixels = min<uint32_t>(pixels - sent, sizeof(block) / 2);
    lcdSpi.writeBytes(block, chunkPixels * 2);
    sent += chunkPixels;
  }

  csHigh();
  lcdSpi.endTransaction();
}

void verticalBars() {
  static const uint16_t colors[] = {
    0xF800, // red
    0x07E0, // green
    0x001F, // blue
    0xFFE0, // yellow
    0x07FF, // cyan
    0xF81F, // magenta
    0xFFFF, // white
    0x0000  // black
  };

  for (int i = 0; i < 8; ++i) {
    uint16_t x0 = i * 30;
    uint16_t x1 = x0 + 29;
    setAddrWindow(x0, 0, x1, 319);

    uint8_t hi = colors[i] >> 8;
    uint8_t lo = colors[i] & 0xFF;
    lcdSpi.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    csLow();
    dcData();
    for (uint32_t p = 0; p < 30UL * 320UL; ++p) {
      lcdSpi.write(hi);
      lcdSpi.write(lo);
    }
    csHigh();
    lcdSpi.endTransaction();
  }
}

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println();
  Serial.println("==================================");
  Serial.println("Invectus CYD Display Diagnostic");
  Serial.println("Target: ESP32-2432S024C");
  Serial.println("LCD: ILI9341 / SPI");
  Serial.println("Pins: MISO12 MOSI13 SCLK14 CS15 DC2 BL27");
  Serial.println("==================================");

  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_BL, OUTPUT);

  csHigh();
  digitalWrite(PIN_BL, LOW);
  delay(300);

  lcdSpi.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_CS);
  ili9341Init();

  Serial.println("[BL] Backlight ON");
  digitalWrite(PIN_BL, HIGH);

  Serial.println("[TEST] RED");
  fillScreen(0xF800);
  delay(1200);

  Serial.println("[TEST] GREEN");
  fillScreen(0x07E0);
  delay(1200);

  Serial.println("[TEST] BLUE");
  fillScreen(0x001F);
  delay(1200);

  Serial.println("[TEST] WHITE");
  fillScreen(0xFFFF);
  delay(1200);

  Serial.println("[TEST] COLOR BARS");
  verticalBars();
  Serial.println("[TEST] Diagnostic pattern will remain on screen");
}

void loop() {
  delay(1000);
}
