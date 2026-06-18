#include "core/data_plane.h"
#include "core/local_bus.h"
#include "common/qos.h"
#include "discovery/endpoint_matcher.h"
#include "discovery.pb.h"

#include <gtest/gtest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string>

using namespace mm;

namespace {
// /dev/shm/<name without leading '/'> 是否存在。
bool shm_exists(const std::string& seg_name) {
    std::string path = "/dev/shm/" + seg_name.substr(1);   // 去掉前导 '/'
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0;
}

// 段名与 data_plane.cpp 的 seg_name() 同构:topic 里 '/' '.' ' ' → '_'。
std::string seg_name(uint64_t pid, const std::string& topic) {
    std::string s = "/mm." + std::to_string(pid) + ".";
    for (char c : topic) s += (c == '/' || c == '.' || c == ' ') ? '_' : c;
    return s;
}

// 本地 PUBLISHER ↔ 远端 SUBSCRIBER(reader),设定 reader 可靠性 + 同 host。
MatchInfo pub_match(const std::string& topic, uint64_t remote_pid,
                    Qos::Reliability reader_rel, const std::string& host) {
    MatchInfo m;
    m.local.set_kind(EndpointInfo::PUBLISHER);
    m.local.set_topic(topic);
    m.local.set_type_name("mm.StringMsg");
    m.remote.set_kind(EndpointInfo::SUBSCRIBER);
    m.remote.set_topic(topic);
    m.remote.set_type_name("mm.StringMsg");
    m.remote.set_reliability(static_cast<uint32_t>(reader_rel));
    m.remote_participant_id = remote_pid;
    m.remote_locator.set_ip("127.0.0.1");
    m.remote_locator.set_port(1);
    m.remote_host_id = host;
    return m;
}
}  // namespace

// 同机 + 订阅者 BEST_EFFORT → 建 SHM 段。
TEST(QosRouting, BestEffortSameHostUsesShm) {
    const std::string host = "qos-route-host";
    const uint64_t pid = 70001;
    const std::string topic = "/qos_be";

    auto bus = std::make_shared<LocalBus>();
    DataPlane dp(bus, "127.0.0.1");
    dp.set_local_identity(pid, host, /*enable_shm=*/true);
    ASSERT_TRUE(dp.start());

    dp.handle_match(pub_match(topic, 222, Qos::Reliability::BEST_EFFORT, host));
    EXPECT_TRUE(shm_exists(seg_name(pid, topic)));   // 走了 SHM

    dp.stop();
    EXPECT_FALSE(shm_exists(seg_name(pid, topic)));  // unlink 干净
}

// 同机 + 订阅者 RELIABLE → 不建 SHM 段(退回 TCP)。
TEST(QosRouting, ReliableSameHostSkipsShm) {
    const std::string host = "qos-route-host";
    const uint64_t pid = 70002;
    const std::string topic = "/qos_rel";

    auto bus = std::make_shared<LocalBus>();
    DataPlane dp(bus, "127.0.0.1");
    dp.set_local_identity(pid, host, /*enable_shm=*/true);
    ASSERT_TRUE(dp.start());

    dp.handle_match(pub_match(topic, 333, Qos::Reliability::RELIABLE, host));
    EXPECT_FALSE(shm_exists(seg_name(pid, topic)));  // 没有走 SHM(走 TCP)

    dp.stop();
}
