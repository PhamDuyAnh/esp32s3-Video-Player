# Định dạng media hiện dùng

Firmware `videoPlayer` hiện phát cặp tệp **MJPEG thô + WAV** trong thư mục gốc thẻ microSD. Tài liệu kế hoạch ban đầu từng đề xuất AVI trong `/videos`; phương án đó chưa được triển khai.

```text
/
├── 1.mjpeg
├── 1.wav
├── 2.mjpeg
└── 2.wav
```

## Video

- Phần mở rộng `.mjpeg` (không phân biệt chữ hoa/thường); mỗi khung là JPEG baseline 240 × 240, nối liên tiếp trong tệp, không có header tự chế ở đầu.
- Firmware phát với nhịp cố định **15 khung/giây**; luồng MJPEG thô không mang timestamp hoặc FPS. Vì vậy video nguồn phải được chuyển đúng 15 fps trước khi chép lên thẻ.
- Bộ đệm khung lớn nhất là 96 KiB. Converter kiểm tra các marker JPEG và từ chối khung vượt giới hạn này.
- Khi video trễ hơn một chu kỳ khung, firmware bỏ khung để giữ tiến độ. Nếu luồng bị cắt hoặc đọc thẻ lỗi, log Serial ghi lý do kết thúc.

## Âm thanh

- Tệp WAV phải cùng tên gốc với video, ví dụ `1.mjpeg` đi với `1.wav`.
- Chuẩn tệp mới: RIFF/WAVE, PCM signed 16-bit little-endian, mono, **24.000 Hz**. `videoConvert.py` còn lọc và chuẩn hóa âm lượng khi chuyển mã.
- Firmware đọc WAV PCM 16-bit mono 24 kHz trên cùng task với MJPEG, đệm mẫu rồi phát qua I²S. WAV 22.050 Hz cũ không còn là định dạng được hỗ trợ; hãy chuyển mã bằng `videoConvert.py`.
- Nếu thiếu WAV, video vẫn phát và log báo tệp âm thanh không có.

## Quét thẻ và thứ tự

Firmware quét tối đa 50 tệp `.mjpeg` trực tiếp ở thư mục gốc, bỏ thư mục con và tên bắt đầu bằng dấu chấm. Danh sách được sắp xếp **theo chuỗi, không phân biệt hoa/thường**; ví dụ `1.mjpeg`, `10.mjpeg`, `2.mjpeg`. Nếu muốn đúng thứ tự số, đặt tiền tố đủ số chữ số như `001_`, `002_`, `010_`. Tên đã chọn được lưu để tự phát sau lần khởi động tiếp theo.

Menu cho phép phát một tệp hoặc cả danh sách, một lượt hoặc lặp lại, tuần tự hoặc ngẫu nhiên. Chi tiết ở [PLAYBACK_SETTINGS.md](PLAYBACK_SETTINGS.md).

## Chuyển mã và chép thẻ

Đặt video nguồn trong `videoConverter/input_videos`, chạy `python videoConverter/videoConvert.py --framing pad` để giữ toàn khung và đệm đen, hoặc dùng `--framing crop` để lấp đầy hình vuông và cắt phần thừa từ giữa. Mặc định là `pad`; hai chế độ dùng cùng tên đầu ra nên lượt chạy sau thay kết quả trước. Sau đó chép các cặp tệp từ `videoConverter/output_sd` vào thư mục gốc thẻ. Xem [README](../README.md) và [MEDIA_TRANSFER.md](MEDIA_TRANSFER.md). Chưa có chức năng tự tải tệp vào thẻ qua thiết bị.
