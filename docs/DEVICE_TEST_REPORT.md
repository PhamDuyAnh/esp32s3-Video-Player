# Báo cáo thử thiết bị COM9

Thiết bị: `xingzhi-cube-1.54tft-wifi`. Bắt đầu thử ngày 2026-09-18; cập nhật đến 2026-09-19.

## Nhận dạng và bảo vệ dữ liệu

- Esptool 4.5.1 nhận dạng COM9 là ESP32-S3 revision 0.2, flash quad 16 MB (hãng `c8`, mã `4018`). Chưa quan sát trực tiếp mã revision trên PCB.
- Đã sao lưu đủ 16.777.216 byte flash vào `.device-backups/COM9_esp32s3_2026-09-18_16MiB.bin`, SHA-256 `E5BEF42575A3792DF4293521B4451C5EFEF377648DAD16B51C8CD3C2D76663DF`. Thời gian đọc 1.885,9 giây ở 921600 baud. Thư mục sao lưu bị Git bỏ qua; không công bố tệp này vì có thể chứa thông tin đăng nhập.
- Các lần nạp dùng xác minh hash, không xóa toàn bộ flash, không format thẻ và không chép media lên thẻ.

## Công cụ và tự kiểm tra phần cứng

Bộ build: Arduino CLI nightly-20260916, Arduino-ESP32 2.0.17, TFT_eSPI 2.5.44, JPEGDEC 1.2.8, ESP32-audioI2S 2.0.0. Firmware `selfTest` build thành công: 768.833 byte chương trình (24%) và 35.496 byte RAM tĩnh (10%). Nạp COM9 thành công, các phân đoạn đều được xác minh hash.

| Hạng mục | Kết quả |
|---|---|
| Khởi động | CPU 240 MHz; flash 16.777.216 byte; PSRAM được phát hiện, còn 8.386.247 byte; heap nội bộ còn 315.140 byte; mã reset 4. |
| LCD | Người dùng xác nhận năm màu hiển thị đúng; chưa có mô tả riêng về hướng chữ hoặc artefact trong lần self-test. |
| Nút GPIO0/39/40 | Serial đã ghi nhận các cạnh nhấn/thả qua những lượt thử. Một lần nhấn SELECT có báo cáo từ người dùng nhưng không bắt được cạnh mới trong cửa sổ log đó. Menu cài đặt về sau đã thao tác được bằng các nút. |
| Thẻ SD 20 MHz | Nhận thẻ khoảng 32,22 GB, liệt kê tệp thành công; đọc tuần tự 4 MiB trong 2.591 ms, khoảng 1.580 KB/s, không lỗi. |
| Thẻ SD 40 MHz | Đọc 4 MiB trong 2.595 ms, khoảng 1.578 KB/s; không có lợi về tốc độ nên quay lại 20 MHz. |
| Loa | Âm chuẩn 1 kHz/24 kHz chạy 8 giây, mức đỉnh 6.000/32.767; người dùng báo nghe ổn. WAV mới đúng 24 kHz trên thẻ còn chờ thử. |
| Microphone | Khi tạo tiếng động, đỉnh I²S 16 kHz tăng từ khoảng 700 lên 8.343. |

Firmware self-test không ghi thẻ và gọi SD API với `format_if_empty=false`; các tệp trong ứng dụng được mở bằng `FILE_READ`. Arduino SD API ở cấu hình này không có tùy chọn mount chỉ đọc.

Lần nạp self-test đầu gặp panic khi in kích thước tệp: giá trị `size_t` 32-bit được truyền sai cho `%llu`. Đã sửa ép kiểu sang `unsigned long long` và thử lại thành công. Phép thử âm chuẩn ban đầu cũng không chiếm được I²S0 do thư viện Audio đã đăng ký cổng đó; bản sửa dùng I²S1 và giải phóng trước khi đo mic. Lệnh Serial `r` chạy lại chuỗi self-test; `s` đo SD 40 MHz rồi trả về 20 MHz.

## Trình phát và media

- `7.mjpeg` cũ ở 10 fps, audio tắt: giải mã 1.937 khung, bỏ 0 khung trong khoảng 194 giây; người dùng xác nhận hình đúng.
- Cùng tệp ở 15 fps, audio tắt: giải mã 2.894 khung, bỏ 8 khung (0,28%) trong khoảng 193 giây; người dùng thấy chuyển động mượt hơn, một số cảnh vỡ hạt do nguồn nén.
- `7.mjpeg` và `8.mjpeg` cũ bắt đầu bằng byte tự tạo `25`, sau đó là JPEG hợp lệ; video gốc 25 fps, WAV PCM mono 16-bit/22.050 Hz. Firmware bỏ qua byte lạ nhưng đây không phải profile 15 fps/24 kHz mới.
- Kiểm tra `8.mjpeg` cũ trên máy: 5.049 khung JPEG đầy đủ, khung lớn nhất 12.936 byte, không khung nào vượt bộ đệm 96 KiB. Lỗi treo hình trong khi âm thanh tiếp tục và SELECT không phản hồi nhiều khả năng liên quan truy cập FAT/SD đồng thời. Firmware đã dùng mutex chung cho các lần đọc SD, giới hạn mỗi lần đọc video 16 KiB và kiểm tra SELECT/Serial trong lúc dò marker JPEG.
- Sau sửa, một lượt phát video 8 giải mã 926 khung; SELECT đã dừng, đóng audio và về danh mục. Đã thêm 50 ms chờ giải phóng I²S/FAT và 300 ms chống nhấn nhầm khi chuyển về menu. Cần thử lặp nhiều lần với media mới.
- Video 4 cũ từng được người dùng báo tự thoát sau 1–2 giây. Sau đó thử lại bằng Serial và nút: đã chạy hơn 50 giây, khoảng 15 fps, không tự thoát. Lần ghi log dài hơn đạt khoảng 98 giây rồi dừng chủ động bằng lệnh `x`: `reason=serial-x pos=5816320/10111368 decoded=1475 dropped=1`. Firmware hiện ghi `reason` và vị trí đọc khi kết thúc để xác định nguyên nhân nếu lỗi tái diễn.
- Menu cài đặt đã được người dùng xác nhận vào và đổi giá trị được. Chưa xác nhận tác dụng thực tế của từng chế độ hoặc mức âm lượng.

## Thử khả năng 24–25 fps ngày 2026-09-19

- LCD SPI 40 MHz, video 7 cũ, chỉ hình ở 24 fps: giai đoạn đầu đạt khoảng 20,5–22,3 fps và tích lũy 165 khung bỏ; đẩy LCD mất khoảng 26,8 ms/khung.
- LCD SPI 80 MHz giảm thời gian đẩy LCD xuống khoảng 15,25 ms/khung. Cảnh nhẹ đạt 24 fps, nhưng cảnh nặng chỉ còn 15–22 fps và tích lũy 158 khung bỏ. Chưa thử hình + tiếng ở 24–25 fps vì chỉ hình đã không đạt ổn định. LCD 80 MHz cũng chưa qua thử hiển thị 30 phút.
- Đã khôi phục và nạp lại cấu hình đã thử ổn định: LCD 40 MHz, SD 20 MHz, 15 fps.
- `videoConvert.py` đã tạo lại sáu video nguồn 1, 2, 3, 4, 7, 8 trên máy tính. Kết quả `.mjpeg` bắt đầu ngay bằng JPEG SOI; WAV là PCM mono 16-bit/24 kHz. Video 8 mới có 3.028 khung, JPEG lớn nhất 4.669 byte, sai lệch thời lượng hình/tiếng khoảng 0,05 giây. Chưa chép các tệp mới lên thẻ bo.

## Trạng thái tiếp theo

Giữ profile 15 fps, LCD 40 MHz và SD 20 MHz. Lỗi tự thoát của video 4, 7, 8 tái hiện được hoàn toàn bằng lệnh Serial, nên không xuất phát từ nút SELECT. Trước sửa, video 4 đọc đúng khối đầu nhưng khối tại offset 16.384 chứa dữ liệu của `4.wav` tại offset 307.200; JPEG lỗi và đôi khi làm treo trình phát. Tắt audio thì video 4 đọc đúng; chờ thêm 1,2 giây, seek lại và ghim hai task vào cùng lõi đều không giải quyết. Đổi thứ tự mở file chuyển lỗi từ video sang audio. Firmware mới chỉ đọc WAV và MJPEG từ một task, đưa PCM qua bộ đệm sang task I2S; không còn hai task gọi SD song song. Năm lượt phát video 4/7/8 qua Serial đạt gần 15 fps, không có lỗi JPEG, audio vẫn báo đang chạy. Lượt thử dài tiếp theo: video 4 chạy 16 giây, 7 chạy 9 giây, 8 chạy 16 giây; tất cả giữ gần 15 fps và underrun=0. Video 4 cũng đã chạy trọn 2.151 khung đến EOF, không rơi khung, underrun=0 sau khi sửa xử lý mẩu PCM cuối. Firmware cũng dừng sau ba JPEG lỗi liên tiếp và thử lại một lần để tránh tiếp tục giải mã dữ liệu hỏng. Cần người dùng xác nhận âm thanh thực nghe, rồi thử phát trọn các tệp còn lại và các chế độ menu.

Tệp flash sao lưu có thể được khôi phục bằng esptool `--port COM9 write_flash 0x0 <đường-dẫn-sao-lưu>`, nhưng thao tác này ghi đè cả NVS và thông tin đăng nhập hiện tại. Chỉ dùng sau khi xác nhận đúng thiết bị và chủ động muốn quay về ảnh flash cũ; không dùng `erase_flash`.

## Sửa lỗi nút SELECT sau khi đổi sang PCM/I²S

Firmware PCM/I²S đầu tiên làm SELECT mất tác dụng: 439 lần lấy mẫu UART đều đọc GPIO0 ở LOW dù người dùng đã thả và nhấn nút. Trong Arduino-ESP32 2.0.17, `i2s_pin_config_t` có trường `mck_io_num` đứng trước các chân BCLK/WS/DOUT; khởi tạo cấu trúc bằng 0 đã gán MCLK vào GPIO0. Đặt trường này thành `I2S_PIN_NO_CHANGE` ở player và self-test đưa GPIO0 về HIGH khi thả nút. Người dùng xác nhận LCD phản ứng đúng; log ghi `reason=select` khi dừng video, tiếp theo là sự kiện SELECT ngắn và phát lại video từ danh mục. UART cũng có các lệnh `u`, `d`, `s`, `m` để thao tác menu và `?` để xem trạng thái SELECT.
