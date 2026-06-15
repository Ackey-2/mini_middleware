#pragma once

#include "discovery/udp_multicast.h"
#include "discovery/endpoint_matcher.h"
#include "discovery.pb.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// DiscoveryAgent:一个进程的发现代理。
// 单后台线程:周期性多播本节点公告 + 收远端公告 + 算匹配 + 存活超时。
// 匹配按 (本地端点, 远端 id, 远端端点) 去重,首次出现触发 on_match,
// 对端超时下线触发 on_unmatch。
// ═══════════════════════════════════════════════════════════════
class DiscoveryAgent {
public:
    using MatchCallback = std::function<void(const MatchInfo&)>;

    DiscoveryAgent(std::string node_name, Locator data_locator,
                   std::string group = "239.255.0.1", uint16_t port = 7400);
    ~DiscoveryAgent();

    DiscoveryAgent(const DiscoveryAgent&) = delete;
    DiscoveryAgent& operator=(const DiscoveryAgent&) = delete;

    // 注册本地端点(可在 start 前或后调用,下次公告生效)
    void add_endpoint(EndpointInfo::Kind kind, const std::string& topic,
                      const std::string& type_name);

    void on_match(MatchCallback cb);
    void on_unmatch(MatchCallback cb);

    // 测试/调参:公告间隔与存活超时。须在 start() 前设置。
    void set_timing(std::chrono::milliseconds announce_interval,
                    std::chrono::milliseconds liveliness_timeout);

    bool start();
    void stop();

    uint64_t participant_id() const { return participant_id_; }

private:
    void run();                                            // 后台线程
    void announce();
    void handle_announcement(const ParticipantAnnouncement& ann);
    void reap_dead();
    static std::string match_key(const MatchInfo& m);

    std::string node_name_;
    Locator data_locator_;
    uint64_t participant_id_;
    UdpMulticast mc_;

    std::mutex mtx_;                                       // 保护 local_endpoints_
    std::vector<EndpointInfo> local_endpoints_;

    // 仅后台线程访问:
    struct Remote {
        std::vector<EndpointInfo> endpoints;
        Locator locator;
        std::chrono::steady_clock::time_point last_seen;
    };
    std::map<uint64_t, Remote> remotes_;
    std::map<std::string, MatchInfo> active_matches_;      // key → match

    MatchCallback on_match_;
    MatchCallback on_unmatch_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::chrono::steady_clock::time_point last_announce_{};

    std::chrono::milliseconds announce_interval_{1000};
    std::chrono::milliseconds liveliness_timeout_{5000};
    std::chrono::milliseconds recv_timeout_{200};
};

}  // namespace mm
