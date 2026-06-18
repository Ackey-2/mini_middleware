#include "common/qos.h"

#include <gtest/gtest.h>

using namespace mm;
using R = mm::Qos::Reliability;

TEST(Qos, Defaults) {
    Qos q;
    EXPECT_EQ(q.reliability, R::BEST_EFFORT);
    EXPECT_EQ(q.history, Qos::History::KEEP_LAST);
    EXPECT_EQ(q.depth, 16u);
}

// RxO 真值表:writer(offered) ≥ reader(requested) 才兼容。
TEST(Qos, CompatibilityTruthTable) {
    // reader BEST_EFFORT:任何 writer 都兼容
    EXPECT_TRUE(Qos::compatible(R::BEST_EFFORT, R::BEST_EFFORT));
    EXPECT_TRUE(Qos::compatible(R::RELIABLE, R::BEST_EFFORT));
    // reader RELIABLE:只有 RELIABLE writer 兼容
    EXPECT_TRUE(Qos::compatible(R::RELIABLE, R::RELIABLE));
    EXPECT_FALSE(Qos::compatible(R::BEST_EFFORT, R::RELIABLE));   // 唯一不兼容
}
