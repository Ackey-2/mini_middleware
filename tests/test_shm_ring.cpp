#include "transport/shm_ring.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace mm;

namespace {
// 64 字节对齐分配一块裸内存承载 ring(模拟 SHM 段)。
struct AlignedBuf {
    explicit AlignedBuf(size_t bytes) {
        size_t rounded = (bytes + 63) & ~size_t(63);
        ptr = std::aligned_alloc(64, rounded);
    }
    ~AlignedBuf() { std::free(ptr); }
    void* ptr = nullptr;
};
}  // namespace

TEST(ShmRing, SingleWriterSingleReaderRoundTrip) {
    AlignedBuf buf(ShmRing::bytes_needed(8, 128));
    ShmRing ring = ShmRing::init(buf.ptr, 8, 128);
    auto reader = ring.make_reader();   // 在写入前创建 → next_ 从 0 开始

    ASSERT_TRUE(ring.write(std::string("hello")));
    ASSERT_TRUE(ring.write(std::string("world")));

    std::string out;
    ASSERT_TRUE(reader.read(out));
    EXPECT_EQ(out, "hello");
    ASSERT_TRUE(reader.read(out));
    EXPECT_EQ(out, "world");
    EXPECT_FALSE(reader.read(out));   // 无更多数据
}

TEST(ShmRing, MultipleReadersEachSeeAll) {
    AlignedBuf buf(ShmRing::bytes_needed(8, 64));
    ShmRing ring = ShmRing::init(buf.ptr, 8, 64);
    auto r1 = ring.make_reader();
    auto r2 = ring.make_reader();

    for (int i = 0; i < 5; ++i) ring.write(std::string("msg") + std::to_string(i));

    for (auto* r : {&r1, &r2}) {
        std::string out;
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(r->read(out));
            EXPECT_EQ(out, std::string("msg") + std::to_string(i));
        }
        EXPECT_FALSE(r->read(out));
    }
}

TEST(ShmRing, LateReaderOnlySeesFuture) {
    AlignedBuf buf(ShmRing::bytes_needed(8, 64));
    ShmRing ring = ShmRing::init(buf.ptr, 8, 64);
    ring.write(std::string("old"));
    auto reader = ring.make_reader();   // 在 "old" 之后创建
    std::string out;
    EXPECT_FALSE(reader.read(out));     // 不回看历史
    ring.write(std::string("new"));
    ASSERT_TRUE(reader.read(out));
    EXPECT_EQ(out, "new");
}

TEST(ShmRing, OverrunDetectedNotCorrupted) {
    const uint32_t slots = 4;
    AlignedBuf buf(ShmRing::bytes_needed(slots, 32));
    ShmRing ring = ShmRing::init(buf.ptr, slots, 32);
    auto reader = ring.make_reader();

    // 写远多于环容量,不读 → 必然覆盖
    const int total = 100;
    for (int i = 0; i < total; ++i) ring.write(std::to_string(i));

    int got = 0;
    long last = -1;
    std::string out;
    while (reader.read(out)) {
        long v = std::stol(out);
        EXPECT_GT(v, last);   // 读到的序列严格递增(可有间隔)
        last = v;
        ++got;
    }
    EXPECT_GT(reader.dropped(), 0u);                 // 确有丢弃
    EXPECT_LE(got, static_cast<int>(slots));         // 最多读到环容量这么多
    EXPECT_EQ(reader.dropped() + got, total);        // 丢弃 + 读到 = 总写入
    EXPECT_EQ(last, total - 1);                       // 最后一条是最新的
}

TEST(ShmRing, ConcurrentWriterReaderNoTornReads) {
    const uint32_t slots = 16;
    const uint32_t payload = 64;
    AlignedBuf buf(ShmRing::bytes_needed(slots, payload));
    ShmRing ring = ShmRing::init(buf.ptr, slots, payload);
    auto reader = ring.make_reader();

    const int total = 200000;
    std::atomic<bool> corrupt{false};
    std::atomic<int> received{0};

    std::thread writer([&] {
        for (int i = 0; i < total; ++i) {
            // payload 全部字节相同 → 撕裂读会读到不一致字节
            std::string msg(payload, static_cast<char>(i % 251));
            ring.write(msg);
        }
    });

    std::thread consumer([&] {
        std::string out;
        int seen = 0;
        // 写者写满 total 后还要给 reader 收尾时间
        while (seen < total) {
            if (reader.read(out)) {
                ++seen;
                if (out.size() != payload) { corrupt = true; break; }
                char c = out[0];
                for (char ch : out) {
                    if (ch != c) { corrupt = true; break; }   // 撕裂读!
                }
                if (corrupt) break;
            } else if (reader.dropped() + seen >= total) {
                break;   // 写者已写完且没有更多可读
            }
        }
        received = seen;
    });

    writer.join();
    consumer.join();

    EXPECT_FALSE(corrupt.load());                          // 绝不撕裂
    EXPECT_GT(received.load(), 0);                          // 确实收到一些
    EXPECT_EQ(reader.dropped() + received.load(), total);  // 守恒
}
