# Phase 2 设计：UDP 多播服务发现

> 所属路线图：[mini_middleware 路线图](2026-06-15-mini-middleware-roadmap.md)
> 前置：[Phase 1 核心抽象](2026-06-15-phase1-core-abstractions-design.md) 已完成(Node/Publisher/Subscriber/LocalBus)。
> 目标：让独立进程里的 Node 通过 UDP 多播自动发现彼此的发布/订阅端点，并报告"匹配"。
> 估算新增约 990 行。

## 0. 背景概念(边做边学)

- **服务发现 (discovery)**：节点事先不知道彼此地址，靠周期性向约定的"多播组"广播自身信息来互相发现。
- **多播 (multicast)**：发往一个特殊组地址(如 `239.255.0.1`)，所有"加入"该组的 socket 都能收到。介于单播(一对一)与广播(整个子网)之间，是发现场景的理想工具。
- **DDS 的 SPDP / SEDP**：真实 DDS 分两步 —— SPDP 发现"参与者(participant)"，SEDP 再发现"端点(谁发布/订阅了哪个 topic)"，且 SEDP 走可靠单播。
- **本阶段的简化(关键设计决策)**：把 SPDP+SEDP **合并成一条周期性多播公告** —— 一条消息里既报"我是谁"，又报"我有哪些 pub/sub"。省掉一整套可靠单播通道，概念照样讲透。代价：端点信息也走多播、消息略大，对本项目规模无影响。

> WSL2 多播可行性已于 2026-06-15 用 smoke test 验证通过(组 `239.255.0.7:7654` 回环收发成功)。
> 必需 socket 选项见 §4.1。

## 1. 目标与非目标

**目标**
- 定义发现消息格式(protobuf)。
- 封装多播 socket(join 组、收、发)。
- `DiscoveryAgent`：周期性公告本节点 + 收远端公告 + 计算匹配 + 存活超时，触发 `on_match`/`on_unmatch`。
- 集成进 `Node`：创建 pub/sub 时自动注册到发现，进程间能自动发现彼此端点。

**非目标(明确推后)**
- 真正的数据传输(Phase 3 TCP / Phase 4 SHM)。Phase 2 只"报告匹配"，不传业务数据。
- QoS 匹配(Phase 5)。本阶段匹配只看 topic + type。
- 可靠的 SEDP 单播通道(本阶段用合并多播公告替代)。

## 2. 组件与职责

| 组件 | 职责 | 依赖 |
|------|------|------|
| `proto/discovery.proto` | 发现消息定义 | protobuf |
| `discovery/udp_multicast.{h,cpp}` | 多播 socket 封装:join/leave、`sendto`、`recvfrom`(带超时) | socket API |
| `discovery/discovery_agent.{h,cpp}` | 本地端点注册表 + 后台线程(周期公告/收包匹配/存活超时) + 匹配回调 | UdpMulticast, discovery.pb |
| `core/node.{h,cpp}` 集成 | 启动时拉起 DiscoveryAgent;`create_publisher/subscriber` 同步注册端点 | DiscoveryAgent |

### 2.1 发现消息格式 (`proto/discovery.proto`)

```proto
syntax = "proto3";
package mm;

message Locator {
    string ip = 1;        // 给 Phase 3 TCP 用的监听地址
    uint32 port = 2;
}

message EndpointInfo {
    enum Kind { PUBLISHER = 0; SUBSCRIBER = 1; }
    Kind kind = 1;
    string topic = 2;
    string type_name = 3;
}

message ParticipantAnnouncement {
    uint64 participant_id = 1;      // 进程内唯一 id(随机生成),用于识别与过滤自身
    string node_name = 2;
    Locator data_locator = 3;       // Phase 3 填真实端口;Phase 2 填配置占位值
    repeated EndpointInfo endpoints = 4;
}
```

### 2.2 接口草图

```cpp
// discovery/udp_multicast.h
namespace mm {
class UdpMulticast {
public:
    UdpMulticast(std::string group, uint16_t port);
    ~UdpMulticast();
    bool open();                                   // 建 socket、设选项、join 组、bind
    void close();
    bool send(const std::string& bytes);           // 多播到组
    // 阻塞收一个数据报,最多等 timeout;收到返回 true 并填 out;超时返回 false
    bool recv(std::string& out, std::chrono::milliseconds timeout);
private:
    std::string group_;
    uint16_t port_;
    int send_fd_ = -1;
    int recv_fd_ = -1;
};
}
```

```cpp
// discovery/discovery_agent.h
namespace mm {
struct MatchInfo {
    EndpointInfo local;        // 本地端点
    EndpointInfo remote;       // 远端端点
    Locator remote_locator;    // 远端 TCP 监听地址(Phase 3 用)
    uint64 remote_participant_id;
};

class DiscoveryAgent {
public:
    using MatchCallback = std::function<void(const MatchInfo&)>;

    DiscoveryAgent(std::string node_name, Locator data_locator,
                   std::string group = "239.255.0.1", uint16_t port = 7400);
    ~DiscoveryAgent();

    // 注册本地端点(Node 在 create_publisher/subscriber 时调用)
    void add_endpoint(EndpointInfo::Kind kind, const std::string& topic,
                      const std::string& type_name);

    void on_match(MatchCallback cb);
    void on_unmatch(MatchCallback cb);

    bool start();                  // 打开 socket,启动后台线程
    void stop();

    uint64_t participant_id() const { return participant_id_; }

private:
    void run();                    // 后台线程主体
    void announce();               // 多播一次本节点公告
    void handle_announcement(const ParticipantAnnouncement& ann);
    void reap_dead();              // 剔除超时参与者,触发 on_unmatch
    void recompute_matches();      // 本地端点 × 远端端点

    // ... 见 §4 状态
};
}
```

## 3. 数据流

```
每 announce_interval(默认 1s):
    agent.announce()
      → 组装 ParticipantAnnouncement(本节点 id/name/locator/所有本地端点)
      → SerializeToString → UdpMulticast.send → 多播到 239.255.0.1:7400

后台线程循环:
    UdpMulticast.recv(timeout=200ms)
      命中 → ParseFromString → handle_announcement:
                participant_id == 自己 ? 丢弃(多播回环)
                : 更新远端表[participant_id] = {endpoints, locator, last_seen=now}
                  → recompute_matches → 新增匹配触发 on_match
      每轮 → 到 announce 时间则 announce()
            → reap_dead():last_seen 超过 liveliness_timeout(默认 5s)的参与者
                          剔除,其涉及的匹配触发 on_unmatch
```

**匹配规则**：本地 `PUBLISHER(topic T, type X)` 匹配远端 `SUBSCRIBER(topic T, type X)`；本地 SUBSCRIBER 匹配远端 PUBLISHER。topic 与 type_name 都相等才算匹配。匹配以 (本地端点, 远端participant_id, 远端端点) 去重，只在"首次出现"时触发 `on_match`，消失时触发 `on_unmatch`。

## 4. 关键设计决策

1. **合并 SPDP/SEDP**：一条 `ParticipantAnnouncement` 同时承载参与者与端点信息(见 §0)。
2. **单后台线程**：`recv` 带 200ms 超时，醒来顺便做"定时公告"与"存活剔除"，无需额外线程或锁同步内部状态。所有内部状态(本地端点表、远端表、回调)只被这一个线程读写;`add_endpoint`/`start`/`stop` 与线程的交互用一把 `std::mutex` 保护本地端点表与 running 标志。
3. **Phase 2 只报匹配、不传数据**：`on_match` 给出远端 locator，作为 Phase 3 建 TCP 连接的输入。这是 Phase 2 → Phase 3 的接缝。
4. **participant_id**：用 `std::random_device` 生成随机 `uint64`。真实 DDS 用 16 字节 GUID;此处简化，够唯一与过滤自身即可。
5. **locator 占位**：Phase 2 没有 TCP server，`data_locator` 填构造时传入的配置值(端口可为 0)。Phase 3 用真实监听端口替换，wire 格式不变。

### 4.1 多播 socket 必需选项(已验证)

- `SO_REUSEADDR`(允许同主机多个 agent 绑同端口,便于测试)
- 接收：bind 到 `INADDR_ANY:port`，再 `IP_ADD_MEMBERSHIP`(`imr_interface = INADDR_ANY`)
- 发送：`IP_MULTICAST_LOOP = 1`(本机回环可收到，单测/同机多节点需要)

## 5. 模块落点

新增：
- `proto/discovery.proto`(并在 `proto/CMakeLists.txt` 的 `PROTO_FILES` 加入)
- `discovery/CMakeLists.txt`、`discovery/include/discovery/udp_multicast.h`、`discovery/src/udp_multicast.cpp`
- `discovery/include/discovery/discovery_agent.h`、`discovery/src/discovery_agent.cpp`
- `tests/test_udp_multicast.cpp`、`tests/test_discovery_agent.cpp`
- `examples/discovery_demo.cpp`

修改：
- 顶层 `CMakeLists.txt`：`add_subdirectory(discovery)`(在 transport 之后、core 之前)。
- `core/CMakeLists.txt`：`mm_core` 链接 `mm_discovery`。
- `core/include/core/node.h`、`core/src/node.cpp`：持有 `DiscoveryAgent`，`create_publisher/subscriber` 时 `add_endpoint`。
- `examples/CMakeLists.txt`、`tests/CMakeLists.txt`：登记新目标。

新建库 `mm_discovery`(static)，依赖 `mm_proto`、`mm_common`、`Threads`。

## 6. 验证标准

- `udp_multicast` 单测:一个实例 send，另一个 recv 收到同样字节。
- `discovery_agent` 核心单测:同进程两个 agent(不同 participant_id、共享组端口)，A `add_endpoint(PUB,/scan,mm.PointCloud)`、B `add_endpoint(SUB,/scan,mm.PointCloud)`，start 后在合理超时内(轮询等待，上限数秒)双方各触发一次 `on_match`，且 `MatchInfo` 的 topic/type/remote_locator/remote_participant_id 正确。
- 存活超时:两 agent 匹配后停掉 A → B 在 `liveliness_timeout` 后触发 `on_unmatch`。
- 不匹配:topic 不同 或 type 不同 → 不触发 `on_match`。
- 自身过滤:单个 agent 同时注册 PUB 与 SUB 同 topic 时，不因收到自己的公告而自我匹配。
- `discovery_demo`:两个进程(或一个进程两 Node)运行，互相打印发现到的对端端点。
- 全量 `cmake --build build -j && (cd build && ctest --output-on-failure)` 绿。
