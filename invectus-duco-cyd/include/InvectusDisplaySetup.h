#pragma once

// ESP32-2432S024C 2.4" capacitive-touch board.
// This exact setup mirrors known-working S024C projects and forces TFT_eSPI
// to use the ILI9341_2 init sequence on the ESP32 HSPI peripheral.

#define USER_SETUP_INFO "Invectus ESP32-2432S024C"
#define ILI9341_2_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   27
#define TFT_BACKLIGHT_ON HIGH

#define USE_HSPI_PORT

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_GFXFF

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

#define TFT_INVERSION_OFF
