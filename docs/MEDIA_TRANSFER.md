# Chép media lên thẻ microSD

Firmware hiện đọc thẻ để phát video, chưa xuất thẻ thành ổ USB hoặc dịch vụ mạng.

## Dùng đầu đọc thẻ

1. Cắm thẻ vào đầu đọc trên máy tính và xác định ký tự ổ thẻ, ví dụ `X:`.
2. Từ thư mục gốc dự án, chạy:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\scripts\sync-media.ps1 -Destination X:\
   ```

3. Script chép các tệp `.mjpeg` và `.wav` từ `videoConverter/output_sd` vào thư mục gốc thẻ, rồi kiểm tra SHA-256 của từng tệp đích. Script không xóa tệp khác.
4. Tháo thẻ an toàn và lắp lại vào bo.

Có thể chép thủ công nếu muốn; luôn chép đủ cặp video và âm thanh cùng tên. Nếu media mới chưa được chép, firmware vẫn phát các tệp cũ trên thẻ.

## Khả năng chuyển qua thiết bị

Truyền qua Serial cần một giao thức nhị phân riêng và quyền sử dụng thẻ độc quyền trong khi ghi; với bộ video lớn, thời gian truyền có thể dài. Web file manager qua Wi‑Fi là khả thi về kỹ thuật nhưng **chưa được cài đặt**. Khi phát triển, cần tạm dừng phát khi ghi/xóa, ghi vào tệp tạm rồi kiểm tra và đổi tên, xác thực truy cập và xử lý truyền bị ngắt. Đầu đọc thẻ hiện là cách đã có thể sử dụng.
