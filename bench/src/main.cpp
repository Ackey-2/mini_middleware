#include "bench/bench_args.h"
#include "bench/stats.h"
#include "core/node.h"
#include "messages.pb.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMinPayloadBytes = 32;
constexpr std::size_t kShmInFlightWindow = 16;

std::uint64_t steady_nanoseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch())
            .count());
}

mm::Qos benchmark_qos() {
    mm::Qos qos;
    qos.history = mm::Qos::History::KEEP_ALL;
    return qos;
}

std::string make_payload(std::size_t sequence, std::size_t payload_bytes) {
    std::string prefix =
        std::to_string(sequence) + "|" + std::to_string(steady_nanoseconds()) + "|";
    if (prefix.size() < payload_bytes) {
        prefix.append(payload_bytes - prefix.size(), 'x');
    }
    return prefix;
}

bool parse_send_time(const std::string& payload, std::uint64_t* send_ns) {
    const auto first_sep = payload.find('|');
    if (first_sep == std::string::npos) {
        return false;
    }
    const auto second_sep = payload.find('|', first_sep + 1);
    if (second_sep == std::string::npos || second_sep == first_sep + 1) {
        return false;
    }

    const auto timestamp_text =
        payload.substr(first_sep + 1, second_sep - first_sep - 1);
    char* end = nullptr;
    const auto value = std::strtoull(timestamp_text.c_str(), &end, 10);
    if (end == timestamp_text.c_str() || *end != '\0') {
        return false;
    }
    *send_ns = static_cast<std::uint64_t>(value);
    return true;
}

void print_report(const mm::bench::BenchOptions& options,
                  std::size_t received,
                  const mm::bench::LatencyStats& stats) {
    std::cout << std::fixed << std::setprecision(2)
              << "mode: " << mm::bench::mode_name(options.mode) << "\n"
              << "messages: " << options.count << "\n"
              << "payload_bytes: " << options.payload_bytes << "\n"
              << "received: " << received << "\n"
              << "duration_ms: " << stats.duration_ms << "\n"
              << "throughput_msg_s: " << stats.throughput_msg_s << "\n"
              << "latency_us_avg: " << stats.avg_us << "\n"
              << "latency_us_p50: " << stats.p50_us << "\n"
              << "latency_us_p95: " << stats.p95_us << "\n"
              << "latency_us_p99: " << stats.p99_us << "\n";
}

}  // namespace

int main(int argc, char** argv) {
    auto parsed = mm::bench::parse_bench_args(argc, argv);
    if (parsed.help) {
        std::cout << parsed.message;
        return 0;
    }
    if (!parsed.ok) {
        std::cerr << parsed.message;
        return parsed.exit_code;
    }

    auto options = parsed.options;
    if (options.payload_bytes < kMinPayloadBytes) {
        std::cerr << "payload_bytes " << options.payload_bytes
                  << " is below the minimum metadata length; using "
                  << kMinPayloadBytes << "\n";
        options.payload_bytes = kMinPayloadBytes;
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::uint64_t> latency_us;
    latency_us.reserve(options.count);

    const bool enable_shm = options.mode == mm::bench::BenchMode::SHM;
    mm::Node subscriber_node("mm_bench_subscriber", enable_shm);
    mm::Node publisher_node("mm_bench_publisher", enable_shm);
    subscriber_node.discovery().set_timing(std::chrono::milliseconds(80),
                                           std::chrono::milliseconds(5000));
    publisher_node.discovery().set_timing(std::chrono::milliseconds(80),
                                          std::chrono::milliseconds(5000));

    const auto qos = benchmark_qos();
    auto subscriber = subscriber_node.create_subscriber<mm::StringMsg>(
        options.topic,
        [&](const mm::StringMsg& msg) {
            std::uint64_t send_ns = 0;
            if (!parse_send_time(msg.data(), &send_ns)) {
                return;
            }
            const auto now_ns = steady_nanoseconds();
            const auto sample_us =
                now_ns >= send_ns ? (now_ns - send_ns) / 1000 : 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (latency_us.size() < options.count) {
                    latency_us.push_back(sample_us);
                }
            }
            cv.notify_all();
        },
        qos);

    auto publisher = publisher_node.create_publisher<mm::StringMsg>(options.topic, qos);

    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    const auto timeout = std::chrono::seconds(5 + options.count / 1000);
    const auto deadline = Clock::now() + timeout;
    const auto started = Clock::now();

    bool publish_failed = false;
    for (std::size_t i = 0; i < options.count; ++i) {
        mm::StringMsg msg;
        msg.set_data(make_payload(i, options.payload_bytes));
        if (!publisher->publish(msg)) {
            std::cerr << "publish failed at sequence " << i << "\n";
            publish_failed = true;
            break;
        }

        if (enable_shm && i + 1 > kShmInFlightWindow) {
            const auto target_received = i + 1 - kShmInFlightWindow;
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_until(lock, deadline, [&] {
                return latency_us.size() >= target_received;
            });
        }
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_until(lock, deadline, [&] {
            return latency_us.size() >= options.count;
        });
    }

    const auto finished = Clock::now();
    std::vector<std::uint64_t> samples;
    {
        std::lock_guard<std::mutex> lock(mutex);
        samples = latency_us;
    }

    const auto duration =
        std::chrono::duration_cast<std::chrono::microseconds>(finished - started);
    const auto stats = mm::bench::compute_latency_stats(samples, duration);
    print_report(options, samples.size(), stats);

    if (publish_failed || samples.size() != options.count) {
        std::cerr << "benchmark incomplete: received " << samples.size()
                  << " of " << options.count << "\n";
        return 1;
    }
    return 0;
}
