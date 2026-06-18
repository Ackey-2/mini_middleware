#pragma once

#include "discovery.pb.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mm {

// 一对匹配:本地端点 ↔ 远端端点,附带远端身份与 locator(给 Phase 3 连 TCP)
struct MatchInfo {
    EndpointInfo local;
    EndpointInfo remote;
    Locator remote_locator;
    uint64_t remote_participant_id = 0;
    std::string remote_host_id;   // Phase 4:远端机器标识,数据面据此判定同机/跨机
};

// 本地端点 × 远端端点,返回所有匹配对。
// 匹配规则:一方是 PUBLISHER 另一方是 SUBSCRIBER(kind 不同),
//          且 topic 与 type_name 都相等。
std::vector<MatchInfo> match_endpoints(
    const std::vector<EndpointInfo>& local_endpoints,
    uint64_t remote_id,
    const Locator& remote_locator,
    const std::vector<EndpointInfo>& remote_endpoints);

}  // namespace mm
