#pragma once

#include <stddef.h>

namespace board {
constexpr int sd_miso = 1;
constexpr int sd_mosi = 2;
constexpr int sd_sck = 3;
constexpr int sd_cs = 46;
constexpr int mic_ws = 4;
constexpr int mic_sck = 5;
constexpr int mic_din = 6;
constexpr int speaker_dout = 7;
constexpr int speaker_bclk = 15;
constexpr int speaker_lrck = 16;
constexpr int button_select = 0;
constexpr int button_up = 40;
constexpr int button_down = 39;
constexpr int lcd_mosi = 10;
constexpr int lcd_sck = 9;
constexpr int lcd_dc = 8;
constexpr int lcd_cs = 14;
constexpr int lcd_reset = 18;
constexpr int lcd_backlight = 13;
constexpr int power_latch = 21;

constexpr int used_pins[] = {
    sd_miso, sd_mosi, sd_sck, sd_cs, mic_ws, mic_sck, mic_din,
    speaker_dout, speaker_bclk, speaker_lrck, button_select,
    button_up, button_down, lcd_mosi, lcd_sck, lcd_dc, lcd_cs,
    lcd_reset, lcd_backlight, power_latch};

constexpr bool unique_pins(const int *pins, size_t count, size_t i = 0,
                           size_t j = 1) {
  return i >= count ? true
                    : j >= count ? unique_pins(pins, count, i + 1, i + 2)
                                 : (pins[i] != pins[j] &&
                                    unique_pins(pins, count, i, j + 1));
}
static_assert(unique_pins(used_pins, sizeof(used_pins) / sizeof(used_pins[0])),
              "Board GPIO conflict");
}  // namespace board
