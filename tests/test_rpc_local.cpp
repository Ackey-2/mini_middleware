#include "core/node.h"
#include "messages.pb.h"

#include <gtest/gtest.h>
#include <chrono>

using namespace mm;
using namespace std::chrono_literals;

TEST(RpcLocal, ClientCallsServiceInSameNode) {
    Node node("rpc_local");

    auto service = node.create_service<mm::StringMsg, mm::StringMsg>(
        "/echo", [](const mm::StringMsg& req) {
            mm::StringMsg resp;
            resp.set_data("echo: " + req.data());
            return resp;
        });
    auto client = node.create_client<mm::StringMsg, mm::StringMsg>("/echo");

    mm::StringMsg req;
    req.set_data("hello");

    auto resp = client->call(req, 500ms);

    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->data(), "echo: hello");
}
