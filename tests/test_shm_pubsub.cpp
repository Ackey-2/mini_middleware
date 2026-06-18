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

// 两个同机 Node(host_id 相同,默认启用 SHM):UDP 发现匹配后,数据面自动
// 切到共享内存零拷贝。发布者写 ring,订阅者轮询 ring 收到。
TEST(ShmPubSub, EndToEndDelivery) {
    Node talker("shm_talker");        // 默认 enable_shm=true
    Node listener("shm_listener");
    talker.discovery().set_timing(80ms, 5000ms);
    listener.discovery().set_timing(80ms, 5000ms);

    const std::string topic = "/shm_pubsub_chatter";
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

    // 周期性发布:等发现匹配 + SHM 建段 + reader 打开 + 投递
    for (int i = 0; i < 300 && got.load() == 0; ++i) {
        mm::StringMsg msg;
        msg.set_data("hello-shm");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_GE(got.load(), 1);
    std::lock_guard<std::mutex> g(lk);
    EXPECT_EQ(last, "hello-shm");
}
