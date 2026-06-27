#pragma once

#include <cstddef>
#include <string>

namespace mm::bench {

constexpr std::size_t kMaxBenchmarkCount = 100000;

enum class BenchMode { SHM, TCP };

struct BenchOptions {
    BenchMode mode = BenchMode::SHM;
    std::size_t count = 10000;
    std::size_t payload_bytes = 256;
    std::string topic = "/bench";
};

struct ParseResult {
    bool ok = false;
    bool help = false;
    int exit_code = 2;
    std::string message;
    BenchOptions options;
};

std::string bench_usage();
const char* mode_name(BenchMode mode);
ParseResult parse_bench_args(int argc, char** argv);

}  // namespace mm::bench
