# esp32s3-Video-Player

Trình phát video cục bộ cho bo **xingzhi-cube-1.54tft-wifi** (ESP32-S3, LCD ST7789 240×240, âm thanh I²S).

> Trạng thái: giai đoạn nghiên cứu phần cứng và lập kế hoạch. Chưa có firmware và chưa tuyên bố đã build/chạy trên bo thật.

## Mục tiêu

- Quét thư mục media trên thẻ TF/microSD.
- Chỉ nhận các tệp đúng quy ước và định dạng được hỗ trợ.
- Sắp xếp danh sách phát theo tên tệp, phát tuần tự và lặp lại.
- Hiển thị video 240×240 trên ST7789, đồng thời phát âm thanh qua loa tích hợp.
- Hoạt động ổn định khi gặp tệp lỗi, mất thẻ hoặc thiếu dữ liệu.

## Kết luận khả thi

Có thể biến bo thành thiết bị phát video độ phân giải thấp, nhưng không nên kỳ vọng MP4/H.264 như điện thoại. Phương án khuyến nghị là **Motion JPEG (MJPEG) + PCM mono trong AVI**, được chuyển mã trước trên PC. Mục tiêu ban đầu: 240×240, 12–15 fps, JPEG quality 8–12, PCM 16-bit mono 24 kHz.

Bo mạch có ESP32-S3 N16R8 (16 MB flash, 8 MB PSRAM), LCD ST7789 240×240 và ngõ âm thanh I²S. Tuy nhiên, nguồn chính thức đang dùng không khai báo giao tiếp thẻ TF tích hợp. Cần xác minh revision PCB và điểm hàn/chân mở rộng trước khi chốt sơ đồ kết nối thẻ. Nếu không có khe TF thật, cần module microSD 3,3 V ngoài.

## Tài liệu

- [Mô tả phần cứng và cấu hình GPIO](docs/HARDWARE.md)
- [Kiến trúc và kế hoạch phát triển](docs/VIDEO_PLAYER_PLAN.md)
- [Quy ước media và danh sách phát](docs/MEDIA_FORMAT.md)

## Nguồn chính

- [Cấu hình bo xingzhi-cube-1.54tft-wifi trong xiaozhi-esp32](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/nologo/xingzhi-cube-1.54tft-wifi)
- [FAQ phần cứng của Nologo](https://www.nologo.tech/product/esp32/esp32s3/esp32s3ai/esp32s3xiaozhi/esp32s3ai_qa.html)
- [ESP32-S3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
- [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/)
