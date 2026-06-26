#include "bench/bench_args.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace mm::bench;

namespace {

ParseResult parse(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    return parse_bench_args(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST(BenchArgs, DefaultsToShmDemoRun) {
    auto result = parse({"mm_bench"});

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.help);
    EXPECT_EQ(result.options.mode, BenchMode::SHM);
    EXPECT_EQ(result.options.count, 10000u);
    EXPECT_EQ(result.options.payload_bytes, 256u);
    EXPECT_EQ(result.options.topic, "/bench");
}

TEST(BenchArgs, ParsesTcpModeAndNumericOptions) {
    auto result = parse({"mm_bench", "--mode", "tcp", "--count", "42",
                         "--payload-bytes", "1024", "--topic", "/demo"});

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.options.mode, BenchMode::TCP);
    EXPECT_EQ(result.options.count, 42u);
    EXPECT_EQ(result.options.payload_bytes, 1024u);
    EXPECT_EQ(result.options.topic, "/demo");
}

TEST(BenchArgs, ParsesShmModeExplicitly) {
    auto result = parse({"mm_bench", "--mode", "shm"});

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(result.options.mode, BenchMode::SHM);
}

TEST(BenchArgs, HelpExitsZero) {
    auto result = parse({"mm_bench", "--help"});

    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(result.help);
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.message.find("Usage:"), std::string::npos);
}

TEST(BenchArgs, RejectsInvalidMode) {
    auto result = parse({"mm_bench", "--mode", "udp"});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.message.find("unsupported mode"), std::string::npos);
}

TEST(BenchArgs, RejectsMissingOptionValue) {
    auto result = parse({"mm_bench", "--count"});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.message.find("--count requires a value"), std::string::npos);
}

TEST(BenchArgs, RejectsAnotherFlagAsOptionValue) {
    auto result = parse({"mm_bench", "--topic", "--count"});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.message.find("--topic requires a value"), std::string::npos);
}

TEST(BenchArgs, RejectsNonPositiveCount) {
    auto result = parse({"mm_bench", "--count", "0"});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.message.find("--count must be positive"), std::string::npos);
}

TEST(BenchArgs, RejectsInvalidNumber) {
    auto result = parse({"mm_bench", "--payload-bytes", "abc"});

    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.message.find("--payload-bytes must be a positive integer"), std::string::npos);
}
