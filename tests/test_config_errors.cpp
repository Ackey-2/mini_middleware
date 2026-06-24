#include "config/config.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(ConfigErrors, RejectsInvalidReliability) {
    auto result = parse_config_text("qos:\n  reliability: maybe\n");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("reliability"), std::string::npos);
}

TEST(ConfigErrors, RejectsInvalidHistory) {
    auto result = parse_config_text("qos:\n  history: ancient\n");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("history"), std::string::npos);
}

TEST(ConfigErrors, RejectsInvalidUnsignedInteger) {
    auto result = parse_config_text("discovery:\n  port: nope\n");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("port"), std::string::npos);
}
