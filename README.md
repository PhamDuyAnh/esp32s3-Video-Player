# esp32s3-Video-Player

Trình phát video từ thẻ microSD cho bo **xingzhi-cube-1.54tft-wifi** (ESP32-S3, LCD ST7789 240 × 240, loa I²S). Firmware hiện dùng Arduino-ESP32, phát luồng MJPEG thô ở **15 fps** cùng tệp WAV cùng tên. Cấu hình LCD ổn định ở 40 MHz và SD ở 20 MHz.

## Trạng thái

- Bo COM9 đã được nhận dạng, sao lưu flash, thử LCD, nút, thẻ, loa và microphone. Firmware trình phát đã được build và nạp lên bo; xem [báo cáo thử nghiệm](docs/DEVICE_TEST_REPORT.md).
- Menu chọn video và menu cài đặt đã được xác nhận hiển thị và thao tác được. Tác dụng của từng chế độ phát và âm lượng mới chưa được kiểm tra đầy đủ.
- Lỗi tự thoát ở video 4, 7, 8 đã được tái hiện qua Serial: khi hai task cùng đọc SD, một khối MJPEG nhận nhầm dữ liệu WAV. Firmware hiện đọc cả hai tệp trên cùng task, đệm PCM cho task I2S và dừng an toàn nếu JPEG hỏng. Video 4 chạy trọn 2.151 khung ở 15 fps, không rơi khung và không thiếu dữ liệu audio. Lỗi SELECT sau đó do I2S chiếm nhầm GPIO0; đã sửa và xác nhận nút dừng/chọn video trên bo.

## Chuẩn bị video và thẻ

[`videoConverter/videoConvert.py`](videoConverter/videoConvert.py) là công cụ chuyển mã của dự án. Đặt video nguồn vào `videoConverter/input_videos`, rồi chạy:

```powershell
python -m pip install imageio-ffmpeg
python videoConverter/videoConvert.py --framing pad
# Hoặc lấp đầy khung vuông, cắt phần thừa ở giữa:
python videoConverter/videoConvert.py --framing crop
```

Kết quả nằm trong `videoConverter/output_sd`: mỗi video tạo một cặp `tên.mjpeg` và `tên.wav`. `--framing pad` là mặc định: giữ toàn bộ hình đúng tỷ lệ rồi đệm đen đối xứng vào cạnh thiếu. `--framing crop` phóng theo cạnh ngắn để lấp đầy khung 240 × 240 rồi cắt giữa cạnh dài. Chạy lại với chế độ khác sẽ thay cặp tệp đầu ra cùng tên. Video là JPEG baseline 240 × 240, 15 fps; âm thanh là WAV PCM 16-bit mono 24 kHz. Tên tệp nguồn được chuẩn hóa thành ký tự ASCII an toàn. Converter kiểm tra khung hình và thời lượng trước khi thay kết quả cũ. Xem [quy ước media](docs/MEDIA_FORMAT.md).

Chép **cả hai tệp** của mỗi cặp vào thư mục gốc thẻ microSD. Có thể dùng đầu đọc thẻ và [`scripts/sync-media.ps1`](scripts/sync-media.ps1) để chép kèm kiểm tra SHA-256; xem [hướng dẫn chuyển tệp](docs/MEDIA_TRANSFER.md). Firmware hiện chưa có chức năng tải tệp qua USB hoặc Wi‑Fi.

## Sử dụng

- UP/DOWN: chọn video; nhấn ngắn SELECT rồi thả: phát.
- Giữ SELECT ít nhất 1 giây ở danh mục: mở cài đặt. Trong lúc phát, nhấn SELECT để dừng.
- Menu cài đặt cho phép chỉnh âm lượng, tự phát sau khởi động, phát một lượt hoặc lặp, phát một video hoặc cả danh mục, và thứ tự tuần tự hoặc ngẫu nhiên. Cài đặt và tên video được chọn được lưu trong bộ nhớ bo. Xem [hướng dẫn cài đặt](docs/PLAYBACK_SETTINGS.md).
- UART 115200 baud: `u`/`d` tương đương UP/DOWN, `s` tương đương SELECT ngắn (hoặc dừng video đang phát), `m` tương đương giữ SELECT để vào/thoát cài đặt. `0`–`9` chọn video theo chỉ số danh sách, `p` phát, `x` dừng, `?` in trạng thái nút SELECT. Các lệnh `A`–`F` dùng để đo profile kỹ thuật.

## Build và tài liệu

- [Build và nạp firmware](docs/BUILD.md)
- [Báo cáo thử thiết bị COM9](docs/DEVICE_TEST_REPORT.md)
- [Phần cứng và GPIO](docs/HARDWARE.md)
- [Đối chiếu GPIO với các biến thể bo khác](docs/GPIO_CROSSCHECK.md)
- [Đánh giá dự án mẫu](docs/SAMPLE_PROJECT_REVIEW.md)
- [Kế hoạch phát triển ban đầu](docs/VIDEO_PLAYER_PLAN.md)
- [Nhiệm vụ thử thiết bị ban đầu](CODEX_DEVICE_TEST_TASK.md)

## Nguồn tham khảo phần cứng

- [Cấu hình bo trong xiaozhi-esp32](https://github.com/78/xiaozhi-esp32/tree/main/main/boards/nologo/xingzhi-cube-1.54tft-wifi)
- [FAQ phần cứng Nologo](https://www.nologo.tech/product/esp32/esp32s3/esp32s3ai/esp32s3xiaozhi/esp32s3ai_qa.html)
- [Tài liệu ESP32-S3](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)
