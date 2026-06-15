#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// ISink:订阅侧的统一落点。
// 任何"能接收一帧字节"的东西都实现它 —— 本地 Subscriber,
// 以及未来(Phase 3)把字节通过 TCP/SHM 发往远端的代理。
// 这是整个项目从单机长成分布式的关键接缝。
// ═══════════════════════════════════════════════════════════════
class ISink {
public:
    virtual ~ISink() = default;
    virtual void enqueue(const std::string& bytes) = 0;
};

// ═══════════════════════════════════════════════════════════════
// LocalBus:进程内总线,按 topic 把字节分发给所有订阅者。
// 用 weak_ptr 持有订阅者,不延长其寿命;分发时锁内收集存活 sink,
// 锁外投递(避免在持锁时回调用户代码导致死锁)。
// ═══════════════════════════════════════════════════════════════
class LocalBus {
public:
    // 发布者声明某 topic 的类型(首次确定该 topic 类型)
    void register_publisher(const std::string& topic, const std::string& type_name);

    // 注册订阅者;type_name 与该 topic 已有类型不一致则拒绝
    void subscribe(const std::string& topic, const std::string& type_name,
                   std::shared_ptr<ISink> sink);

    // 把 bytes 投给该 topic 所有存活订阅者
    void publish(const std::string& topic, const std::string& type_name,
                 const std::string& bytes);

private:
    struct TopicEntry {
        std::string type_name;                      // 该 topic 约定的类型(空=未定)
        std::vector<std::weak_ptr<ISink>> sinks;
    };

    // 检查/确定 topic 类型;一致返回 true,不一致 LOG_ERROR 返回 false
    bool check_type(TopicEntry& entry, const std::string& type_name,
                    const std::string& topic);

    std::mutex mtx_;
    std::unordered_map<std::string, TopicEntry> topics_;
};

}  // namespace mm
