// =============================================================================
// utils.h — Tiện ích chung
// =============================================================================

#pragma once
#include <string>

// Trả về thời gian hiện tại theo định dạng "YYYY-MM-DD HH:MM:SS"
std::string timestamp_now();

// In đếm ngược trên cùng một dòng terminal, blocking đúng secs giây.
// Dùng để chờ gimbal di chuyển đến vị trí trước khi đọc attitude.
void countdown(int secs);
