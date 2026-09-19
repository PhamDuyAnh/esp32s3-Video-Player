/*
 * esp32s3-Video-Player
 * Target: xingzhi-cube-1.54tft-wifi
 *
 * Media pair on SD root:
 *   001_demo.mjpeg  (raw baseline JPEG stream, 240x240, TARGET_FPS)
 *   001_demo.wav    (PCM s16le, mono, 24000 Hz)
 *
 * Status: code-review prototype; hardware build/test is still required.
 */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <JPEGDEC.h>
#include "Audio.h"
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
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
static constexpr uint8_t DEFAULT_VOLUME = 6; // ESP32-audioI2S range: 0..21
static constexpr uint8_t TARGET_FPS = 15;
static constexpr size_t MJPEG_BUFFER_SIZE = 96U * 1024U;
static constexpr size_t MAX_FILES = 50;
static constexpr uint32_t SD_FREQUENCY = 20000000UL;
static constexpr uint32_t AUDIO_PREROLL_MS = 120;

TFT_eSPI tft;
JPEGDEC jpeg;
Audio audio;
SPIClass sdSPI(HSPI);

void audio_info(const char *message) {
  Serial.printf("[AUDIO-LIB] %s\n", message);
}

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
SemaphoreHandle_t mediaMutex = nullptr;
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
    if (Serial.read() == 'x') return true;
  }
  return false;
}

struct ButtonState {
  uint8_t pin;
  bool stableLevel;
  bool lastRawLevel;
  uint32_t changedAtMs;
};

bool buttonPressed(ButtonState &button);

ButtonState buttonUp{PIN_BTN_UP, HIGH, HIGH, 0};
ButtonState buttonDown{PIN_BTN_DOWN, HIGH, HIGH, 0};
ButtonState buttonSelect{PIN_BTN_SEL, HIGH, HIGH, 0};

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

void audioServiceTask(void *) {
  for (;;) {
    if (audioServiceEnabled) {
      if (xSemaphoreTake(mediaMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        audio.loop();
        xSemaphoreGive(mediaMutex);
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
}

bool startAudio(const String &path) {
  audioServiceEnabled = false;
  xSemaphoreTake(mediaMutex, portMAX_DELAY);
  audio.stopSong();

  if (!SD.exists(path.c_str())) {
    Serial.printf("[AUDIO] Missing: %s\n", path.c_str());
    xSemaphoreGive(mediaMutex);
    return false;
  }

  const bool connected = audio.connecttoFS(SD, path.c_str());
  if (!connected) {
    Serial.printf("[AUDIO] Open failed: %s\n", path.c_str());
    xSemaphoreGive(mediaMutex);
    return false;
  }

  audioServiceEnabled = true;
  Serial.printf("[AUDIO] opened %s running=%d size=%lu pos=%lu\n", path.c_str(),
                audio.isRunning(), static_cast<unsigned long>(audio.getFileSize()),
                static_cast<unsigned long>(audio.getFilePos()));
  xSemaphoreGive(mediaMutex);
  return true;
}

bool playbackStopRequested() {
  return buttonPressed(buttonSelect) || serialStopRequested();
}

void stopAudio() {
  audioServiceEnabled = false;
  xSemaphoreTake(mediaMutex, portMAX_DELAY);
  audio.stopSong();
  xSemaphoreGive(mediaMutex);
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

  // Short locked reads prevent the video core from monopolizing FAT/SPI while
  // the audio task also reads the same SD volume.
  const size_t freeBytes = min(MJPEG_BUFFER_SIZE - bufferEnd, size_t(16U * 1024U));
  const uint32_t started = micros();
  if (xSemaphoreTake(mediaMutex, pdMS_TO_TICKS(250)) != pdTRUE) {
    Serial.println("[SD] video read mutex timeout");
    return false;
  }
  const size_t bytesRead = file.read(mjpegBuffer + bufferEnd, freeBytes);
  xSemaphoreGive(mediaMutex);
  videoSdReadUs += micros() - started;
  videoSdBytes += bytesRead;
  ++videoSdReadCalls;
  bufferEnd += bytesRead;

  if (bytesRead == 0) {
    videoEof = true;
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
      Serial.println("[VIDEO] Truncated JPEG at end of file");
      return false;
    }

    // Keep the incomplete frame; compact only when more tail room is needed.
    bufferBegin = soi;
    compactVideoBuffer();

    if (bufferEnd == MJPEG_BUFFER_SIZE) {
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

  const int maxVisible = (tft.height() - 42) / 22;
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
}

void playVideo(const String &videoName) {
  String videoPath = "/" + videoName;
  String audioPath = videoPath;
  const int extensionAt = audioPath.lastIndexOf('.');
  if (extensionAt >= 0) {
    audioPath = audioPath.substring(0, extensionAt) + ".wav";
  }

  File videoFile = SD.open(videoPath.c_str(), FILE_READ);
  if (!videoFile) {
    Serial.printf("[VIDEO] Open failed: %s\n", videoPath.c_str());
    return;
  }

  videoSdBytes = 0;
  videoSdReadUs = 0;
  videoSdReadCalls = 0;
  lcdPushUs = 0;
  resetVideoReader(videoFile);
  const bool hasAudio = playbackAudio && startAudio(audioPath);

  isPlaying = true;
  tft.fillScreen(TFT_BLACK);

  // Give the audio reader/I2S ring buffer a short head start.
  if (hasAudio) {
    const uint32_t prerollUntil = millis() + AUDIO_PREROLL_MS;
    while ((int32_t)(millis() - prerollUntil) < 0) {
      if (buttonPressed(buttonSelect) || serialStopRequested()) {
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
    bool audioRunning = false;
    if (hasAudio && xSemaphoreTake(mediaMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      audioRunning = audio.isRunning();
      xSemaphoreGive(mediaMutex);
    }
    const uint32_t dFrames = decodedFrames - lastDecoded;
    const uint32_t dDrops = droppedFrames - lastDropped;
    const uint32_t dSdBytes = videoSdBytes - lastSdBytes;
    const uint32_t dSdUs = videoSdReadUs - lastSdReadUs;
    const uint32_t dCalls = videoSdReadCalls - lastSdReadCalls;
    const uint32_t dDecodeUs = decodeUs - lastDecodeUs;
    const uint32_t dLcdUs = lcdPushUs - lastLcdUs;
    Serial.printf("[METRIC] ms=%lu decoded=%lu dropped=%lu dropped_window=%lu fps_x100=%lu audio=%d underrun=na heap=%u psram=%u sd_KBps=%lu sd_read_us=%lu jpeg_us=%lu lcd_us=%lu%s\n",
                  static_cast<unsigned long>(nowMs),
                  static_cast<unsigned long>(decodedFrames),
                  static_cast<unsigned long>(droppedFrames),
                  static_cast<unsigned long>(dDrops),
                  static_cast<unsigned long>(dFrames * 100000UL / elapsed),
                  audioRunning,
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
        if (buttonPressed(buttonSelect) || serialStopRequested()) {
          isPlaying = false;
          break;
        }
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
      } else {
        Serial.printf("[VIDEO] JPEG open failed, size=%u\n",
                      static_cast<unsigned>(frameSize));
      }
      nextFrameAt += framePeriodUs;
    }

    if (buttonPressed(buttonSelect)) {
      isPlaying = false;
    }
    if (serialStopRequested()) isPlaying = false;
    logMetrics(false);
    delay(1);
  }

  isPlaying = false;
  logMetrics(true);
  stopAudio();
  delay(50);  // Let I2S/FAT teardown settle before another file can start.
  videoFile.close();

  Serial.printf("[VIDEO] done, decoded=%lu dropped=%lu\n",
                static_cast<unsigned long>(decodedFrames),
                static_cast<unsigned long>(droppedFrames));
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
    bool running = false;
    if (xSemaphoreTake(mediaMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      running = audio.isRunning();
      xSemaphoreGive(mediaMutex);
    }
    if (!running) { stopReason = "audio-not-running"; break; }
    if (millis() - lastLog >= 5000) {
      lastLog = millis();
      Serial.printf("[METRIC] audio-only elapsed_ms=%lu running=%d underrun=na heap=%u psram=%u\n",
                    static_cast<unsigned long>(lastLog - started), running,
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
  else playVideo(fileList[selectedIndex]);
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

  mediaMutex = xSemaphoreCreateMutex();
  if (!mediaMutex) {
    Serial.println("[AUDIO] Cannot create mutex");
    while (true) delay(1000);
  }
  Serial.printf("[AUDIO] pinout=%d volume=%u\n",
                audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT), DEFAULT_VOLUME);
  audio.setVolume(DEFAULT_VOLUME);

  BaseType_t taskResult = xTaskCreatePinnedToCore(
      audioServiceTask,
      "audio-service",
      8192,
      nullptr,
      3,
      &audioTaskHandle,
      0);

  if (taskResult != pdPASS) {
    Serial.println("[AUDIO] Cannot create service task");
    while (true) {
      delay(1000);
    }
  }

  scanFiles();
  drawMenu();
  Serial.println("[TEST] Select file with digit 0-9; run A/B/C/D/E/F; x stops playback; p plays selected file");
}

void loop() {
  if (isPlaying) {
    delay(1);
    return;
  }

  if (static_cast<int32_t>(millis() - menuGuardUntilMs) < 0) {
    buttonPressed(buttonDown);
    buttonPressed(buttonUp);
    buttonPressed(buttonSelect);
    delay(1);
    return;
  }

  if (buttonPressed(buttonDown) && fileCount > 0) {
    selectedIndex = (selectedIndex + 1) % static_cast<int>(fileCount);
    drawMenu();
  }

  if (buttonPressed(buttonUp) && fileCount > 0) {
    selectedIndex =
        (selectedIndex - 1 + static_cast<int>(fileCount)) % static_cast<int>(fileCount);
    drawMenu();
  }

  if (buttonPressed(buttonSelect) && fileCount > 0) {
    playVideo(fileList[selectedIndex]);
  }

  if (Serial.available()) {
    const char command = static_cast<char>(Serial.read());
    if (command >= '0' && command <= '9') {
      const int index = command - '0';
      if (index < static_cast<int>(fileCount)) {
        selectedIndex = index;
        Serial.printf("[TEST] selected index=%d file=%s\n", index, fileList[index].c_str());
        drawMenu();
      }
    } else if (command >= 'A' && command <= 'F') {
      runProfile(command);
    } else if (command == 'p' && fileCount > 0) {
      playbackFps = TARGET_FPS;
      playbackAudio = true;
      playVideo(fileList[selectedIndex]);
    }
  }

  delay(1);
}
