#include "cli/message_format.h"
#include "messages.pb.h"

#include <gtest/gtest.h>

#include <string>

using namespace mm;

TEST(MessageFormat, FormatsStringMsg) {
    StringMsg msg;
    msg.set_data("hello");

    std::string bytes;
    ASSERT_TRUE(msg.SerializeToString(&bytes));

    auto result = format_message("mm.StringMsg", bytes);

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.text, "data: hello");
}

TEST(MessageFormat, FormatsPoint3D) {
    Point3D msg;
    msg.set_x(1.0F);
    msg.set_y(2.0F);
    msg.set_z(3.5F);

    std::string bytes;
    ASSERT_TRUE(msg.SerializeToString(&bytes));

    auto result = format_message("mm.Point3D", bytes);

    EXPECT_TRUE(result.ok);
    EXPECT_NE(result.text.find("x: 1"), std::string::npos);
    EXPECT_NE(result.text.find("z: 3.5"), std::string::npos);
}

TEST(MessageFormat, RejectsUnsupportedType) {
    auto result = format_message("mm.Unknown", "");

    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("unsupported"), std::string::npos);
}
