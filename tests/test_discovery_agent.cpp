#include "discovery/discovery_agent.h"
#include "common/host_id.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

static Locator loc(const std::string& ip, uint16_t port) {
    Locator l;
    l.set_ip(ip);
    l.set_port(port);
    return l;
}

TEST(DiscoveryAgent, TwoAgentsMatch) {
    const std::string group = "239.255.1.20";
    const uint16_t port = 7431;

    DiscoveryAgent a("nodeA", loc("127.0.0.1", 5001), group, port);
    DiscoveryAgent b("nodeB", loc("127.0.0.1", 5002), group, port);
    a.set_timing(50ms, 5000ms);
    b.set_timing(50ms, 5000ms);

    std::atomic<int> a_matches{0}, b_matches{0};
    MatchInfo a_seen;
    a.on_match([&](const MatchInfo& m) { a_seen = m; ++a_matches; });
    b.on_match([&](const MatchInfo&) { ++b_matches; });

    a.add_endpoint(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud");
    b.add_endpoint(EndpointInfo::SUBSCRIBER, "/scan", "mm.PointCloud");

    ASSERT_TRUE(a.start());
    ASSERT_TRUE(b.start());

    for (int i = 0; i < 200 && (a_matches.load() == 0 || b_matches.load() == 0); ++i)
        std::this_thread::sleep_for(25ms);

    EXPECT_GE(a_matches.load(), 1);
    EXPECT_GE(b_matches.load(), 1);
    EXPECT_EQ(a_seen.remote.topic(), "/scan");
    EXPECT_EQ(a_seen.remote.kind(), EndpointInfo::SUBSCRIBER);
    EXPECT_EQ(a_seen.remote_locator.port(), 5002u);
    EXPECT_EQ(a_seen.remote_participant_id, b.participant_id());
    // Phase 4:同机两 agent,匹配应带上对端 host_id,且等于本机 host_id。
    EXPECT_FALSE(a_seen.remote_host_id.empty());
    EXPECT_EQ(a_seen.remote_host_id, local_host_id());

    a.stop();
    b.stop();
}

TEST(DiscoveryAgent, UnmatchOnTimeout) {
    const std::string group = "239.255.1.21";
    const uint16_t port = 7432;

    auto a = std::make_unique<DiscoveryAgent>("nodeA", loc("127.0.0.1", 5001), group, port);
    DiscoveryAgent b("nodeB", loc("127.0.0.1", 5002), group, port);
    a->set_timing(50ms, 300ms);
    b.set_timing(50ms, 300ms);

    std::atomic<int> b_match{0}, b_unmatch{0};
    b.on_match([&](const MatchInfo&) { ++b_match; });
    b.on_unmatch([&](const MatchInfo&) { ++b_unmatch; });

    a->add_endpoint(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud");
    b.add_endpoint(EndpointInfo::SUBSCRIBER, "/scan", "mm.PointCloud");

    ASSERT_TRUE(a->start());
    ASSERT_TRUE(b.start());

    for (int i = 0; i < 100 && b_match.load() == 0; ++i)
        std::this_thread::sleep_for(20ms);
    ASSERT_GE(b_match.load(), 1);

    a->stop();
    a.reset();   // A 下线,不再公告

    for (int i = 0; i < 100 && b_unmatch.load() == 0; ++i)
        std::this_thread::sleep_for(20ms);
    EXPECT_GE(b_unmatch.load(), 1);

    b.stop();
}

// 端点在"远端已被发现之后"才注册,应立即对已知远端重算匹配,
// 而不必等对端重新公告。用很长的公告间隔确保:测试窗口内每个 agent 只在
// 启动时公告一次,因此 A 的匹配只可能来自本地新增端点触发的重算。
TEST(DiscoveryAgent, MatchesEndpointAddedAfterRemoteKnown) {
    const std::string group = "239.255.1.23";
    const uint16_t port = 7434;

    DiscoveryAgent a("nodeA", loc("127.0.0.1", 5001), group, port);
    DiscoveryAgent b("nodeB", loc("127.0.0.1", 5002), group, port);
    a.set_timing(10000ms, 30000ms);
    b.set_timing(10000ms, 30000ms);

    const uint64_t b_id = b.participant_id();
    std::atomic<int> a_match{0};
    MatchInfo a_seen;
    a.on_match([&](const MatchInfo& m) {
        if (m.remote_participant_id != b_id) return;   // 忽略组上其它公告
        a_seen = m;
        ++a_match;
    });

    // B 带着订阅端点启动并公告一次;A 先启动以确保收到这次公告。
    b.add_endpoint(EndpointInfo::SUBSCRIBER, "/late", "mm.StringMsg");
    ASSERT_TRUE(a.start());
    ASSERT_TRUE(b.start());

    // A 此时没有任何本地端点,收到 B 的公告也不会匹配。
    std::this_thread::sleep_for(300ms);
    ASSERT_EQ(a_match.load(), 0);

    // 现在给 A 注册一个匹配的发布端点 —— 应立即对已知的 B 重算并触发匹配。
    a.add_endpoint(EndpointInfo::PUBLISHER, "/late", "mm.StringMsg");

    for (int i = 0; i < 100 && a_match.load() == 0; ++i)
        std::this_thread::sleep_for(20ms);

    EXPECT_GE(a_match.load(), 1);
    EXPECT_EQ(a_seen.remote.topic(), "/late");
    EXPECT_EQ(a_seen.remote.kind(), EndpointInfo::SUBSCRIBER);
    EXPECT_EQ(a_seen.remote_participant_id, b_id);

    a.stop();
    b.stop();
}

TEST(DiscoveryAgent, DoesNotSelfMatch) {
    DiscoveryAgent a("solo", loc("127.0.0.1", 5001), "239.255.1.22", 7433);
    a.set_timing(50ms, 5000ms);

    std::atomic<int> matches{0};
    a.on_match([&](const MatchInfo&) { ++matches; });

    a.add_endpoint(EndpointInfo::PUBLISHER, "/x", "mm.StringMsg");
    a.add_endpoint(EndpointInfo::SUBSCRIBER, "/x", "mm.StringMsg");

    ASSERT_TRUE(a.start());
    std::this_thread::sleep_for(400ms);   // 收到多次自己的公告
    EXPECT_EQ(matches.load(), 0);

    a.stop();
}
