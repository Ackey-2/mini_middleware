#include "core/node.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

// 两个独立 Node(共享默认多播组)互相发现:A 发布 /chatter,B 订阅 /chatter
TEST(NodeDiscovery, TwoNodesDiscoverEachOther) {
    Node a("nodeA");
    Node b("nodeB");
    a.discovery().set_timing(80ms, 5000ms);
    b.discovery().set_timing(80ms, 5000ms);

    std::atomic<int> a_match{0};
    MatchInfo seen;
    a.discovery().on_match([&](const MatchInfo& m) { seen = m; ++a_match; });

    auto pub = a.create_publisher<mm::StringMsg>("/chatter");
    auto sub = b.create_subscriber<mm::StringMsg>("/chatter", [](const mm::StringMsg&) {});

    for (int i = 0; i < 200 && a_match.load() == 0; ++i)
        std::this_thread::sleep_for(25ms);

    ASSERT_GE(a_match.load(), 1);
    EXPECT_EQ(seen.local.topic(), "/chatter");
    EXPECT_EQ(seen.local.kind(), EndpointInfo::PUBLISHER);
    EXPECT_EQ(seen.remote.kind(), EndpointInfo::SUBSCRIBER);
    EXPECT_EQ(seen.remote_participant_id, b.discovery().participant_id());
}
