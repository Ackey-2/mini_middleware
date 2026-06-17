#include "core/node.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

// 两个独立 Node:UDP 发现匹配 + TCP 数据面把消息真正传过去。
TEST(TcpPubSub, EndToEndDelivery) {
    Node talker("talker");
    Node listener("listener");
    talker.discovery().set_timing(80ms, 5000ms);
    listener.discovery().set_timing(80ms, 5000ms);

    const std::string topic = "/tcp_pubsub_chatter";
    std::atomic<int> got{0};
    std::string last;
    std::mutex lk;

    auto sub = listener.create_subscriber<mm::StringMsg>(
        topic, [&](const mm::StringMsg& m) {
            std::lock_guard<std::mutex> g(lk);
            last = m.data();
            ++got;
        });
    auto pub = talker.create_publisher<mm::StringMsg>(topic);

    // 周期性发布:等发现匹配 + TCP 建链 + 投递
    for (int i = 0; i < 300 && got.load() == 0; ++i) {
        mm::StringMsg msg;
        msg.set_data("hello-tcp");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_GE(got.load(), 1);
    std::lock_guard<std::mutex> g(lk);
    EXPECT_EQ(last, "hello-tcp");
}
