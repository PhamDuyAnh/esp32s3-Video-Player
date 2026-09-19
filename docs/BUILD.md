# Build và nạp firmware cho xingzhi-cube-1.54tft-wifi

Trong PowerShell, chạy lệnh tại thư mục gốc dự án:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Sketch videoPlayer
powershell -ExecutionPolicy Bypass -File .\scripts\upload.ps1 -Sketch videoPlayer -Port COM9
```

Có thể thay `videoPlayer` bằng `selfTest` để build/nạp firmware kiểm tra phần cứng. Các tác vụ tương ứng cũng có trong VSCode. Tệp build nằm trong `.build/`; script dùng cấu hình `User_Setup_Xingzhi.h` của dự án qua cờ biên dịch và không sửa thư viện TFT_eSPI đã cài.

| Thành phần | Phiên bản đã thử |
|---|---|
| Arduino CLI | nightly-20260916 |
| Arduino-ESP32 | 2.0.17 |
| TFT_eSPI | 2.5.44 |
| JPEGDEC | 1.2.8 |
| ESP32-audioI2S | 2.0.0 |

FQBN trong script chọn ESP32-S3, flash quad 16 MB, PSRAM OPI, USB CDC và phân vùng ứng dụng 3 MB. Tùy chọn `EraseFlash=none` giữ nguyên dữ liệu khác trong flash. Không nâng cấp đồng loạt toolchain và thư viện khi chưa thử lại trên bo.

Bản `videoPlayer` hiện dùng bộ phát WAV PCM/I²S riêng, build khoảng 403 KB chương trình và 42 KB RAM tĩnh; đã nạp và xác minh hash trên COM9. Thư viện ESP32-audioI2S vẫn cần cho sketch `selfTest`. Kết quả đo trên thiết bị ở [DEVICE_TEST_REPORT.md](DEVICE_TEST_REPORT.md).
