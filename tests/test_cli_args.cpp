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

TEST(CliArgs, StringOptionsRejectAnotherOptionAsValue) {
    auto missing_type_value = parse({"mm", "topic", "echo", "/chatter", "--type", "--config"});

    EXPECT_EQ(missing_type_value.kind, CliCommandKind::ERROR);
    EXPECT_EQ(missing_type_value.exit_code, 2);
    EXPECT_NE(missing_type_value.message.find("--type"), std::string::npos);
    EXPECT_NE(missing_type_value.message.find("requires a value"), std::string::npos);

    auto missing_config_value = parse({"mm", "topic", "list", "--config", "--wait-ms"});

    EXPECT_EQ(missing_config_value.kind, CliCommandKind::ERROR);
    EXPECT_EQ(missing_config_value.exit_code, 2);
    EXPECT_NE(missing_config_value.message.find("--config"), std::string::npos);
    EXPECT_NE(missing_config_value.message.find("requires a value"), std::string::npos);
}

TEST(CliArgs, RejectsOptionsNotAllowedForCommand) {
    auto list_with_type = parse({"mm", "topic", "list", "--type", "mm.StringMsg"});

    EXPECT_EQ(list_with_type.kind, CliCommandKind::ERROR);
    EXPECT_EQ(list_with_type.exit_code, 2);
    EXPECT_NE(list_with_type.message.find("--type"), std::string::npos);

    auto echo_with_window =
        parse({"mm", "topic", "echo", "/chatter", "--type", "mm.StringMsg", "--window", "5"});

    EXPECT_EQ(echo_with_window.kind, CliCommandKind::ERROR);
    EXPECT_EQ(echo_with_window.exit_code, 2);
    EXPECT_NE(echo_with_window.message.find("--window"), std::string::npos);
}
