#include "core/node.h"
#include "common/host_id.h"
#include "common/logger.h"

namespace mm {

Node::Node(std::string name, bool enable_shm)
    : name_(std::move(name)), bus_(std::make_shared<LocalBus>()) {
    // 1. 数据面:启动 TCP 数据服务器(临时端口)+ SHM 读取器
    data_plane_ = std::make_unique<DataPlane>(bus_, "127.0.0.1");
    if (!data_plane_->start()) {
        LOG_WARN("node {}: data plane failed to start", name_);
    }

    // 2. 用真实监听地址构造 Locator(必须在第一条发现公告前就位)
    Locator loc;
    loc.set_ip(data_plane_->advertise_ip());
    loc.set_port(data_plane_->server_port());
    discovery_ = std::make_unique<DiscoveryAgent>(name_, loc);

    // 3. 告诉数据面本地身份(participant_id + host_id),用于同机/跨机选路。
    //    必须在第一个匹配回调前完成 —— 此时 discovery 尚未 start。
    data_plane_->set_local_identity(discovery_->participant_id(), local_host_id(), enable_shm);

    // 4. 把发现层匹配接到数据面(回调在 discovery 后台线程触发)
    DataPlane* dp = data_plane_.get();
    discovery_->on_match([dp](const MatchInfo& m) { dp->handle_match(m); });
    discovery_->on_unmatch([dp](const MatchInfo& m) { dp->handle_unmatch(m); });

    // 5. 启动发现
    if (!discovery_->start()) {
        LOG_WARN("node {}: discovery failed to start (in-process only)", name_);
    }
    LOG_INFO("Node created: {} (data port {}, shm={})",
             name_, data_plane_->server_port(), enable_shm ? "on" : "off");
}

}  // namespace mm
