#include "discovery/udp_multicast.h"
#include "common/logger.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace mm {

UdpMulticast::UdpMulticast(std::string group, uint16_t port)
    : group_(std::move(group)), port_(port) {}

UdpMulticast::~UdpMulticast() { close(); }

bool UdpMulticast::open() {
    // ── 接收 socket:bind 到 INADDR_ANY:port,再 join 多播组 ──
    recv_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_fd_ < 0) {
        LOG_ERROR("udp recv socket failed: {}", strerror(errno));
        return false;
    }
    int one = 1;
    setsockopt(recv_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(recv_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("udp bind {} failed: {}", port_, strerror(errno));
        close();
        return false;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(group_.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(recv_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        LOG_ERROR("udp join {} failed: {}", group_, strerror(errno));
        close();
        return false;
    }

    // ── 发送 socket:开回环,使本机其它 socket(含自己)也能收到 ──
    send_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (send_fd_ < 0) {
        LOG_ERROR("udp send socket failed: {}", strerror(errno));
        close();
        return false;
    }
    int loop = 1;
    setsockopt(send_fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    return true;
}

void UdpMulticast::close() {
    if (recv_fd_ >= 0) { ::close(recv_fd_); recv_fd_ = -1; }
    if (send_fd_ >= 0) { ::close(send_fd_); send_fd_ = -1; }
}

bool UdpMulticast::send(const std::string& bytes) {
    if (send_fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr(group_.c_str());
    ssize_t n = ::sendto(send_fd_, bytes.data(), bytes.size(), 0,
                         (sockaddr*)&addr, sizeof(addr));
    return n == static_cast<ssize_t>(bytes.size());
}

bool UdpMulticast::recv(std::string& out, std::chrono::milliseconds timeout) {
    if (recv_fd_ < 0) return false;
    timeval tv{};
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    setsockopt(recv_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[65536];
    ssize_t n = ::recv(recv_fd_, buf, sizeof(buf), 0);
    if (n < 0) return false;          // 超时(EAGAIN)或错误
    out.assign(buf, n);
    return true;
}

}  // namespace mm
