# Menu phát và cài đặt

Ở màn hình `SELECT VIDEO`, dùng UP/DOWN chọn tệp trên thẻ; nhấn ngắn rồi thả SELECT để phát. Giữ SELECT ít nhất một giây để mở `SETTINGS`. Khi đang phát, nhấn SELECT để dừng và trở về danh mục.

Trong `SETTINGS`, UP/DOWN chọn dòng, SELECT đổi giá trị. Ở dòng `VOLUME`, nhấn SELECT để vào chế độ chỉnh, dùng UP/DOWN đặt mức 0–21, rồi nhấn SELECT để hoàn tất. Chọn `BACK` hoặc giữ SELECT để quay về danh mục. Cài đặt và tên video được chọn được lưu trong bộ nhớ không mất điện của ESP32-S3.

| Mục trên LCD | Giá trị | Tác dụng |
|---|---|---|
| `VOLUME` | 0–21 | Mức âm lượng áp dụng cho lần phát sau. |
| `AUTO` | OFF / ON | ON: khoảng một giây sau khởi động, tự phát video đã lưu. |
| `PLAY` | ONE / REPEAT | ONE: phát một lượt rồi về danh mục. REPEAT: lặp cho đến khi nhấn SELECT. |
| `FILES` | ONE / MULTI | ONE: chỉ video được chọn. MULTI: phát toàn bộ tệp `.mjpeg` trong thư mục gốc thẻ, bắt đầu từ video được chọn. |
| `ORDER` | SEQ / RANDOM | Chỉ tác động khi `FILES=MULTI`. SEQ: theo tên tệp đã sắp xếp và quay vòng. RANDOM: mỗi lượt phát đủ các tệp, không lặp trong một lượt; lượt đầu bắt đầu bằng video được chọn. |

Mặc định: âm lượng 6, `AUTO=OFF`, `PLAY=ONE`, `FILES=ONE`, `ORDER=SEQ`. Tốc độ phát mặc định là 15 fps. Người dùng đã xác nhận vào menu và đổi được các lựa chọn; tác dụng thực tế của từng lựa chọn và âm lượng còn chờ thử đầy đủ.
