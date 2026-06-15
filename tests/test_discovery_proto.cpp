#include "discovery.pb.h"
#include <gtest/gtest.h>

using namespace mm;

TEST(DiscoveryProto, RoundTrip) {
    ParticipantAnnouncement ann;
    ann.set_participant_id(42);
    ann.set_node_name("n1");
    ann.mutable_data_locator()->set_ip("127.0.0.1");
    ann.mutable_data_locator()->set_port(7000);
    auto* ep = ann.add_endpoints();
    ep->set_kind(EndpointInfo::PUBLISHER);
    ep->set_topic("/scan");
    ep->set_type_name("mm.PointCloud");

    std::string bytes;
    ASSERT_TRUE(ann.SerializeToString(&bytes));

    ParticipantAnnouncement got;
    ASSERT_TRUE(got.ParseFromString(bytes));
    EXPECT_EQ(got.participant_id(), 42u);
    EXPECT_EQ(got.node_name(), "n1");
    EXPECT_EQ(got.data_locator().port(), 7000u);
    ASSERT_EQ(got.endpoints_size(), 1);
    EXPECT_EQ(got.endpoints(0).topic(), "/scan");
    EXPECT_EQ(got.endpoints(0).kind(), EndpointInfo::PUBLISHER);
}
