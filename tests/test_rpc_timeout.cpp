#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>
#include <chrono>

using namespace mm;
using namespace std::chrono_literals;

TEST(RpcTimeout, ClientReturnsEmptyOptionalWithoutService) {
    Node node("rpc_timeout");
    auto client = node.create_client<mm::StringMsg, mm::StringMsg>("/missing");

    mm::StringMsg req;
    req.set_data("hello");

    auto resp = client->call(req, 50ms);

    EXPECT_FALSE(resp.has_value());
}
