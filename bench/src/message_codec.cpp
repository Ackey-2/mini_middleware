#include "bench/message_codec.h"

#include "data.pb.h"
#include "messages.pb.h"

#include <charconv>
#include <limits>
#include <utility>

namespace mm::bench {
namespace {

template <typename UInt>
bool parse_unsigned(const std::string& text, UInt* value) {
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), *value);
    return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
}

}  // namespace

std::string make_benchmark_payload(const std::string& run_id,
                                   std::size_t sequence,
                                   std::uint64_t send_ns,
                                   std::size_t payload_bytes) {
    std::string payload = run_id + "|" + std::to_string(sequence) + "|" +
                          std::to_string(send_ns) + "|";
    if (payload.size() < payload_bytes) {
        payload.append(payload_bytes - payload.size(), 'x');
    }
    return payload;
}

std::optional<BenchmarkMessageMetadata> parse_benchmark_payload(
    const std::string& payload) {
    const auto first = payload.find('|');
    const auto second = first == std::string::npos
                            ? std::string::npos
                            : payload.find('|', first + 1);
    const auto third = second == std::string::npos
                           ? std::string::npos
                           : payload.find('|', second + 1);
    if (first == std::string::npos || first == 0 || second == std::string::npos ||
        third == std::string::npos) {
        return std::nullopt;
    }

    BenchmarkMessageMetadata metadata;
    metadata.run_id = payload.substr(0, first);
    const auto sequence_text = payload.substr(first + 1, second - first - 1);
    const auto timestamp_text = payload.substr(second + 1, third - second - 1);
    if (!parse_unsigned(sequence_text, &metadata.sequence) ||
        !parse_unsigned(timestamp_text, &metadata.send_ns)) {
        return std::nullopt;
    }
    return metadata;
}

std::size_t minimum_benchmark_payload_bytes(const std::string& run_id,
                                            std::size_t count) {
    const auto max_sequence = count == 0 ? 0 : count - 1;
    return make_benchmark_payload(run_id, max_sequence,
                                  std::numeric_limits<std::uint64_t>::max(), 0)
        .size();
}

std::size_t serialized_string_message_size(std::size_t payload_bytes) {
    mm::StringMsg message;
    message.mutable_data()->resize(payload_bytes);
    return message.ByteSizeLong();
}

std::size_t tcp_frame_payload_size(const std::string& topic,
                                   std::size_t payload_bytes) {
    mm::StringMsg message;
    message.mutable_data()->resize(payload_bytes);
    std::string serialized_message;
    if (!message.SerializeToString(&serialized_message)) {
        return std::numeric_limits<std::size_t>::max();
    }

    mm::DataMessage envelope;
    envelope.set_topic(topic);
    envelope.set_payload(std::move(serialized_message));
    return envelope.ByteSizeLong();
}

BenchmarkSampleCollector::BenchmarkSampleCollector(std::string run_id,
                                                   std::size_t expected_count)
    : run_id_(std::move(run_id)),
      samples_by_sequence_(expected_count),
      seen_(expected_count) {}

bool BenchmarkSampleCollector::record(const std::string& payload,
                                      std::uint64_t receive_ns) {
    const auto metadata = parse_benchmark_payload(payload);
    if (!metadata || metadata->run_id != run_id_ ||
        metadata->sequence >= seen_.size() || seen_[metadata->sequence]) {
        return false;
    }

    samples_by_sequence_[metadata->sequence] =
        receive_ns >= metadata->send_ns ? (receive_ns - metadata->send_ns) / 1000 : 0;
    seen_[metadata->sequence] = true;
    ++received_count_;
    return true;
}

std::vector<std::uint64_t> BenchmarkSampleCollector::samples() const {
    std::vector<std::uint64_t> result;
    result.reserve(received_count_);
    for (std::size_t i = 0; i < seen_.size(); ++i) {
        if (seen_[i]) result.push_back(samples_by_sequence_[i]);
    }
    return result;
}

}  // namespace mm::bench
