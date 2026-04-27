# mini_middleware

一个轻量级机器人通信中间件,基于共享内存 + Socket 的 pub/sub 框架。
对标 Apollo CyberRT / 简化版 DDS。

## 功能特性

- [ ] TCP 传输层(基于 epoll)
- [ ] 共享内存零拷贝传输
- [ ] 无锁环形队列
- [ ] Protobuf 序列化
- [ ] QoS 策略(可靠性、历史深度等)
- [ ] 节点发现

## 构建

依赖:
- C++17 编译器
- CMake >= 3.15
- Protobuf

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## 运行

```bash
# 终端 1
./examples/subscriber_demo

# 终端 2
./examples/publisher_demo
```

## 项目结构

- `common/`  - 通用工具(日志、队列等)
- `proto/`   - 消息定义
- `transport/` - 传输层(TCP/SHM)
- `core/`    - Pub/Sub/Node 抽象
- `examples/` - 示例程序

## 开发日志

- Day 6: 项目骨架搭建,CMake 多模块构建打通 ✅