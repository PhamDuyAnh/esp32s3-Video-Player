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

// Buttons
static constexpr gpio_num_t PIN_BTN_UP   = GPIO_NUM_40;
static constexpr gpio_num_t PIN_BTN_DOWN = GPIO_NUM_39;
static constexpr gpio_num_t PIN_BTN_SEL  = GPIO_NUM_0;

// Dedicated TF/SD SPI observed in the working sample.
static constexpr int SD_MISO = 1;
static constexpr int SD_MOSI = 2;
static constexpr int SD_SCK  = 3;
static constexpr int SD_CS   = 46;

// I2S speaker
static constexpr int I2S_BCLK = 15;
static constexpr int I2S_LRC  = 16;
static constexpr int I2S_DOUT = 7;

// Board control
static constexpr int PIN_LCD_BACKLIGHT = 13;
static constexpr int PIN_POWER_LATCH   = 21;

static constexpr uint8_t TFT_ROTATION = 4;
static constexpr uint8_t DEFAULT_VOLUME = 6; // ESP32-audioI2S range: 0..21
static constexpr uint8_t TARGET_FPS = 15;
static constexpr uint32_t FRAME_PERIOD_US = 1000000UL / TARGET_FPS;
static constexpr size_t MJPEG_BUFFER_SIZE = 96U * 1024U;
static constexpr size_t MAX_FILES = 50;
static constexpr uint32_t SD_FREQUENCY = 40000000UL;
static constexpr uint32_t AUDIO_PREROLL_MS = 120;

TFT_eSPI tft;
JPEGDEC jpeg;
Audio audio;
SPIClass sdSPI(HSPI);

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

struct ButtonState {
  uint8_t pin;
  bool stableLevel;
  bool lastRawLevel;
  uint32_t changedAtMs;
};

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
      audio.loop();
      taskYIELD();
    } else {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
  }
}

bool startAudio(const String &path) {
  audioServiceEnabled = false;
  vTaskDelay(pdMS_TO_TICKS(10));
  audio.stopSong();

  if (!SD.exists(path.c_str())) {
    Serial.printf("[AUDIO] Missing: %s\n", path.c_str());
    return false;
  }

  const bool connected = audio.connecttoFS(SD, path.c_str());
  if (!connected) {
    Serial.printf("[AUDIO] Open failed: %s\n", path.c_str());
    return false;
  }

  audioServiceEnabled = true;
  return true;
}

void stopAudio() {
  audioServiceEnabled = false;
  vTaskDelay(pdMS_TO_TICKS(20));
  audio.stopSong();
}

int jpegDraw(JPEGDRAW *draw) {
  tft.pushImage(draw->x, draw->y, draw->iWidth, draw->iHeight, draw->pPixels);
  return 1;
}

bool refillVideoBuffer(File &file) {
  if (videoEof || bufferEnd >= MJPEG_BUFFER_SIZE) {
    return false;
  }

  const size_t freeBytes = MJPEG_BUFFER_SIZE - bufferEnd;
  const size_t bytesRead = file.read(mjpegBuffer + bufferEnd, freeBytes);
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
    size_t soi = SIZE_MAX;

    for (size_t i = bufferBegin; i + 1 < bufferEnd; ++i) {
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
    const String name(file.name());
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

  resetVideoReader(videoFile);
  const bool hasAudio = startAudio(audioPath);

  isPlaying = true;
  tft.fillScreen(TFT_BLACK);

  // Give the audio reader/I2S ring buffer a short head start.
  if (hasAudio) {
    const uint32_t prerollUntil = millis() + AUDIO_PREROLL_MS;
    while ((int32_t)(millis() - prerollUntil) < 0) {
      if (buttonPressed(buttonSelect)) {
        isPlaying = false;
        break;
      }
      delay(1);
    }
  }

  uint32_t nextFrameAt = micros();
  uint32_t decodedFrames = 0;
  uint32_t droppedFrames = 0;

  while (isPlaying) {
    uint8_t *frameData = nullptr;
    size_t frameSize = 0;
    if (!nextJpegFrame(videoFile, frameData, frameSize)) {
      break;
    }

    const uint32_t now = micros();
    const int32_t lateness = static_cast<int32_t>(now - nextFrameAt);

    // Audio is the priority. If more than one frame late, discard this JPEG.
    if (lateness > static_cast<int32_t>(FRAME_PERIOD_US)) {
      ++droppedFrames;
      nextFrameAt += FRAME_PERIOD_US;
    } else {
      while ((int32_t)(micros() - nextFrameAt) < 0) {
        if (buttonPressed(buttonSelect)) {
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
        jpeg.decode(x, y, 0);
        jpeg.close();
        ++decodedFrames;
      } else {
        Serial.printf("[VIDEO] JPEG open failed, size=%u\n",
                      static_cast<unsigned>(frameSize));
      }
      nextFrameAt += FRAME_PERIOD_US;
    }

    if (buttonPressed(buttonSelect)) {
      isPlaying = false;
    }
    taskYIELD();
  }

  isPlaying = false;
  stopAudio();
  videoFile.close();

  Serial.printf("[VIDEO] done, decoded=%lu dropped=%lu\n",
                static_cast<unsigned long>(decodedFrames),
                static_cast<unsigned long>(droppedFrames));
  drawMenu();
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
  if (!SD.begin(SD_CS, sdSPI, SD_FREQUENCY)) {
    tft.fillScreen(TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.println("SD FAIL");
    Serial.println("[SD] Mount failed; try 20 MHz before changing wiring");
    while (true) {
      delay(1000);
    }
  }

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
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
}

void loop() {
  if (isPlaying) {
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

  delay(1);
}
