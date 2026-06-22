// =============================================================================
// angle_sweep.h — Tạo danh sách góc mục tiêu pitch/yaw cho vòng lặp đo
//
// make_angle_targets() tạo lưới đầy đủ mọi tổ hợp (pitch, yaw) trong khoảng
// cho trước, sau đó xáo trộn ngẫu nhiên để gimbal không quét theo đường tuyến
// tính (tránh hysteresis bias và phân bố đều nếu bị ngắt giữa chừng).
//
// Ví dụ: pitch [-60..0], yaw [-90..90], step 5°
//   → 13 mức pitch × 37 mức yaw = 481 điểm, xáo trộn ngẫu nhiên
// =============================================================================

#pragma once
#include <vector>

struct TargetAngle {
    float pitch;  // độ, âm = nhìn xuống
    float yaw;    // độ, âm = trái, dương = phải
};

// Tạo lưới pitch/yaw ngẫu nhiên trong khoảng [min, max] với bước step_deg.
std::vector<TargetAngle> make_angle_targets(
    float pitch_min, float pitch_max,
    float yaw_min,   float yaw_max,
    float step_deg);
