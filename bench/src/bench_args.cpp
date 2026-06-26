#include "bench/bench_args.h"

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>

namespace mm::bench {
namespace {

bool is_option_token(const std::string& text) {
    return text.rfind("--", 0) == 0;
}

ParseResult help_result() {
    ParseResult result;
    result.ok = true;
    result.help = true;
    result.exit_code = 0;
    result.message = bench_usage();
    return result;
}

ParseResult error_result(const std::string& message) {
    ParseResult result;
    result.ok = false;
    result.help = false;
    result.exit_code = 2;
    result.message = message + "\n\n" + bench_usage();
    return result;
}

bool read_option_value(int argc, char** argv, int index, std::string* value, ParseResult* error) {
    const std::string name = argv[index];
    if (index + 1 >= argc || is_option_token(argv[index + 1])) {
        *error = error_result(name + " requires a value");
        return false;
    }
    *value = argv[index + 1];
    return true;
}

bool parse_positive_size(const std::string& text, std::size_t* out) {
    if (text.empty() || text[0] == '-') {
        return false;
    }

    char* end = nullptr;
    errno = 0;
    unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || end == text.c_str() || *end != '\0' || value == 0 ||
        value > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    *out = static_cast<std::size_t>(value);
    return true;
}

}  // namespace

std::string bench_usage() {
    return "Usage:\n"
           "  mm_bench [--mode shm|tcp] [--count N] [--payload-bytes N] [--topic NAME]\n"
           "  mm_bench --help\n";
}

const char* mode_name(BenchMode mode) {
    switch (mode) {
        case BenchMode::SHM:
            return "shm";
        case BenchMode::TCP:
            return "tcp";
    }
    return "unknown";
}

ParseResult parse_bench_args(int argc, char** argv) {
    ParseResult result;
    result.ok = true;
    result.exit_code = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            return help_result();
        }
        if (arg == "--mode") {
            std::string value;
            if (!read_option_value(argc, argv, i, &value, &result)) {
                return result;
            }
            if (value == "shm") {
                result.options.mode = BenchMode::SHM;
            } else if (value == "tcp") {
                result.options.mode = BenchMode::TCP;
            } else {
                return error_result("unsupported mode: " + value);
            }
            ++i;
            continue;
        }
        if (arg == "--count") {
            std::string value;
            if (!read_option_value(argc, argv, i, &value, &result)) {
                return result;
            }
            if (!parse_positive_size(value, &result.options.count)) {
                return error_result("--count must be positive");
            }
            ++i;
            continue;
        }
        if (arg == "--payload-bytes") {
            std::string value;
            if (!read_option_value(argc, argv, i, &value, &result)) {
                return result;
            }
            if (!parse_positive_size(value, &result.options.payload_bytes)) {
                return error_result("--payload-bytes must be a positive integer");
            }
            ++i;
            continue;
        }
        if (arg == "--topic") {
            std::string value;
            if (!read_option_value(argc, argv, i, &value, &result)) {
                return result;
            }
            result.options.topic = value;
            ++i;
            continue;
        }
        if (is_option_token(arg)) {
            return error_result("unknown option: " + arg);
        }
        return error_result("unexpected argument: " + arg);
    }

    return result;
}

}  // namespace mm::bench
