# Đối chiếu GPIO giữa hai cấu hình XiaoZhi

## Kết luận

Hai file `config.h` không phải hai nguồn độc lập xác nhận cùng một pinout. Chúng thuộc hai biến thể bo khác nhau:

- `zhengchen/1.54tft-wifi`: một pinout khác.
- `xingzhi-cube-1.54tft-wifi`: khớp phần cứng, dự án mẫu và thử nghiệm XiaoZhi AI của người dùng.

Đối với repo `esp32s3-Video-Player`, phải dùng pinout **xingzhi-cube-1.54tft-wifi**. Không trộn các chân LCD/nút của cấu hình `zhengchen`.

## Nguồn đối chiếu

1. [PhamDuyAnh/XiaoZhi-esp32 — zhengchen/1.54tft-wifi/config.h](https://github.com/PhamDuyAnh/XiaoZhi-esp32/blob/main/main/boards/zhengchen/1.54tft-wifi/config.h)
2. [TienHuyIoT/xiaozhi-esp32_vietnam — xingzhi-cube-1.54tft-wifi/config.h](https://github.com/TienHuyIoT/xiaozhi-esp32_vietnam/blob/develop_vn/main/boards/xingzhi-cube-1.54tft-wifi/config.h)
3. Dự án mẫu `videoPlayer.zip`, đã đọc và phát video từ thẻ.
4. Xác nhận thực tế của người dùng: firmware XiaoZhi AI vận hành tốt LCD, loa, microphone và các nút.

## Bảng so sánh

| Chức năng | zhengchen/1.54tft-wifi | xingzhi-cube-1.54tft-wifi | Chọn cho dự án |
|---|---:|---:|---:|
| SD/TF MISO | Không khai báo | GPIO1 | GPIO1 |
| SD/TF MOSI | Không khai báo | GPIO2 | GPIO2 |
| SD/TF SCK | Không khai báo | GPIO3 | GPIO3 |
| SD/TF CS/D3 | Không khai báo | GPIO46 | GPIO46 |
| Microphone WS | GPIO4 | GPIO4 | GPIO4 |
| Microphone SCK | GPIO5 | GPIO5 | GPIO5 |
| Microphone DIN | GPIO6 | GPIO6 | GPIO6 |
| Speaker DOUT | GPIO7 | GPIO7 | GPIO7 |
| Speaker BCLK | GPIO15 | GPIO15 | GPIO15 |
| Speaker LRCK | GPIO16 | GPIO16 | GPIO16 |
| Boot/Select | GPIO0 | GPIO0 | GPIO0 |
| Volume+ | GPIO10 | GPIO40 | GPIO40 |
| Volume− | GPIO39 | GPIO39 | GPIO39 |
| LCD MOSI/SDA | GPIO41 | GPIO10 | GPIO10 |
| LCD SCLK | GPIO42 | GPIO9 | GPIO9 |
| LCD DC | GPIO40 | GPIO8 | GPIO8 |
| LCD CS | GPIO21 | GPIO14 | GPIO14 |
| LCD RESET | GPIO45 | GPIO18 | GPIO18 |
| LCD backlight | GPIO20 | GPIO13 | GPIO13 |
| LCD | 240×240 | 240×240 | ST7789 240×240 |
| Audio input rate | 16 kHz | 16 kHz | 16 kHz |
| Audio output rate | 24 kHz | 24 kHz | 24 kHz |

## Pinout chuẩn của esp32s3-Video-Player

```text
TF/SD:
  MISO = GPIO1
  MOSI = GPIO2
  SCK  = GPIO3
  CS   = GPIO46

Microphone I2S:
  WS   = GPIO4
  SCK  = GPIO5
  DIN  = GPIO6

Speaker I2S:
  DOUT = GPIO7
  BCLK = GPIO15
  LRCK = GPIO16

Buttons:
  SELECT/BOOT = GPIO0
  UP/VOLUME+  = GPIO40
  DOWN/VOLUME-= GPIO39

LCD ST7789:
  MOSI = GPIO10
  SCLK = GPIO9
  DC   = GPIO8
  CS   = GPIO14
  RST  = GPIO18
  BL   = GPIO13
```

## Mức độ xác nhận

| Khối | Mức xác nhận |
|---|---|
| LCD | Cấu hình xingzhi + dự án mẫu/XiaoZhi chạy thực tế |
| Loa I²S | Hai cấu hình trùng nhau + XiaoZhi chạy thực tế |
| Microphone I²S | Hai cấu hình trùng nhau + XiaoZhi chạy thực tế |
| Nút GPIO0/39/40 | Cấu hình xingzhi + phần cứng chạy thực tế |
| TF GPIO1/2/3/46 | Cấu hình xingzhi + dự án mẫu đọc/phát video thành công |

## Quy tắc cho mã nguồn

- Tập trung pinout vào một file cấu hình; không lặp số GPIO rải rác.
- Thêm `static_assert` hoặc kiểm tra compile-time để phát hiện xung đột GPIO.
- Không dùng pinout `zhengchen` cho target `xingzhi-cube-1.54tft-wifi`.
- Khi test, kiểm tra từng khối độc lập trước khi chạy đồng thời video và audio.
