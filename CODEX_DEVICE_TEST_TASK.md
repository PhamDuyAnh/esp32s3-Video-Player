# Nhiệm vụ cho Codex: build, kiểm tra COM9 và thử nghiệm firmware

> **Lưu trữ yêu cầu thử nghiệm ban đầu.** Nhiều giai đoạn dưới đây đã hoàn thành; xem [báo cáo thiết bị](docs/DEVICE_TEST_REPORT.md) để biết kết quả hiện tại. Bo của người dùng không có nút RESET riêng. Firmware đang dùng phát cặp `.mjpeg` + `.wav` ở 15 fps và có menu cài đặt.

## Vai trò

Bạn đang làm việc trực tiếp trong repo `esp32s3-Video-Player` đã mở bằng VSCode trên máy Windows. Hãy chủ động thực hiện công việc trong phạm vi repo và thiết bị ở COM9. Chỉ yêu cầu người dùng phối hợp khi cần quan sát LCD, nghe loa, nhấn nút hoặc xác nhận thao tác phần cứng.

Không tuyên bố thành công nếu chưa có build log, serial log và xác nhận quan sát cần thiết.

## Mục tiêu

1. Chuẩn hóa dự án để build lặp lại được trong VSCode.
2. Kiểm tra phần cứng `xingzhi-cube-1.54tft-wifi` trên COM9.
3. Xác nhận LCD, nút, thẻ TF/SD và audio I²S bằng firmware test có log.
4. Build và nạp bản video player cải tiến.
5. Đánh giá video/audio, thu số liệu frame drop/SD throughput và sửa các lỗi tìm được.
6. Lưu tài liệu kết quả vào repo.

## Dữ liệu đã xác nhận

Target đúng là **xingzhi-cube-1.54tft-wifi**, không phải pinout `zhengchen/1.54tft-wifi`.

### GPIO chuẩn

```text
TF/SD SPI:
  MISO GPIO1
  MOSI GPIO2
  SCK  GPIO3
  CS   GPIO46

Microphone I2S:
  WS   GPIO4
  SCK  GPIO5
  DIN  GPIO6
  input sample rate 16000 Hz

Speaker I2S:
  DOUT GPIO7
  BCLK GPIO15
  LRCK GPIO16
  output sample rate 24000 Hz

Buttons:
  SELECT/BOOT GPIO0
  UP/VOLUME+  GPIO40
  DOWN/VOLUME- GPIO39

LCD ST7789 240x240:
  MOSI GPIO10
  SCLK GPIO9
  DC   GPIO8
  CS   GPIO14
  RESET GPIO18
  BACKLIGHT GPIO13
```

Các khối LCD, loa, microphone và nút đã hoạt động với firmware XiaoZhi AI. Thẻ đã phát video thành công bằng dự án mẫu. Thiết bị hiện kết nối tại **COM9**.

Đọc trước:

- `README.md`
- `docs/GPIO_CROSSCHECK.md`
- `docs/SAMPLE_PROJECT_REVIEW.md`
- `docs/HARDWARE.md`
- `firmware/videoPlayer/videoPlayer.ino`
- `firmware/videoPlayer/User_Setup_Xingzhi.h`

## Nguyên tắc an toàn

- Chạy `git status --short` trước khi sửa; không ghi đè thay đổi chưa commit của người dùng.
- Không erase toàn bộ flash.
- Không format thẻ TF/SD.
- Không ghi/xóa media trên thẻ; mount read-only nếu framework cho phép.
- Trước lần nạp đầu tiên, đọc thông tin chip/flash và sao lưu toàn bộ flash hiện tại.
- Lưu backup ngoài source tree hoặc trong thư mục bị `.gitignore`; không commit file chứa firmware/NVS/credential.
- Không in credential/NVS vào tài liệu hay commit.
- Nếu COM9 không phải ESP32-S3, dừng và báo rõ.
- Chỉ thay đổi một nhóm yếu tố mỗi vòng thử nghiệm.
- Nếu mất COM9 sau reset, quét lại port; không tự chọn một thiết bị khác khi chưa xác nhận.

## Giai đoạn 1 — Khảo sát môi trường

1. Kiểm tra:
   - `git status --short`
   - VSCode workspace files.
   - `arduino-cli version`
   - `pio --version`
   - Python và `python -m esptool version`
2. Chọn một build path chính:
   - Ưu tiên PlatformIO nếu có hoặc cài được ở mức project/user không phá môi trường.
   - Nếu repo/máy đã có Arduino CLI hoạt động ổn định, có thể giữ Arduino CLI.
3. Ghi phiên bản:
   - Arduino-ESP32 core.
   - TFT_eSPI.
   - JPEGDEC.
   - ESP32-audioI2S.
4. Không nâng tất cả dependency lên bản mới nhất trong cùng một bước. Pin phiên bản đã build được.

## Giai đoạn 2 — Nhận dạng và sao lưu thiết bị

Thực hiện read-only trước:

```powershell
pio device list
python -m esptool --port COM9 chip_id
python -m esptool --port COM9 flash_id
```

Điều chỉnh cú pháp theo esptool đã cài. Xác nhận ESP32-S3 và kích thước flash. Sau đó sao lưu flash bằng kích thước được phát hiện. Với flash 16 MiB, phạm vi dự kiến là `0x000000 0x1000000`, nhưng không dùng con số này nếu `flash_id` báo khác.

Tạo checksum SHA-256 cho backup. Thêm đường dẫn backup vào `.gitignore`. Không commit backup.

Nếu không thể đưa bo vào bootloader tự động:

1. Yêu cầu người dùng giữ BOOT/SELECT.
2. Nhấn RESET hoặc kết nối lại USB theo đúng phần cứng.
3. Thả BOOT khi esptool bắt đầu kết nối.

Không lặp vô hạn; tối đa ba lần rồi báo log.

## Giai đoạn 3 — Chuẩn hóa build

Tạo cấu hình build tái lập được:

- Target ESP32-S3.
- Flash 16 MB và PSRAM 8 MB theo phần cứng thực tế.
- USB/serial settings phù hợp COM9.
- Partition đủ cho application.
- Dependency được pin phiên bản.
- Cấu hình TFT_eSPI lấy từ repo; không sửa trực tiếp file trong global library nếu có thể.
- Tách GPIO vào `board_config.h`.
- Thêm kiểm tra compile-time để không trùng chân giữa SD, LCD, I²S và nút.

Build trước khi nạp. Lưu command và phần cuối build log vào báo cáo.

## Giai đoạn 4 — Firmware hardware self-test

Tạo một chế độ self-test hoặc firmware riêng, không trộn ngay với player.

### 4.1. Boot diagnostics

Serial 115200 phải in:

- Chip model/revision.
- CPU frequency.
- Flash size.
- PSRAM detected/size.
- Free internal heap và PSRAM.
- Reset reason.
- Cấu hình GPIO đang dùng.

### 4.2. LCD

- Khởi tạo ST7789 ở 40 MHz trước.
- Hiện lần lượt đỏ, xanh lá, xanh dương, trắng, đen.
- Hiện tên GPIO và trạng thái từng bài test.
- Không nâng 80 MHz trước khi 40 MHz ổn định.

Yêu cầu người dùng xác nhận: đúng màu, đúng hướng, không nhấp nháy/artefact.

### 4.3. Nút

Serial log cạnh nhấn/thả của GPIO0, GPIO39, GPIO40, có debounce, không busy-wait.

Yêu cầu người dùng lần lượt nhấn SELECT, DOWN, UP. Không dùng giữ SELECT lúc boot trừ khi cần download mode.

### 4.4. Thẻ TF/SD

- Khởi tạo GPIO1/2/3/46.
- Thử 20 MHz trước; nếu ổn định mới thử 40 MHz.
- In card type, capacity, filesystem và danh sách file read-only.
- Chọn file media có sẵn và benchmark đọc tuần tự tối thiểu 4 MiB hoặc tới EOF.
- In KB/s, thời gian đọc và lỗi.
- Không đổi tên, ghi hoặc xóa file.

Nếu 40 MHz lỗi nhưng 20 MHz ổn định, giữ 20 MHz cho bài test video đầu.

### 4.5. Audio riêng

Ưu tiên WAV chuẩn:

- PCM signed 16-bit little-endian.
- Mono.
- 24000 Hz.
- Không clipping; peak tối đa khoảng -2 dBFS.
- Bắt đầu volume thấp.

Phát WAV khi video chưa chạy, duy trì service audio liên tục và log underrun nếu thư viện hỗ trợ. Yêu cầu người dùng đánh giá: sạch/méo/rè/đứt đoạn, volume.

Nếu audio-only đã kém, chưa chuyển sang video. Kiểm tra lần lượt WAV format, I²S standard/slot, nguồn USB ổn định, amplifier và loa.

### 4.6. Microphone

Microphone không cần cho player nhưng phải bảo đảm không có xung đột GPIO:

- Capture I²S 16 kHz trong vài giây.
- In RMS/peak, không dump audio binary ra terminal.
- Xác nhận mức thay đổi khi người dùng nói/vỗ tay.

## Giai đoạn 5 — Player cải tiến

Sau khi self-test đạt:

1. Build `firmware/videoPlayer/videoPlayer.ino`.
2. Sửa lỗi API do phiên bản thư viện bằng thay đổi nhỏ, có ghi chú.
3. Audio service chạy độc lập với video decode.
4. Không dùng busy-wait.
5. Giữ SD và LCD trên hai bus đúng pinout.
6. Mặc định:
   - MJPEG 240×240.
   - 15 fps.
   - WAV PCM mono 16-bit/24 kHz.
   - Volume thấp, có thể tăng từng bước.
7. Video clock dùng deadline cộng dồn; khi trễ thì drop video frame, không làm đói audio.
8. Log mỗi 5 giây:
   - decoded frames.
   - dropped frames.
   - effective FPS.
   - audio running/underrun nếu có.
   - free heap/PSRAM.
   - SD read throughput hoặc read latency.
9. Stop/return menu phải đóng file và dừng audio sạch, không phát tiếng pop kéo dài.

## Giai đoạn 6 — Ma trận thử nghiệm

Thử tuần tự, không thay nhiều biến cùng lúc:

| Test | FPS | SD clock | Audio | Mục tiêu |
|---|---:|---:|---|---|
| A | 10 | 20 MHz | Off | Xác nhận video nền |
| B | 15 | 20 MHz | Off | Đo frame drop |
| C | — | 20 MHz | Audio-only | Xác nhận audio sạch |
| D | 10 | 20 MHz | On | Kiểm tra cạnh tranh tài nguyên |
| E | 15 | 20 MHz | On | Cấu hình mục tiêu |
| F | 15 | 40 MHz | On | Chỉ thử nếu E ổn định |

Mỗi test chạy ít nhất 3 phút; test cuối chạy 30 phút.

Nếu audio kém khi video bật nhưng sạch ở audio-only:

- Giảm FPS.
- Đo thời gian `jpeg.decode()`, SD read và LCD push.
- Kiểm tra audio task có bị block bởi filesystem/SPI mutex.
- Tăng read-ahead/ring buffer có kiểm soát.
- Giảm kích thước/quality JPEG.
- Không tăng task priority mù quáng đến mức watchdog hoặc LCD bị starvation.

## Giai đoạn 7 — Báo cáo và commit

Tạo `docs/DEVICE_TEST_REPORT.md` gồm:

- Ngày thử.
- Hardware/PCB revision nếu đọc được.
- Toolchain/dependency versions.
- COM port.
- Flash/PSRAM.
- Backup path và checksum nhưng không chứa credential.
- Kết quả từng self-test.
- Tên/profile media đã thử.
- FPS, dropped frame, audio observation.
- Lỗi còn lại và giả thuyết.
- Cấu hình hiện đang khuyến nghị.
- Lệnh build/upload/monitor chính xác.
- Cách rollback firmware backup.

Cập nhật `README.md` với trạng thái thực tế. Commit theo nhóm nhỏ, ví dụ:

1. `build: add reproducible ESP32-S3 environment`
2. `test: add xingzhi hardware self-test`
3. `fix: stabilize concurrent MJPEG and I2S playback`
4. `docs: record COM9 device test results`

Không commit build output, flash backup, media có bản quyền hoặc file chứa credential.

## Điểm dừng bắt buộc

Dừng và báo người dùng nếu:

- COM9 không nhận dạng là ESP32-S3.
- Flash backup thất bại.
- Build yêu cầu thay đổi pinout đã xác nhận.
- LCD/audio-only không hoạt động.
- Có nguy cơ format/xóa thẻ.
- Cần thao tác phần cứng ngoài nhấn nút/reset/cắm lại USB.
