# DVP_PayloadSdk_dev

Workspace phát triển ứng dụng điều khiển gimbal/camera Gremsy dựa trên PayloadSDK.

## Cấu trúc thư mục

```
thang_payloadsdk_dev/
├── CMakeLists.txt                    # Root build script
├── libs/                             # Thư viện dùng chung (user_lib)
│   ├── include/
│   │   ├── payload_client.h          # Quản lý kết nối SDK
│   │   ├── gimbal_attitude.h         # Đọc góc gimbal, phát hiện mất kết nối
│   │   ├── camera_control.h          # Zoom, record, object detection
│   │   ├── angle_sweep.h             # Tạo danh sách góc mục tiêu
│   │   ├── csv_logger.h              # Ghi kết quả ra CSV
│   │   └── utils.h                   # Timestamp, countdown
│   └── src/
│       └── *.cpp
└── apps/
    └── vio_gimbal_angle_log/         # Ứng dụng đo sai lệch góc gimbal VIO
        ├── CMakeLists.txt
        └── main.cpp
```

> PayloadSDK nằm **ngoài** thư mục này, tại `../PayloadSdk` — xem hướng dẫn clone bên dưới.

---

## Hướng dẫn Build

### 1. Clone PayloadSDK

PayloadSDK phải được đặt **cùng cấp** với thư mục này (tức là tại `../PayloadSdk` tính từ root của repo).

```bash
cd ~/Gremsy   # hoặc thư mục cha của thang_payloadsdk_dev
git clone https://github.com/Gremsy/PayloadSdk.git
```

Sau bước này, cấu trúc thư mục phải là:

```
~/Gremsy/
├── PayloadSdk/              ← PayloadSDK clone từ GitHub
└── thang_payloadsdk_dev/    ← repo này
```

### 2. Cài đặt dependencies

```bash
sudo apt update
sudo apt install -y \
    cmake build-essential \
    libglib2.0-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev
```

### 3. Build

```bash
cd thang_payloadsdk_dev
mkdir -p build && cd build
cmake .. -DVIO=1
make -j$(nproc)
```

**Tham số `-D` chọn loại thiết bị** (bắt buộc, chọn một trong các giá trị sau):

| Tham số    | Thiết bị          |
|------------|-------------------|
| `-DVIO=1`  | VIO gimbal camera |
| `-DZIO=1`  | ZIO               |
| `-DMB1=1`  | MB1               |
| `-DORUSL=1`| ORUSL             |

File binary sau khi build nằm tại:

```
build/apps/vio_gimbal_angle_log/vio_gimbal_angle_log_v2
```

---

## Hướng dẫn sử dụng: `vio_gimbal_angle_log`

### Mục đích

Đo sai lệch góc thực tế của gimbal VIO so với góc lệnh gửi đi. Kết quả được ghi vào file CSV để phân tích độ chính xác.

### Yêu cầu

- Thiết bị VIO kết nối vào máy tính qua mạng, địa chỉ IP mặc định: `192.168.16.200`, port `14566` (UDP)
- SD card đã cắm vào thiết bị (để ghi video tự động)

### Chạy chương trình

```bash
cd build/apps/vio_gimbal_angle_log
./vio_gimbal_angle_log_v2
```

Chương trình sẽ tự động retry kết nối vô hạn cho đến khi thiết bị online.

### Luồng hoạt động

1. **Kết nối** đến `192.168.16.200:14566`, retry cho đến khi thành công
2. **Khởi tạo**: đặt gimbal về Follow mode, bật Super-Resolution zoom, bật object detection
3. **Kiểm tra SD card** → nếu có thì bắt đầu quay video
4. **Auto-zoom**: cứ mỗi 10 phút tăng lên 1 mức zoom (1x → 2x → ... → 30x → 1x)
5. **Sweep góc**: gửi lệnh đến từng điểm `(pitch, yaw)` theo thứ tự ngẫu nhiên trong lưới:
   - Pitch: `[-60°, 0°]`, bước `5°`
   - Yaw: `[-90°, 90°]`, bước `5°`
   - Mỗi điểm: chờ 5 giây cho gimbal ổn định, đọc góc thực tế, ghi sai số vào CSV
6. **Keep-alive**: sau khi sweep xong, tiếp tục giữ kết nối và giám sát cho đến khi bấm `Ctrl+C`

### Xử lý mất kết nối

Nếu mất kết nối tại bất kỳ thời điểm nào trong quá trình sweep hoặc keep-alive:
- Chương trình tự động reconnect (retry vô hạn)
- Sau khi reconnect, gửi lại lệnh cho đúng điểm đang đo (không bỏ qua điểm bị gián đoạn)
- Khởi động lại zoom thread, object detection và record

### File output

Kết quả được ghi vào `vio_angle_log.csv` trong thư mục chạy binary:

| Cột          | Mô tả                                      |
|--------------|--------------------------------------------|
| `timestamp`  | Thời điểm đo                               |
| `sent_pitch` | Góc pitch lệnh gửi đi (độ)                 |
| `sent_yaw`   | Góc yaw lệnh gửi đi (độ)                   |
| `recv_pitch` | Góc pitch đọc từ gimbal (độ)               |
| `recv_roll`  | Góc roll đọc từ gimbal (độ)                |
| `recv_yaw`   | Góc yaw đọc từ gimbal (độ)                 |
| `diff_pitch` | Sai số pitch = sent − recv (độ)            |
| `diff_yaw`   | Sai số yaw = sent − recv (độ)              |
| `note`       | Ghi chú (ví dụ: `no_data` nếu mất tín hiệu)|

### Dừng chương trình

```
Ctrl+C
```
