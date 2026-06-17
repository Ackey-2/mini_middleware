# Phase 3 TCP Data Plane Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** After discovery reports a match, stream real published messages from a publisher node to a subscriber node over TCP.

**Architecture:** Each `Node` runs a `TcpServerTransport` data server on an ephemeral port (advertised in the discovery `Locator`). When a local *publisher* matches a remote subscriber, the publishing node dials the subscriber's data server and registers a `RemoteSink` (an `ISink`) in its `LocalBus`; `publish` then fans out to it, wrapping bytes in a `DataMessage{topic, payload}` envelope. The receiving server parses the envelope and delivers payloads to local subscribers only (loop-safe).

**Tech Stack:** C++17, Protobuf, epoll-based TCP transports (existing `TcpServerTransport`/`TcpClientTransport`), GoogleTest, CMake.

**Spec:** [docs/superpowers/specs/2026-06-17-phase3-tcp-data-plane-design.md](../specs/2026-06-17-phase3-tcp-data-plane-design.md)

---

## File Structure

**Create:**
- `proto/data.proto` — `DataMessage{topic, payload}` wire envelope.
- `core/include/core/remote_sink.h` — header-only `RemoteSink : ISink` (depends on the `Transport` interface, not the concrete TCP class, so it's unit-testable with a fake).
- `core/include/core/data_plane.h`, `core/src/data_plane.cpp` — `DataPlane`: owns data server + per-peer outbound connections; turns matches into channels.
- `examples/tcp_pubsub_demo.cpp` — talker/listener demo.
- `tests/test_data_message.cpp`, `tests/test_tcp_ephemeral_port.cpp`, `tests/test_remote_sink.cpp`, `tests/test_data_plane.cpp`, `tests/test_tcp_pubsub.cpp`.

**Modify:**
- `transport/include/transport/tcp_server_transport.h`, `transport/src/tcp_server_transport.cpp` — ephemeral port + `local_port()`.
- `core/include/core/local_bus.h`, `core/src/local_bus.cpp` — split local vs remote sinks; add `add_remote_sink`/`remove_remote_sink`/`deliver_inbound`.
- `core/include/core/node.h`, `core/src/node.cpp` — own `DataPlane`, construction order, wire match callbacks.
- `proto/CMakeLists.txt`, `core/CMakeLists.txt`, `examples/CMakeLists.txt`, `tests/CMakeLists.txt` — register new files/targets.
- `tests/test_local_bus.cpp` — add remote-sink / loop-safety tests.

> **Build note:** `cmake --build build -j` auto-reruns the configure step when a `CMakeLists.txt` changes, including new `.proto` files. If a fresh proto header isn't found, run `cmake -S . -B build` once, then rebuild.

---

## Task 1: DataMessage wire envelope

**Files:**
- Create: `proto/data.proto`
- Create: `tests/test_data_message.cpp`
- Modify: `proto/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Create the proto**

`proto/data.proto`:
```proto
syntax = "proto3";

package mm;

// 数据面信封:一条 TCP 帧的 payload。
// topic 让一条多路复用连接上的多个话题可区分;payload 是序列化后的用户消息。
message DataMessage {
    string topic = 1;
    bytes  payload = 2;
}
```

- [ ] **Step 2: Register the proto in the build**

In `proto/CMakeLists.txt`, extend `PROTO_FILES`:
```cmake
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/messages.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/discovery.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/data.proto
)
```

- [ ] **Step 3: Write the failing test**

`tests/test_data_message.cpp`:
```cpp
#include "data.pb.h"
#include <gtest/gtest.h>
#include <string>

using namespace mm;

TEST(DataMessage, RoundTrip) {
    std::string payload("\x01\x02\x03", 3);   // 含 NUL 的原始字节
    payload += " raw bytes";

    DataMessage m;
    m.set_topic("/scan");
    m.set_payload(payload);

    std::string bytes;
    ASSERT_TRUE(m.SerializeToString(&bytes));

    DataMessage out;
    ASSERT_TRUE(out.ParseFromString(bytes));
    EXPECT_EQ(out.topic(), "/scan");
    EXPECT_EQ(out.payload(), payload);
}
```

- [ ] **Step 4: Register the test target**

In `tests/CMakeLists.txt`, inside the `if(GTest_FOUND)` block, add:
```cmake
    add_executable(test_data_message test_data_message.cpp)
    target_link_libraries(test_data_message PRIVATE
        mm_proto
        GTest::gtest_main
    )
    add_test(NAME test_data_message COMMAND test_data_message)
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --build build -j && (cd build && ctest -R test_data_message --output-on-failure)`
Expected: `test_data_message` PASS (1 test).

- [ ] **Step 6: Commit**

```bash
git add proto/data.proto proto/CMakeLists.txt tests/test_data_message.cpp tests/CMakeLists.txt
git commit -m "feat(phase3): add DataMessage wire envelope"
```

---

## Task 2: TcpServerTransport ephemeral port

**Files:**
- Modify: `transport/include/transport/tcp_server_transport.h`
- Modify: `transport/src/tcp_server_transport.cpp`
- Create: `tests/test_tcp_ephemeral_port.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the accessor to the header**

In `transport/include/transport/tcp_server_transport.h`, add a public method right after `bool send(...) override;`:
```cpp
    bool send(const std::string& payload) override;   // 不实现,返回 false

    // 实际监听端口。传入端口 0 时由内核分配临时端口,start() 成功后此处可读真实值。
    uint16_t local_port() const { return port_; }
```

- [ ] **Step 2: Write the failing test**

`tests/test_tcp_ephemeral_port.cpp`:
```cpp
#include "transport/tcp_server_transport.h"
#include <gtest/gtest.h>

using namespace mm;

TEST(TcpServerTransport, EphemeralPortAssigned) {
    TcpServerTransport server(0);          // 端口 0 => 内核分配
    ASSERT_TRUE(server.start());
    EXPECT_GT(server.local_port(), 0);     // start 后应是真实端口
    server.stop();
}
```

- [ ] **Step 3: Register the test target**

In `tests/CMakeLists.txt`, inside `if(GTest_FOUND)`, add:
```cmake
    add_executable(test_tcp_ephemeral_port test_tcp_ephemeral_port.cpp)
    target_link_libraries(test_tcp_ephemeral_port PRIVATE
        mm_transport
        GTest::gtest_main
    )
    add_test(NAME test_tcp_ephemeral_port COMMAND test_tcp_ephemeral_port)
```

- [ ] **Step 4: Run the test to verify it fails**

Run: `cmake --build build -j && (cd build && ctest -R test_tcp_ephemeral_port --output-on-failure)`
Expected: FAIL — `local_port()` returns 0 because `port_` is still the constructor's `0` (bind assigned a real port but we never read it back).

- [ ] **Step 5: Read the bound port back after bind**

In `transport/src/tcp_server_transport.cpp`, in `start()`, immediately after the `bind(...)` success (right after its error-check `}` closes and before the `listen(...)` call), insert:
```cpp
    // 绑定后回填实际端口:传入 0 时内核已分配一个临时端口
    sockaddr_in actual{};
    socklen_t alen = sizeof(actual);
    if (getsockname(listen_fd_, (sockaddr*)&actual, &alen) == 0) {
        port_ = ntohs(actual.sin_port);
    }
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build build -j && (cd build && ctest -R test_tcp_ephemeral_port --output-on-failure)`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add transport/include/transport/tcp_server_transport.h transport/src/tcp_server_transport.cpp tests/test_tcp_ephemeral_port.cpp tests/CMakeLists.txt
git commit -m "feat(phase3): TcpServerTransport ephemeral port + local_port()"
```

---

## Task 3: LocalBus local/remote sink split + deliver_inbound

**Files:**
- Modify: `core/include/core/local_bus.h`
- Modify: `core/src/local_bus.cpp`
- Modify: `tests/test_local_bus.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_local_bus.cpp`:
```cpp
TEST(LocalBus, RemoteSinkReceivesPublishButNotInbound) {
    LocalBus bus;
    auto local = std::make_shared<FakeSink>();
    auto remote = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", local);
    bus.add_remote_sink("/t", remote);

    bus.publish("/t", "mm.StringMsg", "out");
    EXPECT_EQ(local->received.size(), 1u);   // 本地收到
    EXPECT_EQ(remote->received.size(), 1u);  // publish 扇出到远端

    bus.deliver_inbound("/t", "in");
    EXPECT_EQ(local->received.size(), 2u);   // 入站投本地
    EXPECT_EQ(remote->received.size(), 1u);  // ★ 入站绝不触达远端(环路安全)
}

TEST(LocalBus, RemoveRemoteSinkStopsDelivery) {
    LocalBus bus;
    auto remote = std::make_shared<FakeSink>();
    bus.add_remote_sink("/t", remote);
    bus.remove_remote_sink("/t", remote.get());
    bus.publish("/t", "mm.StringMsg", "x");
    EXPECT_EQ(remote->received.size(), 0u);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build -j 2>&1 | head -30`
Expected: compile error — `add_remote_sink`/`remove_remote_sink`/`deliver_inbound` are not members of `LocalBus`.

- [ ] **Step 3: Declare the new methods and remote-sink storage**

In `core/include/core/local_bus.h`, add to the public section after `publish(...)`:
```cpp
    // 注册一个远端转发 sink(Phase 3 RemoteSink)。publish 会扇出到它,
    // 但 deliver_inbound 不会——入站数据绝不被再次转发出去。
    void add_remote_sink(const std::string& topic, std::shared_ptr<ISink> sink);
    void remove_remote_sink(const std::string& topic, ISink* sink);

    // 网络层收到一帧后的落点:只投给该 topic 的本地订阅者。
    void deliver_inbound(const std::string& topic, const std::string& bytes);
```

In the same header, extend `TopicEntry`:
```cpp
    struct TopicEntry {
        std::string type_name;                            // 该 topic 约定的类型(空=未定)
        std::vector<std::weak_ptr<ISink>> sinks;          // 本地订阅者
        std::vector<std::weak_ptr<ISink>> remote_sinks;   // 远端转发代理(RemoteSink)
    };
```

- [ ] **Step 4: Implement in local_bus.cpp**

In `core/src/local_bus.cpp`, add an anonymous-namespace helper just below the `#include`s:
```cpp
namespace {
// 收集存活 sink 到 out,顺手清理失效的 weak_ptr
void collect(std::vector<std::weak_ptr<mm::ISink>>& sinks,
             std::vector<std::shared_ptr<mm::ISink>>& out) {
    for (auto it = sinks.begin(); it != sinks.end();) {
        if (auto sp = it->lock()) { out.push_back(std::move(sp)); ++it; }
        else it = sinks.erase(it);
    }
}
}  // namespace
```

Replace the existing `publish(...)` body's sink-collection loop so it gathers both lists. The full method becomes:
```cpp
void LocalBus::publish(const std::string& topic, const std::string& type_name,
                       const std::string& bytes) {
    std::vector<std::shared_ptr<ISink>> targets;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = topics_.find(topic);
        if (it == topics_.end()) return;
        auto& entry = it->second;
        if (!check_type(entry, type_name, topic)) return;
        collect(entry.sinks, targets);
        collect(entry.remote_sinks, targets);
    }
    // 锁外投递
    for (auto& sp : targets) sp->enqueue(bytes);
}
```

Append the three new methods at the end of the namespace:
```cpp
void LocalBus::add_remote_sink(const std::string& topic, std::shared_ptr<ISink> sink) {
    std::lock_guard<std::mutex> lock(mtx_);
    topics_[topic].remote_sinks.push_back(std::move(sink));
}

void LocalBus::remove_remote_sink(const std::string& topic, ISink* sink) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = topics_.find(topic);
    if (it == topics_.end()) return;
    auto& v = it->second.remote_sinks;
    for (auto sit = v.begin(); sit != v.end();) {
        auto sp = sit->lock();
        if (!sp || sp.get() == sink) sit = v.erase(sit);   // 命中或已失效都剔除
        else ++sit;
    }
}

void LocalBus::deliver_inbound(const std::string& topic, const std::string& bytes) {
    std::vector<std::shared_ptr<ISink>> targets;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = topics_.find(topic);
        if (it == topics_.end()) return;
        collect(it->second.sinks, targets);   // 只本地订阅者,绝不触达 remote_sinks
    }
    for (auto& sp : targets) sp->enqueue(bytes);
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build -j && (cd build && ctest -R test_local_bus --output-on-failure)`
Expected: PASS (all existing tests + the 2 new ones).

- [ ] **Step 6: Commit**

```bash
git add core/include/core/local_bus.h core/src/local_bus.cpp tests/test_local_bus.cpp
git commit -m "feat(phase3): LocalBus remote-sink split + deliver_inbound (loop-safe)"
```

---

## Task 4: RemoteSink

**Files:**
- Create: `core/include/core/remote_sink.h`
- Create: `tests/test_remote_sink.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/test_remote_sink.cpp`:
```cpp
#include "core/remote_sink.h"
#include "data.pb.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace mm;

// 假传输:捕获 send 的字节,不做网络
class FakeTransport : public Transport {
public:
    bool start() override { return true; }
    void stop() override {}
    void on_message(MessageCallback) override {}
    bool send(const std::string& payload) override { sent.push_back(payload); return true; }
    std::vector<std::string> sent;
};

TEST(RemoteSink, WrapsBytesIntoDataMessage) {
    auto t = std::make_shared<FakeTransport>();
    RemoteSink sink("/scan", t);

    sink.enqueue("hello");

    ASSERT_EQ(t->sent.size(), 1u);
    DataMessage m;
    ASSERT_TRUE(m.ParseFromString(t->sent[0]));
    EXPECT_EQ(m.topic(), "/scan");
    EXPECT_EQ(m.payload(), "hello");
}
```

- [ ] **Step 2: Register the test target**

In `tests/CMakeLists.txt`, inside `if(GTest_FOUND)`, add:
```cmake
    add_executable(test_remote_sink test_remote_sink.cpp)
    target_link_libraries(test_remote_sink PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_remote_sink COMMAND test_remote_sink)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake --build build -j 2>&1 | head -20`
Expected: FAIL — `core/remote_sink.h` does not exist.

- [ ] **Step 4: Create the header**

`core/include/core/remote_sink.h`:
```cpp
#pragma once

#include "core/local_bus.h"
#include "transport/transport.h"
#include "data.pb.h"

#include <memory>
#include <string>
#include <utility>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// RemoteSink:代表"远端订阅者"的本地代理。注册进 LocalBus 后,
// 发布者 publish 的字节会扇出到这里,被包成 DataMessage 经 TCP 发往对端。
// 依赖 Transport 接口(而非具体 TCP 类),便于用假传输做单测。
// ═══════════════════════════════════════════════════════════════
class RemoteSink : public ISink {
public:
    RemoteSink(std::string topic, std::shared_ptr<Transport> conn)
        : topic_(std::move(topic)), conn_(std::move(conn)) {}

    void enqueue(const std::string& bytes) override {
        DataMessage msg;
        msg.set_topic(topic_);
        msg.set_payload(bytes);
        std::string out;
        if (!msg.SerializeToString(&out)) return;
        // Transport 内部负责加帧头;未连上时 send 返回 false,字节被丢弃(BEST_EFFORT)。
        conn_->send(out);
    }

private:
    std::string topic_;
    std::shared_ptr<Transport> conn_;
};

}  // namespace mm
```

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build -j && (cd build && ctest -R test_remote_sink --output-on-failure)`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add core/include/core/remote_sink.h tests/test_remote_sink.cpp tests/CMakeLists.txt
git commit -m "feat(phase3): add RemoteSink (publish bytes -> DataMessage over Transport)"
```

---

## Task 5: DataPlane

**Files:**
- Create: `core/include/core/data_plane.h`, `core/src/data_plane.cpp`
- Modify: `core/CMakeLists.txt`
- Create: `tests/test_data_plane.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the header**

`core/include/core/data_plane.h`:
```cpp
#pragma once

#include "core/local_bus.h"
#include "core/remote_sink.h"
#include "transport/transport.h"
#include "discovery/endpoint_matcher.h"   // MatchInfo
#include "discovery.pb.h"                  // Locator, EndpointInfo

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace mm {

class TcpServerTransport;   // 前向声明:仅 .cpp 需要完整类型

// ═══════════════════════════════════════════════════════════════
// DataPlane:TCP 数据面。
//   - 监听一个数据服务器(临时端口),把入站 DataMessage 投给本地订阅者
//   - 对"本地是 PUBLISHER"的匹配,主动连对端并注册 RemoteSink
//   - 每个远端参与者复用一条出站连接,按 topic 多路复用
// 匹配回调在 discovery 后台线程触发,故内部状态用 mtx_ 保护。
// ═══════════════════════════════════════════════════════════════
class DataPlane {
public:
    DataPlane(std::shared_ptr<LocalBus> bus, std::string advertise_ip);
    ~DataPlane();

    DataPlane(const DataPlane&) = delete;
    DataPlane& operator=(const DataPlane&) = delete;

    bool start();                       // 启动数据服务器
    void stop();

    uint16_t server_port() const;       // 供 Node 填 Locator
    const std::string& advertise_ip() const { return advertise_ip_; }

    void handle_match(const MatchInfo& m);
    void handle_unmatch(const MatchInfo& m);

private:
    void on_inbound(const std::string& payload);          // 数据服务器收到一帧
    // 复用/新建到某远端参与者的出站连接(调用方需持 mtx_)
    std::shared_ptr<Transport> connection_for(uint64_t pid, const Locator& loc);

    std::shared_ptr<LocalBus> bus_;
    std::string advertise_ip_;
    std::unique_ptr<TcpServerTransport> server_;

    std::mutex mtx_;
    std::map<uint64_t, std::shared_ptr<Transport>> connections_;   // 每对端一条
    std::map<std::string, std::shared_ptr<RemoteSink>> sinks_;     // match_key → sink
    std::map<uint64_t, int> refcount_;                             // 对端活跃 PUB 匹配数
};

}  // namespace mm
```

- [ ] **Step 2: Create the implementation**

`core/src/data_plane.cpp`:
```cpp
#include "core/data_plane.h"
#include "transport/tcp_server_transport.h"
#include "transport/tcp_client_transport.h"
#include "common/logger.h"
#include "data.pb.h"

namespace mm {

namespace {
// 与 DiscoveryAgent::match_key 同一公式,保证 match/unmatch 对得上
std::string match_key(const MatchInfo& m) {
    return std::to_string(static_cast<int>(m.local.kind())) + ":" + m.local.topic() + "|" +
           std::to_string(m.remote_participant_id) + "|" +
           std::to_string(static_cast<int>(m.remote.kind())) + ":" + m.remote.topic();
}
}  // namespace

DataPlane::DataPlane(std::shared_ptr<LocalBus> bus, std::string advertise_ip)
    : bus_(std::move(bus)), advertise_ip_(std::move(advertise_ip)) {}

DataPlane::~DataPlane() { stop(); }

bool DataPlane::start() {
    server_ = std::make_unique<TcpServerTransport>(0);   // 临时端口
    server_->on_message([this](const std::string& payload) { on_inbound(payload); });
    if (!server_->start()) {
        LOG_ERROR("data plane: server failed to start");
        server_.reset();
        return false;
    }
    LOG_INFO("data plane listening on {}:{}", advertise_ip_, server_->local_port());
    return true;
}

void DataPlane::stop() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        sinks_.clear();
        for (auto& kv : connections_) kv.second->stop();
        connections_.clear();
        refcount_.clear();
    }
    if (server_) { server_->stop(); server_.reset(); }
}

uint16_t DataPlane::server_port() const {
    return server_ ? server_->local_port() : 0;
}

void DataPlane::on_inbound(const std::string& payload) {
    DataMessage msg;
    if (!msg.ParseFromString(payload)) {
        LOG_WARN("data plane: bad DataMessage dropped");
        return;
    }
    bus_->deliver_inbound(msg.topic(), msg.payload());   // 只投本地订阅者
}

std::shared_ptr<Transport> DataPlane::connection_for(uint64_t pid, const Locator& loc) {
    auto it = connections_.find(pid);
    if (it != connections_.end()) return it->second;
    auto conn = std::make_shared<TcpClientTransport>(
        loc.ip(), static_cast<uint16_t>(loc.port()));
    conn->start();   // 异步连接;连上前的 send 会返回 false
    connections_[pid] = conn;
    return conn;
}

void DataPlane::handle_match(const MatchInfo& m) {
    if (m.local.kind() != EndpointInfo::PUBLISHER) return;   // 只有发布方主动连
    std::lock_guard<std::mutex> lock(mtx_);
    std::string key = match_key(m);
    if (sinks_.count(key)) return;                           // 幂等

    auto conn = connection_for(m.remote_participant_id, m.remote_locator);
    auto sink = std::make_shared<RemoteSink>(m.local.topic(), conn);
    bus_->add_remote_sink(m.local.topic(), sink);
    sinks_[key] = sink;
    ++refcount_[m.remote_participant_id];
    LOG_INFO("data plane: channel up topic={} -> {}:{}",
             m.local.topic(), m.remote_locator.ip(), m.remote_locator.port());
}

void DataPlane::handle_unmatch(const MatchInfo& m) {
    if (m.local.kind() != EndpointInfo::PUBLISHER) return;
    std::lock_guard<std::mutex> lock(mtx_);
    std::string key = match_key(m);
    auto it = sinks_.find(key);
    if (it == sinks_.end()) return;

    bus_->remove_remote_sink(m.local.topic(), it->second.get());
    sinks_.erase(it);

    uint64_t pid = m.remote_participant_id;
    if (--refcount_[pid] <= 0) {       // 该对端再无活跃匹配 → 关连接
        refcount_.erase(pid);
        auto cit = connections_.find(pid);
        if (cit != connections_.end()) {
            cit->second->stop();
            connections_.erase(cit);
        }
    }
    LOG_INFO("data plane: channel down topic={} peer={}", m.local.topic(), pid);
}

}  // namespace mm
```

- [ ] **Step 3: Register the source in mm_core**

In `core/CMakeLists.txt`, add `src/data_plane.cpp` to the `add_library(mm_core STATIC ...)` list:
```cmake
add_library(mm_core STATIC
    src/local_bus.cpp
    src/node.cpp
    src/data_plane.cpp
)
```

- [ ] **Step 4: Write the failing test**

`tests/test_data_plane.cpp`:
```cpp
#include "core/data_plane.h"
#include "core/local_bus.h"
#include "discovery/endpoint_matcher.h"
#include "discovery.pb.h"
#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace mm;
using namespace std::chrono_literals;

class FakeSink : public ISink {
public:
    void enqueue(const std::string& bytes) override {
        std::lock_guard<std::mutex> lk(m_); received_.push_back(bytes);
    }
    size_t count() { std::lock_guard<std::mutex> lk(m_); return received_.size(); }
    bool saw(const std::string& s) {
        std::lock_guard<std::mutex> lk(m_);
        for (auto& r : received_) if (r == s) return true;
        return false;
    }
private:
    std::vector<std::string> received_;
    std::mutex m_;
};

static MatchInfo make_match(EndpointInfo::Kind local_kind, const std::string& topic,
                            const std::string& type, uint64_t remote_pid,
                            const std::string& ip, uint16_t port) {
    MatchInfo mi;
    mi.local.set_kind(local_kind);
    mi.local.set_topic(topic);
    mi.local.set_type_name(type);
    mi.remote.set_kind(local_kind == EndpointInfo::PUBLISHER ? EndpointInfo::SUBSCRIBER
                                                             : EndpointInfo::PUBLISHER);
    mi.remote.set_topic(topic);
    mi.remote.set_type_name(type);
    mi.remote_participant_id = remote_pid;
    mi.remote_locator.set_ip(ip);
    mi.remote_locator.set_port(port);
    return mi;
}

TEST(DataPlane, PublisherSideSendsToReceiver) {
    auto bus_recv = std::make_shared<LocalBus>();
    auto sink = std::make_shared<FakeSink>();
    bus_recv->subscribe("/chat", "mm.StringMsg", sink);

    DataPlane receiver(bus_recv, "127.0.0.1");
    ASSERT_TRUE(receiver.start());
    uint16_t rport = receiver.server_port();
    ASSERT_GT(rport, 0);

    auto bus_pub = std::make_shared<LocalBus>();
    DataPlane sender(bus_pub, "127.0.0.1");
    ASSERT_TRUE(sender.start());

    auto m = make_match(EndpointInfo::PUBLISHER, "/chat", "mm.StringMsg",
                        12345, "127.0.0.1", rport);
    sender.handle_match(m);

    // 连接异步建立;周期性发布直到收到或超时
    for (int i = 0; i < 200 && sink->count() == 0; ++i) {
        bus_pub->publish("/chat", "mm.StringMsg", "hi");
        std::this_thread::sleep_for(10ms);
    }
    ASSERT_TRUE(sink->saw("hi"));

    // 排空在途消息后解除匹配
    std::this_thread::sleep_for(100ms);
    sender.handle_unmatch(m);

    // unmatch 后 RemoteSink 已撤销:新内容不应再到达
    bus_pub->publish("/chat", "mm.StringMsg", "AFTER");
    std::this_thread::sleep_for(150ms);
    EXPECT_FALSE(sink->saw("AFTER"));
}

TEST(DataPlane, IgnoresSubscriberSideMatch) {
    auto bus = std::make_shared<LocalBus>();
    DataPlane dp(bus, "127.0.0.1");
    ASSERT_TRUE(dp.start());
    // 本地是 SUBSCRIBER 的匹配:不建任何连接,不崩溃
    auto m = make_match(EndpointInfo::SUBSCRIBER, "/chat", "mm.StringMsg",
                        999, "127.0.0.1", 65000);
    dp.handle_match(m);
    SUCCEED();
}
```

- [ ] **Step 5: Register the test target**

In `tests/CMakeLists.txt`, inside `if(GTest_FOUND)`, add:
```cmake
    add_executable(test_data_plane test_data_plane.cpp)
    target_link_libraries(test_data_plane PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_data_plane COMMAND test_data_plane)
```

- [ ] **Step 6: Build and run the test**

Run: `cmake --build build -j && (cd build && ctest -R test_data_plane --output-on-failure)`
Expected: PASS (2 tests). `PublisherSideSendsToReceiver` proves bytes cross a real TCP socket; `IgnoresSubscriberSideMatch` proves the direction rule.

- [ ] **Step 7: Commit**

```bash
git add core/include/core/data_plane.h core/src/data_plane.cpp core/CMakeLists.txt tests/test_data_plane.cpp tests/CMakeLists.txt
git commit -m "feat(phase3): DataPlane wires matches to TCP channels"
```

---

## Task 6: Node integration + end-to-end test

**Files:**
- Modify: `core/include/core/node.h`, `core/src/node.cpp`
- Create: `tests/test_tcp_pubsub.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add DataPlane to the Node header**

In `core/include/core/node.h`, add the include near the top:
```cpp
#include "core/data_plane.h"
```
Add the member between `bus_` and `discovery_`, and update the destruction-order comment:
```cpp
private:
    // 成员析构顺序(声明逆序):entities_(停订阅线程)→ discovery_(停发现线程,
    // 不再有匹配回调)→ data_plane_(停服务器与所有出站连接,释放 RemoteSink)
    // → bus_。discovery 必须先于 data_plane 析构。
    std::string name_;
    std::shared_ptr<LocalBus> bus_;
    std::unique_ptr<DataPlane> data_plane_;
    std::unique_ptr<DiscoveryAgent> discovery_;
    std::vector<std::shared_ptr<void>> entities_;   // 持有 pub/sub 寿命
```

- [ ] **Step 2: Rewrite the Node constructor**

Replace the body of `Node::Node(...)` in `core/src/node.cpp` with:
```cpp
Node::Node(std::string name)
    : name_(std::move(name)), bus_(std::make_shared<LocalBus>()) {
    // 1. 数据面:启动 TCP 数据服务器(临时端口)
    data_plane_ = std::make_unique<DataPlane>(bus_, "127.0.0.1");
    if (!data_plane_->start()) {
        LOG_WARN("node {}: data plane failed to start", name_);
    }

    // 2. 用真实监听地址构造 Locator(必须在第一条发现公告前就位)
    Locator loc;
    loc.set_ip(data_plane_->advertise_ip());
    loc.set_port(data_plane_->server_port());
    discovery_ = std::make_unique<DiscoveryAgent>(name_, loc);

    // 3. 把发现层匹配接到数据面(回调在 discovery 后台线程触发)
    DataPlane* dp = data_plane_.get();
    discovery_->on_match([dp](const MatchInfo& m) { dp->handle_match(m); });
    discovery_->on_unmatch([dp](const MatchInfo& m) { dp->handle_unmatch(m); });

    // 4. 启动发现
    if (!discovery_->start()) {
        LOG_WARN("node {}: discovery failed to start (in-process only)", name_);
    }
    LOG_INFO("Node created: {} (data port {})", name_, data_plane_->server_port());
}
```

- [ ] **Step 3: Write the failing end-to-end test**

`tests/test_tcp_pubsub.cpp`:
```cpp
#include "core/node.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

// 两个独立 Node:UDP 发现匹配 + TCP 数据面把消息真正传过去。
TEST(TcpPubSub, EndToEndDelivery) {
    Node talker("talker");
    Node listener("listener");
    talker.discovery().set_timing(80ms, 5000ms);
    listener.discovery().set_timing(80ms, 5000ms);

    const std::string topic = "/tcp_pubsub_chatter";
    std::atomic<int> got{0};
    std::string last;
    std::mutex lk;

    auto sub = listener.create_subscriber<mm::StringMsg>(
        topic, [&](const mm::StringMsg& m) {
            std::lock_guard<std::mutex> g(lk);
            last = m.data();
            ++got;
        });
    auto pub = talker.create_publisher<mm::StringMsg>(topic);

    // 周期性发布:等发现匹配 + TCP 建链 + 投递
    for (int i = 0; i < 300 && got.load() == 0; ++i) {
        mm::StringMsg msg;
        msg.set_data("hello-tcp");
        pub->publish(msg);
        std::this_thread::sleep_for(20ms);
    }

    ASSERT_GE(got.load(), 1);
    std::lock_guard<std::mutex> g(lk);
    EXPECT_EQ(last, "hello-tcp");
}
```

- [ ] **Step 4: Register the test target**

In `tests/CMakeLists.txt`, inside `if(GTest_FOUND)`, add:
```cmake
    add_executable(test_tcp_pubsub test_tcp_pubsub.cpp)
    target_link_libraries(test_tcp_pubsub PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_tcp_pubsub COMMAND test_tcp_pubsub)
```

- [ ] **Step 5: Build and run the test**

Run: `cmake --build build -j && (cd build && ctest -R test_tcp_pubsub --output-on-failure)`
Expected: PASS — the subscriber in the "listener" node receives `"hello-tcp"` published by the "talker" node over TCP.

- [ ] **Step 6: Run the full suite (regression check)**

Run: `cmake --build build -j && (cd build && ctest --output-on-failure)`
Expected: all tests green, including the pre-existing discovery/local_bus/node tests.

- [ ] **Step 7: Commit**

```bash
git add core/include/core/node.h core/src/node.cpp tests/test_tcp_pubsub.cpp tests/CMakeLists.txt
git commit -m "feat(phase3): Node runs data plane; cross-node TCP pub/sub end-to-end"
```

---

## Task 7: Cross-process demo

**Files:**
- Create: `examples/tcp_pubsub_demo.cpp`
- Modify: `examples/CMakeLists.txt`

- [ ] **Step 1: Create the demo**

`examples/tcp_pubsub_demo.cpp`:
```cpp
#include "core/node.h"
#include "messages.pb.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// 用法(开两个终端):
//   ./tcp_pubsub_demo listener    # 进程1:订阅并打印
//   ./tcp_pubsub_demo talker      # 进程2:每秒发布一条
int main(int argc, char** argv) {
    std::string role = (argc > 1) ? argv[1] : "talker";
    mm::Node node(role);
    const std::string topic = "/chatter";

    if (role == "listener") {
        auto sub = node.create_subscriber<mm::StringMsg>(
            topic, [](const mm::StringMsg& m) {
                std::cout << "[listener] recv: " << m.data() << std::endl;
            });
        std::cout << "[listener] waiting on " << topic << " ..." << std::endl;
        while (true) std::this_thread::sleep_for(1s);
    } else {
        auto pub = node.create_publisher<mm::StringMsg>(topic);
        for (int n = 0; ; ++n) {
            mm::StringMsg m;
            m.set_data("msg #" + std::to_string(n));
            pub->publish(m);
            std::cout << "[talker] sent: " << m.data() << std::endl;
            std::this_thread::sleep_for(1s);
        }
    }
    return 0;
}
```

- [ ] **Step 2: Register the demo target**

In `examples/CMakeLists.txt`, add:
```cmake
add_executable(tcp_pubsub_demo tcp_pubsub_demo.cpp)
target_link_libraries(tcp_pubsub_demo PRIVATE mm_core)
```

- [ ] **Step 3: Build**

Run: `cmake --build build -j`
Expected: builds `tcp_pubsub_demo` with no errors.

- [ ] **Step 4: Manual smoke test (two terminals)**

Terminal A: `./build/examples/tcp_pubsub_demo listener`
Terminal B: `./build/examples/tcp_pubsub_demo talker`
Expected: within ~1–2s the listener prints `[listener] recv: msg #N` lines tracking the talker's output. Ctrl-C both when done.

- [ ] **Step 5: Commit**

```bash
git add examples/tcp_pubsub_demo.cpp examples/CMakeLists.txt
git commit -m "feat(phase3): cross-process TCP pub/sub demo"
```

---

## Verification Checklist (maps to spec §8)

- [ ] `test_data_message` — DataMessage envelope round-trip (spec: 信封单测).
- [ ] `test_local_bus` new cases — `publish` reaches remote sink, `deliver_inbound` does not (spec: loop-safety 单测).
- [ ] `test_data_plane` — publisher-side sends over real TCP to receiver; unmatch tears down; subscriber-side match ignored (spec: DataPlane 单测).
- [ ] `test_tcp_pubsub` — two Nodes, end-to-end over UDP discovery + TCP data (spec: 端到端集成测试).
- [ ] `tcp_pubsub_demo` — two processes, listener prints talker's messages (spec: 跨进程 demo).
- [ ] `cmake --build build -j && (cd build && ctest --output-on-failure)` fully green (spec: 全量 ctest 绿).
