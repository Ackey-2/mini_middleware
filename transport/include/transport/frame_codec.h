#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mm {

class FrameCodec {
public:
    // 头部固定 4 字节(uint32 大端)
    static constexpr size_t HEADER_SIZE = 4;
    static constexpr size_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024;
    // 把 payload 编码成 [4字节长度][payload],返回完整帧
    // 用于 send 前调用
    static std::string encode(const std::string& payload);

    // 从 buffer 里尽可能多地解析出完整帧的 payload
    //
    // 行为:
    //   - 对每条完整帧,把它的 payload 加入返回值
    //   - 已解析的字节从 buffer 头部移除
    //   - 不完整的尾部留在 buffer 里,等下次再 decode
    //
    // 例:buffer = [4B=5][hello][4B=5][world][4B=3][hi
    //              └────消息1───┘└────消息2───┘└──消息3不完整──┘
    //   返回 ["hello", "world"]
    //   buffer 剩 [4B=3][hi
    static std::vector<std::string> decode(std::string& buffer);

private:
    // 工具函数:把 uint32 转成 4 字节大端
    static void write_uint32_be(char* dst, uint32_t value);
    // 工具函数:从 4 字节大端读出 uint32
    static uint32_t read_uint32_be(const char* src);
};

}  // namespace mm