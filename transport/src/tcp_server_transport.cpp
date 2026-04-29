#include "transport/tcp_server_transport.h"
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
// 工具:设置 fd 为非阻塞
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

constexpr int MAX_EVENTS = 64;
constexpr int BACKLOG = 128;
}  // namespace

// ═══════════════════════════════════════════════════════════════
// 构造 / 析构
// ═══════════════════════════════════════════════════════════════

TcpServerTransport::TcpServerTransport(uint16_t port) : port_(port) {}

TcpServerTransport::~TcpServerTransport() {
    stop();
}

// ═══════════════════════════════════════════════════════════════
// start:创建 listen_fd + epoll,启动后台线程
// ═══════════════════════════════════════════════════════════════

bool TcpServerTransport::start() {
    // 1. 创建监听 socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        LOG_ERROR("socket() failed: {}", strerror(errno));
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);

    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("bind() failed on port {}: {}", port_, strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, BACKLOG) < 0) {
        LOG_ERROR("listen() failed: {}", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    set_nonblocking(listen_fd_);//设置成非阻塞模式

    // 2. 创建 epoll 实例
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        LOG_ERROR("epoll_create1() failed: {}", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // 3. 把 listen_fd_ 加入 epoll
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        LOG_ERROR("epoll_ctl ADD listen_fd failed: {}", strerror(errno));
        close(listen_fd_);
        close(epoll_fd_);
        listen_fd_ = -1;
        epoll_fd_ = -1;
        return false;
    }

    // 4. 启动事件循环线程
    running_ = true;
    thread_ = std::thread(&TcpServerTransport::event_loop, this);

    LOG_INFO("TcpServerTransport started on port {}", port_);
    return true;
}

// ═══════════════════════════════════════════════════════════════
// stop:停止线程,清理资源
// ═══════════════════════════════════════════════════════════════

void TcpServerTransport::stop() {
    if (!running_.exchange(false)) {
        return;   // 已经停了
    }

    // 关 listen_fd 让 epoll_wait 不再有新事件,事件循环检测 running_=false 退出
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (thread_.joinable()) {
        thread_.join();
    }

    // 关闭所有客户端连接
    for (auto& [fd, _] : connection_buffers_) {
        ::close(fd);
    }
    connection_buffers_.clear();

    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }

    LOG_INFO("TcpServerTransport stopped");
}

// ═══════════════════════════════════════════════════════════════
// 接口实现
// ═══════════════════════════════════════════════════════════════

void TcpServerTransport::on_message(MessageCallback cb) {
    callback_ = std::move(cb);
}

bool TcpServerTransport::send(const std::string& /*payload*/) {
    // server 不实现 send
    return false;
}

// ═══════════════════════════════════════════════════════════════
// 事件循环 ⭐ 这是核心
// ═══════════════════════════════════════════════════════════════

void TcpServerTransport::event_loop() {
    epoll_event events[MAX_EVENTS];

    while (running_.load()) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 100);  // 100ms 超时,定期检查 running_//返回那个活跃的连接
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("epoll_wait failed: {}", strerror(errno));
            break;
        }
        for (int i = 0; i < n; ++i){
            int fd=events[i].data.fd;
            if(fd==listen_fd_){
                handle_accept();
            }
            else{
                 handle_read(fd);
            }
        }
        
    }
}

// ═══════════════════════════════════════════════════════════════
// 处理新连接
// ═══════════════════════════════════════════════════════════════

void TcpServerTransport::handle_accept() {
    // 循环 accept,把所有积压的连接都接掉(LT 模式下也是好习惯)
    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int conn_fd = accept(listen_fd_, (sockaddr*)&client_addr, &len);
        if (conn_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            LOG_WARN("accept() failed: {}", strerror(errno));
            break;
        }

        set_nonblocking(conn_fd);

        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = conn_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, conn_fd, &ev) < 0) {
            LOG_ERROR("epoll_ctl ADD conn_fd failed: {}", strerror(errno));
            ::close(conn_fd);
            continue;
        }

        // 为这个连接初始化 buffer
        connection_buffers_[conn_fd] = std::string();

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
        LOG_INFO("new connection fd={} from {}:{}",
                 conn_fd, ip, ntohs(client_addr.sin_port));
    }
}

// ═══════════════════════════════════════════════════════════════
// 处理已连接的客户端发来的数据 ⭐ 这是核心
// ═══════════════════════════════════════════════════════════════

void TcpServerTransport::handle_read(int fd) {
    auto it = connection_buffers_.find(fd);
    if (it == connection_buffers_.end()) {
        LOG_WARN("read event on unknown fd={}", fd);
        return;
    }

    std::string& buffer = it->second;
    char tmp[4096];
    bool peer_closed = false;          // ← 新增

    while (true) {
        ssize_t r = read(fd, tmp, sizeof(tmp));
        if (r > 0) {
            buffer.append(tmp, r);
            continue;
        }
        if (r == 0) {
            peer_closed = true;        // ← 标记一下,不立刻 return
            break;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        LOG_WARN("read error on fd={}: {}", fd, strerror(errno));
        close_connection(fd);
        return;
    }

    // 处理 buffer 里所有完整帧
    auto msgs = FrameCodec::decode(buffer);
    for (auto& msg : msgs) {
        if (callback_) callback_(msg);
    }

    // 数据处理完了,如果对端关闭就清理连接
    if (peer_closed) {
        close_connection(fd);
    }
}

// ═══════════════════════════════════════════════════════════════
// 关闭一个连接
// ═══════════════════════════════════════════════════════════════

void TcpServerTransport::close_connection(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    connection_buffers_.erase(fd);
    LOG_INFO("connection fd={} closed", fd);
}

}  // namespace mm