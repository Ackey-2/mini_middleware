#pragma once

#include "transport/transport.h"
#include <atomic>
#include <thread>
#include <unordered_map>
#include <string>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// TcpServerTransport:基于 TCP 的服务端传输
//
// 行为:
//   - 监听一个 TCP 端口,接受多个客户端连接
//   - 用 epoll 事件循环管理所有连接
//   - 收到完整帧时调用 MessageCallback
//
// 注意:
//   - send() 不实现(server 通常是被动接收方)
//   - 多个客户端时,所有消息都通过同一个 callback 暴露给上层
// ═══════════════════════════════════════════════════════════════

class TcpServerTransport : public Transport {
public:
    explicit TcpServerTransport(uint16_t port);
    ~TcpServerTransport() override;

    bool start() override;
    void stop() override;
    void on_message(MessageCallback cb) override;
    bool send(const std::string& payload) override;   // 不实现,返回 false

private:
    // 后台线程主体:epoll 事件循环
    void event_loop();

    // 处理 listen_fd 的 EPOLLIN(新连接)
    void handle_accept();

    // 处理 conn_fd 的 EPOLLIN(数据到达)
    void handle_read(int fd);

    // 关闭一个连接,从 epoll 移除,清理 buffer
    void close_connection(int fd);

private:
        uint16_t port_;
        int listen_fd_ = -1;
        int epoll_fd_ = -1;

        std::atomic<bool> running_{false};
        std::thread thread_;

        MessageCallback callback_;

        // 每个连接的接收 buffer(字节流累加,等待帧解析)
        std::unordered_map<int, std::string> connection_buffers_;
};

}  // namespace mm