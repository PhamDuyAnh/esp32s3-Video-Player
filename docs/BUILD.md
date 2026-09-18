# Build for xingzhi-cube-1.54tft-wifi

Use `scripts/build.ps1 -Sketch selfTest` or `-Sketch videoPlayer` from PowerShell, or run the matching VSCode build task. The script stores artifacts under `.build/` and uses the repository's `User_Setup_Xingzhi.h` for TFT_eSPI through a compile flag. It does not alter the installed library.

Verified local toolchain on 2026-09-18:

| Component | Version |
|---|---|
| Arduino CLI | nightly-20260916 |
| Arduino-ESP32 | 2.0.17 |
| TFT_eSPI | 2.5.44 |
| JPEGDEC | 1.2.8 |
| ESP32-audioI2S | 2.0.0 |

The FQBN in the script selects ESP32-S3, 16 MB quad flash, OPI PSRAM, USB hardware CDC with CDC on boot, and the 3 MB application partition. `EraseFlash=none` is mandatory. The toolchain and library versions above are the known build set; do not upgrade them as a group without retesting.

The self-test build succeeds with 768833 bytes of program storage and 35496 bytes of static RAM. Hardware results are recorded separately in `DEVICE_TEST_REPORT.md`.
