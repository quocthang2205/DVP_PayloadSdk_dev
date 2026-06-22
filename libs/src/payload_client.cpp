// =============================================================================
// payload_client.cpp — Hiện thực PayloadClient
//
// Luồng kết nối lần đầu:
//   PayloadClient(conn)           → tạo SDK object (không block)
//   connectWithRetry(5, 3)        → thử mỗi 5s, nghỉ 3s giữa các lần
//     └─ connect(5)               → background thread: init + check
//          ├─ sdkInitConnection() → start() → "CHECK FOR MESSAGES" loop
//          └─ checkPayloadConnection() → chờ camera/gimbal heartbeat
//        main polls connected_ mỗi 100ms; timeout → sdkQuit() detached
//
// Luồng reconnect (khi mất kết nối giữa chừng):
//   signalStopZoom()              → flag zoom thread dừng
//   shutdownSdk()                 → sdkQuit() → unblock zoom thread
//   joinZoomThread()              → join an toàn
//   reconnectAfterShutdown()      → reinitSdk() + connectWithRetry()
// =============================================================================

#include "../include/payload_client.h"
#include <cstdio>
#include <csignal>
#include <thread>
#include <chrono>
#include <unistd.h>

// ── Helpers tạo T_ConnInfo ────────────────────────────────────────────────────

T_ConnInfo make_udp_conn(const char* ip, int port) {
    T_ConnInfo c;
    c.type            = CONTROL_UDP;
    c.device.udp.ip   = (char*)ip;
    c.device.udp.port = port;
    return c;
}

T_ConnInfo make_uart_conn(const char* device, int baud) {
    T_ConnInfo c;
    c.type                = CONTROL_UART;
    c.device.uart.name     = (char*)device;
    c.device.uart.baudrate = baud;
    return c;
}

// ── PayloadClient ─────────────────────────────────────────────────────────────

PayloadClient::PayloadClient(const T_ConnInfo& conn) : conn_(conn) {
    // KHÔNG gọi sdkInitConnection() ở đây.
    // sdkInitConnection() → start() → while(!current_messages.sysid) block mãi
    // khi payload chưa bật. Việc init thật sự xảy ra trong connect() trên thread riêng.
    sdk_ptr_ = std::make_shared<PayloadSdkInterface>(conn_);
}

PayloadClient::~PayloadClient() {
    disconnect();
}

void PayloadClient::reinitSdk() {
    // Tạo SDK mới. SDK cũ được giữ bởi shared_ptr trong bất kỳ detached thread
    // nào đang chạy — nó sẽ tự xóa khi thread exit, không cần delete thủ công.
    sdk_ptr_ = std::make_shared<PayloadSdkInterface>(conn_);
}

// ── connect() ─────────────────────────────────────────────────────────────────
//
// Vấn đề cần giải quyết:
//   sdkInitConnection() → payload_interface->start() có loop "CHECK FOR MESSAGES"
//   chờ sysid != 0. Khi payload OFF, loop này block mãi mãi.
//   checkPayloadConnection() cũng loop đợi camera/gimbal heartbeat.
//
// Giải pháp:
//   Chạy cả hai trong background thread. Khi timeout gọi sdkQuit() trong
//   detached thread riêng (không block main) rồi detach thread init.
//   shared_ptr đảm bảo SDK cũ sống đủ lâu cho đến khi cả hai thread exit.

bool PayloadClient::connect(int timeout_sec) {
    connected_ = false;

    auto alive    = std::make_shared<std::atomic<bool>>(true);
    auto sdk_copy = sdk_ptr_;   // bản sao shared_ptr: thread giữ SDK sống độc lập

    std::thread t([sdk_copy, alive, this]() {
        // Bước 1: init kết nối UDP/UART, tạo read/write thread trong SDK
        if (!sdk_copy->sdkInitConnection()) return;
        if (!alive->load()) return;   // timeout đã xảy ra → bỏ qua

        // Bước 2: chờ heartbeat xác nhận camera/gimbal đang online
        sdk_copy->checkPayloadConnection();

        if (alive->load())
            connected_ = true;  // thành công!
    });

    auto start = std::chrono::steady_clock::now();
    while (!connected_.load()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeout_sec) {
            *alive = false;
            // sdkQuit() → pthread_join(write_tid) có thể mất vài giây.
            // Chạy trong detached thread để connect() trả về ngay lập tức.
            std::thread([sdk_copy]() {
                try { sdk_copy->sdkQuit(); } catch (...) {}
            }).detach();
            t.detach();   // không join → tránh block nếu sdkQuit chưa unblock kịp
            return false;
        }
        usleep(100000);  // poll mỗi 100ms
    }
    t.join();
    return true;
}

// ── connectWithRetry() ────────────────────────────────────────────────────────

void PayloadClient::connectWithRetry(int per_attempt_sec, int retry_delay_sec) {
    int attempt = 0;
    while (true) {
        ++attempt;
        printf("[%d] Connecting to payload (timeout %d s)...\n",
               attempt, per_attempt_sec);
        fflush(stdout);

        if (connect(per_attempt_sec))
            return;  // kết nối thành công

        printf("    Connection failed. Retry in %d s...\n", retry_delay_sec);
        fflush(stdout);
        usleep((uint64_t)retry_delay_sec * 1000000);
        reinitSdk();   // SDK mới cho lần thử tiếp theo (SDK cũ đang bị sdkQuit detached)
    }
}

// ── reconnect helpers ─────────────────────────────────────────────────────────

void PayloadClient::shutdownSdk() {
    connected_ = false;
    printf("[PayloadClient] Shutting down SDK...\n");
    // sdkQuit() set time_to_exit → unblock mọi loop đang chạy trong SDK
    try { sdk_ptr_->sdkQuit(); } catch (...) {}
}

void PayloadClient::reconnectAfterShutdown(int per_attempt_sec, int retry_delay_sec) {
    reinitSdk();                                // SDK mới sạch hoàn toàn
    connectWithRetry(per_attempt_sec, retry_delay_sec);
}

void PayloadClient::reconnect(int per_attempt_sec, int retry_delay_sec) {
    shutdownSdk();
    reconnectAfterShutdown(per_attempt_sec, retry_delay_sec);
}

void PayloadClient::disconnect() {
    try { sdk_ptr_->sdkQuit(); } catch (...) {}
}

// ── Signal handler (Ctrl+C) ───────────────────────────────────────────────────

static PayloadClient* s_quit_client = nullptr;

static void quit_signal_handler(int) {
    printf("\nTERMINATING AT USER REQUEST\n");
    if (s_quit_client) s_quit_client->disconnect();
    exit(0);
}

void install_quit_handler(PayloadClient& client) {
    s_quit_client = &client;
    signal(SIGINT, quit_signal_handler);
}
