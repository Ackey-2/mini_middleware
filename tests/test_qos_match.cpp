#include "discovery/endpoint_matcher.h"
#include "common/qos.h"
#include <gtest/gtest.h>
#include <vector>

using namespace mm;

static EndpointInfo make_ep(EndpointInfo::Kind k, const std::string& topic,
                            const std::string& type, Qos::Reliability rel) {
    EndpointInfo e;
    e.set_kind(k);
    e.set_topic(topic);
    e.set_type_name(type);
    e.set_reliability(static_cast<uint32_t>(rel));
    return e;
}

using R = Qos::Reliability;

// 本地 PUB(writer) × 远端 SUB(reader),按 RxO 决定是否匹配。
static size_t match_count(R writer_rel, R reader_rel) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud", writer_rel)};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::SUBSCRIBER, "/scan", "mm.PointCloud", reader_rel)};
    Locator loc;
    return match_endpoints(local, 1, loc, remote).size();
}

TEST(QosMatch, CompatiblePairsMatch) {
    EXPECT_EQ(match_count(R::BEST_EFFORT, R::BEST_EFFORT), 1u);
    EXPECT_EQ(match_count(R::RELIABLE, R::BEST_EFFORT), 1u);   // writer 更强,兼容
    EXPECT_EQ(match_count(R::RELIABLE, R::RELIABLE), 1u);
}

TEST(QosMatch, IncompatiblePairDoesNotMatch) {
    // reader 要 RELIABLE 但 writer 只是 BEST_EFFORT → 不匹配
    EXPECT_EQ(match_count(R::BEST_EFFORT, R::RELIABLE), 0u);
}

// 反方向:本地是 SUB,远端是 PUB —— 协商结论应一致(只看 writer/reader 角色)。
TEST(QosMatch, RoleResolvedRegardlessOfLocalSide) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::SUBSCRIBER, "/scan", "mm.PointCloud", R::RELIABLE)};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud", R::BEST_EFFORT)};
    Locator loc;
    EXPECT_TRUE(match_endpoints(local, 1, loc, remote).empty());   // reader RELIABLE > writer BE
}
