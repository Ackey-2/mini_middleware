#include "core/shm_sink.h"
#include "core/shm_reader.h"
#include "core/local_bus.h"
#include "transport/shm_segment.h"

#include <gtest/gtest.h>

#include <unistd.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace mm;
using namespace std::chrono_literals;

namespace {
std::string seg_name(const char* tag) {
    return std::string("/mm.test.sr.") + tag + "." + std::to_string(::getpid());
}

// 收集投递到本地的字节
class CollectSink : public ISink {
public:
    void enqueue(const std::string& bytes) override {
        std::lock_guard<std::mutex> lk(m_);
        got_.push_back(bytes);
    }
    size_t count() {
        std::lock_guard<std::mutex> lk(m_);
        return got_.size();
    }
    bool saw(const std::string& s) {
        std::lock_guard<std::mutex> lk(m_);
        for (auto& g : got_) if (g == s) return true;
        return false;
    }
private:
    std::vector<std::string> got_;
    std::mutex m_;
};

template <typename Pred>
bool wait_until(Pred p, std::chrono::milliseconds timeout = 2000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (p()) return true;
        std::this_thread::sleep_for(5ms);
    }
    return p();
}
}  // namespace

// 段先建好,reader 后登记:正常投递。
TEST(ShmSinkReader, DeliversToLocalBus) {
    auto name = seg_name("basic");
    auto bus = std::make_shared<LocalBus>();
    auto sink = std::make_shared<CollectSink>();
    bus->subscribe("/scan", "mm.StringMsg", sink);

    std::shared_ptr<ShmSegment> seg = ShmSegment::create(name, 16, 256);
    ASSERT_NE(seg, nullptr);
    ShmSink writer(seg);

    ShmReaderManager mgr(bus);
    mgr.add_reader("k1", "/scan", name);

    // reader 由 poller 异步打开;周期性发布直到收到(BEST_EFFORT:只看连上后的未来消息)
    ASSERT_TRUE(wait_until([&] {
        writer.enqueue("alpha");
        writer.enqueue("beta");
        return sink->saw("alpha") && sink->saw("beta");
    }));
}

// reader 先登记(段尚不存在),写者随后创建:poller 重试 open 后仍能收到。
TEST(ShmSinkReader, ReaderBeforeSegmentRetriesOpen) {
    auto name = seg_name("retry");
    auto bus = std::make_shared<LocalBus>();
    auto sink = std::make_shared<CollectSink>();
    bus->subscribe("/late", "mm.StringMsg", sink);

    ShmReaderManager mgr(bus);
    mgr.add_reader("k1", "/late", name);     // 段还不存在
    std::this_thread::sleep_for(50ms);       // 让 poller 空转几轮(open 失败)

    std::shared_ptr<ShmSegment> seg = ShmSegment::create(name, 8, 128);
    ASSERT_NE(seg, nullptr);
    ShmSink writer(seg);

    ASSERT_TRUE(wait_until([&] {
        writer.enqueue("hello-late");
        return sink->saw("hello-late");
    }));
}

// 超过槽位容量的消息被丢弃,不崩、不投递。
TEST(ShmSinkReader, OversizeDropped) {
    auto name = seg_name("oversize");
    std::shared_ptr<ShmSegment> seg = ShmSegment::create(name, 4, 16);   // 槽位仅 16 字节
    ASSERT_NE(seg, nullptr);
    ShmSink writer(seg);

    auto bus = std::make_shared<LocalBus>();
    auto sink = std::make_shared<CollectSink>();
    bus->subscribe("/big", "mm.StringMsg", sink);
    ShmReaderManager mgr(bus);
    mgr.add_reader("k1", "/big", name);

    ASSERT_TRUE(wait_until([&] {
        writer.enqueue(std::string(100, 'x'));    // 超容量 → write 返回 false,丢弃
        writer.enqueue("small");                   // 这条应到达
        return sink->saw("small");
    }));
    EXPECT_FALSE(sink->saw(std::string(100, 'x')));   // 超容量的从不投递
}
