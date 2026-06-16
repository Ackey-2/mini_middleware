#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// UdpMulticast:UDP 多播 socket 的薄封装。
//   - open():建收/发两个 socket,接收端 join 多播组并 bind 端口
//   - send():把字节多播到组(本机回环也能收到)
//   - recv():阻塞收一个数据报,最多等 timeout
// 每条公告是一个独立数据报,UDP 自带边界,无需分帧。
// ═══════════════════════════════════════════════════════════════
class UdpMulticast {
public:
    UdpMulticast(std::string group, uint16_t port);
    ~UdpMulticast();

    UdpMulticast(const UdpMulticast&) = delete;
    UdpMulticast& operator=(const UdpMulticast&) = delete;

    bool open();
    void close();

    bool send(const std::string& bytes);
    // 收到返回 true 并填 out;超时或错误返回 false
    bool recv(std::string& out, std::chrono::milliseconds timeout);

private:
    std::string group_;
    uint16_t port_;
    int send_fd_ = -1;
    int recv_fd_ = -1;
};

}  // namespace mm
