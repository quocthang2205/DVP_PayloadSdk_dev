// =============================================================================
// payload_client.h — Quản lý vòng đời kết nối với Gremsy PayloadSDK
//
// PayloadClient bọc PayloadSdkInterface, giải quyết 3 vấn đề chính:
//
//  1. sdkInitConnection() block mãi mãi khi payload tắt (loop chờ sysid != 0).
//     → Fix: chạy sdkInitConnection() + checkPayloadConnection() trong background
//       thread; main thread poll timeout rồi detach nếu quá hạn.
//
//  2. sdkQuit() block vài giây do pthread_join(write_tid) trong stop().
//     → Fix: khi timeout trong connect(), gọi sdkQuit() trong detached thread
//       riêng → connect() trả về false ngay lập tức.
//
//  3. Detached thread dùng SDK cũ trong khi reinitSdk() tạo SDK mới
//     → use-after-free.
//     → Fix: sdk_ptr_ dùng shared_ptr; detached thread giữ bản sao riêng
//       → SDK cũ sống cho đến khi mọi thread dùng nó đã exit.
//
// Thứ tự gọi khi reconnect từ bên ngoài (để tránh deadlock với zoom thread):
//   camera.signalStopZoom()       // 1. chỉ set flag, chưa join
//   client.shutdownSdk()          // 2. kill SDK → unblock zoom thread
//   camera.joinZoomThread()       // 3. join an toàn (SDK đã chết)
//   attitude.reset()              // 4. xóa state cũ
//   client.reconnectAfterShutdown() // 5. SDK mới + retry loop
// =============================================================================

#pragma once
#include "payloadSdkInterface.h"
#include "payloadsdk.h"
#include <atomic>
#include <memory>

// Helpers tạo T_ConnInfo
T_ConnInfo make_udp_conn(const char* ip, int port);
T_ConnInfo make_uart_conn(const char* device, int baud);

class PayloadClient {
public:
    explicit PayloadClient(const T_ConnInfo& conn);
    ~PayloadClient();

    PayloadClient(const PayloadClient&) = delete;
    PayloadClient& operator=(const PayloadClient&) = delete;

    // Thử kết nối một lần với timeout. Trả về false nếu không kết nối được
    // trong timeout_sec giây. sdkInitConnection() và checkPayloadConnection()
    // chạy trong background thread; sdkQuit() khi timeout cũng chạy detached.
    bool connect(int timeout_sec);

    // Retry vô hạn: gọi connect() lặp lại cho đến khi thành công.
    // In thông báo mỗi lần thử và mỗi lần thất bại.
    void connectWithRetry(int per_attempt_sec = 5, int retry_delay_sec = 3);

    // Đặt connected_=false và gọi sdkQuit() để tắt SDK hiện tại.
    // Dùng trước joinZoomThread() trong flow reconnect.
    void shutdownSdk();

    // Tạo SDK mới (reinitSdk) rồi gọi connectWithRetry().
    // Gọi sau khi zoom thread đã join xong.
    void reconnectAfterShutdown(int per_attempt_sec = 5, int retry_delay_sec = 3);

    // Shortcut: shutdownSdk() + reconnectAfterShutdown()
    void reconnect(int per_attempt_sec = 5, int retry_delay_sec = 3);

    // Tắt SDK (dùng khi Ctrl+C / destructor)
    void disconnect();

    // Trả về con trỏ SDK hiện tại (luôn hợp lệ sau khi connect thành công)
    PayloadSdkInterface* sdk() { return sdk_ptr_.get(); }
    bool isConnected() const   { return connected_.load(); }

private:
    T_ConnInfo conn_;
    // shared_ptr cho phép detached thread giữ SDK cũ sống sau khi reinitSdk()
    std::shared_ptr<PayloadSdkInterface> sdk_ptr_;
    std::atomic<bool> connected_{false};

    // Thay sdk_ptr_ bằng instance mới; SDK cũ tự xóa khi không còn reference
    void reinitSdk();
};

// Cài đặt handler cho SIGINT (Ctrl+C) → gọi client.disconnect() rồi exit
void install_quit_handler(PayloadClient& client);
