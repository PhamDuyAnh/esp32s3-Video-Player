# Quy ước media và danh sách phát

## 1. Cấu trúc thẻ

```text
/
└── videos/
    ├── 001_intro.avi
    ├── 002_demo.avi
    ├── 010_outro.avi
    └── README.txt
```

Firmware chỉ quét trực tiếp `/videos` ở phiên bản đầu, không đệ quy thư mục con.

## 2. Tệp được xem là ứng viên

Một directory entry chỉ được đưa vào bước kiểm tra nội dung khi:

- Là regular file, không phải thư mục.
- Tên không bắt đầu bằng `.`.
- Extension là `.avi`, không phân biệt hoa/thường.
- Độ dài đường dẫn nằm trong giới hạn cấu hình.
- Kích thước lớn hơn kích thước header tối thiểu và nhỏ hơn giới hạn filesystem.
- Không phải file tạm như `._*`, `~*` hoặc `*.tmp`.

Extension đúng chưa có nghĩa là media hợp lệ.

## 3. Kiểm tra nội dung bắt buộc

Trước khi phát, parser phải xác nhận:

- RIFF/AVI signature hợp lệ.
- Có đúng một video stream MJPEG được hỗ trợ.
- Kích thước frame đúng 240×240.
- Frame rate nằm trong khoảng cho phép, đề xuất 8–15 fps ở bản đầu.
- JPEG baseline; từ chối progressive và kích thước compressed frame vượt giới hạn.
- Có tối đa một audio stream PCM.
- Audio đúng PCM signed 16-bit little-endian, mono, 24.000 Hz.
- Chunk size, offset và phép cộng không overflow.
- Chunk không vượt quá kích thước file thực.
- Index lỗi/thiếu phải được xử lý an toàn; bản đầu có thể yêu cầu index hợp lệ để giảm độ phức tạp.

Nếu một điều kiện không đạt: ghi log lý do, hiển thị lỗi ngắn, đóng file và chuyển sang file kế tiếp.

## 4. Thứ tự phát

Dùng natural sort, không phân biệt chữ hoa/thường:

```text
1_intro.avi
2_demo.avi
10_outro.avi
```

Không dùng thứ tự directory của FAT vì không ổn định sau khi copy/xóa file.

Khóa sắp xếp đề xuất:

1. So sánh từng token.
2. Chuỗi chữ: lowercase để so sánh.
3. Chuỗi số: so sánh theo giá trị số, không theo từ điển.
4. Nếu bằng nhau: tên gốc làm tie-breaker.
5. Nếu vẫn bằng: full path làm tie-breaker.

Với playlist lớn, giới hạn mặc định đề xuất là 1.000 tệp. Entry vượt giới hạn bị bỏ qua và ghi log.

## 5. Luật phát

- Khi khởi động: mount → scan → sort → phát mục đầu.
- Kết thúc bình thường: chuyển mục kế tiếp.
- File lỗi: chuyển mục kế tiếp, không reboot.
- Hết danh sách: quay lại mục đầu nếu `repeat_all=true`.
- Next: đóng file hiện tại an toàn rồi chuyển mục kế.
- Previous: nếu thời gian phát hiện tại > 3 giây thì phát lại file hiện tại; nếu không, về file trước.
- Pause: giữ vị trí, ngừng I²S sạch và giữ frame hiện tại.
- Thẻ bị tháo: dừng phát, xóa playlist, về `NO_CARD`.
- Khi thẻ xuất hiện lại: debounce trạng thái, mount và scan lại.

## 6. Tên tệp và Unicode

Để bản đầu dễ kiểm chứng, khuyến nghị tên ASCII:

```text
NNN_ten-ngan.avi
```

Ví dụ: `001_sadec.avi`.

FAT long filename/UTF-8 chỉ bật sau khi cấu hình FatFs và locale được kiểm thử. Không cắt chuỗi UTF-8 giữa code point khi hiển thị OSD.

## 7. Quy trình chuẩn bị thẻ

1. Sao lưu dữ liệu thẻ.
2. Format FAT32 bằng công cụ phù hợp.
3. Tạo thư mục `videos`.
4. Chuyển mã từng video theo profile dự án.
5. Kiểm tra bằng `ffprobe`.
6. Copy file theo thứ tự tên.
7. Eject an toàn.
8. Thiết bị mount read-only.

Lệnh kiểm tra ví dụ:

```bash
ffprobe -v error -show_entries \
stream=index,codec_name,codec_type,width,height,pix_fmt,r_frame_rate,sample_rate,channels,sample_fmt \
-of default=noprint_wrappers=1 output.avi
```

Kết quả mong đợi: video `mjpeg`, 240×240, 12 fps; audio `pcm_s16le`, 24.000 Hz, mono.

## 8. Bộ file kiểm thử tối thiểu

- Một file hợp lệ 10 giây.
- Một file hợp lệ ít chuyển động.
- Một file hợp lệ chuyển động mạnh/JPEG lớn.
- File không audio.
- File extension đúng nhưng nội dung sai.
- File AVI bị cắt cuối.
- JPEG progressive.
- Sai độ phân giải.
- Sai sample rate/audio stereo.
- Tên có số `1, 2, 10`.
- Tên dài và Unicode.
- File 0 byte.
- Thẻ gần đầy và thẻ chậm.

Không dùng dữ liệu duy nhất/chưa sao lưu để thử thao tác rút thẻ hoặc brownout.
