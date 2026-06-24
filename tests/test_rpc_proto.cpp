#include "rpc.pb.h"

#include <gtest/gtest.h>

TEST(RpcProto, RequestAndReplyRoundTrip) {
    mm::RpcRequest req;
    req.set_request_id(42);
    req.set_reply_topic("/_mm/rpc/echo/reply/client1");
    req.set_payload("hello");

    std::string bytes;
    ASSERT_TRUE(req.SerializeToString(&bytes));

    mm::RpcRequest parsed;
    ASSERT_TRUE(parsed.ParseFromString(bytes));
    EXPECT_EQ(parsed.request_id(), 42);
    EXPECT_EQ(parsed.reply_topic(), "/_mm/rpc/echo/reply/client1");
    EXPECT_EQ(parsed.payload(), "hello");

    mm::RpcReply reply;
    reply.set_request_id(42);
    reply.set_ok(true);
    reply.set_payload("world");

    bytes.clear();
    ASSERT_TRUE(reply.SerializeToString(&bytes));

    mm::RpcReply parsed_reply;
    ASSERT_TRUE(parsed_reply.ParseFromString(bytes));
    EXPECT_EQ(parsed_reply.request_id(), 42);
    EXPECT_TRUE(parsed_reply.ok());
    EXPECT_EQ(parsed_reply.payload(), "world");
}
