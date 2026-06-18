#pragma once

#include "core/local_bus.h"
#include "transport/shm_ring.h"
#include "transport/shm_segment.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// ShmReaderManager:订阅侧的共享内存读取器。
//   - 一个后台 poller 线程轮询所有已登记的 reader ring。
//   - 每读到一条 → bus_->deliver_inbound(topic, bytes)(只投本地订阅者,环路安全)。
//   - 段可能尚未被写者创建:poller 周期性重试 open,直到成功。
//   - 全轮空闲时自适应休眠(命中即清零,空闲渐增至上限),平衡延迟与 CPU。
//
// add_reader/remove_reader 由发现回调线程调用,内部状态用 mtx_ 保护。
// ═══════════════════════════════════════════════════════════════
class ShmReaderManager {
public:
    explicit ShmReaderManager(std::shared_ptr<LocalBus> bus);
    ~ShmReaderManager();

    ShmReaderManager(const ShmReaderManager&) = delete;
    ShmReaderManager& operator=(const ShmReaderManager&) = delete;

    // 登记一个 (key 唯一) 读通道:从段 seg_name 读,投给 topic 的本地订阅者。
    void add_reader(const std::string& key, const std::string& topic,
                    const std::string& seg_name);
    // 撤销读通道(unmatch 时)。
    void remove_reader(const std::string& key);

    void stop();   // 停 poller 线程(幂等)

private:
    struct Entry {
        std::string topic;
        std::string seg_name;
        std::unique_ptr<ShmSegment> seg;   // 未打开前为 null
        ShmRingReader reader;
    };

    void run();                    // poller 线程主体
    bool poll_once();              // 扫一遍所有 reader,返回本轮是否读到数据

    std::shared_ptr<LocalBus> bus_;
    std::mutex mtx_;
    std::map<std::string, Entry> readers_;

    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace mm
