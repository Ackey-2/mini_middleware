#pragma once

#include "common/logger.h"
#include "common/qos.h"
#include "core/local_bus.h"
#include "core/publisher.h"
#include "core/rpc_topics.h"
#include "core/subscriber.h"
#include "rpc.pb.h"

#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace mm {

template <typename RequestT, typename ResponseT>
class Service {
public:
    using Handler = std::function<ResponseT(const RequestT&)>;
    using EndpointRegistrar =
        std::function<void(const std::string& topic, const std::string& type_name)>;

    Service(std::string service_name, std::shared_ptr<LocalBus> bus, Handler handler,
            const Qos& qos = {}, EndpointRegistrar registrar = {})
        : service_name_(std::move(service_name)),
          bus_(std::move(bus)),
          handler_(std::move(handler)),
          qos_(qos),
          registrar_(std::move(registrar)) {
        request_topic_ = rpc_request_topic(service_name_);
        request_sub_ = std::make_shared<Subscriber<RpcRequest>>(
            request_topic_, [this](const RpcRequest& req) { handle_request(req); }, qos_);
        bus_->subscribe(request_topic_, RpcRequest().GetDescriptor()->full_name(), request_sub_);
    }

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

    const std::string& service_name() const { return service_name_; }
    const std::string& request_topic() const { return request_topic_; }

private:
    std::shared_ptr<Publisher<RpcReply>> publisher_for(const std::string& reply_topic) {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = response_pubs_.find(reply_topic);
        if (it != response_pubs_.end()) return it->second;

        auto pub = std::make_shared<Publisher<RpcReply>>(reply_topic, bus_, qos_);
        if (registrar_) {
            registrar_(reply_topic, RpcReply().GetDescriptor()->full_name());
        }
        response_pubs_[reply_topic] = pub;
        return pub;
    }

    void publish_error(const RpcRequest& req, const std::string& error) {
        RpcReply reply;
        reply.set_request_id(req.request_id());
        reply.set_ok(false);
        reply.set_error(error);
        publisher_for(req.reply_topic())->publish(reply);
    }

    void handle_request(const RpcRequest& wire_req) {
        RequestT req;
        if (!req.ParseFromString(wire_req.payload())) {
            LOG_ERROR("rpc service {}: request parse failed", service_name_);
            publish_error(wire_req, "request parse failed");
            return;
        }

        RpcReply wire_reply;
        wire_reply.set_request_id(wire_req.request_id());
        try {
            ResponseT resp = handler_(req);
            std::string payload;
            if (!resp.SerializeToString(&payload)) {
                publish_error(wire_req, "response serialize failed");
                return;
            }
            wire_reply.set_ok(true);
            wire_reply.set_payload(std::move(payload));
        } catch (const std::exception& e) {
            wire_reply.set_ok(false);
            wire_reply.set_error(e.what());
        } catch (...) {
            wire_reply.set_ok(false);
            wire_reply.set_error("handler threw unknown exception");
        }
        publisher_for(wire_req.reply_topic())->publish(wire_reply);
    }

    std::string service_name_;
    std::string request_topic_;
    std::shared_ptr<LocalBus> bus_;
    Handler handler_;
    Qos qos_;
    EndpointRegistrar registrar_;
    std::shared_ptr<Subscriber<RpcRequest>> request_sub_;

    std::mutex mtx_;
    std::map<std::string, std::shared_ptr<Publisher<RpcReply>>> response_pubs_;
};

}  // namespace mm
