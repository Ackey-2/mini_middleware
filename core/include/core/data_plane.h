#pragma once

#include "core/local_bus.h"
#include "core/remote_sink.h"
#include "core/shm_reader.h"
#include "transport/transport.h"
#include "transport/shm_segment.h"
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
// DataPlane:数据面,按"同机/跨机"自动选路。
//   - 跨机(remote_host_id 不同):走 Phase 3 的 TCP —— 发布方主动连对端数据
//     服务器并注册 RemoteSink,按 topic 多路复用;接收方只读。
//   - 同机(remote_host_id 相同 && 启用 SHM):走共享内存零拷贝 —— 发布方建一个
//     per-(本进程, topic) 的 SHM ring 写者(ShmSink);订阅方开同名 ring 读者
//     (ShmReaderManager 轮询)→ deliver_inbound。同机时双方都要动作。
// 匹配回调在 discovery 后台线程触发,故内部状态用 mtx_ 保护。
// ═══════════════════════════════════════════════════════════════
class DataPlane {
public:
    DataPlane(std::shared_ptr<LocalBus> bus, std::string advertise_ip);
    ~DataPlane();

    DataPlane(const DataPlane&) = delete;
    DataPlane& operator=(const DataPlane&) = delete;

    bool start();                       // 启动数据服务器(+ SHM 读取器)
    void stop();

    uint16_t server_port() const;       // 供 Node 填 Locator
    const std::string& advertise_ip() const { return advertise_ip_; }

    // 同机判定所需的本地身份。必须在收到第一个匹配回调前设置(Node 在 discovery
    // 建好、start 前调用)。host_id 为空或 enable_shm=false 时一律走 TCP。
    void set_local_identity(uint64_t pid, std::string host_id, bool enable_shm = true);

    void handle_match(const MatchInfo& m);
    void handle_unmatch(const MatchInfo& m);

private:
    void on_inbound(const std::string& payload);          // 数据服务器收到一帧
    // 复用/新建到某远端参与者的出站连接(调用方需持 mtx_)
    std::shared_ptr<Transport> connection_for(uint64_t pid, const Locator& loc);

    // 选路分支(均需持 mtx_)
    void tcp_pub_match(const MatchInfo& m);
    void tcp_pub_unmatch(const MatchInfo& m);
    void shm_pub_match(const MatchInfo& m);
    void shm_pub_unmatch(const MatchInfo& m);
    void shm_sub_match(const MatchInfo& m);
    void shm_sub_unmatch(const MatchInfo& m);

    bool same_host(const MatchInfo& m) const;             // 启用 SHM 且 host_id 相同

    std::shared_ptr<LocalBus> bus_;
    std::string advertise_ip_;
    std::unique_ptr<TcpServerTransport> server_;

    // 本地身份(set_local_identity 设定;在第一个匹配前完成,后续只读)
    uint64_t local_pid_ = 0;
    std::string local_host_id_;
    bool shm_enabled_ = true;

    std::mutex mtx_;
    // ── 跨机 TCP ──
    std::map<uint64_t, std::shared_ptr<Transport>> connections_;   // 每对端一条
    std::map<std::string, std::shared_ptr<RemoteSink>> sinks_;     // match_key → sink
    std::map<uint64_t, int> refcount_;                             // 对端活跃 TCP-PUB 匹配数
    // ── 同机 SHM ──
    struct ShmPub {
        std::shared_ptr<ISink> sink;   // ShmSink(持有写者段);ref 归零时撤销并 unlink
        int ref = 0;
    };
    std::map<std::string, ShmPub> shm_pub_;        // topic → 写者(同机多订阅者共享一段)
    std::map<std::string, int> shm_sub_ref_;       // seg_name → 活跃读者匹配数
    std::unique_ptr<ShmReaderManager> shm_readers_;

    bool stopped_ = false;            // stop() 后拒绝新匹配,防竞态
};

}  // namespace mm
