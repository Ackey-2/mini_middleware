#include "common/host_id.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(HostId, NonEmpty) {
    EXPECT_FALSE(local_host_id().empty());
}

TEST(HostId, StableWithinProcess) {
    // 进程内缓存:多次调用返回同一字符串(且为同一地址)
    const std::string& a = local_host_id();
    const std::string& b = local_host_id();
    EXPECT_EQ(a, b);
    EXPECT_EQ(&a, &b);
}
