# Kế hoạch phát triển trình phát video

## 1. Mục tiêu và giới hạn

Thiết bị đọc media từ thẻ TF/microSD, tạo danh sách tệp hợp lệ, sắp xếp xác định, phát tuần tự trên LCD 240×240 và loa tích hợp.

Giới hạn thực tế:

- ESP32-S3 không có bộ giải mã H.264/H.265 phần cứng.
- Không chọn MP4/H.264 làm định dạng runtime cho phiên bản đầu.
- LCD nhỏ và dùng SPI; video phải được chuyển mã trước.
- Chưa xác minh giao tiếp vật lý của thẻ trên revision bo cụ thể.
- Đồng bộ A/V phải ưu tiên audio làm clock chính; được phép bỏ frame hình khi chậm, không làm đứt audio.

## 2. Kiến trúc khuyến nghị

### Nền tảng

- ESP-IDF 5.4.x LTS/ổn định cho ESP32-S3.
- C/C++.
- FreeRTOS task, queue và event group.
- FATFS trên SDSPI hoặc SDMMC sau khi xác minh phần cứng.
- `esp_lcd` + ST7789, RGB565.
- I²S standard TX, PCM mono 16-bit.
- JPEG decoder của Espressif hoặc thư viện có benchmark rõ ràng trên ESP32-S3.

### Pipeline

1. Mount thẻ ở chế độ read-only.
2. Quét `/sdcard/videos`.
3. Lọc tên/extension/size và kiểm tra header.
4. Natural-sort không phân biệt hoa thường.
5. Mở AVI, đọc metadata và kiểm tra profile.
6. Demux chunk MJPEG/PCM theo luồng.
7. Đưa audio PCM vào ring buffer I²S.
8. Giải mã JPEG vào buffer RGB565 trong PSRAM.
9. Chép từng dải qua DMA tới LCD.
10. Dùng số sample audio đã phát làm mốc thời gian; bỏ frame video quá hạn.
11. Kết thúc tệp: đóng handle, giải phóng buffer, chuyển sang tệp kế tiếp.
12. Hết danh sách: lặp lại từ đầu; nếu cấu hình `repeat=false` thì dừng.

## 3. Định dạng mục tiêu

Profile khởi đầu:

| Thuộc tính | Giá trị |
|---|---|
| Container | AVI OpenDML/RIFF đơn giản, một video + một audio |
| Video codec | MJPEG |
| Kích thước | 240×240 |
| Pixel aspect | 1:1 |
| Frame rate | 12 fps mặc định; thử 15 fps sau |
| JPEG | Baseline, YUV420/422; không progressive |
| Audio | PCM signed 16-bit little-endian, mono |
| Sample rate | 24.000 Hz |
| Thẻ | FAT32 khuyến nghị |
| Tên tệp | UTF-8/ASCII an toàn; extension `.avi` |

Ước lượng băng thông: nếu JPEG trung bình 12–25 KB/frame ở 15 fps, video khoảng 180–375 KB/s; PCM mono 24 kHz 16-bit là 48 KB/s. Tổng điển hình 0,23–0,43 MB/s, chưa kể overhead. Đây là ước lượng; phải benchmark với nội dung thật.

## 4. Quản lý task và bộ nhớ

| Task/khối | Nhiệm vụ | Ưu tiên tương đối |
|---|---|---:|
| Audio output | Nạp I²S liên tục, chống underrun | Cao nhất |
| Reader/demux | Đọc trước từ SD và tách chunk | Cao |
| Video decode | JPEG → RGB565 | Trung bình |
| LCD writer | Gửi strip/frame qua SPI DMA | Trung bình |
| UI/input | Nút, OSD, trạng thái | Thấp |
| Scanner | Chỉ chạy lúc mount/rescan | Thấp |

Bộ nhớ dự kiến:

- 2 framebuffer RGB565 trong PSRAM: khoảng 225 KiB.
- Audio ring buffer: 16–64 KiB tùy latency.
- Read-ahead từ SD: 32–128 KiB.
- JPEG compressed frame: đặt giới hạn cứng, thí dụ 128 KiB.
- DMA strip buffers: cấp trong internal DMA-capable RAM.
- Mọi cấp phát phải kiểm tra lỗi; nếu thiếu bộ nhớ, dừng file hiện tại và chuyển file kế tiếp.

Không giữ toàn bộ video trong RAM.

## 5. Điều khiển đề xuất

| Nút | Nhấn ngắn | Nhấn giữ |
|---|---|---|
| BOOT / giữa | Play/Pause | Rescan thẻ hoặc menu |
| Volume+ | Tăng âm lượng | Next |
| Volume− | Giảm âm lượng | Previous |
| Nút nguồn bên hông | Giữ nguyên chức năng nguồn | Không ánh xạ ứng dụng nếu thuộc mạch latch/reset |

Debounce bằng driver nút; không xử lý nặng trong ISR.

## 6. Trạng thái ứng dụng

- `BOOTING`
- `NO_CARD`
- `SCANNING`
- `READY`
- `PLAYING`
- `PAUSED`
- `FILE_ERROR`
- `END_OF_LIST`
- `LOW_BATTERY`
- `FATAL_ERROR`

Mỗi lỗi tệp chỉ loại tệp hiện tại; không reboot toàn thiết bị. Mất thẻ: dừng I²S, đóng handle nếu có thể, xóa playlist, hiển thị `NO_CARD`, thử mount lại theo backoff hữu hạn.

## 7. Lộ trình triển khai và tiêu chí đạt

### Giai đoạn 0 — Xác minh phần cứng

- Xác định PCB revision.
- Xác nhận khe/module TF, pin, điện áp và bus.
- Xác nhận loa/I²S bằng tone test.
- Xác nhận LCD color order, rotation, 40/80 MHz.

**Gate:** chưa qua giai đoạn này thì không chốt pin SD trong source.

### Giai đoạn 1 — Bring-up

- Tạo project ESP-IDF.
- Bật flash 16 MB và PSRAM 8 MB đúng chế độ của module.
- Driver power latch, button, backlight, LCD, I²S.
- Self-test màu LCD, tone audio, dung lượng heap/PSRAM.

**Đạt:** chạy 30 phút không reset/artefact/audio error.

### Giai đoạn 2 — Storage và playlist

- Mount FATFS read-only.
- Quét đúng một thư mục.
- Lọc, natural-sort, giới hạn số mục và độ dài đường dẫn.
- Xử lý rút/cắm thẻ và tệp hỏng.

**Đạt:** danh sách 1.000 entry hỗn hợp không tràn bộ nhớ; thứ tự lặp lại giống nhau.

### Giai đoạn 3 — Video không âm thanh

- Parser AVI giới hạn theo profile.
- Giải mã MJPEG.
- Double buffering và đo FPS, decode time, LCD transfer time.
- Drop frame khi trễ.

**Đạt:** 240×240 @ 12 fps liên tục 30 phút.

### Giai đoạn 4 — Audio và đồng bộ

- PCM mono 24 kHz qua I²S.
- Ring buffer, volume software và mute ramp để tránh tiếng “pop”.
- Audio master clock, giới hạn sai lệch A/V.
- Theo dõi underrun/late/drop counters.

**Đạt:** không underrun trong media chuẩn; lệch A/V cảm nhận thấp, mục tiêu ±80 ms.

### Giai đoạn 5 — UX và độ tin cậy

- OSD tên tệp, play/pause, volume và lỗi.
- Next/Previous/repeat.
- Watchdog chỉ cho các task phù hợp.
- Thử nguồn yếu, file cắt cụt, thẻ chậm, tháo thẻ.
- Log phiên bản firmware và thống kê.

**Đạt:** lỗi media/storage không gây boot loop; phục hồi được sau khi cắm lại thẻ.

## 8. Công cụ chuyển mã trên PC

Ví dụ định hướng, cần xác nhận bằng file mẫu và parser thực tế:

```bash
ffmpeg -i input.mp4 \
  -vf "scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:black,fps=12" \
  -c:v mjpeg -q:v 10 -pix_fmt yuvj420p \
  -c:a pcm_s16le -ac 1 -ar 24000 \
  output.avi
```

Nếu bản FFmpeg cảnh báo `yuvj420p`, giữ lựa chọn pixel format theo khả năng của decoder đã benchmark; không thay đổi profile âm thầm.

## 9. Rủi ro chính

| Rủi ro | Ảnh hưởng | Kiểm soát |
|---|---|---|
| Không có khe/chân TF khả dụng | Không đọc được media ngoài | Xác minh PCB; adapter ngoài; đổi phần cứng nếu cần |
| Tranh chấp SPI LCD/SD | Stutter, lỗi đọc | Mutex bus, CS đúng, transaction nhỏ, benchmark |
| JPEG quá lớn/progressive | Hết RAM hoặc decoder lỗi | Kiểm tra header và giới hạn frame |
| SD chậm/phân mảnh | Audio underrun | Read-ahead, audio priority, thẻ tốt, file contiguous |
| Nguồn pin nhỏ | Reset/brownout | USB ổn định, giám sát pin, giảm backlight |
| GPIO21 bị thay đổi | Tắt nguồn | Driver power riêng, test sớm |
| Codec/container quá rộng | Parser phức tạp, lỗi bảo mật | Chỉ hỗ trợ profile hẹp và từ chối file khác |

## 10. Dữ liệu cần người dùng cung cấp trước khi viết driver thẻ

- Ảnh rõ hai mặt bo và mã revision.
- Ảnh khe TF (nếu có) hoặc module TF dự kiến.
- Xác nhận “thẻ FT” trong yêu cầu là thẻ TF/microSD.
- Log boot cho biết flash/PSRAM.
- Cách loa, pin và màn hình đang được nối trên bộ thực tế.
