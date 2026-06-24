#include "core/data_plane.h"
#include "core/shm_sink.h"
#include "transport/tcp_server_transport.h"
#include "transport/tcp_client_transport.h"
#include "common/logger.h"
#include "data.pb.h"

namespace mm {

namespace {
// SHM ring 容量:槽数(2 的幂) × 每槽 payload 上限。
// 32 槽 × 256KB ≈ 8MB/段,足够多数消息;超 256KB 的消息丢弃(BEST_EFFORT)。
constexpr uint32_t kShmSlotCount = 32;
constexpr uint32_t kShmSlotSize = 256 * 1024;

// 与 DiscoveryAgent::match_key 同一公式,保证 match/unmatch 对得上
std::string match_key(const MatchInfo& m) {
    return std::to_string(static_cast<int>(m.local.kind())) + ":" + m.local.topic() + "|" +
           std::to_string(m.remote_participant_id) + "|" +
           std::to_string(static_cast<int>(m.remote.kind())) + ":" + m.remote.topic();
}

// POSIX 共享内存段名:以 '/' 开头且不再含 '/'。把 topic 里的非法字符替换为 '_'。
// 段名由 (写者 participant_id, topic) 唯一确定:发布方用自己的 id,订阅方用对端 id,
// 两端算出同一个名字,无需额外握手。
std::string seg_name(uint64_t writer_pid, const std::string& topic) {
    std::string s = "/mm." + std::to_string(writer_pid) + ".";
    for (char c : topic) {
        s += (c == '/' || c == '.' || c == ' ') ? '_' : c;
    }
    return s;
}
}  // namespace

DataPlane::DataPlane(std::shared_ptr<LocalBus> bus, std::string advertise_ip)
    : bus_(std::move(bus)), advertise_ip_(std::move(advertise_ip)) {}

DataPlane::~DataPlane() { stop(); }

void DataPlane::set_local_identity(uint64_t pid, std::string host_id, bool enable_shm) {
    local_pid_ = pid;
    local_host_id_ = std::move(host_id);
    shm_enabled_ = enable_shm;
}

bool DataPlane::start() {
    server_ = std::make_unique<TcpServerTransport>(0);   // 临时端口
    server_->on_message([this](const std::string& payload) { on_inbound(payload); });
    if (!server_->start()) {
        LOG_ERROR("data plane: server failed to start");
        server_.reset();
        return false;
    }
    shm_readers_ = std::make_unique<ShmReaderManager>(bus_);   // 同机订阅侧轮询器
    LOG_INFO("data plane listening on {}:{}", advertise_ip_, server_->local_port());
    return true;
}

void DataPlane::stop() {
    std::unique_ptr<ShmReaderManager> readers;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        stopped_ = true;
        sinks_.clear();
        for (auto& kv : connections_) kv.second->stop();
        connections_.clear();
        refcount_.clear();
        shm_pub_.clear();            // 释放 ShmSink → 段被 unlink
        shm_sub_ref_.clear();
        readers = std::move(shm_readers_);   // 锁外停 poller 线程,避免与 poll 持锁互等
    }
    readers.reset();                 // 停 poller(join)
    if (server_) { server_->stop(); server_.reset(); }
}

uint16_t DataPlane::server_port() const {
    return server_ ? server_->local_port() : 0;
}

bool DataPlane::same_host(const MatchInfo& m) const {
    return shm_enabled_ && !local_host_id_.empty() && m.remote_host_id == local_host_id_;
}

// SHM 环是有损的(BEST_EFFORT),故仅当订阅者请求 BEST_EFFORT 才走 SHM;
// 订阅者请求 RELIABLE → 退回 TCP(有序不丢)。订阅者端点 = 本地 SUB 或远端 SUB。
bool DataPlane::use_shm(const MatchInfo& m) const {
    if (!same_host(m)) return false;
    const EndpointInfo& reader =
        (m.local.kind() == EndpointInfo::SUBSCRIBER) ? m.local : m.remote;
    return reader.reliability() == 0;   // 0 = BEST_EFFORT
}

void DataPlane::on_inbound(const std::string& payload) {
    DataMessage msg;
    if (!msg.ParseFromString(payload)) {
        LOG_WARN("data plane: bad DataMessage dropped");
        return;
    }
    bus_->deliver_inbound(msg.topic(), msg.payload());   // 只投本地订阅者
}

// ─────────────────────────── 选路入口 ───────────────────────────

void DataPlane::handle_match(const MatchInfo& m) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (stopped_) return;
    if (!((m.local.kind() == EndpointInfo::PUBLISHER && m.remote.kind() == EndpointInfo::SUBSCRIBER) ||
          (m.local.kind() == EndpointInfo::SUBSCRIBER && m.remote.kind() == EndpointInfo::PUBLISHER))) {
        return;
    }
    if (m.local.kind() == EndpointInfo::PUBLISHER) {
        if (use_shm(m)) shm_pub_match(m);     // 同机 + BEST_EFFORT → SHM 写者
        else            tcp_pub_match(m);     // 跨机 或 RELIABLE → TCP 主动连
    } else {                                   // 本地是 SUBSCRIBER
        if (use_shm(m)) shm_sub_match(m);     // 同机 + BEST_EFFORT → SHM 读者
        // 否则(跨机 或 RELIABLE):不动作,等 TCP 入站(Phase 3 规则)
    }
}

void DataPlane::handle_unmatch(const MatchInfo& m) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (stopped_) return;
    if (!((m.local.kind() == EndpointInfo::PUBLISHER && m.remote.kind() == EndpointInfo::SUBSCRIBER) ||
          (m.local.kind() == EndpointInfo::SUBSCRIBER && m.remote.kind() == EndpointInfo::PUBLISHER))) {
        return;
    }
    if (m.local.kind() == EndpointInfo::PUBLISHER) {
        if (use_shm(m)) shm_pub_unmatch(m);
        else            tcp_pub_unmatch(m);
    } else {
        if (use_shm(m)) shm_sub_unmatch(m);
    }
}

// ─────────────────────────── 跨机 TCP ───────────────────────────

std::shared_ptr<Transport> DataPlane::connection_for(uint64_t pid, const Locator& loc) {
    auto it = connections_.find(pid);
    if (it != connections_.end()) return it->second;
    auto conn = std::make_shared<TcpClientTransport>(
        loc.ip(), static_cast<uint16_t>(loc.port()));
    conn->start();   // 异步连接;连上前的 send 会返回 false
    connections_[pid] = conn;
    return conn;
}

void DataPlane::tcp_pub_match(const MatchInfo& m) {
    std::string key = match_key(m);
    if (sinks_.count(key)) return;                           // 幂等
    auto conn = connection_for(m.remote_participant_id, m.remote_locator);
    auto sink = std::make_shared<RemoteSink>(m.local.topic(), conn);
    bus_->add_remote_sink(m.local.topic(), sink);
    sinks_[key] = sink;
    ++refcount_[m.remote_participant_id];
    LOG_INFO("data plane: TCP channel up topic={} -> {}:{}",
             m.local.topic(), m.remote_locator.ip(), m.remote_locator.port());
}

void DataPlane::tcp_pub_unmatch(const MatchInfo& m) {
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
    LOG_INFO("data plane: TCP channel down topic={} peer={}", m.local.topic(), pid);
}

// ─────────────────────────── 同机 SHM ───────────────────────────

void DataPlane::shm_pub_match(const MatchInfo& m) {
    const std::string& topic = m.local.topic();
    auto it = shm_pub_.find(topic);
    if (it != shm_pub_.end()) {        // 该 topic 的写者段已存在,多订阅者复用
        ++it->second.ref;
        return;
    }
    std::string name = seg_name(local_pid_, topic);
    std::shared_ptr<ShmSegment> seg = ShmSegment::create(name, kShmSlotCount, kShmSlotSize);
    if (!seg) {
        LOG_ERROR("data plane: shm create failed for topic={} (no fallback)", topic);
        return;
    }
    auto sink = std::make_shared<ShmSink>(std::move(seg));
    bus_->add_remote_sink(topic, sink);
    shm_pub_[topic] = ShmPub{sink, 1};
    LOG_INFO("data plane: SHM channel up topic={} seg={}", topic, name);
}

void DataPlane::shm_pub_unmatch(const MatchInfo& m) {
    const std::string& topic = m.local.topic();
    auto it = shm_pub_.find(topic);
    if (it == shm_pub_.end()) return;
    if (--it->second.ref > 0) return;              // 还有别的同机订阅者用着
    bus_->remove_remote_sink(topic, it->second.sink.get());
    shm_pub_.erase(it);                            // ShmSink 析构 → 段 unlink
    LOG_INFO("data plane: SHM channel down topic={}", topic);
}

void DataPlane::shm_sub_match(const MatchInfo& m) {
    const std::string& topic = m.local.topic();
    std::string name = seg_name(m.remote_participant_id, topic);   // 写者(对端)的段名
    auto it = shm_sub_ref_.find(name);
    if (it != shm_sub_ref_.end()) {                // 同段已有读者(多个本地订阅者)
        ++it->second;
        return;
    }
    shm_readers_->add_reader(name, topic, name);   // key=段名,唯一
    shm_sub_ref_[name] = 1;
    LOG_INFO("data plane: SHM reader up topic={} seg={}", topic, name);
}

void DataPlane::shm_sub_unmatch(const MatchInfo& m) {
    std::string name = seg_name(m.remote_participant_id, m.local.topic());
    auto it = shm_sub_ref_.find(name);
    if (it == shm_sub_ref_.end()) return;
    if (--it->second > 0) return;
    shm_readers_->remove_reader(name);
    shm_sub_ref_.erase(it);
    LOG_INFO("data plane: SHM reader down topic={} seg={}", m.local.topic(), name);
}

}  // namespace mm
