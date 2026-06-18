# Phase 5 设计：QoS（RELIABLE/BEST_EFFORT · KEEP_LAST N · 发现期协商）

> 所属路线图：[mini_middleware 路线图](2026-06-15-mini-middleware-roadmap.md)
> 前置：Phase 1–4 已完成并合入 main（LocalBus/ISink、UDP 发现、TCP 数据面、SHM 零拷贝）。当前 20/20 测试绿。
> 目标：引入 DDS 的 QoS（可靠性 + 历史深度），发现期协商兼容性，并把可靠性纳入数据面选路。估算 ~500 行。

## 0. 背景概念

- **QoS（服务质量）**：发布/订阅各自声明策略；发现期按 DDS 的 RxO（Requested ≤ Offered）协商，只有兼容才建通道。
- 现状全是 **BEST_EFFORT**：TCP 连上前 / SHM 环满都会丢；订阅队列无界；匹配只看 topic+type。

## 1. 目标与非目标

**目标**
- `Qos` 策略类型：`Reliability{BEST_EFFORT,RELIABLE}`、`History{KEEP_LAST,KEEP_ALL}`、`depth`。
- 发现期可靠性协商（RxO），不兼容则不匹配 + 告警。
- KEEP_LAST N：订阅队列有界，满时丢最旧。
- 可靠性纳入选路：RELIABLE → TCP（即使同机），BEST_EFFORT 同机 → SHM。
- Pub/Sub/Node API 带 QoS，向后兼容（默认 BEST_EFFORT/KEEP_LAST16）。

**非目标（明确推后）**
- **RELIABLE 的历史重发 / ACK / NACK**（真正零丢失，含连接前）。本阶段 RELIABLE = “经可靠传输（TCP）有序不丢”，
  连接建立前发布的消息仍可能丢（留作 Phase 5.1）。
- History/depth 的线上协商（它们是本地策略，与 DDS 一致，不上线）。
- 其它 QoS 策略（Deadline/Lifespan/Ownership 等）。

## 2. 关键设计决策

1. **RELIABLE = 可靠传输语义**：RELIABLE 的一对 pub/sub 强制走 TCP（有序不丢），即使同机也不用有损 SHM。
   把 QoS 与 Phase 4 选路优雅衔接：`use_shm = same_host && 订阅者 BEST_EFFORT`。
2. **不兼容则不匹配 + 告警**：RxO——writer 提供的可靠性 ≥ reader 请求的才兼容。唯一不兼容：reader RELIABLE × writer BEST_EFFORT。
3. **只有可靠性参与协商**：History/depth 本地化。EndpointInfo 仅新增 `reliability` 上线。
4. **有效可靠性看订阅者**：通道能否走 SHM 取决于 reader 请求的可靠性（reader RELIABLE → 必须 TCP）。
5. **向后兼容**：proto 加字段默认 0=BEST_EFFORT；所有 `create_*`/`add_endpoint` 默认 QoS，行为不变。

## 3. 组件与职责

| 组件 | 职责 |
|------|------|
| `common/qos.h`（新） | `Qos` + `compatible(offered_writer, requested_reader)`（header-only） |
| `proto/discovery.proto`（改） | `EndpointInfo.reliability`（0/1） |
| `discovery/endpoint_matcher.cpp`（改） | topic+type 基础上加 RxO 检查；不兼容跳过并 LOG_WARN |
| `discovery/discovery_agent.{h,cpp}`（改） | `add_endpoint(..., reliability=0)`；announce 写入 |
| `common/blocking_queue.h`（改） | 可选容量 + 满时丢最旧（KEEP_LAST N）+ `dropped()` |
| `core/subscriber.h`（改） | `Subscriber(topic, cb, Qos)`；按 history+depth 建队列 |
| `core/publisher.h`（改） | `Publisher(topic, bus, Qos)`；存 Qos |
| `core/node.h`（改） | `create_publisher/subscriber(topic[, cb], Qos={})`；传 reliability 到发现层 |
| `core/data_plane.{h,cpp}`（改） | `use_shm(m) = same_host(m) && reader BEST_EFFORT`；选路改用 use_shm |

## 4. RxO 真值表

| writer(offered) | reader(requested) | 兼容? | 通道 |
|---|---|---|---|
| BEST_EFFORT | BEST_EFFORT | ✅ | 同机 SHM / 跨机 TCP |
| RELIABLE | BEST_EFFORT | ✅ | 同机 SHM / 跨机 TCP |
| RELIABLE | RELIABLE | ✅ | TCP（即使同机） |
| BEST_EFFORT | RELIABLE | ❌ | 不匹配 |

## 5. 验证标准

- `test_qos`：compatible 真值表。
- `test_blocking_queue_capacity`：KEEP_LAST 丢最旧、深度 1 留最新、容量 0 无界。
- `test_qos_match`：不兼容对不产出 MatchInfo；兼容对照常；角色解析与本地端无关。
- `test_qos_routing`：同机 BEST_EFFORT → 建 SHM 段；同机 RELIABLE → 不建（走 TCP）；stop 后段 unlink。
- `test_qos_pubsub`：RELIABLE 两 Node 端到端可达；不兼容对零投递。
- 既有 20 测试保持绿（向后兼容）。全量 `ctest` 25/25。

## 6. 模块落点

新增：`common/include/common/qos.h`、`examples/qos_demo.cpp`、
测试 `test_qos`/`test_blocking_queue_capacity`/`test_qos_match`/`test_qos_routing`/`test_qos_pubsub`。
修改：`proto/discovery.proto`、`discovery/*`、`common/blocking_queue.h`、`core/{publisher,subscriber,node,data_plane}.*`、各 CMake。
