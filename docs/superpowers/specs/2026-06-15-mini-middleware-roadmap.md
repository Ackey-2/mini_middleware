# mini_middleware 路线图

> 一个 DDS 风格（无 master）的轻量级机器人通信中间件，对标简化版 DDS / Apollo CyberRT。
> 目标代码量约 8000 行，作为校招简历项目。

## 1. 目标架构

去中心化发现 + P2P 数据传输，无中心 broker/master。

```
                    UDP 多播 (发现平面)
   ┌──────────────┐  SPDP/SEDP   ┌──────────────┐
   │   Node A     │◄────────────►│   Node B     │
   │ (Participant)│              │ (Participant)│
   │  ┌────────┐  │              │  ┌────────┐  │
   │  │Pub<T>  │  │              │  │Sub<T>  │  │
   │  └───┬────┘  │              │  └───▲────┘  │
   └──────┼───────┘              └──────┼───────┘
          │     数据平面 (匹配后 P2P 直连)  │
          └──────────────────────────────┘
            同机 → SHM 零拷贝环形队列
            跨机 → TCP (epoll)
```

### 核心概念

- **Node / Participant**：每进程一个，持有一个发现代理（discovery agent），管理本进程所有 Publisher / Subscriber。
- **发现平面**：UDP 多播。节点上线先广播自身（参与者发现，SPDP），再广播自身端点（每个 topic 的 pub/sub + QoS，端点发现 SEDP）。
- **匹配**：当 A 的 `Pub<T>(topic, qos)` 与 B 的 `Sub<T>(topic, 兼容 qos)` 互相发现，自动建立数据通道。
- **数据平面自动选路**：同主机 → 共享内存零拷贝；跨主机 → TCP (epoll)。
- **关键改动**：当前线协议里没有 topic / 端点信息，发现协议和数据帧都需要携带端点标识（Phase 1–2 的核心工作）。

## 2. 现状盘点（约 1337 行）

| 模块 | 已完成 | 状态 |
|------|--------|------|
| `common/logger` | 单例、彩色、fmt 风格占位符 | ✅ 完整 |
| `common/blocking_queue` | mutex+cv 线程安全队列 | ✅ 完整 |
| `transport/frame_codec` | 4 字节大端长度前缀分帧 | ✅ 完整 |
| `transport/tcp_server` | epoll 事件循环、多客户端 | ✅ 基本完整 |
| `transport/tcp_client` | 头文件齐全，`.cpp` 部分实现 | ⚠️ 部分 |
| `core/publisher` | 模板类，序列化 + send | ⚠️ 雏形 |
| `proto/messages` | StringMsg / Point3D / PointCloud | ✅ |

最关键缺口：线协议无 topic 字段、无 Subscriber / Node、无发现机制。

## 3. 分阶段路线图

每阶段单独 spec → plan → 实现，可独立验证。

| # | 阶段 | 内容 | 新增行数(估) | 累计 | 状态 |
|---|------|------|------|------|------|
| 0 | 收尾现有 | 补完 `TcpClientTransport`、修 `Publisher`、`FrameCodec` 测试 | ~300 | 1.6k | ✅ |
| 1 | 核心抽象 + 进程内回环 | `Node`、`Publisher<T>`、`Subscriber<T>`(BlockingQueue+回调线程)、topic 注册表、消息类型注册 | ~1200 | 2.8k | ✅ |
| 2 | 发现协议 | UDP 多播 SPDP/SEDP-lite、参与者/端点广播、topic+QoS 匹配、心跳超时 | ~1400 | 4.2k | ✅ |
| 3 | TCP 数据面 P2P | 连接管理器、按匹配建通道、与发现联动自动连、跨机 pub/sub 跑通 | ~900 | 5.1k | ✅ |
| 4 | 共享内存零拷贝 + 无锁环形队列 ⭐ | SHM 段管理、无锁环形 buffer、同机自动切换 SHM | ~1500 | 6.6k | ✅ |
| 5 | QoS | RELIABLE/BEST_EFFORT、KEEP_LAST N 历史、队列深度、发现期 QoS 协商 | ~500 | 7.1k | ✅ |
| 6 | Service/RPC | 请求-响应、服务发现、client/server 桩 | ~700 | 7.8k | ✅ |
| 7 | CLI 工具 + 配置 | `mm topic list/echo/hz`、YAML 配置加载 | ~900 | 8.7k | ✅ |
| 8 | Benchmark + 测试 + 文档 | TCP vs SHM 延迟/吞吐对比、单测/集成测试、README + 架构图 | ~1200 | 9.9k | |

**8000 行约落在 Phase 6 结束处。** Phase 7–8 是可裁剪/可压缩缓冲区；做到 Phase 5（QoS）即为完整的 DDS-lite。

### 简历关键词覆盖

epoll 事件驱动 · UDP 多播服务发现 · 共享内存零拷贝 · 无锁环形队列 · QoS · Protobuf 序列化 · RPC · CMake 多模块。

## 4. 执行顺序

按阶段顺序推进。每阶段进入前单独 brainstorm 出 spec，再写实现计划（plan），再实现。Phase 1 定义 Node/Pub/Sub 接口，是后续所有阶段的地基，优先详细规划。
