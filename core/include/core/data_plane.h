#pragma once

#include "core/local_bus.h"
#include "core/remote_sink.h"
#include "transport/transport.h"
#include "discovery/endpoint_matcher.h"   // MatchInfo
#include "discovery.pb.h"                  // Locator, EndpointInfo

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace mm {

class TcpServerTransport;   // 前向声明:仅 .cpp 需要完整类型

// ═══════════════════════════════════════════════════════════════
// DataPlane:TCP 数据面。
//   - 监听一个数据服务器(临时端口),把入站 DataMessage 投给本地订阅者
//   - 对"本地是 PUBLISHER"的匹配,主动连对端并注册 RemoteSink
//   - 每个远端参与者复用一条出站连接,按 topic 多路复用
// 匹配回调在 discovery 后台线程触发,故内部状态用 mtx_ 保护。
// ═══════════════════════════════════════════════════════════════
class DataPlane {
public:
    DataPlane(std::shared_ptr<LocalBus> bus, std::string advertise_ip);
    ~DataPlane();

    DataPlane(const DataPlane&) = delete;
    DataPlane& operator=(const DataPlane&) = delete;

    bool start();                       // 启动数据服务器
    void stop();

    uint16_t server_port() const;       // 供 Node 填 Locator
    const std::string& advertise_ip() const { return advertise_ip_; }

    void handle_match(const MatchInfo& m);
    void handle_unmatch(const MatchInfo& m);

private:
    void on_inbound(const std::string& payload);          // 数据服务器收到一帧
    // 复用/新建到某远端参与者的出站连接(调用方需持 mtx_)
    std::shared_ptr<Transport> connection_for(uint64_t pid, const Locator& loc);

    std::shared_ptr<LocalBus> bus_;
    std::string advertise_ip_;
    std::unique_ptr<TcpServerTransport> server_;

    std::mutex mtx_;
    std::map<uint64_t, std::shared_ptr<Transport>> connections_;   // 每对端一条
    std::map<std::string, std::shared_ptr<RemoteSink>> sinks_;     // match_key → sink
    std::map<uint64_t, int> refcount_;                             // 对端活跃 PUB 匹配数
    bool stopped_ = false;                                         // stop() 后拒绝新匹配,防竞态
};

}  // namespace mm
