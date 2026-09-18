# Đánh giá dự án mẫu videoPlayer.zip

## 1. Phạm vi kiểm tra

Archive chứa sketch `videoPlayer.ino` và output build Arduino ngày 28-05-2026. Không có `User_Setup.h` của TFT_eSPI, file media mẫu hoặc log Serial; do đó pin LCD được đối chiếu với cấu hình bo chính thức, còn chất lượng âm thanh chưa thể đo trực tiếp.

## 2. GPIO đã xác nhận

| Khối | Chức năng | GPIO | Bằng chứng |
|---|---|---:|---|
| Nút | Up | 40 | Sketch mẫu và cấu hình bo chính thức trùng nhau |
| Nút | Down | 39 | Sketch mẫu và cấu hình bo chính thức trùng nhau |
| Nút | Select/Boot | 0 | Sketch mẫu và cấu hình bo chính thức trùng nhau |
| TF/SD | MISO | 1 | Sketch mẫu đã đọc/phát được media |
| TF/SD | MOSI | 2 | Sketch mẫu đã đọc/phát được media |
| TF/SD | SCK | 3 | Sketch mẫu đã đọc/phát được media |
| TF/SD | CS | 46 | Sketch mẫu đã đọc/phát được media |
| Audio I²S | BCLK | 15 | Sketch mẫu và cấu hình bo chính thức trùng nhau |
| Audio I²S | LRCK/WS | 16 | Sketch mẫu và cấu hình bo chính thức trùng nhau |
| Audio I²S | DOUT | 7 | Sketch mẫu và cấu hình bo chính thức trùng nhau |
| LCD | MOSI | 10 | Cấu hình bo chính thức |
| LCD | SCLK | 9 | Cấu hình bo chính thức |
| LCD | DC | 8 | Cấu hình bo chính thức |
| LCD | CS | 14 | Cấu hình bo chính thức |
| LCD | RESET | 18 | Cấu hình bo chính thức |
| LCD | Backlight | 13 | Cấu hình bo chính thức |
| Nguồn | Power latch | 21 | Cấu hình bo chính thức |

Kết luận mới: biến thể/revision mà dự án mẫu sử dụng có giao tiếp TF/SD độc lập trên GPIO 1/2/3 và sketch khai báo GPIO46 là CS. Điều này tốt hơn phương án chia sẻ SPI với LCD. Tuy nhiên GPIO46 có hạn chế đặc biệt trên ESP32-S3; việc hệ thống thực tế đọc được thẻ chưa chứng minh GPIO46 đang được điều khiển như một CS push-pull thông thường. Cần xác minh schematic hoặc đo logic trước khi diễn giải mạch.

## 3. Cách vận hành của sketch mẫu

- SD chạy trên `SPIClass(HSPI)` ở 40 MHz.
- Video là luồng MJPEG thô trong file `.mjpeg`.
- Audio là file `.wav` cùng tên.
- Buffer MJPEG 60 KiB đặt trong PSRAM.
- Code quét marker JPEG SOI `FFD8` và EOI `FFD9`, giải mã bằng JPEGDEC rồi ghi từng block qua TFT_eSPI.
- Video khóa cứng ở 25 fps.
- Audio dùng ESP32-audioI2S 2.0.0 và chỉ được phục vụ khi code gọi `audio.loop()`.
- `VOLUME=0` làm mute, không khắc phục nguyên nhân gốc.

## 4. Nguyên nhân có khả năng làm audio rất kém

### 4.1. `audio.loop()` bị bỏ đói — nguyên nhân phần mềm chính

ESP32-audioI2S 2.0.0 cần được gọi `audio.loop()` thường xuyên để đọc file, decode và nạp I²S. Trong sketch mẫu, lời gọi này bị ngắt bởi:

- Quét tuần tự tối đa 60 KiB để tìm marker JPEG.
- `memmove()` toàn bộ phần buffer còn lại sau mỗi frame.
- Đọc SD đồng bộ.
- `jpeg.decode()`.
- Nhiều lần `tft.pushImage()` đồng bộ.
- Hàm nút bấm chờ busy-loop cho tới khi thả nút.

Khi I²S không được nạp kịp, âm thanh xuất hiện underrun, đứt đoạn, lặp mẫu hoặc nhiễu. Tải 25 fps làm tình trạng nặng hơn.

### 4.2. Hai file không có clock đồng bộ

Video và WAV được mở riêng. Video dựa vào `millis()`, audio dựa vào sample clock I²S. Không có timestamp chung, cơ chế bù lệch hoặc drop frame. `lastFrameTime` cũng không được reset rõ ràng khi bắt đầu file mới và được gán bằng thời điểm thực tế, gây drift tích lũy.

### 4.3. FPS bị gán cứng 25

Nếu MJPEG được tạo ở FPS khác, video chạy sai tốc độ và lệch audio. 25 fps cũng khá nặng cho JPEG decode + SPI LCD + đọc SD đồng thời.

### 4.4. Sao chép và xử lý buffer chưa hiệu quả

Sau mỗi frame, toàn bộ dữ liệu còn lại bị `memmove`. Ngoài tốn CPU/memory bandwidth, trường hợp frame lớn hơn 60 KiB làm thuật toán rơi vào nhánh bỏ từng byte — cực chậm.

### 4.5. Audio source chưa được chuẩn hóa

Không có kiểm tra WAV sample rate, bit depth, channel count, clipping hoặc mức loudness. File WAV stereo/44,1–48 kHz vẫn có thể phát nhưng tăng tải và có thể không phù hợp với mạch/loa nhỏ. Nếu nguồn đã clip, giảm volume chỉ làm tín hiệu clip nhỏ hơn chứ không phục hồi chất lượng.

### 4.6. Hạn chế vật lý

Loa kit 8 Ω/1 W và thùng rất nhỏ không tái tạo tốt bass, dễ rung/vỡ tiếng ở mức lớn. Nguồn pin yếu hoặc nhiễu nguồn từ LCD/SD cũng có thể làm chất lượng xấu. Đây là nguyên nhân cần đo sau khi đã loại trừ underrun phần mềm.

## 5. Thay đổi trong bản cải tiến

Sketch trong `firmware/videoPlayer/videoPlayer.ino` thực hiện:

- Task audio riêng, pin vào core khác và gọi `audio.loop()` liên tục.
- Không busy-wait khi đọc nút.
- Buffer MJPEG 96 KiB và chỉ compact khi hết vùng trống, không `memmove` sau mọi frame.
- Lịch frame bằng `micros()` với deadline cộng dồn, tránh drift kiểu `last=millis()`.
- Mặc định 15 fps; drop frame khi video trễ để audio tiếp tục đều.
- Reset clock mỗi file.
- Bật power latch GPIO21 và backlight GPIO13 rõ ràng.
- Volume mặc định thận trọng, không còn mute cứng.
- Log frame decode/drop và lỗi frame quá lớn.

Đây là bản cải tiến có cơ sở từ code review, chưa được tuyên bố hoạt động trên bo thật vì môi trường hiện tại không có Arduino CLI, đúng bộ thư viện và phần cứng để build/flash.

## 6. Profile media khuyến nghị cho bản Arduino/MJPEG tách file

Video:

```bash
ffmpeg -i input.mp4 -an \
  -vf "scale=240:240:force_original_aspect_ratio=decrease,pad=240:240:(ow-iw)/2:(oh-ih)/2:black,fps=15" \
  -c:v mjpeg -q:v 10 -f mjpeg 001_demo.mjpeg
```

Audio:

```bash
ffmpeg -i input.mp4 -vn \
  -af "highpass=f=100,lowpass=f=10500,loudnorm=I=-18:TP=-2:LRA=7" \
  -c:a pcm_s16le -ar 24000 -ac 1 001_demo.wav
```

Hai file phải có cùng basename và bắt đầu từ cùng timestamp. Không dùng tùy chọn cắt khác nhau cho hai lệnh.

## 7. Kiểm thử phân biệt lỗi phần mềm và phần cứng

1. Phát WAV 1 kHz/24 kHz mono khi không chạy video.
2. Nếu vẫn méo: kiểm tra file, I²S format, nguồn, amplifier và loa.
3. Nếu WAV đơn lẻ sạch nhưng chạy cùng video bị vỡ: xác nhận starvation/bus/CPU.
4. Chạy 10, 12, 15 và 20 fps, ghi số frame drop và hiện tượng audio.
5. Cấp nguồn USB ổn định, giảm backlight rồi so sánh.
6. Thử loa 8 Ω/1 W khác và kiểm tra rung cơ khí của vỏ.
7. Dùng oscilloscope/logic analyzer kiểm tra BCLK GPIO15, LRCK GPIO16 và DOUT GPIO7 nếu âm thanh vẫn sai.
