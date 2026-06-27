#include "bench/bench_args.h"
#include "core/shm_limits.h"
#include "data.pb.h"
#include "messages.pb.h"
#include "transport/frame_codec.h"

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

void expect_usage_error(const ParseResult& result) {
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.exit_code, 2);
    EXPECT_NE(result.message.find("Usage:"), std::string::npos);
}

std::size_t tcp_frame_payload_size(std::size_t payload_bytes,
                                   const std::string& topic = "/bench") {
    mm::StringMsg message;
    message.mutable_data()->resize(payload_bytes);
    std::string serialized_message;
    EXPECT_TRUE(message.SerializeToString(&serialized_message));

    mm::DataMessage envelope;
    envelope.set_topic(topic);
    envelope.set_payload(serialized_message);
    return envelope.ByteSizeLong();
}

std::size_t largest_tcp_payload(const std::string& topic = "/bench") {
    std::size_t low = 1;
    std::size_t high = mm::FrameCodec::MAX_PAYLOAD_SIZE;
    while (low < high) {
        const auto mid = low + (high - low + 1) / 2;
        if (tcp_frame_payload_size(mid, topic) <=
            mm::FrameCodec::MAX_PAYLOAD_SIZE) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    return low;
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

    expect_usage_error(result);
    EXPECT_NE(result.message.find("unsupported mode"), std::string::npos);
}

TEST(BenchArgs, RejectsMissingOptionValue) {
    auto result = parse({"mm_bench", "--count"});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("--count requires a value"), std::string::npos);
}

TEST(BenchArgs, RejectsAnotherFlagAsOptionValue) {
    auto result = parse({"mm_bench", "--topic", "--count"});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("--topic requires a value"), std::string::npos);
}

TEST(BenchArgs, RejectsNonPositiveCount) {
    auto result = parse({"mm_bench", "--count", "0"});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("--count must be positive"), std::string::npos);
}

TEST(BenchArgs, AcceptsCountAtDemoMaximum) {
    auto result = parse({"mm_bench", "--count",
                         std::to_string(kMaxBenchmarkCount)});

    EXPECT_TRUE(result.ok) << result.message;
}

TEST(BenchArgs, RejectsCountAboveDemoMaximum) {
    auto result = parse({"mm_bench", "--count",
                         std::to_string(kMaxBenchmarkCount + 1)});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("--count must not exceed"), std::string::npos);
    EXPECT_NE(result.message.find(std::to_string(kMaxBenchmarkCount)),
              std::string::npos);
}

TEST(BenchArgs, RejectsInvalidNumber) {
    auto result = parse({"mm_bench", "--payload-bytes", "abc"});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("--payload-bytes must be a positive integer"), std::string::npos);
}

TEST(BenchArgs, AcceptsPayloadAtExactShmSerializedBoundary) {
    constexpr std::size_t kPayloadBytes = 262140;
    mm::StringMsg message;
    message.set_data(std::string(kPayloadBytes, 'x'));

    ASSERT_EQ(message.ByteSizeLong(), mm::kShmSlotCapacity);

    auto result = parse(
        {"mm_bench", "--mode", "shm", "--payload-bytes", "262140"});

    EXPECT_TRUE(result.ok) << result.message;
}

TEST(BenchArgs, RejectsPayloadOneByteBeyondShmSerializedBoundary) {
    constexpr std::size_t kPayloadBytes = 262141;
    mm::StringMsg message;
    message.set_data(std::string(kPayloadBytes, 'x'));

    ASSERT_EQ(message.ByteSizeLong(), mm::kShmSlotCapacity + 1u);

    auto result = parse(
        {"mm_bench", "--mode", "shm", "--payload-bytes", "262141"});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("serialized benchmark message is 262145 bytes"),
              std::string::npos);
}

TEST(BenchArgs, AllowsTcpPayloadBeyondShmSerializedBoundary) {
    auto result = parse(
        {"mm_bench", "--mode", "tcp", "--payload-bytes", "262141"});

    EXPECT_TRUE(result.ok) << result.message;
}

TEST(BenchArgs, AcceptsPayloadAtExactTcpFrameBoundary) {
    const auto payload_bytes = largest_tcp_payload();
    ASSERT_EQ(tcp_frame_payload_size(payload_bytes),
              mm::FrameCodec::MAX_PAYLOAD_SIZE);

    auto result = parse({"mm_bench", "--mode", "tcp", "--payload-bytes",
                         std::to_string(payload_bytes)});

    EXPECT_TRUE(result.ok) << result.message;
}

TEST(BenchArgs, RejectsPayloadOneByteBeyondTcpFrameBoundary) {
    const auto payload_bytes = largest_tcp_payload();
    ASSERT_GT(tcp_frame_payload_size(payload_bytes + 1),
              mm::FrameCodec::MAX_PAYLOAD_SIZE);

    auto result = parse({"mm_bench", "--mode", "tcp", "--payload-bytes",
                         std::to_string(payload_bytes + 1)});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("TCP frame payload"), std::string::npos);
    EXPECT_NE(result.message.find(std::to_string(mm::FrameCodec::MAX_PAYLOAD_SIZE)),
              std::string::npos);
}

TEST(BenchArgs, RejectsExtremeTcpPayloadWithoutAllocationFailure) {
    auto result = parse({"mm_bench", "--mode", "tcp", "--payload-bytes",
                         "18446744073709551615"});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("TCP frame payload"), std::string::npos);
}

TEST(BenchArgs, RejectsUnknownOption) {
    auto result = parse({"mm_bench", "--bogus"});

    expect_usage_error(result);
    EXPECT_NE(result.message.find("unknown option"), std::string::npos);
}
