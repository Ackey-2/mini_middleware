#pragma once

#include "core/local_bus.h"
#include "common/blocking_queue.h"
#include "common/logger.h"
#include "common/qos.h"

#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Subscriber<T>:某 topic 的订阅者。
//   - 实现 ISink::enqueue —— 收到字节立即入队,不阻塞发布者
//   - 自己的工作线程从队列取字节 → 反序列化成 T → 调用户回调
// 析构时关闭队列并 join 工作线程。
// ═══════════════════════════════════════════════════════════════
template <typename MessageT>
class Subscriber : public ISink {
public:
    using Callback = std::function<void(const MessageT&)>;

    // qos.history==KEEP_LAST → 队列上限 = depth(满时丢最旧);KEEP_ALL → 无界。
    Subscriber(std::string topic, Callback cb, const Qos& qos = {})
        : topic_(std::move(topic)),
          cb_(std::move(cb)),
          qos_(qos),
          queue_(qos.history == Qos::History::KEEP_LAST ? qos.depth : 0) {
        worker_ = std::thread(&Subscriber::run, this);
    }

    ~Subscriber() override {
        queue_.close();                       // 唤醒阻塞的 pop,使 run() 退出
        if (worker_.joinable()) worker_.join();
    }

    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;

    void enqueue(const std::string& bytes) override { queue_.push(bytes); }

    const std::string& topic() const { return topic_; }

private:
    void run() {
        std::string bytes;
        while (queue_.pop(bytes)) {           // close 后队列空时返回 false
            MessageT msg;
            if (!msg.ParseFromString(bytes)) {
                LOG_ERROR("subscriber {} parse failed", topic_);
                continue;
            }
            cb_(msg);
        }
    }

    std::string topic_;
    Callback cb_;
    Qos qos_;
    BlockingQueue<std::string> queue_;
    std::thread worker_;
};

}  // namespace mm
