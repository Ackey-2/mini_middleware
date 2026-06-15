#pragma once

#include "core/local_bus.h"
#include "core/publisher.h"
#include "core/subscriber.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Node(Participant):每进程一个。
// 工厂方法创建 Publisher/Subscriber,并持有它们的生命周期。
// 内部持有一个 LocalBus;Phase 2/3 网络层会接到这同一个 bus 上。
// ═══════════════════════════════════════════════════════════════
class Node {
public:
    explicit Node(std::string name);

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    template <typename T>
    std::shared_ptr<Publisher<T>> create_publisher(const std::string& topic) {
        auto pub = std::make_shared<Publisher<T>>(topic, bus_);
        entities_.push_back(pub);
        return pub;
    }

    template <typename T>
    std::shared_ptr<Subscriber<T>> create_subscriber(
        const std::string& topic, typename Subscriber<T>::Callback cb) {
        auto sub = std::make_shared<Subscriber<T>>(topic, std::move(cb));
        bus_->subscribe(topic, T().GetDescriptor()->full_name(), sub);  // 实例方法,非静态
        entities_.push_back(sub);
        return sub;
    }

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::shared_ptr<LocalBus> bus_;
    std::vector<std::shared_ptr<void>> entities_;   // 持有 pub/sub 寿命
};

}  // namespace mm
