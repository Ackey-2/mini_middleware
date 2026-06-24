#include "core/node.h"
#include "common/qos.h"
#include "messages.pb.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

int main(int argc, char** argv) {
    std::string role = (argc > 1) ? argv[1] : "client";

    mm::Qos qos;
    qos.reliability = mm::Qos::Reliability::RELIABLE;

    mm::Node node("rpc_" + role);
    node.discovery().set_timing(200ms, 5000ms);

    if (role == "server") {
        auto service = node.create_service<mm::StringMsg, mm::StringMsg>(
            "/echo",
            [](const mm::StringMsg& req) {
                mm::StringMsg resp;
                resp.set_data("echo: " + req.data());
                return resp;
            },
            qos);
        std::cout << "[server] /echo service ready" << std::endl;
        while (true) std::this_thread::sleep_for(1s);
    }

    auto client = node.create_client<mm::StringMsg, mm::StringMsg>("/echo", qos);
    std::cout << "[client] calling /echo ..." << std::endl;
    for (int n = 0;; ++n) {
        mm::StringMsg req;
        req.set_data("msg #" + std::to_string(n));

        auto resp = client->call(req, 1000ms);
        if (resp) {
            std::cout << "[client] recv: " << resp->data() << std::endl;
        } else {
            std::cout << "[client] timeout" << std::endl;
        }
        std::this_thread::sleep_for(1s);
    }
}
