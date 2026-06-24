#include "core/node.h"
#include "common/qos.h"
#include "messages.pb.h"

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

using namespace mm;
using namespace std::chrono_literals;

TEST(RpcPubSub, ClientCallsServiceAcrossTwoNodes) {
    Qos qos;
    qos.reliability = Qos::Reliability::RELIABLE;

    Node server("rpc_server");
    Node client_node("rpc_client");
    server.discovery().set_timing(80ms, 5000ms);
    client_node.discovery().set_timing(80ms, 5000ms);

    auto service = server.create_service<mm::StringMsg, mm::StringMsg>(
        "/rpc_e2e_echo", [](const mm::StringMsg& req) {
            mm::StringMsg resp;
            resp.set_data("echo: " + req.data());
            return resp;
        },
        qos);
    auto client = client_node.create_client<mm::StringMsg, mm::StringMsg>(
        "/rpc_e2e_echo", qos);

    mm::StringMsg req;
    req.set_data("hello");

    std::optional<mm::StringMsg> resp;
    for (int i = 0; i < 80 && !resp.has_value(); ++i) {
        resp = client->call(req, 150ms);
        if (!resp.has_value()) std::this_thread::sleep_for(50ms);
    }

    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->data(), "echo: hello");
}
