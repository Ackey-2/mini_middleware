#pragma once

#include <functional>
#include <string>
#include <cstdint>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// Transport:传输层抽象接口
// 
// 所有传输实现(TCP / SHM / UDP)都遵守这个接口。
// 上层 Publisher/Subscriber 只依赖此接口,不关心底层。
// ═══════════════════════════════════════════════════════════════

class Transport {
public:
    // 收到一条完整消息时的回调
    // payload 是已经"出帧"的纯消息体(由 FrameCodec 解码出来的)
    using MessageCallback = std::function<void(const std::string& payload)>;
    
    virtual ~Transport() = default;

    // 启动传输层。成功返回 true。
    // 启动后开始监听/接收数据,触发 MessageCallback。
    virtual bool start() = 0;

    // 优雅停止。释放资源,join 后台线程。
    virtual void stop() = 0;

    // 注册消息回调。多次调用会覆盖。
    virtual void on_message(MessageCallback cb) = 0;

    // 发送一条消息(已经是 payload,由调用方序列化好)
    // 内部会自动加帧头。
    // TCP 服务端这种"被动接收"的角色可能不需要 send,
    // 这个接口主要供客户端 Transport 实现。
    virtual bool send(const std::string& payload) = 0;
};

}  // namespace mm