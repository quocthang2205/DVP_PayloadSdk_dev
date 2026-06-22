# =============================================================================
# libs/CMakeLists.txt — Build thư viện tĩnh user_lib
#
# user_lib gom tất cả module dùng chung:
#   payload_client   → quản lý vòng đời kết nối SDK (connect/retry/reconnect)
#   gimbal_attitude  → nhận và kiểm tra dữ liệu góc gimbal (stale detection)
#   camera_control   → điều khiển zoom, record, object detection
#   angle_sweep      → tạo danh sách góc mục tiêu ngẫu nhiên (pitch/yaw grid)
#   csv_logger       → ghi kết quả đo vào file CSV
#   utils            → timestamp, countdown timer
#
# Các app chỉ cần link user_lib là đủ — include paths và PayloadSDK
# được propagate tự động qua PUBLIC target_include_directories.
# =============================================================================

add_library(user_lib STATIC
    src/payload_client.cpp
    src/gimbal_attitude.cpp
    src/angle_sweep.cpp
    src/csv_logger.cpp
    src/utils.cpp
    src/camera_control.cpp
)

# PUBLIC: run targets that link user_lib sẽ tự thấy include/ và PayloadSDK headers
target_include_directories(user_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PAYLOADSDKLIB_DIR}
    ${PAYLOADSDKLIB_DIR}/third-party/mavlink/include
    ${PAYLOADSDKLIB_DIR}/gSDK/src/mavlink/include/mavlink/v2.0
    ${PAYLOADSDKLIB_DIR}/gSDK/src
)

target_link_libraries(user_lib PUBLIC
    PayloadSDK
    ${CMAKE_THREAD_LIBS_INIT}
)
