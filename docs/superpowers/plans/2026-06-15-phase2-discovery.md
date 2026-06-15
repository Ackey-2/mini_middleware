# Phase 2: UDP Multicast Discovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Independent processes' `Node`s automatically discover each other's publish/subscribe endpoints over UDP multicast and report matches.

**Architecture:** Each `Node` runs a `DiscoveryAgent` with one background thread that periodically multicasts a `ParticipantAnnouncement` (participant id + node name + locator + all local endpoints) and receives others'. A pure `match_endpoints()` function pairs local PUB↔remote SUB (and vice versa) on equal topic+type. The agent dedups matches over time, fires `on_match`/`on_unmatch`, and reaps participants that go silent.

**Tech Stack:** C++17, Protobuf, UDP multicast sockets, CMake, GoogleTest, `std::thread`.

**Spec:** `docs/superpowers/specs/2026-06-15-phase2-discovery-design.md`

**Build & test commands (CMake 3.16 — `--test-dir` is NOT available, must `cd build`):**
- Reconfigure (after any `CMakeLists.txt` change, esp. new files/subdirs): `cmake -S . -B build`
- Build one target: `cmake --build build -j --target <name>`
- Run one test: `cd build && ctest -R <name> --output-on-failure` (returns to repo root next command since cwd persists — always prefix build-from-root commands accordingly)
- Build everything: `cmake --build build -j`

> **C++ TDD note:** "RED" is usually a compile/link error (the symbol doesn't exist yet). That counts as the failing test. Each task writes the test first, builds it to see it fail, then implements.
>
> **WSL2 multicast** is verified working on this host (see spec §0). Required socket options are in Task 2.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `proto/discovery.proto` | Discovery wire messages: `Locator`, `EndpointInfo`, `ParticipantAnnouncement` |
| `discovery/include/discovery/udp_multicast.h` + `src/udp_multicast.cpp` | Multicast socket: open/close, send, recv-with-timeout |
| `discovery/include/discovery/endpoint_matcher.h` + `src/endpoint_matcher.cpp` | `MatchInfo` struct + pure `match_endpoints()` |
| `discovery/include/discovery/discovery_agent.h` + `src/discovery_agent.cpp` | Background thread: announce + receive + match dedup + liveliness reap |
| `discovery/CMakeLists.txt` | `mm_discovery` static library |
| `core/include/core/node.h` + `src/node.cpp` | Own a `DiscoveryAgent`; register endpoints on create_publisher/subscriber |
| `tests/test_discovery_proto.cpp` | Proto round-trip |
| `tests/test_udp_multicast.cpp` | Send/recv loopback + recv timeout |
| `tests/test_endpoint_matcher.cpp` | Matching rules |
| `tests/test_discovery_agent.cpp` | Two in-process agents match / unmatch / no-self-match |
| `tests/test_node_discovery.cpp` | Two `Node`s discover each other |
| `examples/discovery_demo.cpp` | Runnable pub/sub discovery demo |

---

## Task 1: discovery.proto + proto wiring

**Files:**
- Create: `proto/discovery.proto`
- Modify: `proto/CMakeLists.txt`
- Create: `tests/test_discovery_proto.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_discovery_proto.cpp`:

```cpp
#include "discovery.pb.h"
#include <gtest/gtest.h>

using namespace mm;

TEST(DiscoveryProto, RoundTrip) {
    ParticipantAnnouncement ann;
    ann.set_participant_id(42);
    ann.set_node_name("n1");
    ann.mutable_data_locator()->set_ip("127.0.0.1");
    ann.mutable_data_locator()->set_port(7000);
    auto* ep = ann.add_endpoints();
    ep->set_kind(EndpointInfo::PUBLISHER);
    ep->set_topic("/scan");
    ep->set_type_name("mm.PointCloud");

    std::string bytes;
    ASSERT_TRUE(ann.SerializeToString(&bytes));

    ParticipantAnnouncement got;
    ASSERT_TRUE(got.ParseFromString(bytes));
    EXPECT_EQ(got.participant_id(), 42u);
    EXPECT_EQ(got.node_name(), "n1");
    EXPECT_EQ(got.data_locator().port(), 7000u);
    ASSERT_EQ(got.endpoints_size(), 1);
    EXPECT_EQ(got.endpoints(0).topic(), "/scan");
    EXPECT_EQ(got.endpoints(0).kind(), EndpointInfo::PUBLISHER);
}
```

- [ ] **Step 2: Register the test and confirm it fails to build**

Modify `tests/CMakeLists.txt` — add inside `if(GTest_FOUND)` (after the `test_node` block):

```cmake
    add_executable(test_discovery_proto test_discovery_proto.cpp)
    target_link_libraries(test_discovery_proto PRIVATE
        mm_proto
        GTest::gtest_main
    )
    add_test(NAME test_discovery_proto COMMAND test_discovery_proto)
```

Run: `cmake -S . -B build && cmake --build build -j --target test_discovery_proto`
Expected: FAIL — `fatal error: discovery.pb.h: No such file or directory`.

- [ ] **Step 3: Write the proto**

Create `proto/discovery.proto`:

```proto
syntax = "proto3";

package mm;

// 给 Phase 3 TCP 用的监听地址
message Locator {
    string ip = 1;
    uint32 port = 2;
}

// 一个端点:某进程在某 topic 上发布或订阅
message EndpointInfo {
    enum Kind {
        PUBLISHER = 0;
        SUBSCRIBER = 1;
    }
    Kind kind = 1;
    string topic = 2;
    string type_name = 3;
}

// 周期性多播公告:合并了 DDS 的 SPDP(参与者) + SEDP(端点)
message ParticipantAnnouncement {
    uint64 participant_id = 1;   // 随机生成,用于识别和过滤自身
    string node_name = 2;
    Locator data_locator = 3;    // Phase 3 填真实端口;Phase 2 填占位值
    repeated EndpointInfo endpoints = 4;
}
```

- [ ] **Step 4: Wire the proto into the build**

Modify `proto/CMakeLists.txt` — change the `PROTO_FILES` list to include both protos:

```cmake
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/messages.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery.proto
)
```

- [ ] **Step 5: Build and run — expect PASS**

Run: `cmake -S . -B build && cmake --build build -j --target test_discovery_proto && (cd build && ctest -R test_discovery_proto --output-on-failure)`
Expected: PASS — 1 test.

- [ ] **Step 6: Commit**

```bash
git add proto/discovery.proto proto/CMakeLists.txt tests/test_discovery_proto.cpp tests/CMakeLists.txt
git commit -m "feat(proto): add discovery wire messages"
```

---

## Task 2: UdpMulticast socket wrapper

**Files:**
- Create: `discovery/include/discovery/udp_multicast.h`
- Create: `discovery/src/udp_multicast.cpp`
- Create: `discovery/CMakeLists.txt`
- Modify: `CMakeLists.txt` (top-level: add_subdirectory)
- Create: `tests/test_udp_multicast.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_udp_multicast.cpp`:

```cpp
#include "discovery/udp_multicast.h"
#include <gtest/gtest.h>
#include <chrono>
#include <string>

using namespace mm;
using namespace std::chrono_literals;

TEST(UdpMulticast, SendReceiveLoopback) {
    UdpMulticast a("239.255.0.9", 7411);
    UdpMulticast b("239.255.0.9", 7411);
    ASSERT_TRUE(a.open());
    ASSERT_TRUE(b.open());

    ASSERT_TRUE(a.send("ping"));

    std::string got;
    ASSERT_TRUE(b.recv(got, 1000ms));
    EXPECT_EQ(got, "ping");
}

TEST(UdpMulticast, RecvTimeoutReturnsFalse) {
    UdpMulticast a("239.255.0.10", 7412);
    ASSERT_TRUE(a.open());
    std::string got;
    EXPECT_FALSE(a.recv(got, 100ms));
}
```

- [ ] **Step 2: Create the discovery module CMake and register everything**

Create `discovery/CMakeLists.txt`:

```cmake
# discovery 模块:UDP 多播服务发现
add_library(mm_discovery STATIC
    src/udp_multicast.cpp
)

target_include_directories(mm_discovery PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mm_discovery PUBLIC
    mm_proto
    mm_common
    Threads::Threads
)
```

Modify top-level `CMakeLists.txt` — add `add_subdirectory(discovery)` between transport and core:

```cmake
add_subdirectory(transport)            # 传输层
add_subdirectory(discovery)            # 发现层
add_subdirectory(core)                 # Pub/Sub/Node
```

Modify `tests/CMakeLists.txt` — add inside `if(GTest_FOUND)`:

```cmake
    add_executable(test_udp_multicast test_udp_multicast.cpp)
    target_link_libraries(test_udp_multicast PRIVATE
        mm_discovery
        GTest::gtest_main
    )
    add_test(NAME test_udp_multicast COMMAND test_udp_multicast)
```

- [ ] **Step 3: Confirm it fails to build**

Run: `cmake -S . -B build && cmake --build build -j --target test_udp_multicast`
Expected: FAIL — `fatal error: discovery/udp_multicast.h: No such file or directory`.

- [ ] **Step 4: Write the header**

Create `discovery/include/discovery/udp_multicast.h`:

```cpp
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// UdpMulticast:UDP 多播 socket 的薄封装。
//   - open():建收/发两个 socket,接收端 join 多播组并 bind 端口
//   - send():把字节多播到组(本机回环也能收到)
//   - recv():阻塞收一个数据报,最多等 timeout
// 每条公告是一个独立数据报,UDP 自带边界,无需分帧。
// ═══════════════════════════════════════════════════════════════
class UdpMulticast {
public:
    UdpMulticast(std::string group, uint16_t port);
    ~UdpMulticast();

    UdpMulticast(const UdpMulticast&) = delete;
    UdpMulticast& operator=(const UdpMulticast&) = delete;

    bool open();
    void close();

    bool send(const std::string& bytes);
    // 收到返回 true 并填 out;超时或错误返回 false
    bool recv(std::string& out, std::chrono::milliseconds timeout);

private:
    std::string group_;
    uint16_t port_;
    int send_fd_ = -1;
    int recv_fd_ = -1;
};

}  // namespace mm
```

- [ ] **Step 5: Write the implementation**

Create `discovery/src/udp_multicast.cpp`:

```cpp
#include "discovery/udp_multicast.h"
#include "common/logger.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace mm {

UdpMulticast::UdpMulticast(std::string group, uint16_t port)
    : group_(std::move(group)), port_(port) {}

UdpMulticast::~UdpMulticast() { close(); }

bool UdpMulticast::open() {
    // ── 接收 socket:bind 到 INADDR_ANY:port,再 join 多播组 ──
    recv_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_fd_ < 0) {
        LOG_ERROR("udp recv socket failed: {}", strerror(errno));
        return false;
    }
    int one = 1;
    setsockopt(recv_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(recv_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("udp bind {} failed: {}", port_, strerror(errno));
        close();
        return false;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(group_.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(recv_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        LOG_ERROR("udp join {} failed: {}", group_, strerror(errno));
        close();
        return false;
    }

    // ── 发送 socket:开回环,使本机其它 socket(含自己)也能收到 ──
    send_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (send_fd_ < 0) {
        LOG_ERROR("udp send socket failed: {}", strerror(errno));
        close();
        return false;
    }
    int loop = 1;
    setsockopt(send_fd_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    return true;
}

void UdpMulticast::close() {
    if (recv_fd_ >= 0) { ::close(recv_fd_); recv_fd_ = -1; }
    if (send_fd_ >= 0) { ::close(send_fd_); send_fd_ = -1; }
}

bool UdpMulticast::send(const std::string& bytes) {
    if (send_fd_ < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = inet_addr(group_.c_str());
    ssize_t n = ::sendto(send_fd_, bytes.data(), bytes.size(), 0,
                         (sockaddr*)&addr, sizeof(addr));
    return n == static_cast<ssize_t>(bytes.size());
}

bool UdpMulticast::recv(std::string& out, std::chrono::milliseconds timeout) {
    if (recv_fd_ < 0) return false;
    timeval tv{};
    tv.tv_sec = timeout.count() / 1000;
    tv.tv_usec = (timeout.count() % 1000) * 1000;
    setsockopt(recv_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buf[65536];
    ssize_t n = ::recv(recv_fd_, buf, sizeof(buf), 0);
    if (n < 0) return false;          // 超时(EAGAIN)或错误
    out.assign(buf, n);
    return true;
}

}  // namespace mm
```

- [ ] **Step 6: Build and run — expect PASS**

Run: `cmake -S . -B build && cmake --build build -j --target test_udp_multicast && (cd build && ctest -R test_udp_multicast --output-on-failure)`
Expected: PASS — 2 tests.

- [ ] **Step 7: Commit**

```bash
git add discovery/include/discovery/udp_multicast.h discovery/src/udp_multicast.cpp discovery/CMakeLists.txt CMakeLists.txt tests/test_udp_multicast.cpp tests/CMakeLists.txt
git commit -m "feat(discovery): add UdpMulticast socket wrapper"
```

---

## Task 3: endpoint_matcher (pure matching logic)

**Files:**
- Create: `discovery/include/discovery/endpoint_matcher.h`
- Create: `discovery/src/endpoint_matcher.cpp`
- Modify: `discovery/CMakeLists.txt`
- Create: `tests/test_endpoint_matcher.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_endpoint_matcher.cpp`:

```cpp
#include "discovery/endpoint_matcher.h"
#include <gtest/gtest.h>
#include <vector>

using namespace mm;

static EndpointInfo make_ep(EndpointInfo::Kind k, std::string topic, std::string type) {
    EndpointInfo e;
    e.set_kind(k);
    e.set_topic(std::move(topic));
    e.set_type_name(std::move(type));
    return e;
}

TEST(EndpointMatcher, PubMatchesRemoteSubSameTopicType) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::SUBSCRIBER, "/scan", "mm.PointCloud")};
    Locator loc;
    loc.set_ip("127.0.0.1");
    loc.set_port(7000);

    auto m = match_endpoints(local, 99, loc, remote);
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(m[0].local.kind(), EndpointInfo::PUBLISHER);
    EXPECT_EQ(m[0].remote.kind(), EndpointInfo::SUBSCRIBER);
    EXPECT_EQ(m[0].remote_participant_id, 99u);
    EXPECT_EQ(m[0].remote_locator.port(), 7000u);
}

TEST(EndpointMatcher, NoMatchDifferentTopic) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::SUBSCRIBER, "/odom", "mm.PointCloud")};
    Locator loc;
    EXPECT_TRUE(match_endpoints(local, 1, loc, remote).empty());
}

TEST(EndpointMatcher, NoMatchDifferentType) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::SUBSCRIBER, "/scan", "mm.StringMsg")};
    Locator loc;
    EXPECT_TRUE(match_endpoints(local, 1, loc, remote).empty());
}

TEST(EndpointMatcher, NoMatchSameKind) {
    std::vector<EndpointInfo> local{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    std::vector<EndpointInfo> remote{make_ep(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud")};
    Locator loc;
    EXPECT_TRUE(match_endpoints(local, 1, loc, remote).empty());
}
```

- [ ] **Step 2: Register the test and confirm it fails to build**

Modify `tests/CMakeLists.txt` — add inside `if(GTest_FOUND)`:

```cmake
    add_executable(test_endpoint_matcher test_endpoint_matcher.cpp)
    target_link_libraries(test_endpoint_matcher PRIVATE
        mm_discovery
        GTest::gtest_main
    )
    add_test(NAME test_endpoint_matcher COMMAND test_endpoint_matcher)
```

Run: `cmake -S . -B build && cmake --build build -j --target test_endpoint_matcher`
Expected: FAIL — `fatal error: discovery/endpoint_matcher.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `discovery/include/discovery/endpoint_matcher.h`:

```cpp
#pragma once

#include "discovery.pb.h"

#include <cstdint>
#include <vector>

namespace mm {

// 一对匹配:本地端点 ↔ 远端端点,附带远端身份与 locator(给 Phase 3 连 TCP)
struct MatchInfo {
    EndpointInfo local;
    EndpointInfo remote;
    Locator remote_locator;
    uint64_t remote_participant_id = 0;
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
```

- [ ] **Step 4: Write the implementation**

Create `discovery/src/endpoint_matcher.cpp`:

```cpp
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
```

- [ ] **Step 5: Add endpoint_matcher.cpp to the library**

Modify `discovery/CMakeLists.txt` — update the `add_library` sources:

```cmake
add_library(mm_discovery STATIC
    src/udp_multicast.cpp
    src/endpoint_matcher.cpp
)
```

- [ ] **Step 6: Build and run — expect PASS**

Run: `cmake -S . -B build && cmake --build build -j --target test_endpoint_matcher && (cd build && ctest -R test_endpoint_matcher --output-on-failure)`
Expected: PASS — 4 tests.

- [ ] **Step 7: Commit**

```bash
git add discovery/include/discovery/endpoint_matcher.h discovery/src/endpoint_matcher.cpp discovery/CMakeLists.txt tests/test_endpoint_matcher.cpp tests/CMakeLists.txt
git commit -m "feat(discovery): add pure endpoint matching logic"
```

---

## Task 4: DiscoveryAgent (announce + receive + match + reap)

**Files:**
- Create: `discovery/include/discovery/discovery_agent.h`
- Create: `discovery/src/discovery_agent.cpp`
- Modify: `discovery/CMakeLists.txt`
- Create: `tests/test_discovery_agent.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_discovery_agent.cpp`:

```cpp
#include "discovery/discovery_agent.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

static Locator loc(const std::string& ip, uint16_t port) {
    Locator l;
    l.set_ip(ip);
    l.set_port(port);
    return l;
}

TEST(DiscoveryAgent, TwoAgentsMatch) {
    const std::string group = "239.255.1.20";
    const uint16_t port = 7431;

    DiscoveryAgent a("nodeA", loc("127.0.0.1", 5001), group, port);
    DiscoveryAgent b("nodeB", loc("127.0.0.1", 5002), group, port);
    a.set_timing(50ms, 5000ms);
    b.set_timing(50ms, 5000ms);

    std::atomic<int> a_matches{0}, b_matches{0};
    MatchInfo a_seen;
    a.on_match([&](const MatchInfo& m) { a_seen = m; ++a_matches; });
    b.on_match([&](const MatchInfo&) { ++b_matches; });

    a.add_endpoint(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud");
    b.add_endpoint(EndpointInfo::SUBSCRIBER, "/scan", "mm.PointCloud");

    ASSERT_TRUE(a.start());
    ASSERT_TRUE(b.start());

    for (int i = 0; i < 200 && (a_matches.load() == 0 || b_matches.load() == 0); ++i)
        std::this_thread::sleep_for(25ms);

    EXPECT_GE(a_matches.load(), 1);
    EXPECT_GE(b_matches.load(), 1);
    EXPECT_EQ(a_seen.remote.topic(), "/scan");
    EXPECT_EQ(a_seen.remote.kind(), EndpointInfo::SUBSCRIBER);
    EXPECT_EQ(a_seen.remote_locator.port(), 5002u);
    EXPECT_EQ(a_seen.remote_participant_id, b.participant_id());

    a.stop();
    b.stop();
}

TEST(DiscoveryAgent, UnmatchOnTimeout) {
    const std::string group = "239.255.1.21";
    const uint16_t port = 7432;

    auto a = std::make_unique<DiscoveryAgent>("nodeA", loc("127.0.0.1", 5001), group, port);
    DiscoveryAgent b("nodeB", loc("127.0.0.1", 5002), group, port);
    a->set_timing(50ms, 300ms);
    b.set_timing(50ms, 300ms);

    std::atomic<int> b_match{0}, b_unmatch{0};
    b.on_match([&](const MatchInfo&) { ++b_match; });
    b.on_unmatch([&](const MatchInfo&) { ++b_unmatch; });

    a->add_endpoint(EndpointInfo::PUBLISHER, "/scan", "mm.PointCloud");
    b.add_endpoint(EndpointInfo::SUBSCRIBER, "/scan", "mm.PointCloud");

    ASSERT_TRUE(a->start());
    ASSERT_TRUE(b.start());

    for (int i = 0; i < 100 && b_match.load() == 0; ++i)
        std::this_thread::sleep_for(20ms);
    ASSERT_GE(b_match.load(), 1);

    a->stop();
    a.reset();   // A 下线,不再公告

    for (int i = 0; i < 100 && b_unmatch.load() == 0; ++i)
        std::this_thread::sleep_for(20ms);
    EXPECT_GE(b_unmatch.load(), 1);

    b.stop();
}

TEST(DiscoveryAgent, DoesNotSelfMatch) {
    DiscoveryAgent a("solo", loc("127.0.0.1", 5001), "239.255.1.22", 7433);
    a.set_timing(50ms, 5000ms);

    std::atomic<int> matches{0};
    a.on_match([&](const MatchInfo&) { ++matches; });

    a.add_endpoint(EndpointInfo::PUBLISHER, "/x", "mm.StringMsg");
    a.add_endpoint(EndpointInfo::SUBSCRIBER, "/x", "mm.StringMsg");

    ASSERT_TRUE(a.start());
    std::this_thread::sleep_for(400ms);   // 收到多次自己的公告
    EXPECT_EQ(matches.load(), 0);

    a.stop();
}
```

- [ ] **Step 2: Register the test and confirm it fails to build**

Modify `tests/CMakeLists.txt` — add inside `if(GTest_FOUND)`:

```cmake
    add_executable(test_discovery_agent test_discovery_agent.cpp)
    target_link_libraries(test_discovery_agent PRIVATE
        mm_discovery
        GTest::gtest_main
    )
    add_test(NAME test_discovery_agent COMMAND test_discovery_agent)
```

Run: `cmake -S . -B build && cmake --build build -j --target test_discovery_agent`
Expected: FAIL — `fatal error: discovery/discovery_agent.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `discovery/include/discovery/discovery_agent.h`:

```cpp
#pragma once

#include "discovery/udp_multicast.h"
#include "discovery/endpoint_matcher.h"
#include "discovery.pb.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// DiscoveryAgent:一个进程的发现代理。
// 单后台线程:周期性多播本节点公告 + 收远端公告 + 算匹配 + 存活超时。
// 匹配按 (本地端点, 远端 id, 远端端点) 去重,首次出现触发 on_match,
// 对端超时下线触发 on_unmatch。
// ═══════════════════════════════════════════════════════════════
class DiscoveryAgent {
public:
    using MatchCallback = std::function<void(const MatchInfo&)>;

    DiscoveryAgent(std::string node_name, Locator data_locator,
                   std::string group = "239.255.0.1", uint16_t port = 7400);
    ~DiscoveryAgent();

    DiscoveryAgent(const DiscoveryAgent&) = delete;
    DiscoveryAgent& operator=(const DiscoveryAgent&) = delete;

    // 注册本地端点(可在 start 前或后调用,下次公告生效)
    void add_endpoint(EndpointInfo::Kind kind, const std::string& topic,
                      const std::string& type_name);

    void on_match(MatchCallback cb);
    void on_unmatch(MatchCallback cb);

    // 测试/调参:公告间隔与存活超时。须在 start() 前设置。
    void set_timing(std::chrono::milliseconds announce_interval,
                    std::chrono::milliseconds liveliness_timeout);

    bool start();
    void stop();

    uint64_t participant_id() const { return participant_id_; }

private:
    void run();                                            // 后台线程
    void announce();
    void handle_announcement(const ParticipantAnnouncement& ann);
    void reap_dead();
    static std::string match_key(const MatchInfo& m);

    std::string node_name_;
    Locator data_locator_;
    uint64_t participant_id_;
    UdpMulticast mc_;

    std::mutex mtx_;                                       // 保护 local_endpoints_
    std::vector<EndpointInfo> local_endpoints_;

    // 仅后台线程访问:
    struct Remote {
        std::vector<EndpointInfo> endpoints;
        Locator locator;
        std::chrono::steady_clock::time_point last_seen;
    };
    std::map<uint64_t, Remote> remotes_;
    std::map<std::string, MatchInfo> active_matches_;      // key → match

    MatchCallback on_match_;
    MatchCallback on_unmatch_;

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::chrono::steady_clock::time_point last_announce_{};

    std::chrono::milliseconds announce_interval_{1000};
    std::chrono::milliseconds liveliness_timeout_{5000};
    std::chrono::milliseconds recv_timeout_{200};
};

}  // namespace mm
```

- [ ] **Step 4: Write the implementation**

Create `discovery/src/discovery_agent.cpp`:

```cpp
#include "discovery/discovery_agent.h"
#include "common/logger.h"

#include <unistd.h>
#include <random>

namespace mm {

namespace {
uint64_t make_participant_id() {
    std::random_device rd;
    uint64_t hi = rd();
    uint64_t lo = rd();
    return (hi << 32) ^ lo ^ static_cast<uint64_t>(::getpid());
}
}  // namespace

DiscoveryAgent::DiscoveryAgent(std::string node_name, Locator data_locator,
                               std::string group, uint16_t port)
    : node_name_(std::move(node_name)),
      data_locator_(std::move(data_locator)),
      participant_id_(make_participant_id()),
      mc_(std::move(group), port) {}

DiscoveryAgent::~DiscoveryAgent() { stop(); }

void DiscoveryAgent::add_endpoint(EndpointInfo::Kind kind, const std::string& topic,
                                  const std::string& type_name) {
    std::lock_guard<std::mutex> lock(mtx_);
    EndpointInfo e;
    e.set_kind(kind);
    e.set_topic(topic);
    e.set_type_name(type_name);
    local_endpoints_.push_back(std::move(e));
}

void DiscoveryAgent::on_match(MatchCallback cb) { on_match_ = std::move(cb); }
void DiscoveryAgent::on_unmatch(MatchCallback cb) { on_unmatch_ = std::move(cb); }

void DiscoveryAgent::set_timing(std::chrono::milliseconds announce_interval,
                                std::chrono::milliseconds liveliness_timeout) {
    announce_interval_ = announce_interval;
    liveliness_timeout_ = liveliness_timeout;
}

bool DiscoveryAgent::start() {
    if (!mc_.open()) {
        LOG_ERROR("discovery: multicast open failed for node {}", node_name_);
        return false;
    }
    running_ = true;
    thread_ = std::thread(&DiscoveryAgent::run, this);
    LOG_INFO("discovery started: node={} id={}", node_name_, participant_id_);
    return true;
}

void DiscoveryAgent::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
    mc_.close();
}

void DiscoveryAgent::run() {
    announce();                                      // 上线先公告一次
    last_announce_ = std::chrono::steady_clock::now();

    while (running_.load()) {
        std::string bytes;
        if (mc_.recv(bytes, recv_timeout_)) {
            ParticipantAnnouncement ann;
            if (ann.ParseFromString(bytes)) {
                handle_announcement(ann);
            } else {
                LOG_WARN("discovery: bad announcement dropped");
            }
        }
        auto now = std::chrono::steady_clock::now();
        if (now - last_announce_ >= announce_interval_) {
            announce();
            last_announce_ = now;
        }
        reap_dead();
    }
}

void DiscoveryAgent::announce() {
    ParticipantAnnouncement ann;
    ann.set_participant_id(participant_id_);
    ann.set_node_name(node_name_);
    *ann.mutable_data_locator() = data_locator_;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        for (const auto& e : local_endpoints_) *ann.add_endpoints() = e;
    }
    std::string bytes;
    ann.SerializeToString(&bytes);
    mc_.send(bytes);
}

void DiscoveryAgent::handle_announcement(const ParticipantAnnouncement& ann) {
    if (ann.participant_id() == participant_id_) return;   // 自己,忽略

    Remote& r = remotes_[ann.participant_id()];
    r.endpoints.assign(ann.endpoints().begin(), ann.endpoints().end());
    r.locator = ann.data_locator();
    r.last_seen = std::chrono::steady_clock::now();

    std::vector<EndpointInfo> local;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        local = local_endpoints_;
    }

    auto matches = match_endpoints(local, ann.participant_id(), r.locator, r.endpoints);
    for (auto& m : matches) {
        std::string key = match_key(m);
        if (active_matches_.find(key) == active_matches_.end()) {
            active_matches_[key] = m;
            if (on_match_) on_match_(m);
        }
    }
}

void DiscoveryAgent::reap_dead() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = remotes_.begin(); it != remotes_.end();) {
        if (now - it->second.last_seen > liveliness_timeout_) {
            uint64_t dead_id = it->first;
            for (auto mit = active_matches_.begin(); mit != active_matches_.end();) {
                if (mit->second.remote_participant_id == dead_id) {
                    if (on_unmatch_) on_unmatch_(mit->second);
                    mit = active_matches_.erase(mit);
                } else {
                    ++mit;
                }
            }
            LOG_INFO("discovery: participant {} timed out", dead_id);
            it = remotes_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string DiscoveryAgent::match_key(const MatchInfo& m) {
    return std::to_string(static_cast<int>(m.local.kind())) + ":" + m.local.topic() + "|" +
           std::to_string(m.remote_participant_id) + "|" +
           std::to_string(static_cast<int>(m.remote.kind())) + ":" + m.remote.topic();
}

}  // namespace mm
```

- [ ] **Step 5: Add discovery_agent.cpp to the library**

Modify `discovery/CMakeLists.txt` — update sources:

```cmake
add_library(mm_discovery STATIC
    src/udp_multicast.cpp
    src/endpoint_matcher.cpp
    src/discovery_agent.cpp
)
```

- [ ] **Step 6: Build and run — expect PASS**

Run: `cmake -S . -B build && cmake --build build -j --target test_discovery_agent && (cd build && ctest -R test_discovery_agent --output-on-failure)`
Expected: PASS — 3 tests (TwoAgentsMatch, UnmatchOnTimeout, DoesNotSelfMatch). May take a few seconds total due to timing waits.

- [ ] **Step 7: Commit**

```bash
git add discovery/include/discovery/discovery_agent.h discovery/src/discovery_agent.cpp discovery/CMakeLists.txt tests/test_discovery_agent.cpp tests/CMakeLists.txt
git commit -m "feat(discovery): add DiscoveryAgent with announce/match/liveliness"
```

---

## Task 5: Node integration

**Files:**
- Modify: `core/include/core/node.h`
- Modify: `core/src/node.cpp`
- Modify: `core/CMakeLists.txt`
- Create: `tests/test_node_discovery.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_node_discovery.cpp`:

```cpp
#include "core/node.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

// 两个独立 Node(共享默认多播组)互相发现:A 发布 /chatter,B 订阅 /chatter
TEST(NodeDiscovery, TwoNodesDiscoverEachOther) {
    Node a("nodeA");
    Node b("nodeB");
    a.discovery().set_timing(80ms, 5000ms);
    b.discovery().set_timing(80ms, 5000ms);

    std::atomic<int> a_match{0};
    MatchInfo seen;
    a.discovery().on_match([&](const MatchInfo& m) { seen = m; ++a_match; });

    auto pub = a.create_publisher<mm::StringMsg>("/chatter");
    auto sub = b.create_subscriber<mm::StringMsg>("/chatter", [](const mm::StringMsg&) {});

    for (int i = 0; i < 200 && a_match.load() == 0; ++i)
        std::this_thread::sleep_for(25ms);

    ASSERT_GE(a_match.load(), 1);
    EXPECT_EQ(seen.local.topic(), "/chatter");
    EXPECT_EQ(seen.local.kind(), EndpointInfo::PUBLISHER);
    EXPECT_EQ(seen.remote.kind(), EndpointInfo::SUBSCRIBER);
    EXPECT_EQ(seen.remote_participant_id, b.discovery().participant_id());
}
```

- [ ] **Step 2: Register the test and confirm it fails to build**

Modify `tests/CMakeLists.txt` — add inside `if(GTest_FOUND)`:

```cmake
    add_executable(test_node_discovery test_node_discovery.cpp)
    target_link_libraries(test_node_discovery PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_node_discovery COMMAND test_node_discovery)
```

Run: `cmake -S . -B build && cmake --build build -j --target test_node_discovery`
Expected: FAIL — `error: 'class mm::Node' has no member named 'discovery'` (and no DiscoveryAgent include in node.h).

- [ ] **Step 3: Update node.h to own a DiscoveryAgent**

Replace the entire contents of `core/include/core/node.h` with:

```cpp
#pragma once

#include "core/local_bus.h"
#include "core/publisher.h"
#include "core/subscriber.h"
#include "discovery/discovery_agent.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Node(Participant):每进程一个。
// 工厂方法创建 Publisher/Subscriber,并持有它们的生命周期。
// 内部持有一个 LocalBus(进程内投递)和一个 DiscoveryAgent(跨进程发现)。
// 创建 pub/sub 时,会把端点注册到发现层,供其它进程匹配。
// ═══════════════════════════════════════════════════════════════
class Node {
public:
    explicit Node(std::string name);

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    template <typename T>
    std::shared_ptr<Publisher<T>> create_publisher(const std::string& topic) {
        auto pub = std::make_shared<Publisher<T>>(topic, bus_);
        entities_.push_back(pub);
        discovery_->add_endpoint(EndpointInfo::PUBLISHER, topic,
                                 T().GetDescriptor()->full_name());
        return pub;
    }

    template <typename T>
    std::shared_ptr<Subscriber<T>> create_subscriber(
        const std::string& topic, typename Subscriber<T>::Callback cb) {
        auto sub = std::make_shared<Subscriber<T>>(topic, std::move(cb));
        bus_->subscribe(topic, T().GetDescriptor()->full_name(), sub);
        entities_.push_back(sub);
        discovery_->add_endpoint(EndpointInfo::SUBSCRIBER, topic,
                                 T().GetDescriptor()->full_name());
        return sub;
    }

    const std::string& name() const { return name_; }

    // 暴露发现代理(注册匹配回调 / 调参 / demo 用)
    DiscoveryAgent& discovery() { return *discovery_; }

private:
    std::string name_;
    std::shared_ptr<LocalBus> bus_;
    std::unique_ptr<DiscoveryAgent> discovery_;
    std::vector<std::shared_ptr<void>> entities_;   // 持有 pub/sub 寿命
};

}  // namespace mm
```

- [ ] **Step 4: Update node.cpp to start discovery**

Replace the entire contents of `core/src/node.cpp` with:

```cpp
#include "core/node.h"
#include "common/logger.h"

namespace mm {

Node::Node(std::string name) : name_(std::move(name)), bus_(std::make_shared<LocalBus>()) {
    // Phase 2:locator 端口先占位(0),Phase 3 接 TCP server 后填真实端口
    Locator loc;
    loc.set_ip("127.0.0.1");
    loc.set_port(0);
    discovery_ = std::make_unique<DiscoveryAgent>(name_, loc);
    if (!discovery_->start()) {
        LOG_WARN("node {}: discovery failed to start (continuing in-process only)", name_);
    }
    LOG_INFO("Node created: {}", name_);
}

}  // namespace mm
```

- [ ] **Step 5: Link mm_core against mm_discovery**

Modify `core/CMakeLists.txt` — add `mm_discovery` to the link libraries:

```cmake
target_link_libraries(mm_core PUBLIC
    mm_transport
    mm_discovery
    mm_proto
    mm_common
    Threads::Threads
)
```

- [ ] **Step 6: Build and run — expect PASS**

Run: `cmake -S . -B build && cmake --build build -j --target test_node_discovery && (cd build && ctest -R test_node_discovery --output-on-failure)`
Expected: PASS — 1 test.

- [ ] **Step 7: Verify Phase 1 tests still pass (Node now starts discovery)**

Run: `cmake --build build -j --target test_node && (cd build && ctest -R test_node --output-on-failure)`
Expected: PASS — the in-process `test_node` still passes (discovery runs alongside, harmless).

- [ ] **Step 8: Commit**

```bash
git add core/include/core/node.h core/src/node.cpp core/CMakeLists.txt tests/test_node_discovery.cpp tests/CMakeLists.txt
git commit -m "feat(core): Node runs DiscoveryAgent and registers endpoints"
```

---

## Task 6: discovery_demo + full verification

**Files:**
- Create: `examples/discovery_demo.cpp`
- Modify: `examples/CMakeLists.txt`

- [ ] **Step 1: Write the demo**

Create `examples/discovery_demo.cpp`:

```cpp
#include "core/node.h"
#include "common/logger.h"
#include "messages.pb.h"

#include <chrono>
#include <string>
#include <thread>

// 用法:discovery_demo pub   或   discovery_demo sub
// 起两个进程(一 pub 一 sub),它们会通过 UDP 多播互相发现并打印 MATCH。
int main(int argc, char** argv) {
    std::string role = (argc > 1) ? argv[1] : "pub";
    mm::Node node("discovery_demo_" + role);

    node.discovery().on_match([](const mm::MatchInfo& m) {
        LOG_INFO("MATCH: local(kind={},topic={}) <-> remote(kind={},topic={}) @ {}:{} pid={}",
                 static_cast<int>(m.local.kind()), m.local.topic(),
                 static_cast<int>(m.remote.kind()), m.remote.topic(),
                 m.remote_locator.ip(), m.remote_locator.port(),
                 m.remote_participant_id);
    });

    if (role == "pub") {
        node.create_publisher<mm::StringMsg>("/chatter");
    } else {
        node.create_subscriber<mm::StringMsg>("/chatter", [](const mm::StringMsg&) {});
    }

    LOG_INFO("{} running, waiting for discovery... (Ctrl-C to quit)", role);
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
```

- [ ] **Step 2: Register the demo in CMake**

Modify `examples/CMakeLists.txt` — add after the `intra_process_demo` lines:

```cmake
add_executable(discovery_demo discovery_demo.cpp)
target_link_libraries(discovery_demo PRIVATE mm_core)
```

- [ ] **Step 3: Build and run two processes — observe MATCH**

Run:
```bash
cmake -S . -B build && cmake --build build -j --target discovery_demo
./build/examples/discovery_demo sub > /tmp/disc_sub.log 2>&1 &
SUB_PID=$!
./build/examples/discovery_demo pub > /tmp/disc_pub.log 2>&1 &
PUB_PID=$!
sleep 3
kill $SUB_PID $PUB_PID 2>/dev/null
echo "=== sub log ==="; grep MATCH /tmp/disc_sub.log
echo "=== pub log ==="; grep MATCH /tmp/disc_pub.log
```
Expected: both logs contain a `MATCH:` line — sub sees a remote PUBLISHER on `/chatter`, pub sees a remote SUBSCRIBER on `/chatter`.

- [ ] **Step 4: Full build + entire test suite green**

Run: `cmake --build build -j && (cd build && ctest --output-on-failure)`
Expected: PASS — all tests: `test_frame_codec`, `test_local_bus`, `test_subscriber`, `test_publisher`, `test_node`, `test_discovery_proto`, `test_udp_multicast`, `test_endpoint_matcher`, `test_discovery_agent`, `test_node_discovery`.

- [ ] **Step 5: Commit**

```bash
git add examples/discovery_demo.cpp examples/CMakeLists.txt
git commit -m "feat(examples): add cross-process discovery demo"
```

---

## Self-Review Notes

- **Spec coverage:** §2.1 proto → Task 1. §2 `UdpMulticast` → Task 2. matching (§3 rule) → Task 3. `DiscoveryAgent` announce/receive/match-dedup/reap (§2, §3, §4.1–4.5) → Task 4. Node integration (§5) → Task 5. §6 verification: udp loopback → Task 2; matcher rules → Task 3; two-agent match/unmatch/self-filter → Task 4; two-node discovery → Task 5; demo + full suite → Task 6.
- **Decision traceability:** §4.1 combined SPDP/SEDP = single `ParticipantAnnouncement` (Task 1/4). §4.2 single thread + one mutex over `local_endpoints_` (Task 4). §4.3 `on_match` carries remote locator = Phase 3 seam (Task 3 `MatchInfo`, Task 4). §4.4 random uint64 id (Task 4 `make_participant_id`). §4.5 locator placeholder port 0 (Task 5 node.cpp).
- **Type consistency:** `MatchInfo{local, remote, remote_locator, remote_participant_id}` defined in Task 3, used identically in Tasks 4–6. `match_endpoints(local, id, locator, remote)` signature stable across Task 3 (def) and Task 4 (call). `DiscoveryAgent::{add_endpoint, on_match, on_unmatch, set_timing, start, stop, participant_id}` consistent across Tasks 4–6. `EndpointInfo::{PUBLISHER, SUBSCRIBER}` from Task 1 proto used everywhere.
- **Build-green between tasks:** Tasks 1–4 only add new files/targets (no existing code changes), so the build stays green. Task 5 is the only one that modifies existing code (`node.h/.cpp`, `core/CMakeLists.txt`) and includes the Phase-1 regression check in Step 7.
- **Timing/flakiness:** agent tests use `set_timing` with short intervals (50–80ms announce) and poll with generous upper bounds (up to ~5s) rather than fixed sleeps, to stay fast but not flaky. Each socket test uses a distinct multicast group+port to avoid cross-test interference.
