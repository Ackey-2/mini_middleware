#include "core/local_bus.h"
#include "common/logger.h"

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

        // 收集存活 sink,顺手清理已失效的 weak_ptr
        auto& sinks = entry.sinks;
        for (auto sit = sinks.begin(); sit != sinks.end();) {
            if (auto sp = sit->lock()) {
                targets.push_back(std::move(sp));
                ++sit;
            } else {
                sit = sinks.erase(sit);
            }
        }
    }
    // 锁外投递
    for (auto& sp : targets) sp->enqueue(bytes);
}

}  // namespace mm
