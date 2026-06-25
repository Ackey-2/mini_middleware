#include "cli/topic_commands.h"

#include "cli/message_format.h"
#include "core/node.h"
#include "discovery/discovery_agent.h"
#include "messages.pb.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace mm {
namespace {

const char* kind_name(EndpointInfo::Kind kind) {
    switch (kind) {
    case EndpointInfo::PUBLISHER:
        return "PUBLISHER";
    case EndpointInfo::SUBSCRIBER:
        return "SUBSCRIBER";
    case EndpointInfo::SERVICE:
        return "SERVICE";
    case EndpointInfo::CLIENT:
        return "CLIENT";
    default:
        return "UNKNOWN";
    }
}

std::string locator_text(const Locator& locator) {
    std::ostringstream out;
    out << locator.ip() << ':' << locator.port();
    return out.str();
}

void print_supported_types(std::ostream& out) {
    out << "supported types:\n";
    for (const auto& type : supported_message_types()) {
        out << "  " << type << '\n';
    }
}

template <typename MessageT>
std::string serialize_message(const MessageT& msg) {
    std::string bytes;
    msg.SerializeToString(&bytes);
    return bytes;
}

template <typename MessageT>
int run_echo_for(Node& node, const std::string& topic, const std::string& type_name,
                 int count, std::ostream& out) {
    std::mutex mutex;
    std::condition_variable cv;
    int received = 0;

    auto sub = node.create_subscriber<MessageT>(
        topic,
        [&](const MessageT& msg) {
            auto formatted = format_message(type_name, serialize_message(msg));
            std::lock_guard<std::mutex> lock(mutex);
            if (formatted.ok) {
                out << formatted.text << '\n';
            } else {
                out << formatted.error << '\n';
            }
            out << "---\n";
            ++received;
            cv.notify_one();
        });

    if (count <= 0) {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return received >= count; });
    return 0;
}

template <typename MessageT>
int run_hz_for(Node& node, const std::string& topic, int window, int count,
               std::ostream& out) {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::chrono::steady_clock::time_point> timestamps;

    auto sub = node.create_subscriber<MessageT>(
        topic,
        [&](const MessageT&) {
            std::lock_guard<std::mutex> lock(mutex);
            timestamps.push_back(std::chrono::steady_clock::now());
            cv.notify_one();
        });

    if (count <= 0) {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [&] { return static_cast<int>(timestamps.size()) >= count; });

    const int bounded_window = window > 1 ? window : 2;
    const int sample_count = std::min<int>(bounded_window, timestamps.size());
    double rate = 0.0;
    if (sample_count >= 2) {
        const auto first = timestamps[timestamps.size() - sample_count];
        const auto last = timestamps.back();
        const auto elapsed = std::chrono::duration<double>(last - first).count();
        if (elapsed > 0.0) {
            rate = static_cast<double>(sample_count - 1) / elapsed;
        }
    }

    out << "average rate: " << std::fixed << std::setprecision(2) << rate << " Hz\n";
    return 0;
}

int unsupported_type(std::ostream& out) {
    print_supported_types(out);
    return 2;
}

}  // namespace

int run_topic_list(DiscoveryAgent& discovery, std::ostream& out) {
    out << "KIND\tTOPIC\tTYPE\tNODE\tLOCATOR\n";
    for (const auto& endpoint : discovery.snapshot_endpoints()) {
        out << kind_name(endpoint.endpoint.kind()) << '\t'
            << endpoint.endpoint.topic() << '\t'
            << endpoint.endpoint.type_name() << '\t'
            << endpoint.node_name << '\t'
            << locator_text(endpoint.locator) << '\n';
    }
    return 0;
}

int run_topic_echo(Node& node, const std::string& topic, const std::string& type_name,
                   int count, std::ostream& out) {
    if (type_name == "mm.StringMsg") {
        return run_echo_for<mm::StringMsg>(node, topic, type_name, count, out);
    }
    if (type_name == "mm.Point3D") {
        return run_echo_for<mm::Point3D>(node, topic, type_name, count, out);
    }
    if (type_name == "mm.PointCloud") {
        return run_echo_for<mm::PointCloud>(node, topic, type_name, count, out);
    }

    return unsupported_type(out);
}

int run_topic_hz(Node& node, const std::string& topic, const std::string& type_name,
                 int window, int count, std::ostream& out) {
    if (type_name == "mm.StringMsg") {
        return run_hz_for<mm::StringMsg>(node, topic, window, count, out);
    }
    if (type_name == "mm.Point3D") {
        return run_hz_for<mm::Point3D>(node, topic, window, count, out);
    }
    if (type_name == "mm.PointCloud") {
        return run_hz_for<mm::PointCloud>(node, topic, window, count, out);
    }

    return unsupported_type(out);
}

}  // namespace mm
