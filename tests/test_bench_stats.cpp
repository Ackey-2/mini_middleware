#include "bench/stats.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace mm::bench;

TEST(BenchStats, EmptySamplesReturnZeroes) {
    auto stats = compute_latency_stats({}, std::chrono::microseconds(0));

    EXPECT_EQ(stats.count, 0u);
    EXPECT_DOUBLE_EQ(stats.avg_us, 0.0);
    EXPECT_EQ(stats.p50_us, 0u);
    EXPECT_EQ(stats.p95_us, 0u);
    EXPECT_EQ(stats.p99_us, 0u);
    EXPECT_DOUBLE_EQ(stats.duration_ms, 0.0);
    EXPECT_DOUBLE_EQ(stats.throughput_msg_s, 0.0);
}

TEST(BenchStats, SingleSampleUsesSamePercentiles) {
    auto stats = compute_latency_stats({37}, std::chrono::milliseconds(10));

    EXPECT_EQ(stats.count, 1u);
    EXPECT_DOUBLE_EQ(stats.avg_us, 37.0);
    EXPECT_EQ(stats.p50_us, 37u);
    EXPECT_EQ(stats.p95_us, 37u);
    EXPECT_EQ(stats.p99_us, 37u);
    EXPECT_DOUBLE_EQ(stats.duration_ms, 10.0);
    EXPECT_DOUBLE_EQ(stats.throughput_msg_s, 100.0);
}

TEST(BenchStats, SortsSamplesBeforePercentiles) {
    auto stats = compute_latency_stats({40, 10, 30, 20}, std::chrono::milliseconds(2));

    EXPECT_EQ(stats.count, 4u);
    EXPECT_DOUBLE_EQ(stats.avg_us, 25.0);
    EXPECT_EQ(stats.p50_us, 20u);
    EXPECT_EQ(stats.p95_us, 40u);
    EXPECT_EQ(stats.p99_us, 40u);
    EXPECT_DOUBLE_EQ(stats.duration_ms, 2.0);
    EXPECT_DOUBLE_EQ(stats.throughput_msg_s, 2000.0);
}

TEST(BenchStats, ZeroDurationAvoidsDivisionByZero) {
    auto stats = compute_latency_stats({10, 20}, std::chrono::microseconds(0));

    EXPECT_EQ(stats.count, 2u);
    EXPECT_DOUBLE_EQ(stats.throughput_msg_s, 0.0);
}
