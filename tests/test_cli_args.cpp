#include "cli/args.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace mm;

namespace {

CliCommand parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    return parse_cli_args(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST(CliArgs, ParsesTopicList) {
    auto cmd = parse({"mm", "topic", "list", "--wait-ms", "250"});

    EXPECT_EQ(cmd.kind, CliCommandKind::TOPIC_LIST);
    EXPECT_EQ(cmd.wait_ms, 250);
}

TEST(CliArgs, ParsesTopicEcho) {
    auto cmd = parse({"mm", "topic", "echo", "/chatter", "--type", "mm.StringMsg", "--count", "2"});

    EXPECT_EQ(cmd.kind, CliCommandKind::TOPIC_ECHO);
    EXPECT_EQ(cmd.topic, "/chatter");
    EXPECT_EQ(cmd.type_name, "mm.StringMsg");
    EXPECT_EQ(cmd.count, 2);
}

TEST(CliArgs, ParsesTopicHz) {
    auto cmd = parse({"mm", "topic", "hz", "/chatter", "--type", "mm.StringMsg", "--window", "5"});

    EXPECT_EQ(cmd.kind, CliCommandKind::TOPIC_HZ);
    EXPECT_EQ(cmd.topic, "/chatter");
    EXPECT_EQ(cmd.type_name, "mm.StringMsg");
    EXPECT_EQ(cmd.window, 5);
}

TEST(CliArgs, MissingTypeIsUsageError) {
    auto cmd = parse({"mm", "topic", "echo", "/chatter"});

    EXPECT_EQ(cmd.kind, CliCommandKind::ERROR);
    EXPECT_EQ(cmd.exit_code, 2);
    EXPECT_NE(cmd.message.find("--type"), std::string::npos);
}
