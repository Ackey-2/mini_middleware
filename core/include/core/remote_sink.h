#pragma once

#include "core/local_bus.h"   // ISink 目前与 LocalBus 同头;若将来 ISink 拆出独立头,这里随之更新
#include "transport/transport.h"
#include "data.pb.h"

#include <memory>
#include <string>
#include <utility>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// RemoteSink:代表"远端订阅者"的本地代理。注册进 LocalBus 后,
// 发布者 publish 的字节会扇出到这里,被包成 DataMessage 经 TCP 发往对端。
// 依赖 Transport 接口(而非具体 TCP 类),便于用假传输做单测。
// ═══════════════════════════════════════════════════════════════
class RemoteSink : public ISink {
public:
    RemoteSink(std::string topic, std::shared_ptr<Transport> conn)
        : topic_(std::move(topic)), conn_(std::move(conn)) {}

    void enqueue(const std::string& bytes) override {
        DataMessage msg;
        msg.set_topic(topic_);
        msg.set_payload(bytes);
        std::string out;
        // DataMessage 仅两个可选字段,序列化实际上不会失败;失败则静默丢弃(BEST_EFFORT)
        if (!msg.SerializeToString(&out)) return;
        // Transport 内部负责加帧头;未连上时 send 返回 false,字节被丢弃(BEST_EFFORT)。
        conn_->send(out);
    }

private:
    std::string topic_;
    std::shared_ptr<Transport> conn_;
};

}  // namespace mm
