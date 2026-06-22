// =============================================================================
// camera_control.h — Điều khiển camera VIO: zoom, record, object detection
//
// Module này wrap các lệnh SDK liên quan đến camera:
//
//  Object detection:
//    enableObjectDetection()  → bật chế độ theo dõi đối tượng (VIO tracking)
//    disableObjectDetection() → tắt
//
//  Video record:
//    startRecord() / stopRecord() → bắt đầu/dừng quay
//    checkSdCardAndRecord()       → kiểm tra thẻ nhớ qua callback,
//                                   nếu có thẻ → tự gọi startRecord()
//
//  Auto zoom (EO Super-Resolution, 12 mức: 1x→2x→4x→…→30x):
//    startAutoZoom(interval_sec) → chạy background thread, cứ interval_sec
//                                   giây tăng lên 1 mức zoom
//    stopAutoZoom()              → signal + join (sạch sẽ)
//    signalStopZoom()            → chỉ set flag, KHÔNG join
//    joinZoomThread()            → chỉ join, KHÔNG set flag
//
//  Tại sao tách signalStop / join?
//    Khi reconnect, thứ tự phải là:
//      signalStopZoom()  → set flag
//      shutdownSdk()     → kill SDK, unblock bất kỳ SDK call trong zoom thread
//      joinZoomThread()  → giờ mới an toàn
//    Nếu join trước khi kill SDK → deadlock nếu zoom thread đang bị block
//    bên trong setPayloadCameraParam() (dù UDP không block, đây là precaution).
//
//  Sau reconnect:
//    reattachSdk(new_sdk) → cập nhật con trỏ SDK, re-enable obj detection,
//                            resume recording nếu trước đó đang record.
// =============================================================================

#pragma once
#include "payloadSdkInterface.h"
#include <atomic>
#include <thread>

class CameraControl {
public:
    explicit CameraControl(PayloadSdkInterface* sdk);
    ~CameraControl();

    CameraControl(const CameraControl&) = delete;
    CameraControl& operator=(const CameraControl&) = delete;

    // Object detection (PAYLOAD_CAMERA_TRACKING_MODE)
    void enableObjectDetection();
    void disableObjectDetection();

    // Record video
    void startRecord();
    void stopRecord();
    bool isRecording() const { return recording_.load(); }

    // Kiểm tra SD card qua storage event callback (blocking, tối đa timeout_sec).
    // Nếu phát hiện thẻ nhớ (total>0 && status>=2) → tự động gọi startRecord().
    // QUAN TRỌNG: dùng regPayloadStatusChanged() tạm thời → phải gọi
    // TRƯỚC attitude.attach() vì SDK chỉ có 1 slot callback.
    bool checkSdCardAndRecord(int timeout_sec = 5);

    // Background thread: cứ interval_sec giây tăng 1 mức EO zoom.
    // Vòng: 1x→2x→4x→6x→8x→10x→12x→14x→16x→18x→20x→30x→1x→…
    void startAutoZoom(int interval_sec = 600);

    // Dừng hoàn toàn (signal + join) — dùng khi shutdown sạch (Ctrl+C / destructor).
    void stopAutoZoom();

    // Chỉ set flag zoom_run_=false, KHÔNG join — gọi TRƯỚC khi kill SDK.
    void signalStopZoom();

    // Chỉ join zoom thread — gọi SAU khi SDK đã bị kill để tránh deadlock.
    void joinZoomThread();

    // Cắm lại vào SDK mới sau reconnect.
    // Tự động re-enable obj detection và resume recording nếu recording_ còn true.
    void reattachSdk(PayloadSdkInterface* sdk);

    // Index mức zoom hiện tại (0–11, tương ứng 1x–30x)
    int currentZoomIndex() const { return zoom_index_.load(); }

private:
    PayloadSdkInterface* sdk_;
    std::atomic<bool>    recording_{false};
    std::atomic<bool>    zoom_run_{false};
    std::thread          zoom_thread_;
    std::atomic<int>     zoom_index_{0};  // index trong bảng kZoomLevels
};
