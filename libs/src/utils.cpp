// =============================================================================
// utils.cpp — Tiện ích chung: timestamp và countdown timer
// =============================================================================

#include "../include/utils.h"
#include <cstdio>
#include <ctime>
#include <unistd.h>

// Trả về thời gian hiện tại theo định dạng "YYYY-MM-DD HH:MM:SS"
// để ghi vào cột timestamp trong CSV.
std::string timestamp_now() {
    time_t t = time(nullptr);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&t));
    return std::string(buf);
}

// In đếm ngược trên cùng một dòng terminal (overwrite liên tục bằng \r),
// blocking trong đúng secs giây. Dùng để cho gimbal có thời gian di chuyển
// đến vị trí mục tiêu trước khi đọc attitude.
void countdown(int secs) {
    for (int i = secs; i > 0; --i) {
        printf("\r  settling... %2d s remaining  ", i);
        fflush(stdout);
        sleep(1);
    }
    printf("\n");
}
