#include "config/config.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(ConfigDefaults, ProvidesCliReadyDefaults) {
    MiddlewareConfig cfg = default_config();

    EXPECT_EQ(cfg.node.name, "mm_cli");
    EXPECT_TRUE(cfg.transport.enable_shm);
    EXPECT_EQ(cfg.discovery.group, "239.255.0.1");
    EXPECT_EQ(cfg.discovery.port, 7400u);
    EXPECT_EQ(cfg.qos.reliability, Qos::Reliability::BEST_EFFORT);
    EXPECT_EQ(cfg.qos.history, Qos::History::KEEP_LAST);
    EXPECT_EQ(cfg.qos.depth, 16u);
}
