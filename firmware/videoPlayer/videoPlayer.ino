/*
 * esp32s3-Video-Player
 * Target: xingzhi-cube-1.54tft-wifi
 *
 * Media pair on SD root:
 *   001_demo.mjpeg  (raw baseline JPEG stream, 240x240, TARGET_FPS)
 *   001_demo.wav    (PCM s16le, mono, 24000 Hz)
 *
 * Tested on COM9: 15 fps, LCD SPI 40 MHz, SD SPI 20 MHz.
 */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <JPEGDEC.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <driver/i2s.h>
#include <freertos/stream_buffer.h>
#include "board_config.h"

// Buttons
static constexpr int PIN_BTN_UP   = board::button_up;
static constexpr int PIN_BTN_DOWN = board::button_down;
static constexpr int PIN_BTN_SEL  = board::button_select;

// Dedicated TF/SD SPI observed in the working sample.
static constexpr int SD_MISO = board::sd_miso;
static constexpr int SD_MOSI = board::sd_mosi;
static constexpr int SD_SCK  = board::sd_sck;
static constexpr int SD_CS   = board::sd_cs;

// I2S speaker
static constexpr int I2S_BCLK = board::speaker_bclk;
static constexpr int I2S_LRC  = board::speaker_lrck;
static constexpr int I2S_DOUT = board::speaker_dout;

// Board control
static constexpr int PIN_LCD_BACKLIGHT = board::lcd_backlight;
static constexpr int PIN_POWER_LATCH   = board::power_latch;

static constexpr uint8_t TFT_ROTATION = 4;
static constexpr uint8_t DEFAULT_VOLUME = 6; // PCM gain: 10 = unity, range 0..21
static constexpr uint8_t TARGET_FPS = 15;
static constexpr size_t MJPEG_BUFFER_SIZE = 96U * 1024U;
static constexpr size_t MAX_FILES = 50;
static constexpr uint32_t SD_FREQUENCY = 20000000UL;
static constexpr uint32_t AUDIO_PREROLL_MS = 120;
static constexpr uint32_t SELECT_HOLD_MS = 1000;

TFT_eSPI tft;
JPEGDEC jpeg;
SPIClass sdSPI(HSPI);
Preferences preferences;

struct PlaybackSettings {
  uint8_t volume = DEFAULT_VOLUME;
  bool autoStart = false;
  bool repeat = false;
  bool multi = false;
  bool randomOrder = false;
} settings;

enum class Screen { Videos, Settings };
Screen screen = Screen::Videos;
uint8_t settingRow = 0;
bool editingVolume = false;
uint32_t autoStartAtMs = 0;
bool playbackInterrupted = false;
const char *playbackStopReason = "end-of-file";
enum class PlaybackResult { Completed, Interrupted, Corrupt };

uint8_t *mjpegBuffer = nullptr;
size_t bufferBegin = 0;
size_t bufferEnd = 0;
bool videoEof = false;

String fileList[MAX_FILES];
size_t fileCount = 0;
int selectedIndex = 0;
volatile bool isPlaying = false;
volatile bool audioServiceEnabled = false;
TaskHandle_t audioTaskHandle = nullptr;
StreamBufferHandle_t audioStream = nullptr;
File audioFile;
volatile uint32_t audioDataRemaining = 0;
volatile uint32_t audioUnderruns = 0;
uint8_t audioReadBuffer[4096];
uint32_t videoSdBytes = 0;
uint32_t videoSdReadUs = 0;
uint32_t videoSdReadCalls = 0;
uint32_t lcdPushUs = 0;
uint8_t playbackFps = TARGET_FPS;
bool playbackAudio = true;
uint32_t sdFrequency = SD_FREQUENCY;
uint32_t menuGuardUntilMs = 0;

bool serialStopRequested() {
  while (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command == 'x' || command == 's') return true;
    if (command == '?') {
      Serial.printf("[BUTTON] SELECT raw=%d playing=%d\n",
                    digitalRead(PIN_BTN_SEL), isPlaying);
    }
  }
  return false;
}

struct ButtonState {
  uint8_t pin;
  bool stableLevel;
  bool lastRawLevel;
  uint32_t changedAtMs;
  uint32_t pressedAtMs;
  bool longReported;
};

bool buttonPressed(ButtonState &button);

ButtonState buttonUp{PIN_BTN_UP, HIGH, HIGH, 0, 0, false};
ButtonState buttonDown{PIN_BTN_DOWN, HIGH, HIGH, 0, 0, false};
ButtonState buttonSelect{PIN_BTN_SEL, HIGH, HIGH, 0, 0, false};

bool buttonPressed(ButtonState &button) {
  const bool raw = digitalRead(button.pin);
  const uint32_t now = millis();

  if (raw != button.lastRawLevel) {
    button.lastRawLevel = raw;
    button.changedAtMs = now;
  }

  if ((uint32_t)(now - button.changedAtMs) >= 25 && raw != button.stableLevel) {
    button.stableLevel = raw;
    return button.stableLevel == LOW;
  }
  return false;
}

enum class SelectEvent { None, Short, Long };
SelectEvent pollSelect();

SelectEvent pollSelect() {
  const uint32_t now = millis();
  const bool wasPressed = buttonSelect.stableLevel == LOW;
  buttonPressed(buttonSelect);
  const bool isPressed = buttonSelect.stableLevel == LOW;
  if (!wasPressed && isPressed) {
    buttonSelect.pressedAtMs = now;
    buttonSelect.longReported = false;
    Serial.println("[BUTTON] SELECT pressed");
  }
  if (isPressed && !buttonSelect.longReported &&
      uint32_t(now - buttonSelect.pressedAtMs) >= SELECT_HOLD_MS) {
    buttonSelect.longReported = true;
    Serial.println("[BUTTON] SELECT long");
    return SelectEvent::Long;
  }
  if (wasPressed && !isPressed) {
    if (!buttonSelect.longReported) {
      Serial.println("[BUTTON] SELECT short");
      return SelectEvent::Short;
    }
    Serial.println("[BUTTON] SELECT released after stop/long");
  }
  return SelectEvent::None;
}

void saveSelectedVideo() {
  if (fileCount > 0) preferences.putString("selected", fileList[selectedIndex]);
}

void saveSettings() {
  preferences.putUChar("volume", settings.volume);
  preferences.putBool("autostart", settings.autoStart);
  preferences.putBool("repeat", settings.repeat);
  preferences.putBool("multi", settings.multi);
  preferences.putBool("random", settings.randomOrder);
  Serial.printf("[SETTINGS] volume=%u auto=%d repeat=%d multi=%d random=%d\n",
                settings.volume, settings.autoStart, settings.repeat,
                settings.multi, settings.randomOrder);
}

void loadSettings() {
  preferences.begin("video-player", false);
  settings.volume = preferences.getUChar("volume", DEFAULT_VOLUME);
  if (settings.volume > 21) settings.volume = DEFAULT_VOLUME;
  settings.autoStart = preferences.getBool("autostart", false);
  settings.repeat = preferences.getBool("repeat", false);
  settings.multi = preferences.getBool("multi", false);
  settings.randomOrder = preferences.getBool("random", false);
}

uint32_t readLe32(const uint8_t *data) {
  return uint32_t(data[0]) | (uint32_t(data[1]) << 8) |
         (uint32_t(data[2]) << 16) | (uint32_t(data[3]) << 24);
}

void audioServiceTask(void *) {
  int16_t mono[512];
  int16_t stereo[1024];
  for (;;) {
    if (!audioServiceEnabled) {
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    const size_t got = xStreamBufferReceive(audioStream, mono, sizeof(mono), pdMS_TO_TICKS(10));
    if (got == 0) {
      if (audioDataRemaining > 0) ++audioUnderruns;
      continue;
    }
    const int volume = settings.volume;
    for (size_t i = 0; i < got / sizeof(int16_t); ++i) {
      const int32_t scaled = int32_t(mono[i]) * volume / 10;
      const int16_t sample = static_cast<int16_t>(constrain(scaled, -32768, 32767));
      stereo[2 * i] = sample;
      stereo[2 * i + 1] = sample;
    }
    size_t written = 0;
    i2s_write(I2S_NUM_1, stereo, got * 2, &written, pdMS_TO_TICKS(100));
  }
}

void stopAudio() {
  audioServiceEnabled = false;
  delay(30);
  if (audioFile) audioFile.close();
  audioDataRemaining = 0;
  xStreamBufferReset(audioStream);
  i2s_zero_dma_buffer(I2S_NUM_1);
}

void serviceAudio() {
  if (!audioServiceEnabled || audioDataRemaining == 0) return;
  const size_t freeBytes = xStreamBufferSpacesAvailable(audioStream);
  size_t wanted = min(size_t(audioDataRemaining), min(freeBytes, sizeof(audioReadBuffer)));
  wanted &= ~size_t(1);
  if (wanted < 2) return;
  const size_t got = audioFile.read(audioReadBuffer, wanted);
  if (got == 0) {
    Serial.printf("[AUDIO] SD read failed at %u\n", static_cast<unsigned>(audioFile.position()));
    audioDataRemaining = 0;
    return;
  }
  audioDataRemaining -= got;
  xStreamBufferSend(audioStream, audioReadBuffer, got, 0);
}

bool startAudio(const String &path) {
  stopAudio();
  audioFile = SD.open(path.c_str(), FILE_READ);
  if (!audioFile) {
    Serial.printf("[AUDIO] Missing: %s\n", path.c_str());
    return false;
  }
  uint8_t header[12];
  if (audioFile.read(header, sizeof(header)) != sizeof(header) ||
      memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
    Serial.printf("[AUDIO] Invalid RIFF/WAVE: %s\n", path.c_str());
    audioFile.close();
    return false;
  }
  bool validFormat = false;
  bool foundData = false;
  while (audioFile.available()) {
    uint8_t chunk[8];
    if (audioFile.read(chunk, sizeof(chunk)) != sizeof(chunk)) break;
    const uint32_t length = readLe32(chunk + 4);
    const uint32_t next = audioFile.position() + length + (length & 1U);
    if (memcmp(chunk, "fmt ", 4) == 0 && length >= 16) {
      uint8_t format[16];
      if (audioFile.read(format, sizeof(format)) != sizeof(format)) break;
      validFormat = format[0] == 1 && format[1] == 0 &&
                    format[2] == 1 && format[3] == 0 &&
                    readLe32(format + 4) == 24000 &&
                    format[14] == 16 && format[15] == 0;
    } else if (memcmp(chunk, "data", 4) == 0) {
      if (!validFormat) break;
      audioDataRemaining = min(length, uint32_t(audioFile.size() - audioFile.position()));
      foundData = true;
      break;
    }
    if (!audioFile.seek(next)) break;
  }
  if (!foundData || !validFormat || audioDataRemaining == 0) {
    Serial.printf("[AUDIO] Unsupported WAV: %s\n", path.c_str());
    audioFile.close();
    return false;
  }
  audioUnderruns = 0;
  audioServiceEnabled = true;
  for (int i = 0; i < 4; ++i) serviceAudio();
  Serial.printf("[AUDIO] opened %s PCM bytes=%u\n", path.c_str(),
                static_cast<unsigned>(audioDataRemaining));
  return true;
}

bool playbackStopRequested() {
  if (buttonPressed(buttonSelect)) {
    Serial.println("[BUTTON] SELECT stop playback");
    playbackStopReason = "select";
    buttonSelect.longReported = true; // Ignore this release in the video menu.
    playbackInterrupted = true;
    return true;
  }
  if (serialStopRequested()) {
    playbackStopReason = "serial-x";
    buttonSelect.longReported = true; // Ignore this release in the video menu.
    playbackInterrupted = true;
    return true;
  }
  return false;
}

int jpegDraw(JPEGDRAW *draw) {
  const uint32_t started = micros();
  tft.pushImage(draw->x, draw->y, draw->iWidth, draw->iHeight, draw->pPixels);
  lcdPushUs += micros() - started;
  return 1;
}

bool refillVideoBuffer(File &file) {
  if (videoEof || bufferEnd >= MJPEG_BUFFER_SIZE) {
    return false;
  }

  // Both SD files are read by this task; the audio task only writes I2S.
  const size_t freeBytes = min(MJPEG_BUFFER_SIZE - bufferEnd, size_t(16U * 1024U));
  const uint32_t started = micros();
  const size_t bytesRead = file.read(mjpegBuffer + bufferEnd, freeBytes);
  const size_t filePosition = file.position();
  const size_t fileSize = file.size();
  videoSdReadUs += micros() - started;
  videoSdBytes += bytesRead;
  ++videoSdReadCalls;
  bufferEnd += bytesRead;

  if (bytesRead == 0) {
    videoEof = true;
    if (filePosition < fileSize) {
      playbackStopReason = "sd-read-failed";
      Serial.printf("[SD] Unexpected zero read at %u/%u\n",
                    static_cast<unsigned>(filePosition), static_cast<unsigned>(fileSize));
    }
  }
  return bytesRead > 0;
}

void compactVideoBuffer() {
  if (bufferBegin == 0) {
    return;
  }

  const size_t remaining = bufferEnd - bufferBegin;
  if (remaining > 0) {
    memmove(mjpegBuffer, mjpegBuffer + bufferBegin, remaining);
  }
  bufferBegin = 0;
  bufferEnd = remaining;
}

// Returns a pointer that remains valid until the next refill/compact operation.
bool nextJpegFrame(File &file, uint8_t *&frameData, size_t &frameSize) {
  for (;;) {
    if (playbackStopRequested()) return false;
    size_t soi = SIZE_MAX;

    for (size_t i = bufferBegin; i + 1 < bufferEnd; ++i) {
      if ((i & 0x3FF) == 0 && playbackStopRequested()) return false;
      if (mjpegBuffer[i] == 0xFF && mjpegBuffer[i + 1] == 0xD8) {
        soi = i;
        break;
      }
    }

    if (soi == SIZE_MAX) {
      if (videoEof) {
        return false;
      }

      // Preserve a trailing 0xFF in case the SOI marker crosses a read boundary.
      if (bufferEnd > bufferBegin && mjpegBuffer[bufferEnd - 1] == 0xFF) {
        mjpegBuffer[0] = 0xFF;
        bufferBegin = 0;
        bufferEnd = 1;
      } else {
        bufferBegin = 0;
        bufferEnd = 0;
      }
      refillVideoBuffer(file);
      continue;
    }

    for (size_t i = soi + 2; i + 1 < bufferEnd; ++i) {
      if ((i & 0x3FF) == 0 && playbackStopRequested()) return false;
      if (mjpegBuffer[i] == 0xFF && mjpegBuffer[i + 1] == 0xD9) {
        frameData = mjpegBuffer + soi;
        frameSize = i + 2 - soi;
        bufferBegin = i + 2;
        return true;
      }
    }

    if (videoEof) {
      if (strcmp(playbackStopReason, "end-of-file") == 0) playbackStopReason = "truncated-jpeg";
      Serial.println("[VIDEO] Truncated JPEG at end of file");
      return false;
    }

    // Keep the incomplete frame; compact only when more tail room is needed.
    bufferBegin = soi;
    compactVideoBuffer();

    if (bufferEnd == MJPEG_BUFFER_SIZE) {
      playbackStopReason = "oversized-jpeg";
      Serial.printf("[VIDEO] JPEG frame exceeds %u bytes\n",
                    static_cast<unsigned>(MJPEG_BUFFER_SIZE));
      return false;
    }
    refillVideoBuffer(file);
  }
}

void resetVideoReader(File &file) {
  bufferBegin = 0;
  bufferEnd = 0;
  videoEof = false;
  refillVideoBuffer(file);
}

bool hasMjpegExtension(const String &name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".mjpeg");
}

void sortFiles() {
  // Deterministic case-insensitive lexical order. Numeric prefixes (001_, 002_)
  // provide the intended playlist order without heap-heavy natural-sort code.
  for (size_t i = 1; i < fileCount; ++i) {
    String key = fileList[i];
    String keyLower = key;
    keyLower.toLowerCase();
    int j = static_cast<int>(i) - 1;

    while (j >= 0) {
      String currentLower = fileList[j];
      currentLower.toLowerCase();
      if (currentLower <= keyLower) {
        break;
      }
      fileList[j + 1] = fileList[j];
      --j;
    }
    fileList[j + 1] = key;
  }
}

void scanFiles() {
  fileCount = 0;
  selectedIndex = 0;

  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    Serial.println("[SD] Cannot open root");
    return;
  }

  for (File file = root.openNextFile(); file && fileCount < MAX_FILES;
       file = root.openNextFile()) {
    String name(file.name());
    while (name.startsWith("/")) name.remove(0, 1);
    if (!file.isDirectory() && !name.startsWith(".") && hasMjpegExtension(name)) {
      fileList[fileCount++] = name;
    }
  }
  root.close();
  sortFiles();
  Serial.printf("[SD] Found %u MJPEG files\n", static_cast<unsigned>(fileCount));
}

void drawMenu() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(0, 8);
  tft.println("SELECT VIDEO");
  tft.drawFastHLine(0, 32, tft.width(), TFT_BLUE);

  const int maxVisible = (tft.height() - 60) / 22;
  int first = 0;
  if (selectedIndex >= maxVisible) {
    first = selectedIndex - maxVisible + 1;
  }

  for (int row = 0; row < maxVisible; ++row) {
    const int index = first + row;
    if (index >= static_cast<int>(fileCount)) {
      break;
    }

    tft.setCursor(4, 42 + row * 22);
    tft.setTextColor(index == selectedIndex ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    tft.print(index == selectedIndex ? "> " : "  ");
    tft.println(fileList[index]);
  }
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(2, tft.height() - 9);
  tft.print("Hold SELECT 1s: settings");
}

void drawSettings() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(8, 8);
  tft.println("SETTINGS");
  tft.drawFastHLine(0, 32, tft.width(), TFT_BLUE);
  const String rows[] = {
      "VOLUME " + String(settings.volume),
      String("AUTO ") + (settings.autoStart ? "ON" : "OFF"),
      String("PLAY ") + (settings.repeat ? "REPEAT" : "ONE"),
      String("FILES ") + (settings.multi ? "MULTI" : "ONE"),
      String("ORDER ") + (settings.randomOrder ? "RANDOM" : "SEQ"),
      "BACK"};
  for (uint8_t row = 0; row < 6; ++row) {
    tft.setCursor(4, 40 + row * 29);
    tft.setTextColor(row == settingRow ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    tft.print(row == settingRow ? ">" : " ");
    tft.print(rows[row]);
    if (row == 0 && editingVolume) tft.print(" *");
  }
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setCursor(4, 220);
  tft.print(editingVolume ? "UP/DOWN: volume  SEL: done" : "UP/DOWN: item  SEL: change");
}

void leaveSettings() {
  editingVolume = false;
  screen = Screen::Videos;
  drawMenu();
  menuGuardUntilMs = millis() + 250;
}

void changeSetting(int direction) {
  if (settingRow == 0 && editingVolume) {
    const int next = constrain(static_cast<int>(settings.volume) + direction, 0, 21);
    settings.volume = static_cast<uint8_t>(next);
    saveSettings();
    drawSettings();
  } else {
    settingRow = (settingRow + 6 + direction) % 6;
    drawSettings();
  }
}

void selectSetting() {
  switch (settingRow) {
    case 0: editingVolume = !editingVolume; break;
    case 1: settings.autoStart = !settings.autoStart; break;
    case 2: settings.repeat = !settings.repeat; break;
    case 3: settings.multi = !settings.multi; break;
    case 4: settings.randomOrder = !settings.randomOrder; break;
    default: leaveSettings(); return;
  }
  saveSettings();
  drawSettings();
}

PlaybackResult playVideo(const String &videoName) {
  String videoPath = "/" + videoName;
  String audioPath = videoPath;
  const int extensionAt = audioPath.lastIndexOf('.');
  if (extensionAt >= 0) {
    audioPath = audioPath.substring(0, extensionAt) + ".wav";
  }

  playbackInterrupted = false;
  playbackStopReason = "end-of-file";
  // Read both SD files on this task. The audio task only consumes buffered PCM.
  const bool hasAudio = playbackAudio && startAudio(audioPath);

  File videoFile = SD.open(videoPath.c_str(), FILE_READ);
  if (!videoFile) {
    Serial.printf("[VIDEO] Open failed: %s\n", videoPath.c_str());
    if (hasAudio) stopAudio();
    return PlaybackResult::Corrupt;
  }

  videoSdBytes = 0;
  videoSdReadUs = 0;
  videoSdReadCalls = 0;
  lcdPushUs = 0;
  resetVideoReader(videoFile);

  isPlaying = true;
  tft.fillScreen(TFT_BLACK);

  // Give the audio reader/I2S ring buffer a short head start.
  if (hasAudio) {
    const uint32_t prerollUntil = millis() + AUDIO_PREROLL_MS;
    while ((int32_t)(millis() - prerollUntil) < 0) {
      if (playbackStopRequested()) {
        isPlaying = false;
        break;
      }
      delay(1);
    }
  }

  uint32_t nextFrameAt = micros();
  const uint32_t framePeriodUs = 1000000UL / playbackFps;
  uint32_t decodedFrames = 0;
  uint32_t droppedFrames = 0;
  uint8_t consecutiveJpegFailures = 0;
  uint32_t decodeUs = 0;
  uint32_t lastLogMs = millis();
  uint32_t lastDecoded = 0;
  uint32_t lastDropped = 0;
  uint32_t lastSdBytes = 0;
  uint32_t lastSdReadUs = 0;
  uint32_t lastSdReadCalls = 0;
  uint32_t lastDecodeUs = 0;
  uint32_t lastLcdUs = 0;

  auto logMetrics = [&](bool final) {
    const uint32_t nowMs = millis();
    const uint32_t elapsed = nowMs - lastLogMs;
    if (!final && elapsed < 5000) return;
    if (elapsed == 0) return;
    const bool audioRunning = hasAudio && audioServiceEnabled &&
                              (audioDataRemaining || xStreamBufferBytesAvailable(audioStream));
    const uint32_t dFrames = decodedFrames - lastDecoded;
    const uint32_t dDrops = droppedFrames - lastDropped;
    const uint32_t dSdBytes = videoSdBytes - lastSdBytes;
    const uint32_t dSdUs = videoSdReadUs - lastSdReadUs;
    const uint32_t dCalls = videoSdReadCalls - lastSdReadCalls;
    const uint32_t dDecodeUs = decodeUs - lastDecodeUs;
    const uint32_t dLcdUs = lcdPushUs - lastLcdUs;
    Serial.printf("[METRIC] ms=%lu decoded=%lu dropped=%lu dropped_window=%lu fps_x100=%lu audio=%d underrun=%lu heap=%u psram=%u sd_KBps=%lu sd_read_us=%lu jpeg_us=%lu lcd_us=%lu%s\n",
                  static_cast<unsigned long>(nowMs),
                  static_cast<unsigned long>(decodedFrames),
                  static_cast<unsigned long>(droppedFrames),
                  static_cast<unsigned long>(dDrops),
                  static_cast<unsigned long>(dFrames * 100000UL / elapsed),
                  audioRunning,
                  static_cast<unsigned long>(audioUnderruns),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram(),
                  static_cast<unsigned long>(static_cast<uint64_t>(dSdBytes) * 1000ULL / elapsed / 1024ULL),
                  static_cast<unsigned long>(dCalls ? dSdUs / dCalls : 0),
                  static_cast<unsigned long>(dFrames ? dDecodeUs / dFrames : 0),
                  static_cast<unsigned long>(dFrames ? dLcdUs / dFrames : 0),
                  final ? " final" : "");
    lastLogMs = nowMs;
    lastDecoded = decodedFrames;
    lastDropped = droppedFrames;
    lastSdBytes = videoSdBytes;
    lastSdReadUs = videoSdReadUs;
    lastSdReadCalls = videoSdReadCalls;
    lastDecodeUs = decodeUs;
    lastLcdUs = lcdPushUs;
  };

  while (isPlaying) {
    serviceAudio();
    uint8_t *frameData = nullptr;
    size_t frameSize = 0;
    if (!nextJpegFrame(videoFile, frameData, frameSize)) {
      break;
    }

    const uint32_t now = micros();
    const int32_t lateness = static_cast<int32_t>(now - nextFrameAt);

    // Audio is the priority. If more than one frame late, discard this JPEG.
    if (lateness > static_cast<int32_t>(framePeriodUs)) {
      ++droppedFrames;
      nextFrameAt += framePeriodUs;
    } else {
      while ((int32_t)(micros() - nextFrameAt) < 0) {
        if (playbackStopRequested()) {
          isPlaying = false;
          break;
        }
        serviceAudio();
        delay(1);
      }
      if (!isPlaying) {
        break;
      }

      if (jpeg.openRAM(frameData, frameSize, jpegDraw)) {
        const int x = (tft.width() - jpeg.getWidth()) / 2;
        const int y = (tft.height() - jpeg.getHeight()) / 2;
        const uint32_t decodeStarted = micros();
        jpeg.decode(x, y, 0);
        decodeUs += micros() - decodeStarted;
        jpeg.close();
        ++decodedFrames;
        consecutiveJpegFailures = 0;
      } else {
        Serial.printf("[VIDEO] JPEG open failed, size=%u\n",
                      static_cast<unsigned>(frameSize));
        if (++consecutiveJpegFailures >= 3) {
          playbackStopReason = "jpeg-invalid";
          break;
        }
      }
      nextFrameAt += framePeriodUs;
    }

    if (playbackStopRequested()) isPlaying = false;
    logMetrics(false);
    delay(1);
  }

  isPlaying = false;
  logMetrics(true);
  stopAudio();
  delay(50);  // Let I2S/FAT teardown settle before another file can start.
  const size_t stoppedAt = videoFile.position();
  const size_t fileSize = videoFile.size();
  videoFile.close();

  Serial.printf("[VIDEO] done, reason=%s pos=%u/%u decoded=%lu dropped=%lu\n",
                playbackStopReason, static_cast<unsigned>(stoppedAt),
                static_cast<unsigned>(fileSize),
                static_cast<unsigned long>(decodedFrames),
                static_cast<unsigned long>(droppedFrames));
  if (playbackInterrupted) return PlaybackResult::Interrupted;
  if (strcmp(playbackStopReason, "end-of-file") != 0) return PlaybackResult::Corrupt;
  return PlaybackResult::Completed;
}

void playPlaylist() {
  if (fileCount == 0) return;
  autoStartAtMs = 0;
  saveSelectedVideo();
  const int first = selectedIndex;
  const size_t count = settings.multi ? fileCount : 1;
  int order[MAX_FILES];
  for (size_t i = 0; i < count; ++i) {
    order[i] = (first + static_cast<int>(i)) % static_cast<int>(fileCount);
  }
  bool firstPass = true;
  do {
    if (settings.multi && settings.randomOrder) {
      // The selected video leads the first cycle; later cycles shuffle all files.
      const size_t start = firstPass ? 1 : 0;
      for (size_t i = count; i > start + 1; --i) {
        const size_t j = start + (esp_random() % (i - start));
        const int temp = order[i - 1];
        order[i - 1] = order[j];
        order[j] = temp;
      }
    }
    for (size_t i = 0; i < count; ++i) {
      if (!SD.exists(("/" + fileList[order[i]]).c_str())) {
        Serial.println("[PLAYLIST] Video missing; stopping playlist");
        drawMenu();
        return;
      }
      Serial.printf("[PLAYLIST] %u/%u %s\n", static_cast<unsigned>(i + 1),
                    static_cast<unsigned>(count), fileList[order[i]].c_str());
      PlaybackResult result = playVideo(fileList[order[i]]);
      if (result == PlaybackResult::Corrupt) {
        Serial.printf("[PLAYLIST] Retry once: %s\n", fileList[order[i]].c_str());
        delay(500);
        result = playVideo(fileList[order[i]]);
      }
      if (result != PlaybackResult::Completed) {
        drawMenu();
        menuGuardUntilMs = millis() + 300;
        return;
      }
    }
    firstPass = false;
  } while (settings.repeat);
  drawMenu();
  menuGuardUntilMs = millis() + 300;
}

bool setSdClock(uint32_t frequency) {
  if (sdFrequency == frequency) return true;
  const String selectedName = fileCount ? fileList[selectedIndex] : "";
  SD.end();
  if (!SD.begin(SD_CS, sdSPI, frequency, "/sd", 5, false)) {
    Serial.printf("[SD] remount failed at %lu MHz\n", static_cast<unsigned long>(frequency / 1000000));
    SD.begin(SD_CS, sdSPI, SD_FREQUENCY, "/sd", 5, false);
    sdFrequency = SD_FREQUENCY;
    return false;
  }
  sdFrequency = frequency;
  scanFiles();
  for (size_t i = 0; i < fileCount; ++i) {
    if (fileList[i] == selectedName) { selectedIndex = static_cast<int>(i); break; }
  }
  Serial.printf("[SD] clock=%lu MHz\n", static_cast<unsigned long>(sdFrequency / 1000000));
  return true;
}

void playAudioOnly(const String &videoName) {
  String path = "/" + videoName;
  path = path.substring(0, path.lastIndexOf('.')) + ".wav";
  if (!startAudio(path)) return;
  Serial.printf("[TEST] audio-only %s\n", path.c_str());
  const uint32_t started = millis();
  uint32_t lastLog = started;
  const char *stopReason = "unknown";
  for (;;) {
    if (serialStopRequested()) { stopReason = "serial"; break; }
    if (millis() - started >= 180000) { stopReason = "duration"; break; }
    serviceAudio();
    const bool running = audioDataRemaining || xStreamBufferBytesAvailable(audioStream);
    if (!running) { stopReason = "audio-not-running"; break; }
    if (millis() - lastLog >= 5000) {
      lastLog = millis();
      Serial.printf("[METRIC] audio-only elapsed_ms=%lu running=%d underrun=%lu heap=%u psram=%u\n",
                  static_cast<unsigned long>(lastLog - started), running,
                  static_cast<unsigned long>(audioUnderruns),
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL), ESP.getFreePsram());
    }
    delay(10);
  }
  stopAudio();
  Serial.printf("[TEST] audio-only done elapsed_ms=%lu reason=%s\n",
                static_cast<unsigned long>(millis() - started), stopReason);
  drawMenu();
}

void runProfile(char profile) {
  if (fileCount == 0) { Serial.println("[TEST] no MJPEG files"); return; }
  if (profile == 'A' || profile == 'B' || profile == 'C' ||
      profile == 'D' || profile == 'E') {
    if (!setSdClock(20000000UL)) return;
  } else if (profile == 'F') {
    if (!setSdClock(40000000UL)) return;
  }
  playbackFps = (profile == 'A' || profile == 'D') ? 10 : 15;
  playbackAudio = (profile == 'C' || profile == 'D' || profile == 'E' || profile == 'F');
  Serial.printf("[TEST] profile=%c file=%s fps=%u SD_MHz=%lu audio=%d\n",
                profile, fileList[selectedIndex].c_str(), playbackFps,
                static_cast<unsigned long>(sdFrequency / 1000000), playbackAudio);
  if (profile == 'C') playAudioOnly(fileList[selectedIndex]);
  else { playVideo(fileList[selectedIndex]); drawMenu(); }
  if (profile == 'F') setSdClock(20000000UL);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_POWER_LATCH, OUTPUT);
  digitalWrite(PIN_POWER_LATCH, HIGH);
  pinMode(PIN_LCD_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_LCD_BACKLIGHT, HIGH);

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  pinMode(PIN_BTN_SEL, INPUT_PULLUP);
  loadSettings();

  tft.init();
  tft.setRotation(TFT_ROTATION);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  if (!psramInit()) {
    Serial.println("[MEM] PSRAM init failed");
  }
  mjpegBuffer = static_cast<uint8_t *>(ps_malloc(MJPEG_BUFFER_SIZE));
  if (!mjpegBuffer) {
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.println("PSRAM FAIL");
    Serial.println("[MEM] MJPEG buffer allocation failed");
    while (true) {
      delay(1000);
    }
  }

  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI, SD_FREQUENCY, "/sd", 5, false)) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.println("SD FAIL");
    Serial.println("[SD] Mount failed at 20 MHz");
    while (true) {
      delay(1000);
    }
  }

  audioStream = xStreamBufferCreate(32768, 1);
  if (!audioStream) {
    Serial.println("[AUDIO] Cannot create PCM buffer");
    while (true) delay(1000);
  }
  i2s_config_t audioConfig = {};
  audioConfig.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  audioConfig.sample_rate = 24000;
  audioConfig.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  audioConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  audioConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  audioConfig.dma_buf_count = 8;
  audioConfig.dma_buf_len = 240;
  audioConfig.tx_desc_auto_clear = true;
  i2s_pin_config_t audioPins = {};
  audioPins.mck_io_num = I2S_PIN_NO_CHANGE; // GPIO0 is the SELECT button.
  audioPins.bck_io_num = I2S_BCLK;
  audioPins.ws_io_num = I2S_LRC;
  audioPins.data_out_num = I2S_DOUT;
  audioPins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_driver_install(I2S_NUM_1, &audioConfig, 0, nullptr) != ESP_OK ||
      i2s_set_pin(I2S_NUM_1, &audioPins) != ESP_OK) {
    Serial.println("[AUDIO] I2S init failed");
    while (true) delay(1000);
  }

  BaseType_t taskResult = xTaskCreatePinnedToCore(
      audioServiceTask, "audio-service", 8192, nullptr, 3,
      &audioTaskHandle, 0);
  if (taskResult != pdPASS) {
    Serial.println("[AUDIO] Cannot create service task");
    while (true) delay(1000);
  }

  scanFiles();
  const String savedVideo = preferences.getString("selected", "");
  for (size_t i = 0; i < fileCount; ++i) {
    if (fileList[i] == savedVideo) { selectedIndex = static_cast<int>(i); break; }
  }
  drawMenu();
  if (settings.autoStart && fileCount > 0) autoStartAtMs = millis() + 1000;
  Serial.printf("[SETTINGS] volume=%u auto=%d repeat=%d multi=%d random=%d\n",
                settings.volume, settings.autoStart, settings.repeat,
                settings.multi, settings.randomOrder);
  Serial.println("[TEST] 0-9 select file; A-F profiles; p play; s SELECT; u UP; d DOWN; m long SELECT; x stop; ? button state");
}

void loop() {
  if (isPlaying) {
    delay(1);
    return;
  }

  if (static_cast<int32_t>(millis() - menuGuardUntilMs) < 0) {
    buttonPressed(buttonDown);
    buttonPressed(buttonUp);
    pollSelect();
    delay(1);
    return;
  }

  const char command = Serial.available() ? static_cast<char>(Serial.read()) : 0;
  if (command == '?') {
    Serial.printf("[BUTTON] SELECT raw=%d stable=%d long=%d screen=%d\n",
                  digitalRead(PIN_BTN_SEL), buttonSelect.stableLevel,
                  buttonSelect.longReported, static_cast<int>(screen));
  }
  SelectEvent select = pollSelect();
  if (command == 's') select = SelectEvent::Short;
  if (command == 'm') select = SelectEvent::Long;
  if (screen == Screen::Settings) {
    if (buttonPressed(buttonDown) || command == 'd') changeSetting(1);
    if (buttonPressed(buttonUp) || command == 'u') changeSetting(-1);
    if (select == SelectEvent::Short) selectSetting();
    if (select == SelectEvent::Long) leaveSettings();
    delay(1);
    return;
  }

  if (select == SelectEvent::Long) {
    autoStartAtMs = 0;
    screen = Screen::Settings;
    settingRow = 0;
    editingVolume = false;
    drawSettings();
    delay(1);
    return;
  }

  if ((buttonPressed(buttonDown) || command == 'd') && fileCount > 0) {
    selectedIndex = (selectedIndex + 1) % static_cast<int>(fileCount);
    saveSelectedVideo();
    drawMenu();
  }

  if ((buttonPressed(buttonUp) || command == 'u') && fileCount > 0) {
    selectedIndex =
        (selectedIndex - 1 + static_cast<int>(fileCount)) % static_cast<int>(fileCount);
    saveSelectedVideo();
    drawMenu();
  }

  if (select == SelectEvent::Short && fileCount > 0) {
    playbackFps = TARGET_FPS;
    playbackAudio = true;
    playPlaylist();
  }

  if (command) {
    if (command >= '0' && command <= '9') {
      const int index = command - '0';
      if (index < static_cast<int>(fileCount)) {
        selectedIndex = index;
        saveSelectedVideo();
        Serial.printf("[TEST] selected index=%d file=%s\n", index, fileList[index].c_str());
        drawMenu();
      }
    } else if (command >= 'A' && command <= 'F') {
      runProfile(command);
    } else if (command == 'p' && fileCount > 0) {
      playbackFps = TARGET_FPS;
      playbackAudio = true;
      playPlaylist();
    }
  }

  if (autoStartAtMs && static_cast<int32_t>(millis() - autoStartAtMs) >= 0) {
    playbackFps = TARGET_FPS;
    playbackAudio = true;
    playPlaylist();
  }

  delay(1);
}
