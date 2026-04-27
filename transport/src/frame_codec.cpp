#include "transport/frame_codec.h"
#include <cstring>

namespace mm {

// ═══════════════════════════════════════════════════════════════
// 工具函数:大端字节序读写
// ═══════════════════════════════════════════════════════════════

void FrameCodec::write_uint32_be(char* dst, uint32_t value) {
    // TODO: 把 value 写成 4 字节大端到 dst  
    // 1. 右移 24 位，把 0x12 推到最右边，然后与 0xFF 掩码做与运算，提取出来
    dst[0] = static_cast<char>((value >> 24) & 0xFF); 
    
    // 2. 右移 16 位，把 0x34 推到最右边提取出来
    dst[1] = static_cast<char>((value >> 16) & 0xFF);
    
    // 3. 右移 8 位，提取 0x56
    dst[2] = static_cast<char>((value >> 8)  & 0xFF);
    
    // 4. 不移位，直接提取最末尾的 0x78
    dst[3] = static_cast<char>(value & 0xFF);
}

uint32_t FrameCodec::read_uint32_be(const char* src) {

    uint32_t value=0;
    for(int i=0;i<4;i++){
        value |= static_cast<uint32_t>(static_cast<uint8_t>(src[i])) << (8 * (3 - i));
    }

    return value;
}

// ═══════════════════════════════════════════════════════════════
// encode:简单
// ═══════════════════════════════════════════════════════════════

std::string FrameCodec::encode(const std::string& payload) {
    if (payload.size() > MAX_PAYLOAD_SIZE) {
        // 调用方传了太大的消息,这是 bug
        return {};   // 或者扔异常
    }
    std::string frame;
    frame.resize(HEADER_SIZE + payload.size());
    write_uint32_be(&frame[0], static_cast<uint32_t>(payload.size()));
    std::memcpy(&frame[HEADER_SIZE], payload.data(), payload.size());
    return frame;
}

// ═══════════════════════════════════════════════════════════════
// decode:今天的核心 ⭐
// ═══════════════════════════════════════════════════════════════

std::vector<std::string> FrameCodec::decode(std::string& buffer) {
    std::vector<std::string> messages;

    // TODO: 你来写
    //
    // 算法:
    //   while (buffer 里至少有 HEADER_SIZE 字节) {
    //       读 length = read_uint32_be(buffer 开头)
    //       if (buffer 总长 < HEADER_SIZE + length) {
    //           // 还没收齐这条消息的完整 body,等下次
    //           break;
    //       }
    //       提取 payload(buffer[HEADER_SIZE .. HEADER_SIZE+length])
    //       加入 messages
    //       从 buffer 头部移除 HEADER_SIZE + length 字节
    //   }
    //
    // 注意:
    //   1. 必须用 while 不是 if —— 一次调用可能取出多条消息
    //   2. buffer.erase(0, n) 可以从头部删 n 字节(简单但低效,后面优化)
    //   3. 别忘了边界:buffer 太短直接 return 空

    while(buffer.size()>=HEADER_SIZE){
        uint32_t length = read_uint32_be(&buffer[0]);
        if (length > MAX_PAYLOAD_SIZE) {
            // 这是攻击或协议损坏。清空 buffer,断开是上层的责任
            buffer.clear();
            // 也可以扔异常,这里先用最简单的处理
            break;
        }
        if(buffer.size()<HEADER_SIZE+length){

            break;
        }
        messages.push_back(buffer.substr(HEADER_SIZE,length));
        buffer.erase(0,length+HEADER_SIZE);
    }
    
    return messages;
}

}  // namespace mm