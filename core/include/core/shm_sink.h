#pragma once

#include "core/local_bus.h"          // ISink
#include "transport/shm_segment.h"
#include "common/logger.h"

#include <memory>
#include <string>
#include <utility>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// ShmSink:代表"同机远端订阅者"的本地代理(类比 TCP 的 RemoteSink)。
// 注册进 LocalBus 后,发布者 publish 的字节扇出到这里,直接写进共享内存 ring,
// 不经内核、不加 DataMessage 信封(topic 已隐含在段名里)。
// 超过槽位容量的消息丢弃 + 告警(BEST_EFFORT)。
// ═══════════════════════════════════════════════════════════════
class ShmSink : public ISink {
public:
    explicit ShmSink(std::shared_ptr<ShmSegment> seg) : seg_(std::move(seg)) {}

    void enqueue(const std::string& bytes) override {
        if (!seg_->ring().write(bytes)) {
            LOG_WARN("shm sink: message {} bytes exceeds slot capacity {}, dropped",
                     bytes.size(), seg_->ring().slot_capacity());
        }
    }

private:
    std::shared_ptr<ShmSegment> seg_;
};

}  // namespace mm
