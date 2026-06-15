#include "discovery/udp_multicast.h"
#include <gtest/gtest.h>
#include <chrono>
#include <string>

using namespace mm;
using namespace std::chrono_literals;

TEST(UdpMulticast, SendReceiveLoopback) {
    UdpMulticast a("239.255.0.9", 7411);
    UdpMulticast b("239.255.0.9", 7411);
    ASSERT_TRUE(a.open());
    ASSERT_TRUE(b.open());

    ASSERT_TRUE(a.send("ping"));

    std::string got;
    ASSERT_TRUE(b.recv(got, 1000ms));
    EXPECT_EQ(got, "ping");
}

TEST(UdpMulticast, RecvTimeoutReturnsFalse) {
    UdpMulticast a("239.255.0.10", 7412);
    ASSERT_TRUE(a.open());
    std::string got;
    EXPECT_FALSE(a.recv(got, 100ms));
}
