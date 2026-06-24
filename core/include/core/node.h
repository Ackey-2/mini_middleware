#pragma once

#include "core/data_plane.h"
#include "core/client.h"
#include "core/local_bus.h"
#include "core/publisher.h"
#include "core/rpc_topics.h"
#include "core/service.h"
#include "core/subscriber.h"
#include "discovery/discovery_agent.h"
#include "rpc.pb.h"

#include <cstdint>
#include <memory>
#include <string>
#include <functional>
#include <utility>
#include <vector>

namespace mm {

struct NodeOptions {
    bool enable_shm = true;
    std::string discovery_group = "239.255.0.1";
    uint16_t discovery_port = 7400;
};

// ═══════════════════════════════════════════════════════════════
// Node(Participant):每进程一个。
// 工厂方法创建 Publisher/Subscriber,并持有它们的生命周期。
// 内部持有一个 LocalBus(进程内投递)和一个 DiscoveryAgent(跨进程发现)。
// 创建 pub/sub 时,会把端点注册到发现层,供其它进程匹配。
// ═══════════════════════════════════════════════════════════════
class Node {
public:
    // enable_shm=false 强制同机也走 TCP(用于测试 TCP 路径 / 排障)。
    explicit Node(std::string name, bool enable_shm = true);
    explicit Node(std::string name, const NodeOptions& options);

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    template <typename T>
    std::shared_ptr<Publisher<T>> create_publisher(const std::string& topic,
                                                   const Qos& qos = {}) {
        auto pub = std::make_shared<Publisher<T>>(topic, bus_, qos);
        entities_.push_back(pub);
        discovery_->add_endpoint(EndpointInfo::PUBLISHER, topic,
                                 T().GetDescriptor()->full_name(),
                                 static_cast<uint32_t>(qos.reliability));
        return pub;
    }

    template <typename T>
    std::shared_ptr<Subscriber<T>> create_subscriber(
        const std::string& topic, typename Subscriber<T>::Callback cb,
        const Qos& qos = {}) {
        auto sub = std::make_shared<Subscriber<T>>(topic, std::move(cb), qos);
        bus_->subscribe(topic, T().GetDescriptor()->full_name(), sub);
        entities_.push_back(sub);
        discovery_->add_endpoint(EndpointInfo::SUBSCRIBER, topic,
                                 T().GetDescriptor()->full_name(),
                                 static_cast<uint32_t>(qos.reliability));
        return sub;
    }

    template <typename Req, typename Resp>
    std::shared_ptr<Service<Req, Resp>> create_service(
        const std::string& service_name,
        typename Service<Req, Resp>::Handler handler,
        const Qos& qos = {}) {
        auto registrar = [this, qos](const std::string& topic, const std::string& type_name) {
            discovery_->add_endpoint(EndpointInfo::PUBLISHER, topic, type_name,
                                     static_cast<uint32_t>(qos.reliability));
        };
        auto service = std::make_shared<Service<Req, Resp>>(
            service_name, bus_, std::move(handler), qos, std::move(registrar));
        entities_.push_back(service);

        discovery_->add_endpoint(EndpointInfo::SUBSCRIBER, service->request_topic(),
                                 RpcRequest().GetDescriptor()->full_name(),
                                 static_cast<uint32_t>(qos.reliability));
        discovery_->add_endpoint(EndpointInfo::SERVICE, service_name,
                                 Req().GetDescriptor()->full_name(),
                                 static_cast<uint32_t>(qos.reliability),
                                 Resp().GetDescriptor()->full_name());
        return service;
    }

    template <typename Req, typename Resp>
    std::shared_ptr<Client<Req, Resp>> create_client(const std::string& service_name,
                                                     const Qos& qos = {}) {
        std::string client_id = name_ + "-" + std::to_string(discovery_->participant_id()) +
                                "-" + std::to_string(entities_.size());
        auto client = std::make_shared<Client<Req, Resp>>(service_name, bus_,
                                                          std::move(client_id), qos);
        entities_.push_back(client);

        discovery_->add_endpoint(EndpointInfo::PUBLISHER, client->request_topic(),
                                 RpcRequest().GetDescriptor()->full_name(),
                                 static_cast<uint32_t>(qos.reliability));
        discovery_->add_endpoint(EndpointInfo::SUBSCRIBER, client->reply_topic(),
                                 RpcReply().GetDescriptor()->full_name(),
                                 static_cast<uint32_t>(qos.reliability));
        discovery_->add_endpoint(EndpointInfo::CLIENT, service_name,
                                 Req().GetDescriptor()->full_name(),
                                 static_cast<uint32_t>(qos.reliability),
                                 Resp().GetDescriptor()->full_name());
        return client;
    }

    const std::string& name() const { return name_; }

    // 暴露发现代理(注册匹配回调 / 调参 / demo 用)
    DiscoveryAgent& discovery() { return *discovery_; }

private:
    // 成员析构顺序(声明逆序):entities_(停订阅线程)→ discovery_(停发现线程,
    // 不再有匹配回调)→ data_plane_(停服务器与所有出站连接,释放 RemoteSink)
    // → bus_。discovery 必须先于 data_plane 析构。
    std::string name_;
    std::shared_ptr<LocalBus> bus_;
    std::unique_ptr<DataPlane> data_plane_;
    std::unique_ptr<DiscoveryAgent> discovery_;
    std::vector<std::shared_ptr<void>> entities_;   // 持有 pub/sub 寿命
};

}  // namespace mm
