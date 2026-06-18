#include "transport/shm_segment.h"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <string>

using namespace mm;

namespace {
// 每个测试用唯一段名,避免并行/残留干扰。
std::string seg_name(const char* tag) {
    return std::string("/mm.test.") + tag + "." + std::to_string(::getpid());
}
}  // namespace

TEST(ShmSegment, CreateThenOpenAndTransfer) {
    auto name = seg_name("xfer");
    auto writer = ShmSegment::create(name, 8, 128);
    ASSERT_NE(writer, nullptr);

    auto reader_seg = ShmSegment::open(name);
    ASSERT_NE(reader_seg, nullptr);
    auto reader = reader_seg->ring().make_reader();

    ASSERT_TRUE(writer->ring().write(std::string("payload-1")));
    ASSERT_TRUE(writer->ring().write(std::string("payload-2")));

    std::string out;
    ASSERT_TRUE(reader.read(out));
    EXPECT_EQ(out, "payload-1");
    ASSERT_TRUE(reader.read(out));
    EXPECT_EQ(out, "payload-2");
}

TEST(ShmSegment, OpenMissingReturnsNull) {
    EXPECT_EQ(ShmSegment::open("/mm.test.does.not.exist.12345"), nullptr);
}

TEST(ShmSegment, OwnerUnlinksOnDestruction) {
    auto name = seg_name("unlink");
    {
        auto writer = ShmSegment::create(name, 4, 64);
        ASSERT_NE(writer, nullptr);
        EXPECT_NE(ShmSegment::open(name), nullptr);   // 存活期间可打开
    }
    // 创建者析构后 shm_unlink:名字应消失
    EXPECT_EQ(ShmSegment::open(name), nullptr);
}
