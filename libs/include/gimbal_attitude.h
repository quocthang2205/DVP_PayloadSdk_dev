// =============================================================================
// gimbal_attitude.h — Nhận và theo dõi dữ liệu góc từ gimbal
//
// GimbalAttitude đăng ký callback PAYLOAD_GB_ATTITUDE qua SDK để nhận
// góc pitch/roll/yaw theo thời gian thực.
//
// Stale detection:
//   Nếu không nhận packet trong STALE_MS (3s) → read() trả về valid=false
//   → main loop phát hiện mất kết nối và gọi handle_reconnect()
//
// everReceived() tránh false-positive reconnect khi mới khởi động:
//   - Lần đầu: chưa nhận packet nào → everReceived()=false → không reconnect
//   - Giữa chừng: đã nhận, nay mất → everReceived()=true → reconnect
//
// Callback slot collision:
//   SDK chỉ có 1 slot regPayloadStatusChanged() → attach() phải được gọi
//   CUỐI CÙNG, sau checkSdCardAndRecord() (dùng slot tạm thời).
// =============================================================================

#pragma once
#include "payloadSdkInterface.h"
#include <mutex>
#include <chrono>

class GimbalAttitude {
public:
    struct Data {
        float pitch = 0.0f;
        float roll  = 0.0f;
        float yaw   = 0.0f;
        bool  valid = false;  // false nếu chưa nhận hoặc data quá cũ (stale)
    };

    // Nếu không nhận được attitude packet trong STALE_MS, read() trả về {0,0,0,false}
    static constexpr int STALE_MS = 3000;

    // Gắn vào SDK — có thể gọi lại sau reconnect
    void attach(PayloadSdkInterface* sdk);

    // Đọc attitude mới nhất (thread-safe).
    // Trả về {0,0,0,false} nếu data stale (mất kết nối).
    Data read() const;

    // true nếu đã nhận được ít nhất 1 packet kể từ lần reset() gần nhất.
    // Dùng để phân biệt "chưa nhận lần nào" vs "đã có rồi bây giờ mất".
    bool everReceived() const;

    // Xóa toàn bộ state (data + everReceived). Gọi ngay trước khi reconnect.
    // Sau reset, disconnect detection tạm tắt cho đến khi packet đầu tiên đến.
    void reset();

private:
    mutable std::mutex mtx_;
    Data               data_;
    bool               ever_received_{false};
    std::chrono::steady_clock::time_point last_recv_;

    void onEvent(int event, double* param);
};
