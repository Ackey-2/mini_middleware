#include "discovery/endpoint_matcher.h"

namespace mm {

std::vector<MatchInfo> match_endpoints(
    const std::vector<EndpointInfo>& local_endpoints,
    uint64_t remote_id,
    const Locator& remote_locator,
    const std::vector<EndpointInfo>& remote_endpoints) {
    std::vector<MatchInfo> out;
    for (const auto& le : local_endpoints) {
        for (const auto& re : remote_endpoints) {
            if (le.kind() != re.kind() &&
                le.topic() == re.topic() &&
                le.type_name() == re.type_name()) {
                MatchInfo m;
                m.local = le;
                m.remote = re;
                m.remote_locator = remote_locator;
                m.remote_participant_id = remote_id;
                out.push_back(std::move(m));
            }
        }
    }
    return out;
}

}  // namespace mm
