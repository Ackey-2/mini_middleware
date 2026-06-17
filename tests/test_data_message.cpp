#include "data.pb.h"
#include <gtest/gtest.h>
#include <string>

using namespace mm;

TEST(DataMessage, RoundTrip) {
    std::string payload("\x01\x02\x03", 3);   // 含 NUL 的原始字节
    payload += " raw bytes";

    DataMessage m;
    m.set_topic("/scan");
    m.set_payload(payload);

    std::string bytes;
    ASSERT_TRUE(m.SerializeToString(&bytes));

    DataMessage out;
    ASSERT_TRUE(out.ParseFromString(bytes));
    EXPECT_EQ(out.topic(), "/scan");
    EXPECT_EQ(out.payload(), payload);
}
