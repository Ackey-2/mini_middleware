#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mm::bench {

struct LatencyStats {
    std::size_t count = 0;
    double avg_us = 0.0;
    std::uint64_t p50_us = 0;
    std::uint64_t p95_us = 0;
    std::uint64_t p99_us = 0;
    double duration_ms = 0.0;
    double throughput_msg_s = 0.0;
};

LatencyStats compute_latency_stats(std::vector<std::uint64_t> samples_us,
                                   std::chrono::microseconds duration);

}  // namespace mm::bench
