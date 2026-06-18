#include "discovery/discovery_agent.h"
#include "common/host_id.h"
#include "common/logger.h"

#include <unistd.h>
#include <random>

namespace mm {

namespace {
uint64_t make_participant_id() {
    std::random_device rd;
    uint64_t hi = rd();
    uint64_t lo = rd();
    return (hi << 32) ^ lo ^ static_cast<uint64_t>(::getpid());
}
}  // namespace

DiscoveryAgent::DiscoveryAgent(std::string node_name, Locator data_locator,
                               std::string group, uint16_t port)
    : node_name_(std::move(node_name)),
      data_locator_(std::move(data_locator)),
      participant_id_(make_participant_id()),
      mc_(std::move(group), port) {}

DiscoveryAgent::~DiscoveryAgent() { stop(); }

void DiscoveryAgent::add_endpoint(EndpointInfo::Kind kind, const std::string& topic,
                                  const std::string& type_name) {
    std::lock_guard<std::mutex> lock(mtx_);
    EndpointInfo e;
    e.set_kind(kind);
    e.set_topic(topic);
    e.set_type_name(type_name);
    local_endpoints_.push_back(std::move(e));
    endpoints_dirty_.store(true);   // 让后台线程下一轮对已知远端重算匹配
}

void DiscoveryAgent::on_match(MatchCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mtx_);
    on_match_ = std::move(cb);
}
void DiscoveryAgent::on_unmatch(MatchCallback cb) {
    std::lock_guard<std::mutex> lock(cb_mtx_);
    on_unmatch_ = std::move(cb);
}

void DiscoveryAgent::set_timing(std::chrono::milliseconds announce_interval,
                                std::chrono::milliseconds liveliness_timeout) {
    announce_interval_.store(announce_interval);
    liveliness_timeout_.store(liveliness_timeout);
}

bool DiscoveryAgent::start() {
    if (running_.load()) return true;     // 已启动,幂等
    if (!mc_.open()) {
        LOG_ERROR("discovery: multicast open failed for node {}", node_name_);
        return false;
    }
    running_ = true;
    thread_ = std::thread(&DiscoveryAgent::run, this);
    LOG_INFO("discovery started: node={} id={}", node_name_, participant_id_);
    return true;
}

void DiscoveryAgent::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    mc_.close();
}

void DiscoveryAgent::run() {
    announce();                                      // 上线先公告一次
    last_announce_ = std::chrono::steady_clock::now();

    while (running_.load()) {
        std::string bytes;
        if (mc_.recv(bytes, recv_timeout_)) {
            ParticipantAnnouncement ann;
            if (ann.ParseFromString(bytes)) {
                handle_announcement(ann);
            } else {
                LOG_WARN("discovery: bad announcement dropped");
            }
        }
        if (endpoints_dirty_.exchange(false)) {
            rematch_all();   // 本地新增端点,对所有已知远端重算
        }
        auto now = std::chrono::steady_clock::now();
        if (now - last_announce_ >= announce_interval_.load()) {
            announce();
            last_announce_ = now;
        }
        reap_dead();
    }
}

void DiscoveryAgent::announce() {
    ParticipantAnnouncement ann;
    ann.set_participant_id(participant_id_);
    ann.set_node_name(node_name_);
    ann.set_host_id(local_host_id());
    *ann.mutable_data_locator() = data_locator_;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& e : local_endpoints_) *ann.add_endpoints() = e;
    }
    std::string bytes;
    ann.SerializeToString(&bytes);
    mc_.send(bytes);
}

void DiscoveryAgent::handle_announcement(const ParticipantAnnouncement& ann) {
    if (ann.participant_id() == participant_id_) return;   // 自己,忽略

    Remote& r = remotes_[ann.participant_id()];
    r.endpoints.assign(ann.endpoints().begin(), ann.endpoints().end());
    r.locator = ann.data_locator();
    r.host_id = ann.host_id();
    r.last_seen = std::chrono::steady_clock::now();

    try_match(ann.participant_id(), r);
}

void DiscoveryAgent::try_match(uint64_t remote_id, const Remote& r) {
    std::vector<EndpointInfo> local;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        local = local_endpoints_;
    }

    auto matches = match_endpoints(local, remote_id, r.locator, r.endpoints);
    for (auto& m : matches) {
        m.remote_host_id = r.host_id;   // 供数据面判定同机/跨机
        std::string key = match_key(m);
        if (active_matches_.find(key) == active_matches_.end()) {
            active_matches_[key] = m;
            MatchCallback cb;
            {
                std::lock_guard<std::mutex> lock(cb_mtx_);
                cb = on_match_;
            }
            if (cb) cb(m);
        }
    }
}

void DiscoveryAgent::rematch_all() {
    for (const auto& kv : remotes_) {
        try_match(kv.first, kv.second);
    }
}

void DiscoveryAgent::reap_dead() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = remotes_.begin(); it != remotes_.end();) {
        if (now - it->second.last_seen > liveliness_timeout_.load()) {
            uint64_t dead_id = it->first;
            for (auto mit = active_matches_.begin(); mit != active_matches_.end();) {
                if (mit->second.remote_participant_id == dead_id) {
                    MatchCallback cb;
                    {
                        std::lock_guard<std::mutex> lock(cb_mtx_);
                        cb = on_unmatch_;
                    }
                    if (cb) cb(mit->second);
                    mit = active_matches_.erase(mit);
                } else {
                    ++mit;
                }
            }
            LOG_INFO("discovery: participant {} timed out", dead_id);
            it = remotes_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string DiscoveryAgent::match_key(const MatchInfo& m) {
    return std::to_string(static_cast<int>(m.local.kind())) + ":" + m.local.topic() + "|" +
           std::to_string(m.remote_participant_id) + "|" +
           std::to_string(static_cast<int>(m.remote.kind())) + ":" + m.remote.topic();
}

}  // namespace mm
