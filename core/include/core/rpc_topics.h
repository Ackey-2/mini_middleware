#pragma once

#include <cctype>
#include <string>

namespace mm {

inline std::string rpc_sanitize_segment(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) out.push_back(static_cast<char>(ch));
        else out.push_back('_');
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "default" : out;
}

inline std::string rpc_topic_segment(const std::string& service_name) {
    return rpc_sanitize_segment(service_name);
}

inline std::string rpc_client_segment(const std::string& client_id) {
    return rpc_sanitize_segment(client_id);
}

inline std::string rpc_request_topic(const std::string& service_name) {
    return "/_mm/rpc/" + rpc_topic_segment(service_name) + "/request";
}

inline std::string rpc_reply_topic(const std::string& service_name,
                                   const std::string& client_id) {
    return "/_mm/rpc/" + rpc_topic_segment(service_name) + "/reply/" +
           rpc_client_segment(client_id);
}

}  // namespace mm
