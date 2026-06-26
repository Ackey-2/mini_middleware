#include "bench/stats.h"

#include <algorithm>

namespace mm::bench {

namespace {

std::uint64_t nearest_rank_percentile(const std::vector<std::uint64_t>& sorted_samples,
                                      std::size_t percentile) {
    const auto count = sorted_samples.size();
    const auto rank = (count * percentile + 99) / 100;
    return sorted_samples[rank - 1];
}

}  // namespace

LatencyStats compute_latency_stats(std::vector<std::uint64_t> samples_us,
                                   std::chrono::microseconds duration) {
    LatencyStats stats;
    stats.count = samples_us.size();
    stats.duration_ms = static_cast<double>(duration.count()) / 1000.0;

    if (samples_us.empty()) {
        return stats;
    }

    std::sort(samples_us.begin(), samples_us.end());

    long double total_us = 0.0;
    for (const auto sample_us : samples_us) {
        total_us += sample_us;
    }

    stats.avg_us = static_cast<double>(total_us / samples_us.size());
    stats.p50_us = nearest_rank_percentile(samples_us, 50);
    stats.p95_us = nearest_rank_percentile(samples_us, 95);
    stats.p99_us = nearest_rank_percentile(samples_us, 99);

    if (duration.count() > 0) {
        const auto seconds = static_cast<double>(duration.count()) / 1000000.0;
        stats.throughput_msg_s = static_cast<double>(stats.count) / seconds;
    }

    return stats;
}

}  // namespace mm::bench
