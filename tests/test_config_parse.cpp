#include "config/config.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(ConfigParse, AppliesYamlOverrides) {
    const std::string text =
        "node:\n"
        "  name: inspector\n"
        "transport:\n"
        "  enable_shm: false\n"
        "discovery:\n"
        "  group: 239.1.2.3\n"
        "  port: 7501\n"
        "qos:\n"
        "  reliability: reliable\n"
        "  history: keep_all\n"
        "  depth: 32\n";

    auto result = parse_config_text(text);

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.config.node.name, "inspector");
    EXPECT_FALSE(result.config.transport.enable_shm);
    EXPECT_EQ(result.config.discovery.group, "239.1.2.3");
    EXPECT_EQ(result.config.discovery.port, 7501u);
    EXPECT_EQ(result.config.qos.reliability, Qos::Reliability::RELIABLE);
    EXPECT_EQ(result.config.qos.history, Qos::History::KEEP_ALL);
    EXPECT_EQ(result.config.qos.depth, 32u);
}

TEST(ConfigParse, IgnoresBlankLinesAndComments) {
    auto result = parse_config_text(
        "# demo config\n"
        "\n"
        "qos:\n"
        "  reliability: best_effort # inline comment\n");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.config.qos.reliability, Qos::Reliability::BEST_EFFORT);
}
