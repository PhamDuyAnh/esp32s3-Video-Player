# Phần cứng xingzhi-cube-1.54tft-wifi

## 1. Phạm vi và mức độ xác minh

Tài liệu này mô tả đúng biến thể Wi-Fi có tên build `xingzhi-cube-1.54tft-wifi`. Không áp dụng nguyên trạng cho bản OLED, 0.85 TFT, CUBE 2.0 hoặc bản 4G/ML307.

Nguồn ưu tiên:

1. `config.h`, file khởi tạo bo và `power_manager.h` trong dự án [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/nologo/xingzhi-cube-1.54tft-wifi).
2. [FAQ chính thức của Nologo](https://www.nologo.tech/product/esp32/esp32s3/esp32s3ai/esp32s3xiaozhi/esp32s3ai_qa.html).
3. Tài liệu ESP32-S3 của Espressif.

Các chân dưới đây đã được firmware bo chính thức sử dụng. Chúng phản ánh cấu hình phần mềm hiện hành, không thay thế schematic/PCB revision của nhà sản xuất.

## 2. Cấu hình tổng quát

| Khối | Thông số đã xác minh | Ghi chú |
|---|---|---|
| SoC/module | ESP32-S3 N16R8 | N16: 16 MB flash; R8: 8 MB octal PSRAM |
| CPU | 2 nhân Xtensa LX7, tối đa 240 MHz | Có SIMD/vector instructions, phù hợp giải mã JPEG mức thấp |
| Wi-Fi | 2,4 GHz 802.11 b/g/n | Bo này là biến thể Wi-Fi |
| Bluetooth | Bluetooth LE của ESP32-S3 | Chưa phải yêu cầu của trình phát |
| LCD | ST7789, 1,54 inch, 240×240 | SPI, RGB565 16-bit |
| LCD clock trong firmware tham chiếu | 80 MHz, SPI mode 3 | Phải thử ổn định trên bo thật; có thể hạ 40 MHz |
| Audio input | Microphone số I²S, 16 kHz trong firmware tham chiếu | Không cần cho chức năng phát video |
| Audio output | I²S, 24 kHz trong firmware tham chiếu | `NoAudioCodecSimplex`; đường ra số tới mạch công suất/loa |
| Loa trong bộ kit | 8 Ω, 1 W | Theo FAQ Nologo |
| Nguồn di động | Pin Li-ion/LiPo 300 mAh trong bộ kit | FAQ nêu khoảng 1,5 giờ với firmware AI + TFT; video có thể ngắn hơn |
| USB | Nguồn, nạp firmware/USB của ESP32-S3 | Cần xác minh cổng và chế độ USB/JTAG trên revision thực tế |
| Nút | Boot, Volume+, Volume−; nút nguồn/reset bên hông | Chức năng ứng dụng có thể ánh xạ lại |
| Thẻ TF/microSD | SPI riêng: MISO 1, MOSI 2, SCK 3, CS được sketch mẫu khai báo 46 | Dự án mẫu do người dùng cung cấp đã phát được video từ thẻ |

## 3. Bảng GPIO đã xác minh từ firmware tham chiếu

| GPIO | Chức năng | Hướng/ngoại vi | Lưu ý cho dự án |
|---:|---|---|---|
| 0 | BOOT / nút giữa | Input, pull-up | Strapping pin; không dùng cho SD |\n| 1 | TF/SD MISO | Input, SPI | Xác nhận từ dự án mẫu hoạt động |\n| 2 | TF/SD MOSI | Output, SPI | Xác nhận từ dự án mẫu hoạt động |\n| 3 | TF/SD SCK | Output, SPI | Xác nhận từ dự án mẫu hoạt động |
| 4 | I²S microphone WS | Output | Đang dành cho microphone |
| 5 | I²S microphone SCK | Output | Đang dành cho microphone |
| 6 | I²S microphone DIN | Input | Dữ liệu microphone |
| 7 | I²S speaker DOUT | Output | Dữ liệu âm thanh ra |
| 8 | LCD DC | Output | Data/command |
| 9 | LCD SCLK | Output, SPI3 | Tên `DISPLAY_SCL`, thực tế là SPI clock |
| 10 | LCD MOSI | Output, SPI3 | Tên `DISPLAY_SDA`, thực tế là SPI MOSI |
| 13 | LCD backlight PWM | Output | Điều chỉnh độ sáng |
| 14 | LCD CS | Output | Chip select LCD |
| 15 | I²S speaker BCLK | Output | Clock âm thanh |
| 16 | I²S speaker LRCK/WS | Output | 24 kHz theo firmware tham chiếu |
| 17 | Đo điện áp pin | ADC2 channel 6 | Suy ra từ ánh xạ ADC2_CH6 của ESP32-S3 và code `power_manager.h` |
| 18 | LCD reset | Output | Reset ST7789 |
| 21 | Giữ nguồn/power latch | RTC output | Firmware kéo mức 1 để duy trì nguồn, mức 0 để tắt |
| 38 | Trạng thái sạc | Input | Mức 1 được firmware hiểu là đang sạc |
| 39 | Volume− | Input, pull-up | Có thể dùng Previous khi phát media |
| 40 | Volume+ | Input, pull-up | Có thể dùng Next khi phát media |\n| 46 | TF/SD CS theo sketch mẫu | Cần xác minh điện | GPIO đặc biệt; không xem như output thông thường nếu chưa đo/schematic |

### Cấu hình LCD

- Host: `SPI3_HOST`.
- Không dùng MISO.
- Pixel format: RGB565, 16 bit/pixel.
- `swap_xy=false`, `mirror_x=false`, `mirror_y=false`.
- Offset X/Y bằng 0.
- Firmware gọi `esp_lcd_panel_invert_color(panel, true)`.

Một frame đầy đủ cần:

`240 × 240 × 2 = 115.200 byte`

Hai framebuffer đầy đủ cần 230.400 byte, phù hợp khi cấp phát trong PSRAM. Nên dùng buffer DMA nội bộ theo dải (strip/line buffer), không giả định DMA đọc trực tiếp mọi vùng PSRAM mà không kiểm tra cấu hình IDF.

### Cấu hình audio

| Luồng | Sample rate tham chiếu | Chân |
|---|---:|---|
| Microphone I²S | 16.000 Hz | SCK 5, WS 4, DIN 6 |
| Speaker I²S | 24.000 Hz | BCLK 15, LRCK 16, DOUT 7 |

Firmware tham chiếu gọi thiết bị là `NoAudioCodecSimplex`: không thấy bus I²C điều khiển codec. Vì vậy mức âm lượng phần mềm nên thực hiện bằng scale PCM có saturation, còn mức công suất cuối phụ thuộc mạch amplifier trên bo.

## 4. Thẻ TF/microSD: trạng thái và phương án

### Dữ liệu đã biết

Dự án mẫu do người dùng cung cấp đã đọc và phát MJPEG từ thẻ bằng một bus SPI riêng:

- MISO: GPIO1.
- MOSI: GPIO2.
- SCK: GPIO3.
- CS được sketch khai báo: GPIO46.
- Tần số yêu cầu: 40 MHz.

Bus thẻ tách khỏi bus LCD (GPIO9/10), thuận lợi cho pipeline video.

### Điểm cần xác minh riêng đối với GPIO46

GPIO46 có các hạn chế đặc biệt/strapping trên ESP32-S3. Dù sketch mẫu khai báo GPIO46 làm SD CS và hệ thống thực tế đã phát được video, cần xác định trên schematic/revision bo liệu chân này là CS chủ động, card-detect, được kéo cứng hay đi qua mạch trung gian. Không suy rộng cấu hình này sang bo khác.

### Điều kiện sử dụng

- Giữ đúng bus riêng GPIO1/2/3 của bo đã thử nghiệm.
- Khởi đầu ở 20 MHz khi chẩn đoán; chỉ nâng 40 MHz sau khi đọc ổn định.
- Không cấp 5 V logic vào ESP32-S3 hoặc thẻ microSD.
- Mount read-only trong ứng dụng phát video.
- Không thay đổi GPIO46 hoặc chế độ boot trước khi có schematic/đo logic.
- Nếu dùng revision PCB khác, phải xác nhận continuity và mức CS trước khi cắm thẻ có dữ liệu quan trọng.

## 5. Nguồn và an toàn

- GPIO ESP32-S3 dùng logic 3,3 V; không chịu 5 V.
- Loa 8 Ω/1 W phải đi qua amplifier sẵn có; không nối trực tiếp với GPIO/I²S.
- Khi thêm module thẻ, nguồn 3,3 V cần đủ dòng xung và có tụ decoupling gần socket (thí dụ 100 nF + 10 µF; giá trị cuối dựa trên module thực tế).
- Ghi thẻ khi nguồn pin yếu dễ hỏng filesystem. Trình phát chỉ cần mount read-only; log nên ghi UART hoặc vùng NVS có kiểm soát.
- GPIO21 là power latch theo firmware tham chiếu. Cấu hình sai có thể làm bo tắt ngay.
- Pin 300 mAh nhỏ; khi phát video nên ưu tiên USB 5 V ổn định và giữ chức năng giám sát pin.

## 6. Các kiểm tra bắt buộc trên bo thật

- Chụp rõ hai mặt PCB, socket và mã revision.
- Xác nhận có/không khe TF.
- Đọc marking chip/module; kiểm tra flash/PSRAM bằng log boot.
- Chạy thử LCD ở 40 MHz rồi 80 MHz, kiểm tra artefact ít nhất 30 phút.
- Phát sine 1 kHz ở 24 kHz PCM với mức nhỏ, xác nhận pin I²S và loa.
- Đo logic/pinout của thẻ trước khi cắm thẻ chứa dữ liệu quan trọng.
