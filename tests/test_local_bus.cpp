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

TEST(LocalBus, RemoteSinkReceivesPublishButNotInbound) {
    LocalBus bus;
    auto local = std::make_shared<FakeSink>();
    auto remote = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", local);
    bus.add_remote_sink("/t", remote);

    bus.publish("/t", "mm.StringMsg", "out");
    EXPECT_EQ(local->received.size(), 1u);   // 本地收到
    EXPECT_EQ(remote->received.size(), 1u);  // publish 扇出到远端

    bus.deliver_inbound("/t", "in");
    EXPECT_EQ(local->received.size(), 2u);   // 入站投本地
    EXPECT_EQ(remote->received.size(), 1u);  // ★ 入站绝不触达远端(环路安全)
}

TEST(LocalBus, RemoveRemoteSinkStopsDelivery) {
    LocalBus bus;
    auto remote = std::make_shared<FakeSink>();
    bus.add_remote_sink("/t", remote);

    bus.publish("/t", "mm.StringMsg", "before");
    EXPECT_EQ(remote->received.size(), 1u);   // 移除前能收到

    bus.remove_remote_sink("/t", remote.get());
    bus.publish("/t", "mm.StringMsg", "after");
    EXPECT_EQ(remote->received.size(), 1u);    // 移除后不再增长
}
