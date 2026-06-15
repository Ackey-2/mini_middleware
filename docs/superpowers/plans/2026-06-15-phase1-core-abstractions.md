# Phase 1: Core Abstractions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `Node` / `Publisher<T>` / `Subscriber<T>` so a single process can publish and subscribe to topics in-process, end to end.

**Architecture:** A per-process `LocalBus` routes serialized bytes by topic to subscribers. `Publisher<T>` serializes a protobuf message and hands bytes to the bus. Each `Subscriber<T>` owns a `BlockingQueue` + worker thread that parses bytes back into `T` and invokes the user callback off the publisher's thread. Subscribers register as `ISink` — the single seam the network layer (Phase 2/3) plugs into later.

**Tech Stack:** C++17, Protobuf, CMake, GoogleTest, `std::thread` + existing `BlockingQueue`.

**Spec:** `docs/superpowers/specs/2026-06-15-phase1-core-abstractions-design.md`

**Build & test commands used throughout:**
- Build one target: `cmake --build build -j --target <name>` (auto-reconfigures when a `CMakeLists.txt` changes)
- Run one test: `cd build && ctest -R <name> --output-on-failure`
- Build everything: `cmake --build build -j`

> **C++ TDD note:** In C++ the "RED" state is usually a **compile or link error** (the class/method doesn't exist yet), not a runtime assertion failure. That counts as a failing test here. Each task writes the test first, builds it to see it fail, then implements.

---

## File Structure

| File | Responsibility |
|------|----------------|
| `core/include/core/local_bus.h` | `ISink` interface + `LocalBus` declaration |
| `core/src/local_bus.cpp` | `LocalBus` routing + topic type-consistency check |
| `core/include/core/subscriber.h` | `Subscriber<T>` (header-only template): queue + worker thread, `ISink` impl |
| `core/include/core/publisher.h` | `Publisher<T>` (rewritten): serialize → `LocalBus` |
| `core/include/core/node.h` | `Node`: factory methods (header-only templates) |
| `core/src/node.cpp` | `Node` ctor |
| `tests/test_local_bus.cpp` | LocalBus routing + type-check tests |
| `tests/test_subscriber.cpp` | Subscriber queue/parse/teardown tests |
| `tests/test_publisher.cpp` | Publisher serialize→bus test |
| `tests/test_node.cpp` | End-to-end 1 pub + 2 sub test (spec §6) |
| `examples/intra_process_demo.cpp` | Runnable 1 pub + 2 sub demo |

---

## Task 1: ISink + LocalBus (topic routing & type check)

**Files:**
- Create: `core/include/core/local_bus.h`
- Create: `core/src/local_bus.cpp`
- Create: `tests/test_local_bus.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_local_bus.cpp`:

```cpp
#include "core/local_bus.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace mm;

// 测试替身:一个最简单的 sink,把收到的字节存起来
class FakeSink : public ISink {
public:
    void enqueue(const std::string& bytes) override { received.push_back(bytes); }
    std::vector<std::string> received;
};

TEST(LocalBus, DeliversToSingleSubscriber) {
    LocalBus bus;
    auto sink = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", sink);
    bus.publish("/t", "mm.StringMsg", "hello");
    ASSERT_EQ(sink->received.size(), 1u);
    EXPECT_EQ(sink->received[0], "hello");
}

TEST(LocalBus, DeliversToMultipleSubscribers) {
    LocalBus bus;
    auto a = std::make_shared<FakeSink>();
    auto b = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", a);
    bus.subscribe("/t", "mm.StringMsg", b);
    bus.publish("/t", "mm.StringMsg", "x");
    EXPECT_EQ(a->received.size(), 1u);
    EXPECT_EQ(b->received.size(), 1u);
}

TEST(LocalBus, NoSubscriberIsNoop) {
    LocalBus bus;
    bus.publish("/empty", "mm.StringMsg", "x");  // 不崩溃即可
    SUCCEED();
}

TEST(LocalBus, PrunesExpiredSinks) {
    LocalBus bus;
    auto a = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", a);
    {
        auto b = std::make_shared<FakeSink>();
        bus.subscribe("/t", "mm.StringMsg", b);
    }  // b 离开作用域被销毁,bus 里只剩 weak_ptr
    bus.publish("/t", "mm.StringMsg", "x");  // 不能因为 b 失效而崩溃
    EXPECT_EQ(a->received.size(), 1u);
}

TEST(LocalBus, RejectsTypeMismatch) {
    LocalBus bus;
    auto a = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.StringMsg", a);
    // 同一 topic 用不同类型订阅,应被拒绝(不加入投递列表)
    auto b = std::make_shared<FakeSink>();
    bus.subscribe("/t", "mm.Point3D", b);
    bus.publish("/t", "mm.StringMsg", "x");
    EXPECT_EQ(a->received.size(), 1u);
    EXPECT_EQ(b->received.size(), 0u);
}
```

- [ ] **Step 2: Register the test in CMake and confirm it fails to build**

Modify `tests/CMakeLists.txt` — add inside the `if(GTest_FOUND)` block, after the `test_frame_codec` block:

```cmake
    add_executable(test_local_bus test_local_bus.cpp)
    target_link_libraries(test_local_bus PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_local_bus COMMAND test_local_bus)
```

Run: `cmake --build build -j --target test_local_bus`
Expected: FAIL — `fatal error: core/local_bus.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `core/include/core/local_bus.h`:

```cpp
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// ISink:订阅侧的统一落点。
// 任何"能接收一帧字节"的东西都实现它 —— 本地 Subscriber,
// 以及未来(Phase 3)把字节通过 TCP/SHM 发往远端的代理。
// 这是整个项目从单机长成分布式的关键接缝。
// ═══════════════════════════════════════════════════════════════
class ISink {
public:
    virtual ~ISink() = default;
    virtual void enqueue(const std::string& bytes) = 0;
};

// ═══════════════════════════════════════════════════════════════
// LocalBus:进程内总线,按 topic 把字节分发给所有订阅者。
// 用 weak_ptr 持有订阅者,不延长其寿命;分发时锁内收集存活 sink,
// 锁外投递(避免在持锁时回调用户代码导致死锁)。
// ═══════════════════════════════════════════════════════════════
class LocalBus {
public:
    // 发布者声明某 topic 的类型(首次确定该 topic 类型)
    void register_publisher(const std::string& topic, const std::string& type_name);

    // 注册订阅者;type_name 与该 topic 已有类型不一致则拒绝
    void subscribe(const std::string& topic, const std::string& type_name,
                   std::shared_ptr<ISink> sink);

    // 把 bytes 投给该 topic 所有存活订阅者
    void publish(const std::string& topic, const std::string& type_name,
                 const std::string& bytes);

private:
    struct TopicEntry {
        std::string type_name;                      // 该 topic 约定的类型(空=未定)
        std::vector<std::weak_ptr<ISink>> sinks;
    };

    // 检查/确定 topic 类型;一致返回 true,不一致 LOG_ERROR 返回 false
    bool check_type(TopicEntry& entry, const std::string& type_name,
                    const std::string& topic);

    std::mutex mtx_;
    std::unordered_map<std::string, TopicEntry> topics_;
};

}  // namespace mm
```

- [ ] **Step 4: Write the implementation**

Create `core/src/local_bus.cpp`:

```cpp
#include "core/local_bus.h"
#include "common/logger.h"

namespace mm {

bool LocalBus::check_type(TopicEntry& entry, const std::string& type_name,
                          const std::string& topic) {
    if (entry.type_name.empty()) {
        entry.type_name = type_name;     // 首次确定
        return true;
    }
    if (entry.type_name != type_name) {
        LOG_ERROR("topic {} type mismatch: expected {}, got {}",
                  topic, entry.type_name, type_name);
        return false;
    }
    return true;
}

void LocalBus::register_publisher(const std::string& topic,
                                  const std::string& type_name) {
    std::lock_guard<std::mutex> lock(mtx_);
    check_type(topics_[topic], type_name, topic);
}

void LocalBus::subscribe(const std::string& topic, const std::string& type_name,
                         std::shared_ptr<ISink> sink) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto& entry = topics_[topic];
    if (!check_type(entry, type_name, topic)) return;
    entry.sinks.push_back(std::move(sink));
}

void LocalBus::publish(const std::string& topic, const std::string& type_name,
                       const std::string& bytes) {
    std::vector<std::shared_ptr<ISink>> targets;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = topics_.find(topic);
        if (it == topics_.end()) return;
        auto& entry = it->second;
        if (!check_type(entry, type_name, topic)) return;

        // 收集存活 sink,顺手清理已失效的 weak_ptr
        auto& sinks = entry.sinks;
        for (auto sit = sinks.begin(); sit != sinks.end();) {
            if (auto sp = sit->lock()) {
                targets.push_back(std::move(sp));
                ++sit;
            } else {
                sit = sinks.erase(sit);
            }
        }
    }
    // 锁外投递
    for (auto& sp : targets) sp->enqueue(bytes);
}

}  // namespace mm
```

- [ ] **Step 5: Wire core sources into CMake**

Modify `core/CMakeLists.txt` — replace the whole file with:

```cmake
# core 模块:Node / Publisher / Subscriber
add_library(mm_core STATIC
    src/local_bus.cpp
)

target_include_directories(mm_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# core 依赖 transport / proto / common,并用到 std::thread
target_link_libraries(mm_core PUBLIC
    mm_transport
    mm_proto
    mm_common
    Threads::Threads
)
```

- [ ] **Step 6: Build and run the test — expect PASS**

Run: `cmake --build build -j --target test_local_bus && cd build && ctest -R test_local_bus --output-on-failure`
Expected: PASS — 5 tests pass. The `RejectsTypeMismatch` test will also print a red `LOG_ERROR` line; that is expected output, not a failure.

- [ ] **Step 7: Commit**

```bash
git add core/include/core/local_bus.h core/src/local_bus.cpp core/CMakeLists.txt tests/test_local_bus.cpp tests/CMakeLists.txt
git commit -m "feat(core): add ISink and LocalBus topic routing"
```

---

## Task 2: Subscriber<T>

**Files:**
- Create: `core/include/core/subscriber.h`
- Create: `tests/test_subscriber.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_subscriber.cpp`:

```cpp
#include "core/subscriber.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

// 把一个 StringMsg 序列化成字节
static std::string make_bytes(const std::string& data) {
    mm::StringMsg m;
    m.set_data(data);
    std::string out;
    m.SerializeToString(&out);
    return out;
}

TEST(Subscriber, DeliversParsedMessageToCallback) {
    std::atomic<int> count{0};
    std::string last;
    auto sub = std::make_shared<Subscriber<mm::StringMsg>>(
        "/t", [&](const mm::StringMsg& m) { last = m.data(); ++count; });

    sub->enqueue(make_bytes("hello"));

    // 工作线程异步处理,轮询等待最多 1s
    for (int i = 0; i < 100 && count.load() == 0; ++i) std::this_thread::sleep_for(10ms);

    EXPECT_EQ(count.load(), 1);
    EXPECT_EQ(last, "hello");
}

TEST(Subscriber, ProcessesMultipleInOrder) {
    std::vector<std::string> got;
    std::mutex m;
    std::atomic<int> count{0};
    auto sub = std::make_shared<Subscriber<mm::StringMsg>>(
        "/t", [&](const mm::StringMsg& msg) {
            std::lock_guard<std::mutex> lk(m);
            got.push_back(msg.data());
            ++count;
        });

    for (int i = 0; i < 5; ++i) sub->enqueue(make_bytes(std::to_string(i)));
    for (int i = 0; i < 100 && count.load() < 5; ++i) std::this_thread::sleep_for(10ms);

    ASSERT_EQ(count.load(), 5);
    std::vector<std::string> expected{"0", "1", "2", "3", "4"};
    EXPECT_EQ(got, expected);
}

TEST(Subscriber, DestructorJoinsCleanly) {
    // 创建后立刻销毁,不应挂起(队列 close + 线程 join)
    auto sub = std::make_shared<Subscriber<mm::StringMsg>>(
        "/t", [](const mm::StringMsg&) {});
    sub.reset();
    SUCCEED();
}
```

- [ ] **Step 2: Register the test in CMake and confirm it fails to build**

Modify `tests/CMakeLists.txt` — add inside `if(GTest_FOUND)`:

```cmake
    add_executable(test_subscriber test_subscriber.cpp)
    target_link_libraries(test_subscriber PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_subscriber COMMAND test_subscriber)
```

Run: `cmake --build build -j --target test_subscriber`
Expected: FAIL — `fatal error: core/subscriber.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `core/include/core/subscriber.h`:

```cpp
#pragma once

#include "core/local_bus.h"
#include "common/blocking_queue.h"
#include "common/logger.h"

#include <functional>
#include <string>
#include <thread>
#include <utility>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Subscriber<T>:某 topic 的订阅者。
//   - 实现 ISink::enqueue —— 收到字节立即入队,不阻塞发布者
//   - 自己的工作线程从队列取字节 → 反序列化成 T → 调用户回调
// 析构时关闭队列并 join 工作线程。
// ═══════════════════════════════════════════════════════════════
template <typename MessageT>
class Subscriber : public ISink {
public:
    using Callback = std::function<void(const MessageT&)>;

    Subscriber(std::string topic, Callback cb)
        : topic_(std::move(topic)), cb_(std::move(cb)) {
        worker_ = std::thread(&Subscriber::run, this);
    }

    ~Subscriber() override {
        queue_.close();                       // 唤醒阻塞的 pop,使 run() 退出
        if (worker_.joinable()) worker_.join();
    }

    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;

    void enqueue(const std::string& bytes) override { queue_.push(bytes); }

    const std::string& topic() const { return topic_; }

private:
    void run() {
        std::string bytes;
        while (queue_.pop(bytes)) {           // close 后队列空时返回 false
            MessageT msg;
            if (!msg.ParseFromString(bytes)) {
                LOG_ERROR("subscriber {} parse failed", topic_);
                continue;
            }
            cb_(msg);
        }
    }

    std::string topic_;
    Callback cb_;
    BlockingQueue<std::string> queue_;
    std::thread worker_;
};

}  // namespace mm
```

- [ ] **Step 4: Build and run the test — expect PASS**

Run: `cmake --build build -j --target test_subscriber && cd build && ctest -R test_subscriber --output-on-failure`
Expected: PASS — 3 tests pass.

- [ ] **Step 5: Commit**

```bash
git add core/include/core/subscriber.h tests/test_subscriber.cpp tests/CMakeLists.txt
git commit -m "feat(core): add Subscriber<T> with worker thread and queue"
```

---

## Task 3: Publisher<T> (rewrite onto LocalBus)

**Files:**
- Modify: `core/include/core/publisher.h` (full rewrite)
- Create: `tests/test_publisher.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `examples/CMakeLists.txt` (remove obsolete `publisher_demo` target)
- Delete: `examples/publisher_demo.cpp` (used the old Transport-based Publisher)

- [ ] **Step 1: Write the failing test**

Create `tests/test_publisher.cpp`:

```cpp
#include "core/publisher.h"
#include "core/local_bus.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace mm;

class FakeSink : public ISink {
public:
    void enqueue(const std::string& bytes) override { received.push_back(bytes); }
    std::vector<std::string> received;
};

TEST(Publisher, SerializesAndPublishesToBus) {
    auto bus = std::make_shared<LocalBus>();
    auto sink = std::make_shared<FakeSink>();
    // 用 StringMsg 的全名订阅,类型要和 Publisher 一致
    bus->subscribe("/chatter", mm::StringMsg::GetDescriptor()->full_name(), sink);

    Publisher<mm::StringMsg> pub("/chatter", bus);
    mm::StringMsg msg;
    msg.set_data("hi");
    EXPECT_TRUE(pub.publish(msg));

    ASSERT_EQ(sink->received.size(), 1u);
    mm::StringMsg parsed;
    ASSERT_TRUE(parsed.ParseFromString(sink->received[0]));
    EXPECT_EQ(parsed.data(), "hi");
}

TEST(Publisher, ReportsTopic) {
    auto bus = std::make_shared<LocalBus>();
    Publisher<mm::StringMsg> pub("/chatter", bus);
    EXPECT_EQ(pub.topic(), "/chatter");
}
```

- [ ] **Step 2: Register the test in CMake and confirm it fails to build**

Modify `tests/CMakeLists.txt` — add inside `if(GTest_FOUND)`:

```cmake
    add_executable(test_publisher test_publisher.cpp)
    target_link_libraries(test_publisher PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_publisher COMMAND test_publisher)
```

Run: `cmake --build build -j --target test_publisher`
Expected: FAIL — compile error: `Publisher<...>` constructor takes a `Transport`, not a `LocalBus` (old `publisher.h`), plus no `GetDescriptor` use matching the new ctor.

- [ ] **Step 3: Rewrite the header**

Replace the entire contents of `core/include/core/publisher.h` with:

```cpp
#pragma once

#include "core/local_bus.h"
#include "common/logger.h"

#include <memory>
#include <string>
#include <utility>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Publisher<T>:某 topic 的发布者。
// publish() 把消息序列化成字节,交给 LocalBus 按 topic 分发。
// 类型名取自 protobuf descriptor,用于 topic 级别的类型一致性检查。
// ═══════════════════════════════════════════════════════════════
template <typename MessageT>
class Publisher {
public:
    Publisher(std::string topic, std::shared_ptr<LocalBus> bus)
        : topic_(std::move(topic)),
          type_name_(MessageT().GetDescriptor()->full_name()),  // 实例方法,非静态
          bus_(std::move(bus)) {
        bus_->register_publisher(topic_, type_name_);
        LOG_INFO("Publisher created: topic={} type={}", topic_, type_name_);
    }

    Publisher(const Publisher&) = delete;
    Publisher& operator=(const Publisher&) = delete;

    bool publish(const MessageT& msg) {
        std::string bytes;
        if (!msg.SerializeToString(&bytes)) {
            LOG_ERROR("serialize failed for topic {}", topic_);
            return false;
        }
        bus_->publish(topic_, type_name_, bytes);
        return true;
    }

    const std::string& topic() const { return topic_; }

private:
    std::string topic_;
    std::string type_name_;
    std::shared_ptr<LocalBus> bus_;
};

}  // namespace mm
```

- [ ] **Step 4: Remove the obsolete publisher_demo**

The old `examples/publisher_demo.cpp` constructs `Publisher` from a `TcpClientTransport`, which no longer compiles. It is superseded by `intra_process_demo` (Task 5).

Delete the file:

```bash
git rm examples/publisher_demo.cpp
```

Modify `examples/CMakeLists.txt` — delete these two lines:

```cmake
add_executable(publisher_demo publisher_demo.cpp)
target_link_libraries(publisher_demo PRIVATE mm_core mm_proto)
```

- [ ] **Step 5: Build and run the test — expect PASS**

Run: `cmake --build build -j --target test_publisher && cd build && ctest -R test_publisher --output-on-failure`
Expected: PASS — 2 tests pass.

- [ ] **Step 6: Commit**

```bash
git add core/include/core/publisher.h tests/test_publisher.cpp tests/CMakeLists.txt examples/CMakeLists.txt
git commit -m "feat(core): rewrite Publisher<T> onto LocalBus; drop obsolete publisher_demo"
```

---

## Task 4: Node (factory + end-to-end)

**Files:**
- Create: `core/include/core/node.h`
- Create: `core/src/node.cpp`
- Create: `tests/test_node.cpp`
- Modify: `core/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_node.cpp`:

```cpp
#include "core/node.h"
#include "messages.pb.h"
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

// 规格 §6:1 个 Pub + 2 个 Sub 同 topic,发 N 条,两个 Sub 各收 N 条且内容一致
TEST(Node, OnePublisherTwoSubscribers) {
    Node node("test_node");

    std::atomic<int> a_count{0}, b_count{0};
    std::string a_last, b_last;

    auto sub_a = node.create_subscriber<mm::StringMsg>(
        "/chatter", [&](const mm::StringMsg& m) { a_last = m.data(); ++a_count; });
    auto sub_b = node.create_subscriber<mm::StringMsg>(
        "/chatter", [&](const mm::StringMsg& m) { b_last = m.data(); ++b_count; });

    auto pub = node.create_publisher<mm::StringMsg>("/chatter");

    const int N = 10;
    for (int i = 0; i < N; ++i) {
        mm::StringMsg m;
        m.set_data("msg" + std::to_string(i));
        ASSERT_TRUE(pub->publish(m));
    }

    for (int i = 0; i < 200 && (a_count.load() < N || b_count.load() < N); ++i)
        std::this_thread::sleep_for(10ms);

    EXPECT_EQ(a_count.load(), N);
    EXPECT_EQ(b_count.load(), N);
    EXPECT_EQ(a_last, "msg9");
    EXPECT_EQ(b_last, "msg9");
}
```

- [ ] **Step 2: Register the test in CMake and confirm it fails to build**

Modify `tests/CMakeLists.txt` — add inside `if(GTest_FOUND)`:

```cmake
    add_executable(test_node test_node.cpp)
    target_link_libraries(test_node PRIVATE
        mm_core
        GTest::gtest_main
    )
    add_test(NAME test_node COMMAND test_node)
```

Run: `cmake --build build -j --target test_node`
Expected: FAIL — `fatal error: core/node.h: No such file or directory`.

- [ ] **Step 3: Write the header**

Create `core/include/core/node.h`:

```cpp
#pragma once

#include "core/local_bus.h"
#include "core/publisher.h"
#include "core/subscriber.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Node(Participant):每进程一个。
// 工厂方法创建 Publisher/Subscriber,并持有它们的生命周期。
// 内部持有一个 LocalBus;Phase 2/3 网络层会接到这同一个 bus 上。
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
        return pub;
    }

    template <typename T>
    std::shared_ptr<Subscriber<T>> create_subscriber(
        const std::string& topic, typename Subscriber<T>::Callback cb) {
        auto sub = std::make_shared<Subscriber<T>>(topic, std::move(cb));
        bus_->subscribe(topic, T().GetDescriptor()->full_name(), sub);  // 实例方法,非静态
        entities_.push_back(sub);
        return sub;
    }

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::shared_ptr<LocalBus> bus_;
    std::vector<std::shared_ptr<void>> entities_;   // 持有 pub/sub 寿命
};

}  // namespace mm
```

- [ ] **Step 4: Write the implementation**

Create `core/src/node.cpp`:

```cpp
#include "core/node.h"
#include "common/logger.h"

namespace mm {

Node::Node(std::string name)
    : name_(std::move(name)), bus_(std::make_shared<LocalBus>()) {
    LOG_INFO("Node created: {}", name_);
}

}  // namespace mm
```

- [ ] **Step 5: Add node.cpp to the core library**

Modify `core/CMakeLists.txt` — change the `add_library` call to include both sources:

```cmake
add_library(mm_core STATIC
    src/local_bus.cpp
    src/node.cpp
)
```

- [ ] **Step 6: Build and run the test — expect PASS**

Run: `cmake --build build -j --target test_node && cd build && ctest -R test_node --output-on-failure`
Expected: PASS — 1 test passes.

- [ ] **Step 7: Commit**

```bash
git add core/include/core/node.h core/src/node.cpp core/CMakeLists.txt tests/test_node.cpp tests/CMakeLists.txt
git commit -m "feat(core): add Node factory with end-to-end pub/sub test"
```

---

## Task 5: Intra-process demo + full verification

**Files:**
- Create: `examples/intra_process_demo.cpp`
- Modify: `examples/CMakeLists.txt`

- [ ] **Step 1: Write the demo**

Create `examples/intra_process_demo.cpp`:

```cpp
#include "core/node.h"
#include "common/logger.h"
#include "messages.pb.h"

#include <chrono>
#include <thread>

int main() {
    mm::Node node("demo_node");

    // 两个订阅者订阅同一个 topic
    auto sub1 = node.create_subscriber<mm::StringMsg>(
        "/chatter", [](const mm::StringMsg& m) {
            LOG_INFO("[sub1] got: {}", m.data());
        });
    auto sub2 = node.create_subscriber<mm::StringMsg>(
        "/chatter", [](const mm::StringMsg& m) {
            LOG_INFO("[sub2] got: {}", m.data());
        });

    auto pub = node.create_publisher<mm::StringMsg>("/chatter");

    for (int i = 0; i < 5; ++i) {
        mm::StringMsg msg;
        msg.set_data("hello " + std::to_string(i));
        pub->publish(msg);
        LOG_INFO("[pub] sent: {}", msg.data());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 给订阅者工作线程一点时间把队列里的消息处理完
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return 0;
}
```

- [ ] **Step 2: Register the demo in CMake**

Modify `examples/CMakeLists.txt` — add:

```cmake
add_executable(intra_process_demo intra_process_demo.cpp)
target_link_libraries(intra_process_demo PRIVATE mm_core)
```

- [ ] **Step 3: Build and run the demo — observe output**

Run: `cmake --build build -j --target intra_process_demo && ./build/examples/intra_process_demo`
Expected: 5 `[pub] sent: hello N` lines, and for each, one `[sub1] got: hello N` and one `[sub2] got: hello N` line (sub ordering between sub1/sub2 may interleave; each must receive all 5).

- [ ] **Step 4: Run the full build and test suite — everything green**

Run: `cmake --build build -j && cd build && ctest --output-on-failure`
Expected: PASS — `test_frame_codec`, `test_local_bus`, `test_subscriber`, `test_publisher`, `test_node` all pass.

- [ ] **Step 5: Commit**

```bash
git add examples/intra_process_demo.cpp examples/CMakeLists.txt
git commit -m "feat(examples): add intra-process pub/sub demo"
```

---

## Self-Review Notes

- **Spec coverage:** §2 components → Tasks 1–4. §3 data flow → Task 4 e2e test. §4.1 ISink seam → Task 1. §4.2 type check → Task 1 (`RejectsTypeMismatch`) + Publisher/Node use of `GetDescriptor()->full_name()`. §4.3 lifecycle/threading → Task 2 (`DestructorJoinsCleanly`), Task 1 (`PrunesExpiredSinks`, lock-out delivery). §6 verification → Tasks 4 & 5.
- **Type consistency:** `ISink::enqueue(const std::string&)`, `LocalBus::{register_publisher, subscribe, publish}`, `Subscriber<T>::Callback`, `Node::{create_publisher, create_subscriber}` are used identically across all tasks.
- **Build green between tasks:** Task 1 switches `mm_core` to compile `local_bus.cpp` (old `publisher.h` stays valid, still header-only and unused by the lib). Task 3 rewrites `publisher.h` and removes the now-broken `publisher_demo` in the same commit so the build never breaks.
