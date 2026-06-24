#pragma once

#include "common/logger.h"
#include "common/qos.h"
#include "core/local_bus.h"
#include "core/publisher.h"
#include "core/rpc_topics.h"
#include "core/subscriber.h"
#include "rpc.pb.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace mm {

template <typename RequestT, typename ResponseT>
class Client {
public:
    Client(std::string service_name, std::shared_ptr<LocalBus> bus, std::string client_id,
           const Qos& qos = {})
        : service_name_(std::move(service_name)),
          bus_(std::move(bus)),
          client_id_(std::move(client_id)),
          request_topic_(rpc_request_topic(service_name_)),
          reply_topic_(rpc_reply_topic(service_name_, client_id_)),
          request_pub_(std::make_shared<Publisher<RpcRequest>>(request_topic_, bus_, qos)),
          reply_sub_(std::make_shared<Subscriber<RpcReply>>(
              reply_topic_, [this](const RpcReply& reply) { handle_reply(reply); }, qos)) {
        bus_->subscribe(reply_topic_, RpcReply().GetDescriptor()->full_name(), reply_sub_);
    }

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    std::optional<ResponseT> call(const RequestT& req, std::chrono::milliseconds timeout) {
        std::string payload;
        if (!req.SerializeToString(&payload)) {
            LOG_ERROR("rpc client {}: request serialize failed", service_name_);
            return std::nullopt;
        }

        uint64_t request_id = next_request_id_.fetch_add(1);
        auto pending = std::make_shared<Pending>();
        {
            std::lock_guard<std::mutex> lock(pending_mtx_);
            pending_[request_id] = pending;
        }

        RpcRequest wire_req;
        wire_req.set_request_id(request_id);
        wire_req.set_reply_topic(reply_topic_);
        wire_req.set_payload(std::move(payload));
        if (!request_pub_->publish(wire_req)) {
            erase_pending(request_id);
            return std::nullopt;
        }

        std::unique_lock<std::mutex> lock(pending->mtx);
        bool ready = pending->cv.wait_for(lock, timeout, [&pending] { return pending->done; });
        erase_pending(request_id);
        if (!ready) return std::nullopt;
        return pending->response;
    }

    const std::string& request_topic() const { return request_topic_; }
    const std::string& reply_topic() const { return reply_topic_; }

private:
    struct Pending {
        std::mutex mtx;
        std::condition_variable cv;
        bool done = false;
        std::optional<ResponseT> response;
    };

    void erase_pending(uint64_t request_id) {
        std::lock_guard<std::mutex> lock(pending_mtx_);
        pending_.erase(request_id);
    }

    void handle_reply(const RpcReply& wire_reply) {
        std::shared_ptr<Pending> pending;
        {
            std::lock_guard<std::mutex> lock(pending_mtx_);
            auto it = pending_.find(wire_reply.request_id());
            if (it == pending_.end()) return;
            pending = it->second;
        }

        std::optional<ResponseT> parsed;
        if (wire_reply.ok()) {
            ResponseT resp;
            if (resp.ParseFromString(wire_reply.payload())) {
                parsed = std::move(resp);
            } else {
                LOG_ERROR("rpc client {}: response parse failed", service_name_);
            }
        }

        {
            std::lock_guard<std::mutex> lock(pending->mtx);
            pending->response = std::move(parsed);
            pending->done = true;
        }
        pending->cv.notify_one();
    }

    std::string service_name_;
    std::shared_ptr<LocalBus> bus_;
    std::string client_id_;
    std::string request_topic_;
    std::string reply_topic_;
    std::shared_ptr<Publisher<RpcRequest>> request_pub_;
    std::shared_ptr<Subscriber<RpcReply>> reply_sub_;
    std::atomic<uint64_t> next_request_id_{1};

    std::mutex pending_mtx_;
    std::map<uint64_t, std::shared_ptr<Pending>> pending_;
};

}  // namespace mm
