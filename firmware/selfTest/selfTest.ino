#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Audio.h>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include "board_config.h"

// Read-only hardware exercise. No filesystem write or format calls.
TFT_eSPI lcd;
SPIClass sdSpi(HSPI);
Audio audio;
String wavPath;
bool sdReady = false;
bool audioReady = false;
uint32_t audioStartMs = 0;
uint32_t lastAudioLogMs = 0;

struct Button {
  int pin;
  const char *name;
  bool stable;
  bool raw;
  uint32_t changed;
};
Button buttons[] = {
    {board::button_select, "SELECT", HIGH, HIGH, 0},
    {board::button_down, "DOWN", HIGH, HIGH, 0},
    {board::button_up, "UP", HIGH, HIGH, 0},
};

void pollButtons() {
  const uint32_t now = millis();
  for (Button &b : buttons) {
    const bool raw = digitalRead(b.pin);
    if (raw != b.raw) {
      b.raw = raw;
      b.changed = now;
    }
    if (raw != b.stable && now - b.changed >= 30) {
      b.stable = raw;
      Serial.printf("[BUTTON] %s GPIO%d %s\n", b.name, b.pin,
                    raw == LOW ? "PRESSED" : "RELEASED");
    }
  }
}

void showStatus(const char *line, uint16_t color = TFT_WHITE) {
  lcd.fillRect(0, 170, 240, 70, TFT_BLACK);
  lcd.setTextColor(color, TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(2, 175);
  lcd.println(line);
}

void lcdTest() {
  const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
  const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
  for (unsigned i = 0; i < 5; ++i) {
    lcd.fillScreen(colors[i]);
    lcd.setTextSize(2);
    lcd.setTextColor(i == 4 ? TFT_WHITE : TFT_BLACK, colors[i]);
    lcd.setCursor(12, 105);
    lcd.printf("%s %u/5", names[i], i + 1);
    Serial.printf("[LCD] %s\n", names[i]);
    const uint32_t until = millis() + 1200;
    while (static_cast<int32_t>(millis() - until) < 0) {
      pollButtons();
      delay(2);
    }
  }
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(2, 2);
  lcd.println("Xingzhi self-test");
  lcd.setTextSize(1);
  lcd.println("LCD 10/9/8/14/18/13");
  lcd.println("SD 1/2/3/46 20MHz");
  lcd.println("MIC 4/5/6 16kHz");
  lcd.println("SPK 7/15/16 24kHz");
  lcd.println("BTN 0/39/40");
  showStatus("LCD done");
}

void printDiagnostics() {
  Serial.printf("[BOOT] chip=%s revision=%d cpu=%uMHz flash=%u\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(),
                ESP.getFlashChipSize());
  Serial.printf("[BOOT] psram=%s size=%u free=%u internal_heap=%u reset=%d\n",
                psramFound() ? "yes" : "no", ESP.getPsramSize(),
                ESP.getFreePsram(),
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL), esp_reset_reason());
  Serial.println("[GPIO] SD MISO=1 MOSI=2 SCK=3 CS=46; LCD MOSI=10 SCK=9 DC=8 CS=14 RST=18 BL=13");
  Serial.println("[GPIO] MIC WS=4 SCK=5 DIN=6; SPK DOUT=7 BCLK=15 LRCK=16; BTN SELECT=0 DOWN=39 UP=40; POWER=21");
}

void testSd(uint32_t frequency = 20000000) {
  sdSpi.begin(board::sd_sck, board::sd_miso, board::sd_mosi, board::sd_cs);
  sdReady = SD.begin(board::sd_cs, sdSpi, frequency, "/sd", 5, false);
  if (!sdReady) {
    Serial.printf("[SD] mount failed at %uMHz\n", frequency / 1000000);
    showStatus("SD FAIL", TFT_RED);
    return;
  }
  Serial.printf("[SD] clock=%uMHz type=%d capacity=%llu bytes\n",
                frequency / 1000000, SD.cardType(), SD.cardSize());
  Serial.println("[SD] filesystem=FAT mount (Arduino SD API)");
  File root = SD.open("/", FILE_READ);
  File benchmark;
  if (root) {
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      String path = String(f.name());
      if (!path.startsWith("/")) path = "/" + path;
      Serial.printf("[SD] %s %llu bytes%s\n", path.c_str(),
                    static_cast<unsigned long long>(f.size()),
                    f.isDirectory() ? " [dir]" : "");
      String lower = path;
      lower.toLowerCase();
      if (!f.isDirectory() && lower.endsWith(".wav") && wavPath.isEmpty()) wavPath = path;
      if (!f.isDirectory() && !benchmark && f.size() > 0) benchmark = SD.open(path, FILE_READ);
      f.close();
    }
    root.close();
  }
  if (benchmark) {
    uint8_t buf[4096];
    uint32_t bytes = 0;
    const uint32_t start = millis();
    const uint32_t limit = 4UL * 1024UL * 1024UL;
    bool readError = false;
    while (bytes < limit && benchmark.available()) {
      const int wanted = min(static_cast<uint32_t>(sizeof(buf)), limit - bytes);
      const int count = benchmark.read(buf, wanted);
      if (count <= 0) { readError = true; break; }
      bytes += count;
      delay(0);
    }
    const uint32_t elapsed = max(1UL, millis() - start);
    Serial.printf("[SD] benchmark clock=%uMHz bytes=%u ms=%u KB/s=%u error=%d\n",
                  frequency / 1000000, bytes, elapsed, bytes * 1000UL / elapsed / 1024UL,
                  readError);
    benchmark.close();
  }
  showStatus(frequency == 20000000 ? "SD 20MHz OK" : "SD 40MHz OK", TFT_GREEN);
}

void testMic();

void playReferenceTone() {
  i2s_config_t cfg = {};
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = 24000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 240;
  cfg.tx_desc_auto_clear = true;
  // Audio reserves I2S0 even when a WAV is rejected; use I2S1 for the tone.
  const esp_err_t init = i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr);
  if (init != ESP_OK) { Serial.printf("[AUDIO] tone init error=%d\n", init); return; }
  i2s_pin_config_t pins = {};
  pins.bck_io_num = board::speaker_bclk;
  pins.ws_io_num = board::speaker_lrck;
  pins.data_out_num = board::speaker_dout;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  const esp_err_t pinResult = i2s_set_pin(I2S_NUM_1, &pins);
  if (pinResult != ESP_OK) { Serial.printf("[AUDIO] tone pin error=%d\n", pinResult); i2s_driver_uninstall(I2S_NUM_1); return; }
  Serial.println("[AUDIO] reference tone 1000Hz, 24000Hz, peak=6000/32767, 8 seconds");
  showStatus("AUDIO 1kHz", TFT_GREEN);
  int16_t samples[480];
  for (int i = 0; i < 240; ++i) {
    const int16_t value = static_cast<int16_t>(6000 * sinf(2.0f * PI * (i % 24) / 24.0f));
    samples[2 * i] = value;
    samples[2 * i + 1] = value;
  }
  for (int i = 0; i < 800; ++i) {
    size_t written = 0;
    const esp_err_t result = i2s_write(I2S_NUM_1, samples, sizeof(samples), &written, pdMS_TO_TICKS(100));
    if (result != ESP_OK || written != sizeof(samples)) {
      Serial.printf("[AUDIO] tone write error=%d bytes=%u\n", result, written);
      break;
    }
    pollButtons();
  }
  i2s_zero_dma_buffer(I2S_NUM_1);
  i2s_driver_uninstall(I2S_NUM_1);
  Serial.println("[AUDIO] tone done");
}

bool validWav(const String &path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  uint8_t header[44] = {};
  const int read = f.read(header, sizeof(header));
  f.close();
  if (read != 44 || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4) ||
      memcmp(header + 12, "fmt ", 4)) return false;
  const uint16_t format = header[20] | header[21] << 8;
  const uint16_t channels = header[22] | header[23] << 8;
  const uint32_t rate = header[24] | header[25] << 8 | header[26] << 16 | header[27] << 24;
  const uint16_t bits = header[34] | header[35] << 8;
  Serial.printf("[AUDIO] WAV format=%u channels=%u rate=%u bits=%u\n",
                format, channels, rate, bits);
  return format == 1 && channels == 1 && rate == 24000 && bits == 16;
}

void startAudioTest() {
  if (!sdReady || wavPath.isEmpty()) {
    Serial.println("[AUDIO] no WAV file found; audio-only test skipped");
    playReferenceTone();
    testMic();
    return;
  }
  if (!validWav(wavPath)) {
    Serial.printf("[AUDIO] incompatible WAV %s; audio-only test skipped\n", wavPath.c_str());
    playReferenceTone();
    testMic();
    return;
  }
  audio.setPinout(board::speaker_bclk, board::speaker_lrck, board::speaker_dout);
  audio.setVolume(3);
  audioReady = audio.connecttoFS(SD, wavPath.c_str());
  audioStartMs = millis();
  Serial.printf("[AUDIO] path=%s started=%d volume=3/21\n", wavPath.c_str(), audioReady);
  showStatus(audioReady ? "AUDIO playing" : "AUDIO FAIL", audioReady ? TFT_GREEN : TFT_RED);
}

void testMic() {
  i2s_config_t cfg = {};
  cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = 16000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  const esp_err_t init = i2s_driver_install(I2S_NUM_1, &cfg, 0, nullptr);
  if (init != ESP_OK) { Serial.printf("[MIC] init error=%d\n", init); return; }
  i2s_pin_config_t pins = {};
  pins.bck_io_num = board::mic_sck;
  pins.ws_io_num = board::mic_ws;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = board::mic_din;
  const esp_err_t pinResult = i2s_set_pin(I2S_NUM_1, &pins);
  if (pinResult != ESP_OK) { Serial.printf("[MIC] pin error=%d\n", pinResult); i2s_driver_uninstall(I2S_NUM_1); return; }
  Serial.println("[MIC] capturing 5 seconds at 16kHz; speak or clap");
  const uint32_t endAt = millis() + 5000;
  uint32_t reportAt = millis() + 500;
  uint64_t sumSquare = 0;
  uint32_t sampleCount = 0;
  int32_t reportPeak = 0;
  while (static_cast<int32_t>(millis() - endAt) < 0) {
    int32_t samples[256];
    size_t got = 0;
    const esp_err_t result = i2s_read(I2S_NUM_1, samples, sizeof(samples), &got, pdMS_TO_TICKS(100));
    if (result != ESP_OK) { Serial.printf("[MIC] read error=%d\n", result); break; }
    const size_t count = got / sizeof(int32_t);
    for (size_t i = 0; i < count; ++i) {
      const int32_t value = samples[i] >> 16;
      const int32_t absolute = abs(value);
      if (absolute > reportPeak) reportPeak = absolute;
      sumSquare += static_cast<int64_t>(value) * value;
    }
    sampleCount += count;
    if (static_cast<int32_t>(millis() - reportAt) >= 0 && sampleCount) {
      Serial.printf("[MIC] rms=%.1f peak=%ld n=%u\n",
                    sqrt(static_cast<double>(sumSquare) / sampleCount),
                    static_cast<long>(reportPeak), sampleCount);
      reportAt += 500;
      sumSquare = 0;
      sampleCount = 0;
      reportPeak = 0;
    }
    pollButtons();
  }
  i2s_driver_uninstall(I2S_NUM_1);
  showStatus("MIC done", TFT_GREEN);
}

void setup() {
  pinMode(board::power_latch, OUTPUT);
  digitalWrite(board::power_latch, HIGH);
  pinMode(board::lcd_backlight, OUTPUT);
  digitalWrite(board::lcd_backlight, HIGH);
  for (Button &b : buttons) pinMode(b.pin, INPUT_PULLUP);
  Serial.begin(115200);
  delay(1000);
  printDiagnostics();
  lcd.init();
  lcd.setRotation(4);
  lcdTest();
  testSd();
  startAudioTest();
}

void loop() {
  pollButtons();
  if (Serial.available()) {
    const int command = Serial.read();
    if (command == 'r') {
      Serial.println("[TEST] replay requested over serial");
      if (audioReady) { audio.stopSong(); audioReady = false; }
      wavPath = "";
      lcdTest();
      SD.end();
      testSd();
      startAudioTest();
    } else if (command == 's' && !audioReady) {
      Serial.println("[TEST] SD 40MHz read test requested");
      SD.end();
      testSd(40000000);
      SD.end();
      Serial.println("[TEST] restoring SD 20MHz");
      testSd();
    }
  }
  if (audioReady) {
    audio.loop();
    if (millis() - lastAudioLogMs >= 1000) {
      lastAudioLogMs = millis();
      Serial.printf("[AUDIO] running=%d elapsed_ms=%u\n", audio.isRunning(), millis() - audioStartMs);
    }
    if (!audio.isRunning() || millis() - audioStartMs >= 15000) {
      audio.stopSong();
      audioReady = false;
      Serial.println("[AUDIO] stopped; no underrun counter in library 2.0.0");
      testMic();
    }
  }
  delay(1);
}
