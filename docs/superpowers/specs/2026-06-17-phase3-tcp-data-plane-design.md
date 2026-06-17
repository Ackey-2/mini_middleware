# Phase 3 设计：TCP 数据面 P2P

> 所属路线图：[mini_middleware 路线图](2026-06-15-mini-middleware-roadmap.md)
> 前置：[Phase 1 核心抽象](2026-06-15-phase1-core-abstractions-design.md)（Node/Publisher/Subscriber/LocalBus/ISink 接缝）、
> [Phase 2 发现协议](2026-06-15-phase2-discovery-design.md)（DiscoveryAgent 报告匹配，`MatchInfo` 带 `remote_locator`）均已完成。
> 目标：匹配发生后，让发布者把真实业务数据通过 TCP 送到远端进程的订阅者。
> 估算新增约 900 行。

## 0. 背景概念（边做边学）

- **数据面 vs 发现面**：发现面（Phase 2，UDP 多播）只回答"谁在哪、谁要什么 topic"；数据面（本阶段，TCP）负责把真实消息字节从发布者送到订阅者。两者完全解耦。
- **本阶段的接缝来自 Phase 1**：`LocalBus` 里的 `ISink` 抽象。本地 `Subscriber` 是一个 `ISink`；本阶段新增的"远端订阅者代理" `RemoteSink` 也是一个 `ISink`。对 `Publisher` 而言两者透明——它只管 `publish`，`LocalBus` 负责把字节扇出给所有 sink（本地的直接入队，远端的通过 TCP 发出去）。
- **多路复用（multiplexing）**：两个节点间只用**一条** TCP 连接承载它们之间的所有 topic。靠在每帧前加一个 `DataMessage{topic, payload}` 信封（envelope）来区分 topic。这正是路线图里说的"数据帧需要携带端点标识"。

## 1. 目标与非目标

**目标**
- 定义数据面线协议：`DataMessage{topic, payload}` 信封（protobuf），外层仍用 Phase 0 的 `FrameCodec` 4 字节长度前缀分帧。
- 每个 `Node` 启动一个 TCP **数据服务器**（监听一个端口，接受多个对端连接）。
- `RemoteSink`：实现 `ISink`，把发布的字节包成 `DataMessage` 通过 TCP 连接发给远端。
- `DataPlane`：把发现层的 `on_match`/`on_unmatch` 转成"建/拆数据通道"，并把收到的远端数据投递给本地订阅者。
- 集成进 `Node`：用真实监听端口填 `Locator`，匹配后自动连，跨进程把 pub/sub 跑通。

**非目标（明确推后）**
- 自动重连 / 可靠传输 / 历史重发（Phase 5 QoS）。连接建立完成前发布的消息会被丢弃（BEST_EFFORT 语义）。
- 真正的跨主机部署所需的 IP 自动探测。本阶段默认广播 `127.0.0.1`，用两个本机进程（不同临时端口）验证；跨机只需把广播 IP 换成 LAN IP，wire 格式不变。
- SHM 零拷贝（Phase 4）、QoS 协商（Phase 5）。

## 2. 核心数据流

### 2.1 出站（发布 → 线上）

```
Pub.publish(msg)
  → LocalBus.publish(topic, type_name, bytes)
      → 扇出给该 topic 的【本地 sink】：Subscriber.enqueue(bytes)   （进程内，照旧）
      → 扇出给该 topic 的【远端 sink】：RemoteSink.enqueue(bytes)
            → DataMessage{topic, bytes}.SerializeToString
            → TcpClientTransport.send(serialized)   （内部 FrameCodec 加帧头）
```

### 2.2 入站（线上 → 订阅）

```
对端 TCP 帧到达本节点数据服务器(TcpServerTransport)
  → FrameCodec 解出 payload = 序列化的 DataMessage
  → DataPlane 的 on_message 回调:DataMessage.ParseFromString
  → LocalBus.deliver_inbound(topic, payload)
      → 只投给该 topic 的【本地 sink】(Subscriber.enqueue)   ★ 绝不再转发给 RemoteSink
            → Subscriber 工作线程 parse 成 T → 用户回调
```

**关键：环路安全。** 入站投递只走本地 sink，绝不重新走 `publish()`、绝不触达 `RemoteSink`。否则节点会把收到的数据再转发出去形成环路。这要求 `LocalBus` 把"本地 sink"与"远端 sink"分开存。

## 3. 连接方向规则（最重要的设计决策）

一次匹配 `(本地 PUBLISHER ↔ 远端 SUBSCRIBER)` 会被**双方**各自看到（对端看到的是镜像 `(本地 SUBSCRIBER ↔ 远端 PUBLISHER)`）。为避免双方都去连对方、产生重复连接与竞态，规则是：

> **只对"本地端点是 PUBLISHER"的匹配采取连接动作。** 发布方主动连订阅方的数据服务器并写数据；接收方只接受、只读。

推论：
- **无需握手、无需 tie-break。** 接收方甚至不需要知道是谁连进来的——它只按 `DataMessage.topic` 把入站数据路由给本地订阅者。
- **每个远端参与者一条出站连接**（在发布方这侧），复用该节点发往该对端的所有 topic（即所选的"每对端一条多路复用连接"模型）。若两个节点互相发布，则每个方向各一条 socket，仍按 topic 多路复用。
- 对"本地端点是 SUBSCRIBER"的匹配：**不做任何连接动作**（本地订阅者已在 `create_subscriber` 时入了 `LocalBus`，等入站数据即可）。

## 4. 组件与职责

| 组件 | 职责 | 依赖 |
|------|------|------|
| `proto/data.proto` | `DataMessage{string topic=1; bytes payload=2;}` | protobuf |
| `core/remote_sink.h` | `RemoteSink : ISink`，持 `topic` + `shared_ptr<TcpClientTransport>`；`enqueue(bytes)` → 组 `DataMessage` → `transport->send` | LocalBus(ISink), transport, data.pb |
| `core/data_plane.{h,cpp}` | 持数据服务器 + `map<participant_id, shared_ptr<TcpClientTransport>>`；`handle_match`/`handle_unmatch`；入站回调 → `bus->deliver_inbound` | LocalBus, transports, discovery 类型 |
| `core/local_bus.{h,cpp}`（改） | 本地 sink 与远端 sink 分开存；新增 `add_remote_sink`/`remove_remote_sink`/`deliver_inbound`；`publish` 扇出到两者 | — |
| `core/node.{h,cpp}`（改） | 先建 `DataPlane`（拿真实端口）→ 用真实 ip/port 建 `Locator` → 启动 discovery → 把 `on_match`/`on_unmatch` 接到 `DataPlane` | DataPlane, DiscoveryAgent |
| `transport/tcp_server_transport`（小改） | 允许绑端口 `0`（临时端口），`bind` 后 `getsockname` 取实际端口；新增 `uint16_t local_port() const` | — |

### 4.1 接口草图

```cpp
// proto/data.proto
message DataMessage {
    string topic = 1;
    bytes  payload = 2;
}
```

```cpp
// core/remote_sink.h
namespace mm {
class RemoteSink : public ISink {
public:
    RemoteSink(std::string topic, std::shared_ptr<TcpClientTransport> conn);
    void enqueue(const std::string& bytes) override;   // 组 DataMessage → conn->send
private:
    std::string topic_;
    std::shared_ptr<TcpClientTransport> conn_;
};
}
```

```cpp
// core/data_plane.h
namespace mm {
class DataPlane {
public:
    DataPlane(std::shared_ptr<LocalBus> bus, std::string advertise_ip);
    ~DataPlane();
    bool start();                       // 启动数据服务器(临时端口)
    void stop();
    uint16_t server_port() const;       // 供 Node 填 Locator
    const std::string& advertise_ip() const;

    // 发现层匹配回调(在 discovery 后台线程触发)
    void handle_match(const MatchInfo& m);
    void handle_unmatch(const MatchInfo& m);

private:
    void on_inbound(const std::string& payload);   // 数据服务器收到一帧
    std::shared_ptr<TcpClientTransport> connection_for(uint64_t pid,
                                                        const Locator& loc);

    std::shared_ptr<LocalBus> bus_;
    std::string advertise_ip_;
    std::unique_ptr<TcpServerTransport> server_;

    std::mutex mtx_;
    std::map<uint64_t, std::shared_ptr<TcpClientTransport>> connections_;
    // 已建立的 RemoteSink：key=match_key，value=(topic, sink) 供 unmatch 时撤销
    std::map<std::string, std::shared_ptr<RemoteSink>> sinks_;
    // 每个远端参与者当前还有几个活跃 PUB 匹配,降到 0 时可关连接
    std::map<uint64_t, int> refcount_;
};
}
```

```cpp
// core/local_bus.h 变化(节选)
void add_remote_sink(const std::string& topic, std::shared_ptr<ISink> sink);
void remove_remote_sink(const std::string& topic, ISink* sink);
// 只投给本地订阅者(入站路径用),不触达远端 sink
void deliver_inbound(const std::string& topic, const std::string& bytes);
```

## 5. Node 集成与析构顺序

构造流程（顺序关键，因为 Locator 必须在第一条公告前就带真实端口）：
1. 建 `LocalBus`。
2. 建 `DataPlane(bus_, "127.0.0.1")` 并 `start()` → 取 `server_port()`。
3. 用真实 `ip:port` 构造 `Locator`，建 `DiscoveryAgent`。
4. 注册 `discovery_->on_match([dp](m){ dp->handle_match(m); })`、`on_unmatch(...)`。
5. `discovery_->start()`。

成员**声明顺序** `name_, bus_, data_plane_, discovery_, entities_`，**逆序析构**：
`entities_`（订阅者停线程）→ `discovery_`（停后台线程，**不再有匹配回调**）→ `data_plane_`（停服务器与所有出站连接，释放 RemoteSink）→ `bus_`。
discovery 必须先于 data_plane 析构，否则晚到的匹配回调可能触达正在销毁的 `DataPlane`。

## 6. 关键设计决策小结

1. **复用 ISink 接缝**：RemoteSink 与本地 Subscriber 对 Publisher 完全透明（Phase 1 预留）。
2. **每对端一条多路复用连接 + DataMessage 信封**：对标真实 DDS；topic 写进信封而非靠"一连接一 topic"。
3. **发布方主动连、接收方只读**：消除握手/tie-break/重复连接，接收方无状态。
4. **LocalBus 本地/远端 sink 分离 + `deliver_inbound`**：杜绝转发环路。
5. **复用现有 transport**：数据服务器=`TcpServerTransport`，出站=`TcpClientTransport`，本阶段主要是"把现有传输接到发现层"。
6. **临时端口**：数据服务器绑 `0`，`getsockname` 回填真实端口写进 Locator。

## 7. 模块落点

新增：
- `proto/data.proto`（并加入 `proto/CMakeLists.txt` 的 `PROTO_FILES`）
- `core/include/core/remote_sink.h`
- `core/include/core/data_plane.h`、`core/src/data_plane.cpp`
- `examples/tcp_pubsub_demo.cpp`
- `tests/test_data_message.cpp`（信封编解码）、`tests/test_data_plane.cpp`（loop-safety / 入站投递）、`tests/test_tcp_pubsub.cpp`（两 Node 端到端集成）

修改：
- `core/include/core/local_bus.h`、`core/src/local_bus.cpp`：sink 分离 + `deliver_inbound`。
- `core/include/core/node.h`、`core/src/node.cpp`：持 `DataPlane`，构造顺序与回调接线。
- `transport/include/transport/tcp_server_transport.h`、`transport/src/tcp_server_transport.cpp`：临时端口 + `local_port()`。
- `core/CMakeLists.txt`：加 `src/data_plane.cpp`。
- `examples/CMakeLists.txt`、`tests/CMakeLists.txt`：登记新目标。

`mm_core` 已链接 `mm_transport`、`mm_discovery`、`mm_proto`，无需新增库依赖。

## 8. 验证标准

- **信封单测**：`DataMessage{topic,payload}` 序列化→反序列化 round-trip，字段一致。
- **loop-safety 单测**：`LocalBus` 同一 topic 注册一个本地 sink 和一个远端 sink；`deliver_inbound` 只命中本地 sink，不命中远端 sink；`publish` 两者都命中。
- **DataPlane 单测**：构造一个 DataPlane，模拟一次 `handle_match`（本地 PUB ↔ 远端 SUB，remote_locator 指向另一个本机 DataPlane 的服务器），发布字节，远端 DataPlane 的 `deliver_inbound` 收到正确 topic+payload；`handle_unmatch` 后 RemoteSink 撤销、（引用计数归零时）连接关闭。
- **端到端集成测试**：同进程两个 `Node`（共享多播组、各自临时数据端口），A `create_publisher<StringMsg>("/chat")`，B `create_subscriber<StringMsg>("/chat", cb)`；A 周期性发布，B 在合理超时内收到 ≥N 条且内容正确。
- **跨进程 demo**：`tcp_pubsub_demo talker` 与 `tcp_pubsub_demo listener` 两进程，listener 打印收到的消息。
- 全量 `cmake --build build -j && (cd build && ctest --output-on-failure)` 绿。
