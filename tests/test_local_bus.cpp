#include "core/local_bus.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace mm;

// 测试替身:一个最简单的 sink,把收到的字节存起来
class FakeSink : public ISink {
public:
    void enqueue(const std::string& bytes) override { received.push_back(bytes); }
    std::vector<std::string> received;
};

TEST(LocalBus, DeliversToSingleSubscriber) {
    LocalBus bus;
    auto sink = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", sink);
    bus.publish("/t", "mm.StringMsg", "hello");
    ASSERT_EQ(sink->received.size(), 1u);
    EXPECT_EQ(sink->received[0], "hello");
}

TEST(LocalBus, DeliversToMultipleSubscribers) {
    LocalBus bus;
    auto a = std::make_shared<FakeSink>();
    auto b = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", a);
    bus.subscribe("/t", "mm.StringMsg", b);
    bus.publish("/t", "mm.StringMsg", "x");
    EXPECT_EQ(a->received.size(), 1u);
    EXPECT_EQ(b->received.size(), 1u);
}

TEST(LocalBus, NoSubscriberIsNoop) {
    LocalBus bus;
    bus.publish("/empty", "mm.StringMsg", "x");  // 不崩溃即可
    SUCCEED();
}

TEST(LocalBus, PrunesExpiredSinks) {
    LocalBus bus;
    auto a = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", a);
    {
        auto b = std::make_shared<FakeSink>();
        bus.subscribe("/t", "mm.StringMsg", b);
    }  // b 离开作用域被销毁,bus 里只剩 weak_ptr
    bus.publish("/t", "mm.StringMsg", "x");  // 不能因为 b 失效而崩溃
    EXPECT_EQ(a->received.size(), 1u);
}

TEST(LocalBus, RejectsTypeMismatch) {
    LocalBus bus;
    auto a = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", a);
    // 同一 topic 用不同类型订阅,应被拒绝(不加入投递列表)
    auto b = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.Point3D", b);
    bus.publish("/t", "mm.StringMsg", "x");
    EXPECT_EQ(a->received.size(), 1u);
    EXPECT_EQ(b->received.size(), 0u);
}
