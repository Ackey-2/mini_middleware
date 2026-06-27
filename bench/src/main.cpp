#include "bench/bench_args.h"
#include "bench/message_codec.h"
#include "bench/stats.h"
#include "core/node.h"
#include "messages.pb.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

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

std::string make_run_id() {
    std::random_device random;
    return std::to_string(steady_nanoseconds()) + "-" +
           std::to_string(random()) + "-" + std::to_string(random());
}

struct CallbackState {
    CallbackState(std::string run_id, std::size_t expected_count)
        : collector(std::move(run_id), expected_count) {}

    std::mutex mutex;
    std::condition_variable cv;
    mm::bench::BenchmarkSampleCollector collector;
    bool accepting = true;
};

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
    if (options.requested_payload_bytes < options.payload_bytes) {
        std::cerr << "payload_bytes " << options.requested_payload_bytes
                  << " is below the minimum metadata length; using "
                  << options.payload_bytes << "\n";
    }
    const auto run_id = make_run_id();

    auto state = std::make_shared<CallbackState>(run_id, options.count);

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
        [state](const mm::StringMsg& msg) {
            const auto now_ns = steady_nanoseconds();
            bool accepted = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->accepting) {
                    accepted = state->collector.record(msg.data(), now_ns);
                }
            }
            if (accepted) state->cv.notify_all();
        },
        qos);

    auto publisher = publisher_node.create_publisher<mm::StringMsg>(options.topic, qos);

    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    const auto timeout = std::chrono::seconds(5 + options.count / 1000);
    const auto deadline = Clock::now() + timeout;
    const auto started = Clock::now();

    bool publish_failed = false;
    for (std::size_t i = 0; i < options.count; ++i) {
        if (Clock::now() >= deadline) {
            std::cerr << "publish deadline reached before sequence " << i << "\n";
            publish_failed = true;
            break;
        }

        mm::StringMsg msg;
        msg.set_data(mm::bench::make_benchmark_payload(
            run_id, i, steady_nanoseconds(), options.payload_bytes));
        if (!publisher->publish(msg)) {
            std::cerr << "publish failed at sequence " << i << "\n";
            publish_failed = true;
            break;
        }

        const auto target_received = mm::bench::flow_control_target(i + 1);
        if (target_received > 0) {
            std::unique_lock<std::mutex> lock(state->mutex);
            const bool advanced = state->cv.wait_until(lock, deadline, [&] {
                return state->collector.received_count() >= target_received;
            });
            if (!advanced) {
                std::cerr << "flow-control wait timed out after sequence " << i
                          << ": received " << state->collector.received_count()
                          << ", required " << target_received << "\n";
                publish_failed = true;
                break;
            }
        }
    }

    {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->cv.wait_until(lock, deadline, [&] {
            return state->collector.received_count() >= options.count;
        });
    }

    const auto finished = Clock::now();
    std::vector<std::uint64_t> samples;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->accepting = false;
        samples = state->collector.samples();
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
