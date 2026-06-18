#include "core/data_plane.h"
#include "core/local_bus.h"
#include "discovery/endpoint_matcher.h"
#include "discovery.pb.h"
#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace mm;
using namespace std::chrono_literals;

class FakeSink : public ISink {
public:
    void enqueue(const std::string& bytes) override {
        std::lock_guard<std::mutex> lk(m_); received_.push_back(bytes);
    }
    size_t count() { std::lock_guard<std::mutex> lk(m_); return received_.size(); }
    bool saw(const std::string& s) {
        std::lock_guard<std::mutex> lk(m_);
        for (auto& r : received_) if (r == s) return true;
        return false;
    }
private:
    std::vector<std::string> received_;
    std::mutex m_;
};

static MatchInfo make_match(EndpointInfo::Kind local_kind, const std::string& topic,
                            const std::string& type, uint64_t remote_pid,
                            const std::string& ip, uint16_t port,
                            const std::string& remote_host_id = "") {
    MatchInfo mi;
    mi.local.set_kind(local_kind);
    mi.local.set_topic(topic);
    mi.local.set_type_name(type);
    mi.remote.set_kind(local_kind == EndpointInfo::PUBLISHER ? EndpointInfo::SUBSCRIBER
                                                             : EndpointInfo::PUBLISHER);
    mi.remote.set_topic(topic);
    mi.remote.set_type_name(type);
    mi.remote_participant_id = remote_pid;
    mi.remote_locator.set_ip(ip);
    mi.remote_locator.set_port(port);
    mi.remote_host_id = remote_host_id;
    return mi;
}

TEST(DataPlane, PublisherSideSendsToReceiver) {
    auto bus_recv = std::make_shared<LocalBus>();
    auto sink = std::make_shared<FakeSink>();
    bus_recv->subscribe("/chat", "mm.StringMsg", sink);

    DataPlane receiver(bus_recv, "127.0.0.1");
    ASSERT_TRUE(receiver.start());
    uint16_t rport = receiver.server_port();
    ASSERT_GT(rport, 0);

    auto bus_pub = std::make_shared<LocalBus>();
    DataPlane sender(bus_pub, "127.0.0.1");
    ASSERT_TRUE(sender.start());

    auto m = make_match(EndpointInfo::PUBLISHER, "/chat", "mm.StringMsg",
                        12345, "127.0.0.1", rport);
    sender.handle_match(m);

    // 连接异步建立;周期性发布直到收到或超时
    for (int i = 0; i < 200 && sink->count() == 0; ++i) {
        bus_pub->publish("/chat", "mm.StringMsg", "hi");
        std::this_thread::sleep_for(10ms);
    }
    ASSERT_TRUE(sink->saw("hi"));

    // 排空在途消息后解除匹配
    std::this_thread::sleep_for(100ms);
    sender.handle_unmatch(m);

    // unmatch 后 RemoteSink 已撤销:新内容不应再到达
    bus_pub->publish("/chat", "mm.StringMsg", "AFTER");
    std::this_thread::sleep_for(150ms);
    EXPECT_FALSE(sink->saw("AFTER"));
}

// 两个 DataPlane 共享同一 host_id → 走共享内存:PUBLISHER 端写 ring,
// SUBSCRIBER 端开同名 ring 读 → deliver_inbound。不经 TCP。
TEST(DataPlane, SameHostUsesSharedMemory) {
    const std::string host = "unit-test-host";
    const uint64_t pub_pid = 111, sub_pid = 222;
    const std::string topic = "/shm_chat";

    auto bus_recv = std::make_shared<LocalBus>();
    auto sink = std::make_shared<FakeSink>();
    bus_recv->subscribe(topic, "mm.StringMsg", sink);
    DataPlane receiver(bus_recv, "127.0.0.1");
    receiver.set_local_identity(sub_pid, host, /*enable_shm=*/true);
    ASSERT_TRUE(receiver.start());

    auto bus_pub = std::make_shared<LocalBus>();
    DataPlane sender(bus_pub, "127.0.0.1");
    sender.set_local_identity(pub_pid, host, /*enable_shm=*/true);
    ASSERT_TRUE(sender.start());

    // sender 本地 PUBLISHER ↔ 远端 SUB(同 host)
    auto mpub = make_match(EndpointInfo::PUBLISHER, topic, "mm.StringMsg",
                           sub_pid, "127.0.0.1", 1, host);
    sender.handle_match(mpub);
    // receiver 本地 SUBSCRIBER ↔ 远端 PUB(同 host)。段名由对端(发布者)id 决定。
    auto msub = make_match(EndpointInfo::SUBSCRIBER, topic, "mm.StringMsg",
                           pub_pid, "127.0.0.1", 2, host);
    receiver.handle_match(msub);

    for (int i = 0; i < 300 && sink->count() == 0; ++i) {
        bus_pub->publish(topic, "mm.StringMsg", "shm-hi");
        std::this_thread::sleep_for(10ms);
    }
    ASSERT_TRUE(sink->saw("shm-hi"));

    std::this_thread::sleep_for(100ms);
    sender.handle_unmatch(mpub);     // 撤销写者 + unlink 段
    receiver.handle_unmatch(msub);   // 撤销读者

    bus_pub->publish(topic, "mm.StringMsg", "AFTER");   // 写者已撤,无人写
    std::this_thread::sleep_for(150ms);
    EXPECT_FALSE(sink->saw("AFTER"));
}

TEST(DataPlane, IgnoresSubscriberSideMatch) {
    auto bus = std::make_shared<LocalBus>();
    DataPlane dp(bus, "127.0.0.1");
    ASSERT_TRUE(dp.start());
    // 本地是 SUBSCRIBER 的匹配:不建任何连接,不崩溃
    auto m = make_match(EndpointInfo::SUBSCRIBER, "/chat", "mm.StringMsg",
                        999, "127.0.0.1", 65000);
    dp.handle_match(m);
    SUCCEED();
}
