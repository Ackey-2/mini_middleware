#include "discovery/endpoint_matcher.h"
#include "common/qos.h"
#include "common/logger.h"

namespace mm {

namespace {
Qos::Reliability rel_of(const EndpointInfo& e) {
    return e.reliability() == 0 ? Qos::Reliability::BEST_EFFORT
                                : Qos::Reliability::RELIABLE;
}
}  // namespace

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
                // QoS 协商(RxO):PUB=writer(offered),SUB=reader(requested)。
                const EndpointInfo& writer =
                    (le.kind() == EndpointInfo::PUBLISHER) ? le : re;
                const EndpointInfo& reader =
                    (le.kind() == EndpointInfo::SUBSCRIBER) ? le : re;
                if (!Qos::compatible(rel_of(writer), rel_of(reader))) {
                    LOG_WARN("qos incompatible on topic {}: reader RELIABLE but writer BEST_EFFORT, no match",
                             le.topic());
                    continue;   // 不兼容 → 不产出匹配
                }
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
