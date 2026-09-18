// TFT_eSPI setup for xingzhi-cube-1.54tft-wifi.
// Copy/select this file through TFT_eSPI/User_Setup_Select.h before building.

#define USER_SETUP_ID 15401

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

#define TFT_MOSI 10
#define TFT_SCLK 9
#define TFT_CS   14
#define TFT_DC   8
#define TFT_RST  18

// Backlight GPIO13 is controlled by the sketch.
#define TFT_BL 13
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF

#define SPI_FREQUENCY       40000000
#define SPI_READ_FREQUENCY  20000000

// The official ESP-IDF board implementation uses SPI mode 3 at up to 80 MHz.
// Start at 40 MHz for margin; increase only after a long stability test.
#define TFT_SPI_MODE SPI_MODE3
