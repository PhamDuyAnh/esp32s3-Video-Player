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
| LCD colors/orientation | Passed for colors | User confirmed the five colors changed correctly. Orientation/artefact details were not separately described |
| GPIO0/39/40 buttons | Passed with caveat | Serial recorded press/release on all three GPIOs across runs; user reported pressing SELECT. The deliberate SELECT press did not produce a new captured edge in the latest monitor window |
| TF/SD 20 MHz | Passed | Card type 3, capacity 32,220,643,328 bytes; file listing succeeded; 4 MiB sequential read in 2,591 ms = 1,580 KB/s, zero read errors |
| TF/SD 40 MHz | Passed, no useful speed gain | 4 MiB sequential read in 2,595 ms = 1,578 KB/s, zero read errors; returned to 20 MHz, 1,579 KB/s |
| Audio only | Passed for speaker path | Reference 1 kHz/24 kHz tone completed for 8 seconds at peak 6,000/32,767; user reported sound OK. Existing card WAV is 22,050 Hz, so playback of a compliant 24 kHz WAV remains untested |
| Microphone | Passed | User reported making noise during capture; 16 kHz I²S peak rose from roughly 700 to 8,343 in the same run |

The test firmware does not write to SD and passes `format_if_empty=false` to the Arduino SD API. This API does not expose a read-only mount option; all application file opens use `FILE_READ`.

The first self-test upload crashed during SD listing. Decoded backtrace pointed to `Serial.printf` in `testSd()` at the file-size print; a 32-bit `size_t` was passed to `%llu`. The value is now cast to `unsigned long long`, and the corrected build/upload succeeded. No further panic appeared in the partial second monitor capture.

The first reference-tone attempt could not install I²S0 because the Audio library had already registered that port. The tone test now uses I²S1, runs for 8 seconds, then releases the port before microphone capture on I²S1. Serial replay with `r` reruns the visual/SD/audio/microphone sequence without a hardware RESET button; `s` runs the 40 MHz SD read benchmark and restores 20 MHz.

## Player and media matrix

Player build/upload and tests A–F are pending. The player now builds, but no FPS or dropped-frame figures are claimed yet. The media on SD was generated at 25 fps with 16-bit mono 22050 Hz WAV, so it does not meet the target 15 fps/24000 Hz profile. These media files were not modified or copied to SD.

### Player observations

- Test A, `7.mjpeg`, 10 fps, SD 20 MHz, audio off: 1937 decoded, 0 dropped over about 194 seconds. User reported correct display.
- Test B, `7.mjpeg`, 15 fps, SD 20 MHz, audio off: 2894 decoded, 8 dropped (0.28%) over about 193 seconds. User reported smoother motion and compression grain in some scenes.
- Existing `7.mjpeg` and `8.mjpeg` contain a custom leading byte `25`, then valid JPEG streams. They are 25 fps. Their WAV files are PCM mono 16-bit/22050 Hz. The player tolerates the leading byte but this is not the target format.
- Offline scan of `8.mjpeg`: 5049 complete JPEG frames, no truncated frame, maximum frame 12936 bytes, no frame above the 96 KiB buffer limit. A bad/oversized JPEG is therefore not the likely cause of its intermittent freeze.
- The observed failure signature (image stops, audio continues, SELECT cannot exit) points to concurrent FAT/SD access from video and audio tasks. The player now serializes all Audio and video file reads with one mutex, limits each video read to 16 KiB, and polls SELECT/serial stop while scanning frame markers.
- Retest of existing video 8 with the fix and volume 6/21 reached 926 decoded frames; SELECT produced a final metric, closed audio and returned from playback. No unresponsive SELECT was observed in that run.
- Starting more files immediately after that test produced JPEG open failures. The final build adds a 50 ms teardown delay and a 300 ms button guard before another file may start. This transition fix is built and uploaded but still needs a repeated-file hardware test.

## Rollback

Once the backup has a verified 16 MB length and SHA-256, it can be restored with esptool 4.5.1 using `--port COM9 write_flash 0x0 <backup-path>`. This overwrites the entire current flash, including NVS and credentials, so keep the image private and verify that COM9 is the same device before running it. Do not use `erase_flash`.

## Current recommendation

The updated player is installed on COM9. Keep LCD at 40 MHz and SD at 20 MHz: the 40 MHz SD trial showed no throughput gain. Default volume is 6/21. Convert new media with `videoConverter/videoConvert.py` to MJPEG 240×240/15 fps plus PCM mono 16-bit/24 kHz WAV.

### 2026-09-19: 24 fps feasibility and media preparation

- At LCD SPI 40 MHz, 24 fps video-only on the older `7.mjpeg` reached roughly 20.5–22.3 effective fps in early windows and dropped 165 frames by the stop point. LCD push took about 26.8 ms/frame.
- Changing only LCD SPI to 80 MHz reduced LCD push to about 15.25 ms/frame. Light scenes initially achieved 24 fps with no drops, but heavy scenes fell to 15–22 fps and cumulative drops reached 158 by the later log. Video/audio together at 24–25 fps was not attempted because video-only did not pass.
- The 80 MHz display trial did not complete a 30-minute visual stability test. Firmware was rebuilt and reuploaded with the proven LCD 40 MHz, SD 20 MHz, 15 fps configuration; the final upload hash verified on COM9.
- `python videoConverter/videoConvert.py` regenerated all six source videos present in `input_videos`: 1, 2, 3, 4, 7, 8. Output `.mjpeg` starts at JPEG SOI without a custom header; WAV is PCM mono 16-bit/24 kHz. The new `8.mjpeg` has 3028 frames, maximum JPEG 4669 bytes, and A/V duration difference about 0.05 s. Media remained on the PC; no write to the device SD card occurred.
- For card transfer, see `docs/MEDIA_TRANSFER.md` and `scripts/sync-media.ps1`. It requires a removable card reader drive and verifies SHA-256 after copying; it has not been run against a card.
