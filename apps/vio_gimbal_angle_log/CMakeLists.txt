# =============================================================================
# apps/vio_gimbal_angle_log/CMakeLists.txt
#
# Build executable: vio_gimbal_angle_log_v2
# Đây là ứng dụng chính điều khiển VIO gimbal camera:
#   - Kết nối đến payload qua UDP (192.168.16.200:14566)
#   - Sweep qua các góc pitch/yaw ngẫu nhiên, đọc góc thực từ gimbal, ghi CSV
#   - Tự động zoom, record, object detection
#   - Tự reconnect khi mất kết nối, retry đúng điểm đang đo
# =============================================================================

add_executable(vio_gimbal_angle_log_v2 main.cpp)

target_link_libraries(vio_gimbal_angle_log_v2
    user_lib
)
