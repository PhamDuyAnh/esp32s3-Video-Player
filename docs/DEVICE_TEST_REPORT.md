# COM9 device test report

Date: 2026-09-18. Target: `xingzhi-cube-1.54tft-wifi`.

## Identification and protection

- COM9 was identified by esptool 4.5.1 as ESP32-S3 revision 0.2. Flash ID reported manufacturer `c8`, device `4018`, 16 MB quad flash.
- PCB revision: not yet visually confirmed.
- PSRAM size: pending serial boot diagnostics.
- Full flash backup: **passed**, 16,777,216 bytes. Path: `.device-backups/COM9_esp32s3_2026-09-18_16MiB.bin`. SHA-256: `E5BEF42575A3792DF4293521B4451C5EFEF377648DAD16B51C8CD3C2D76663DF`. Esptool read took 1885.9 seconds at 921600 baud. This directory is ignored by Git and must not be committed or shared; the image may contain credentials.
- No flash erase, SD format, or media write has been performed.

## Toolchain and build

- Arduino CLI nightly-20260916, Arduino-ESP32 2.0.17, TFT_eSPI 2.5.44, JPEGDEC 1.2.8, ESP32-audioI2S 2.0.0.
- Primary build path: `scripts/build.ps1 -Sketch selfTest` (VSCode task `Build Xingzhi self-test`).
- Self-test build: **passed**. Final output: `Sketch uses 768833 bytes (24%) of program storage space. Maximum is 3145728 bytes.`; `Global variables use 35496 bytes (10%) of dynamic memory, leaving 292184 bytes for local variables.`
- Self-test upload: **passed** via `scripts/upload.ps1 -Sketch selfTest -Port COM9`; esptool verified hash of each written segment and reported COM9 after reset. No full-chip erase was used.

## Hardware self-test

| Test | Status | Evidence |
|---|---|---|
| Boot diagnostics | Passed | Serial: ESP32-S3, CPU 240 MHz, flash 16,777,216 bytes, PSRAM detected with 8,386,247 bytes, internal free heap 315,140 bytes, reset reason 4 |
| LCD colors/orientation | Pending | Visual confirmation required |
| GPIO0/39/40 buttons | Partial | Serial recorded press/release on GPIO39 and GPIO40; SELECT was seen pressed in an earlier run. One deliberate SELECT press/release still needs confirmation |
| TF/SD 20 MHz | Passed | Card type 3, capacity 32,220,643,328 bytes; file listing succeeded; 4 MiB sequential read in 2,591 ms = 1,580 KB/s, zero read errors |
| TF/SD 40 MHz | Passed, no useful speed gain | 4 MiB sequential read in 2,595 ms = 1,578 KB/s, zero read errors; returned to 20 MHz, 1,579 KB/s |
| Audio only | Partial | Reference 1 kHz/24 kHz tone completed for 8 seconds at peak 6,000/32,767; listening confirmation required. Existing card WAV is 22,050 Hz and was rejected for the target profile |
| Microphone | Partial | 16 kHz I²S capture produced RMS/peak logs; peak varied from about 500 to 9,501 after startup. Correlation with deliberate speech/clap still needs confirmation |

The test firmware does not write to SD and passes `format_if_empty=false` to the Arduino SD API. This API does not expose a read-only mount option; all application file opens use `FILE_READ`.

The first self-test upload crashed during SD listing. Decoded backtrace pointed to `Serial.printf` in `testSd()` at the file-size print; a 32-bit `size_t` was passed to `%llu`. The value is now cast to `unsigned long long`, and the corrected build/upload succeeded. No further panic appeared in the partial second monitor capture.

The first reference-tone attempt could not install I²S0 because the Audio library had already registered that port. The tone test now uses I²S1, runs for 8 seconds, then releases the port before microphone capture on I²S1. Serial replay with `r` reruns the visual/SD/audio/microphone sequence without a hardware RESET button; `s` runs the 40 MHz SD read benchmark and restores 20 MHz.

## Player and media matrix

Player build/upload and tests A–F are pending successful self-test. No FPS, dropped-frame, audio quality, or SD throughput figures are claimed yet. The untracked WAV files found in `videoConverter/output_sd/` are 16-bit mono at 22050 Hz, so they do not meet the target 24000 Hz audio profile. They were not modified or copied to SD.

## Rollback

Once the backup has a verified 16 MB length and SHA-256, it can be restored with esptool 4.5.1 using `--port COM9 write_flash 0x0 <backup-path>`. This overwrites the entire current flash, including NVS and credentials, so keep the image private and verify that COM9 is the same device before running it. Do not use `erase_flash`.

## Current recommendation

Self-test firmware is currently installed. Keep LCD at 40 MHz and SD at 20 MHz: the 40 MHz SD trial showed no throughput gain. Wait for LCD and audio listening confirmation before building/uploading the player.
