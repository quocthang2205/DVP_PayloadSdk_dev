// =============================================================================
// angle_sweep.cpp — Tạo danh sách góc mục tiêu pitch/yaw ngẫu nhiên
// =============================================================================

#include "../include/angle_sweep.h"
#include <random>
#include <algorithm>

// Tạo lưới tất cả tổ hợp (pitch, yaw) trong khoảng [min, max] với bước step_deg,
// rồi xáo trộn ngẫu nhiên để gimbal không quét theo thứ tự tuyến tính.
//
// Xáo trộn ngẫu nhiên có 2 lợi ích:
//   1. Tránh bias khi gimbal luôn đi theo một hướng cố định (hysteresis)
//   2. Nếu chương trình bị ngắt giữa chừng, dữ liệu đã thu vẫn phân bố đều
std::vector<TargetAngle> make_angle_targets(
    float pitch_min, float pitch_max,
    float yaw_min,   float yaw_max,
    float step_deg)
{
    std::vector<TargetAngle> v;

    // Duyệt từ pitch_max xuống pitch_min (gimbal thường stable hơn khi đi từ trên xuống)
    // +/- 0.01f để tránh lỗi làm tròn floating-point bỏ sót điểm cuối
    for (float p = pitch_max; p >= pitch_min - 0.01f; p -= step_deg)
        for (float y = yaw_min; y <= yaw_max + 0.01f; y += step_deg)
            v.push_back({p, y});

    // Dùng random_device làm seed cho mt19937 → khác nhau mỗi lần chạy
    std::mt19937 rng(std::random_device{}());
    std::shuffle(v.begin(), v.end(), rng);
    return v;
}
