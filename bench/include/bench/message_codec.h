#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm::bench {

constexpr std::size_t kBenchmarkInFlightWindow = 16;

constexpr std::size_t flow_control_target(std::size_t published_count) {
    return published_count > kBenchmarkInFlightWindow
               ? published_count - kBenchmarkInFlightWindow
               : 0;
}

struct BenchmarkMessageMetadata {
    std::string run_id;
    std::size_t sequence = 0;
    std::uint64_t send_ns = 0;
};

std::string make_benchmark_payload(const std::string& run_id,
                                   std::size_t sequence,
                                   std::uint64_t send_ns,
                                   std::size_t payload_bytes);
std::optional<BenchmarkMessageMetadata> parse_benchmark_payload(
    const std::string& payload);
std::size_t minimum_benchmark_payload_bytes(const std::string& run_id,
                                            std::size_t count);
std::size_t effective_benchmark_payload_bytes(std::size_t requested_payload_bytes,
                                              std::size_t count);
std::size_t serialized_string_message_size(std::size_t payload_bytes);
std::size_t tcp_frame_payload_size(const std::string& topic,
                                   std::size_t payload_bytes);

class BenchmarkSampleCollector {
public:
    BenchmarkSampleCollector(std::string run_id, std::size_t expected_count);

    bool record(const std::string& payload, std::uint64_t receive_ns);
    std::size_t received_count() const { return received_count_; }
    std::vector<std::uint64_t> samples() const;

private:
    std::string run_id_;
    std::vector<std::uint64_t> samples_by_sequence_;
    std::vector<bool> seen_;
    std::size_t received_count_ = 0;
};

}  // namespace mm::bench
