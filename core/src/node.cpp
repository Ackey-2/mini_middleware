#include "core/node.h"
#include "common/logger.h"

namespace mm {

Node::Node(std::string name) : name_(std::move(name)), bus_(std::make_shared<LocalBus>()) {
    // Phase 2:locator 端口先占位(0),Phase 3 接 TCP server 后填真实端口
    Locator loc;
    loc.set_ip("127.0.0.1");
    loc.set_port(0);
    discovery_ = std::make_unique<DiscoveryAgent>(name_, loc);
    if (!discovery_->start()) {
        LOG_WARN("node {}: discovery failed to start (continuing in-process only)", name_);
    }
    LOG_INFO("Node created: {}", name_);
}

}  // namespace mm
