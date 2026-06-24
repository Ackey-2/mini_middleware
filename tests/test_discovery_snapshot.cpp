#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

TEST(DiscoverySnapshot, IncludesLocalEndpoint) {
    Node node("snapshot_node");
    auto pub = node.create_publisher<mm::StringMsg>("/snapshot_chatter");

    auto endpoints = node.discovery().snapshot_endpoints();

    bool found = false;
    for (const auto& ep : endpoints) {
        if (ep.local && ep.node_name == "snapshot_node" &&
            ep.endpoint.topic() == "/snapshot_chatter" &&
            ep.endpoint.kind() == EndpointInfo::PUBLISHER) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(DiscoverySnapshot, NodeOptionsConfigureDiscoveryPort) {
    NodeOptions opts;
    opts.discovery_port = 7502;
    Node talker("configured_talker", opts);
    Node listener("configured_listener", opts);
    talker.discovery().set_timing(80ms, 5000ms);
    listener.discovery().set_timing(80ms, 5000ms);
    auto pub = talker.create_publisher<mm::StringMsg>("/configured_chatter");

    bool found = false;
    for (int i = 0; i < 30 && !found; ++i) {
        auto endpoints = listener.discovery().snapshot_endpoints();
        for (const auto& ep : endpoints) {
            if (!ep.local && ep.node_name == "configured_talker" &&
                ep.endpoint.topic() == "/configured_chatter") {
                found = true;
            }
        }
        std::this_thread::sleep_for(50ms);
    }
    EXPECT_TRUE(found);
}
