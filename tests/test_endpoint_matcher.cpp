#include "discovery/endpoint_matcher.h"
#include <gtest/gtest.h>
#include <vector>

using namespace mm;

static EndpointInfo make_ep(EndpointInfo::Kind k, std::string topic, std::string type) {
    EndpointInfo e;
    e.set_kind(k);
    e.set_topic(std::move(topic));
    e.set_type_name(std::move(type));
    return e;
}

TEST(EndpointMatcher, PubMatchesRemoteSubSameTopicType) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::SUBSCRIBER, "/scan", "mm.PointCloud")};
    Locator loc;
    loc.set_ip("127.0.0.1");
    loc.set_port(7000);

    auto m = match_endpoints(local, 99, loc, remote);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].local.kind(), EndpointInfo::PUBLISHER);
    EXPECT_EQ(m[0].remote.kind(), EndpointInfo::SUBSCRIBER);
    EXPECT_EQ(m[0].remote_participant_id, 99u);
    EXPECT_EQ(m[0].remote_locator.port(), 7000u);
}

TEST(EndpointMatcher, NoMatchDifferentTopic) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::SUBSCRIBER, "/odom", "mm.PointCloud")};
    Locator loc;
    EXPECT_TRUE(match_endpoints(local, 1, loc, remote).empty());
}

TEST(EndpointMatcher, NoMatchDifferentType) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::SUBSCRIBER, "/scan", "mm.StringMsg")};
    Locator loc;
    EXPECT_TRUE(match_endpoints(local, 1, loc, remote).empty());
}

TEST(EndpointMatcher, NoMatchSameKind) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    Locator loc;
    EXPECT_TRUE(match_endpoints(local, 1, loc, remote).empty());
}
