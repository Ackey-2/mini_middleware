# Phase 1 设计：核心抽象 + 进程内回环 pub/sub

> 所属路线图：[mini_middleware 路线图](2026-06-15-mini-middleware-roadmap.md)
> 目标：不碰网络，先让 `Node` / `Publisher<T>` / `Subscriber<T>` 在单进程内端到端跑通，
> 并定义好后面发现层（Phase 2）和传输层（Phase 3/4）要插入的接缝。
> 估算新增约 1200 行。

## 0. 背景概念（边做边学）

- **pub/sub（发布-订阅）**：发布者不直接调用订阅者，而是把消息发到一个"topic（话题）"上；
  订阅了同一 topic 的人各自收到一份。发布者和订阅者互不知道对方存在，靠 topic 名字解耦。
- **topic**：一个字符串名字（如 `"/scan"`），就是消息的"频道"。
- **序列化（serialize）**：把内存里的对象（这里是 protobuf 消息）转成一串字节，方便存储或传输；
  反序列化（deserialize/parse）是反过程。本项目用 Protobuf 做这件事。
- **本阶段为什么进程内也序列化**：为了让"进程内投递"和"未来网络投递"走同一条字节路径，
  接缝统一。代价是进程内多一次序列化/反序列化，等 Phase 4 共享内存零拷贝时再优化。

## 1. 目标与非目标

**目标**
- `Node`、`Publisher<T>`、`Subscriber<T>` 三个抽象在单进程内跑通。
- 一个进程内总线 `LocalBus` 按 topic 路由消息。
- Subscriber 的回调在自己的工作线程上执行，不阻塞发布者。
- 定义清楚网络层后续插入的两个接缝（见 §4）。

**非目标（明确推后）**
- 任何网络 / UDP 发现 / TCP 传输（Phase 2/3）。
- 共享内存、零拷贝（Phase 4）。
- 运行时类型注册表（按 topic 名动态反序列化）——推到 Phase 7 CLI echo 真正需要时再做。
- QoS（Phase 5）。

## 2. 组件与职责

| 组件 | 职责 | 依赖 |
|------|------|------|
| `Node` | 每进程一个；工厂方法 `create_publisher<T>(topic)` / `create_subscriber<T>(topic, cb)`；持有 `LocalBus`；管理 Pub/Sub 生命周期 | LocalBus |
| `LocalBus` | 进程内总线；按 topic 维护订阅者 sink 列表；`publish(topic, type_name, bytes)` 分发；topic 类型一致性检查 | — |
| `Publisher<T>` | `publish(const T&)`：序列化为字节 → 交给 LocalBus | LocalBus |
| `Subscriber<T>` | 持有 `BlockingQueue<std::string>` + 工作线程；`enqueue(bytes)` 入队；工作线程取出 → 反序列化成 `T` → 调用用户回调 | LocalBus, BlockingQueue |

### 2.1 接口草图

```cpp
// core/local_bus.h
namespace mm {
// 订阅侧落点：任何能接收一帧字节的对象（本地 Subscriber，未来还有远端 sink）
class ISink {
public:
    virtual ~ISink() = default;
    virtual void enqueue(const std::string& bytes) = 0;
};

class LocalBus {
public:
    // 注册一个订阅者到某 topic；type_name 用于一致性检查
    void subscribe(const std::string& topic, const std::string& type_name,
                   std::shared_ptr<ISink> sink);
    void unsubscribe(const std::string& topic, ISink* sink);

    // 发布者标明某 topic 的类型（首次确定该 topic 的类型）
    void register_publisher(const std::string& topic, const std::string& type_name);

    // 分发：把 bytes 投给该 topic 的所有 sink
    void publish(const std::string& topic, const std::string& type_name,
                 const std::string& bytes);
private:
    struct TopicEntry {
        std::string type_name;                          // 该 topic 约定的类型
        std::vector<std::weak_ptr<ISink>> sinks;        // 订阅者们
    };
    std::mutex mtx_;
    std::unordered_map<std::string, TopicEntry> topics_;
};
}
```

```cpp
// core/subscriber.h
template <typename MessageT>
class Subscriber : public ISink {
public:
    using Callback = std::function<void(const MessageT&)>;
    Subscriber(std::string topic, Callback cb);
    ~Subscriber() override;                 // queue_.close(); worker_.join();
    void enqueue(const std::string& bytes) override;   // 入队，立即返回
    const std::string& topic() const;
private:
    void run();                              // 工作线程：pop → parse → cb
    std::string topic_;
    Callback cb_;
    BlockingQueue<std::string> queue_;
    std::thread worker_;
    std::atomic<bool> running_;
};
```

```cpp
// core/node.h
class Node {
public:
    explicit Node(std::string name);
    template <typename T>
    std::shared_ptr<Publisher<T>> create_publisher(const std::string& topic);
    template <typename T>
    std::shared_ptr<Subscriber<T>> create_subscriber(
        const std::string& topic, typename Subscriber<T>::Callback cb);
private:
    std::string name_;
    std::shared_ptr<LocalBus> bus_;
    std::vector<std::shared_ptr<void>> entities_;   // 持有 pub/sub 生命周期
};
```

## 3. 数据流

```
Pub.publish(msg)
  → msg.SerializeToString(&bytes)
  → LocalBus.publish(topic, type_name, bytes)
      → 查 topics_[topic]，遍历每个存活的 sink
      → sink->enqueue(bytes)            [发布线程到此返回，不阻塞]
          ⋮ (Subscriber 工作线程)
      → queue_.pop(bytes)
      → MessageT msg; msg.ParseFromString(bytes)
      → cb_(msg)
```

## 4. 关键设计决策

1. **统一接缝（最重要）**
   - `LocalBus::publish(topic, type_name, bytes)` 就是未来网络层的统一入口。Phase 2/3 中，
     发现层匹配到的"远端订阅者"会实现 `ISink`（其 `enqueue` 把字节通过 TCP/SHM 发出去），
     注册进同一张 topic 表。对 Publisher 而言，本地订阅者和远端订阅者完全透明。
   - `ISink::enqueue(bytes)` 也是网络层"收到一帧"后的落点——Phase 3 里 TCP 收到帧后，
     同样调用本地 Subscriber 的 `enqueue`。
   - **结论**：本地与远端走同一抽象 `ISink`，是整个项目能从单机长成分布式的关键。

2. **类型安全**
   - `Publisher<T>`/`Subscriber<T>` 编译期已知类型；用 `T::GetDescriptor()->full_name()`
     取类型名（如 `"mm.StringMsg"`）。
   - LocalBus 在某 topic 首次注册时记下类型名；后续 pub/sub 在该 topic 上类型名不一致 → `LOG_ERROR` 拒绝。
   - 不做运行时 descriptor 注册表（YAGNI，推到 Phase 7）。

3. **生命周期与线程安全**
   - Pub/Sub 用 `shared_ptr`，由 `Node` 持有并返回给用户。
   - `LocalBus` 用 `weak_ptr<ISink>` 持订阅者，避免 Bus 延长 Subscriber 寿命；分发时 `lock()`，
     失效的顺手清理。
   - `Subscriber` 析构：`queue_.close()`（唤醒阻塞的 pop）+ `worker_.join()`。
   - `LocalBus` 所有方法加一把 `std::mutex`（Phase 1 不追求无锁，正确优先）。

## 5. 模块落点

新增：
- `core/include/core/node.h`、`core/src/node.cpp`
- `core/include/core/local_bus.h`、`core/src/local_bus.cpp`
- `core/include/core/subscriber.h`
- `examples/intra_process_demo.cpp`
- `tests/test_local_bus.cpp`、`tests/test_subscriber.cpp`

修改：
- `core/include/core/publisher.h`：从依赖裸 `Transport` 改为依赖 `LocalBus`。
- `core/CMakeLists.txt`、`examples/CMakeLists.txt`、`tests/CMakeLists.txt`：登记新文件。

## 6. 验证标准

- 单进程内 1 个 Pub + 2 个 Sub 同 topic，发 N 条消息，两个 Sub 各收到 N 条且内容一致、有序。
- 同一 topic 上类型不匹配的 Pub/Sub → 报错并拒绝。
- Subscriber 析构不挂起（队列正确 close、线程正确 join）。
- 单测覆盖：LocalBus 路由、类型一致性检查、Subscriber 队列收发与析构、weak_ptr 失效清理。
- `intra_process_demo` 能跑出可观察的输出（发 X 收 X）。
