#include "transport/frame_codec.h"
#include <gtest/gtest.h>

using namespace mm;

// ─────────────────────────────────────────────────────────
// encode 基础
// ─────────────────────────────────────────────────────────

TEST(FrameCodec, EncodeBasic) {
    std::string frame = FrameCodec::encode("hello");
    ASSERT_EQ(frame.size(), 4 + 5);
    // 大端:0x00 0x00 0x00 0x05
    EXPECT_EQ((uint8_t)frame[0], 0x00);
    EXPECT_EQ((uint8_t)frame[1], 0x00);
    EXPECT_EQ((uint8_t)frame[2], 0x00);
    EXPECT_EQ((uint8_t)frame[3], 0x05);
    EXPECT_EQ(frame.substr(4), "hello");
}

// ─────────────────────────────────────────────────────────
// 编码 → 解码,数据一致
// ─────────────────────────────────────────────────────────

TEST(FrameCodec, RoundTrip) {
    std::string buffer;
    buffer += FrameCodec::encode("hello");
    buffer += FrameCodec::encode("world");

    auto msgs = FrameCodec::decode(buffer);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0], "hello");
    EXPECT_EQ(msgs[1], "world");
    EXPECT_EQ(buffer.size(), 0u);   // 全部解析完
}

// ─────────────────────────────────────────────────────────
// 半帧:头部不完整
// ─────────────────────────────────────────────────────────

TEST(FrameCodec, HalfHeader) {
    std::string buffer("\x00\x00", 2);   // ← 第二个参数显式给长度
    auto msgs = FrameCodec::decode(buffer);
    EXPECT_EQ(msgs.size(), 0u);
    EXPECT_EQ(buffer.size(), 2u);
}

// ─────────────────────────────────────────────────────────
// 半帧:头部完整,body 不完整
// ─────────────────────────────────────────────────────────

TEST(FrameCodec, HalfBody) {
    std::string buffer;
    std::string frame = FrameCodec::encode("hello");
    buffer = frame.substr(0, 7);      // 4字节头 + 3字节 body,差 2 字节

    auto msgs = FrameCodec::decode(buffer);
    EXPECT_EQ(msgs.size(), 0u);
    EXPECT_EQ(buffer.size(), 7u);     // 不动,等下次

    // 补齐剩下 2 字节
    buffer += frame.substr(7);
    msgs = FrameCodec::decode(buffer);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0], "hello");
    EXPECT_EQ(buffer.size(), 0u);
}

// ─────────────────────────────────────────────────────────
// 多帧 + 末尾半帧
// ─────────────────────────────────────────────────────────

TEST(FrameCodec, MultiFramesPlusPartial) {
    std::string buffer;
    buffer += FrameCodec::encode("aaa");
    buffer += FrameCodec::encode("bbbb");
    std::string partial = FrameCodec::encode("ccccc");
    buffer += partial.substr(0, 5);    // 只来了一半

    auto msgs = FrameCodec::decode(buffer);
    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0], "aaa");
    EXPECT_EQ(msgs[1], "bbbb");
    EXPECT_EQ(buffer.size(), 5u);      // 留着 ccccc 的开头部分
}

// ─────────────────────────────────────────────────────────
// 大消息
// ─────────────────────────────────────────────────────────

TEST(FrameCodec, LargePayload) {
    std::string big(10000, 'x');
    std::string frame = FrameCodec::encode(big);
    ASSERT_EQ(frame.size(), 4 + 10000);

    auto msgs = FrameCodec::decode(frame);
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(msgs[0].size(), 10000u);
    EXPECT_EQ(msgs[0], big);
}