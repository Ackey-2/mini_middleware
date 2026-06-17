#include "core/node.h"
#include "common/logger.h"

namespace mm {

Node::Node(std::string name)
    : name_(std::move(name)), bus_(std::make_shared<LocalBus>()) {
    // 1. 数据面:启动 TCP 数据服务器(临时端口)
    data_plane_ = std::make_unique<DataPlane>(bus_, "127.0.0.1");
    if (!data_plane_->start()) {
        LOG_WARN("node {}: data plane failed to start", name_);
    }

    // 2. 用真实监听地址构造 Locator(必须在第一条发现公告前就位)
    Locator loc;
    loc.set_ip(data_plane_->advertise_ip());
    loc.set_port(data_plane_->server_port());
    discovery_ = std::make_unique<DiscoveryAgent>(name_, loc);

    // 3. 把发现层匹配接到数据面(回调在 discovery 后台线程触发)
    DataPlane* dp = data_plane_.get();
    discovery_->on_match([dp](const MatchInfo& m) { dp->handle_match(m); });
    discovery_->on_unmatch([dp](const MatchInfo& m) { dp->handle_unmatch(m); });

    // 4. 启动发现
    if (!discovery_->start()) {
        LOG_WARN("node {}: discovery failed to start (in-process only)", name_);
    }
    LOG_INFO("Node created: {} (data port {})", name_, data_plane_->server_port());
}

}  // namespace mm
