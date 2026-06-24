#include "core/rpc_topics.h"

#include <gtest/gtest.h>

using namespace mm;

TEST(RpcTopics, BuildsStableRequestTopic) {
    EXPECT_EQ(rpc_request_topic("/echo"), "/_mm/rpc/echo/request");
}

TEST(RpcTopics, SanitizesServiceNameForInternalTopics) {
    EXPECT_EQ(rpc_request_topic("/robot/echo service"), "/_mm/rpc/robot_echo_service/request");
}

TEST(RpcTopics, BuildsReplyTopicWithClientId) {
    EXPECT_EQ(rpc_reply_topic("/echo", "client.1"), "/_mm/rpc/echo/reply/client_1");
}
