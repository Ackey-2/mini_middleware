#include "core/data_plane.h"
#include "transport/tcp_server_transport.h"
#include "transport/tcp_client_transport.h"
#include "common/logger.h"
#include "data.pb.h"

namespace mm {

namespace {
// 与 DiscoveryAgent::match_key 同一公式,保证 match/unmatch 对得上
std::string match_key(const MatchInfo& m) {
    return std::to_string(static_cast<int>(m.local.kind())) + ":" + m.local.topic() + "|" +
           std::to_string(m.remote_participant_id) + "|" +
           std::to_string(static_cast<int>(m.remote.kind())) + ":" + m.remote.topic();
}
}  // namespace

DataPlane::DataPlane(std::shared_ptr<LocalBus> bus, std::string advertise_ip)
    : bus_(std::move(bus)), advertise_ip_(std::move(advertise_ip)) {}

DataPlane::~DataPlane() { stop(); }

bool DataPlane::start() {
    server_ = std::make_unique<TcpServerTransport>(0);   // 临时端口
    server_->on_message([this](const std::string& payload) { on_inbound(payload); });
    if (!server_->start()) {
        LOG_ERROR("data plane: server failed to start");
        server_.reset();
        return false;
    }
    LOG_INFO("data plane listening on {}:{}", advertise_ip_, server_->local_port());
    return true;
}

void DataPlane::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stopped_ = true;
        sinks_.clear();
        for (auto& kv : connections_) kv.second->stop();
        connections_.clear();
        refcount_.clear();
    }
    if (server_) { server_->stop(); server_.reset(); }
}

uint16_t DataPlane::server_port() const {
    return server_ ? server_->local_port() : 0;
}

void DataPlane::on_inbound(const std::string& payload) {
    DataMessage msg;
    if (!msg.ParseFromString(payload)) {
        LOG_WARN("data plane: bad DataMessage dropped");
        return;
    }
    bus_->deliver_inbound(msg.topic(), msg.payload());   // 只投本地订阅者
}

std::shared_ptr<Transport> DataPlane::connection_for(uint64_t pid, const Locator& loc) {
    auto it = connections_.find(pid);
    if (it != connections_.end()) return it->second;
    auto conn = std::make_shared<TcpClientTransport>(
        loc.ip(), static_cast<uint16_t>(loc.port()));
    conn->start();   // 异步连接;连上前的 send 会返回 false
    connections_[pid] = conn;
    return conn;
}

void DataPlane::handle_match(const MatchInfo& m) {
    if (m.local.kind() != EndpointInfo::PUBLISHER) return;   // 只有发布方主动连
    std::lock_guard<std::mutex> lock(mtx_);
    if (stopped_) return;                                    // 已停止,拒绝新通道
    std::string key = match_key(m);
    if (sinks_.count(key)) return;                           // 幂等

    auto conn = connection_for(m.remote_participant_id, m.remote_locator);
    auto sink = std::make_shared<RemoteSink>(m.local.topic(), conn);
    bus_->add_remote_sink(m.local.topic(), sink);
    sinks_[key] = sink;
    ++refcount_[m.remote_participant_id];
    LOG_INFO("data plane: channel up topic={} -> {}:{}",
             m.local.topic(), m.remote_locator.ip(), m.remote_locator.port());
}

void DataPlane::handle_unmatch(const MatchInfo& m) {
    if (m.local.kind() != EndpointInfo::PUBLISHER) return;
    std::lock_guard<std::mutex> lock(mtx_);
    if (stopped_) return;
    std::string key = match_key(m);
    auto it = sinks_.find(key);
    if (it == sinks_.end()) return;

    bus_->remove_remote_sink(m.local.topic(), it->second.get());
    sinks_.erase(it);

    uint64_t pid = m.remote_participant_id;
    if (--refcount_[pid] <= 0) {       // 该对端再无活跃匹配 → 关连接
        refcount_.erase(pid);
        auto cit = connections_.find(pid);
        if (cit != connections_.end()) {
            cit->second->stop();
            connections_.erase(cit);
        }
    }
    LOG_INFO("data plane: channel down topic={} peer={}", m.local.topic(), pid);
}

}  // namespace mm
