// =============================================================================
// main.cpp — VIO Gimbal Angle Log
//
// Ứng dụng đo sai lệch góc của gimbal VIO camera (Gremsy) bằng cách:
//   1. Kết nối đến payload qua UDP (192.168.16.200:14566), retry vô hạn
//   2. Khởi tạo: gimbal Follow mode, EO Super-Resolution zoom, object detection
//   3. Kiểm tra SD card → nếu có → bắt đầu quay video
//   4. Bắt đầu auto-zoom: cứ 10 phút tăng lên 1 mức zoom (1x→2x→...→30x→1x)
//   5. Sweep góc: gửi lệnh đến từng điểm (pitch, yaw) theo thứ tự ngẫu nhiên,
//      chờ gimbal ổn định (5s), đọc attitude thực tế, ghi sai số vào CSV
//   6. Keep-alive loop: sau khi sweep xong, tiếp tục giám sát kết nối
//
// Xử lý mất kết nối (bất kỳ lúc nào trong sweep hoặc keep-alive):
//   - Phát hiện: attitude.read() trả về valid=false sau STALE_MS (3s)
//   - Thứ tự xử lý để tránh deadlock với zoom thread:
//       signalStopZoom() → shutdownSdk() → joinZoomThread()
//       → attitude.reset() → reconnectAfterShutdown()
//       → init_after_connect() → startAutoZoom()
//   - Sau reconnect trong sweep: gửi lại lệnh gimbal cho đúng điểm đang đo
//     (inner while loop retry) để không bỏ qua điểm bị gián đoạn
//
// File output: vio_angle_log.csv (tạo trong thư mục chạy binary)
//   Cột: timestamp, sent_pitch, sent_yaw, recv_pitch, recv_roll, recv_yaw,
//         diff_pitch, diff_yaw, note
// =============================================================================

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <unistd.h>

#include "payload_client.h"
#include "gimbal_attitude.h"
#include "angle_sweep.h"
#include "csv_logger.h"
#include "utils.h"
#include "camera_control.h"
#include "payload-define/vio_sdk.h"

// ── Cấu hình ──────────────────────────────────────────────────────────────────
static const char* VIO_IP   = "192.168.16.200";
static const int   VIO_PORT = 14566;

static const int   WAIT_AFTER_SEND_SEC = 5;
static const int   WAIT_AFTER_READ_SEC = 5;

static const float ANGLE_STEP = 5.0f;
static const float PITCH_MIN  = -60.0f;
static const float PITCH_MAX  =   0.0f;
static const float YAW_MIN    = -90.0f;
static const float YAW_MAX    =  90.0f;

static const int   AUTO_ZOOM_INTERVAL_SEC = 600;   // 10 phút

// ──────────────────────────────────────────────────────────────────────────────

static std::string fmt2(float v) {
    std::ostringstream s;
    s << std::fixed << std::setprecision(2) << v;
    return s.str();
}

// Khởi tạo payload sau khi (re)connect:
//   - set gimbal/zoom mode
//   - reattach camera (obj detection, resume record)
//   - attach attitude callback CUỐI CÙNG để không bị ghi đè
static void init_after_connect(PayloadClient& client,
                               CameraControl& camera,
                               GimbalAttitude& attitude) {
    printf("Initializing payload...\n");

    client.sdk()->setPayloadCameraParam(
        (char*)PAYLOAD_CAMERA_GIMBAL_MODE,
        PAYLOAD_CAMERA_GIMBAL_MODE_FOLLOW,
        PARAM_TYPE_UINT32);
    usleep(500000);

    client.sdk()->setPayloadCameraParam(
        (char*)PAYLOAD_CAMERA_VIDEO_ZOOM_MODE,
        PAYLOAD_CAMERA_VIDEO_ZOOM_MODE_SUPER_RESOLUTION,
        PARAM_TYPE_UINT32);
    usleep(200000);

    // Reattach camera: re-enable obj detection + resume record nếu cần
    camera.reattachSdk(client.sdk());

    // Attitude callback phải đăng ký CUỐI vì SDK chỉ có 1 slot callback —
    // checkSdCardAndRecord() đăng ký tạm rồi xoá, nên attach attitude sau.
    attitude.attach(client.sdk());
}

// Xử lý mất kết nối — thứ tự quan trọng để tránh deadlock với zoom thread:
//   1. signal zoom dừng (chưa join)
//   2. kill SDK → unblock bất kỳ SDK call nào đang block trong zoom thread
//   3. join zoom thread (giờ mới an toàn)
//   4. reset state + reconnect + khởi động lại
static void handle_reconnect(PayloadClient& client,
                              CameraControl& camera,
                              GimbalAttitude& attitude) {
    printf("\n[!] Connection lost — waiting to reconnect...\n");

    // Bước 1: báo zoom thread dừng (chỉ set flag, chưa join)
    camera.signalStopZoom();

    // Bước 2: kill SDK — unblock bất kỳ sdk->setPayloadCameraParam() đang bị kẹt
    //         trong zoom thread do mất kết nối
    client.shutdownSdk();

    // Bước 3: an toàn join zoom thread (SDK đã kill nên call trong thread đã return)
    camera.joinZoomThread();

    // Bước 4: reset attitude để không kích hoạt lại detection ngay sau reconnect
    attitude.reset();

    // Bước 5: tạo lại SDK và kết nối lại — retry vô hạn, không dừng
    client.reconnectAfterShutdown();
    printf("[!] Reconnected!\n\n");

    // Bước 6: khởi tạo lại toàn bộ (gimbal mode, obj detection, record, attitude callback)
    init_after_connect(client, camera, attitude);

    // Bước 7: khởi động lại zoom thread (giữ nguyên zoom_index_)
    camera.startAutoZoom(AUTO_ZOOM_INTERVAL_SEC);

    // Chờ attitude packet đầu tiên (tối đa 3 giây)
    for (int i = 0; i < 30 && !attitude.everReceived(); ++i)
        usleep(100000);
    printf("[!] Session resumed.\n\n");
}

// ──────────────────────────────────────────────────────────────────────────────

int main() {
    printf("=== VIO Gimbal Angle Log ===\n");
    printf("Target: %s:%d\n\n", VIO_IP, VIO_PORT);

    PayloadClient client(make_udp_conn(VIO_IP, VIO_PORT));
    install_quit_handler(client);

    // Kết nối retry vô hạn
    printf("Waiting for payload connection (retry until connected)...\n");
    client.connectWithRetry(/*per_attempt_sec=*/5, /*retry_delay_sec=*/3);
    printf("VIO connected!\n\n");

    GimbalAttitude attitude;
    CameraControl  camera(client.sdk());

    // ── Lần đầu: check SD card rồi mới attach attitude (fix thứ tự callback) ──
    printf("Initializing payload...\n");

    client.sdk()->setPayloadCameraParam(
        (char*)PAYLOAD_CAMERA_GIMBAL_MODE,
        PAYLOAD_CAMERA_GIMBAL_MODE_FOLLOW,
        PARAM_TYPE_UINT32);
    usleep(500000);

    client.sdk()->setPayloadCameraParam(
        (char*)PAYLOAD_CAMERA_VIDEO_ZOOM_MODE,
        PAYLOAD_CAMERA_VIDEO_ZOOM_MODE_SUPER_RESOLUTION,
        PARAM_TYPE_UINT32);
    usleep(200000);

    camera.enableObjectDetection();
    usleep(200000);

    // checkSdCardAndRecord() dùng callback tạm → phải gọi TRƯỚC attitude.attach()
    camera.checkSdCardAndRecord(/*timeout_sec=*/5);

    // Attitude callback đăng ký CUỐI — không bị ghi đè bởi storage callback
    attitude.attach(client.sdk());

    // Bắt đầu auto zoom
    camera.startAutoZoom(AUTO_ZOOM_INTERVAL_SEC);

    // ── Sweep góc ─────────────────────────────────────────────────────────────
    CsvLogger csv("vio_angle_log.csv", {
        "timestamp",
        "sent_pitch", "sent_yaw",
        "recv_pitch", "recv_roll", "recv_yaw",
        "diff_pitch", "diff_yaw",
        "note"
    });

    auto targets = make_angle_targets(PITCH_MIN, PITCH_MAX, YAW_MIN, YAW_MAX, ANGLE_STEP);
    int  total   = (int)targets.size();

    printf("Sweep: pitch [%.0f..%.0f]  yaw [%.0f..%.0f]  step %.0f deg  → %d points\n\n",
           PITCH_MAX, PITCH_MIN, YAW_MIN, YAW_MAX, ANGLE_STEP, total);

    for (int i = 1; i <= total; ++i) {
        const auto& tgt = targets[i - 1];

        // Inner loop: retry điểm này nếu mất kết nối trong khi đang chờ data.
        // Sau khi reconnect, gửi lại lệnh gimbal cho đúng vị trí rồi đọc lại.
        GimbalAttitude::Data recv;
        while (true) {
            printf("[%d/%d] Send  pitch=%.2f  yaw=%.2f  (zoom level %d)\n",
                   i, total, tgt.pitch, tgt.yaw, camera.currentZoomIndex() + 1);
            fflush(stdout);

            client.sdk()->setGimbalSpeed(tgt.pitch, 0.0f, tgt.yaw, INPUT_ANGLE);
            countdown(WAIT_AFTER_SEND_SEC);

            recv = attitude.read();

            // Phát hiện mất kết nối: data stale VÀ đã từng nhận được packet.
            // Nếu everReceived() = false → chưa bao giờ có data, không retry.
            if (!recv.valid && attitude.everReceived()) {
                handle_reconnect(client, camera, attitude);
                // Sau reconnect: gửi lại lệnh gimbal cho điểm này (continue inner loop)
                printf("[!] Retrying point [%d/%d] after reconnect...\n\n", i, total);
                fflush(stdout);
                continue;
            }
            break;  // có data (hoặc chưa bao giờ có data) → tiếp tục log
        }

        if (!recv.valid)
            printf("  WARNING: no attitude data\n");

        float dp = tgt.pitch - recv.pitch;
        float dy = tgt.yaw   - recv.yaw;
        std::string ts   = timestamp_now();
        std::string note = recv.valid ? "" : "no_data";

        printf("[%s]\n"
               "  sent : pitch=%.2f  yaw=%.2f\n"
               "  recv : pitch=%.2f  roll=%.2f  yaw=%.2f%s\n"
               "  error: pitch=%.2f  yaw=%.2f\n\n",
               ts.c_str(),
               tgt.pitch, tgt.yaw,
               recv.pitch, recv.roll, recv.yaw,
               recv.valid ? "" : "  [no data]",
               dp, dy);

        csv.write({
            ts,
            fmt2(tgt.pitch), fmt2(tgt.yaw),
            fmt2(recv.pitch), fmt2(recv.roll), fmt2(recv.yaw),
            fmt2(dp), fmt2(dy),
            note
        });

        if (i < total) countdown(WAIT_AFTER_READ_SEC);
    }

    printf("Done. %d points logged to: vio_angle_log.csv\n\n", total);

    // ── Giữ session sau khi sweep xong ───────────────────────────────────────
    printf("Session active:\n");
    if (camera.isRecording()) printf("  - Recording: ON\n");
    printf("  - Object detection: ON\n");
    printf("  - Auto-zoom: ON (every %d s)\n", AUTO_ZOOM_INTERVAL_SEC);
    printf("Press Ctrl+C to stop.\n\n");

    while (true) {
        usleep(1000000);

        // Tiếp tục theo dõi kết nối trong keep-alive loop
        auto recv = attitude.read();
        if (!recv.valid && attitude.everReceived()) {
            handle_reconnect(client, camera, attitude);
        }
    }

    return 0;
}
