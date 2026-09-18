# esp32s3-Video-Player

Trình phát video cục bộ cho bo **xingzhi-cube-1.54tft-wifi** (ESP32-S3, LCD ST7789 240×240, âm thanh I²S).

> Trạng thái 2026-09-18: đã nhận dạng bo tại COM9 là ESP32-S3 revision 0.2 với flash 16 MB, sao lưu đủ flash và build thành công firmware hardware self-test. Chưa có kết quả kiểm tra LCD, nút, thẻ, audio, microphone hoặc player trên bo; xem [báo cáo thiết bị](docs/DEVICE_TEST_REPORT.md).

## Mục tiêu

- Quét thư mục media trên thẻ TF/microSD.
- Chỉ nhận các tệp đúng quy ước và định dạng được hỗ trợ.
- Sắp xếp danh sách phát theo tên tệp, phát tuần tự và lặp lại.
- Hiển thị video 240×240 trên ST7789, đồng thời phát âm thanh qua loa tích hợp.
- Hoạt động ổn định khi gặp tệp lỗi, mất thẻ hoặc thiếu dữ liệu.

## Kết luận khả thi

Bo có thể phát video MJPEG 240×240 kèm WAV. Dự án mẫu đã xác nhận thẻ TF chạy trên SPI riêng: MISO GPIO1, MOSI GPIO2, SCK GPIO3 và CS được sketch khai báo GPIO46. Audio I²S dùng BCLK GPIO15, LRCK GPIO16, DOUT GPIO7.

Bản Arduino tương thích với cách vận hành mẫu dùng cặp file `.mjpeg` + `.wav`, mặc định 15 fps và PCM mono 24 kHz. Hướng phát triển dài hạn vẫn là AVI chứa MJPEG + PCM để có timestamp/container thống nhất. Không khuyến nghị MP4/H.264 vì ESP32-S3 không có phần cứng giải mã H.264.

## Tài liệu

- [Build trong VSCode hoặc PowerShell](docs/BUILD.md)
- [Báo cáo thử thiết bị COM9](docs/DEVICE_TEST_REPORT.md)
- [Mô tả phần cứng và cấu hình GPIO](docs/HARDWARE.md)\n- [Đối chiếu hai cấu hình XiaoZhi và pinout được chọn](docs/GPIO_CROSSCHECK.md)
- [Kiến trúc và kế hoạch phát triển](docs/VIDEO_PLAYER_PLAN.md)
- [Quy ước media và danh sách phát](docs/MEDIA_FORMAT.md)\n- [Đánh giá dự án mẫu và nguyên nhân audio kém](docs/SAMPLE_PROJECT_REVIEW.md)\n- [Firmware Arduino cải tiến](firmware/videoPlayer/videoPlayer.ino)\n- [Cấu hình TFT_eSPI](firmware/videoPlayer/User_Setup_Xingzhi.h)\n- [Nhiệm vụ Codex: kiểm tra COM9 và nạp thử](CODEX_DEVICE_TEST_TASK.md)

## Nguồn chính

- [Cấu hình bo xingzhi-cube-1.54tft-wifi trong xiaozhi-esp32](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/nologo/xingzhi-cube-1.54tft-wifi)
- [FAQ phần cứng của Nologo](https://www.nologo.tech/product/esp32/esp32s3/esp32s3ai/esp32s3xiaozhi/esp32s3ai_qa.html)
- [ESP32-S3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/)
