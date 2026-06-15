#include "core/node.h"
#include "common/logger.h"

namespace mm {

Node::Node(std::string name)
    : name_(std::move(name)), bus_(std::make_shared<LocalBus>()) {
    LOG_INFO("Node created: {}", name_);
}

}  // namespace mm
