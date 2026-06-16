#include "core/node.h"
#include "common/logger.h"
#include "messages.pb.h"

#include <chrono>
#include <string>
#include <thread>

// 用法:discovery_demo pub   或   discovery_demo sub
// 起两个进程(一 pub 一 sub),它们会通过 UDP 多播互相发现并打印 MATCH。
int main(int argc, char** argv) {
    std::string role = (argc > 1) ? argv[1] : "pub";
    mm::Node node("discovery_demo_" + role);

    node.discovery().on_match([](const mm::MatchInfo& m) {
        LOG_INFO("MATCH: local(kind={},topic={}) <-> remote(kind={},topic={}) @ {}:{} pid={}",
                 static_cast<int>(m.local.kind()), m.local.topic(),
                 static_cast<int>(m.remote.kind()), m.remote.topic(),
                 m.remote_locator.ip(), m.remote_locator.port(),
                 m.remote_participant_id);
    });

    if (role == "pub") {
        node.create_publisher<mm::StringMsg>("/chatter");
    } else {
        node.create_subscriber<mm::StringMsg>("/chatter", [](const mm::StringMsg&) {});
    }

    LOG_INFO("{} running, waiting for discovery... (Ctrl-C to quit)", role);
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}
