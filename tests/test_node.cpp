#include "core/node.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

// 规格 §6:1 个 Pub + 2 个 Sub 同 topic,发 N 条,两个 Sub 各收 N 条且内容一致
TEST(Node, OnePublisherTwoSubscribers) {
    Node node("test_node");

    std::atomic<int> a_count{0}, b_count{0};
    std::string a_last, b_last;

    auto sub_a = node.create_subscriber<mm::StringMsg>(
        "/chatter", [&](const mm::StringMsg& m) { a_last = m.data(); ++a_count; });
    auto sub_b = node.create_subscriber<mm::StringMsg>(
        "/chatter", [&](const mm::StringMsg& m) { b_last = m.data(); ++b_count; });

    auto pub = node.create_publisher<mm::StringMsg>("/chatter");

    const int N = 10;
    for (int i = 0; i < N; ++i) {
        mm::StringMsg m;
        m.set_data("msg" + std::to_string(i));
        ASSERT_TRUE(pub->publish(m));
    }

    for (int i = 0; i < 200 && (a_count.load() < N || b_count.load() < N); ++i)
        std::this_thread::sleep_for(10ms);

    EXPECT_EQ(a_count.load(), N);
    EXPECT_EQ(b_count.load(), N);
    EXPECT_EQ(a_last, "msg9");
    EXPECT_EQ(b_last, "msg9");
}
