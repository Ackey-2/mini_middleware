#include "cli/topic_commands.h"
#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>

#include <sstream>

using namespace mm;

TEST(CliTopicList, PrintsPublisherEndpoint) {
    Node node("topic_list_source");
    auto pub = node.create_publisher<mm::StringMsg>("/cli_list_chatter");

    std::ostringstream out;
    int code = run_topic_list(node.discovery(), out);

    EXPECT_EQ(code, 0);
    EXPECT_NE(out.str().find("/cli_list_chatter"), std::string::npos);
    EXPECT_NE(out.str().find("PUBLISHER"), std::string::npos);
}
