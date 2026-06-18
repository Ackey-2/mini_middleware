#pragma once

#include "core/local_bus.h"
#include "common/logger.h"
#include "common/qos.h"

#include <memory>
#include <string>
#include <utility>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Publisher<T>:某 topic 的发布者。
// publish() 把消息序列化成字节,交给 LocalBus 按 topic 分发。
// 类型名取自 protobuf descriptor,用于 topic 级别的类型一致性检查。
// ═══════════════════════════════════════════════════════════════
template <typename MessageT>
class Publisher {
public:
    Publisher(std::string topic, std::shared_ptr<LocalBus> bus, const Qos& qos = {})
        : topic_(std::move(topic)),
          type_name_(MessageT().GetDescriptor()->full_name()),
          bus_(std::move(bus)),
          qos_(qos) {
        bus_->register_publisher(topic_, type_name_);
        LOG_INFO("Publisher created: topic={} type={}", topic_, type_name_);
    }

    const Qos& qos() const { return qos_; }

    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;

    bool publish(const MessageT& msg) {
        std::string bytes;
        if (!msg.SerializeToString(&bytes)) {
            LOG_ERROR("serialize failed for topic {}", topic_);
            return false;
        }
        bus_->publish(topic_, type_name_, bytes);
        return true;
    }

    const std::string& topic() const { return topic_; }

private:
    std::string topic_;
    std::string type_name_;
    std::shared_ptr<LocalBus> bus_;
    Qos qos_;
};

}  // namespace mm
