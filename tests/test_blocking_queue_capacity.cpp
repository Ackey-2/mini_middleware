#include "common/blocking_queue.h"

#include <gtest/gtest.h>

#include <string>

using namespace mm;

// KEEP_LAST N:容量满时丢最旧,只保留最近 N 条。
TEST(BlockingQueueCapacity, KeepLastDropsOldest) {
    BlockingQueue<int> q(3);   // 容量 3
    for (int i = 0; i < 10; ++i) q.push(i);   // 推 0..9

    EXPECT_EQ(q.size(), 3u);
    EXPECT_EQ(q.dropped(), 7u);   // 丢了 0..6

    int v;
    ASSERT_TRUE(q.try_pop(v, std::chrono::milliseconds(0)));
    EXPECT_EQ(v, 7);              // 队首是最旧的"幸存者" 7
    ASSERT_TRUE(q.try_pop(v, std::chrono::milliseconds(0)));
    EXPECT_EQ(v, 8);
    ASSERT_TRUE(q.try_pop(v, std::chrono::milliseconds(0)));
    EXPECT_EQ(v, 9);
}

// capacity=0:无界(KEEP_ALL),不丢。
TEST(BlockingQueueCapacity, UnboundedKeepsAll) {
    BlockingQueue<int> q;   // 默认无界
    for (int i = 0; i < 1000; ++i) q.push(i);
    EXPECT_EQ(q.size(), 1000u);
    EXPECT_EQ(q.dropped(), 0u);
}

// 容量为 1:始终只留最新一条。
TEST(BlockingQueueCapacity, DepthOneKeepsLatest) {
    BlockingQueue<std::string> q(1);
    q.push("a");
    q.push("b");
    q.push("c");
    EXPECT_EQ(q.size(), 1u);
    std::string s;
    ASSERT_TRUE(q.try_pop(s, std::chrono::milliseconds(0)));
    EXPECT_EQ(s, "c");
}
