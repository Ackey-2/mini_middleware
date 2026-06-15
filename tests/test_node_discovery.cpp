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

// 两个独立 Node(共享默认多播组)互相发现:A 发布,B 订阅同一 topic。
// 注意:默认多播组是全局共享的,组上可能有其它参与者(如其它测试残留的公告)。
// 因此用一个不易撞车的 topic,并且只认来自 nodeB 的匹配(按 participant_id 过滤),
// 这样测试对组上的其它公告免疫,稳定不 flaky。
TEST(NodeDiscovery, TwoNodesDiscoverEachOther) {
    Node a("nodeA");
    Node b("nodeB");
    a.discovery().set_timing(80ms, 5000ms);
    b.discovery().set_timing(80ms, 5000ms);

    const std::string topic = "/nd_test_chatter";
    const uint64_t b_id = b.discovery().participant_id();

    std::atomic<int> a_match{0};
    std::mutex seen_mtx;
    MatchInfo seen;

    a.discovery().on_match([&](const MatchInfo& m) {
        if (m.remote_participant_id != b_id) return;   // 只认 nodeB,忽略组上其它公告
        std::lock_guard<std::mutex> lk(seen_mtx);
        seen = m;
        ++a_match;
    });

    auto pub = a.create_publisher<mm::StringMsg>(topic);
    auto sub = b.create_subscriber<mm::StringMsg>(topic, [](const mm::StringMsg&) {});

    for (int i = 0; i < 200 && a_match.load() == 0; ++i)
        std::this_thread::sleep_for(25ms);

    ASSERT_GE(a_match.load(), 1);
    std::lock_guard<std::mutex> lk(seen_mtx);
    EXPECT_EQ(seen.local.topic(), topic);
    EXPECT_EQ(seen.local.kind(), EndpointInfo::PUBLISHER);
    EXPECT_EQ(seen.remote.kind(), EndpointInfo::SUBSCRIBER);
    EXPECT_EQ(seen.remote_participant_id, b_id);
}
