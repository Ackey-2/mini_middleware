#include "bench/message_codec.h"

#include <gtest/gtest.h>

#include <string>

using namespace mm::bench;

TEST(BenchFlowControl, UsesA16MessageWindow) {
    EXPECT_EQ(flow_control_target(16), 0u);
    EXPECT_EQ(flow_control_target(17), 1u);
    EXPECT_EQ(flow_control_target(1000), 984u);
}

TEST(BenchMessageCodec, RoundTripsRunSequenceAndTimestamp) {
    const auto payload = make_benchmark_payload("run-a", 7, 123456, 64);

    ASSERT_EQ(payload.size(), 64u);
    const auto decoded = parse_benchmark_payload(payload);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->run_id, "run-a");
    EXPECT_EQ(decoded->sequence, 7u);
    EXPECT_EQ(decoded->send_ns, 123456u);
}

TEST(BenchMessageCodec, SmallPayloadGrowsToFitRunMetadata) {
    const auto payload = make_benchmark_payload("a-long-run-id", 999, 123456, 1);

    EXPECT_GT(payload.size(), 1u);
    EXPECT_TRUE(parse_benchmark_payload(payload).has_value());
}

TEST(BenchMessageCodec, RejectsMalformedNumericFields) {
    EXPECT_FALSE(parse_benchmark_payload("run|sequence|123|").has_value());
    EXPECT_FALSE(parse_benchmark_payload("run|1|timestamp|").has_value());
}

TEST(BenchmarkSampleCollector, IgnoresForeignRunIds) {
    BenchmarkSampleCollector collector("expected", 2);

    EXPECT_FALSE(collector.record(
        make_benchmark_payload("foreign", 0, 1000, 64), 2000));
    EXPECT_EQ(collector.received_count(), 0u);
}

TEST(BenchmarkSampleCollector, RejectsDuplicateAndOutOfRangeSequences) {
    BenchmarkSampleCollector collector("expected", 2);
    const auto first = make_benchmark_payload("expected", 0, 1000, 64);

    EXPECT_TRUE(collector.record(first, 2000));
    EXPECT_FALSE(collector.record(first, 3000));
    EXPECT_FALSE(collector.record(
        make_benchmark_payload("expected", 2, 1000, 64), 2000));
    EXPECT_EQ(collector.received_count(), 1u);

    const auto samples = collector.samples();
    ASSERT_EQ(samples.size(), 1u);
    EXPECT_EQ(samples.front(), 1u);
}

TEST(BenchmarkSampleCollector, RecordsExactlyOneSamplePerExpectedSequence) {
    BenchmarkSampleCollector collector("expected", 3);

    EXPECT_TRUE(collector.record(
        make_benchmark_payload("expected", 2, 1000, 64), 5000));
    EXPECT_TRUE(collector.record(
        make_benchmark_payload("expected", 0, 2000, 64), 5000));
    EXPECT_TRUE(collector.record(
        make_benchmark_payload("expected", 1, 3000, 64), 5000));

    EXPECT_EQ(collector.received_count(), 3u);
    EXPECT_EQ(collector.samples().size(), 3u);
}
