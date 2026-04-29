#include "transport/tcp_client_transport.h"
#include "transport/frame_codec.h"
#include "common/logger.h"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace mm {

namespace {
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
constexpr int MAX_EVENTS = 16;
}  // namespace

// ═══════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════

TcpClientTransport::TcpClientTransport(const std::string& host, uint16_t port)
    : host_(host), port_(port) {}

TcpClientTransport::~TcpClientTransport() { stop(); }

// ═══════════════════════════════════════════════════════════════
// start:发起非阻塞 connect,起线程跑事件循环
// ═══════════════════════════════════════════════════════════════

bool TcpClientTransport::start() {
    sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd_ < 0) {
        LOG_ERROR("socket() failed: {}", strerror(errno));
        return false;
    }

    set_nonblocking(sock_fd_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        LOG_ERROR("invalid host: {}", host_);
        ::close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }//填充服务端地址结构体
    //阻塞 connect:卡在这一行,直到三次握手完成或失败,才返回。
    // 非阻塞 connect:可能立刻成功(本机),也可能 EINPROGRESS
    int ret = ::connect(sock_fd_, (sockaddr*)&addr, sizeof(addr));//
    if (ret == 0) {
        // 极少见:连接立刻完成
        state_ = ConnectionState::CONNECTED;
        LOG_INFO("connected immediately to {}:{}", host_, port_);
    } else if (errno == EINPROGRESS) {
        state_ = ConnectionState::CONNECTING;
        LOG_INFO("connecting to {}:{}...", host_, port_);
    } else {
        LOG_ERROR("connect() failed: {}", strerror(errno));
        ::close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }

    // 创建 epoll
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        LOG_ERROR("epoll_create1 failed: {}", strerror(errno));
        ::close(sock_fd_);
        sock_fd_ = -1;
        return false;
    }

    // 注册 sock_fd_:
    //   - CONNECTING:监听 EPOLLOUT(连接完成)
    //   - CONNECTED:监听 EPOLLIN(可读)
    epoll_event ev{};
    if (state_ == ConnectionState::CONNECTING) {
        ev.events = EPOLLOUT;
    } else {
        ev.events = EPOLLIN;
    }
    ev.data.fd = sock_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, sock_fd_, &ev);

    running_ = true;
    thread_ = std::thread(&TcpClientTransport::event_loop, this);
    return true;
}

// ═══════════════════════════════════════════════════════════════
// stop
// ═══════════════════════════════════════════════════════════════

void TcpClientTransport::stop() {
    if (!running_.exchange(false)) return;

    if (sock_fd_ >= 0) {
        ::shutdown(sock_fd_, SHUT_RDWR);
    }

    if (thread_.joinable()) thread_.join();

    if (sock_fd_ >= 0) { ::close(sock_fd_); sock_fd_ = -1; }
    if (epoll_fd_ >= 0) { ::close(epoll_fd_); epoll_fd_ = -1; }

    state_ = ConnectionState::DISCONNECTED;
    LOG_INFO("TcpClientTransport stopped");
}

void TcpClientTransport::on_message(MessageCallback cb) {
    callback_ = std::move(cb);
}

// ═══════════════════════════════════════════════════════════════
// event_loop:和 server 类似,但只一个 fd
// ═══════════════════════════════════════════════════════════════

void TcpClientTransport::event_loop() {
    epoll_event events[MAX_EVENTS];

    while (running_.load()) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll_wait failed: {}", strerror(errno));
            break;
        }

        for (int i = 0; i < n; ++i) {
            uint32_t evs = events[i].events;

            if (evs & (EPOLLERR | EPOLLHUP)) {
                LOG_WARN("connection error/hangup");
                running_ = false;
                break;
            }

            if (state_ == ConnectionState::CONNECTING && (evs & EPOLLOUT)) {
                handle_connect_complete();
            } else if (state_ == ConnectionState::CONNECTED) {
                if (evs & EPOLLIN)  handle_read();
                if (evs & EPOLLOUT) handle_write();
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════
// ⭐ handle_connect_complete:连接完成后的处理(你来写)
// ═══════════════════════════════════════════════════════════════

void TcpClientTransport::handle_connect_complete() {
    // TODO ⭐ 你来写
    int err=0;
    socklen_t len=sizeof(err);
    getsockopt(sock_fd_, SOL_SOCKET, SO_ERROR, &err, &len);
    if(err!=0){
        running_=false;
        return;
    }
    state_ = ConnectionState::CONNECTED;
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = sock_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_fd_, &ev);



}

// ═══════════════════════════════════════════════════════════════
// handle_read:和 server 几乎一样
// ═══════════════════════════════════════════════════════════════

void TcpClientTransport::handle_read() {
    char tmp[4096];
    bool peer_closed = false;

    while (true) {
        ssize_t r = read(sock_fd_, tmp, sizeof(tmp));
        if (r > 0) {
            read_buffer_.append(tmp, r);
            continue;
        }
        if (r == 0) { peer_closed = true; break; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        LOG_WARN("read error: {}", strerror(errno));
        running_ = false;
        return;
    }

    auto msgs = FrameCodec::decode(read_buffer_);
    for (auto& msg : msgs) {
        if (callback_) callback_(msg);
    }

    if (peer_closed) {
        LOG_INFO("peer closed");
        running_ = false;
    }
}

// ═══════════════════════════════════════════════════════════════
// handle_write:把 write_buffer_ 里的数据写出去
// ═══════════════════════════════════════════════════════════════

void TcpClientTransport::handle_write() {
    std::lock_guard<std::mutex> lock(write_mutex_);

    while (!write_buffer_.empty()) {
        ssize_t w = write(sock_fd_, write_buffer_.data(), write_buffer_.size());
        if (w > 0) {
            write_buffer_.erase(0, w);
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;     // 内核缓冲区满,等下次 EPOLLOUT
        }
        LOG_WARN("write error: {}", strerror(errno));
        running_ = false;
        return;
    }

    // 写完了,改回只监听 EPOLLIN
    if (write_buffer_.empty()) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = sock_fd_;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_fd_, &ev);
    }
}

// ═══════════════════════════════════════════════════════════════
// ⭐ send:把 payload 编码成帧,排进 write_buffer_,触发 EPOLLOUT(你来写)
// ═══════════════════════════════════════════════════════════════

bool TcpClientTransport::send(const std::string& payload) {
    if(state_.load()!=ConnectionState::CONNECTED) return false;
    std:: string frame =FrameCodec::encode(payload);
    {
        std::lock_guard<std::mutex> lock(write_mutex_);
        write_buffer_.append(frame);
    }
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = sock_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, sock_fd_, &ev);
    return true;
    // TODO ⭐ 你来写
    //
    // 任务:
    //   1. 检查 state_:必须是 CONNECTED 才能 send,否则返回 false
    //   2. 用 FrameCodec::encode(payload) 把数据包成帧
    //   3. 加锁后 write_buffer_.append(frame)
    //   4. 修改 epoll 监听:增加 EPOLLOUT(EPOLL_CTL_MOD)
    //      这样后台线程的 epoll_wait 会被触发,handle_write 会跑起来

}

}  // namespace mm