#pragma once

#include "transport/transport.h"
#include <atomic>
#include <thread>
#include <string>
#include <mutex>
#include <queue>
#include <cstdint>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// TcpClientTransport:基于 TCP 的客户端传输
//
// 行为:
//   - connect 到指定 host:port
//   - 用 epoll 处理收发
//   - 收到完整帧 → callback
//   - send() 把 payload 包成帧发出去
//
// 简化(不做):
//   - 自动重连(后面再加)
//   - 多目标连接(一个 client 只连一个 server)
// ═══════════════════════════════════════════════════════════════

enum class ConnectionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
};

class TcpClientTransport : public Transport {
public:
    TcpClientTransport(const std::string& host, uint16_t port);
    ~TcpClientTransport() override;

    bool start() override;
    void stop() override;
    void on_message(MessageCallback cb) override;
    bool send(const std::string& payload) override;     // ⭐ 真正实现

private:
    void event_loop();
    void handle_connect_complete();    // ⭐ 你写
    void handle_read();
    void handle_write();

private:
    std::string host_;
    uint16_t port_;

    int sock_fd_ = -1;
    int epoll_fd_ = -1;

    std::atomic<bool> running_{false};
    std::atomic<bool> stopped_{false};    // stop() 幂等守卫(running_ 可被事件循环自行清零)
    std::atomic<ConnectionState> state_{ConnectionState::DISCONNECTED};
    std::thread thread_;

    MessageCallback callback_;

    std::string read_buffer_;             // 收数据用

    std::mutex write_mutex_;              // 保护 write_buffer_(send 跨线程调用)
    std::string write_buffer_;            // 待发送数据
};

}  // namespace mm