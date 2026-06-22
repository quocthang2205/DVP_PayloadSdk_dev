# =============================================================================
# CMakeLists.txt — Root build script cho project VIO Gimbal Angle Log
#
# Cấu trúc project:
#   libs/                   → thư viện dùng chung (user_lib)
#   apps/vio_gimbal_angle_log/  → ứng dụng chính: sweep góc gimbal + ghi CSV
#
# PayloadSDK nằm ở ../PayloadSdk (ngoài thư mục này).
# Phải chỉ định loại thiết bị khi cmake, ví dụ: cmake .. -DVIO=1
# =============================================================================

cmake_minimum_required(VERSION 3.5)
project(ThangDev)

set(CMAKE_BUILD_TYPE Debug)

set(PAYLOADSDK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../PayloadSdk)
set(PAYLOADSDKLIB_DIR ${PAYLOADSDK_DIR}/libs)

# Device type flags — pass one via: cmake .. -DVIO=1  (or ZIO, MB1, ORUSL)
if(VIO)
    add_definitions(-DVIO=${VIO})
    message("-- Device: VIO")
elseif(ZIO)
    add_definitions(-DZIO=${ZIO})
    message("-- Device: ZIO")
elseif(MB1)
    add_definitions(-DMB1=${MB1})
    message("-- Device: MB1")
elseif(ORUSL)
    add_definitions(-DORUSL=${ORUSL})
    message("-- Device: ORUSL")
else()
    message(FATAL_ERROR "No device type specified. Run cmake with one of: -DVIO=1, -DZIO=1, -DMB1=1, -DORUSL=1")
endif()

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-video-1.0
    gstreamer-app-1.0
)

find_package(Threads REQUIRED)

# Build PayloadSDK library from source
add_subdirectory(${PAYLOADSDK_DIR}/libs ${CMAKE_BINARY_DIR}/payloadsdk_libs)

add_subdirectory(libs)
add_subdirectory(apps/vio_gimbal_angle_log)
