// =============================================================================
// gimbal_attitude.cpp — Hiện thực GimbalAttitude
// =============================================================================

#include "../include/gimbal_attitude.h"

// Đăng ký callback SDK. Mỗi lần gọi attach() ghi đè callback cũ
// (SDK chỉ giữ 1 slot) → chỉ instance cuối cùng attach() nhận event.
void GimbalAttitude::attach(PayloadSdkInterface* sdk) {
    sdk->regPayloadStatusChanged([this](int event, double* param) {
        onEvent(event, param);
    });
}

// Trả về bản sao dữ liệu hiện tại. Nếu packet cuối cùng đến hơn STALE_MS
// ms trước, trả về {0,0,0, valid=false} mà KHÔNG xóa data_ gốc.
// Caller nhìn thấy zeros như thể gimbal đang ở vị trí 0 — an toàn hơn
// là trả về giá trị cũ có thể gây nhầm lẫn.
GimbalAttitude::Data GimbalAttitude::read() const {
    std::lock_guard<std::mutex> lk(mtx_);
    Data result = data_;
    if (result.valid) {
        auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - last_recv_).count();
        if (age_ms > STALE_MS) {
            // Packet quá cũ → báo mất kết nối, không trả giá trị lỗi thời
            result.valid = false;
            result.pitch = result.roll = result.yaw = 0.0f;
        }
    }
    return result;
}

bool GimbalAttitude::everReceived() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return ever_received_;
}

// Xóa state trước reconnect. Sau reset():
//   - read() luôn trả về valid=false (last_recv_ rất cũ từ epoch)
//   - everReceived() = false → disconnect detection tạm tắt
//   - Kích hoạt lại khi packet đầu tiên của session mới đến qua onEvent()
void GimbalAttitude::reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    data_          = Data{};
    ever_received_ = false;
    // last_recv_ không reset → nó sẽ stale ngay từ đầu (age > STALE_MS)
}

// Callback được gọi bởi SDK thread khi nhận MAVLINK attitude message từ gimbal.
// Cập nhật data_ và đánh dấu ever_received_ = true.
void GimbalAttitude::onEvent(int event, double* param) {
    if (event == PAYLOAD_GB_ATTITUDE) {
        std::lock_guard<std::mutex> lk(mtx_);
        data_.pitch    = (float)param[0];
        data_.roll     = (float)param[1];
        data_.yaw      = (float)param[2];
        data_.valid    = true;
        ever_received_ = true;
        last_recv_     = std::chrono::steady_clock::now();
    }
}
