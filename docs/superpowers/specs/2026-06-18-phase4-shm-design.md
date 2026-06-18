# Phase 4 设计:共享内存零拷贝 + 无锁环形队列 ⭐

> 所属路线图：[mini_middleware 路线图](2026-06-15-mini-middleware-roadmap.md)
> 前置：[Phase 1 核心抽象](2026-06-15-phase1-core-abstractions-design.md)（Node/Pub/Sub/LocalBus/ISink 接缝）、
> [Phase 2 发现协议](2026-06-15-phase2-discovery-design.md)（DiscoveryAgent 报告匹配）、
> [Phase 3 TCP 数据面](2026-06-17-phase3-tcp-data-plane-design.md)（匹配后发布方主动连、DataMessage 信封）均已完成。
> 目标：同主机的一对 pub/sub 不再绕 TCP/内核，而是通过**共享内存的无锁环形队列**直接传字节；跨主机仍走 TCP。
> 估算新增约 1500 行。

## 0. 背景概念（边做边学）

- **零拷贝（zero-copy）**：TCP 路径下一条消息要经历 用户态→内核 socket 缓冲→内核→对端用户态 两次拷贝外加协议栈。
  共享内存把同一段物理内存映射进两个进程的地址空间，发布者把序列化字节 `memcpy` 进环形槽位（一次拷贝），订阅者直接从该槽位读出——**跨进程不再经内核**。这是「零拷贝」的标准含义（端到端仍有一次序列化拷贝，诚实表述）。
- **无锁环形队列（lock-free ring）**：固定大小的槽位数组 + 单调递增序列号。写者用原子操作发布、永不阻塞；多个读者各持游标、靠序列号检测「被覆盖/撕裂读」。无互斥锁，跨进程也成立（依赖 lock-free 的 64 位原子）。
- **同机判定**：现 `Locator` 只有 ip+port（且硬编码 127.0.0.1），无法区分同机/跨机。引入机器级 `host_id`，发现公告携带，两端相等即同主机。

## 1. 目标与非目标

**目标**
- 无锁 SPMC 环形队列 `ShmRing`，放置在裸内存上（可堆可 SHM），可独立单测。
- POSIX 共享段 RAII `ShmSegment`（`shm_open`/`ftruncate`/`mmap`/`shm_unlink`）。
- 同机判定：发现层新增 `host_id`，贯通到 `MatchInfo.remote_host_id`。
- 数据面自动选路：同机 → SHM，跨机 → TCP（Phase 3 不变）。
- 集成进 `Node`，两进程同机 pub/sub 经 SHM 跑通。

**非目标（明确推后）**
- 真正的跨主机 IP 探测（沿用 Phase 3，默认 127.0.0.1）。
- 可靠传输 / 历史重发 / QoS 协商（Phase 5）。SHM 维持 BEST_EFFORT：环满覆盖、超槽位丢弃、晚加入的订阅者只看未来。
- 用户级零拷贝 API（直接在槽位上构造消息）。本阶段仍是「序列化成 string → 写槽位」。
- 大消息分片：超过槽位容量者丢弃 + 告警。

## 2. 关键设计决策（相对前几阶段的不变量变化）

1. **打破 Phase 3「只有 PUBLISHER 端动作」规则——仅限 SHM 路径。**
   TCP 时接收方无状态、只读 socket。但 SHM 的读者必须**主动**打开共享段、自己轮询读取。
   因此同机匹配时**双方都要动作**：PUB 端建 writer ring + `ShmSink`；SUB 端开 reader ring + 轮询投递。跨机匹配维持原规则。

2. **同机判定靠 `host_id`**（取自 `/etc/machine-id`，回退 `gethostname()`，进程内缓存）。
   `DataPlane` 比较本地 host_id 与 `MatchInfo.remote_host_id`，相等且启用 SHM 即走共享内存。

3. **SHM 段命名是确定式的，双方无需握手即可算出同一个名字。**
   段名 = `seg_name(publisher_participant_id, topic)`。PUB 端用自己的 id，SUB 端用 `remote_participant_id`，两端一致。
   每个 `(publisher, topic)` 一个 ring（单写多读），天然支持同机多个本地订阅者复用一段。

4. **每段单 topic，故不需要 DataMessage 信封**（topic 已隐含在段名里）。槽位直接存用户序列化字节。

5. **诚实的零拷贝**：发布者 `memcpy` 进槽位一次，但跨进程不经内核。

## 3. 无锁环形队列设计（核心）

布局放在裸内存上（`transport/shm_ring.h`，header-only）：

```
Header { atomic<u32> magic; u32 slot_count; u32 slot_size; ...; atomic<u64> write_seq; }
Slot   { atomic<u64> seq; u32 len; char data[slot_size]; }   // slot_count 个,cache-line 对齐
```

**写**（`ShmRing::write`，永不阻塞）：
1. `len > slot_size` → 返回 false（调用方告警丢弃）。
2. `pos = write_seq.fetch_add(1)`；`idx = pos & (slot_count-1)`。
3. `slot.seq = pos | WRITING_BIT`（release，占用,旧数据立即失效）。
4. 写 `len`、`memcpy` data。
5. `slot.seq = pos`（release，发布就绪）。

**读**（`ShmRingReader`，每读者私有 `next_`，创建时 = 当前 write_seq → 只看未来）：
- `w = write_seq`（acquire）；`next_ >= w` → 无新数据。
- `w - next_ > slot_count` → overrun，快进到 `w - slot_count + 1`（留 1 槽余量避开正被写的最旧槽），丢失计入 `dropped`。
- `s1 = slot.seq`（acquire）；低位 ≠ next_ → 被覆盖,丢弃快进;带 WRITING → 正在写,返回 false。
- 拷贝 data 后再读 `s2 = slot.seq`；≠ next_ → 撕裂读,丢弃。否则成功,`next_++`。

这是 seqlock 风格的 SPMC ring，正确处理覆盖与撕裂读，是本阶段技术亮点。

## 4. 组件与职责

| 组件 | 职责 | 依赖 |
|------|------|------|
| `common/host_id.{h,cpp}` | `local_host_id()`：机器级标识，进程内缓存 | — |
| `transport/shm_ring.h` | header-only 无锁 SPMC ring：`ShmRing`(写) + `ShmRingReader`(读) | atomic |
| `transport/shm_segment.{h,cpp}` | POSIX 共享段 RAII：`create`(写者,析构 unlink) / `open`(读者,重试友好) | librt |
| `core/shm_sink.h` | `ShmSink : ISink`，publish 字节 → ring.write；超容量告警丢弃 | ShmSegment |
| `core/shm_reader.{h,cpp}` | `ShmReaderManager`：一个 poller 线程轮询所有 reader ring → `bus->deliver_inbound`；段未就绪重试 open；空闲自适应退避 | ShmSegment, LocalBus |
| `core/data_plane.{h,cpp}`（改） | `set_local_identity`；同机走 SHM(PUB 建写者段/SUB 开读者)、跨机走 TCP；按 topic/段名 refcount 撤销 | 全部 |
| `core/node.{h,cpp}`（改） | `Node(name, enable_shm=true)`；discovery 建好后 `set_local_identity(pid, host_id, enable_shm)` | DataPlane |
| 发现层（改） | `discovery.proto` 加 `host_id`；announce/Remote/try_match 回填 `MatchInfo.remote_host_id` | — |

## 5. 数据流（同机）

出站：`Pub.publish → LocalBus.publish → ShmSink.enqueue(bytes) → ring.write(bytes)`
入站：`ShmReaderManager poller → ring.read() → bus->deliver_inbound(topic, bytes) → Subscriber 回调`

环路安全沿用 Phase 3：入站只走 `deliver_inbound`（本地 sink），绝不触达任何 remote sink。

## 6. DataPlane 选路与生命周期

- `handle_match`：本地 PUB + 同机 → `shm_pub_match`（per-topic 写者段,refcount 复用）；本地 PUB + 跨机 → TCP（不变）；
  本地 SUB + 同机 → `shm_sub_match`（per-段 reader,refcount）；本地 SUB + 跨机 → 不动作。
- `handle_unmatch`：对称撤销。PUB-SHM refcount 归零 → 撤 ShmSink + 段 unlink；SUB-SHM refcount 归零 → remove_reader。
- 析构 / `stop()`：清写者段（unlink）→ 锁外停 poller 线程（避免与 poll 持锁互等）→ 停 TCP server/连接。
- 锁序：DataPlane mtx_ → ShmReaderManager mtx_；poller：ShmReaderManager mtx_ → LocalBus mtx_。无环。

## 7. 风险与权衡

- **段泄漏**：进程被 kill -9 来不及 unlink，`/dev/shm` 残留。`create` 用 `O_CREAT`(非 EXCL) 覆盖式重用并重初始化以容忍；demo 注明 `rm -f /dev/shm/mm.*`。
- **reader 早于 writer**：poller 周期性重试 `open` 直至段就绪，不报错。
- **晚加入订阅者**：只看连上后的未来消息（BEST_EFFORT），故单测/demo 周期性发布。
- **同进程双 Node**：host_id 相同,e2e 自然走 SHM；TCP 回归靠 `enable_shm=false` 显式锁定。

## 8. 模块落点

新增：`common/host_id.{h,cpp}`、`transport/shm_ring.h`、`transport/shm_segment.{h,cpp}`、
`core/shm_sink.h`、`core/shm_reader.{h,cpp}`、`examples/shm_pubsub_demo.cpp`、
测试 `test_host_id`/`test_shm_ring`/`test_shm_segment`/`test_shm_sink_reader`/`test_shm_pubsub`。

修改：`proto/discovery.proto`、`discovery/endpoint_matcher.h`、`discovery/discovery_agent.{h,cpp}`、
`core/data_plane.{h,cpp}`、`core/node.{h,cpp}`、各 `CMakeLists.txt`（transport 链 `rt`）、`tests/test_data_plane.cpp`、`tests/test_tcp_pubsub.cpp`（关 SHM）、`tests/test_discovery_agent.cpp`（host_id 断言）。

## 9. 验证标准

- **ring 单测**：round-trip、多读者各读全量、晚加入只看未来、overrun 检测守恒、并发 20 万条无撕裂读。
- **segment 单测**：create→open 跨句柄读写、open 缺失返回 null、owner 析构后 unlink。
- **sink/reader 单测**：sink→ring→manager→LocalBus 投递；reader 段后建重试；超容量丢弃。
- **discovery 单测**：同机两 agent 匹配带 `remote_host_id` == `local_host_id()`。
- **DataPlane 单测**：同机两 plane 经 SHM 投递、unmatch 后撤销;跨机仍 TCP;SUB 侧匹配跨机不动作。
- **端到端**：两同机 Node 经 SHM 收到 ≥1 条且内容正确;TCP 路径(关 SHM)回归绿。
- **跨进程 demo**：listener 打印 talker 消息;`ls /dev/shm` 见 `mm.*`,正常退出后消失。
- 全量 `cmake --build build -j && (cd build && ctest --output-on-failure)` 绿（20 测试）。
