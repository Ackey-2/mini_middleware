#include "core/node.h"
#include "common/qos.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

// RELIABLE 两端:发现期协商通过,数据走 TCP(同机也不用有损 SHM),端到端可达。
TEST(QosPubSub, ReliableEndToEnd) {
    Qos rel;
    rel.reliability = Qos::Reliability::RELIABLE;

    Node talker("qos_talker");
    Node listener("qos_listener");
    talker.discovery().set_timing(80ms, 5000ms);
    listener.discovery().set_timing(80ms, 5000ms);

    const std::string topic = "/qos_reliable_chatter";
    std::atomic<int> got{0};
    std::string last;
    std::mutex lk;

    auto sub = listener.create_subscriber<mm::StringMsg>(
        topic,
        [&](const mm::StringMsg& m) {
            std::lock_guard<std::mutex> g(lk);
            last = m.data();
            ++got;
        },
        rel);
    auto pub = talker.create_publisher<mm::StringMsg>(topic, rel);

    for (int i = 0; i < 300 && got.load() == 0; ++i) {
        mm::StringMsg msg;
        msg.set_data("hello-reliable");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_GE(got.load(), 1);
    std::lock_guard<std::mutex> g(lk);
    EXPECT_EQ(last, "hello-reliable");
}

// 不兼容:订阅者要 RELIABLE,发布者只是 BEST_EFFORT → 永不匹配 → 收不到。
TEST(QosPubSub, IncompatibleQosNoDelivery) {
    Qos reliable;
    reliable.reliability = Qos::Reliability::RELIABLE;
    Qos best_effort;   // 默认 BEST_EFFORT

    Node talker("qos_talker2");
    Node listener("qos_listener2");
    talker.discovery().set_timing(80ms, 5000ms);
    listener.discovery().set_timing(80ms, 5000ms);

    const std::string topic = "/qos_incompatible";
    std::atomic<int> got{0};

    auto sub = listener.create_subscriber<mm::StringMsg>(
        topic, [&](const mm::StringMsg&) { ++got; }, reliable);   // reader RELIABLE
    auto pub = talker.create_publisher<mm::StringMsg>(topic, best_effort);  // writer BE

    // 持续发布一段时间,足以完成多轮发现;不兼容则始终不匹配。
    for (int i = 0; i < 100; ++i) {
        mm::StringMsg msg;
        msg.set_data("should-not-arrive");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
    }

    EXPECT_EQ(got.load(), 0);
}
