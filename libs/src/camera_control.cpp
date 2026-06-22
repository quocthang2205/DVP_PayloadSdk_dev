// =============================================================================
// camera_control.cpp — Hiện thực CameraControl
// =============================================================================

#include "../include/camera_control.h"
#include "payload-define/vio_sdk.h"
#include "payloadsdk.h"
#include <cstdio>
#include <chrono>
#include <unistd.h>

// Bảng 12 mức zoom EO Super-Resolution theo thứ tự tăng dần
static const uint32_t kZoomLevels[] = {
    ZOOM_SUPER_RESOLUTION_1X,
    ZOOM_SUPER_RESOLUTION_2X,
    ZOOM_SUPER_RESOLUTION_4X,
    ZOOM_SUPER_RESOLUTION_6X,
    ZOOM_SUPER_RESOLUTION_8X,
    ZOOM_SUPER_RESOLUTION_10X,
    ZOOM_SUPER_RESOLUTION_12X,
    ZOOM_SUPER_RESOLUTION_14X,
    ZOOM_SUPER_RESOLUTION_16X,
    ZOOM_SUPER_RESOLUTION_18X,
    ZOOM_SUPER_RESOLUTION_20X,
    ZOOM_SUPER_RESOLUTION_30X,
};
static const char* kZoomNames[] = {
    "1x","2x","4x","6x","8x","10x","12x","14x","16x","18x","20x","30x"
};
static const int kZoomCount = (int)(sizeof(kZoomLevels) / sizeof(kZoomLevels[0]));

// ── Constructor/Destructor ────────────────────────────────────────────────────

CameraControl::CameraControl(PayloadSdkInterface* sdk) : sdk_(sdk) {}

CameraControl::~CameraControl() {
    // Dừng zoom thread trước khi object bị hủy
    stopAutoZoom();
}

// ── Object detection ──────────────────────────────────────────────────────────

void CameraControl::enableObjectDetection() {
    printf("[Camera] Enable object detection\n");
    sdk_->setPayloadCameraParam(
        (char*)PAYLOAD_CAMERA_TRACKING_MODE,
        PAYLOAD_CAMERA_TRACKING_OBJ_DETECTION,
        PARAM_TYPE_UINT32);
}

void CameraControl::disableObjectDetection() {
    printf("[Camera] Disable object detection\n");
    sdk_->setPayloadCameraParam(
        (char*)PAYLOAD_CAMERA_TRACKING_MODE,
        PAYLOAD_CAMERA_TRACKING_OBJ_TRACKING,
        PARAM_TYPE_UINT32);
}

// ── Record ────────────────────────────────────────────────────────────────────

void CameraControl::startRecord() {
    if (recording_.load()) return;  // tránh gọi SDK hai lần nếu đã đang record
    printf("[Camera] Start recording\n");
    sdk_->setPayloadCameraRecordVideoStart();
    recording_ = true;
}

void CameraControl::stopRecord() {
    if (!recording_.load()) return;
    printf("[Camera] Stop recording\n");
    sdk_->setPayloadCameraRecordVideoStop();
    recording_ = false;
}

// ── SD card check ─────────────────────────────────────────────────────────────
//
// SDK chỉ có 1 slot callback cho regPayloadStatusChanged().
// Hàm này đăng ký callback tạm, gửi yêu cầu lấy storage info,
// chờ event PAYLOAD_CAM_STORAGE_INFO, rồi HỦY callback trước khi return.
// Phải gọi TRƯỚC attitude.attach() để không bị ghi đè.
//
// param layout: [0]=total_capacity(MB) [1]=used [2]=available [3]=status
// status >= 2 → thẻ nhớ đã format và sẵn sàng ghi

bool CameraControl::checkSdCardAndRecord(int timeout_sec) {
    std::atomic<bool> got{false};
    std::atomic<bool> has_sd{false};

    sdk_->regPayloadStatusChanged([&](int event, double* param) {
        if (event == PAYLOAD_CAM_STORAGE_INFO) {
            bool card_ready = (param[0] > 0.0 && param[3] >= 2.0);
            has_sd = card_ready;
            got    = true;
            printf("[Camera] Storage: total=%.0f MB  used=%.0f MB  avail=%.0f MB  status=%d\n",
                   param[0], param[1], param[2], (int)param[3]);
        }
    });

    printf("[Camera] Checking SD card...\n");
    sdk_->getPayloadStorage();  // gửi yêu cầu → SDK gọi callback khi có phản hồi

    auto start = std::chrono::steady_clock::now();
    while (!got.load()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= timeout_sec)
            break;
        usleep(100000);
    }

    // Xóa callback để slot trống cho attitude.attach() sau đó
    sdk_->regPayloadStatusChanged(nullptr);

    if (!got.load()) {
        printf("[Camera] SD card check timeout (%d s) — no response\n", timeout_sec);
        return false;
    }

    if (has_sd.load()) {
        printf("[Camera] SD card present — starting record\n");
        startRecord();
        return true;
    }

    printf("[Camera] No SD card detected\n");
    return false;
}

// ── Auto zoom ─────────────────────────────────────────────────────────────────

void CameraControl::startAutoZoom(int interval_sec) {
    if (zoom_run_.load()) return;  // tránh khởi động 2 lần
    zoom_run_ = true;

    zoom_thread_ = std::thread([this, interval_sec]() {
        printf("[Camera] Auto-zoom started — interval %d s, %d levels\n",
               interval_sec, kZoomCount);

        // Đặt về mức 1x khi khởi động (đồng bộ với zoom_index_ = 0 ban đầu)
        sdk_->setPayloadCameraParam(
            (char*)PAYLOAD_CAMERA_VIDEO_ZOOM_SUPER_RESOLUTION_FACTOR,
            kZoomLevels[0],
            PARAM_TYPE_UINT32);

        while (zoom_run_.load()) {
            // Sleep chia nhỏ thành từng 100ms để phản hồi signalStopZoom() nhanh
            for (int i = 0; i < interval_sec * 10 && zoom_run_.load(); ++i)
                usleep(100000);

            if (!zoom_run_.load()) break;

            // Tăng lên mức tiếp theo, vòng lại từ đầu khi hết bảng
            int next = (zoom_index_.load() + 1) % kZoomCount;
            zoom_index_ = next;

            printf("[Camera] Auto-zoom → %s (level %d/%d)\n",
                   kZoomNames[next], next + 1, kZoomCount);
            sdk_->setPayloadCameraParam(
                (char*)PAYLOAD_CAMERA_VIDEO_ZOOM_SUPER_RESOLUTION_FACTOR,
                kZoomLevels[next],
                PARAM_TYPE_UINT32);
        }

        printf("[Camera] Auto-zoom stopped\n");
    });
}

// Chỉ set flag — KHÔNG join. Gọi trước shutdownSdk() trong flow reconnect.
void CameraControl::signalStopZoom() {
    zoom_run_ = false;
}

// Chỉ join — gọi SAU khi SDK đã bị kill (zoom thread không còn block SDK call).
void CameraControl::joinZoomThread() {
    if (zoom_thread_.joinable())
        zoom_thread_.join();
}

// Dừng hoàn toàn: set flag + join. Dùng khi shutdown sạch (Ctrl+C, destructor).
void CameraControl::stopAutoZoom() {
    signalStopZoom();
    joinZoomThread();
}

// ── Reattach sau reconnect ────────────────────────────────────────────────────

void CameraControl::reattachSdk(PayloadSdkInterface* sdk) {
    sdk_ = sdk;
    printf("[Camera] Re-attaching to new SDK after reconnect\n");

    // Camera hardware có thể đã reset sau khi mất điện → phải bật lại
    enableObjectDetection();
    usleep(200000);

    // Nếu trước đó đang record, resume lại trên session mới
    if (recording_.load()) {
        printf("[Camera] Resuming record after reconnect\n");
        recording_ = false;   // reset flag vì hardware đã reset
        startRecord();
    }
}
