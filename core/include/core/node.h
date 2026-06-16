#pragma once

#include "core/local_bus.h"
#include "core/publisher.h"
#include "core/subscriber.h"
#include "discovery/discovery_agent.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Node(Participant):每进程一个。
// 工厂方法创建 Publisher/Subscriber,并持有它们的生命周期。
// 内部持有一个 LocalBus(进程内投递)和一个 DiscoveryAgent(跨进程发现)。
// 创建 pub/sub 时,会把端点注册到发现层,供其它进程匹配。
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
        discovery_->add_endpoint(EndpointInfo::PUBLISHER, topic,
                                 T().GetDescriptor()->full_name());
        return pub;
    }

    template <typename T>
    std::shared_ptr<Subscriber<T>> create_subscriber(
        const std::string& topic, typename Subscriber<T>::Callback cb) {
        auto sub = std::make_shared<Subscriber<T>>(topic, std::move(cb));
        bus_->subscribe(topic, T().GetDescriptor()->full_name(), sub);
        entities_.push_back(sub);
        discovery_->add_endpoint(EndpointInfo::SUBSCRIBER, topic,
                                 T().GetDescriptor()->full_name());
        return sub;
    }

    const std::string& name() const { return name_; }

    // 暴露发现代理(注册匹配回调 / 调参 / demo 用)
    DiscoveryAgent& discovery() { return *discovery_; }

private:
    // 成员析构顺序(声明逆序):entities_ 先析构(Subscriber 停线程)→ discovery_
    // (停发现线程,结束在途回调)→ bus_ 最后。顺序不可随意调换。
    std::string name_;
    std::shared_ptr<LocalBus> bus_;
    std::unique_ptr<DiscoveryAgent> discovery_;
    std::vector<std::shared_ptr<void>> entities_;   // 持有 pub/sub 寿命
};

}  // namespace mm
