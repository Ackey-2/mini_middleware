#include "core/local_bus.h"
#include "common/logger.h"

namespace {
// 收集存活 sink 到 out,顺手清理失效的 weak_ptr
void collect(std::vector<std::weak_ptr<mm::ISink>>& sinks,
             std::vector<std::shared_ptr<mm::ISink>>& out) {
    for (auto it = sinks.begin(); it != sinks.end();) {
        if (auto sp = it->lock()) { out.push_back(std::move(sp)); ++it; }
        else it = sinks.erase(it);
    }
}
}  // namespace

namespace mm {

bool LocalBus::check_type(TopicEntry& entry, const std::string& type_name,
                          const std::string& topic) {
    if (entry.type_name.empty()) {
        entry.type_name = type_name;     // 首次确定
        return true;
    }
    if (entry.type_name != type_name) {
        LOG_ERROR("topic {} type mismatch: expected {}, got {}",
                  topic, entry.type_name, type_name);
        return false;
    }
    return true;
}

void LocalBus::register_publisher(const std::string& topic,
                                  const std::string& type_name) {
    std::lock_guard<std::mutex> lock(mtx_);
    check_type(topics_[topic], type_name, topic);
}

void LocalBus::subscribe(const std::string& topic, const std::string& type_name,
                         std::shared_ptr<ISink> sink) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto& entry = topics_[topic];
    if (!check_type(entry, type_name, topic)) return;
    entry.sinks.push_back(std::move(sink));
}

void LocalBus::publish(const std::string& topic, const std::string& type_name,
                       const std::string& bytes) {
    std::vector<std::shared_ptr<ISink>> targets;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = topics_.find(topic);
        if (it == topics_.end()) return;
        auto& entry = it->second;
        if (!check_type(entry, type_name, topic)) return;
        collect(entry.sinks, targets);
        collect(entry.remote_sinks, targets);
    }
    // 锁外投递
    for (auto& sp : targets) sp->enqueue(bytes);
}

void LocalBus::add_remote_sink(const std::string& topic, std::shared_ptr<ISink> sink) {
    std::lock_guard<std::mutex> lock(mtx_);
    // 类型由上层(Node 端点匹配)保证,此处不校验
    topics_[topic].remote_sinks.push_back(std::move(sink));
}

void LocalBus::remove_remote_sink(const std::string& topic, ISink* sink) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = topics_.find(topic);
    if (it == topics_.end()) return;
    auto& v = it->second.remote_sinks;
    for (auto sit = v.begin(); sit != v.end();) {
        auto sp = sit->lock();
        if (!sp || sp.get() == sink) sit = v.erase(sit);   // 命中或已失效都剔除
        else ++sit;
    }
}

void LocalBus::deliver_inbound(const std::string& topic, const std::string& bytes) {
    // 不校验 type_name:发现层匹配已保证类型兼容;入站只投本地订阅者
    std::vector<std::shared_ptr<ISink>> targets;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = topics_.find(topic);
        if (it == topics_.end()) return;
        collect(it->second.sinks, targets);   // 只本地订阅者,绝不触达 remote_sinks
    }
    for (auto& sp : targets) sp->enqueue(bytes);
}

}  // namespace mm
