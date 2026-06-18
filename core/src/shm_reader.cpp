#include "core/shm_reader.h"
#include "common/logger.h"

#include <chrono>

namespace mm {

namespace {
constexpr int kMaxDrainPerReader = 256;                  // 每轮每 reader 最多取这么多,避免饿死他人
constexpr auto kMinSleep = std::chrono::microseconds(50);
constexpr auto kMaxSleep = std::chrono::microseconds(1000);
}  // namespace

ShmReaderManager::ShmReaderManager(std::shared_ptr<LocalBus> bus) : bus_(std::move(bus)) {
    running_.store(true);
    thread_ = std::thread(&ShmReaderManager::run, this);
}

ShmReaderManager::~ShmReaderManager() { stop(); }

void ShmReaderManager::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mtx_);
    readers_.clear();
}

void ShmReaderManager::add_reader(const std::string& key, const std::string& topic,
                                  const std::string& seg_name) {
    std::lock_guard<std::mutex> lock(mtx_);
    if (readers_.count(key)) return;   // 幂等
    Entry e;
    e.topic = topic;
    e.seg_name = seg_name;
    readers_.emplace(key, std::move(e));   // seg 先留空,poller 负责打开
    LOG_INFO("shm reader: + topic={} seg={}", topic, seg_name);
}

void ShmReaderManager::remove_reader(const std::string& key) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = readers_.find(key);
    if (it == readers_.end()) return;
    LOG_INFO("shm reader: - topic={} seg={}", it->second.topic, it->second.seg_name);
    readers_.erase(it);
}

bool ShmReaderManager::poll_once() {
    bool did_work = false;
    std::lock_guard<std::mutex> lock(mtx_);
    std::string bytes;
    for (auto& kv : readers_) {
        Entry& e = kv.second;
        if (!e.seg) {                                  // 段尚未就绪:尝试打开
            e.seg = ShmSegment::open(e.seg_name);
            if (!e.seg) continue;                      // 写者还没创建,下轮再试
            e.reader = e.seg->ring().make_reader();
        }
        for (int i = 0; i < kMaxDrainPerReader; ++i) {
            if (!e.reader.read(bytes)) break;
            bus_->deliver_inbound(e.topic, bytes);     // 只投本地订阅者
            did_work = true;
        }
    }
    return did_work;
}

void ShmReaderManager::run() {
    auto sleep = kMinSleep;
    while (running_.load()) {
        if (poll_once()) {
            sleep = kMinSleep;                          // 有数据:压低延迟
        } else {
            std::this_thread::sleep_for(sleep);
            if (sleep < kMaxSleep) sleep *= 2;          // 空闲:渐增退避,省 CPU
        }
    }
}

}  // namespace mm
